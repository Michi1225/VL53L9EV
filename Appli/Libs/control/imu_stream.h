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
extern volatile HAL_StatusTypeDef imu_stream_status;
extern volatile ULONG imu_samples_published;
extern volatile ULONG imu_packets_dropped;
extern volatile ULONG imu_deadlines_missed;
typedef enum {
    IMU_STREAM_NOT_STARTED,
    IMU_STREAM_INITIALIZING,
    IMU_STREAM_SAMPLING,
    IMU_STREAM_WAITING_DATA,
    IMU_STREAM_RUNNING,
    IMU_STREAM_FAULT
} imu_stream_phase_t;
extern volatile imu_stream_phase_t imu_stream_phase;
extern volatile ULONG imu_valid_data;
extern volatile ULONG imu_fifo_level;
extern volatile ULONG imu_fifo_overrun;
extern volatile ULONG imu_fifo_overruns;
extern volatile ULONG imu_usb_packets_sent;
extern volatile ULONG imu_usb_packets_dropped;
extern volatile ULONG imu_fault_during_init;

UINT imu_stream_queue_init(void);
void imu_stream_thread_entry(ULONG argument);

#endif
