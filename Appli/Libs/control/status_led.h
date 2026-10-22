#ifndef STATUS_LED_H
#define STATUS_LED_H
#include <stdint.h>

/** @brief Display the acknowledged ToF lifecycle; call only from its owner thread. */
void status_led_tof(uint32_t state);
#endif
