"""Exercise the real firmware accounting with a simulated Cortex cycle counter."""
import pathlib
import shutil
import subprocess
import tempfile
import unittest

from USB_Debug import (HEADER, IMU_MAGIC, IMU_PAYLOAD, PARAM_MAGIC, PROFILE_MAGIC,
                       PROFILE_HEADER, PROFILE_THREAD, Packet, PacketParser,
                       decode_profile, profile_rows)

ROOT = pathlib.Path(__file__).resolve().parents[1]

HARDWARE = r'''
#ifndef TEST_MAIN_H
#define TEST_MAIN_H
#include <stdint.h>
typedef struct { uint32_t CYCCNT, CTRL; } FakeDWT;
typedef struct { uint32_t DEMCR; } FakeCoreDebug;
extern FakeDWT fake_dwt;
extern FakeCoreDebug fake_debug;
extern uint32_t SystemCoreClock, irq_mask;
#define DWT (&fake_dwt)
#define CoreDebug (&fake_debug)
#define CoreDebug_DEMCR_TRCENA_Msk 1U
#define DWT_CTRL_CYCCNTENA_Msk 1U
/** @brief Read the simulated interrupt mask. */
static inline uint32_t __get_PRIMASK(void) { return irq_mask; }
/** @brief Set the simulated interrupt mask. */
static inline void __disable_irq(void) { irq_mask = 1; }
/** @brief Restore the simulated interrupt mask. */
static inline void __set_PRIMASK(uint32_t mask) { irq_mask = mask; }
#endif
'''
THREADX = r'''
#ifndef TEST_TX_API_H
#define TEST_TX_API_H
#include <stdint.h>
#define TX_NULL ((void *)0)
typedef uint32_t ULONG;
typedef struct TX_THREAD_STRUCT {
    unsigned long long profiler_cycles;
    char *tx_thread_name;
    unsigned tx_thread_priority;
    struct TX_THREAD_STRUCT *tx_thread_created_next;
} TX_THREAD;
extern TX_THREAD *_tx_thread_current_ptr, *_tx_thread_created_ptr;
extern ULONG _tx_thread_created_count;
ULONG tx_time_get(void);
#endif
'''
HARNESS = r'''
#include "main.h"
#include "tx_api.h"
#include "thread_profiler.h"
#include <assert.h>
#include <stdio.h>
FakeDWT fake_dwt;
FakeCoreDebug fake_debug;
uint32_t SystemCoreClock = 1000, irq_mask;
TX_THREAD *_tx_thread_current_ptr, *_tx_thread_created_ptr;
ULONG _tx_thread_created_count;
/** @brief Return the simulated ThreadX time base. */
ULONG tx_time_get(void) { return 1000; }
/** @brief Advance the simulated CPU cycle counter. */
static void advance(uint32_t cycles) { fake_dwt.CYCCNT += cycles; }
/** @brief Write a real firmware profiling packet to the harness output. */
static void emit(void) {
    uint32_t size;
    uint8_t *data = thread_profiler_packet(&size);
    assert(size > 16);
    assert(fwrite(data, 1, size, stdout) == size);
}
/** @brief Exercise CPU accounting across context switches, interrupts, and counter wraparound. */
int main(void) {
    uint32_t size = 99;
    _tx_execution_isr_enter(); /* Early HAL interrupts precede kernel init. */
    _tx_execution_isr_exit();
    thread_profiler_packet(&size);
    assert(size == 0);
    TX_THREAD a = {.tx_thread_name = "Camera", .tx_thread_priority = 12};
    TX_THREAD b = {.tx_thread_name = "Transport", .tx_thread_priority = 10};
    a.tx_thread_created_next = &b;
    b.tx_thread_created_next = &a;
    _tx_thread_created_ptr = &a;
    _tx_thread_created_count = 2;
    fake_dwt.CYCCNT = UINT32_MAX - 9;
    _tx_execution_initialize();
    advance(5);
    _tx_thread_current_ptr = &a;
    _tx_execution_thread_enter();
    advance(10); /* Cross the 32-bit counter wrap in a running thread. */
    _tx_execution_isr_enter();
    advance(4);
    _tx_execution_isr_enter();
    advance(3);
    _tx_execution_isr_exit();
    advance(5);
    _tx_execution_isr_exit();
    advance(20);
    _tx_execution_thread_exit();
    advance(7);
    _tx_execution_idle_enter();
    advance(40);
    _tx_execution_isr_enter();
    advance(6);
    _tx_execution_isr_exit();
    advance(10);
    _tx_thread_current_ptr = &b;
    _tx_execution_thread_enter();
    advance(15);
    irq_mask = 1;
    emit();
    assert(irq_mask == 1); /* Keep the caller's interrupt mask. */
    irq_mask = 0;
    advance(25);
    emit(); /* Account for the currently running thread and reset the window. */
    assert(irq_mask == 0);
    _tx_execution_thread_exit();

    /* More than one full CYCCNT wrap between reports, with regular ticks. */
    _tx_execution_idle_enter();
    for (unsigned i = 0; i < 6; ++i) {
        advance(1000000000);
        _tx_execution_isr_enter();
        advance(10);
        _tx_execution_isr_exit();
    }
    emit();

    TX_THREAD many[65] = {0};
    _tx_thread_created_ptr = many;
    _tx_thread_created_count = 65;
    for (unsigned i = 0; i < 65; ++i) {
        many[i].tx_thread_name = "A thread name longer than thirty one characters";
        many[i].tx_thread_created_next = &many[(i + 1) % 65];
        _tx_thread_current_ptr = &many[i];
        _tx_execution_thread_enter();
        advance(1);
        _tx_execution_thread_exit();
    }
    emit();
    for (unsigned i = 0; i < 65; ++i) assert(many[i].profiler_cycles == 0);
    return 0;
}
'''


