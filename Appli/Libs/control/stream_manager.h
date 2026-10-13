/**
 * This File contains the thread for the control plane
 *
 
 */

#ifndef STREAM_MANAGER_H
#define STREAM_MANAGER_H

#include "main.h"
#include "VL53L9_process.h"
#include "app_threadx.h" // For typedefs

/* vl53l9_write() puts the 9,865-byte firmware patch plus its 2-byte
 * register address on the stack. Reserve headroom for the HAL call chain. */
#define STREAM_THREAD_STACK_SIZE     16384U
#define STREAM_THREAD_PRIO           12U



/* One USB consumer owns this packet until it sets STREAM_PACKET_CONSUMED.
 * Wire format: "VL9F", LE32 sequence, LE32 payload bytes, LE16 width,
 * LE16 height, then tightly packed RAW8 CSI data including the status row. */
#define STREAM_PACKET_READY       (1UL << 0)
#define STREAM_PACKET_CONSUMED    (1UL << 1)
#define STREAM_IMU_READY          (1UL << 2)
#define STREAM_HEADER_SIZE        16U
extern TX_EVENT_FLAGS_GROUP stream_events;
extern UCHAR stream_packet[STREAM_HEADER_SIZE + CSI_OUTPUT_PAYLOAD_SIZE];
extern ULONG stream_packet_length;

typedef enum {
    VL53_STREAM_NOT_STARTED,
    VL53_STREAM_INITIALIZING,
    VL53_STREAM_STARTING_RANGING,
    VL53_STREAM_WAITING_FRAME,
    VL53_STREAM_WAITING_CONSUMER,
    VL53_STREAM_REARMING,
    VL53_STREAM_SENSOR_FAULT
} vl53_stream_phase_t;

extern volatile vl53_stream_phase_t vl53_stream_phase;
extern volatile int vl53_stream_status;
extern volatile ULONG vl53_csi_clock_hz;
extern volatile uint32_t vl53_capture_bytes;
extern volatile uint32_t vl53_capture_counter;
extern volatile ULONG vl53_frame_nonzero_bytes;
extern volatile ULONG vl53_frame_changed_bytes;
extern volatile uint32_t vl53_buffer_address;
extern volatile uint32_t vl53_dma_address;
extern volatile vl53l9_status_t vl53_sensor_status;
extern volatile int vl53_sensor_status_result;

void stream_thread_entry(ULONG argument);


#endif
