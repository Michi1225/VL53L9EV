#include "status_led.h"
#include "tof_control.h"
#include "LEDController.h"

void status_led_tof(uint32_t state)
{
    LED_Animation_t animation;
    switch (state) {
    case TOF_INITIALIZING: animation = LED_ANIMATION_APP_RESETTING; break;
    case TOF_STOPPED: animation = LED_ANIMATION_APP_IDLE; break;
    case TOF_STREAMING: animation = LED_ANIMATION_APP_RANGING; break;
    case TOF_OFF: animation = LED_ANIMATION_APP_POWER_DOWN; break;
    default: animation = LED_ANIMATION_APP_FAULT; break;
    }
    (void)LEDController_SetAnimation(&led_controller, animation);
}
