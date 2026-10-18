#!/usr/bin/env python3
"""Record VL9F/IMUF USB packets and reconstruct a scene with IMU-seeded ICP.

Dependencies: numpy, pyserial, matplotlib, Pillow. No Open3D/SciPy required.
See RECONSTRUCTION.md for calibration conventions and accuracy limitations.
"""
import argparse
import json
from pathlib import Path
import time

import numpy as np
from USB_Debug import Packet, PacketParser, MAGIC, IMU_MAGIC, IMU_PAYLOAD, RAW_GRID_SIZES, decode_depth


def load_calibration(path, approximate=False):
    if path is None:
        if not approximate:
            raise ValueError("Supply --calibration FILE or explicitly select --approximate")
        config = {}
        root = Path.cwd()
    else:
        root = Path(path).resolve().parent
        config = json.loads(Path(path).read_text())
        for key in ("rotation_lidar_to_imu", "translation_lidar_to_imu_m"):
            if key not in config:
                raise ValueError(f"Calibration is missing {key}")
        config.setdefault('mounting_calibrated', True)
    rotation = np.asarray(config.get("rotation_lidar_to_imu", np.eye(3)), dtype=float)
    translation = np.asarray(config.get("translation_lidar_to_imu_m", [0, 0, 0]), dtype=float)
    if rotation.shape != (3, 3) or not np.all(np.isfinite(rotation)) or not np.allclose(rotation.T @ rotation, np.eye(3), atol=1e-4) or not np.isclose(np.linalg.det(rotation), 1, atol=1e-4):
        raise ValueError("Mounting rotation must be a finite right-handed orthonormal 3x3 matrix")
    if translation.shape != (3,) or not np.all(np.isfinite(translation)):
        raise ValueError("Mounting translation must be three finite values in metres")
    config["rotation_lidar_to_imu"] = rotation
    config["translation_lidar_to_imu_m"] = translation
    if "ray_directions_npy" in config:
        rays = np.load(root / config["ray_directions_npy"], allow_pickle=False)
        if rays.ndim != 3 or rays.shape[2] != 3 or not np.all(np.isfinite(rays)) or np.any(np.linalg.norm(rays, axis=2) == 0):
            raise ValueError("Ray directions must be a finite H x W x 3 array with nonzero vectors")
        config["rays"] = rays / np.linalg.norm(rays, axis=2, keepdims=True)
        config["ray_directions_npy"] = str((root / config["ray_directions_npy"]).resolve())
    return config


def quaternion_matrix(q):
    q = np.asarray(q, dtype=float)
    norm = np.linalg.norm(q)
    if not np.isfinite(norm) or norm < 1e-8:
        raise ValueError("Invalid IMU quaternion")
    x, y, z, w = q / norm
    return np.array([[1-2*(y*y+z*z), 2*(x*y-z*w), 2*(x*z+y*w)],
                     [2*(x*y+z*w), 1-2*(x*x+z*z), 2*(y*z-x*w)],
                     [2*(x*z-y*w), 2*(y*z+x*w), 1-2*(x*x+y*y)]])


def orientation_at(timestamp, imu_times, imu_values, max_gap=0.15):
    """Interpolate sign-consistent xyzw quaternions using host arrival times."""
    right = int(np.searchsorted(imu_times, timestamp))
    left = max(0, right - 1)
    right = min(right, len(imu_times) - 1)
    if min(abs(timestamp-imu_times[left]), abs(timestamp-imu_times[right])) > max_gap:
        raise ValueError("No nearby IMU sample")
    if left != right and imu_times[right] - imu_times[left] > max_gap:
        raise ValueError("IMU timing gap")
    a, b = imu_values[left, 7:11].copy(), imu_values[right, 7:11].copy()
    if np.dot(a, b) < 0:
        b = -b
    span = imu_times[right] - imu_times[left]
    fraction = np.clip((timestamp-imu_times[left])/span, 0, 1) if span else 0
    return quaternion_matrix((1-fraction)*a + fraction*b)


