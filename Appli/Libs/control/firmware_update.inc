/* Included once from main.c USER CODE. The updater owns all post-startup flash
 * accesses; the existing one-time parameter read has finished before it starts. */
#include "firmware_update.h"
#include "../../../common/firmware/fw_update.c"
#include "../../../common/firmware/fw_flash.c"
#include "../../../common/firmware/fw_wire.c"
#include "app_netxduo.h"
#include "app_events.h"
#include "tof_control.h"
#include "ethernet_link.h"
#include "xspi.h"

static TX_THREAD dfu_thread;
static ULONG dfu_stack[2048];
static TX_MUTEX dfu_usb_mutex;
static TX_QUEUE dfu_usb_queue;
typedef struct { ULONG session,length; uint8_t data[48]; } dfu_usb_item_t;
static ULONG dfu_usb_storage[32*sizeof(dfu_usb_item_t)/sizeof(ULONG)];
static volatile bool dfu_initialized,dfu_suppress_telemetry;
static volatile uint32_t dfu_usb_heartbeat;
static fw_update_t dfu_update;
static fw_parser_t dfu_usb_parser,dfu_tcp_parser;
static NX_TCP_SOCKET dfu_socket;
static bool dfu_connected;
static uint32_t dfu_usb_session,dfu_tcp_session,dfu_owner_route,dfu_owner_session,dfu_last_activity,dfu_reset_at;
static uint32_t dfu_running=FW_NONE;
static SEMPER_Handle_t dfu_flash;
static int dfu_start_result=FW_NOT_READY;
extern UINT stream_usb_write(UCHAR *,ULONG);
extern uint32_t stream_usb_session(void);
_Static_assert(sizeof(dfu_usb_item_t)%sizeof(ULONG)==0 && sizeof(dfu_usb_item_t)<=16*sizeof(ULONG),"ThreadX USB queue item");

