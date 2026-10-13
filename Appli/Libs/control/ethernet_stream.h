#ifndef ETHERNET_STREAM_H
#define ETHERNET_STREAM_H
#include "tx_api.h"
#define ETHERNET_STREAM_PORT 50002U
#define ETHERNET_STREAM_STACK_SIZE 4096U
#define ETHERNET_STREAM_PRIO 12U
void ethernet_stream_thread_entry(ULONG argument);
#endif
