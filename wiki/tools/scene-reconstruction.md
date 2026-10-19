# Scene reconstruction

[Wiki home](../README.md) · [Full contents](../SUMMARY.md)

## Contents

- [Recording a scan](#recording-a-scan)
- [Output artifacts](#output-artifacts)
- [Replaying a capture](#replaying-a-capture)
- [Calibration files](#calibration-files)
- [Coordinate conventions](#coordinate-conventions)
- [Board mounting calibration](#board-mounting-calibration)
- [Calibrated projection rays](#calibrated-projection-rays)
- [Approximate mode](#approximate-mode)
- [Reconstruction pipeline](#reconstruction-pipeline)
- [Accuracy limits](#accuracy-limits)

The reconstruction script records the existing VL9F/IMUF USB stream. It produces
an animated **depth video**, not an RGB video: this board's camera stream contains
lidar ranging data rather than colour images.

Close `USB_Debug.py` before recording; only one process should own the serial port.
Dependencies are NumPy, pyserial, Matplotlib and Pillow, already present in this
workspace's Python environment. Open3D and SciPy are not required.

## Recording a scan

With uncertain IMU/lidar axes, record slow rotations about at least two axes while
viewing a static scene with depth structure (corners/furniture, not just a flat
wall). The following records five seconds after both streams arrive, tries to
estimate the mounting rotation, and reconstructs the scene:

```bash
python3 test_scripts/Reconstruct_Scene.py --port /dev/ttyACM1 --duration 5 \
  --estimate-mounting --output scans/scan01 --preview
```

Mounting estimation compares unseeded lidar ICP rotations with SFLP relative
rotations and solves the rotation equation A X = X B. It rejects insufficient
motion or inconsistent fits. It cannot guarantee correctness on ambiguous scenes,
and it cannot infer mounting translation from unconstrained handheld motion.
Translation defaults to zero unless supplied in a calibration JSON. Inspect the
estimated rotation and validate it with known physical motion before trusting it.

## Output artifacts

Output files:

- `usb_stream.bin`: original received USB bytes.
- `capture.npz`: depth payloads/sequence numbers, all IMU readings (MCU timestamp,
  acc mg, gyro mdps, quaternion xyzw), and host arrival times. Load without pickle.
- `depth_video.gif`: depth visualization; maximum colour scale is 4000 mm.
- `estimated_calibration.json`: rotation fit, when estimation succeeds.
- `scene.ply`: reconstructed XYZ point cloud in metres, viewable in point-cloud tools.
- `poses.npz`: accepted `world_from_lidar` 4×4 transforms and their timestamps.
- `report.json`: accepted/rejected frames, ICP overlap/RMSE, calibration assumptions.

## Replaying a capture

The output directory must be empty. If rotation estimation fails, the recording
and depth GIF remain available. Reprocess without reconnecting or recapturing:

```bash
python3 test_scripts/Reconstruct_Scene.py --input scans/scan01/capture.npz \
  --calibration mounting.json --output scans/reprocessed --preview
```

## Calibration files

A calibration file uses this schema (identity below is an example, not a measured
calibration for this board):

```json
{
  "rotation_lidar_to_imu": [[1, 0, 0], [0, 1, 0], [0, 0, 1]],
  "translation_lidar_to_imu_m": [0, 0, 0],
  "horizontal_fov_deg": 55,
  "vertical_fov_deg": 42,
  "invert_sflp_quaternion": false,
  "mounting_calibrated": true
}
```

## Coordinate conventions

The transform is `point_imu = rotation_lidar_to_imu @ point_lidar + translation`.
Translation is the lidar origin's position relative to the IMU origin, expressed
in **IMU axes**, in metres. Both coordinate systems must be right-handed. The
nominal lidar projection uses X toward increasing columns, Y toward increasing
rows (down), and Z forward. The expected SFLP convention is IMU-to-world xyzw;
`invert_sflp_quaternion` transposes that rotation if your convention is reversed.

The provided PCB locations, IMU `(10, 10)` mm and lidar `(33.4, -10)` mm, imply a
PCB displacement `(23.4, -20)` mm. This is not yet an IMU-coordinate translation:
its axes must first be confirmed. Do not infer an axis inversion from placement
alone. A 180° Z rotation maps X and Y to their negatives while preserving Z;
negating all three axes is a reflection, not a valid mounting rotation.

## Board mounting calibration

`test_scripts/mounting.json` now includes the rotation inferred from the reported
static-gravity observations: upright +IMU X, clockwise 90° (viewed from behind)
+IMU Y, pointing the lidar upwards −IMU Z. With lidar X right, Y down, Z forward,
the mapping is lidar X → −IMU Y, lidar Y → −IMU X, lidar Z → −IMU Z. Its matrix is
right-handed. This assumes static measurements with the displayed image aligned
to those lidar image axes. Translation is explicitly provisional zero until the
PCB-to-IMU axes and Z offset are known; nominal FoV rays remain approximate.

Use that rotation without estimating a new one:

```bash
python3 test_scripts/Reconstruct_Scene.py --port /dev/ttyACM1 --duration 5 \
  --calibration test_scripts/mounting.json --output scans/scan02 --preview
```

## Calibrated projection rays

If calibrated per-zone rays are available, add `"ray_directions_npy": "rays.npy"`.
That file must contain an H×W×3 array for the **uncropped raw grid**, in the same
zone order as the received depth, with directions expressed in lidar axes. Rays
are normalized by the script, and crop metadata selects the corresponding subset.
A size mismatch is rejected. Otherwise the script uses an approximate pinhole
ray model from the nominal 55°×42° FoV, documented in
[ST's datasheet](https://www.st.com/resource/en/datasheet/vl53l9cx.pdf).

## Approximate mode

For a deliberately approximate first experiment, `--approximate` assumes aligned
IMU/lidar axes and zero offset. It should not be used to interpret unknown mounting
as calibrated geometry. Configuration can be refined and the same capture replayed.

## Reconstruction pipeline

Reconstruction converts the raw **radial** range to metres along normalized rays.
It applies crop/binning metadata, rejects faulted frames and ranges outside
0.05–8.8 m, and uses SFLP (the IMU's fused acc/gyro orientation) as a rotation prior.
Trimmed point-to-point ICP estimates translation and refines rotation against the
previous accepted frame. Poor overlap, residuals or large corrections are rejected.
Accepted frames are merged into 2 cm voxels (`--voxel` changes this); reconstruction
selects up to 10 depth frames/s (`--fps` changes this) while recording retains all
packets. The IMU lever-arm offset contributes to predicted lidar displacement.

## Accuracy limits

Accuracy limits:

- There is no shared depth/IMU timestamp in VL9F. Association uses USB arrival
  times, which include batching and transport latency. Move slowly.
- SFLP supplies relative attitude without a compass; yaw and ICP translation can
  drift. Accelerometer readings are saved, not double-integrated for position.
- Raw depth has not received ST's calibration/confidence/postprocessing pipeline.
- ICP cannot reliably determine motion in every scene, particularly flat,
  repetitive or moving scenes. There is no loop closure, pose-graph optimization,
  or guaranteed SLAM accuracy. A PLY point cloud is produced, not a watertight mesh.

---

[Wiki home](../README.md) · [Full contents](../SUMMARY.md)