/** @brief Acquire the mutex that keeps USB telemetry and update replies from interleaving. */
UINT firmware_update_usb_lock(void)
{ return dfu_initialized?tx_mutex_get(&dfu_usb_mutex,TX_TIMER_TICKS_PER_SECOND*2U):TX_NOT_AVAILABLE; }
/** @brief Release the shared USB packet mutex. */
void firmware_update_usb_unlock(void) { (void)tx_mutex_put(&dfu_usb_mutex); }
/** @brief Report whether telemetry is allowed during the current upload. */
bool firmware_update_usb_telemetry(void) { return !dfu_suppress_telemetry; }
/** @brief Record that the USB task is running for trial boot health checks. */
void firmware_update_usb_alive(void) { dfu_usb_heartbeat=HAL_GetTick(); }
/** @brief Queue USB input for the updater while preserving the session and byte order. */
void firmware_update_usb_feed(const uint8_t *data,size_t size,uint32_t session)
{
    while(size && dfu_initialized){
        dfu_usb_item_t item={.session=session};
        size_t take=size>sizeof(item.data)?sizeof(item.data):size;
        item.length=(ULONG)take;memcpy(item.data,data,take);
        /* Backpressure preserves every byte of the multiplexed byte stream. */
        if(tx_queue_send(&dfu_usb_queue,&item,TX_WAIT_FOREVER)!=TX_SUCCESS)return;
        size-=take;data+=take;
    }
}
/** @brief Yield to other ThreadX threads while waiting for flash operations. */
static void dfu_idle(void) { tx_thread_sleep(1); }
/** @brief Close the DFU TCP session and abort an upload owned by that client. */
static void dfu_disconnect(void)
{
    if(dfu_connected && dfu_owner_route==TOF_TCP && dfu_owner_session==dfu_tcp_session){
        (void)fw_abort(&dfu_update);dfu_owner_route=0;dfu_suppress_telemetry=false;
    }
    nx_tcp_socket_disconnect(&dfu_socket,NX_NO_WAIT);
    nx_tcp_server_socket_unaccept(&dfu_socket);
    dfu_connected=false;dfu_tcp_parser.used=0;
}
/** @brief Send a DFU status packet through the matching USB or TCP session. */
static bool dfu_send(uint32_t route,uint32_t session,uint8_t *packet)
{
    if(route==TOF_USB){
        if(firmware_update_usb_lock()!=TX_SUCCESS)return false;
        bool ok=session==stream_usb_session() && stream_usb_write(packet,FW_STATUS_SIZE)==0;
        firmware_update_usb_unlock();return ok;
    }
    if(!dfu_connected || session!=dfu_tcp_session)return false;
    NX_PACKET *p=NX_NULL;
    UINT r=nx_packet_allocate(nx_get_app_pool(),&p,NX_TCP_PACKET,TX_TIMER_TICKS_PER_SECOND);
    if(r!=NX_SUCCESS)return false;
    r=nx_packet_data_append(p,packet,FW_STATUS_SIZE,nx_get_app_pool(),TX_TIMER_TICKS_PER_SECOND);
    if(r==NX_SUCCESS)r=nx_tcp_socket_send(&dfu_socket,p,TX_TIMER_TICKS_PER_SECOND);
    if(r!=NX_SUCCESS){nx_packet_release(p);return false;}
    return true;
}
/** @brief Encode update progress, slot versions, and persistent flags in a DFUS reply. */
static void dfu_status(uint8_t p[FW_STATUS_SIZE],uint32_t id,uint32_t op,int result)
{
    memset(p,0,FW_STATUS_SIZE);memcpy(p,"DFUS",4);fw_put32(p+4,id);fw_put32(p+8,48);
    p[12]=1;p[14]=(uint8_t)op;
    fw_put32(p+16,(uint32_t)result);fw_put32(p+20,dfu_update.meta.flags);
    fw_put32(p+24,dfu_running);fw_put32(p+28,dfu_update.meta.confirmed);
    fw_put32(p+32,dfu_update.meta.pending);fw_put32(p+36,dfu_update.received);
    fw_put32(p+40,dfu_update.uploading?dfu_update.incoming.size:0);
    fw_put32(p+44,dfu_update.meta.last_error);fw_put32(p+48,dfu_update.meta.generation);
    fw_put32(p+52,dfu_update.meta.image[0].version);fw_put32(p+56,dfu_update.meta.image[1].version);
    fw_put32(p+60,FW_CHUNK_SIZE);
}
typedef struct { uint32_t route,session; } dfu_route_t;
/** @brief Validate and execute a DFU request or forward a multiplexed ToF command. */
static void dfu_command(void *context,const uint8_t *p,uint32_t size)
{
    dfu_route_t *route=context;
    if(memcmp(p,"CTRL",4)==0){
        if(route->route==TOF_USB){tof_parser_t parser={0};tof_control_feed(&parser,p,size,TOF_USB,route->session);}
        return;
    }
    uint32_t id=fw_get32(p+4),length=fw_get32(p+8),op=p[14]|((uint32_t)p[15]<<8);
    int result=FW_BAD_REQUEST;
    bool owner=dfu_owner_route==route->route && dfu_owner_session==route->session;
    if(id && op==FW_OP_STATUS && !length)result=dfu_start_result;
#ifndef DEV_MODE
    else if(dfu_start_result==FW_OK && id && !dfu_reset_at){
        if(op==FW_OP_BEGIN && length==40){
            if(dfu_running!=dfu_update.meta.confirmed || dfu_update.meta.pending!=FW_NONE)result=FW_NOT_READY;
            else {
                dfu_suppress_telemetry=true;
                result=fw_begin(&dfu_update,fw_get32(p+16),p+24,fw_get32(p+20));
                if(result==FW_OK){dfu_owner_route=route->route;dfu_owner_session=route->session;}
                else if(!dfu_update.uploading)dfu_suppress_telemetry=false;
            }
        }
        else if(op==FW_OP_DATA && length>=12 && length<=FW_CHUNK_SIZE+8)
            result=owner?fw_data(&dfu_update,fw_get32(p+16),p+24,length-8,fw_get32(p+20)):FW_BUSY;
        else if(op==FW_OP_FINISH && !length)result=owner?fw_finish(&dfu_update):FW_BUSY;
        else if(op==FW_OP_ABORT && !length)result=(!dfu_owner_route || owner)?fw_abort(&dfu_update):FW_BUSY;
        else if(op==FW_OP_CLEAR && !length && !dfu_update.uploading)result=fw_clear_warnings(&dfu_update);
        else if(op==FW_OP_RESET && !length)
            result=dfu_update.meta.pending!=FW_NONE && !dfu_update.meta.trial && !dfu_update.uploading?FW_OK:FW_NOT_READY;
    }
#else
    else if(id)result=FW_NOT_READY; /* SRAM development images never mutate boot metadata/slots. */
#endif
    if(owner || (op==FW_OP_BEGIN && result==FW_OK))dfu_last_activity=HAL_GetTick();
    uint8_t reply[FW_STATUS_SIZE];dfu_status(reply,id,op,result);
    bool sent=dfu_send(route->route,route->session,reply);
    if(op==FW_OP_RESET && result==FW_OK && sent)dfu_reset_at=HAL_GetTick()+500U;
    if(!dfu_update.uploading){dfu_suppress_telemetry=false;dfu_owner_route=0;}
}
/** @brief Own flash updates, service USB and TCP requests, and confirm healthy trial boots. */
static void dfu_thread_entry(ULONG argument)
{
    (void)argument;
    fw_flash_t flash={&dfu_flash,dfu_idle};
    if(SEMPER_Init(&dfu_flash,&hxspi1)==SEMPER_OK){
        fw_io_t io=fw_flash_io(&flash);dfu_start_result=fw_open(&dfu_update,&io);
        /* Only the new FSBL creates/adopts boot metadata. Refuse writes when
         * running with a legacy FSBL rather than pretending it supports A/B. */
        if(dfu_start_result==FW_OK && dfu_update.record<0)dfu_start_result=FW_NOT_READY;
        if(dfu_start_result==FW_OK){
            dfu_running=dfu_update.meta.trial?dfu_update.meta.pending:dfu_update.meta.confirmed;
            uint8_t digest[32];fw_image_t *image=&dfu_update.meta.image[dfu_running];
            fw_sha256((const void *)FW_RAM_BASE,image->size,digest);
            if(!image->size || memcmp(digest,image->sha256,32))dfu_start_result=FW_BAD_IMAGE;
        }
    }
    else dfu_start_result=FW_IO_ERROR;
    bool socket_ready=nx_tcp_socket_create(nx_get_ip(),&dfu_socket,"Firmware update",NX_IP_NORMAL,
        NX_FRAGMENT_OKAY,NX_IP_TIME_TO_LIVE,4096,NX_NULL,NX_NULL)==NX_SUCCESS;
    bool listening=false;uint32_t generation=ethernet_link_generation();
#ifndef DEV_MODE
    uint32_t healthy_since=0;
    bool confirmed=dfu_start_result==FW_OK && dfu_update.meta.pending==FW_NONE;
#endif
    for(;;){
        ULONG flags=0;tx_event_flags_get(&app_events,APP_EVT_ETH_IP_READY,TX_AND,&flags,TX_NO_WAIT);
        uint32_t current_generation=ethernet_link_generation();
        if(listening && (!(flags&APP_EVT_ETH_IP_READY) || current_generation!=generation)){
            dfu_disconnect();nx_tcp_server_socket_unlisten(nx_get_ip(),FW_DFU_PORT);listening=false;
        }
        generation=current_generation;
        if(dfu_connected && dfu_socket.nx_tcp_socket_state!=NX_TCP_ESTABLISHED)dfu_disconnect();
        if(socket_ready && (flags&APP_EVT_ETH_IP_READY)){
            if(!listening)listening=nx_tcp_server_socket_listen(nx_get_ip(),FW_DFU_PORT,&dfu_socket,1,NX_NULL)==NX_SUCCESS;
            if(listening && !dfu_connected){
                if(dfu_socket.nx_tcp_socket_state==NX_TCP_CLOSED)
                    (void)nx_tcp_server_socket_relisten(nx_get_ip(),FW_DFU_PORT,&dfu_socket);
                dfu_connected=nx_tcp_server_socket_accept(&dfu_socket,NX_NO_WAIT)==NX_SUCCESS;
                if(dfu_connected){++dfu_tcp_session;dfu_tcp_parser.used=0;}
            }
        }
        if(dfu_connected){
            NX_PACKET *packet;
            UINT r=nx_tcp_socket_receive(&dfu_socket,&packet,NX_NO_WAIT);
            if(r==NX_SUCCESS){
                ULONG offset=0;dfu_route_t route={TOF_TCP,dfu_tcp_session};
                while(offset<packet->nx_packet_length){
                    uint8_t input[64];ULONG copied=0;
                    if(nx_packet_data_extract_offset(packet,offset,input,sizeof(input),&copied)!=NX_SUCCESS || !copied)break;
                    fw_wire_feed(&dfu_tcp_parser,input,copied,dfu_command,&route);offset+=copied;
                }
                nx_packet_release(packet);
            }
            else if(r!=NX_NO_PACKET)dfu_disconnect();
        }
        dfu_usb_item_t item;
        for(unsigned i=0;i<32 && tx_queue_receive(&dfu_usb_queue,&item,TX_NO_WAIT)==TX_SUCCESS;++i){
            if(item.session!=stream_usb_session())continue;
            if(item.session!=dfu_usb_session){dfu_usb_parser.used=0;dfu_usb_session=item.session;}
            dfu_route_t route={TOF_USB,item.session};
            fw_wire_feed(&dfu_usb_parser,item.data,item.length,dfu_command,&route);
        }
        uint32_t now=HAL_GetTick();
        if(dfu_owner_route && ((now-dfu_last_activity)>60000U ||
           (dfu_owner_route==TOF_USB && dfu_owner_session!=stream_usb_session()))){
            (void)fw_abort(&dfu_update);dfu_owner_route=0;dfu_suppress_telemetry=false;
        }
        if(dfu_reset_at && (int32_t)(now-dfu_reset_at)>=0)NVIC_SystemReset();
        /* Confirmation is automatic only after the update server and USB task
         * have run for five seconds. A trial that hangs earlier is reset by
         * IWDG and rejected by FSBL on the next boot. No cable is required. */
#ifndef DEV_MODE
        bool healthy=dfu_start_result==FW_OK && socket_ready &&
                     dfu_usb_heartbeat && now-dfu_usb_heartbeat<2000U;
        if(!healthy)healthy_since=0;
        else if(!healthy_since)healthy_since=now;
        if(!confirmed && healthy_since && now-healthy_since>=5000U){
            if(fw_confirm(&dfu_update,dfu_running)==FW_OK)confirmed=true;
        }
        if(confirmed)fw_watchdog_refresh();
#endif
        tx_thread_sleep(1);
    }
}
/** @brief Create the updater's USB synchronization objects and flash owner thread. */
UINT firmware_update_init(void)
{
    UINT r=tx_mutex_create(&dfu_usb_mutex,"USB packet writer",TX_INHERIT);
    if(r!=TX_SUCCESS)return r;
    r=tx_queue_create(&dfu_usb_queue,"DFU USB input",sizeof(dfu_usb_item_t)/sizeof(ULONG),dfu_usb_storage,sizeof(dfu_usb_storage));
    if(r!=TX_SUCCESS)return r;
    dfu_initialized=true;
    return tx_thread_create(&dfu_thread,"Firmware update",dfu_thread_entry,0,dfu_stack,sizeof(dfu_stack),
                            11,11,TX_NO_TIME_SLICE,TX_AUTO_START);
}
