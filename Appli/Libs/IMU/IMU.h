#ifndef __IMU_H_
#define __IMU_H_

#include "main.h"
#include "lsm6dsv16x_reg.h"
#include "platform.h"
#include "stm32n6xx_hal_def.h"
#include <string.h>

#define IMU_SPI_HANDLE (&hspi4)

#define BOOT_TIME      (10)
#define FIFO_WATERMARK (32)

#define IMU_INIT_TIMEOUT_MS (1000)

typedef struct 
{
    uint8_t whoamI;
    uint8_t tx_buf[128];
    stmdev_ctx_t dev_ctx;
    lsm6dsv16x_fifo_sflp_raw_t fifo_sflp;
    lsm6dsv16x_fifo_status_t fifo_status;
    lsm6dsv16x_fifo_out_raw_t f_data;

    float_t quat[4];
    uint32_t timestamp_quat;
    float_t acc_mg[3];
    uint32_t timestamp_acc;
    float_t gyro_mdps[3];
    uint32_t timestamp_gyro;
} IMU;

HAL_StatusTypeDef imu_init(IMU *imu);
HAL_StatusTypeDef imu_get_data_blocking(IMU *imu);


#endif //__IMU_H_