def frame_points(packet, calibration, min_depth=0.05, max_depth=8.8):
    grid, binning = decode_depth(packet)
    metadata = packet.payload[-100:]
    if int.from_bytes(metadata[60:62], 'little') or metadata[62] & 0xC0:
        raise ValueError("Sensor fault is recorded in frame metadata")
    raw_width, raw_height = RAW_GRID_SIZES[binning]
    crop = int.from_bytes(metadata[72:76], 'little')
    x0, y0 = ((crop >> 12) & 63, (crop >> 18) & 63) if crop & (1 << 24) else (0, 0)
    height, width = grid.shape
    if "rays" in calibration:
        all_rays = calibration["rays"]
        if all_rays.shape[:2] != (raw_height, raw_width):
            raise ValueError("Calibrated rays do not match this frame's raw zone grid")
        rays = all_rays[y0:y0+height, x0:x0+width]
    else:
        hfov = float(calibration.get("horizontal_fov_deg", 55))
        vfov = float(calibration.get("vertical_fov_deg", 42))
        if not (0 < hfov < 180 and 0 < vfov < 180):
            raise ValueError("FoV angles must be between 0 and 180 degrees")
        x = ((np.arange(width)+x0+0.5)/raw_width*2-1)*np.tan(np.deg2rad(hfov/2))
        y = ((np.arange(height)+y0+0.5)/raw_height*2-1)*np.tan(np.deg2rad(vfov/2))
        xx, yy = np.meshgrid(x, y)
        rays = np.stack((xx, yy, np.ones_like(xx)), axis=-1)
        rays /= np.linalg.norm(rays, axis=2, keepdims=True)
    depth = grid.filled(0) * 0.001  # Raw radial range, not optical-axis Z.
    valid = np.isfinite(depth) & (depth >= min_depth) & (depth <= max_depth)
    return rays[valid] * depth[valid, None]


def nearest(source, target):
    indices, distances = [], []
    for start in range(0, len(source), 128):
        block = source[start:start+128]
        squared = np.sum((block[:, None, :] - target[None, :, :])**2, axis=2)
        index = np.argmin(squared, axis=1)
        indices.append(index)
        distances.append(np.sqrt(squared[np.arange(len(block)), index]))
    return np.concatenate(indices), np.concatenate(distances)


def icp(source, target, initial, max_distance=0.15, iterations=12):
    """Trimmed point-to-point ICP; returns pose, inlier fraction and RMSE."""
    pose = initial.copy()
    for _ in range(iterations):
        moved = source @ pose[:3, :3].T + pose[:3, 3]
        index, distance = nearest(moved, target)
        mask = distance < max_distance
        if np.count_nonzero(mask) < 20:
            return pose, 0.0, float('inf')
        cutoff = np.quantile(distance[mask], 0.8)
        mask &= distance <= cutoff
        a, b = moved[mask], target[index[mask]]
        ca, cb = a.mean(axis=0), b.mean(axis=0)
        u, _, vt = np.linalg.svd((a-ca).T @ (b-cb))
        rotation = vt.T @ u.T
        if np.linalg.det(rotation) < 0:
            vt[-1] *= -1
            rotation = vt.T @ u.T
        delta = np.eye(4)
        delta[:3, :3], delta[:3, 3] = rotation, cb - rotation @ ca
        pose = delta @ pose
        if np.linalg.norm(delta[:3, 3]) < 1e-4 and np.linalg.norm(rotation-np.eye(3)) < 1e-4:
            break
    moved = source @ pose[:3, :3].T + pose[:3, 3]
    _, distance = nearest(moved, target)
    inliers = distance < max_distance
    return pose, float(inliers.mean()), float(np.sqrt(np.mean(distance[inliers]**2))) if inliers.any() else float('inf')


def voxel_downsample(points, size):
    keys = np.floor(points / size).astype(np.int64)
    _, inverse, counts = np.unique(keys, axis=0, return_inverse=True, return_counts=True)
    sums = np.zeros((len(counts), 3))
    np.add.at(sums, inverse, points)
    return sums / counts[:, None]


def solve_mounting_rotation(pairs):
    """Solve A X = X B for lidar-to-IMU rotation from independent rotations."""
    if len(pairs) < 3:
        raise ValueError('Mounting estimation needs at least three good rotation pairs')
    system = np.concatenate([np.kron(np.eye(3), a)-np.kron(b.T, np.eye(3)) for a, b in pairs])
    _, singular, vt = np.linalg.svd(system, full_matrices=False)
    if singular[-2] < 0.03:
        raise ValueError('Insufficient rotation excitation; rotate about at least two different axes')
    raw = vt[-1].reshape(3, 3, order='F')
    if np.linalg.det(raw) < 0:
        raw = -raw
    u, _, v = np.linalg.svd(raw)
    rotation = u @ v
    residuals = [np.rad2deg(np.arccos(np.clip((np.trace((a @ rotation).T @ (rotation @ b))-1)/2, -1, 1))) for a, b in pairs]
    if np.sqrt(np.mean(np.square(residuals))) > 2:
        raise ValueError('Mounting rotations are inconsistent; improve overlap, motion and timing')
    return rotation, residuals


