"""USB framing and reconnect tests; no serial device or plot dependencies."""

import importlib.util
from pathlib import Path
import sys
import types
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location(
    "usb_debug", Path(__file__).with_name("USB_Debug.py"))
usb = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = usb
spec.loader.exec_module(usb)


def camera(sequence=0):
    return usb.HEADER.pack(b"VL9F", sequence, 4, 2, 2) + bytes(range(4))


def inertial(sequence=0):
    payload = usb.IMU_PAYLOAD.pack(1234, 0, 0, 1000, 1, 2, 3, 0, 0, 0, 1)
    return usb.HEADER.pack(b"IMUF", sequence, len(payload), 10, 1) + payload


class StreamingTests(unittest.TestCase):
    def tearDown(self):
        usb.stop_event.clear()

    def test_mixed_packets_at_every_split(self):
        wire = b"noise" + camera(7) + inertial(3) + camera(8)
        for split in range(len(wire) + 1):
            with self.subTest(split=split):
                parser = usb.PacketParser()
                packets = parser.feed(wire[:split]) + parser.feed(wire[split:])
                self.assertEqual([p.kind for p in packets], [b"VL9F", b"IMUF", b"VL9F"])
                self.assertEqual([p.sequence for p in packets], [7, 3, 8])
                self.assertEqual(usb.IMU_PAYLOAD.unpack(packets[1].payload)[0], 1234)

    def test_magic_inside_payload_is_not_a_new_packet(self):
        payload = b"IMUFVL9F"
        wire = usb.HEADER.pack(b"VL9F", 2, len(payload), 4, 2) + payload
        packets = usb.PacketParser().feed(wire + inertial())
        self.assertEqual(len(packets), 2)
        self.assertEqual(packets[0].payload, payload)

    def test_recovers_after_invalid_length_or_imu_version(self):
        for bad in (usb.HEADER.pack(b"VL9F", 0, 0xFFFFFFFF, 2, 2),
                    usb.HEADER.pack(b"IMUF", 0, 44, 10, 99)):
            parser = usb.PacketParser()
            packets = parser.feed(bad + inertial(9))
            self.assertEqual(packets[0].sequence, 9)
            self.assertEqual(parser.invalid_headers, 1)

    def test_retries_explicit_port(self):
        attempts = []
        def reader(port):
            attempts.append(port)
            if len(attempts) == 3:
                usb.stop_event.set()
        with patch.object(usb, "serial_reader", reader), \
                patch.object(usb.stop_event, "wait", return_value=False):
            usb.usb_monitor("/dev/ttyACM1")
        self.assertEqual(attempts, ["/dev/ttyACM1"] * 3)

    def test_auto_reconnect_after_renumbering(self):
        attempts = []
        def reader(port):
            attempts.append(port)
            if len(attempts) == 2:
                usb.stop_event.set()
        with patch.object(usb, "serial_reader", reader), \
                patch.object(usb.glob, "glob", side_effect=[[], ["/dev/ttyACM1"], [], ["/dev/ttyACM2"]]), \
                patch.object(usb.stop_event, "wait", return_value=False):
            usb.usb_monitor()
        self.assertEqual(attempts, ["/dev/ttyACM1", "/dev/ttyACM2"])

    def test_reader_drains_available_data_without_waiting_for_large_read(self):
        class Disconnected(Exception):
            pass

        class SerialDevice:
            def __init__(self):
                self.buffer = bytearray(camera(1) + inertial(2))
                self.read_sizes = []
                self.closed = False

            @property
            def in_waiting(self):
                return len(self.buffer)

            def read(self, count):
                self.read_sizes.append(count)
                if not self.buffer:
                    raise Disconnected("test unplug")
                data = bytes(self.buffer[:count])
                del self.buffer[:count]
                return data

            def close(self):
                self.closed = True

        device = SerialDevice()
        serial = types.SimpleNamespace(Serial=lambda *args, **kwargs: device,
                                       SerialException=Disconnected)
        before = usb.packet_count
        with patch.dict(sys.modules, serial=serial):
            usb.serial_reader("/dev/test")
        self.assertEqual(device.read_sizes[:2], [1, len(camera(1) + inertial(2)) - 1])
        self.assertEqual(usb.packet_count - before, 2)
        self.assertEqual(usb.latest_imu_packet.sequence, 2)
        self.assertTrue(device.closed)
        self.assertFalse(usb.connected)

    def test_depth_uses_zone_dimensions_and_preserves_step_flagged_distances(self):
        import numpy as np
        metadata = bytearray(100)
        usb.struct.pack_into("<H", metadata, 58, 2 << 6)
        distances = np.arange(54 * 42, dtype="<u2")
        distances[10] |= 0x8000
        payload = distances.tobytes() + bytes(54 * 42 * 4 + 54 * 42 // 2) + metadata
        grid, binning = usb.decode_depth(usb.Packet(1, 100, 149, payload))
        self.assertEqual(grid.shape, (42, 54))
        self.assertEqual(binning, 2)
        self.assertEqual(grid[0, 10], 10)
        self.assertEqual(grid[1, 0], 54)
        self.assertTrue(np.ma.is_masked(grid[0, 0]))

    def test_depth_rejects_bad_metadata(self):
        with self.assertRaises(ValueError):
            usb.decode_depth(usb.Packet(1, 100, 149, bytes(14900)))

    def test_ethernet_reader_reconnects_and_resets_partial_packets(self):
        connections = []
        wire = camera(9) + inertial(10)
        class Connection:
            def __init__(self, second):
                self.parts = ([camera(1) + b'VL9'] if not second else
                              [wire[:5], wire[5:19], wire[19:]])
                self.second = second
                self.closed = False
            def __enter__(self):
                return self
            def __exit__(self, *args):
                self.closed = True
            def settimeout(self, timeout):
                pass
            def recv(self, count):
                if self.parts:
                    return self.parts.pop(0)
                if self.second:
                    usb.stop_event.set()
                return b''
        def connect(*args, **kwargs):
            connection = Connection(bool(connections))
            connections.append(connection)
            return connection
        import socket
        with patch.object(socket, 'create_connection', side_effect=connect), \
                patch.object(usb.stop_event, 'wait', return_value=False):
            usb.ethernet_monitor('192.168.8.20', 50002)
        self.assertEqual(len(connections), 2)
        self.assertTrue(all(c.closed for c in connections))
        self.assertEqual(usb.latest_packet.sequence, 9)
        self.assertEqual(usb.latest_imu_packet.sequence, 10)
        self.assertFalse(usb.connected)


if __name__ == "__main__":
    unittest.main()
