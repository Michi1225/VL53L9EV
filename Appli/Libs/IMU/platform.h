#pragma once

#include "main.h"
#include "stm32n6xx_hal.h"
#include "stm32n6xx_hal_gpio.h"
#include "stm32n6xx_hal_spi.h"

int32_t platform_write(void *handle, uint8_t reg, const uint8_t *bufp, uint16_t len);
int32_t platform_read(void *handle, uint8_t reg, uint8_t *bufp, uint16_t len);

void platform_delay(uint32_t ms);
void platform_init(void *handle);

extern volatile HAL_StatusTypeDef imu_spi_status;
extern volatile uint32_t imu_spi_error;
extern volatile uint32_t imu_spi_register;
extern volatile uint32_t imu_spi_operation;
