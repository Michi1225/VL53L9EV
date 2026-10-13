#include "ethernet_stream.h"
#include "stream_manager.h"
#include "imu_stream.h"
#include "app_netxduo.h"
#include "app_events.h"

/* This thread exclusively consumes the producer buffers, sends whole packets
 * to each active transport, then returns ownership to the camera producer. */
static NX_TCP_SOCKET stream_socket;
static bool client_connected;
static void disconnect_client(void)
{
    nx_tcp_socket_disconnect(&stream_socket, TX_NO_WAIT);
    nx_tcp_server_socket_unaccept(&stream_socket);
    client_connected = false;
}
static UINT tcp_write(const UCHAR *data, ULONG length)
{
    while (length)
    {
        ULONG count = length > 1460U ? 1460U : length;
        NX_PACKET *packet = NX_NULL;
        UINT status = nx_packet_allocate(nx_get_app_pool(), &packet, NX_TCP_PACKET,
                                        TX_TIMER_TICKS_PER_SECOND / 4U);
        if (status != NX_SUCCESS) return status;
        status = nx_packet_data_append(packet, (VOID *)data, count,
                                       nx_get_app_pool(), TX_TIMER_TICKS_PER_SECOND / 4U);
        if (status == NX_SUCCESS)
            status = nx_tcp_socket_send(&stream_socket, packet, TX_TIMER_TICKS_PER_SECOND / 4U);
        if (status != NX_SUCCESS)
        {
            nx_packet_release(packet);
            return status;
        }
        data += count;
        length -= count;
    }
    return NX_SUCCESS;
}
static void write_transports(UCHAR *data, ULONG size)
{
    (void)stream_usb_write(data, size);
    if (client_connected && tcp_write(data, size) != NX_SUCCESS)
        disconnect_client(); /* A partial frame must end this TCP session. */
}
void ethernet_stream_thread_entry(ULONG argument)
{
    (void)argument;
    NX_IP *ip = nx_get_ip();
    bool socket_ready = nx_tcp_socket_create(ip, &stream_socket, "Sensor stream",
        NX_IP_NORMAL, NX_FRAGMENT_OKAY, NX_IP_TIME_TO_LIVE, 8192, NX_NULL, NX_NULL) == NX_SUCCESS;
    bool listening = false;
    for (;;)
    {
        if (client_connected && stream_socket.nx_tcp_socket_state != NX_TCP_ESTABLISHED)
            disconnect_client();
        ULONG link_flags = 0;
        tx_event_flags_get(&app_events, APP_EVT_ETH_IP_READY, TX_AND, &link_flags, TX_NO_WAIT);
        if (socket_ready && (link_flags & APP_EVT_ETH_IP_READY))
        {
            if (!listening)
                listening = nx_tcp_server_socket_listen(ip, ETHERNET_STREAM_PORT,
                                                        &stream_socket, 1, NX_NULL) == NX_SUCCESS;
            if (listening && !client_connected)
            {
                if (stream_socket.nx_tcp_socket_state == NX_TCP_CLOSED)
                    (void)nx_tcp_server_socket_relisten(ip, ETHERNET_STREAM_PORT, &stream_socket);
                client_connected = nx_tcp_server_socket_accept(&stream_socket, TX_NO_WAIT) == NX_SUCCESS;
            }
        }
        else if (listening)
        {
            if (client_connected) disconnect_client();
            nx_tcp_server_socket_unlisten(ip, ETHERNET_STREAM_PORT);
            listening = false;
        }
        ULONG flags = 0;
        tx_event_flags_get(&stream_events, STREAM_PACKET_READY | STREAM_IMU_READY,
                           TX_OR_CLEAR, &flags, TX_TIMER_TICKS_PER_SECOND / 100U);
        if (flags & STREAM_PACKET_READY)
        {
            write_transports(stream_packet, stream_packet_length);
            tx_event_flags_set(&stream_events, STREAM_PACKET_CONSUMED, TX_OR);
        }
        ULONG packet[IMU_PACKET_WORDS];
        for (unsigned i = 0; i < IMU_QUEUE_DEPTH; ++i)
        {
            if (tx_queue_receive(&imu_packet_queue, packet, TX_NO_WAIT) != TX_SUCCESS) break;
            write_transports((UCHAR *)packet, IMU_PACKET_SIZE);
        }
    }
}