def estimate_mounting(data, calibration, fps=10, max_distance=0.15):
    pairs = []
    previous = None
    last_time = -float('inf')
    for i, timestamp in enumerate(data['tof_times']):
        if timestamp-last_time < 1/fps:
            continue
        last_time = timestamp
        try:
            points = frame_points(packet_at(data, i), calibration)
            if len(points) < 40:
                continue
            body = orientation_at(timestamp, data['imu_times'], data['imu_values'])
            if calibration.get('invert_sflp_quaternion', False):
                body = body.T
            if previous is not None:
                old_points, old_body = previous
                a = old_body.T @ body
                angle = np.rad2deg(np.arccos(np.clip((np.trace(a)-1)/2, -1, 1)))
                if 1 <= angle <= 15:
                    sample = points[np.linspace(0, len(points)-1, min(500, len(points)), dtype=int)]
                    transform, fitness, error = icp(sample, old_points, np.eye(4), max_distance)
                    if fitness >= 0.65 and error < 0.025:
                        pairs.append((a, transform[:3, :3]))
            previous = (points, body)
        except ValueError:
            continue
    rotation, residuals = solve_mounting_rotation(pairs)
    calibration['rotation_lidar_to_imu'] = rotation
    calibration['rotation_estimated'] = True
    calibration['rotation_fit_rms_deg'] = float(np.sqrt(np.mean(np.square(residuals))))
    calibration['rotation_fit_pairs'] = len(pairs)
    print(f'Estimated mounting rotation from {len(pairs)} pairs; RMS {calibration["rotation_fit_rms_deg"]:.2f} degrees')
    return calibration


def save_capture(path, tof, imu):
    max_size = max((len(packet.payload) for _, packet in tof), default=0)
    payloads = np.zeros((len(tof), max_size), dtype=np.uint8)
    for i, (_, packet) in enumerate(tof):
        payloads[i, :len(packet.payload)] = np.frombuffer(packet.payload, dtype=np.uint8)
    np.savez_compressed(path, tof_times=np.array([t for t, _ in tof]),
        tof_payloads=payloads, tof_lengths=np.array([len(p.payload) for _, p in tof]),
        tof_sequences=np.array([p.sequence for _, p in tof], dtype=np.uint32),
        tof_widths=np.array([p.width for _, p in tof]), tof_heights=np.array([p.height for _, p in tof]),
        imu_times=np.array([t for t, _ in imu]), imu_values=np.array([v for _, v in imu]).reshape(-1, 11))


def capture(port, duration, output, ready_timeout=15):
    import serial
    parser = PacketParser()
    tof, imu = [], []
    started = time.monotonic()
    recording_start = None
    print(f"Opening {port}; waiting for depth and IMU packets. Close USB_Debug.py first.")
    try:
        with serial.Serial(port, 115200, timeout=0.05) as device, (output / 'usb_stream.bin').open('wb') as raw:
            while True:
                now = time.monotonic()
                if recording_start is not None and now-recording_start >= duration:
                    break
                if recording_start is None and now-started > ready_timeout:
                    break
                data = device.read(1)
                if not data:
                    continue
                if device.in_waiting:
                    data += device.read(min(device.in_waiting, 65535))
                raw.write(data)
                arrival = time.monotonic()-started
                for packet in parser.feed(data):
                    if packet.kind == MAGIC:
                        tof.append((arrival, packet))
                    elif packet.kind == IMU_MAGIC:
                        imu.append((arrival, IMU_PAYLOAD.unpack(packet.payload)))
                if recording_start is None and tof and imu:
                    recording_start = time.monotonic()
                    print(f"Recording {duration:g} seconds; move slowly through a static scene.")
    except serial.SerialException as exc:
        print(f"USB disconnected: {exc}; saving the partial recording")
    finally:
        save_capture(output / 'capture.npz', tof, imu)
    print(f"Saved {len(tof)} depth frames, {len(imu)} IMU samples; discarded {parser.discarded} framing bytes.")
    if not tof or not imu:
        raise ValueError("Both depth and IMU streams are required; partial capture was saved")


