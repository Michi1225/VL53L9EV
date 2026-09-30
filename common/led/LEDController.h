#ifndef LEDCONTROLLER_H
#define LEDCONTROLLER_H

#include "i2c.h"
#include "main.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LED_I2C_HANDLE (&hi2c2)
#define LED_I2C_ADDRESS 0x2DU
#define LED_UPDATE_CMD 0x0FU
#define LED_START_CMD 0x10U
#define LED_STOP_CMD 0x11U
#define LED_PATTERN_REG_START 0x1CU
#define LED_REG_DC 0x14U
#define LED_DC_RED 0x05U
#define LED_DC_GREEN 0x04U
#define LED_DC_BLUE 0x06U

typedef struct __attribute__((packed))
{
    uint8_t enable : 1;
    uint8_t instablink_dis : 1;
    uint8_t : 6;

    uint8_t max_current : 1;
    uint8_t : 7;

    uint8_t out0_en : 1;
    uint8_t out1_en : 1;
    uint8_t out2_en : 1;
    uint8_t : 5;

    uint8_t out0_fade_en : 1;
    uint8_t out1_fade_en : 1;
    uint8_t out2_fade_en : 1;
    uint8_t : 1;
    uint8_t led_fade_time : 4;

    uint8_t out0_auto_en : 1;
    uint8_t out1_auto_en : 1;
    uint8_t out2_auto_en : 1;
    uint8_t : 1;
    uint8_t out0_exp_en : 1;
    uint8_t out1_exp_en : 1;
    uint8_t out2_exp_en : 1;
    uint8_t : 1;

    uint8_t out0_engine_ch : 2;
    uint8_t out1_engine_ch : 2;
    uint8_t out2_engine_ch : 2;
    uint8_t : 2;
} LED_Controller_Config_t;

typedef struct __attribute__((packed))
{
    uint8_t pause_t1 : 4;
    uint8_t pause_t0 : 4;
    uint8_t repeat : 4;
    uint8_t reserved : 4;
    uint8_t PWM[5];
    uint8_t sloper0 : 4;
    uint8_t sloper1 : 4;
    uint8_t sloper2 : 4;
    uint8_t sloper3 : 4;
} LED_Controller_Pattern_t;

typedef struct __attribute__((packed))
{
    uint8_t repetitions;
    LED_Controller_Pattern_t *patterns[4];
} LED_Controller_Engine_t;

typedef struct __attribute__((packed))
{
    uint8_t engine0_order0 : 2;
    uint8_t engine0_order1 : 2;
    uint8_t engine0_order2 : 2;
    uint8_t engine0_order3 : 2;

    uint8_t engine1_order0 : 2;
    uint8_t engine1_order1 : 2;
    uint8_t engine1_order2 : 2;
    uint8_t engine1_order3 : 2;

    uint8_t engine2_order0 : 2;
    uint8_t engine2_order1 : 2;
    uint8_t engine2_order2 : 2;
    uint8_t engine2_order3 : 2;

    uint8_t engine3_order0 : 2;
    uint8_t engine3_order1 : 2;
    uint8_t engine3_order2 : 2;
    uint8_t engine3_order3 : 2;

    uint8_t e0o0_en : 1;
    uint8_t e0o1_en : 1;
    uint8_t e0o2_en : 1;
    uint8_t e0o3_en : 1;
    uint8_t e1o0_en : 1;
    uint8_t e1o1_en : 1;
    uint8_t e1o2_en : 1;
    uint8_t e1o3_en : 1;

    uint8_t e2o0_en : 1;
    uint8_t e2o1_en : 1;
    uint8_t e2o2_en : 1;
    uint8_t e2o3_en : 1;
    uint8_t e3o0_en : 1;
    uint8_t e3o1_en : 1;
    uint8_t e3o2_en : 1;
    uint8_t e3o3_en : 1;

    uint8_t engine0_rept : 2;
    uint8_t engine1_rept : 2;
    uint8_t engine2_rept : 2;
    uint8_t engine3_rept : 2;
} LED_Controller_Engine_Config_t;

typedef struct __attribute__((packed))
{
    uint8_t enabled : 1;
    uint8_t engine_index : 2;
    uint8_t : 5;
} LED_Controller_Output_t;

typedef enum
{
    LED_ANIMATION_OFF,
    LED_ANIMATION_ALL,
    LED_ANIMATION_CONFIGURING,
    LED_ANIMATION_SENSOR_INIT_FAILED_IMU,
    LED_ANIMATION_SENSOR_INIT_FAILED_LDC,
    LED_ANIMATION_SENSOR_INIT_FAILED_TOF,
    LED_ANIMATION_SENSOR_INIT_FAILED_MAG,
    LED_ANIMATION_APPLICATION_RUNNING,
    LED_ANIMATION_UNCAUGHT_EXCEPTION,
    LED_ANIMATION_CHARGER_NOT_RESPONDING,
    LED_ANIMATION_CYAN,
    LED_ANIMATION_GREEN
} LED_Animation_t;

typedef struct
{
    LED_Controller_Pattern_t patterns[4];
    LED_Controller_Engine_t engines[4];
    LED_Controller_Config_t config;
} LEDController;

void LEDController_Init(LEDController *controller);
void LEDController_SetAnimation(LEDController *controller, LED_Animation_t animation);

static LEDController led_controller;

#ifdef __cplusplus
}
#endif

#endif