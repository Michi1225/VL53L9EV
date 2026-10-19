#ifndef IMU_STREAM_H
#define IMU_STREAM_H

#include "tx_api.h"
#include "stm32n6xx_hal.h"

#define IMU_STREAM_STACK_SIZE 2048U
#define IMU_STREAM_PRIO       9U /* Sample before USB application work (priority 10). */
#define IMU_STREAM_RATE_HZ    100U
#define IMU_PACKET_SIZE       60U
#define IMU_PACKET_WORDS      (IMU_PACKET_SIZE / sizeof(ULONG))
#define IMU_QUEUE_DEPTH       16U

/* Header <4sIIHH: "IMUF", sequence, 44 payload bytes, 10 floats, version 1.
 * Payload <I10f: MCU milliseconds, acc xyz (mg), gyro xyz (mdps),
 * SFLP game-rotation quaternion xyzw. All fields are little-endian. */
extern TX_QUEUE imu_packet_queue;

/** @brief Create the bounded queue of complete IMU telemetry packets. */
UINT imu_stream_queue_init(void);
/** @brief Sample the IMU and enqueue telemetry at the configured publish rate. */
void imu_stream_thread_entry(ULONG argument);

#endif
