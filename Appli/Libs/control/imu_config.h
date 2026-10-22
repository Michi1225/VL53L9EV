#ifndef IMU_CONFIG_H
#define IMU_CONFIG_H
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    uint32_t sample_hz, publish_hz, acc_g, gyro_dps;
} imu_config_t;
#define IMU_CONFIG_DEFAULT {120U, 100U, 4U, 1000U}
/** @brief Validate the supported synchronous accelerometer, gyro and SFLP profile. */
static inline bool imu_config_valid(const imu_config_t *c)
{
    return (c->sample_hz == 30U || c->sample_hz == 60U || c->sample_hz == 120U ||
            c->sample_hz == 240U || c->sample_hz == 480U) &&
        c->publish_hz >= 1U && c->publish_hz <= c->sample_hz &&
        (c->acc_g == 2U || c->acc_g == 4U || c->acc_g == 8U || c->acc_g == 16U) &&
        (c->gyro_dps == 250U || c->gyro_dps == 500U || c->gyro_dps == 1000U || c->gyro_dps == 2000U);
}
#endif
