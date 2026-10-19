#include "IMU.h"
#include "lsm6dsv16x_reg.h"
#include "platform.h"
#include "stm32n6xx_hal_def.h"
#include <math.h>

/** @brief Convert an IMU half-precision sample into a single-precision value. */
static float_t npy_half_to_float(uint16_t h)
{
    union { float_t ret; uint32_t retbits; } conv;
    conv.retbits = lsm6dsv16x_from_f16_to_f32(h);
    return conv.ret;
}

/** @brief Expand the SFLP rotation vector into a normalized xyzw quaternion. */
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

#define IMU_INIT_CALL(call) do { if ((call) != 0) return HAL_ERROR; } while (0)

/** @brief Initialize the IMU transport, sampling, FIFO, and SFLP game rotation output. */
HAL_StatusTypeDef imu_init(IMU *imu)
{
    memset(imu, 0, sizeof(*imu));

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
        if (lsm6dsv16x_device_id_get(dev_ctx, &(imu->whoamI)) != 0)
            return HAL_ERROR;
        platform_delay(1);
        ++timeout;
        if(timeout > IMU_INIT_TIMEOUT_MS) return HAL_TIMEOUT;
    }while (imu->whoamI != LSM6DSV16X_ID);

    // Reset Device
    IMU_INIT_CALL(lsm6dsv16x_sw_por(dev_ctx));

    // Output registers are not updated until LSB and MSB have been read
    IMU_INIT_CALL(lsm6dsv16x_block_data_update_set(dev_ctx, PROPERTY_ENABLE));

    IMU_INIT_CALL(lsm6dsv16x_xl_full_scale_set(dev_ctx, LSM6DSV16X_4g));
    IMU_INIT_CALL(lsm6dsv16x_gy_full_scale_set(dev_ctx, LSM6DSV16X_1000dps));
    IMU_INIT_CALL(lsm6dsv16x_fifo_xl_batch_set(dev_ctx, LSM6DSV16X_XL_BATCHED_AT_120Hz));
    IMU_INIT_CALL(lsm6dsv16x_fifo_gy_batch_set(dev_ctx, LSM6DSV16X_GY_BATCHED_AT_120Hz));

    IMU_INIT_CALL(lsm6dsv16x_fifo_watermark_set(dev_ctx, FIFO_WATERMARK));
    IMU_INIT_CALL(lsm6dsv16x_fifo_compress_algo_set(dev_ctx, LSM6DSV16X_CMP_DISABLE));

    imu->fifo_sflp.game_rotation = 1;
    imu->fifo_sflp.gbias = 0;
    imu->fifo_sflp.gravity = 0;
    IMU_INIT_CALL(lsm6dsv16x_fifo_sflp_batch_set(dev_ctx, imu->fifo_sflp));

    IMU_INIT_CALL(lsm6dsv16x_fifo_mode_set(dev_ctx, LSM6DSV16X_STREAM_MODE));

    /* SFLP has no 100 Hz setting. Sample at 120 Hz, publish at 100 Hz. */
    IMU_INIT_CALL(lsm6dsv16x_xl_data_rate_set(dev_ctx, LSM6DSV16X_ODR_AT_120Hz));
    IMU_INIT_CALL(lsm6dsv16x_gy_data_rate_set(dev_ctx, LSM6DSV16X_ODR_AT_120Hz));
    IMU_INIT_CALL(lsm6dsv16x_sflp_data_rate_set(dev_ctx, LSM6DSV16X_SFLP_120Hz));

    IMU_INIT_CALL(lsm6dsv16x_sflp_game_rotation_set(dev_ctx, PROPERTY_ENABLE));

    return HAL_OK;
}

#undef IMU_INIT_CALL

/** @brief Flush the IMU FIFO and clear sample validity before resuming streaming. */
HAL_StatusTypeDef imu_reset_fifo(IMU *imu)
{
    if (lsm6dsv16x_fifo_mode_set(&imu->dev_ctx, LSM6DSV16X_BYPASS_MODE) != 0)
        return HAL_ERROR;
    imu->valid_data = 0;
    if (lsm6dsv16x_fifo_mode_set(&imu->dev_ctx, LSM6DSV16X_STREAM_MODE) != 0)
        return HAL_ERROR;
    return HAL_OK;
}

/** @brief Drain available FIFO entries into the latest acceleration, gyro, and quaternion samples. */
HAL_StatusTypeDef imu_get_data_blocking(IMU *imu)
{
    int32_t errorcode = 0;

    stmdev_ctx_t *dev_ctx = &(imu->dev_ctx);
    lsm6dsv16x_fifo_out_raw_t * f_data = &(imu->f_data);
    errorcode |= lsm6dsv16x_fifo_status_get(dev_ctx, &(imu->fifo_status));
    if (errorcode != 0) return HAL_ERROR;
    /* Discontinuity requires a FIFO flush, rather than stopping sampling. */
    if (imu->fifo_status.fifo_ovr) return HAL_BUSY;

    uint16_t fifo_level = imu->fifo_status.fifo_level;

    while(fifo_level > 0)
    {
        errorcode |= lsm6dsv16x_fifo_out_raw_get(dev_ctx, f_data);
        if (errorcode != 0) return HAL_ERROR;
        int16_t raw[3];
        memcpy(raw, f_data->data, sizeof(raw));

        switch(f_data->tag)
        {
            case LSM6DSV16X_XL_NC_TAG:
                for (unsigned i = 0; i < 3; ++i)
                    imu->acc_mg[i] = lsm6dsv16x_from_fs4_to_mg(raw[i]);
                imu->valid_data |= IMU_DATA_ACC;
                imu->timestamp_acc = HAL_GetTick();
                break;
            case LSM6DSV16X_GY_NC_TAG:
                for (unsigned i = 0; i < 3; ++i)
                    imu->gyro_mdps[i] = lsm6dsv16x_from_fs1000_to_mdps(raw[i]);
                imu->valid_data |= IMU_DATA_GYRO;
                imu->timestamp_gyro = HAL_GetTick();
                break;
            case LSM6DSV16X_SFLP_GAME_ROTATION_VECTOR_TAG:
                {
                    uint16_t rotation[3];
                    memcpy(rotation, f_data->data, sizeof(rotation));
                    sflp2q(imu->quat, rotation);
                }
                imu->valid_data |= IMU_DATA_SFLP;
                imu->timestamp_quat = HAL_GetTick();
                break;
            default:
                break;
        }

        --fifo_level;
    }

    return HAL_OK;
}
