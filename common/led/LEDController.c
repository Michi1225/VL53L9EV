#include "LEDController.h"

#include <stddef.h>
#include <string.h>

static void led_set_engine_pattern(LEDController *controller,
                                  uint8_t engine_index,
                                  uint8_t engine_pattern_index,
                                  uint8_t pattern_index)
{
    if ((controller == NULL) || (engine_index >= 4U) || (engine_pattern_index >= 4U) || (pattern_index >= 4U))
    {
        return;
    }

    controller->engines[engine_index].patterns[engine_pattern_index] = &controller->patterns[pattern_index];
}

static void led_disable_engine_pattern(LEDController *controller,
                                       uint8_t engine_index,
                                       uint8_t pattern_index)
{
    if ((controller == NULL) || (engine_index >= 4U) || (pattern_index >= 4U))
    {
        return;
    }

    controller->engines[engine_index].patterns[pattern_index] = NULL;
}

static void led_stop(LEDController *controller)
{
    uint8_t txdata = 0xAAU;

    (void)controller;

    HAL_I2C_Mem_Write(LED_I2C_HANDLE, LED_I2C_ADDRESS << 1, LED_STOP_CMD, I2C_MEMADD_SIZE_8BIT, &txdata, 1U, 10);
}

static void led_start(LEDController *controller)
{
    uint8_t txdata = 0xFFU;

    (void)controller;

    HAL_I2C_Mem_Write(LED_I2C_HANDLE, LED_I2C_ADDRESS << 1, LED_START_CMD, I2C_MEMADD_SIZE_8BIT, &txdata, 1U, 10);
}

