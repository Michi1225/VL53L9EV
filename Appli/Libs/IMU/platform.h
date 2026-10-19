#pragma once

#include "main.h"
#include "stm32n6xx_hal.h"
#include "stm32n6xx_hal_gpio.h"
#include "stm32n6xx_hal_spi.h"

/** @brief Write a bounded IMU register payload through SPI. */
int32_t platform_write(void *handle, uint8_t reg, const uint8_t *bufp, uint16_t len);
/** @brief Read a bounded IMU register payload through SPI. */
int32_t platform_read(void *handle, uint8_t reg, uint8_t *bufp, uint16_t len);

/** @brief Wait for the requested number of milliseconds using the HAL time base. */
void platform_delay(uint32_t ms);
/** @brief Provide the IMU initialization hook; SPI and GPIO are configured separately. */
void platform_init(void *handle);
