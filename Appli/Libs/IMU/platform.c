#include "platform.h"

int32_t platform_write(void *handle, uint8_t reg, const uint8_t *bufp, uint16_t len)
{
    HAL_GPIO_WritePin(IMU_NCS_GPIO_Port, IMU_NCS_Pin, GPIO_PIN_RESET);
    HAL_SPI_Transmit(handle, &reg, 1, 100);
    HAL_SPI_Transmit(handle, (uint8_t*) bufp, len, 100);
    HAL_GPIO_WritePin(IMU_NCS_GPIO_Port, IMU_NCS_Pin, GPIO_PIN_SET);
    return 0;
}
int32_t platform_read(void *handle, uint8_t reg, uint8_t *bufp, uint16_t len)
{
    reg |= 0x80;
    HAL_GPIO_WritePin(IMU_NCS_GPIO_Port, IMU_NCS_Pin, GPIO_PIN_RESET);
    HAL_SPI_Transmit(handle, &reg, 1, 100);
    HAL_SPI_Receive(handle, bufp, len, 100);
    HAL_GPIO_WritePin(IMU_NCS_GPIO_Port, IMU_NCS_Pin, GPIO_PIN_SET);
    return 0;
}

void platform_delay(uint32_t ms)
{
    HAL_Delay(ms - 1);
}
void platform_init(void *handle)
{
    __NOP();
    return;
}