static void led_update_config(LEDController *controller)
{
    if (controller == NULL)
    {
        return;
    }

    led_stop(controller);

    struct
    {
        LED_Controller_Config_t config;
        LED_Controller_Engine_Config_t engine_config;
    } config_frame;

    memset(&config_frame, 0, sizeof(config_frame));

    config_frame.config = controller->config;

    /* Engine 0 */
    config_frame.engine_config.engine0_rept = controller->engines[0].repetitions;

    config_frame.engine_config.e0o0_en = (controller->engines[0].patterns[0] != NULL);
    if (controller->engines[0].patterns[0] == controller->patterns)        config_frame.engine_config.engine0_order0 = 0;
    else if (controller->engines[0].patterns[0] == controller->patterns + 1) config_frame.engine_config.engine0_order0 = 1;
    else if (controller->engines[0].patterns[0] == controller->patterns + 2) config_frame.engine_config.engine0_order0 = 2;
    else if (controller->engines[0].patterns[0] == controller->patterns + 3) config_frame.engine_config.engine0_order0 = 3;

    config_frame.engine_config.e0o1_en = (controller->engines[0].patterns[1] != NULL);
    if (controller->engines[0].patterns[1] == controller->patterns)        config_frame.engine_config.engine0_order1 = 0;
    else if (controller->engines[0].patterns[1] == controller->patterns + 1) config_frame.engine_config.engine0_order1 = 1;
    else if (controller->engines[0].patterns[1] == controller->patterns + 2) config_frame.engine_config.engine0_order1 = 2;
    else if (controller->engines[0].patterns[1] == controller->patterns + 3) config_frame.engine_config.engine0_order1 = 3;

    config_frame.engine_config.e0o2_en = (controller->engines[0].patterns[2] != NULL);
    if (controller->engines[0].patterns[2] == controller->patterns)        config_frame.engine_config.engine0_order2 = 0;
    else if (controller->engines[0].patterns[2] == controller->patterns + 1) config_frame.engine_config.engine0_order2 = 1;
    else if (controller->engines[0].patterns[2] == controller->patterns + 2) config_frame.engine_config.engine0_order2 = 2;
    else if (controller->engines[0].patterns[2] == controller->patterns + 3) config_frame.engine_config.engine0_order2 = 3;

    config_frame.engine_config.e0o3_en = (controller->engines[0].patterns[3] != NULL);
    if (controller->engines[0].patterns[3] == controller->patterns)        config_frame.engine_config.engine0_order3 = 0;
    else if (controller->engines[0].patterns[3] == controller->patterns + 1) config_frame.engine_config.engine0_order3 = 1;
    else if (controller->engines[0].patterns[3] == controller->patterns + 2) config_frame.engine_config.engine0_order3 = 2;
    else if (controller->engines[0].patterns[3] == controller->patterns + 3) config_frame.engine_config.engine0_order3 = 3;

    /* Engine 1 */
    config_frame.engine_config.engine1_rept = controller->engines[1].repetitions;

    config_frame.engine_config.e1o0_en = (controller->engines[1].patterns[0] != NULL);
    if (controller->engines[1].patterns[0] == controller->patterns)        config_frame.engine_config.engine1_order0 = 0;
    else if (controller->engines[1].patterns[0] == controller->patterns + 1) config_frame.engine_config.engine1_order0 = 1;
    else if (controller->engines[1].patterns[0] == controller->patterns + 2) config_frame.engine_config.engine1_order0 = 2;
    else if (controller->engines[1].patterns[0] == controller->patterns + 3) config_frame.engine_config.engine1_order0 = 3;

    config_frame.engine_config.e1o1_en = (controller->engines[1].patterns[1] != NULL);
    if (controller->engines[1].patterns[1] == controller->patterns)        config_frame.engine_config.engine1_order1 = 0;
    else if (controller->engines[1].patterns[1] == controller->patterns + 1) config_frame.engine_config.engine1_order1 = 1;
    else if (controller->engines[1].patterns[1] == controller->patterns + 2) config_frame.engine_config.engine1_order1 = 2;
    else if (controller->engines[1].patterns[1] == controller->patterns + 3) config_frame.engine_config.engine1_order1 = 3;

    config_frame.engine_config.e1o2_en = (controller->engines[1].patterns[2] != NULL);
    if (controller->engines[1].patterns[2] == controller->patterns)        config_frame.engine_config.engine1_order2 = 0;
    else if (controller->engines[1].patterns[2] == controller->patterns + 1) config_frame.engine_config.engine1_order2 = 1;
    else if (controller->engines[1].patterns[2] == controller->patterns + 2) config_frame.engine_config.engine1_order2 = 2;
    else if (controller->engines[1].patterns[2] == controller->patterns + 3) config_frame.engine_config.engine1_order2 = 3;

    config_frame.engine_config.e1o3_en = (controller->engines[1].patterns[3] != NULL);
    if (controller->engines[1].patterns[3] == controller->patterns)        config_frame.engine_config.engine1_order3 = 0;
    else if (controller->engines[1].patterns[3] == controller->patterns + 1) config_frame.engine_config.engine1_order3 = 1;
    else if (controller->engines[1].patterns[3] == controller->patterns + 2) config_frame.engine_config.engine1_order3 = 2;
    else if (controller->engines[1].patterns[3] == controller->patterns + 3) config_frame.engine_config.engine1_order3 = 3;

    /* Engine 2 */
    config_frame.engine_config.engine2_rept = controller->engines[2].repetitions;

    config_frame.engine_config.e2o0_en = (controller->engines[2].patterns[0] != NULL);
    if (controller->engines[2].patterns[0] == controller->patterns)        config_frame.engine_config.engine2_order0 = 0;
    else if (controller->engines[2].patterns[0] == controller->patterns + 1) config_frame.engine_config.engine2_order0 = 1;
    else if (controller->engines[2].patterns[0] == controller->patterns + 2) config_frame.engine_config.engine2_order0 = 2;
    else if (controller->engines[2].patterns[0] == controller->patterns + 3) config_frame.engine_config.engine2_order0 = 3;

    config_frame.engine_config.e2o1_en = (controller->engines[2].patterns[1] != NULL);
    if (controller->engines[2].patterns[1] == controller->patterns)        config_frame.engine_config.engine2_order1 = 0;
    else if (controller->engines[2].patterns[1] == controller->patterns + 1) config_frame.engine_config.engine2_order1 = 1;
    else if (controller->engines[2].patterns[1] == controller->patterns + 2) config_frame.engine_config.engine2_order1 = 2;
    else if (controller->engines[2].patterns[1] == controller->patterns + 3) config_frame.engine_config.engine2_order1 = 3;

    config_frame.engine_config.e2o2_en = (controller->engines[2].patterns[2] != NULL);
    if (controller->engines[2].patterns[2] == controller->patterns)        config_frame.engine_config.engine2_order2 = 0;
    else if (controller->engines[2].patterns[2] == controller->patterns + 1) config_frame.engine_config.engine2_order2 = 1;
    else if (controller->engines[2].patterns[2] == controller->patterns + 2) config_frame.engine_config.engine2_order2 = 2;
    else if (controller->engines[2].patterns[2] == controller->patterns + 3) config_frame.engine_config.engine2_order2 = 3;

    config_frame.engine_config.e2o3_en = (controller->engines[2].patterns[3] != NULL);
    if (controller->engines[2].patterns[3] == controller->patterns)        config_frame.engine_config.engine2_order3 = 0;
    else if (controller->engines[2].patterns[3] == controller->patterns + 1) config_frame.engine_config.engine2_order3 = 1;
    else if (controller->engines[2].patterns[3] == controller->patterns + 2) config_frame.engine_config.engine2_order3 = 2;
    else if (controller->engines[2].patterns[3] == controller->patterns + 3) config_frame.engine_config.engine2_order3 = 3;

    /* Engine 3 */
    config_frame.engine_config.engine3_rept = controller->engines[3].repetitions;

    config_frame.engine_config.e3o0_en = (controller->engines[3].patterns[0] != NULL);
    if (controller->engines[3].patterns[0] == controller->patterns)        config_frame.engine_config.engine3_order0 = 0;
    else if (controller->engines[3].patterns[0] == controller->patterns + 1) config_frame.engine_config.engine3_order0 = 1;
    else if (controller->engines[3].patterns[0] == controller->patterns + 2) config_frame.engine_config.engine3_order0 = 2;
    else if (controller->engines[3].patterns[0] == controller->patterns + 3) config_frame.engine_config.engine3_order0 = 3;

    config_frame.engine_config.e3o1_en = (controller->engines[3].patterns[1] != NULL);
    if (controller->engines[3].patterns[1] == controller->patterns)        config_frame.engine_config.engine3_order1 = 0;
    else if (controller->engines[3].patterns[1] == controller->patterns + 1) config_frame.engine_config.engine3_order1 = 1;
    else if (controller->engines[3].patterns[1] == controller->patterns + 2) config_frame.engine_config.engine3_order1 = 2;
    else if (controller->engines[3].patterns[1] == controller->patterns + 3) config_frame.engine_config.engine3_order1 = 3;

    config_frame.engine_config.e3o2_en = (controller->engines[3].patterns[2] != NULL);
    if (controller->engines[3].patterns[2] == controller->patterns)        config_frame.engine_config.engine3_order2 = 0;
    else if (controller->engines[3].patterns[2] == controller->patterns + 1) config_frame.engine_config.engine3_order2 = 1;
    else if (controller->engines[3].patterns[2] == controller->patterns + 2) config_frame.engine_config.engine3_order2 = 2;
    else if (controller->engines[3].patterns[2] == controller->patterns + 3) config_frame.engine_config.engine3_order2 = 3;

    config_frame.engine_config.e3o3_en = (controller->engines[3].patterns[3] != NULL);
    if (controller->engines[3].patterns[3] == controller->patterns)        config_frame.engine_config.engine3_order3 = 0;
    else if (controller->engines[3].patterns[3] == controller->patterns + 1) config_frame.engine_config.engine3_order3 = 1;
    else if (controller->engines[3].patterns[3] == controller->patterns + 2) config_frame.engine_config.engine3_order3 = 2;
    else if (controller->engines[3].patterns[3] == controller->patterns + 3) config_frame.engine_config.engine3_order3 = 3;

    HAL_I2C_Mem_Write(LED_I2C_HANDLE,
                      LED_I2C_ADDRESS << 1,
                      0x00,
                      I2C_MEMADD_SIZE_8BIT,
                      (uint8_t *)&config_frame,
                      sizeof(config_frame),
                      10);

    HAL_I2C_Mem_Write(LED_I2C_HANDLE,
                      LED_I2C_ADDRESS << 1,
                      LED_PATTERN_REG_START,
                      I2C_MEMADD_SIZE_8BIT,
                      (uint8_t *)&controller->patterns,
                      sizeof(controller->patterns),
                      10);

    uint8_t update = 0x55U;
    HAL_I2C_Mem_Write(LED_I2C_HANDLE,
                      LED_I2C_ADDRESS << 1,
                      LED_UPDATE_CMD,
                      I2C_MEMADD_SIZE_8BIT,
                      &update,
                      sizeof(update),
                      10);

    led_start(controller);
}

