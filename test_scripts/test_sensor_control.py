"""Control wire format and real firmware state machine with simulated hardware."""
import queue
import socket
import time
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest

from USB_Debug import HEADER, MAGIC, IMU_MAGIC, IMU_PAYLOAD, TOF_MAGIC, TOF_STATUS, TOF_STATUS_V2, PacketParser
from Sensor_Control import build_command, decode_status, validate_config, GET_CONFIG, START, STOP, SET_CONFIG, RESET, POWER_DOWN

ROOT = Path(__file__).resolve().parents[1]
CONFIG = dict(period_us=10000, exposure_ms=1, binning=2, power=0, context=1, dss_enabled=True)


## @brief Encode a synthetic ToF status or acknowledgement packet.
def status_wire(request_id=0, operation=0, state=2, result=0, config=None, version=2):
    c = config or CONFIG
    height = {2: 149, 4: 39, 6: 18, 8: 9, 12: 6, 24: 3}[c['binning']]
    layout = TOF_STATUS if version == 1 else TOF_STATUS_V2
    extra = () if version == 1 else (int(not c.get("dss_enabled", True)), 0)
    payload = layout.pack(1000, 1, 0, c['period_us'], c['exposure_ms'], c['binning'],
                              c['power'], c['context'], state, result, 100, height, 42, *extra)
    return HEADER.pack(TOF_MAGIC, request_id, len(payload), version, operation) + payload


class ControlProtocolTests(unittest.TestCase):
    ## @brief Verify exact command bytes.
    def test_exact_command_bytes(self):
        self.assertEqual(build_command(0x12345678, STOP).hex(' '),
                         '43 54 52 4c 78 56 34 12 00 00 00 00 01 00 03 00')
        self.assertEqual(build_command(1, RESET).hex(' '),
                         '43 54 52 4c 01 00 00 00 00 00 00 00 01 00 05 00')
        wire = build_command(7, SET_CONFIG, CONFIG)
        self.assertEqual(HEADER.unpack(wire[:16]), (b'CTRL', 7, 12, 2, 4))
        self.assertEqual(struct.unpack('<IHHBBH', wire[16:]), (10000, 1, 2, 0, 1, 0))

    ## @brief Verify invalid settings and commands.
    def test_invalid_settings_and_commands(self):
        for changes in [dict(period_us=9999), dict(period_us=1000001), dict(exposure_ms=0),
                        dict(exposure_ms=31), dict(exposure_ms=11), dict(binning=3),
                        dict(power=3), dict(context=2), dict(period_us=10.0), dict(dss_enabled=2)]:
            with self.subTest(changes=changes), self.assertRaises(ValueError):
                build_command(1, SET_CONFIG, dict(CONFIG, **changes))
        for request_id, operation in [(0, START), (0x100000000, START), (1, 99)]:
            with self.assertRaises(ValueError):
                build_command(request_id, operation)

    ## @brief Verify status with mixed sensor packets at every split.
    def test_status_with_mixed_sensor_packets_at_every_split(self):
        imu = IMU_PAYLOAD.pack(1000, *([1.] * 10))
        wire = HEADER.pack(MAGIC, 1, 4, 2, 2) + bytes(4)
        wire += status_wire(17, STOP, state=1) + HEADER.pack(IMU_MAGIC, 1, len(imu), 10, 1) + imu
        for split in range(len(wire) + 1):
            parser = PacketParser()
            packets = parser.feed(wire[:split]) + parser.feed(wire[split:])
            self.assertEqual([p.kind for p in packets], [MAGIC, TOF_MAGIC, IMU_MAGIC])
            status = decode_status(packets[1])
            self.assertEqual((status['request_id'], status['operation'], status['state']), (17, STOP, 1))
            self.assertEqual(status['config'], CONFIG)
            self.assertEqual(parser.discarded, 0)


