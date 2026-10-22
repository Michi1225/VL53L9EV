# DHCP client source provenance

Imported unchanged from Eclipse ThreadX NetX Duo tag `v6.4.0_rel`, matching
the existing STM32 NetX Duo 6.4.0 core. Only the IPv4 DHCP client is included.
Source: https://github.com/eclipse-threadx/netxduo/tree/v6.4.0_rel/addons/dhcp

Original copyright/license headers are retained; see `../../LICENSE.txt`.

SHA-256:

- `nxd_dhcp_client.c`: `7fc5cd0f6c30a5065522cc2277fb5b010ef7d2622e354ae3b4a7bef2cccd4fcf`
- `nxd_dhcp_client.h`: `f983d485dd2f4419dff295a9d54abe2a8ab93a4b5db7f5623cd36332155cb9ed`


Project integration uses the shared application packet pool and ARP probing.
The core IPv4 receiver has local fixed-header and unicast-DHCP UDP-header
length/contiguity guards, including the path described by upstream advisory
https://github.com/eclipse-threadx/netxduo/security/advisories/GHSA-c9pq-93jp-w649.
These changes do not constitute a full middleware upgrade or security audit.