void LEDController_Init(LEDController *controller)
{
    uint8_t i;

    if (controller == NULL)
    {
        return;
    }

    memset(controller, 0, sizeof(*controller));

    controller->config.enable = 1U;
    controller->config.instablink_dis = 1U;
    controller->config.max_current = 0U;

    controller->config.out0_en = 1U;
    controller->config.out1_en = 1U;
    controller->config.out2_en = 1U;

    controller->config.out0_fade_en = 0U;
    controller->config.out1_fade_en = 0U;
    controller->config.out2_fade_en = 0U;
    controller->config.led_fade_time = 0U;

    controller->config.out0_auto_en = 1U;
    controller->config.out1_auto_en = 1U;
    controller->config.out2_auto_en = 1U;
    controller->config.out0_exp_en = 1U;
    controller->config.out1_exp_en = 1U;
    controller->config.out2_exp_en = 1U;

    controller->config.out0_engine_ch = 0U;
    controller->config.out1_engine_ch = 0U;
    controller->config.out2_engine_ch = 0U;

    for (i = 0U; i < 4U; ++i)
    {
        controller->patterns[i].pause_t0 = 0U;
        controller->patterns[i].pause_t1 = 0U;
        controller->patterns[i].repeat = 0U;
        controller->patterns[i].reserved = 0U;

        controller->patterns[i].PWM[0] = 0U;
        controller->patterns[i].PWM[1] = 0U;
        controller->patterns[i].PWM[2] = 0U;
        controller->patterns[i].PWM[3] = 0U;
        controller->patterns[i].PWM[4] = 0U;

        controller->patterns[i].sloper0 = 0U;
        controller->patterns[i].sloper1 = 0U;
        controller->patterns[i].sloper2 = 0U;
        controller->patterns[i].sloper3 = 0U;
    }

    for (i = 0U; i < 4U; ++i)
    {
        controller->engines[i].repetitions = 0U;
        controller->engines[i].patterns[0] = NULL;
        controller->engines[i].patterns[1] = NULL;
        controller->engines[i].patterns[2] = NULL;
        controller->engines[i].patterns[3] = NULL;
    }

    uint8_t txdata[3] = { LED_DC_RED, LED_DC_GREEN, LED_DC_BLUE };
    HAL_I2C_Mem_Write(LED_I2C_HANDLE, LED_I2C_ADDRESS << 1, LED_REG_DC, I2C_MEMADD_SIZE_8BIT, txdata, sizeof(txdata), 10);
}