class SensorConnection:
    """Full-duplex fake used by receiver and Tk integration tests."""
    ## @brief Initialize the simulated transport state used by this test.
    def __init__(self):
        self.parts = queue.Queue()
        self.parts.put(status_wire())
        self.commands = []
        self.state = 2
        self.config = dict(CONFIG)
        self.auto_reply = True

    ## @brief Return the simulated transport when entering its context manager.
    def __enter__(self):
        return self

    ## @brief Finish the simulated transport context without suppressing test failures.
    def __exit__(self, *args):
        pass

    ## @brief Accept the socket timeout setting required by the simulated transport.
    def settimeout(self, timeout):
        pass

    ## @brief Report the input currently available from the simulated serial transport.
    @property
    def in_waiting(self):
        return 1

    ## @brief Return the next simulated TCP input or the configured timeout or disconnection.
    def recv(self, count):
        try:
            return self.parts.get(timeout=.01)
        except queue.Empty:
            raise socket.timeout()

    read = recv

    ## @brief Submit outgoing bytes to the simulated transport.
    def sendall(self, wire):
        self.write(wire)

    ## @brief Consume outgoing bytes and emulate the transport response used by the test.
    def write(self, wire):
        _, request, size, _, operation = HEADER.unpack(wire[:16])
        self.commands.append((request, operation))
        result = 0
        if operation == STOP:
            self.state = 1
        elif operation == START:
            self.state = 2
        elif operation == POWER_DOWN:
            self.state = 4
        elif operation == RESET:
            self.state = 1
            self.config = dict(CONFIG)
        elif operation == SET_CONFIG:
            if self.state != 1:
                result = 5
            else:
                values = struct.unpack('<IHHBBH', wire[16:])
                self.config = dict(zip(('period_us', 'exposure_ms', 'binning', 'power', 'context'), values[:5]))
                self.config['dss_enabled'] = not bool(values[5] & 1)
        if self.auto_reply:
            reply = status_wire(request, operation, self.state, result, self.config)
            for part in (reply[:3], reply[3:19], reply[19:]):
                self.parts.put(part)
        return len(wire)


## @brief Pump asynchronous work until a test condition succeeds or the deadline expires.
def wait_for(condition, pump=lambda: None):
    deadline = time.monotonic() + 3
    while not condition() and time.monotonic() < deadline:
        pump()
        time.sleep(.01)
    if not condition():
        raise AssertionError('Timed out waiting for receiver')


class ReceiverControlTests(unittest.TestCase):
    ## @brief Verify start stop set on tcp and usb without blocking reader.
    def test_start_stop_set_on_tcp_and_usb_without_blocking_reader(self):
        from unittest.mock import patch
        from Board_Viewer import Receiver
        for mode in ('Ethernet', 'USB'):
            with self.subTest(mode=mode):
                connection = SensorConnection()
                target = 'socket.create_connection' if mode == 'Ethernet' else 'serial.Serial'
                with patch(target, return_value=connection):
                    receiver = Receiver(mode, 'test-device', 50002)
                    try:
                        wait_for(lambda: receiver.tof_status is not None)
                        receiver.send_command(STOP)
                        wait_for(lambda: receiver.pending_command is None)
                        self.assertEqual(receiver.tof_status['state'], 1)
                        changed = dict(CONFIG, period_us=20000, binning=4, exposure_ms=2, context=0, dss_enabled=False)
                        receiver.send_command(SET_CONFIG, changed)
                        wait_for(lambda: receiver.pending_command is None)
                        self.assertEqual(receiver.tof_settings, changed)
                        receiver.send_command(START)
                        wait_for(lambda: receiver.pending_command is None)
                        self.assertEqual(receiver.tof_status['state'], 2)
                        receiver.send_command(POWER_DOWN)
                        wait_for(lambda: receiver.pending_command is None)
                        self.assertEqual(receiver.tof_status["state"], 4)
                        self.assertIsNone(receiver.depth)
                        receiver.send_command(RESET)
                        wait_for(lambda: receiver.pending_command is None)
                        self.assertEqual(receiver.tof_status['state'], 1)
                        self.assertEqual(receiver.tof_settings, CONFIG)
                        self.assertEqual(receiver.invalid, 0)
                        self.assertEqual([op for _, op in connection.commands], [STOP, SET_CONFIG, START, POWER_DOWN, RESET])
                    finally:
                        receiver.stop.set()
                        receiver.thread.join(timeout=1)
                    self.assertIsNone(receiver.tof_status)

    ## @brief Verify acknowledgements match both id and operation and timeout is not retried.
    def test_acknowledgements_match_both_id_and_operation_and_timeout_is_not_retried(self):
        from unittest.mock import patch
        from Board_Viewer import Receiver
        connection = SensorConnection()
        connection.auto_reply = False
        with patch('socket.create_connection', return_value=connection):
            receiver = Receiver('Ethernet', 'test-device', 50002)
            try:
                wait_for(lambda: receiver.tof_status is not None)
                receiver.send_command(STOP)
                wait_for(lambda: len(connection.commands) == 1)
                request_id = connection.commands[0][0]
                connection.parts.put(status_wire(request_id, GET_CONFIG, state=1))
                wait_for(lambda: receiver.tof_status['operation'] == GET_CONFIG)
                self.assertIsNotNone(receiver.pending_command)
                connection.parts.put(status_wire(request_id, STOP, state=1))
                wait_for(lambda: receiver.pending_command is None)
                receiver.send_command(START)
                wait_for(lambda: len(connection.commands) == 2)
                with receiver.lock:
                    ident, operation, _ = receiver.pending_command
                    receiver.pending_command = (ident, operation, time.monotonic() - 9)
                wait_for(lambda: receiver.pending_command is None)
                self.assertIn('No acknowledgement', receiver.command_message)
                self.assertEqual(len(connection.commands), 2)
            finally:
                receiver.stop.set()
                receiver.thread.join(timeout=1)


