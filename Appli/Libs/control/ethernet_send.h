#ifndef ETHERNET_SEND_H
#define ETHERNET_SEND_H
#include "nx_api.h"
/** @brief Send one framed message with bounded backpressure retries and exact packet ownership. */
UINT ethernet_send(NX_TCP_SOCKET *socket, const UCHAR *data, ULONG length);
#endif
