#include "control_server.h"
#include "tx_api.h"
#include "tx_port.h"
#include "ADIN1300.h"



static NX_TCP_SOCKET control_socket;

static void Control_HandleClient(NX_TCP_SOCKET *socket)
{
    UINT status;
    NX_PACKET *packet;

    while (1)
    {
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

static UINT Ethernet_TxTest(void)
{
    NX_UDP_SOCKET udp_socket;
    NX_PACKET *packet_ptr = NX_NULL;
    NX_PACKET_POOL *pool = nx_get_app_pool();
    NX_IP *ip = nx_get_ip();
    UINT status;

    status = nx_udp_socket_create(
        ip,
        &udp_socket,
        "TX Test",
        NX_IP_NORMAL,
        NX_FRAGMENT_OKAY,
        NX_IP_TIME_TO_LIVE,
        512);

    if (status != NX_SUCCESS)
        return status;

    status = nx_udp_socket_bind(
        &udp_socket,
        NX_ANY_PORT,
        TX_NO_WAIT);

    if (status != NX_SUCCESS)
        return status;

    status = nx_packet_allocate(
        pool,
        &packet_ptr,
        NX_UDP_PACKET,
        TX_NO_WAIT);

    if (status != NX_SUCCESS)
        return status;

    static const char msg[] = "STM32 TX TEST";

    status = nx_packet_data_append(
        packet_ptr,
        (VOID *)msg,
        sizeof(msg) - 1,
        pool,
        TX_NO_WAIT);

    if (status != NX_SUCCESS)
    {
        nx_packet_release(packet_ptr);
        return status;
    }

    status = nx_udp_socket_send(
        &udp_socket,
        packet_ptr,
        IP_ADDRESS(192, 168, 1, 10),
        5001);

    return status;
}


void control_thread_entry(ULONG thread_input)
{
    ULONG flags;
    ULONG events;
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
            volatile UINT tx_test_status = Ethernet_TxTest();

            // Ethernet link disappeared
            if ((events & APP_EVT_ETH_IP_READY) == 0U)
            {
                break;
            }

            volatile uint32_t dmamr      = ETH1->DMAMR;
            volatile uint32_t dmasbmr    = ETH1->DMASBMR;
            volatile uint32_t mtlrxmap   = ETH1->MTLRXQDMAMR;

            volatile uint32_t q0rxomr =
                ETH1->MTL_QUEUE[0].MTLRXQOMR;

            volatile uint32_t q1rxomr =
                ETH1->MTL_QUEUE[1].MTLRXQOMR;

            volatile uint32_t ch0_rxcr =
                ETH1->DMA_CH[0].DMACRXCR;

            volatile uint32_t ch1_rxcr =
                ETH1->DMA_CH[1].DMACRXCR;

            volatile uint32_t ch0_sr =
                ETH1->DMA_CH[0].DMACSR;

            volatile uint32_t ch1_sr =
                ETH1->DMA_CH[1].DMACSR;

            volatile uint32_t dmad_sr =
                ETH1->DMADSR;
            

            status = nx_tcp_server_socket_accept(
                &control_socket,
                CONTROL_ACCEPT_TIMEOUT);

            if (status == NX_SUCCESS)
            {
                /*
                 * Client connected.
                 */
                Control_HandleClient(&control_socket);

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
        nx_tcp_server_socket_unlisten(
            ip,
            CONTROL_SERVER_PORT);
    }
}