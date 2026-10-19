"""Exercise viewer update ownership, cancellation, activation, and recovery."""
import argparse
import os
from pathlib import Path
import struct
import tempfile
import threading
import time
import unittest
from unittest.mock import patch
import zlib

from Firmware_Update import UpdateWorker, fw
from test_firmware_update import image_fixture


class UpdateDevice:
    ## @brief Initialize a simulated two-slot updater shared by successive connections.
    def __init__(self, fallback=False):
        self.report = dict(result=0, flags=8, running=0, confirmed=0, pending=fw.NONE,
                           received=0, total=0, last_error=0, generation=1,
                           version_a=1, version_b=0, chunk_size=512)
        self.operations = []
        self.connections = []
        self.closed = 0
        self.data = bytearray()
        self.fallback = fallback
        self.after_finish = None

    ## @brief Open a simulated client while retaining device state across reset queries.
    def connect(self, **options):
        self.connections.append(options)
        return self

    ## @brief Record release of the update connection.
    def close(self):
        self.closed += 1

    ## @brief Validate uploaded chunks and emulate persistent status and trial boot outcomes.
    def request(self, operation, payload=b''):
        self.operations.append(operation)
        r = self.report
        if operation == fw.BEGIN:
            self.size, self.version, self.digest = struct.unpack('<II32s', payload)
            self.data.clear()
            r.update(flags=1, total=self.size, received=0)
        elif operation == fw.DATA:
            offset, crc = struct.unpack_from('<II', payload)
            assert offset == len(self.data) and zlib.crc32(payload[8:]) == crc
            self.data.extend(payload[8:])
            r['received'] = len(self.data)
        elif operation == fw.FINISH:
            assert len(self.data) == self.size and fw.validate_image(self.data) == self.digest
            r.update(flags=6, pending=1, total=0, version_b=self.version)
            if self.after_finish:
                self.after_finish()
        elif operation == fw.ABORT:
            r.update(flags=256, received=0, total=0)
        elif operation == fw.RESET:
            if r['pending'] != 1:
                raise RuntimeError('No staged image')
            r.update(pending=fw.NONE, running=0 if self.fallback else 1,
                     confirmed=0 if self.fallback else 1, flags=22 if self.fallback else 14,
                     last_error=8 if self.fallback else 0)
        elif operation == fw.CLEAR:
            r.update(flags=r['flags'] & ~0x1f0, last_error=0)
        return dict(r)


