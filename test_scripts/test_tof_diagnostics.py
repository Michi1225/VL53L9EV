"""Sensor metadata, mode protocol compatibility, and register-level profile tests."""
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest

from USB_Debug import Packet, PacketParser, HEADER, MAGIC, decode_depth, decode_depth_metadata, SensorFrameTiming
from Sensor_Control import build_command, decode_status, SET_CONFIG, POWER_DOWN
from test_sensor_control import CONFIG, status_wire

ROOT = Path(__file__).resolve().parents[1]


class DiagnosticsTests(unittest.TestCase):
    ## @brief Verify modes and both status versions.
    def test_modes_and_both_status_versions(self):
        for version in (1, 2):
            wire = status_wire(version=version)
            for split in range(len(wire) + 1):
                parser = PacketParser()
                packets = parser.feed(wire[:split]) + parser.feed(wire[split:])
                self.assertEqual(decode_status(packets[0])['config'], CONFIG)
        config = dict(CONFIG, context=0, dss_enabled=False)
        packet = PacketParser().feed(status_wire(config=config))[0]
        self.assertEqual(decode_status(packet)['config'], config)
        self.assertEqual(struct.unpack_from('<H', build_command(1, SET_CONFIG, config), 26)[0], 1)
        self.assertEqual(build_command(2, POWER_DOWN)[14:], b'\x06\x00')

    ## @brief Verify metadata and short no dss frame.
    def test_metadata_and_short_no_dss_frame(self):
        row = bytearray(100)
        struct.pack_into('<IhH', row, 0, 123, -12, 456)
        struct.pack_into('<HH', row, 58, 2 << 6, 0x0f00)
        struct.pack_into('<I', row, 68, 10000)
        data = struct.pack('<H', 1000) * (54 * 42 * 3) + row
        packet = Packet(1, 100, 138, data)
        meta = decode_depth_metadata(packet)
        self.assertEqual(meta, dict(counter=123, temperature_c=-12, ldd_temperature_raw=456,
                                   dss_mode=0, context=0, period_us=10000, error=0x0f00))
        depth, binning = decode_depth(packet)
        self.assertEqual((depth.shape, binning, depth[0, 0]), ((42, 54), 2, 1000))
        # Turning on DSS requires its extra payload plane.
        struct.pack_into('<H', row, 58, (2 << 6) | (2 << 4))
        with self.assertRaises(ValueError):
            decode_depth(Packet(2, 100, 138, data[:-100] + row))

    ## @brief Verify counter estimate distinguishes sensor and received rate.
    def test_counter_estimate_distinguishes_sensor_and_received_rate(self):
        timing = SensorFrameTiming()
        # Only every second frame arrives, but the sensor is running at 100 Hz.
        for i in range(51):
            timing.add(i * 2, i * .02)
        self.assertAlmostEqual(timing.fps, 100)
        timing.add(0, 1.1)  # sensor reset
        self.assertIsNone(timing.fps)
        timing.add(25, 1.6)
        self.assertAlmostEqual(timing.fps, 50)
        timing.reset()
        timing.add(0xfffffff0, 2)
        timing.add(34, 2.5)
        self.assertEqual(timing.fps, 100)