def depth_video(data, output, fps=10, max_depth_mm=4000):
    from PIL import Image
    from matplotlib import colormaps
    frames = []
    frame_times = []
    last = -float('inf')
    cmap = colormaps['turbo_r']
    for i, timestamp in enumerate(data['tof_times']):
        if timestamp-last < 1/fps:
            continue
        packet = packet_at(data, i)
        try:
            grid, _ = decode_depth(packet)
        except ValueError:
            continue
        rgb = (cmap(np.clip(grid.filled(0)/max_depth_mm, 0, 1))[:, :, :3]*255).astype(np.uint8)
        rgb[np.ma.getmaskarray(grid)] = 70
        frames.append(Image.fromarray(rgb).resize((540, 420), getattr(Image, 'Resampling', Image).NEAREST))
        frame_times.append(float(timestamp))
        last = timestamp
    if frames:
        durations = [max(10, round(1000*(b-a))) for a, b in zip(frame_times, frame_times[1:])]
        durations.append(max(10, round(1000/fps)))
        frames[0].save(output / 'depth_video.gif', save_all=True, append_images=frames[1:],
                       duration=durations, loop=0)


def packet_at(data, i):
    payload = data['tof_payloads'][i, :int(data['tof_lengths'][i])].tobytes()
    return Packet(int(data['tof_sequences'][i]), int(data['tof_widths'][i]), int(data['tof_heights'][i]), payload)


def reconstruct(data, calibration, output, fps=10, max_distance=0.15, voxel=0.02):
    if len(data['imu_times']) < 2 or not len(data['tof_times']):
        raise ValueError("Need depth frames and at least two IMU samples")
    times, imu = data['imu_times'], data['imu_values']
    mounting = calibration['rotation_lidar_to_imu']
    offset = calibration['translation_lidar_to_imu_m']
    reference = None
    previous_prior = None
    previous_body = None
    last_pose = np.eye(4)
    target = None
    clouds, trajectory, report = [], [], []
    last_time = -float('inf')
    for i, timestamp in enumerate(data['tof_times']):
        if timestamp-last_time < 1/fps:
            continue
        last_time = timestamp
        try:
            source = frame_points(packet_at(data, i), calibration)
            if len(source) < 40:
                raise ValueError("Too few usable depth zones")
            orientation = orientation_at(timestamp, times, imu)
            if calibration.get('invert_sflp_quaternion', False):
                orientation = orientation.T
            if reference is None:
                reference = orientation
            body = reference.T @ orientation
            prior = body @ mounting
            guess = np.eye(4)
            if target is None:
                guess[:3, :3] = prior
                pose, fitness, rmse = guess, 1.0, 0.0
            else:
                guess[:3, :3] = last_pose[:3, :3] @ previous_prior.T @ prior
                guess[:3, 3] = last_pose[:3, 3] + (body-previous_body) @ offset
                sampled = source[np.linspace(0, len(source)-1, min(500, len(source)), dtype=int)]
                pose, fitness, rmse = icp(sampled, target, guess, max_distance)
                angle = np.rad2deg(np.arccos(np.clip((np.trace(guess[:3, :3].T @ pose[:3, :3])-1)/2, -1, 1)))
                if fitness < 0.3 or rmse > max_distance*0.6 or np.linalg.norm(pose[:3, 3]-guess[:3, 3]) > 0.4 or angle > 15:
                    raise ValueError(f"Rejected ICP: overlap={fitness:.2f}, rmse={rmse:.3f}m, rotation correction={angle:.1f}deg")
            world = source @ pose[:3, :3].T + pose[:3, 3]
            clouds.append(world)
            # Align against actual measured points. Voxel centroids would
            # introduce apparent motion even between identical depth frames.
            target = world
            last_pose, previous_prior, previous_body = pose, prior, body
            trajectory.append((timestamp, pose))
            report.append(dict(sequence=int(data['tof_sequences'][i]), time_s=float(timestamp), accepted=True, overlap=fitness, rmse_m=rmse))
        except ValueError as exc:
            report.append(dict(sequence=int(data['tof_sequences'][i]), time_s=float(timestamp), accepted=False, reason=str(exc)))
        if len(report) % 10 == 0:
            print(f"Reconstruction: {len(trajectory)}/{len(report)} selected frames accepted")
    if not clouds:
        raise ValueError("No frames could be reconstructed; inspect depth and calibration")
    scene = voxel_downsample(np.concatenate(clouds), voxel)
    with (output / 'scene.ply').open('w') as file:
        file.write(f"ply\nformat ascii 1.0\nelement vertex {len(scene)}\nproperty float x\nproperty float y\nproperty float z\nend_header\n")
        np.savetxt(file, scene, fmt='%.6f')
    np.savez_compressed(output / 'poses.npz', times=np.array([t for t, _ in trajectory]),
                        world_from_lidar=np.array([p for _, p in trajectory]))
    config = {k: v.tolist() if isinstance(v, np.ndarray) else v for k, v in calibration.items() if k != 'rays'}
    (output / 'report.json').write_text(json.dumps(dict(calibration=config, frames=report,
        points=len(scene), accepted_frames=len(trajectory), selected_frames=len(report),
        projection='Calibrated per-zone rays' if 'rays' in calibration else 'Approximate pinhole projection from FoV',
        mounting=('Estimated rotation; translation from configuration or assumed zero' if calibration.get('rotation_estimated') else
                  'User-supplied mounting calibration' if calibration.get('mounting_calibrated') else
                  'Provisional user-supplied rotation/offset' if calibration.get('rotation_source') else 'Assumed identity/zero offset'),
        timing='Host USB arrival time; no shared lidar/IMU hardware timestamps',
        method='SFLP rotation prior + trimmed point-to-point ICP; no loop closure'), indent=2))
    print(f"Saved scene.ply: {len(scene)} points from {len(trajectory)} frames")
    return scene