class ViewerUpdateWorkerTests(unittest.TestCase):
    ## @brief Prepare a valid image fixture and an isolated simulated update device.
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.image = Path(self.directory.name) / 'appli-signed.bin'
        self.image.write_bytes(image_fixture())
        self.device = UpdateDevice()

    ## @brief Run the actual uploader through a worker with a deterministic boot polling interval.
    def run_worker(self, worker):
        with patch.object(fw, 'Client', side_effect=self.device.connect), patch.object(worker.cancel, 'wait', return_value=False):
            worker.run()
        return worker.snapshot()

    ## @brief Verify progress, dedicated TCP routing, a single reset, and confirmed activation.
    def test_upload_confirms_boot_and_uses_update_port(self):
        worker = UpdateWorker('Ethernet', 'device', 51003, 'upload', image=self.image, version=42)
        snapshots = []
        progress = worker.progress

        ## @brief Record every acknowledged progress update before continuing the upload.
        def record(received, total, report):
            progress(received, total, report)
            snapshots.append(worker.snapshot())

        with patch.object(worker, 'progress', side_effect=record):
            state = self.run_worker(worker)
        self.assertEqual(state['outcome'], 'confirmed')
        self.assertEqual(state['report']['version_b'], 42)
        self.assertEqual([s['received'] for s in snapshots], [0, 512, 1024, 1536, 2048, 2048])
        self.assertTrue(all(c['tcp'] == 'device' and c['port'] == 51003 for c in self.device.connections))
        self.assertEqual(self.device.operations.count(fw.RESET), 1)
        self.assertEqual(self.device.closed, 2)

    ## @brief Verify that an unconfirmed candidate is reported as fallback rather than success.
    def test_fallback_warning_is_preserved(self):
        self.device.fallback = True
        state = self.run_worker(UpdateWorker('Ethernet', 'device', 50003, 'upload', image=self.image))
        self.assertEqual(state['outcome'], 'fallback')
        self.assertIn('Fallback warning', state['message'])
        self.assertEqual(state['report']['running'], 0)
        self.assertTrue(state['report']['flags'] & 16)

    ## @brief Verify that staging without reset remains distinct from confirmed boot success.
    def test_stage_without_reset(self):
        state = self.run_worker(UpdateWorker('USB', '/dev/test', 50003, 'upload', image=self.image, reset=False))
        self.assertEqual(state['outcome'], 'verified')
        self.assertEqual(state['report']['pending'], 1)
        self.assertNotIn(fw.RESET, self.device.operations)
        self.assertEqual(self.device.connections[0]['usb'], '/dev/test')

    ## @brief Verify that cancellation aborts only the unfinished upload after an acknowledged chunk.
    def test_cancel_upload_aborts_without_finishing_or_resetting(self):
        worker = UpdateWorker('USB', '/dev/test', 50003, 'upload', image=self.image)
        progress = worker.progress

        ## @brief Request cancellation after the first data chunk has been acknowledged.
        def cancel_after_chunk(received, total, report):
            progress(received, total, report)
            if received:
                worker.cancel.set()

        with patch.object(worker, 'progress', side_effect=cancel_after_chunk):
            state = self.run_worker(worker)
        self.assertEqual(state['outcome'], 'cancelled')
        self.assertEqual(self.device.operations.count(fw.ABORT), 1)
        self.assertNotIn(fw.FINISH, self.device.operations)
        self.assertNotIn(fw.RESET, self.device.operations)
        self.assertEqual(self.device.report['confirmed'], 0)

    ## @brief Verify cancellation after verification keeps the staged image and does not reset it.
    def test_cancel_after_finish_preserves_verified_image(self):
        worker = UpdateWorker('USB', '/dev/test', 50003, 'upload', image=self.image)
        self.device.after_finish = worker.cancel.set
        state = self.run_worker(worker)
        self.assertEqual(state['outcome'], 'verified')
        self.assertEqual(state['report']['pending'], 1)
        self.assertNotIn(fw.RESET, self.device.operations)
        self.assertNotIn(fw.ABORT, self.device.operations)

    ## @brief Verify corrupt files and development images cannot start flash programming.
    def test_invalid_images_never_begin(self):
        for development in (False, True):
            self.image.write_bytes(image_fixture() if development else b'corrupt')
            if development:
                self.image.with_suffix('.manifest.json').write_text('{"options":{"DEV_MODE":true}}')
            state = self.run_worker(UpdateWorker('USB', '/dev/test', 50003, 'upload', image=self.image))
            self.assertEqual(state['outcome'], 'error')
            self.assertNotIn(fw.BEGIN, self.device.operations)

    ## @brief Verify a lost data acknowledgement does not trigger a chunk retry or reset.
    def test_lost_acknowledgement_is_not_replayed(self):
        request = self.device.request

        ## @brief Simulate a lost acknowledgement after programming the first data chunk.
        def lose_ack(operation, payload=b''):
            report = request(operation, payload)
            if operation == fw.DATA:
                raise TimeoutError('Acknowledgement lost')
            return report

        with patch.object(self.device, 'request', side_effect=lose_ack):
            state = self.run_worker(UpdateWorker('USB', '/dev/test', 50003, 'upload', image=self.image))
        self.assertEqual(state['outcome'], 'error')
        self.assertEqual(self.device.operations.count(fw.DATA), 1)
        self.assertNotIn(fw.RESET, self.device.operations)
        self.assertIn('Read device status', state['message'])

    ## @brief Verify the USB stream exits before the updater opens the serial device.
    def test_usb_receiver_releases_port_before_updater_opens(self):
        class Receiver:
            stop = threading.Event()
            thread = threading.Thread(target=lambda: Receiver.stop.wait())
        receiver = Receiver()
        receiver.thread.start()
        self.addCleanup(receiver.stop.set)
        self.addCleanup(receiver.thread.join, 1)
        connect = self.device.connect

        ## @brief Assert exclusive serial ownership at the instant the updater connects.
        def exclusive_connect(**options):
            self.assertTrue(receiver.stop.is_set())
            self.assertFalse(receiver.thread.is_alive())
            return connect(**options)

        with patch.object(self.device, 'connect', side_effect=exclusive_connect):
            state = self.run_worker(UpdateWorker('USB', '/dev/test', 50003, 'status', receiver=receiver))
        self.assertEqual(state['outcome'], 'status')

    ## @brief Verify stopping boot monitoring leaves a single reset and an uncertain outcome.
    def test_cancel_boot_monitor_does_not_replay_reset(self):
        worker = UpdateWorker('USB', '/dev/test', 50003, 'reset')
        self.device.report.update(pending=1, flags=6)
        request = self.device.request

        ## @brief Stop boot monitoring immediately after the reset acknowledgement.
        def stop_monitor(operation, payload=b''):
            result = request(operation, payload)
            if operation == fw.RESET:
                worker.cancel.set()
            return result

        with patch.object(fw, 'Client', side_effect=self.device.connect), patch.object(self.device, 'request', side_effect=stop_monitor):
            worker.run()
        self.assertEqual(worker.snapshot()['outcome'], 'unconfirmed')
        self.assertEqual(self.device.operations.count(fw.RESET), 1)

    ## @brief Verify a lost reset acknowledgement is resolved by queries without repeating reset.
    def test_lost_reset_acknowledgement_still_checks_boot(self):
        self.device.report.update(pending=1, flags=6)
        request = self.device.request

        ## @brief Apply reset but lose its acknowledgement before the client can receive it.
        def lose_reset_ack(operation, payload=b''):
            result = request(operation, payload)
            if operation == fw.RESET:
                raise ConnectionError('Device reset before acknowledgement arrived')
            return result

        with patch.object(self.device, 'request', side_effect=lose_reset_ack):
            state = self.run_worker(UpdateWorker('Ethernet', 'device', 50003, 'reset'))
        self.assertEqual(state['outcome'], 'confirmed')
        self.assertEqual(self.device.operations.count(fw.RESET), 1)

    ## @brief Verify cancellation during connection setup prevents a queued maintenance mutation.
    def test_cancel_during_connect_prevents_maintenance_write(self):
        worker = UpdateWorker('USB', '/dev/test', 50003, 'clear')
        connect = self.device.connect

        ## @brief Cancel while the connection is being established.
        def cancel_connect(**options):
            worker.cancel.set()
            return connect(**options)

        with patch.object(self.device, 'connect', side_effect=cancel_connect):
            state = self.run_worker(worker)
        self.assertEqual(state['outcome'], 'cancelled')
        self.assertNotIn(fw.CLEAR, self.device.operations)


