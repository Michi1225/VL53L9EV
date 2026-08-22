#ifndef __VL53L9_PROCESS_H
#define __VL53L9_PROCESS_H

#include <stdio.h>
#include <stdlib.h>

#include "stm32n6xx_hal.h"
#include "vl53l9.h"
#include "vl53l9_device.h"
#include "vl53l9_interface.h"
#include "vl53l9_utils.h"

#include "stm32n6xx.h" // for SCB_InvalidateDCache_by_Addr

#define CONF_DEVICE_ID   (0) /**< select device entry in platform descriptor array (see vl53l9_device.c) */
#define CONF_USECASE     (VL53L9_USECASE_GAMING) /**< select ranging profile to be applied (see vl53l9_utils.h) */

#define CSI_OUTPUT_BUFFER_SIZE 14900 // 149 * 100 (csi_width * csi_height)

#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#define MIN(x, y) (((x) < (y)) ? (x) : (y))
#define ALIGNED32 __attribute__((aligned(32)))


typedef struct 
{
    ALIGNED32 volatile uint8_t g_csi_output_buffer[CSI_OUTPUT_BUFFER_SIZE];
    vl53l9_device_t *p_dev;
    vl53l9_profile_t *p_profile;
    vl53l9_hw_config_t hw_config;
    vl53l9_frame_t frame;

    uint8_t csi_width, csi_height;


} VL53L9_TypeDef_t;

HAL_StatusTypeDef VL53L9_init(VL53L9_TypeDef_t *tof);
HAL_StatusTypeDef VL53L9_start_ranging(VL53L9_TypeDef_t *tof);
HAL_StatusTypeDef VL53L9_process(VL53L9_TypeDef_t *tof);




#endif //__VL53L9_PROCESS_H