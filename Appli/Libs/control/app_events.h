#ifndef APP_EVENTS_H
#define APP_EVENTS_H

#include "tx_api.h"



#define APP_EVT_ETH_LINK_UP           (1UL << 0)
#define APP_EVT_ETH_IP_READY          (1UL << 1)

extern TX_EVENT_FLAGS_GROUP app_events;

#endif