@unittest.skipUnless(os.environ.get('DISPLAY'), 'requires a display (or xvfb-run)')
class ViewerFirmwareTabTests(unittest.TestCase):
    ## @brief Verify closing the window waits for an acknowledged cancellation and releases the updater.
    def test_close_cancels_upload_before_destroying_window(self):
        import tkinter as tk
        from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
        from Board_Viewer import Viewer
        from test_profiler_viewer import destroy_root
        try:
            root = tk.Tk()
        except tk.TclError as error:
            self.skipTest(f'display unavailable: {error}')
        device = UpdateDevice()
        waiting, release = threading.Event(), threading.Event()
        request = device.request
        viewer = worker = None

        ## @brief Hold the first data acknowledgement until the GUI has requested cancellation.
        def hold_ack(operation, payload=b''):
            report = request(operation, payload)
            if operation == fw.DATA:
                waiting.set()
                if not release.wait(3):
                    raise TimeoutError('Test acknowledgement was not released')
            return report

        try:
            with tempfile.TemporaryDirectory() as directory, patch.object(FigureCanvasTkAgg, 'draw_idle'), \
                    patch.object(fw, 'Client', side_effect=device.connect), \
                    patch.object(device, 'request', side_effect=hold_ack):
                image = Path(directory) / 'signed.bin'
                image.write_bytes(image_fixture())
                args = argparse.Namespace(port=None, host='test-device', tcp_port=50002,
                                          depth_max=4000, connect=False)
                viewer = Viewer(root, args)
                viewer.fw_image.set(str(image))
                viewer.firmware_action('upload')
                worker = viewer.fw_worker
                self.assertTrue(waiting.wait(1))
                viewer.close()
                self.assertTrue(root.winfo_exists())
                self.assertTrue(worker.cancel.is_set())
                release.set()
                worker.thread.join(2)
                self.assertFalse(worker.thread.is_alive())
                self.assertEqual(worker.snapshot()['outcome'], 'cancelled')
                self.assertIn(fw.ABORT, device.operations)
                self.assertNotIn(fw.RESET, device.operations)
                viewer.update()
                with self.assertRaises(tk.TclError):
                    root.winfo_exists()
        finally:
            release.set()
            if worker:
                worker.cancel.set()
                worker.thread.join(3)
            try:
                if root.winfo_exists():
                    destroy_root(root)
            except tk.TclError:
                pass

    ## @brief Verify USB status queries pause the receiver and automatically resume a fresh session.
    def test_usb_status_handoff_resumes_receiver(self):
        import tkinter as tk
        from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
        import Board_Viewer
        from test_profiler_viewer import destroy_root
        from test_sensor_control import SensorConnection
        try:
            root = tk.Tk()
        except tk.TclError as error:
            self.skipTest(f'display unavailable: {error}')
        viewer = None
        device = UpdateDevice()
        args = argparse.Namespace(port='/dev/test', host='unused', tcp_port=50002,
                                  depth_max=4000, connect=True)

        ## @brief Reject a second serial owner until the old viewer receiver has exited.
        def exclusive_connect(**options):
            self.assertFalse(viewer.receiver.thread.is_alive())
            return device.connect(**options)

        try:
            with patch.object(FigureCanvasTkAgg, 'draw_idle'), \
                    patch.object(Board_Viewer.serial, 'Serial', side_effect=lambda *a, **k: SensorConnection()), \
                    patch.object(fw, 'Client', side_effect=exclusive_connect):
                viewer = Board_Viewer.Viewer(root, args)
                old_receiver = viewer.receiver
                deadline = time.monotonic() + 2
                while old_receiver.status != 'Connected' and time.monotonic() < deadline:
                    time.sleep(.01)
                self.assertEqual(old_receiver.status, 'Connected')
                viewer.firmware_action('status')
                deadline = time.monotonic() + 3
                while viewer.fw_worker is not None and time.monotonic() < deadline:
                    root.update()
                    time.sleep(.01)
                self.assertIsNone(viewer.fw_worker)
                self.assertFalse(old_receiver.thread.is_alive())
                self.assertIsNot(viewer.receiver, old_receiver)
                self.assertTrue(viewer.receiver.thread.is_alive())
                self.assertIn('status received', viewer.fw_message.get())
                self.assertTrue(viewer.fw_buttons['status'].instate(['!disabled']))
        finally:
            if viewer:
                viewer.disconnect()
                if viewer.fw_worker:
                    viewer.fw_worker.thread.join(3)
                viewer.receiver.thread.join(1)
            destroy_root(root)

    ## @brief Exercise the actual tab's upload, warning display, staging, and reset controls.
    def test_tab_stages_resets_and_shows_fallback(self):
        import tkinter as tk
        from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
        from Board_Viewer import Viewer
        from test_profiler_viewer import destroy_root
        try:
            root = tk.Tk()
        except tk.TclError as error:
            self.skipTest(f'display unavailable: {error}')
        viewer = None
        device = UpdateDevice(fallback=True)
        args = argparse.Namespace(port=None, host='test-device', tcp_port=50002,
                                  depth_max=4000, connect=False)
        try:
            with tempfile.TemporaryDirectory() as directory, patch.object(FigureCanvasTkAgg, 'draw_idle'), \
                    patch.object(fw, 'Client', side_effect=device.connect):
                image = Path(directory) / 'signed.bin'
                image.write_bytes(image_fixture())
                viewer = Viewer(root, args)
                viewer.fw_image.set(str(image))
                viewer.fw_version.set('42')
                viewer.fw_reset.set(False)
                viewer.firmware_action('upload')
                deadline = time.monotonic() + 3
                while viewer.fw_worker is not None and time.monotonic() < deadline:
                    root.update()
                    time.sleep(.01)
                self.assertIsNone(viewer.fw_worker)
                self.assertIn('Image verified in slot B', viewer.fw_message.get())
                self.assertIn('Pending: B', viewer.fw_slots.get())
                self.assertIn('Release B: 42', viewer.fw_versions.get())
                self.assertEqual(float(viewer.fw_progress['value']), 100)
                self.assertTrue(viewer.fw_buttons['reset'].instate(['!disabled']))
                viewer.firmware_action('reset')
                deadline = time.monotonic() + 4
                while viewer.fw_worker is not None and time.monotonic() < deadline:
                    root.update()
                    time.sleep(.01)
                self.assertIsNone(viewer.fw_worker)
                self.assertIn('Fallback warning', viewer.fw_message.get())
                self.assertIn('Running: A', viewer.fw_slots.get())
                self.assertIn('fallback warning', viewer.fw_warning.get())
                self.assertTrue(viewer.fw_buttons['reset'].instate(['disabled']))
                viewer.firmware_action('clear')
                if viewer.fw_worker is not None:
                    viewer.fw_worker.thread.join(1)
                viewer.update_firmware()
                self.assertIn('Warnings: none', viewer.fw_warning.get())
        finally:
            if viewer and viewer.fw_worker:
                viewer.fw_worker.cancel.set()
                viewer.fw_worker.thread.join(3)
            destroy_root(root)


if __name__ == '__main__':
    unittest.main()
