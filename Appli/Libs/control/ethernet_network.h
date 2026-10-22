#ifndef ETHERNET_NETWORK_H
#define ETHERNET_NETWORK_H
#include "nx_api.h"
#include <stdbool.h>
#include <stdint.h>
#ifndef MAGNECKO_DHCP
#define MAGNECKO_DHCP 1
#endif
#define NETWORK_STATUS_SIZE 92U
enum { NET_STATIC, NET_DHCP_WAITING, NET_DHCP_BOUND, NET_STATIC_FALLBACK, NET_ERROR };
enum { NET_CLOSE_NONE, NET_CLOSE_LINK, NET_CLOSE_PEER, NET_CLOSE_RECEIVE, NET_CLOSE_SEND };
void ethernet_network_poll(NX_IP *ip, bool mac_up, bool phy_up, bool phy_error);
void ethernet_network_client(bool connected, uint32_t reason, uint32_t status);
void ethernet_network_retry(bool timeout);
void ethernet_network_status_packet(uint8_t packet[NETWORK_STATUS_SIZE]);
#endif
