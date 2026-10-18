#include "control_server.h"
#include "tx_api.h"
#include "tx_port.h"
#include "ADIN1300.h"
#include "ethernet_link.h"

static NX_TCP_SOCKET control_socket;

static void Control_HandleClient(NX_TCP_SOCKET *socket, uint32_t generation)
{
    UINT status;
    NX_PACKET *packet;

    while (1)
    {
        ULONG events = 0;
        tx_event_flags_get(&app_events, APP_EVT_ETH_IP_READY, TX_AND, &events, TX_NO_WAIT);
        if (!(events & APP_EVT_ETH_IP_READY) || generation != ethernet_link_generation())
            break;
        status = nx_tcp_socket_receive(
            socket,
            &packet,
            CONTROL_RX_TIMEOUT);

        if (status == NX_SUCCESS)
        {
            status = nx_tcp_socket_send(
                socket,
                packet,
                NX_NO_WAIT);

            if (status != NX_SUCCESS)
            {
                nx_packet_release(packet);
                break;
            }
        }
        else if (status == NX_NO_PACKET)
        {
            /* No data yet. */
            continue;
        }
        else
        {
            /*
             * Client disconnected, Ethernet disappeared,
             * or another socket error occurred.
             */
            break;
        }
    }
}

void control_thread_entry(ULONG thread_input)
{
    ULONG flags;
    UINT status;

    (void)thread_input;

    NX_IP *ip = nx_get_ip();

    status = nx_tcp_socket_create(
        ip,
        &control_socket,
        "Control Socket",
        NX_IP_NORMAL,
        NX_FRAGMENT_OKAY,
        NX_IP_TIME_TO_LIVE,
        2048U,
        NX_NULL,
        NX_NULL);

    if (status != NX_SUCCESS)
    {
        /* TODO: record fatal network error */
        return;
    }

    while (1)
    {
        /*
         * Wait until Ethernet is usable.
         */
        tx_event_flags_get(
            &app_events,
            APP_EVT_ETH_IP_READY,
            TX_AND,
            &flags,
            TX_WAIT_FOREVER);

        uint32_t generation = ethernet_link_generation();

        /*
         * Start listening.
         */
        status = nx_tcp_server_socket_listen(
            ip,
            CONTROL_SERVER_PORT,
            &control_socket,
            1U,
            NX_NULL);

        if (status != NX_SUCCESS)
        {
            tx_thread_sleep(TX_TIMER_TICKS_PER_SECOND);
            continue;
        }

        /*
         * Accept clients while Ethernet remains available.
         */
        while (1)
        {
            ULONG events = 0;
            UINT status;

            status = tx_event_flags_get(
                &app_events,
                APP_EVT_ETH_IP_READY,
                TX_AND,
                &events,
                TX_NO_WAIT);

            // Ethernet link disappeared
            if ((events & APP_EVT_ETH_IP_READY) == 0U || generation != ethernet_link_generation())
            {
                break;
            }

            status = nx_tcp_server_socket_accept(
                &control_socket,
                CONTROL_ACCEPT_TIMEOUT);

            if (status == NX_SUCCESS)
            {
                /*
                 * Client connected.
                 */
                Control_HandleClient(&control_socket, generation);

                /*
                 * Cleanly return the socket to listening state.
                 */
                nx_tcp_socket_disconnect(
                    &control_socket,
                    NX_NO_WAIT);

                nx_tcp_server_socket_unaccept(
                    &control_socket);

                nx_tcp_server_socket_relisten(
                    ip,
                    CONTROL_SERVER_PORT,
                    &control_socket);
            }
        }

        /*
         * Ethernet disappeared.
         */
        /* Cancel half-open accepts as well as established clients. */
        nx_tcp_socket_disconnect(&control_socket, NX_NO_WAIT);
        nx_tcp_server_socket_unaccept(&control_socket);
        nx_tcp_server_socket_unlisten(
            ip,
            CONTROL_SERVER_PORT);
    }
}
