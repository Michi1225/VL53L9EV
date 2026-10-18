#include "ethernet_link.h"
#include "app_netxduo.h"
#include "app_events.h"
#include <stdbool.h>

static volatile uint32_t generation;

uint32_t ethernet_link_generation(void) { return generation; }

void ethernet_link_thread_entry(ULONG argument)
{
    (void)argument;
    NX_IP *ip = nx_get_ip();
    bool ready = false;
    /* Reconcile NetX's boot attempt, and retry a failed stop before re-enable. */
    bool disable_pending = true;
    for (;;)
    {
        ULONG physical = 0, unused = 0;
        UINT status = nx_ip_status_check(ip, NX_IP_LINK_ENABLED, &physical, TX_NO_WAIT);
        bool up = status == NX_SUCCESS && (physical & NX_IP_LINK_ENABLED);
        if (!up)
        {
            /* This driver's GET_STATUS reports the PHY only. It neither stops
             * DMA nor clears NetX's software link flag when the cable is lost. */
            tx_event_flags_set(&app_events,
                (ULONG)~(APP_EVT_ETH_LINK_UP | APP_EVT_ETH_IP_READY), TX_AND);
            ready = false;
            if (disable_pending &&
                nx_ip_driver_direct_command(ip, NX_LINK_DISABLE, &unused) == NX_SUCCESS)
                disable_pending = false;
        }
        else if (!ready)
        {
            if (disable_pending &&
                nx_ip_driver_direct_command(ip, NX_LINK_DISABLE, &unused) == NX_SUCCESS)
                disable_pending = false;
            /* NetX's initial LINK_ENABLE can fail with no cable. Retry here
             * after insertion, configuring the MAC from the negotiated speed. */
            if (!disable_pending)
            {
                status = nx_ip_driver_direct_command(ip, NX_LINK_ENABLE, &unused);
                if (status == NX_SUCCESS || status == NX_ALREADY_ENABLED)
                {
                    ready = true;
                    disable_pending = true;
                    ++generation;
                    /* Static IPv4: publish readiness only after MAC/DMA enable. */
                    tx_event_flags_set(&app_events,
                        APP_EVT_ETH_LINK_UP | APP_EVT_ETH_IP_READY, TX_OR);
                }
            }
        }
        tx_thread_sleep(TX_TIMER_TICKS_PER_SECOND / 5U);
    }
}
