#ifndef APP_FIRMWARE_UPDATE_H
#define APP_FIRMWARE_UPDATE_H
#include "../../../common/firmware/fw_wire.h"
#include "tx_api.h"
/** @brief Create the updater's USB synchronization objects and flash owner thread. */
UINT firmware_update_init(void);
/** @brief Queue USB input for the updater while preserving the session and byte order. */
void firmware_update_usb_feed(const uint8_t *,size_t,uint32_t);
/** @brief Record that the USB task is running for trial boot health checks. */
void firmware_update_usb_alive(void);
/** @brief Acquire the mutex that keeps USB telemetry and update replies from interleaving. */
UINT firmware_update_usb_lock(void);
/** @brief Release the shared USB packet mutex. */
void firmware_update_usb_unlock(void);
/** @brief Report whether telemetry is allowed during the current upload. */
bool firmware_update_usb_telemetry(void);
#endif
