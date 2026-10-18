"""Run the firmware link manager and TCP listener against a scripted driver."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from test_sensor_control import TX_STUB, MAIN_STUB, SENSOR_STUB

ROOT = Path(__file__).resolve().parents[1]
CONTROL = ROOT / 'Appli/Libs/control'
NETX_STUB = r'''
#ifndef NX_API_H
#define NX_API_H
#include "tx_api.h"
#define NX_SUCCESS 0U
#define NX_ALREADY_ENABLED 2U
#define NX_DRIVER_ERROR 3U
#define NX_NO_PACKET 4U
#define NX_IP_LINK_ENABLED 1U
#define NX_LINK_ENABLE 1U
#define NX_LINK_DISABLE 2U
#define NX_LINK_GET_STATUS 3U
#define NX_NULL NULL
#define NX_NO_WAIT 0U
#define NX_IP_NORMAL 0U
#define NX_FRAGMENT_OKAY 0U
#define NX_IP_TIME_TO_LIVE 128U
#define NX_TCP_PACKET 1U
#define NX_TCP_CLOSED 0U
#define NX_TCP_LISTEN_STATE 1U
#define NX_TCP_SYN_RECEIVED 2U
#define NX_TCP_ESTABLISHED 3U
#define NX_TCP_CLOSE_WAIT 4U
#define NX_NOT_LISTEN_STATE 5U
#define NX_NOT_CLOSED 6U
typedef void VOID;
typedef struct { int unused; } NX_IP;
typedef struct { int unused; } NX_PACKET_POOL;
typedef struct { UINT nx_tcp_socket_state; } NX_TCP_SOCKET;
typedef struct { ULONG nx_packet_length; } NX_PACKET;
UINT nx_ip_status_check(NX_IP *, ULONG, ULONG *, ULONG);
UINT nx_ip_driver_direct_command(NX_IP *, UINT, ULONG *);
UINT nx_tcp_socket_create(NX_IP *, NX_TCP_SOCKET *, char *, UINT, UINT, UINT, ULONG, void *, void *);
UINT nx_tcp_socket_disconnect(NX_TCP_SOCKET *, ULONG);
UINT nx_tcp_server_socket_unaccept(NX_TCP_SOCKET *);
UINT nx_tcp_server_socket_listen(NX_IP *, UINT, NX_TCP_SOCKET *, UINT, void *);
UINT nx_tcp_server_socket_relisten(NX_IP *, UINT, NX_TCP_SOCKET *);
UINT nx_tcp_server_socket_unlisten(NX_IP *, UINT);
UINT nx_tcp_server_socket_accept(NX_TCP_SOCKET *, ULONG);
UINT nx_tcp_socket_receive(NX_TCP_SOCKET *, NX_PACKET **, ULONG);
UINT nx_tcp_socket_send(NX_TCP_SOCKET *, NX_PACKET *, ULONG);
UINT nx_packet_allocate(NX_PACKET_POOL *, NX_PACKET **, UINT, ULONG);
UINT nx_packet_data_append(NX_PACKET *, VOID *, ULONG, NX_PACKET_POOL *, ULONG);
UINT nx_packet_data_extract_offset(NX_PACKET *, ULONG, UCHAR *, ULONG, ULONG *);
UINT nx_packet_release(NX_PACKET *);
#endif
'''
APP_STUB = '''#include "nx_api.h"
NX_IP *nx_get_ip(void);
NX_PACKET_POOL *nx_get_app_pool(void);
'''
TX_EXTRA = '''
#define TX_TIMER_TICKS_PER_SECOND 1000U
#define TX_AND 3U
#define TX_WAIT_FOREVER 0xffffffffU
ULONG tx_time_get(void);
'''
LINK_HARNESS = r'''
#include "ethernet_link.h"
#include "app_netxduo.h"
#include "app_events.h"
#include <assert.h>
#include <stdbool.h>
#include <setjmp.h>
#include <stdlib.h>
TX_EVENT_FLAGS_GROUP app_events;
static NX_IP ip;
static bool cable, mac, fail_enable, fail_disable, read_error;
static unsigned step, starts, stops, scenario;
static jmp_buf done;
NX_IP *nx_get_ip(void) { return &ip; }
UINT nx_ip_status_check(NX_IP *p, ULONG needed, ULONG *actual, ULONG wait) {
 assert(p==&ip && needed==NX_IP_LINK_ENABLED && wait==TX_NO_WAIT);
 *actual=cable ? NX_IP_LINK_ENABLED : 0;
 return read_error ? NX_DRIVER_ERROR : cable ? NX_SUCCESS : 1;
}
UINT nx_ip_driver_direct_command(NX_IP *p, UINT op, ULONG *ret) {
 (void)ret; assert(p==&ip);
 if(op==NX_LINK_ENABLE) {
  ++starts; if(fail_enable) { fail_enable=false; return NX_DRIVER_ERROR; }
  if(mac) return NX_ALREADY_ENABLED;
  assert(cable); mac=true; return NX_SUCCESS;
 }
 assert(op==NX_LINK_DISABLE); ++stops;
 if(fail_disable) { fail_disable=false; return NX_DRIVER_ERROR; }
 if(!mac) return NX_SUCCESS; /* disabled is idempotent in the board driver */
 mac=false; return NX_SUCCESS;
}
UINT tx_event_flags_set(TX_EVENT_FLAGS_GROUP *g, ULONG flags, UINT op) {
 assert(g==&app_events);
 if(op==TX_OR) { assert(mac && cable); g->flags|=flags; }
 else { assert(op==TX_AND); g->flags&=flags; }
 return TX_SUCCESS;
}
void tx_thread_sleep(ULONG ticks) {
 assert(ticks==200 && ++step<12);
 ULONG ready=app_events.flags&(APP_EVT_ETH_LINK_UP|APP_EVT_ETH_IP_READY);
 if(scenario==1) { /* NetX already enabled the MAC during a cabled boot. */
  assert(step==1 && ready==3 && starts==1 && ethernet_link_generation()==1);
  longjmp(done,1);
 }
 switch(step) {
 case 1: /* Boot without cable: stale readiness must be cleared. */
  assert(!ready && !mac && starts==0 && ethernet_link_generation()==0); break;
 case 2:
  assert(!ready && starts==0); cable=true; fail_enable=true; break;
 case 3: /* PHY up is insufficient when MAC startup fails. */
  assert(!ready && !mac && starts==1 && ethernet_link_generation()==0); break;
 case 4:
  assert(ready==3 && mac && starts==2 && ethernet_link_generation()==1); break;
 case 5: /* A stable link must not restart the MAC each poll. */
  assert(ready==3 && starts==2); cable=false; fail_disable=true; break;
 case 6: /* Readiness drops even if disabling the driver needs a retry. */
  assert(!ready && mac); break;
 case 7:
  assert(!ready && !mac); cable=true; break;
 case 8:
  assert(ready==3 && mac && starts==3 && ethernet_link_generation()==2);
  read_error=true; break;
 case 9: /* MDIO/status failures must also withdraw readiness. */
  assert(!ready && !mac); read_error=false; break;
 case 10:
  assert(ready==3 && mac && starts==4 && ethernet_link_generation()==3);
  longjmp(done,1);
 default: assert(0);
 }
}
int main(int argc, char **argv) {
 (void)argc; scenario=(unsigned)atoi(argv[1]);
 cable=mac=scenario==1;
 app_events.flags=3; /* prove stale application flags are cleared at boot */
 if(setjmp(done)==0) ethernet_link_thread_entry(0);
 return 0;
}
'''
STREAM_HARNESS = r'''
#include "ethernet_link.h"
#include "ethernet_stream.h"
#include "stream_manager.h"
#include "imu_stream.h"
#include "app_netxduo.h"
#include "app_events.h"
#include "tof_control.h"
#include <assert.h>
#include <setjmp.h>
#include <stdlib.h>
TX_EVENT_FLAGS_GROUP app_events, stream_events;
TX_QUEUE imu_packet_queue;
UCHAR stream_packet[STREAM_HEADER_SIZE + CSI_OUTPUT_PAYLOAD_SIZE];
ULONG stream_packet_length;
static NX_IP ip;
static NX_TCP_SOCKET *socket;
static unsigned step, listens, unlistens, disconnects, accepts, usb_writes, scenario;
static uint32_t generation;
static bool listening, unaccepted;
static jmp_buf done;
NX_IP *nx_get_ip(void) { return &ip; }
NX_PACKET_POOL *nx_get_app_pool(void) { return NULL; }
uint32_t ethernet_link_generation(void) { return generation; }
ULONG tx_time_get(void) { return 0; }
UINT nx_tcp_socket_create(NX_IP *p,NX_TCP_SOCKET *s,char *n,UINT a,UINT b,UINT c,ULONG w,void *d,void *e) {
 (void)p;(void)n;(void)a;(void)b;(void)c;(void)w;(void)d;(void)e;
 socket=s; s->nx_tcp_socket_state=NX_TCP_CLOSED; return NX_SUCCESS;
}
UINT nx_tcp_socket_disconnect(NX_TCP_SOCKET *s,ULONG wait) {
 assert(s==socket && wait==0); ++disconnects;
 if(s->nx_tcp_socket_state!=NX_TCP_LISTEN_STATE) s->nx_tcp_socket_state=NX_TCP_LISTEN_STATE;
 return 0;
}
UINT nx_tcp_server_socket_unaccept(NX_TCP_SOCKET *s) {
 assert(s==socket && s->nx_tcp_socket_state==NX_TCP_LISTEN_STATE);
 s->nx_tcp_socket_state=NX_TCP_CLOSED; unaccepted=true; return 0;
}
UINT nx_tcp_server_socket_listen(NX_IP *p,UINT port,NX_TCP_SOCKET *s,UINT count,void *cb) {
 (void)p;(void)port;(void)count;(void)cb;
 assert(!listening && s->nx_tcp_socket_state==NX_TCP_CLOSED);
 listening=true; ++listens; s->nx_tcp_socket_state=NX_TCP_LISTEN_STATE; return 0;
}
UINT nx_tcp_server_socket_relisten(NX_IP *p,UINT port,NX_TCP_SOCKET *s) {
 (void)p;(void)port; assert(listening && s->nx_tcp_socket_state==NX_TCP_CLOSED);
 s->nx_tcp_socket_state=NX_TCP_LISTEN_STATE; return 0;
}
UINT nx_tcp_server_socket_unlisten(NX_IP *p,UINT port) {
 (void)p;(void)port; assert(listening && unaccepted && socket->nx_tcp_socket_state==NX_TCP_CLOSED);
 listening=false; unaccepted=false; ++unlistens; return 0;
}
UINT nx_tcp_server_socket_accept(NX_TCP_SOCKET *s,ULONG wait) {
 assert(wait==0 && listening); ++accepts;
 if(scenario==2) { s->nx_tcp_socket_state=NX_TCP_ESTABLISHED; return 0; }
 return NX_NO_PACKET;
}
UINT nx_tcp_socket_receive(NX_TCP_SOCKET *s,NX_PACKET **p,ULONG wait) {
 (void)s;(void)p;(void)wait; return NX_NO_PACKET;
}
UINT nx_tcp_socket_send(NX_TCP_SOCKET *s,NX_PACKET *p,ULONG wait) {
 (void)s;(void)p;(void)wait; return 0;
}
UINT nx_packet_allocate(NX_PACKET_POOL *pool,NX_PACKET **p,UINT type,ULONG wait) {
 (void)pool;(void)p;(void)type;(void)wait; assert(0); return 1;
}
UINT nx_packet_data_append(NX_PACKET *p,VOID *data,ULONG len,NX_PACKET_POOL *pool,ULONG wait) {
 (void)p;(void)data;(void)len;(void)pool;(void)wait; assert(0); return 1;
}
UINT nx_packet_data_extract_offset(NX_PACKET *p,ULONG offset,UCHAR *data,ULONG len,ULONG *copied) {
 (void)p;(void)offset;(void)data;(void)len;(void)copied; assert(0); return 1;
}
UINT nx_packet_release(NX_PACKET *p) { (void)p; return 0; }
uint32_t stream_usb_session(void) { return 0; }
UINT stream_usb_write(UCHAR *p,ULONG len) { (void)p;(void)len; ++usb_writes; return 0; }
uint8_t *device_parameters_packet(uint32_t *size) { *size=0; return NULL; }
uint8_t *thread_profiler_packet(uint32_t *size) { *size=0; return NULL; }
void tof_control_feed(tof_parser_t *p,const uint8_t *data,size_t len,uint32_t route,uint32_t session) {
 (void)p;(void)data;(void)len;(void)route;(void)session;
}
bool tof_control_next_reply(tof_reply_t *reply) { (void)reply; return false; }
void tof_control_status_packet(uint8_t p[TOF_STATUS_PACKET_SIZE]) { (void)p; }
UINT tx_queue_receive(TX_QUEUE *q,void *p,UINT wait) { (void)q;(void)p;(void)wait; return 1; }
UINT tx_event_flags_set(TX_EVENT_FLAGS_GROUP *g,ULONG flags,UINT op) {
 (void)op; g->flags|=flags; return 0;
}
UINT tx_event_flags_get(TX_EVENT_FLAGS_GROUP *g,ULONG flags,UINT op,ULONG *actual,ULONG wait) {
 (void)op;(void)wait;
 if(g==&app_events) { *actual=g->flags&flags; return *actual?0:1; }
 assert(g==&stream_events && ++step<5);
 switch(step) {
 case 1:
  assert(listens==0 && usb_writes>0); /* USB/status continues with no cable */
  app_events.flags=3; generation=1; break;
 case 2:
  assert(listens==1 && accepts==1);
  if(scenario==2) {
   assert(socket->nx_tcp_socket_state==NX_TCP_ESTABLISHED);
   ++generation; /* complete down/up while the transport was blocked */
  } else {
   socket->nx_tcp_socket_state=NX_TCP_SYN_RECEIVED; app_events.flags=0;
  }
  break;
 case 3:
  assert(disconnects==1 && unlistens==1);
  if(scenario==2) { assert(listens==2 && listening); longjmp(done,1); }
  assert(!listening); app_events.flags=3; ++generation; break;
 case 4:
  assert(listens==2 && listening && accepts==2); longjmp(done,1);
 default: assert(0);
 }
 *actual=0; return 1;
}
int main(int argc,char **argv) {
 (void)argc; scenario=(unsigned)atoi(argv[1]);
 if(setjmp(done)==0) ethernet_stream_thread_entry(0);
 return 0;
}
'''


@unittest.skipUnless(shutil.which('cc'), 'native C compiler required')
class EthernetHotplugTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        temp = Path(cls.temp.name)
        for name, content in [('tx_api.h', TX_STUB + TX_EXTRA), ('nx_api.h', NETX_STUB),
                              ('app_netxduo.h', APP_STUB), ('main.h', MAIN_STUB),
                              ('VL53L9_process.h', SENSOR_STUB), ('app_threadx.h', '#include "tx_api.h"'),
                              ('stm32n6xx_hal.h', ''), ('link.c', LINK_HARNESS), ('stream.c', STREAM_HARNESS)]:
            (temp / name).write_text(content)
        cls.executables = {}
        for name, source in [('link', 'ethernet_link.c'), ('stream', 'ethernet_stream.c')]:
            executable = temp / name
            result = subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                                     '-I', str(temp), '-I', str(CONTROL), str(CONTROL / source),
                                     str(temp / f'{name}.c'), '-o', str(executable)],
                                    capture_output=True, text=True)
            if result.returncode:
                raise AssertionError(result.stderr)
            cls.executables[name] = executable

    def test_boot_without_cable_insert_remove_retry_and_reinsert(self):
        subprocess.run([str(self.executables['link']), '0'], check=True, timeout=5)

    def test_boot_with_link_already_enabled(self):
        subprocess.run([str(self.executables['link']), '1'], check=True, timeout=5)

    def test_half_open_accept_is_cleared_before_unlisten_and_reinsert(self):
        subprocess.run([str(self.executables['stream']), '0'], check=True, timeout=5)

    def test_completed_cable_cycle_invalidates_old_tcp_session(self):
        subprocess.run([str(self.executables['stream']), '2'], check=True, timeout=5)


if __name__ == '__main__':
    unittest.main()