def preview(scene):
    import matplotlib.pyplot as plt
    points = scene[np.linspace(0, len(scene)-1, min(20000, len(scene)), dtype=int)]
    fig = plt.figure()
    ax = fig.add_subplot(projection='3d')
    ax.scatter(*points.T, c=points[:, 2], s=1, cmap='viridis')
    ax.set(xlabel='X (m)', ylabel='Y (m)', zlabel='Z (m)', title='Reconstructed scene')
    span = np.maximum(np.ptp(points, axis=0), 0.01)
    ax.set_box_aspect(span)
    plt.show()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', help='USB serial device, e.g. /dev/ttyACM1')
    parser.add_argument('--duration', type=float, default=5)
    parser.add_argument('--input', type=Path, help='Replay an existing capture.npz instead of recording')
    parser.add_argument('--output', type=Path, default=Path('tof_scan'))
    parser.add_argument('--calibration', type=Path)
    parser.add_argument('--approximate', action='store_true', help='Explicitly assume aligned axes and nominal FoV')
    parser.add_argument('--estimate-mounting', action='store_true',
                        help='Estimate mounting rotation from motion about two axes; translation defaults to zero')
    parser.add_argument('--fps', type=float, default=10, help='Offline video/reconstruction frame rate')
    parser.add_argument('--voxel', type=float, default=0.02, help='Map voxel size in metres')
    parser.add_argument('--icp-distance', type=float, default=0.15, help='Maximum correspondence distance in metres')
    parser.add_argument('--preview', action='store_true')
    args = parser.parse_args()
    numeric = (args.duration, args.fps, args.voxel, args.icp_distance)
    if not all(np.isfinite(value) and value > 0 for value in numeric):
        parser.error('Duration, FPS, voxel size and ICP distance must be finite and positive')
    if not args.input and not args.port:
        parser.error('--port is required when recording')
    try:
        calibration = load_calibration(args.calibration, args.approximate or args.estimate_mounting)
        if args.output.exists() and any(args.output.iterdir()):
            raise ValueError('Output directory is not empty; use a new --output path')
        args.output.mkdir(parents=True, exist_ok=True)
        if not args.input:
            capture(args.port, args.duration, args.output)
        source = args.input or args.output / 'capture.npz'
        with np.load(source, allow_pickle=False) as data:
            depth_video(data, args.output, args.fps)
            if args.estimate_mounting:
                calibration = estimate_mounting(data, calibration, args.fps, args.icp_distance)
                saved = {k: v.tolist() if isinstance(v, np.ndarray) else v for k, v in calibration.items() if k != 'rays'}
                (args.output / 'estimated_calibration.json').write_text(json.dumps(saved, indent=2))
            scene = reconstruct(data, calibration, args.output, args.fps, args.icp_distance, args.voxel)
        if args.preview:
            preview(scene)
    except (ValueError, OSError, json.JSONDecodeError) as exc:
        parser.exit(1, f'Error: {exc}\n')


if __name__ == '__main__':
    main()