TX_STUB = r'''
#ifndef TX_API_H
#define TX_API_H
#include <stdint.h>
#include <stddef.h>
typedef uint32_t ULONG;
typedef unsigned UINT;
typedef unsigned char UCHAR;
typedef struct { unsigned words, capacity, head, tail, count; uint8_t *data; } TX_QUEUE;
typedef struct { ULONG flags; } TX_EVENT_FLAGS_GROUP;
#define TX_SUCCESS 0
#define TX_NO_WAIT 0
#define TX_OR 1
#define TX_OR_CLEAR 2
unsigned tx_queue_create(TX_QUEUE *, char *, unsigned, void *, unsigned);
unsigned tx_queue_send(TX_QUEUE *, void *, unsigned);
unsigned tx_queue_receive(TX_QUEUE *, void *, unsigned);
unsigned tx_event_flags_get(TX_EVENT_FLAGS_GROUP *, ULONG, unsigned, ULONG *, unsigned);
unsigned tx_event_flags_set(TX_EVENT_FLAGS_GROUP *, ULONG, unsigned);
void tx_thread_sleep(ULONG);
#endif
'''
MAIN_STUB = r'''
#ifndef MAIN_H
#define MAIN_H
#include <stdint.h>
#include <stdbool.h>
#define HAL_OK 0
#define HAL_ERROR 1
typedef int HAL_StatusTypeDef;
uint32_t HAL_GetTick(void);
/** @brief Read the simulated interrupt mask. */
static inline uint32_t __get_PRIMASK(void) { return 0; }
/** @brief Set the simulated interrupt mask. */
static inline void __disable_irq(void) {}
/** @brief Restore the simulated interrupt mask. */
static inline void __set_PRIMASK(uint32_t value) { (void)value; }
/** @brief Provide the no-op cache invalidation hook used by host tests. */
static inline void SCB_InvalidateDCache_by_Addr(uint32_t *p, unsigned n) { (void)p; (void)n; }
#endif
'''
SENSOR_STUB = r'''
#ifndef VL53L9_PROCESS_H
#define VL53L9_PROCESS_H
#include "main.h"
#define CSI_OUTPUT_PAYLOAD_SIZE 14900U
#define CSI_OUTPUT_PITCH 100U
#define CSI_OUTPUT_BUFFER_SIZE 14912U
#define PLATFORM_CAM_PIPE_FRAME_EVT 1
typedef int vl53l9_power_mode_t;
typedef int vl53l9_context_t;
typedef struct { uint32_t frame_period_us; uint16_t exposure_ms; uint8_t binning; int power, context; bool dss_enabled; } vl53l9_profile_t;
typedef struct { uint16_t firmware; struct { bool internal_fw, ref_array; } error; } vl53l9_status_t;
typedef struct { volatile uint8_t g_csi_output_buffer[14912]; void *p_dev; vl53l9_profile_t profile; uint8_t csi_width, csi_height; } VL53L9_TypeDef_t;
int VL53L9_init(VL53L9_TypeDef_t *);
int VL53L9_start_ranging(VL53L9_TypeDef_t *);
int VL53L9_power_down(VL53L9_TypeDef_t *);
int VL53L9_configure(VL53L9_TypeDef_t *, const vl53l9_profile_t *);
void VL53L9_prepare_capture(VL53L9_TypeDef_t *);
int platform_stop_csi_pipe(void);
int platform_start_csi_pipe(uint8_t *);
int platform_get_event_status(int, bool *);
int platform_acknowledge_event(int);
int vl53l9_stop(void *);
int vl53l9_get_status(void *, vl53l9_status_t *);
#endif
'''
HARNESS = r'''
#include "tx_api.h"
#include "tof_control.h"
#include "VL53L9_process.h"
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static uint32_t led_state = UINT32_MAX;
/* Record LED publication separately from the wire status; the adapter's
 * hardware mappings are exercised by test_led_controller.py. */
void status_led_tof(uint32_t state) { led_state = state; }
/** @brief Initialize an in-memory queue with the requested capacity and message size. */
unsigned tx_queue_create(TX_QUEUE *q, char *name, unsigned words, void *storage, unsigned size) {
    (void)name; *q = (TX_QUEUE){.words=words, .capacity=size/(words*4), .data=storage}; return 0;
}
/** @brief Copy a message into the simulated bounded queue when space is available. */
unsigned tx_queue_send(TX_QUEUE *q, void *p, unsigned wait) {
    (void)wait; if(q->count==q->capacity) return 1;
    memcpy(q->data+q->tail*q->words*4,p,q->words*4); q->tail=(q->tail+1)%q->capacity; ++q->count; return 0;
}
/** @brief Take a message from the simulated queue or report that it is empty. */
unsigned tx_queue_receive(TX_QUEUE *q, void *p, unsigned wait) {
    (void)wait; if(!q->count) return 1;
    memcpy(p,q->data+q->head*q->words*4,q->words*4); q->head=(q->head+1)%q->capacity; --q->count; return 0;
}
/** @brief Return the event flags configured for the current test scenario. */
unsigned tx_event_flags_get(TX_EVENT_FLAGS_GROUP *e, ULONG flags, unsigned op, ULONG *actual, unsigned wait) {
    (void)wait; *actual=e->flags&flags; if(op==TX_OR_CLEAR) e->flags&=~flags; return *actual?0:1;
}
/** @brief Update simulated event flags and check the expected firmware transition. */
unsigned tx_event_flags_set(TX_EVENT_FLAGS_GROUP *e, ULONG flags, unsigned op) { (void)op; e->flags|=flags; return 0; }
static uint32_t tick;
/** @brief Return the harness's simulated millisecond clock. */
uint32_t HAL_GetTick(void) { return tick; }
static unsigned starts, stops, captures, configurations, initializations, shutdowns;
static bool ready=true, capturing, failed, fail_initialization;
/** @brief Initialize the simulated sensor profile and count reset attempts. */
int VL53L9_init(VL53L9_TypeDef_t *s) {
    ++initializations; if(fail_initialization) return 1;
    failed=false; s->p_dev=(void *)1; s->profile=(vl53l9_profile_t){10000,1,2,0,1,true};
    s->csi_width=100; s->csi_height=149; return 0;
}
/** @brief Record the simulated sensor shutdown. */
int VL53L9_power_down(VL53L9_TypeDef_t *s) { (void)s; assert(!capturing); ++shutdowns; return 0; }
/** @brief Record a simulated ranging start and apply the configured result. */
int VL53L9_start_ranging(VL53L9_TypeDef_t *s) { (void)s; ++starts; ++captures; capturing=true; return 0; }
/** @brief Apply the requested profile and geometry to the simulated sensor. */
int VL53L9_configure(VL53L9_TypeDef_t *s, const vl53l9_profile_t *p) {
    assert(!capturing); ++configurations; s->profile=*p; s->csi_height=39; return 0;
}
/** @brief Provide the capture buffer preparation hook for the sensor harness. */
void VL53L9_prepare_capture(VL53L9_TypeDef_t *s) { (void)s; }
/** @brief Record a simulated CSI capture stop. */
int platform_stop_csi_pipe(void) { capturing=false; return 0; }
/** @brief Record a simulated CSI capture start. */
int platform_start_csi_pipe(uint8_t *p) { (void)p; ++captures; capturing=true; return 0; }
/** @brief Report the frame readiness selected by the test scenario. */
int platform_get_event_status(int e, bool *r) { (void)e; *r=ready; return 0; }
/** @brief Clear the simulated frame event. */
int platform_acknowledge_event(int e) { (void)e; ready=false; capturing=false; return 0; }
/** @brief Record a simulated sensor ranging stop. */
int vl53l9_stop(void *p) { (void)p; ++stops; return 0; }
/** @brief Return the configured simulated sensor health status. */
int vl53l9_get_status(void *p, vl53l9_status_t *s) { (void)p; s->firmware=failed?15:0; return 0; }
extern TX_EVENT_FLAGS_GROUP stream_events;
static tof_parser_t parser;
static unsigned stage;
static jmp_buf finished;
/** @brief Queue a sensor command for the lifecycle test. */
static void submit(uint32_t id, uint32_t opcode, bool set) {
    uint8_t wire[28]={'C','T','R','L'};
    wire[4]=id; wire[8]=set?12:0; wire[12]=1; wire[14]=opcode;
    if(set) { wire[16]=0x20; wire[17]=0x4e; wire[20]=2; wire[22]=4; wire[24]=1; }
    for(unsigned i=0;i<(set?28U:16U);++i) tof_control_feed(&parser,wire+i,1,TOF_TCP,17);
}
/** @brief Print a queued sensor acknowledgement for host-side assertions. */
static void emit_reply(unsigned id, unsigned result) {
    tof_reply_t reply; assert(tof_control_next_reply(&reply));
    assert(reply.route==TOF_TCP && reply.session==17 && reply.packet[4]==id && reply.packet[39]==result);
    assert(led_state == reply.packet[38]);
    assert(fwrite(reply.packet,1,sizeof(reply.packet),stdout)==sizeof(reply.packet));
}
/** @brief Advance the test scenario when the firmware yields. */
void tx_thread_sleep(ULONG t) {
    tick+=t;
    assert(++stage<40);
    switch(stage) {
    case 1: /* A depth packet is still held by the transport. */
        assert(starts==1 && captures==2 && capturing); submit(1,TOF_STOP,false); break;
    case 2: /* Stop must wait for the consumer; no premature acknowledgement. */
        assert(stops==0); assert(!tof_control_next_reply(&(tof_reply_t){0}));
        tx_event_flags_set(&stream_events,2,TX_OR); break;
    case 3:
        emit_reply(1,TOF_OK); assert(stops==1 && !capturing); submit(2,TOF_SET,true); break;
    case 4:
        emit_reply(2,TOF_OK); assert(configurations==1); submit(3,TOF_START,false); break;
    case 5:
        emit_reply(3,TOF_OK); assert(starts==2); ready=true; break;
    case 6:
        tx_event_flags_set(&stream_events,2,TX_OR); submit(4,TOF_START,false); break;
    case 7:
        emit_reply(4,TOF_OK); assert(starts==2 && captures==4 && capturing);
        submit(5,TOF_SET,true); break;
    case 8:
        emit_reply(5,TOF_MUST_STOP); assert(configurations==1); submit(6,TOF_GET,false); break;
    case 9:
        emit_reply(6,TOF_OK); failed=true; tick=1001; break;
    case 10: {
        uint8_t packet[TOF_STATUS_PACKET_SIZE]; tof_control_status_packet(packet);
        assert(packet[38]==TOF_FAULT); assert(packet[24]==15);
        assert(fwrite(packet,1,sizeof(packet),stdout)==sizeof(packet));
        assert(!capturing); submit(7,TOF_START,false); break;
    }
    case 11:
        emit_reply(7,TOF_FAULT_LATCHED); assert(starts==2 && initializations==1);
        fail_initialization=true; submit(8,TOF_RESET,false); break;
    case 12:
        emit_reply(8,TOF_SENSOR_ERROR); assert(starts==2 && initializations==2 && !capturing);
        fail_initialization=false; submit(9,TOF_RESET,false); break;
    case 13:
        emit_reply(9,TOF_OK); assert(starts==2 && initializations==3 && !capturing);
        submit(10,TOF_START,false); break;
    case 14:
        emit_reply(10,TOF_OK); assert(starts==3 && capturing); ready=true; break;
    case 15:
        submit(11,TOF_RESET,false); break;
    case 16: /* Reset must drain the last frame just like Stop. */
        assert(initializations==3); assert(!tof_control_next_reply(&(tof_reply_t){0}));
        tx_event_flags_set(&stream_events,2,TX_OR); break;
    case 17:
        emit_reply(11,TOF_OK); assert(starts==3 && initializations==4 && !capturing);
        submit(12,TOF_POWER_DOWN,false); break;
    case 18:
        emit_reply(12,TOF_OK); assert(shutdowns==1 && starts==3);
        submit(13,TOF_START,false); break;
    case 19:
        emit_reply(13,TOF_POWERED_DOWN); assert(starts==3 && !capturing);
        submit(14,TOF_SET,true); break;
    case 20:
        emit_reply(14,TOF_POWERED_DOWN); assert(configurations==1);
        submit(15,TOF_STOP,false); break;
    case 21:
        emit_reply(15,TOF_OK); assert(shutdowns==1);
        submit(16,TOF_POWER_DOWN,false); break;
    case 22:
        emit_reply(16,TOF_OK); assert(shutdowns==1);
        submit(17,TOF_GET,false); break;
    case 23:
        emit_reply(17,TOF_OK); submit(18,TOF_RESET,false); break;
    case 24:
        emit_reply(18,TOF_OK); assert(starts==3 && initializations==5 && !capturing);
        failed=true; submit(19,TOF_START,false); break;
    case 25:
        emit_reply(19,TOF_OK); failed=true; tick+=1001; break;
    case 26:
        submit(20,TOF_POWER_DOWN,false); break;
    case 27:
        emit_reply(20,TOF_OK); assert(shutdowns==2 && !capturing);
        submit(21,TOF_RESET,false); break;
    case 28:
        emit_reply(21,TOF_OK); submit(22,TOF_START,false); break;
    case 29:
        emit_reply(22,TOF_OK); ready=true; break;
    case 30:
        submit(23,TOF_POWER_DOWN,false); break;
    case 31: /* Shutdown must also drain a packet before toggling XSHUT. */
        assert(shutdowns==2 && capturing);
        assert(!tof_control_next_reply(&(tof_reply_t){0}));
        tx_event_flags_set(&stream_events,2,TX_OR); break;
    case 32:
        emit_reply(23,TOF_OK); assert(shutdowns==3 && !capturing);
        longjmp(finished,1);
    default: assert(0);
    }
}
extern void stream_thread_entry(ULONG);
/** @brief Run the selected sensor lifecycle or command parsing scenario. */
int main(int argc, char **argv) {
    (void)argv; assert(tof_control_init()==0);
    if(argc>1) {
        uint8_t byte;
        while(fread(&byte,1,1,stdin)==1) tof_control_feed(&parser,&byte,1,TOF_USB,23);
        tof_command_t c;
        while(tof_control_next(&c)) printf("%u %u %u %u %u %u %u %u %u %u %u\n",c.request_id,c.operation,c.result,
            c.config.period_us,c.config.exposure_ms,c.config.binning,c.config.power,c.config.context,c.route,c.session,c.config.dss_enabled);
        tof_reply_t reply; while(tof_control_next_reply(&reply)) assert(reply.packet[39]==TOF_BUSY);
    } else if(setjmp(finished)==0) stream_thread_entry(0);
    return 0;
}
'''


