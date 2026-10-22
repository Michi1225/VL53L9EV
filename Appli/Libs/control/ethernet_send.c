#include "ethernet_send.h"
#include "ethernet_network.h"
#include "ethernet_link.h"
#include "app_netxduo.h"
UINT ethernet_send(NX_TCP_SOCKET *socket, const UCHAR *data, ULONG length)
{
    ULONG began = tx_time_get();
    uint32_t generation = ethernet_link_generation();
    while (length) {
        ULONG count = length > 1460U ? 1460U : length;
        NX_PACKET *packet = NX_NULL;
        UINT result;
        /* A single deadline bounds the whole frame, not each of its segments. */
        for (;;) {
            if (generation != ethernet_link_generation()) { if (packet) nx_packet_release(packet); return NX_NOT_CONNECTED; }
            if ((ULONG)(tx_time_get() - began) >= 2U * TX_TIMER_TICKS_PER_SECOND) {
                if (packet) nx_packet_release(packet);
                ethernet_network_retry(true); return NX_WINDOW_OVERFLOW;
            }
            if (!packet) {
                result = nx_packet_allocate(nx_get_app_pool(), &packet, NX_TCP_PACKET, TX_NO_WAIT);
                if (result == NX_SUCCESS) {
                    result = nx_packet_data_append(packet, (VOID *)data, count, nx_get_app_pool(), TX_NO_WAIT);
                    if (result != NX_SUCCESS) { nx_packet_release(packet); packet = NX_NULL; }
                }
            } else result = NX_SUCCESS;
            if (result == NX_SUCCESS) result = nx_tcp_socket_send(socket, packet, TX_NO_WAIT);
            if (result == NX_SUCCESS) break; /* NetX owns this packet now. */
            if (result != NX_NO_PACKET && result != NX_WINDOW_OVERFLOW && result != NX_TX_QUEUE_DEPTH) {
                if (packet) nx_packet_release(packet);
                return result;
            }
            /* Retain this exact packet: NetX may already have accepted a prefix
             * and adjusted it to contain only the unsent remainder. Rebuilding
             * it would duplicate bytes and corrupt the framing. */
            ethernet_network_retry(false);
            tx_thread_sleep(TX_TIMER_TICKS_PER_SECOND / 1000U);
        }
        data += count; length -= count;
    }
    return NX_SUCCESS;
}
