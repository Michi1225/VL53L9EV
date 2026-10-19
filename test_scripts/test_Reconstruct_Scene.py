import json
from pathlib import Path
import tempfile
import unittest

import numpy as np
import Reconstruct_Scene as scene
from USB_Debug import Packet


## @brief Build a synthetic depth packet with valid metadata for reconstruction tests.
def depth_packet(seq=0):
    metadata = bytearray(100)
    metadata[58:60] = (2 << 6).to_bytes(2, 'little')
    distances = np.full(54*42, 1500, dtype='<u2')
    # A mildly varied surface avoids a completely uniform point distribution.
    distances += np.arange(54*42, dtype='<u2') % 101
    return Packet(seq, 100, 149, distances.tobytes() + bytes(54*42*4 + 54*42//2) + metadata)


class ReconstructionTests(unittest.TestCase):
    ## @brief Verify capture ignores profiler and parameter packets.
    def test_capture_ignores_profiler_and_parameter_packets(self):
        import sys
        import types
        from unittest.mock import patch
        from USB_Debug import HEADER, MAGIC, IMU_MAGIC, IMU_PAYLOAD, PARAM_MAGIC
        from test_thread_profiler import wire_profile

        imu = IMU_PAYLOAD.pack(1000, *([1.0] * 10))
        wire = HEADER.pack(PARAM_MAGIC, 0, 2, 1, 0) + b'{}' + wire_profile()
        wire += HEADER.pack(MAGIC, 0, 4, 2, 2) + bytes(4)
        wire += HEADER.pack(IMU_MAGIC, 0, len(imu), 10, 1) + imu

        class Device:
            ## @brief Initialize the simulated transport state used by this test.
            def __init__(self):
                self.buffer = bytearray(wire)
            ## @brief Return the simulated transport when entering its context manager.
            def __enter__(self):
                return self
            ## @brief Finish the simulated transport context without suppressing test failures.
            def __exit__(self, *args):
                pass
            ## @brief Report the input currently available from the simulated serial transport.
            @property
            def in_waiting(self):
                return len(self.buffer)
            ## @brief Return the next bytes supplied by the simulated input stream.
            def read(self, count):
                data = bytes(self.buffer[:count])
                del self.buffer[:count]
                return data

        serial = types.SimpleNamespace(Serial=lambda *args, **kwargs: Device(),
                                       SerialException=OSError)
        with tempfile.TemporaryDirectory() as temp, patch.dict(sys.modules, serial=serial):
            output = Path(temp)
            scene.capture('/dev/test', 0, output)
            with np.load(output / 'capture.npz') as capture:
                self.assertEqual(len(capture['tof_sequences']), 1)
                self.assertEqual(capture['imu_values'].shape, (1, 11))
            self.assertEqual((output / 'usb_stream.bin').read_bytes(), wire)

    ## @brief Verify observed mounting gravity directions.
    def test_observed_mounting_gravity_directions(self):
        config = scene.load_calibration(Path(__file__).with_name('mounting.json'))
        rotation = config['rotation_lidar_to_imu']
        # Upright up, clockwise 90-degree up, and optical axis pointing up.
        np.testing.assert_allclose(rotation @ [0, -1, 0], [1, 0, 0])
        np.testing.assert_allclose(rotation @ [-1, 0, 0], [0, 1, 0])
        np.testing.assert_allclose(rotation @ [0, 0, 1], [0, 0, -1])
        self.assertAlmostEqual(np.linalg.det(rotation), 1)

    ## @brief Verify radial points keep range.
    def test_radial_points_keep_range(self):
        points = scene.frame_points(depth_packet(), scene.load_calibration(None, True))
        np.testing.assert_allclose(np.linalg.norm(points, axis=1),
                                   (1500 + np.arange(54*42) % 101)*0.001)
        self.assertGreater(points[-1, 0], points[0, 0])
        self.assertGreater(points[-1, 1], points[0, 1])

    ## @brief Verify quaternion sign interpolation.
    def test_quaternion_sign_interpolation(self):
        imu = np.zeros((2, 11))
        imu[0, 10], imu[1, 10] = 1, -1
        np.testing.assert_allclose(scene.orientation_at(0.05, np.array([0, 0.1]), imu), np.eye(3))
        with self.assertRaises(ValueError):
            scene.orientation_at(1, np.array([0, 0.1]), imu)

    ## @brief Verify icp known translation and rotation.
    def test_icp_known_translation_and_rotation(self):
        source = np.random.default_rng(7).uniform(-0.5, 0.5, (300, 3))
        rotation = scene.quaternion_matrix([0, 0, np.sin(0.02), np.cos(0.02)])
        translation = np.array([0.03, -0.02, 0.01])
        target = source @ rotation.T + translation
        pose, fitness, error = scene.icp(source, target, np.eye(4), 0.2, 30)
        self.assertGreater(fitness, 0.95)
        self.assertLess(error, 1e-5)
        np.testing.assert_allclose(pose[:3, :3], rotation, atol=1e-5)
        np.testing.assert_allclose(pose[:3, 3], translation, atol=1e-5)

    ## @brief Verify voxels average duplicate points.
    def test_voxels_average_duplicate_points(self):
        result = scene.voxel_downsample(np.array([[0.01, 0, 0], [0.03, 0, 0]]), 0.1)
        np.testing.assert_allclose(result, [[0.02, 0, 0]])

    ## @brief Verify mounting rotation from independent motion.
    def test_mounting_rotation_from_independent_motion(self):
        mounting = scene.quaternion_matrix([0, 0, 1, 0])
        motions = [scene.quaternion_matrix(q) for q in
                   ([0.1, 0, 0, 1], [0, 0.15, 0, 1], [0.1, 0.1, 0.05, 1])]
        pairs = [(a, mounting.T @ a @ mounting) for a in motions]
        rotation, residuals = scene.solve_mounting_rotation(pairs)
        np.testing.assert_allclose(rotation, mounting, atol=1e-6)
        self.assertLess(max(residuals), 0.001)
        with self.assertRaises(ValueError):
            scene.solve_mounting_rotation([(np.eye(3), np.eye(3))]*3)

    ## @brief Verify calibration rejects reflection.
    def test_calibration_rejects_reflection(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / 'calibration.json'
            path.write_text(json.dumps({'rotation_lidar_to_imu': np.diag([-1, 1, 1]).tolist(),
                                        'translation_lidar_to_imu_m': [0, 0, 0]}))
            with self.assertRaises(ValueError):
                scene.load_calibration(path)

    ## @brief Verify record replay artifacts.
    def test_record_replay_artifacts(self):
        with tempfile.TemporaryDirectory() as temp:
            output = Path(temp)
            tof = [(0.01, depth_packet(1)), (0.12, depth_packet(2))]
            imu = [(t, [t*1000, 0, 0, 1000, 0, 0, 0, 0, 0, 0, 1]) for t in [0, 0.1, 0.2]]
            scene.save_capture(output / 'capture.npz', tof, imu)
            with np.load(output / 'capture.npz', allow_pickle=False) as data:
                points = scene.reconstruct(data, scene.load_calibration(None, True), output)
                scene.depth_video(data, output)
            self.assertGreater(len(points), 100)
            self.assertTrue((output / 'scene.ply').exists())
            self.assertTrue((output / 'depth_video.gif').exists())
            report = json.loads((output / 'report.json').read_text())
            self.assertEqual(report['accepted_frames'], 2)
            with np.load(output / 'poses.npz') as poses:
                np.testing.assert_allclose(poses['world_from_lidar'][1], np.eye(4), atol=1e-5)


if __name__ == '__main__':
    unittest.main()
