#include "IMU.h"
#include "lsm6dsv16x_reg.h"
#include "platform.h"
#include "stm32n6xx_hal_def.h"
#include <math.h>

static float_t npy_half_to_float(uint16_t h)
{
    union { float_t ret; uint32_t retbits; } conv;
    conv.retbits = lsm6dsv16x_from_f16_to_f32(h);
    return conv.ret;
}

static void sflp2q(float_t quat[4], uint16_t sflp[3])
{
  float_t sumsq = 0;

  quat[0] = npy_half_to_float(sflp[0]);
  quat[1] = npy_half_to_float(sflp[1]);
  quat[2] = npy_half_to_float(sflp[2]);

  for (uint8_t i = 0; i < 3; i++)
    sumsq += quat[i] * quat[i];

  if (sumsq > 1.0f) {
    float_t n = sqrtf(sumsq);
    quat[0] /= n;
    quat[1] /= n;
    quat[2] /= n;
    sumsq = 1.0f;
  }

  quat[3] = sqrtf(1.0f - sumsq);
}

HAL_StatusTypeDef imu_init(IMU *imu)
{
    int32_t errorcode = 0;

    memset(imu->acc_mg, 0, sizeof(imu->acc_mg));
    memset(imu->gyro_mdps, 0, sizeof(imu->gyro_mdps));
    memset(imu->quat, 0, sizeof(imu->quat));

    stmdev_ctx_t *dev_ctx = &(imu->dev_ctx);
    dev_ctx->write_reg = platform_write;
    dev_ctx->read_reg  = platform_read;
    dev_ctx->mdelay    = platform_delay;
    dev_ctx->handle    = IMU_SPI_HANDLE;

    // Init platform
    platform_init(dev_ctx->handle);

    platform_delay(BOOT_TIME);

    uint16_t timeout = 0;
    do {
        lsm6dsv16x_device_id_get(dev_ctx, &(imu->whoamI));
        platform_delay(1);
        ++timeout;
        if(timeout > IMU_INIT_TIMEOUT_MS) return HAL_TIMEOUT;
    }while (imu->whoamI != LSM6DSV16X_ID);

    // Reset Device
    errorcode |= lsm6dsv16x_sw_por(dev_ctx);

    // Output registers are not updated until LSB and MSB have been read
    errorcode |= lsm6dsv16x_block_data_update_set(dev_ctx, PROPERTY_ENABLE);

    errorcode |= lsm6dsv16x_xl_full_scale_set(dev_ctx, LSM6DSV16X_4g);
    errorcode |= lsm6dsv16x_gy_full_scale_set(dev_ctx, LSM6DSV16X_1000dps);
    errorcode |= lsm6dsv16x_fifo_xl_batch_set(dev_ctx, LSM6DSV16X_XL_BATCHED_AT_480Hz);
    errorcode |= lsm6dsv16x_fifo_gy_batch_set(dev_ctx, LSM6DSV16X_GY_BATCHED_AT_480Hz);

    errorcode |= lsm6dsv16x_fifo_watermark_set(dev_ctx, FIFO_WATERMARK);
    errorcode |= lsm6dsv16x_fifo_compress_algo_set(dev_ctx, LSM6DSV16X_CMP_DISABLE);

    imu->fifo_sflp.game_rotation = 1;
    imu->fifo_sflp.gbias = 0;
    imu->fifo_sflp.gravity = 0;
    errorcode |= lsm6dsv16x_fifo_sflp_batch_set(dev_ctx, imu->fifo_sflp);

    errorcode |= lsm6dsv16x_fifo_mode_set(dev_ctx, LSM6DSV16X_STREAM_MODE);

    errorcode |= lsm6dsv16x_xl_data_rate_set(dev_ctx, LSM6DSV16X_ODR_AT_480Hz);
    errorcode |= lsm6dsv16x_gy_data_rate_set(dev_ctx, LSM6DSV16X_ODR_AT_480Hz);
    errorcode |= lsm6dsv16x_sflp_data_rate_set(dev_ctx, LSM6DSV16X_SFLP_480Hz);

    errorcode |= lsm6dsv16x_sflp_game_rotation_set(dev_ctx, PROPERTY_ENABLE);

    //TODO: Add Timestamps

    return errorcode;
}

HAL_StatusTypeDef imu_get_data_blocking(IMU *imu)
{
    int32_t errorcode = 0;

    stmdev_ctx_t *dev_ctx = &(imu->dev_ctx);
    lsm6dsv16x_fifo_out_raw_t * f_data = &(imu->f_data);
    int16_t *datax = (int16_t *)&f_data->data[0];
    int16_t *datay = (int16_t *)&f_data->data[2];
    int16_t *dataz = (int16_t *)&f_data->data[4];

    errorcode |= lsm6dsv16x_fifo_status_get(dev_ctx, &(imu->fifo_status));

    uint16_t fifo_level = imu->fifo_status.fifo_level;

    while(fifo_level > 0)
    {
        errorcode |= lsm6dsv16x_fifo_out_raw_get(dev_ctx, f_data);



        switch(f_data->tag)
        {
            case LSM6DSV16X_XL_NC_TAG:
                imu->acc_mg[0] = lsm6dsv16x_from_fs4_to_mg(*datax);
                imu->acc_mg[1] = lsm6dsv16x_from_fs4_to_mg(*datay);
                imu->acc_mg[2] = lsm6dsv16x_from_fs4_to_mg(*dataz);
                break;
            case LSM6DSV16X_GY_NC_TAG:
                imu->gyro_mdps[0] = lsm6dsv16x_from_fs1000_to_mdps(*datax);
                imu->gyro_mdps[1] = lsm6dsv16x_from_fs1000_to_mdps(*datay);
                imu->gyro_mdps[2] = lsm6dsv16x_from_fs1000_to_mdps(*dataz);
                break;
            case LSM6DSV16X_SFLP_GAME_ROTATION_VECTOR_TAG:
                sflp2q(imu->quat, (uint16_t *)&(f_data->data[0]));
                break;
            default:
                break;
        }

        --fifo_level;
    }

    return errorcode;
}