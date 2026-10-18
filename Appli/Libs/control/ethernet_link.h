#ifndef ETHERNET_LINK_H
#define ETHERNET_LINK_H

#include "tx_api.h"
#include <stdint.h>

/* Runs in the existing NetXDuo application thread. */
void ethernet_link_thread_entry(ULONG argument);
/* Changes on each successful link enable, including a down/up while a
 * transport thread was blocked. Consumers must discard old TCP sessions. */
uint32_t ethernet_link_generation(void);

#endif