## @brief Encode a valid profiling interval for mixed-stream tests.
def wire_profile(sequence=0):
    payload = PROFILE_HEADER.pack(1000, 1000, 100, 50, 10, 5, 1, 0)
    payload += PROFILE_THREAD.pack(123, 12, 35, b'Camera')
    return HEADER.pack(PROFILE_MAGIC, sequence, len(payload), 1, 1) + payload


class ProfilerTests(unittest.TestCase):
    ## @brief Verify firmware accounting and wire format.
    @unittest.skipUnless(shutil.which('cc'), 'native C compiler required')
    def test_firmware_accounting_and_wire_format(self):
        with tempfile.TemporaryDirectory() as directory:
            temp = pathlib.Path(directory)
            (temp / 'main.h').write_text(HARDWARE)
            (temp / 'tx_api.h').write_text(THREADX)
            (temp / 'tx_thread.h').write_text('#include "tx_api.h"\n')
            (temp / 'harness.c').write_text(HARNESS)
            source = ROOT / 'Appli/Libs/control/thread_profiler.c'
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                            '-I', str(temp), '-I', str(source.parent), str(source),
                            str(temp / 'harness.c'), '-o', str(temp / 'profiler')],
                           check=True, capture_output=True)
            wire = subprocess.check_output([str(temp / 'profiler')])
        parser = PacketParser()
        packets = parser.feed(wire)
        self.assertEqual(len(packets), 4)
        self.assertEqual([p.sequence for p in packets], [0, 1, 2, 3])
        profiles = [decode_profile(p) for p in packets]
        first, second, long_idle, truncated = profiles
        self.assertEqual(first['interval_cycles'], 125)
        self.assertEqual([t['cycles'] for t in first['threads']], [30, 15])
        self.assertEqual([first[k] for k in ['idle_cycles', 'isr_cycles', 'scheduler_cycles']], [50, 18, 12])
        self.assertEqual(second['interval_cycles'], 25)
        self.assertEqual([t['cycles'] for t in second['threads']], [0, 25])
        self.assertEqual(second['idle_cycles'], 0)
        self.assertEqual(long_idle['interval_cycles'], 6000000060)
        self.assertEqual(long_idle['idle_cycles'], 6000000000)
        self.assertEqual(long_idle['isr_cycles'], 60)
        self.assertEqual(len(truncated['threads']), 64)
        self.assertTrue(truncated['truncated'])
        self.assertEqual(len(truncated['threads'][0]['name']), 31)
        self.assertEqual(profile_rows(truncated)[-1], ('other', 'Other / removed threads', '', 1.0, 100 / 65))
        for profile in profiles:
            self.assertAlmostEqual(sum(row[-1] for row in profile_rows(profile)), 100)
        self.assertEqual(parser.discarded, 0)

    ## @brief Verify mixed stream at every byte boundary.
    def test_mixed_stream_at_every_byte_boundary(self):
        imu = IMU_PAYLOAD.pack(1000, *([1.0] * 10))
        wire = wire_profile() + HEADER.pack(IMU_MAGIC, 1, len(imu), 10, 1) + imu
        wire += HEADER.pack(PARAM_MAGIC, 0, 2, 1, 0) + b'{}'
        for split in range(len(wire) + 1):
            parser = PacketParser()
            packets = parser.feed(wire[:split]) + parser.feed(wire[split:])
            self.assertEqual([p.kind for p in packets], [PROFILE_MAGIC, IMU_MAGIC, PARAM_MAGIC])
            rows = profile_rows(decode_profile(packets[0]))
            self.assertEqual(rows[0], ('thread-0000007b', 'Camera', 12, 35.0, 35.0))
            self.assertEqual(parser.discarded, 0)

    ## @brief Verify invalid profiler headers resynchronize.
    def test_invalid_profiler_headers_resynchronize(self):
        for length, version, count in [(44, 1, 65), (44, 2, 0), (43, 1, 0), (45, 1, 0)]:
            parser = PacketParser()
            bad = HEADER.pack(PROFILE_MAGIC, 0, length, version, count)
            packets = parser.feed(bad + wire_profile(9))
            self.assertEqual([p.sequence for p in packets], [9])
            self.assertEqual(parser.invalid_headers, 1)

    ## @brief Verify decoder rejects inconsistent times and counts.
    def test_decoder_rejects_inconsistent_times_and_counts(self):
        for clock, interval, idle, isr, scheduler, count, flags in [
                (0, 100, 0, 0, 0, 0, 0), (1000, 0, 0, 0, 0, 0, 0),
                (1000, 100, 90, 20, 0, 0, 0), (1000, 100, 0, 0, 0, 1, 0),
                (1000, 100, 0, 0, 0, 0, 2)]:
            payload = PROFILE_HEADER.pack(0, clock, interval, idle, isr, scheduler, count, flags)
            with self.assertRaises(ValueError):
                decode_profile(Packet(0, 1, 0, payload, PROFILE_MAGIC))

    ## @brief Verify legacy reader accepts profile without treating it as imu.
    def test_legacy_reader_accepts_profile_without_treating_it_as_imu(self):
        import USB_Debug as usb
        from unittest.mock import patch
        chunks = iter([wire_profile(), b''])
        ## @brief Return the next bytes supplied by the simulated input stream.
        def read():
            chunk = next(chunks)
            if not chunk:
                usb.stop_event.set()
            return chunk
        old_imu, old_packets = usb.imu_packet_count, usb.packet_count
        with patch.object(usb, 'latest_profile'), patch.object(usb, 'latest_imu_packet'):
            try:
                usb.stream_reader(read, 'test')
                self.assertEqual(usb.latest_profile['threads'][0]['name'], 'Camera')
                self.assertIsNone(usb.latest_imu_packet)
                self.assertEqual(usb.imu_packet_count, old_imu)
                self.assertEqual(usb.packet_count, old_packets)
            finally:
                usb.stop_event.clear()


if __name__ == '__main__':
    unittest.main()
