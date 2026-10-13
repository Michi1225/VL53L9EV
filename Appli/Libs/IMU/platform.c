#include "platform.h"
#include <string.h>

#define IMU_SPI_MAX_PAYLOAD 128U
volatile HAL_StatusTypeDef imu_spi_status = HAL_BUSY;
volatile uint32_t imu_spi_error;
volatile uint32_t imu_spi_register;
volatile uint32_t imu_spi_operation; /* 1 = write, 2 = read */

static int32_t exchange(void *handle, uint8_t *tx, uint8_t *rx, uint16_t size)
{
    SPI_HandleTypeDef *spi = (SPI_HandleTypeDef *)handle;
    HAL_GPIO_WritePin(IMU_NCS_GPIO_Port, IMU_NCS_Pin, GPIO_PIN_RESET);
    /* Clock the command and payload in one full-duplex transaction. */
    imu_spi_status = HAL_SPI_TransmitReceive(spi, tx, rx, size, 10);
    imu_spi_error = spi->ErrorCode;
    HAL_GPIO_WritePin(IMU_NCS_GPIO_Port, IMU_NCS_Pin, GPIO_PIN_SET);
    return imu_spi_status == HAL_OK ? 0 : -1;
}

int32_t platform_write(void *handle, uint8_t reg, const uint8_t *bufp, uint16_t len)
{
    if (handle == NULL || bufp == NULL || len == 0 || len > IMU_SPI_MAX_PAYLOAD)
        return -1;
    uint8_t tx[IMU_SPI_MAX_PAYLOAD + 1U] = {0};
    uint8_t rx[IMU_SPI_MAX_PAYLOAD + 1U];
    tx[0] = reg & 0x7FU;
    memcpy(tx + 1, bufp, len);
    imu_spi_register = reg;
    imu_spi_operation = 1;
    return exchange(handle, tx, rx, len + 1U);
}

int32_t platform_read(void *handle, uint8_t reg, uint8_t *bufp, uint16_t len)
{
    if (handle == NULL || bufp == NULL || len == 0 || len > IMU_SPI_MAX_PAYLOAD)
        return -1;
    uint8_t tx[IMU_SPI_MAX_PAYLOAD + 1U] = {0};
    uint8_t rx[IMU_SPI_MAX_PAYLOAD + 1U];
    tx[0] = reg | 0x80U;
    imu_spi_register = reg;
    imu_spi_operation = 2;
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
