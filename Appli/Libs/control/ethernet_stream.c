#include "ethernet_stream.h"
#include "ethernet_link.h"
#include "stream_manager.h"
#include "imu_stream.h"
#include "app_netxduo.h"
#include "app_events.h"
#include "device_parameters.h"
#include "thread_profiler.h"
#include "tof_control.h"

/* This thread exclusively consumes the producer buffers, sends whole packets
 * to each active transport, then returns ownership to the camera producer. */
static NX_TCP_SOCKET stream_socket;
static bool client_connected;
static uint32_t client_session;
static tof_parser_t command_parser;
/** @brief Disconnect the stream client and discard its partial command. */
static void disconnect_client(void)
{
    nx_tcp_socket_disconnect(&stream_socket, TX_NO_WAIT);
    nx_tcp_server_socket_unaccept(&stream_socket);
    client_connected = false;
    command_parser.used = 0;
}
/** @brief Send stream bytes through bounded TCP packets, returning the first transport error. */
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
/** @brief Read available TCP data and feed the current client's ToF command parser. */
static void read_commands(void)
{
    for (unsigned i = 0; client_connected && i < 4U; ++i)
    {
        NX_PACKET *packet;
        UINT status = nx_tcp_socket_receive(&stream_socket, &packet, TX_NO_WAIT);
        if (status == NX_NO_PACKET) break;
        if (status != NX_SUCCESS) { disconnect_client(); break; }
        ULONG offset = 0;
        while (offset < packet->nx_packet_length)
        {
            UCHAR input[64];
            ULONG copied = 0;
            if (nx_packet_data_extract_offset(packet, offset, input, sizeof(input), &copied) != NX_SUCCESS || !copied)
                break;
            tof_control_feed(&command_parser, input, copied, TOF_TCP, client_session);
            offset += copied;
        }
        nx_packet_release(packet);
    }
}
/** @brief Send a complete telemetry packet to USB and the connected TCP client. */
static void write_transports(UCHAR *data, ULONG size)
{
    (void)stream_usb_write(data, size);
    if (client_connected && tcp_write(data, size) != NX_SUCCESS)
        disconnect_client(); /* A partial frame must end this TCP session. */
}
/** @brief Manage stream clients and forward sensor packets, metadata, and command replies. */
void ethernet_stream_thread_entry(ULONG argument)
{
    (void)argument;
    NX_IP *ip = nx_get_ip();
    bool socket_ready = nx_tcp_socket_create(ip, &stream_socket, "Sensor stream",
        NX_IP_NORMAL, NX_FRAGMENT_OKAY, NX_IP_TIME_TO_LIVE, 8192, NX_NULL, NX_NULL) == NX_SUCCESS;
    bool listening = false;
    uint32_t link_generation = 0;
    ULONG last_parameters = tx_time_get() - 2U * TX_TIMER_TICKS_PER_SECOND;
    ULONG last_profile = tx_time_get();
    ULONG last_status = tx_time_get() - TX_TIMER_TICKS_PER_SECOND;
    for (;;)
    {
        ULONG link_flags = 0;
        tx_event_flags_get(&app_events, APP_EVT_ETH_IP_READY, TX_AND, &link_flags, TX_NO_WAIT);
        uint32_t current_generation = ethernet_link_generation();
        if (listening && (!(link_flags & APP_EVT_ETH_IP_READY) || current_generation != link_generation))
        {
            /* Unaccept even a half-open SYN_RECEIVED socket before unlisten.
             * The generation catches an entire cable cycle during a USB write. */
            disconnect_client();
            nx_tcp_server_socket_unlisten(ip, ETHERNET_STREAM_PORT);
            listening = false;
        }
        link_generation = current_generation;
        if (client_connected && stream_socket.nx_tcp_socket_state != NX_TCP_ESTABLISHED)
            disconnect_client();
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
                if (client_connected) { ++client_session; command_parser.used = 0; }
            }
        }
        read_commands();
        tof_reply_t reply;
        for (unsigned i = 0; i < 8U && tof_control_next_reply(&reply); ++i)
        {
            if (reply.route == TOF_USB && reply.session == stream_usb_session())
                (void)stream_usb_write(reply.packet, sizeof(reply.packet));
            else if (reply.route == TOF_TCP && client_connected && reply.session == client_session)
            {
                if (tcp_write(reply.packet, sizeof(reply.packet)) != NX_SUCCESS) disconnect_client();
            }
        }
        /* Repeat metadata for late USB opens and reconnects on either link. */
        ULONG now = tx_time_get();
        if ((ULONG)(now - last_parameters) >= 2U * TX_TIMER_TICKS_PER_SECOND)
        {
            uint32_t size;
            UCHAR *parameters = device_parameters_packet(&size);
            if (size) write_transports(parameters, size);
            last_parameters = now;
        }
        if ((ULONG)(now - last_status) >= TX_TIMER_TICKS_PER_SECOND)
        {
            UCHAR status_packet[TOF_STATUS_PACKET_SIZE];
            tof_control_status_packet(status_packet);
            write_transports(status_packet, sizeof(status_packet));
            last_status = now;
        }
        if ((ULONG)(now - last_profile) >= TX_TIMER_TICKS_PER_SECOND)
        {
            uint32_t size;
            UCHAR *profile = thread_profiler_packet(&size);
            if (size) write_transports(profile, size);
            last_profile = now;
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
