#ifndef IMU_CONTROL_H
#define IMU_CONTROL_H
#include "imu_config.h"
#include <stddef.h>
#define IMU_GET 7U
#define IMU_SET 8U
#define IMU_RESET 9U
#define IMU_INITIALIZING 0U
#define IMU_STREAMING 1U
#define IMU_FAULT 2U
#define IMU_STATUS_PACKET_SIZE 52U
typedef struct {
    uint32_t request_id, operation, result, route, session;
    imu_config_t config;
} imu_command_t;
typedef struct {
    uint32_t revision;
    int32_t error;
    imu_config_t config;
    uint32_t state, published, dropped;
} imu_status_t;
typedef struct { uint32_t route, session; uint8_t packet[IMU_STATUS_PACKET_SIZE]; } imu_reply_t;
unsigned imu_control_init(void);
/** @brief Validate and queue a complete IMU CTRL request from either transport. */
void imu_control_submit(uint32_t id, uint32_t op, const uint8_t *payload, uint32_t size,
                        uint32_t route, uint32_t session);
bool imu_control_next(imu_command_t *command);
void imu_control_publish(const imu_status_t *status);
void imu_control_reply(const imu_command_t *command, uint32_t result);
bool imu_control_next_reply(imu_reply_t *reply);
void imu_control_status_packet(uint8_t packet[IMU_STATUS_PACKET_SIZE]);
#endif
