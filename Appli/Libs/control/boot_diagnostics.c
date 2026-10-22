#include "boot_diagnostics.h"
#include "LEDController.h"

volatile app_boot_stage_t app_boot_stage = APP_BOOT_HAL;
volatile app_boot_stage_t app_boot_failed_stage;
volatile uint32_t app_boot_error_return_address;

void app_boot_report_failure(void)
{
    app_boot_failed_stage = app_boot_stage;
    uint32_t stage = (uint32_t)app_boot_failed_stage;
    uint8_t pulses = stage & 15U;
    LED_DiagnosticColor_t color = LED_DIAGNOSTIC_RED;
    if ((stage >> 4) == 2U) color = LED_DIAGNOSTIC_BLUE;
    if ((stage >> 4) == 3U) color = LED_DIAGNOSTIC_MAGENTA;
    if (!pulses || pulses > 14U) pulses = 1;
    (void)LEDController_SetFaultCode(&led_controller, color, pulses);
}
