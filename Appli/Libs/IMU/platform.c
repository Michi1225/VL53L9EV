#include "platform.h"
#include <string.h>

#define IMU_SPI_MAX_PAYLOAD 128U

static int32_t exchange(void *handle, uint8_t *tx, uint8_t *rx, uint16_t size)
{
    SPI_HandleTypeDef *spi = (SPI_HandleTypeDef *)handle;
    HAL_GPIO_WritePin(IMU_NCS_GPIO_Port, IMU_NCS_Pin, GPIO_PIN_RESET);
    /* Clock the command and payload in one full-duplex transaction. */
    HAL_StatusTypeDef status = HAL_SPI_TransmitReceive(spi, tx, rx, size, 10);

    HAL_GPIO_WritePin(IMU_NCS_GPIO_Port, IMU_NCS_Pin, GPIO_PIN_SET);
    return status == HAL_OK ? 0 : -1;
}

int32_t platform_write(void *handle, uint8_t reg, const uint8_t *bufp, uint16_t len)
{
    if (handle == NULL || bufp == NULL || len == 0 || len > IMU_SPI_MAX_PAYLOAD)
        return -1;
    uint8_t tx[IMU_SPI_MAX_PAYLOAD + 1U] = {0};
    uint8_t rx[IMU_SPI_MAX_PAYLOAD + 1U];
    tx[0] = reg & 0x7FU;
    memcpy(tx + 1, bufp, len);

    return exchange(handle, tx, rx, len + 1U);
}

int32_t platform_read(void *handle, uint8_t reg, uint8_t *bufp, uint16_t len)
{
    if (handle == NULL || bufp == NULL || len == 0 || len > IMU_SPI_MAX_PAYLOAD)
        return -1;
    uint8_t tx[IMU_SPI_MAX_PAYLOAD + 1U] = {0};
    uint8_t rx[IMU_SPI_MAX_PAYLOAD + 1U];
    tx[0] = reg | 0x80U;

    int32_t result = exchange(handle, tx, rx, len + 1U);
    if (result == 0)
        memcpy(bufp, rx + 1, len); /* Discard the command's received byte. */
    return result;
}

void platform_delay(uint32_t ms)
{
    HAL_Delay(ms);
}
void platform_init(void *handle)
{
    (void)handle;
}
