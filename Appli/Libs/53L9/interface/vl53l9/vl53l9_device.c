/**
 ******************************************************************************
 * @file    vl53l9_device.c
 * @author  IMD Software Team
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2026 STMicroelectronics.
 * All rights reserved.
 *
 * This software is licensed under terms that can be found in the LICENSE file
 * in the root directory of this software component.
 * If no LICENSE file comes with this software, it is provided AS-IS.
 *
 ******************************************************************************
 */

#include "vl53l9_device.h"
#include "main.h"

extern I3C_HandleTypeDef hi3c2;

vl53l9_device_t device[NB_DEVICES] = {
#ifdef CONFIG_HW_STEVAL_MIPI
    { .bus = &hi3c2,
      .bus_type = PLATFORM_BUS_I3C | PLATFORM_BUS_CSI,
      .address = VL53L9_DEFAULT_ADDRESS,
      .vdda = VDDA_3V3,
      .vddio = VDDIO_1V8,
      .ext_clock = 24e6, 
      .intr = { .pin = TOF_INT_1V8_Pin, .port = TOF_INT_1V8_GPIO_Port },
      .xshut = { .pin = TOF_XSHUT_Pin, .port = TOF_XSHUT_GPIO_Port } },
#endif
#ifdef CONFIG_HW_X_NUCLEO
#error "X-NUCLEO-53L9A1 hardware not supported yet"
#endif
};