void LEDController_SetAnimation(LEDController *controller, LED_Animation_t animation)
{
    if (controller == NULL)
    {
        return;
    }

    switch (animation)
    {
        case LED_ANIMATION_OFF:
            controller->config.out0_en = 0U;
            controller->config.out1_en = 0U;
            controller->config.out2_en = 0U;
            break;

        case LED_ANIMATION_ALL:
            controller->config.out0_en = 1U;
            controller->config.out1_en = 1U;
            controller->config.out2_en = 1U;

            controller->patterns[0].pause_t0 = 0U;
            controller->patterns[0].pause_t1 = 0U;
            controller->patterns[0].repeat = 0xFU;
            controller->patterns[0].PWM[0] = 0xFFU;
            controller->patterns[0].PWM[1] = 0xFFU;
            controller->patterns[0].PWM[2] = 0xFFU;
            controller->patterns[0].PWM[3] = 0xFFU;
            controller->patterns[0].PWM[4] = 0xFFU;
            controller->patterns[0].sloper0 = 0x01U;
            controller->patterns[0].sloper1 = 0x01U;
            controller->patterns[0].sloper2 = 0x01U;
            controller->patterns[0].sloper3 = 0x01U;

            led_set_engine_pattern(controller, 0U, 0U, 0U);
            led_disable_engine_pattern(controller, 0U, 1U);
            led_disable_engine_pattern(controller, 0U, 2U);
            led_disable_engine_pattern(controller, 0U, 3U);

            controller->config.out0_engine_ch = 0U;
            controller->config.out1_engine_ch = 0U;
            controller->config.out2_engine_ch = 0U;
            break;

        case LED_ANIMATION_CONFIGURING:
            controller->config.out0_en = 0U;
            controller->config.out1_en = 1U;
            controller->config.out2_en = 1U;

            controller->patterns[0].pause_t0 = 0U;
            controller->patterns[0].pause_t1 = 0U;
            controller->patterns[0].repeat = 0xFU;
            controller->patterns[0].PWM[0] = 0x00U;
            controller->patterns[0].PWM[1] = 0xBFU;
            controller->patterns[0].PWM[2] = 0xFFU;
            controller->patterns[0].PWM[3] = 0xBFU;
            controller->patterns[0].PWM[4] = 0x00U;
            controller->patterns[0].sloper0 = 0x01U;
            controller->patterns[0].sloper1 = 0x01U;
            controller->patterns[0].sloper2 = 0x01U;
            controller->patterns[0].sloper3 = 0x01U;

            led_set_engine_pattern(controller, 0U, 0U, 0U);
            led_disable_engine_pattern(controller, 0U, 1U);
            led_disable_engine_pattern(controller, 0U, 2U);
            led_disable_engine_pattern(controller, 0U, 3U);
            controller->config.out1_engine_ch = 0U;

            controller->patterns[1].pause_t0 = 0U;
            controller->patterns[1].pause_t1 = 0U;
            controller->patterns[1].repeat = 0xFU;
            controller->patterns[1].PWM[0] = 0xFFU;
            controller->patterns[1].PWM[1] = 0xBFU;
            controller->patterns[1].PWM[2] = 0x00U;
            controller->patterns[1].PWM[3] = 0xBFU;
            controller->patterns[1].PWM[4] = 0xFFU;
            controller->patterns[1].sloper0 = 0x01U;
            controller->patterns[1].sloper1 = 0x01U;
            controller->patterns[1].sloper2 = 0x01U;
            controller->patterns[1].sloper3 = 0x01U;

            led_set_engine_pattern(controller, 1U, 0U, 1U);
            led_disable_engine_pattern(controller, 1U, 1U);
            led_disable_engine_pattern(controller, 1U, 2U);
            led_disable_engine_pattern(controller, 1U, 3U);
            controller->config.out2_engine_ch = 1U;
            break;

        case LED_ANIMATION_SENSOR_INIT_FAILED_IMU:
            controller->config.out0_en = 1U;
            controller->config.out1_en = 0U;
            controller->config.out2_en = 0U;

            controller->patterns[0].pause_t0 = 0U;
            controller->patterns[0].pause_t1 = 0x0U;
            controller->patterns[0].repeat = 0x2U;
            controller->patterns[0].PWM[0] = 0x00U;
            controller->patterns[0].PWM[1] = 0xFFU;
            controller->patterns[0].PWM[2] = 0xFFU;
            controller->patterns[0].PWM[3] = 0x00U;
            controller->patterns[0].PWM[4] = 0x00U;
            controller->patterns[0].sloper0 = 0x01U;
            controller->patterns[0].sloper1 = 0x01U;
            controller->patterns[0].sloper2 = 0x01U;
            controller->patterns[0].sloper3 = 0x02U;
            led_set_engine_pattern(controller, 0U, 0U, 0U);

            controller->patterns[1].pause_t0 = 0xAU;
            controller->patterns[1].pause_t1 = 0x9U;
            controller->patterns[1].repeat = 0x1U;
            controller->patterns[1].PWM[0] = 0x00U;
            controller->patterns[1].PWM[1] = 0xFFU;
            controller->patterns[1].PWM[2] = 0xFFU;
            controller->patterns[1].PWM[3] = 0x00U;
            controller->patterns[1].PWM[4] = 0x00U;
            controller->patterns[1].sloper0 = 0x01U;
            controller->patterns[1].sloper1 = 0x01U;
            controller->patterns[1].sloper2 = 0x01U;
            controller->patterns[1].sloper3 = 0x08U;

            led_set_engine_pattern(controller, 0U, 1U, 1U);
            led_disable_engine_pattern(controller, 0U, 2U);
            led_disable_engine_pattern(controller, 0U, 3U);

            controller->engines[0].repetitions = 3U;
            controller->config.out0_engine_ch = 0U;
            break;

        case LED_ANIMATION_SENSOR_INIT_FAILED_LDC:
            controller->config.out0_en = 1U;
            controller->config.out1_en = 0U;
            controller->config.out2_en = 0U;

            controller->patterns[0].pause_t0 = 0x05U;
            controller->patterns[0].pause_t1 = 0x05U;
            controller->patterns[0].repeat = 0x2U;
            controller->patterns[0].PWM[0] = 0x00U;
            controller->patterns[0].PWM[1] = 0xFFU;
            controller->patterns[0].PWM[2] = 0xFFU;
            controller->patterns[0].PWM[3] = 0x00U;
            controller->patterns[0].PWM[4] = 0x00U;
            controller->patterns[0].sloper0 = 0x01U;
            controller->patterns[0].sloper1 = 0x01U;
            controller->patterns[0].sloper2 = 0x01U;
            controller->patterns[0].sloper3 = 0x02U;
            led_set_engine_pattern(controller, 0U, 0U, 0U);

            led_disable_engine_pattern(controller, 0U, 1U);
            led_disable_engine_pattern(controller, 0U, 2U);
            led_disable_engine_pattern(controller, 0U, 3U);

            controller->engines[0].repetitions = 3U;
            controller->config.out0_engine_ch = 0U;
            break;

        case LED_ANIMATION_SENSOR_INIT_FAILED_TOF:
            controller->config.out0_en = 1U;
            controller->config.out1_en = 0U;
            controller->config.out2_en = 0U;

            controller->patterns[0].pause_t0 = 0x00U;
            controller->patterns[0].pause_t1 = 0x0AU;
            controller->patterns[0].repeat = 0x2U;
            controller->patterns[0].PWM[0] = 0x00U;
            controller->patterns[0].PWM[1] = 0xFFU;
            controller->patterns[0].PWM[2] = 0xFFU;
            controller->patterns[0].PWM[3] = 0x00U;
            controller->patterns[0].PWM[4] = 0x00U;
            controller->patterns[0].sloper0 = 0x01U;
            controller->patterns[0].sloper1 = 0x01U;
            controller->patterns[0].sloper2 = 0x01U;
            controller->patterns[0].sloper3 = 0x02U;
            led_set_engine_pattern(controller, 0U, 0U, 0U);

            controller->patterns[1].pause_t0 = 0x00U;
            controller->patterns[1].pause_t1 = 0x05U;
            controller->patterns[1].repeat = 0x03U;
            controller->patterns[1].PWM[0] = 0x00U;
            controller->patterns[1].PWM[1] = 0xFFU;
            controller->patterns[1].PWM[2] = 0xFFU;
            controller->patterns[1].PWM[3] = 0x00U;
            controller->patterns[1].PWM[4] = 0x00U;
            controller->patterns[1].sloper0 = 0x01U;
            controller->patterns[1].sloper1 = 0x01U;
            controller->patterns[1].sloper2 = 0x01U;
            controller->patterns[1].sloper3 = 0x02U;
            led_set_engine_pattern(controller, 0U, 1U, 1U);

            led_disable_engine_pattern(controller, 0U, 2U);
            led_disable_engine_pattern(controller, 0U, 3U);

            controller->engines[0].repetitions = 3U;
            controller->config.out0_engine_ch = 0U;
            break;

        case LED_ANIMATION_SENSOR_INIT_FAILED_MAG:
            controller->config.out0_en = 1U;
            controller->config.out1_en = 0U;
            controller->config.out2_en = 0U;

            controller->patterns[0].pause_t0 = 0x00U;
            controller->patterns[0].pause_t1 = 0x0AU;
            controller->patterns[0].repeat = 0x2U;
            controller->patterns[0].PWM[0] = 0x00U;
            controller->patterns[0].PWM[1] = 0xFFU;
            controller->patterns[0].PWM[2] = 0xFFU;
            controller->patterns[0].PWM[3] = 0x00U;
            controller->patterns[0].PWM[4] = 0x00U;
            controller->patterns[0].sloper0 = 0x01U;
            controller->patterns[0].sloper1 = 0x01U;
            controller->patterns[0].sloper2 = 0x01U;
            controller->patterns[0].sloper3 = 0x02U;
            led_set_engine_pattern(controller, 0U, 0U, 0U);

            controller->patterns[1].pause_t0 = 0x00U;
            controller->patterns[1].pause_t1 = 0x00U;
            controller->patterns[1].repeat = 0x01U;
            controller->patterns[1].PWM[0] = 0x00U;
            controller->patterns[1].PWM[1] = 0xFFU;
            controller->patterns[1].PWM[2] = 0xFFU;
            controller->patterns[1].PWM[3] = 0xFFU;
            controller->patterns[1].PWM[4] = 0x00U;
            controller->patterns[1].sloper0 = 0x02U;
            controller->patterns[1].sloper1 = 0x08U;
            controller->patterns[1].sloper2 = 0x08U;
            controller->patterns[1].sloper3 = 0x02U;
            led_set_engine_pattern(controller, 0U, 1U, 1U);

            led_disable_engine_pattern(controller, 0U, 2U);
            led_disable_engine_pattern(controller, 0U, 3U);

            controller->engines[0].repetitions = 3U;
            controller->config.out0_engine_ch = 0U;
            break;

        case LED_ANIMATION_APPLICATION_RUNNING:
        case LED_ANIMATION_GREEN:
            controller->config.out0_en = 0U;
            controller->config.out1_en = 1U;
            controller->config.out2_en = 0U;

            controller->patterns[0].pause_t0 = 0U;
            controller->patterns[0].pause_t1 = 0U;
            controller->patterns[0].repeat = 0xFU;
            controller->patterns[0].PWM[0] = 0xFFU;
            controller->patterns[0].PWM[1] = 0xFFU;
            controller->patterns[0].PWM[2] = 0xFFU;
            controller->patterns[0].PWM[3] = 0xFFU;
            controller->patterns[0].PWM[4] = 0xFFU;
            controller->patterns[0].sloper0 = 0x01U;
            controller->patterns[0].sloper1 = 0x01U;
            controller->patterns[0].sloper2 = 0x01U;
            controller->patterns[0].sloper3 = 0x01U;
            led_set_engine_pattern(controller, 0U, 0U, 0U);

            led_disable_engine_pattern(controller, 0U, 1U);
            led_disable_engine_pattern(controller, 0U, 2U);
            led_disable_engine_pattern(controller, 0U, 3U);

            controller->engines[0].repetitions = 0x3U;
            controller->config.out1_engine_ch = 0U;
            break;

        case LED_ANIMATION_CHARGER_NOT_RESPONDING:
        case LED_ANIMATION_UNCAUGHT_EXCEPTION:
            controller->config.out0_en = 1U;
            controller->config.out1_en = 0U;
            controller->config.out2_en = 0U;

            controller->patterns[0].pause_t0 = 0U;
            controller->patterns[0].pause_t1 = 0U;
            controller->patterns[0].repeat = 0xFU;
            controller->patterns[0].PWM[0] = 0x00U;
            controller->patterns[0].PWM[1] = 0xFFU;
            controller->patterns[0].PWM[2] = 0xFFU;
            controller->patterns[0].PWM[3] = 0x00U;
            controller->patterns[0].PWM[4] = 0x00U;
            controller->patterns[0].sloper0 = 0x01U;
            controller->patterns[0].sloper1 = 0x01U;
            controller->patterns[0].sloper2 = 0x01U;
            controller->patterns[0].sloper3 = 0x01U;
            led_set_engine_pattern(controller, 0U, 0U, 0U);

            led_disable_engine_pattern(controller, 0U, 1U);
            led_disable_engine_pattern(controller, 0U, 2U);
            led_disable_engine_pattern(controller, 0U, 3U);

            controller->engines[0].repetitions = 0x3U;
            controller->config.out0_engine_ch = 0U;
            break;

        case LED_ANIMATION_CYAN:
            controller->config.out0_en = 0U;
            controller->config.out1_en = 1U;
            controller->config.out2_en = 1U;

            controller->patterns[0].pause_t0 = 0U;
            controller->patterns[0].pause_t1 = 0U;
            controller->patterns[0].repeat = 0xFU;
            controller->patterns[0].PWM[0] = 0xFFU;
            controller->patterns[0].PWM[1] = 0xFFU;
            controller->patterns[0].PWM[2] = 0xFFU;
            controller->patterns[0].PWM[3] = 0xFFU;
            controller->patterns[0].PWM[4] = 0xFFU;
            controller->patterns[0].sloper0 = 0x01U;
            controller->patterns[0].sloper1 = 0x01U;
            controller->patterns[0].sloper2 = 0x01U;
            controller->patterns[0].sloper3 = 0x01U;
            led_set_engine_pattern(controller, 0U, 0U, 0U);

            led_disable_engine_pattern(controller, 0U, 1U);
            led_disable_engine_pattern(controller, 0U, 2U);
            led_disable_engine_pattern(controller, 0U, 3U);

            controller->engines[0].repetitions = 0x3U;
            controller->config.out1_engine_ch = 0U;
            controller->config.out2_engine_ch = 0U;
            break;

        default:
            break;
    }

    led_update_config(controller);
}