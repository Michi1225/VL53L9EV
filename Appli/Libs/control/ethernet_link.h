#ifndef ETHERNET_LINK_H
#define ETHERNET_LINK_H

#include "tx_api.h"
#include <stdint.h>

/* Runs in the existing NetXDuo application thread. */
/** @brief Monitor the PHY, recover MAC operation, and publish Ethernet readiness. */
void ethernet_link_thread_entry(ULONG argument);
/* Changes on each successful link enable, including a down/up while a
 * transport thread was blocked. Consumers must discard old TCP sessions. */
/** @brief Return the generation counter used to detect Ethernet reconnections. */
uint32_t ethernet_link_generation(void);
void ethernet_link_invalidate(void);

#endif
