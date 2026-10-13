import json
import unittest
from USB_Debug import HEADER, PacketParser, PARAM_MAGIC, IMU_MAGIC, IMU_PAYLOAD


class ParameterPacketTests(unittest.TestCase):
    def test_fragmented_metadata_and_imu(self):
        payload = json.dumps({'repository': {'git_hash': 'a'*40, 'git_dirty': True}}).encode()
        metadata = HEADER.pack(PARAM_MAGIC, 0, len(payload), 1, 0) + payload
        imu_payload = IMU_PAYLOAD.pack(42, *([1.0]*10))
        imu = HEADER.pack(IMU_MAGIC, 1, len(imu_payload), 10, 1) + imu_payload
        parser = PacketParser()
        packets = []
        for byte in metadata + imu:
            packets.extend(parser.feed(bytes([byte])))
        self.assertEqual([p.kind for p in packets], [PARAM_MAGIC, IMU_MAGIC])
        self.assertEqual(json.loads(packets[0].payload)['repository']['git_dirty'], True)
        self.assertEqual(parser.discarded, 0)

    def test_invalid_metadata_size_resynchronizes(self):
        payload = b'{}'
        stream = HEADER.pack(PARAM_MAGIC, 0, 16385, 1, 0)
        stream += HEADER.pack(PARAM_MAGIC, 0, len(payload), 1, 0) + payload
        parser = PacketParser()
        packets = parser.feed(stream)
        self.assertEqual(len(packets), 1)
        self.assertEqual(packets[0].payload, payload)
        self.assertEqual(parser.invalid_headers, 1)