@unittest.skipUnless(shutil.which('cc'), 'native C compiler required')
class FirmwareControlTests(unittest.TestCase):
    ## @brief Compile the firmware harness and prepare shared test fixtures.
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        temp = Path(cls.temp.name)
        for name, source in [('main.h', MAIN_STUB), ('tx_api.h', TX_STUB),
                             ('VL53L9_process.h', SENSOR_STUB), ('harness.c', HARNESS),
                             ('dcmipp.h', ''), ('app_threadx.h', '#include "tx_api.h"\n')]:
            (temp / name).write_text(source)
        cls.executable = temp / 'control'
        control = ROOT / 'Appli/Libs/control'
        result = subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-I', str(temp),
                                 '-I', str(control), str(control / 'tof_control.c'),
                                 str(control / 'stream_manager.c'), str(temp / 'harness.c'),
                                 '-o', str(cls.executable)], capture_output=True, text=True)
        if result.returncode:
            raise AssertionError(result.stderr)

    ## @brief Verify real firmware lifecycle and frame ownership.
    def test_real_firmware_lifecycle_and_frame_ownership(self):
        wire = subprocess.check_output([str(self.executable)])
        packets = PacketParser().feed(wire)
        reports = [decode_status(packet) for packet in packets]
        self.assertEqual([r['state'] for r in reports], [1, 1, 2, 2, 2, 2, 3, 3, 3, 1, 2, 1, 4, 4, 4, 4, 4, 4, 1, 2, 4, 1, 2, 4])
        self.assertEqual([r['result'] for r in reports], [0, 0, 0, 0, 5, 0, 0, 6, 4, 0, 0, 0, 0, 7, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0])
        self.assertEqual(reports[1]['config'], dict(period_us=20000, exposure_ms=2, binning=4, power=1, context=0, dss_enabled=True))
        self.assertEqual(reports[1]['height'], 39)
        self.assertEqual(reports[6]['error'], 15)
        self.assertEqual(reports[8]['error'], -1)
        self.assertEqual(reports[9]['error'], 0)
        self.assertEqual(reports[9]['config'], CONFIG)
        self.assertEqual(reports[9]['height'], 149)
        self.assertEqual(reports[-1]['frames'], 4)

    ## @brief Run the firmware parser harness and decode its emitted command records.
    def parse_commands(self, wire):
        result = subprocess.check_output([str(self.executable), 'parse'], input=wire)
        return [[int(value) for value in line.split()] for line in result.splitlines()]

    ## @brief Verify fragmented coalesced and invalid commands in real parser.
    def test_fragmented_coalesced_and_invalid_commands_in_real_parser(self):
        bad = HEADER.pack(b'CTRL', 55, 0xffffffff, 1, 1)
        wire = b'noise' + bad + build_command(1, GET_CONFIG) + build_command(2, SET_CONFIG, CONFIG)
        wire += HEADER.pack(b'CTRL', 3, 0, 1, 99)
        wire += HEADER.pack(b'CTRL', 4, 0, 1, SET_CONFIG)
        invalid = bytearray(build_command(5, SET_CONFIG, CONFIG)); invalid[-1] = 1
        wire += invalid + build_command(6, RESET)
        wire += HEADER.pack(b'CTRL', 7, 12, 1, RESET) + bytes(12)
        commands = self.parse_commands(wire)
        self.assertEqual([c[:3] for c in commands], [[1, 1, 0], [2, 4, 0], [3, 99, 1], [4, 4, 2], [5, 4, 2], [6, 5, 0], [7, 5, 2]])
        self.assertEqual(commands[1][3:8], [10000, 1, 2, 0, 1])
        self.assertTrue(all(c[8:10] == [1, 23] for c in commands))

    ## @brief Verify versions dss flags and power down in real parser.
    def test_versions_dss_flags_and_power_down_in_real_parser(self):
        no_dss = build_command(1, SET_CONFIG, dict(CONFIG, context=0, dss_enabled=False))
        legacy = bytearray(no_dss); legacy[12] = 1
        unknown_flags = bytearray(no_dss); unknown_flags[-2] = 2
        commands = self.parse_commands(no_dss + legacy + unknown_flags + build_command(4, POWER_DOWN))
        self.assertEqual([c[2] for c in commands], [0, 2, 2, 0])
        self.assertEqual(commands[0][10], 0)
        self.assertEqual(commands[-1][1], POWER_DOWN)
        legacy[-2] = 0
        commands = self.parse_commands(legacy)
        self.assertEqual((commands[0][2], commands[0][10]), (0, 1))

    ## @brief Verify queue is bounded.
    def test_queue_is_bounded(self):
        wire = b''.join(build_command(i, GET_CONFIG) for i in range(1, 10))
        self.assertEqual(len(self.parse_commands(wire)), 8)


if __name__ == '__main__':
    unittest.main()
