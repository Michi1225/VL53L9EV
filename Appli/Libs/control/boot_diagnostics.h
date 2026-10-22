#ifndef APP_BOOT_DIAGNOSTICS_H
#define APP_BOOT_DIAGNOSTICS_H
#include <stdint.h>

/* Low nibble: pulse count. High nibble: diagnostic color group.
 * Stable values are documented in wiki/firmware/status-led.md. */
typedef enum {
    APP_BOOT_HAL = 0x01,
    APP_BOOT_CLOCKS = 0x02,
    APP_BOOT_GPIO = 0x03,
    APP_BOOT_I2C = 0x04,
    APP_BOOT_DMA = 0x05,
    APP_BOOT_DCMIPP = 0x06,
    APP_BOOT_ETHERNET = 0x07,
    APP_BOOT_UCPD = 0x08,
    APP_BOOT_USB = 0x09,
    APP_BOOT_SPI = 0x0a,
    APP_BOOT_I3C = 0x0b,
    APP_BOOT_XSPI = 0x0c,
    APP_BOOT_ISOLATION = 0x0d,
    APP_BOOT_PD_PREINIT = 0x0e,
    APP_BOOT_PARAMETERS = 0x21,
    APP_BOOT_KERNEL = 0x22,
    APP_BOOT_RUNNING = 0x23,
    APP_BOOT_TX_POOL = 0x31,
    APP_BOOT_TX_INIT = 0x32,
    APP_BOOT_NX_POOL = 0x33,
    APP_BOOT_NX_INIT = 0x34,
    APP_BOOT_UX_POOL = 0x35,
    APP_BOOT_UX_INIT = 0x36,
    APP_BOOT_PD_POOL = 0x37,
    APP_BOOT_PD_INIT = 0x38
} app_boot_stage_t;

extern volatile app_boot_stage_t app_boot_stage;
extern volatile app_boot_stage_t app_boot_failed_stage;
extern volatile uint32_t app_boot_error_return_address;

/** @brief Snapshot the last startup checkpoint and program its fatal LED code. */
void app_boot_report_failure(void);
#endif
