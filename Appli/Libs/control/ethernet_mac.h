#ifndef ETHERNET_MAC_H
#define ETHERNET_MAC_H
#include <stdint.h>
/** @brief Derive a stable locally administered unicast MAC from the 96-bit MCU UID. */
static inline void ethernet_mac(const uint32_t uid[3], uint8_t mac[6])
{
    uint64_t hash = UINT64_C(14695981039346656037);
    for (unsigned w = 0; w < 3; ++w)
        for (unsigned b = 0; b < 4; ++b) { hash ^= (uid[w] >> (8 * b)) & 255U; hash *= UINT64_C(1099511628211); }
    mac[0] = 0x02;
    for (unsigned b = 1; b < 6; ++b) mac[b] = (uint8_t)(hash >> (8 * (b - 1)));
}
#endif
