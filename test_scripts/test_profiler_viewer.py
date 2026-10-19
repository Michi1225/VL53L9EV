"""Desktop profiler integration; run with xvfb-run when no display is available."""
import argparse
import json
import os
import socket
import struct
import time
import unittest
from unittest.mock import patch

from USB_Debug import HEADER, PARAM_MAGIC, IMU_MAGIC, IMU_PAYLOAD
from test_thread_profiler import wire_profile


## @brief Cancel scheduled Tk callbacks and destroy the test window.
def destroy_root(root):
    # Separate Tk roots share the event loop; remove queued callbacks before
    # the next integration test creates another interpreter.
    for callback in root.tk.call('after', 'info'):
        root.tk.call('after', 'cancel', callback)
    root.destroy()


@unittest.skipUnless(os.environ.get('DISPLAY'), 'requires a display (or xvfb-run)')
class ProfilerViewerTests(unittest.TestCase):
    ## @brief Verify sensor configuration tab and acknowledged controls.
    def test_sensor_configuration_tab_and_acknowledged_controls(self):
        import tkinter as tk
        from Board_Viewer import Viewer
        from Sensor_Control import START, STOP, SET_CONFIG, RESET, POWER_DOWN
        from test_sensor_control import SensorConnection, wait_for, status_wire
        try:
            root = tk.Tk()
        except tk.TclError as error:
            self.skipTest(f'display unavailable: {error}')
        connection = SensorConnection()
        viewer = None
        args = argparse.Namespace(port=None, host='test-device', tcp_port=50002,
                                  depth_max=4000, connect=True)
        try:
            with patch.object(socket, 'create_connection', return_value=connection):
                viewer = Viewer(root, args)
                wait_for(lambda: viewer.last_tof_settings is not None, root.update)
                self.assertIn('Streaming', viewer.tof_state.get())
                row = bytearray(100)
                struct.pack_into('<Ih', row, 0, 100, 45)
                struct.pack_into('<H', row, 58, 2 << 6)
                struct.pack_into('<I', row, 68, 10000)
                payload = bytes(14800) + row
                connection.parts.put(HEADER.pack(b'VL9F', 0, len(payload), 100, 149) + payload)
                wait_for(lambda: '45 °C' in viewer.tof_diagnostics.get(), root.update)
                self.assertIn('Precision (no DSS)', viewer.tof_diagnostics.get())
                self.assertIn('reported period 10 ms', viewer.tof_diagnostics.get())
                self.assertTrue(all(button.instate(['disabled']) for button, op in viewer.tof_buttons if op == SET_CONFIG))
                viewer.sensor_command(STOP)
                wait_for(lambda: viewer.receiver.pending_command is None, root.update)
                root.update()
                viewer.tof_period.set('20')
                viewer.tof_exposure.set('2')
                viewer.tof_binning.set('4')
                viewer.tof_mode.set('Precision (no DSS)')
                viewer.sensor_command(SET_CONFIG)
                wait_for(lambda: viewer.receiver.pending_command is None, root.update)
                self.assertEqual(connection.config['period_us'], 20000)
                self.assertEqual(connection.config['binning'], 4)
                self.assertFalse(connection.config['dss_enabled'])
                self.assertEqual(connection.config['context'], 0)
                viewer.sensor_command(START)
                wait_for(lambda: viewer.receiver.pending_command is None, root.update)
                self.assertEqual(connection.state, 2)
                connection.state = 3
                connection.parts.put(status_wire(state=3))
                wait_for(lambda: 'Fault' in viewer.tof_state.get(), root.update)
                self.assertTrue(all(not button.instate(['disabled']) for button, op in viewer.tof_buttons if op == RESET))
                viewer.sensor_command(RESET)
                wait_for(lambda: viewer.receiver.pending_command is None, root.update)
                wait_for(lambda: viewer.tof_binning.get() == '2', root.update)
                self.assertEqual(connection.state, 1)
                self.assertIn('Stopped', viewer.tof_state.get())
                viewer.sensor_command(POWER_DOWN)
                wait_for(lambda: 'Powered down' in viewer.tof_state.get(), root.update)
                self.assertEqual(connection.state, 4)
                self.assertIn('45 °C', viewer.tof_diagnostics.get())
                self.assertIn('last frame', viewer.tof_diagnostics.get())
                self.assertTrue(all(button.instate(['disabled']) for button, op in viewer.tof_buttons if op in (START, SET_CONFIG, POWER_DOWN)))
                viewer.sensor_command(RESET)
                wait_for(lambda: 'Stopped' in viewer.tof_state.get(), root.update)
                viewer.tof_period.set('not a number')
                viewer.sensor_command(SET_CONFIG)
                viewer.update()
                self.assertNotIn('waiting for device', viewer.tof_command_status.get())
                self.assertEqual(len(connection.commands), 6)
        finally:
            if viewer:
                viewer.disconnect()
                viewer.receiver.thread.join(timeout=1)
            destroy_root(root)

    ## @brief Verify receiver and profiler tab with mixed stream.
    def test_receiver_and_profiler_tab_with_mixed_stream(self):
        import tkinter as tk
        from Board_Viewer import Viewer

        payload = json.dumps({'serial': 'test'}).encode()
        imu = IMU_PAYLOAD.pack(1000, *([1.0] * 10))
        wire = HEADER.pack(PARAM_MAGIC, 0, len(payload), 1, 0) + payload
        wire += wire_profile() + HEADER.pack(IMU_MAGIC, 0, len(imu), 10, 1) + imu

        class Connection:
            ## @brief Initialize the simulated transport state used by this test.
            def __init__(self):
                self.parts = [wire[:7], wire[7:20], wire[20:]]
            ## @brief Return the simulated transport when entering its context manager.
            def __enter__(self):
                return self
            ## @brief Finish the simulated transport context without suppressing test failures.
            def __exit__(self, *args):
                pass
            ## @brief Accept the socket timeout setting required by the simulated transport.
            def settimeout(self, value):
                pass
            ## @brief Return the next simulated TCP input or the configured timeout or disconnection.
            def recv(self, length):
                if self.parts:
                    return self.parts.pop(0)
                time.sleep(.01)
                raise socket.timeout()

        try:
            root = tk.Tk()
        except tk.TclError as error:
            self.skipTest(f'display unavailable: {error}')
        args = argparse.Namespace(port=None, host='test-device', tcp_port=50002,
                                  depth_max=4000, connect=True)
        viewer = None
        try:
            with patch.object(socket, 'create_connection', return_value=Connection()):
                viewer = Viewer(root, args)
                deadline = time.monotonic() + 3
                while not viewer.profile_tree.exists('thread-0000007b') and time.monotonic() < deadline:
                    root.update()
                    time.sleep(.01)
                self.assertTrue(viewer.profile_tree.exists('thread-0000007b'))
                self.assertEqual(viewer.profile_tree.item('thread-0000007b', 'values'),
                                 ('Camera', '12', '35.000', '35.00'))
                self.assertEqual(float(viewer.cpu_busy['value']), 50.0)
                self.assertEqual(viewer.metadata, {'serial': 'test'})
                self.assertEqual(viewer.receiver.imu_count, 1)
                self.assertEqual(viewer.receiver.invalid, 0)
                self.assertEqual(viewer.receiver.discarded, 0)
                viewer.show_profile(viewer.last_profile, time.monotonic() - 4)
                self.assertIn('STALE', viewer.profile_status.get())
                viewer.disconnect()
                viewer.receiver.thread.join(timeout=1)
                viewer.update()
                self.assertFalse(viewer.profile_tree.get_children())
                self.assertEqual(float(viewer.cpu_busy['value']), 0.0)
        finally:
            if viewer:
                viewer.disconnect()
            destroy_root(root)


if __name__ == '__main__':
    unittest.main()