@unittest.skipUnless(shutil.which('cc'), 'native C compiler required')
class SensorRegisterTests(unittest.TestCase):
    ## @brief Verify gaming profile registers readback and no dss decode.
    def test_gaming_profile_registers_readback_and_no_dss_decode(self):
        driver = ROOT / 'Appli/Libs/53L9/vl53l9'
        utils = ROOT / 'Appli/Libs/53L9/interface/vl53l9'
        with tempfile.TemporaryDirectory() as directory:
            temp = Path(directory)
            (temp / 'vl53l9_interface.h').write_text('#include "vl53l9.h"\ntypedef struct { int unused; } vl53l9_device_t;\n')
            (temp / 'test.c').write_text(r'''
#include "vl53l9_utils.h"
#include "vl53l9_platform.h"
#include "vl53l9_reg.h"
#include <assert.h>
#include <string.h>
static unsigned char registers[65536];
static int mismatch, writes;
/** @brief Capture sensor register writes in the simulated register map. */
int vl53l9_write(void *p, uint16_t a, uint8_t *v, uint32_t size) { (void)p; memcpy(registers+a,v,size); return 0; }
/** @brief Read an eight-bit value from the simulated sensor register map. */
int vl53l9_read8(void *p, uint16_t a, uint8_t *v) {
 (void)p; *v=registers[a];
 if(mismatch && a==VL53L9_REGADDR_STANDBY_DSS_MODE(0)) *v=0xff;
 return 0;
}
/** @brief Write an eight-bit value to the simulated sensor register map. */
int vl53l9_write8(void *p, uint16_t a, uint8_t v) { (void)p; ++writes; registers[a]=v; return 0; }
/** @brief Read a 32-bit value from the simulated sensor register map. */
int vl53l9_read32(void *p, uint16_t a, uint32_t *v) { (void)p; memcpy(v,registers+a,4); return 0; }
/** @brief Write a 16-bit value to the simulated sensor register map. */
int vl53l9_write16(void *p, uint16_t a, uint16_t v) { (void)p; memcpy(registers+a,&v,2); return 0; }
/** @brief Write a 32-bit value to the simulated sensor register map. */
int vl53l9_write32(void *p, uint16_t a, uint32_t v) { (void)p; memcpy(registers+a,&v,4); return 0; }
/** @brief Verify sensor register configuration and CSI frame decoding. */
int main(void) {
 vl53l9_device_t device={0};
 registers[VL53L9_REGADDR_SYSTEM_FSM]=2;
 vl53l9_profile_t p=g_ranging_profiles[VL53L9_USECASE_GAMING];
 assert(p.context==VL53L9_CONTEXT_SHORT && !p.dss_enabled);
 /* Exercise a 1 ms profile without pinning the editable preset's exposure. */
 p.exposure_ms=1;
 assert(p.frame_period_us==10000 && p.power==VL53L9_POWER_REGULAR);
 assert(vl53l9_utils_set_profile(&device,&p)==0);
 assert(registers[VL53L9_REGADDR_STANDBY_DSS_MODE(0)]==0);
 uint32_t period=0; assert(vl53l9_get_frame_period(&device,&period)==0 && period==10000);
 p.dss_enabled=true; assert(vl53l9_utils_set_profile(&device,&p)==0);
 assert(registers[VL53L9_REGADDR_STANDBY_DSS_MODE(0)]==2);
 p.context=VL53L9_CONTEXT_LONG; assert(vl53l9_utils_set_profile(&device,&p)==0);
 assert(registers[VL53L9_REGADDR_STANDBY_DSS_MODE(1)]==1);
 mismatch=1; assert(vl53l9_set_dss_enabled(&device,VL53L9_CONTEXT_SHORT,false)!=0);
 mismatch=0; registers[VL53L9_REGADDR_SYSTEM_FSM]=3;
 unsigned before=writes; assert(vl53l9_set_dss_enabled(&device,VL53L9_CONTEXT_SHORT,false)!=0);
 assert(before==(unsigned)writes);
 uint8_t buffer[54*42*6+100]={0}, dumped[sizeof(buffer)];
 vl53l9_meta_t *meta=(vl53l9_meta_t *)(buffer+sizeof(buffer)-100);
 _Static_assert(sizeof(vl53l9_meta_t)==100,"status row layout");
 meta->binning=2; meta->dss_mode=0;
 vl53l9_frame_t frame;
 assert(vl53l9_utils_parse_frame(buffer,sizeof(buffer),&frame)==0 && !frame.p_dss_lut);
 assert(vl53l9_utils_dump_csi_frame(&frame,dumped,sizeof(dumped))==0);
 assert(memcmp(buffer,dumped,sizeof(buffer))==0);
 meta->dss_mode=2; assert(vl53l9_utils_parse_frame(buffer,sizeof(buffer),&frame)!=0);
 assert(vl53l9_utils_parse_frame(buffer,1,&frame)!=0);
 return 0;
}
''')
            result = subprocess.run(['cc', '-std=gnu11', '-ffunction-sections', '-fdata-sections',
                                     '-I', str(temp), '-I', str(driver), '-I', str(utils),
                                     str(driver / 'vl53l9.c'), str(utils / 'vl53l9_utils.c'),
                                     str(temp / 'test.c'), '-Wl,--gc-sections', '-lm', '-o', str(temp / 'test')],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            subprocess.run([str(temp / 'test')], check=True)


if __name__ == '__main__':
    unittest.main()
