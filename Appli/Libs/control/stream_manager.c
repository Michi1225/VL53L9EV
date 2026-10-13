#include "stream_manager.h"
#include "dcmipp.h"
#include <stdbool.h>
#include <string.h>

TX_EVENT_FLAGS_GROUP stream_events;
UCHAR stream_packet[STREAM_HEADER_SIZE + CSI_OUTPUT_PAYLOAD_SIZE];
ULONG stream_packet_length;
static VL53L9_TypeDef_t sensor;
volatile ULONG vl53_frames_captured;
volatile uint32_t vl53_capture_bytes;
volatile uint32_t vl53_capture_counter;
volatile ULONG vl53_frame_nonzero_bytes;
volatile ULONG vl53_frame_changed_bytes;
volatile uint32_t vl53_buffer_address;
volatile uint32_t vl53_dma_address;
volatile int vl53_stream_status = HAL_BUSY;
volatile vl53_stream_phase_t vl53_stream_phase;
volatile ULONG vl53_csi_clock_hz;
volatile vl53l9_status_t vl53_sensor_status;
volatile int vl53_sensor_status_result = -1;

static void put_le32(UCHAR *dst, ULONG value)
{
    for (UINT i = 0; i < 4U; ++i)
        dst[i] = (UCHAR)(value >> (8U * i));
}

void stream_thread_entry(ULONG argument)
{
    ULONG flags;
    ULONG sequence = 0;
    uint32_t last_status_tick;
    (void)argument;

    vl53_stream_phase = VL53_STREAM_INITIALIZING;
    vl53_csi_clock_hz = HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_CSI);
    vl53_stream_status = VL53L9_init(&sensor);
    if (vl53_stream_status != HAL_OK)
        return;
    vl53_buffer_address = (uint32_t)sensor.g_csi_output_buffer;
    if (sensor.csi_width > CSI_OUTPUT_PITCH ||
        (ULONG)sensor.csi_width * sensor.csi_height > CSI_OUTPUT_PAYLOAD_SIZE ||
        (ULONG)CSI_OUTPUT_PITCH * sensor.csi_height > CSI_OUTPUT_BUFFER_SIZE)
    {
        vl53_stream_status = HAL_ERROR;
        return;
    }
    vl53_stream_phase = VL53_STREAM_STARTING_RANGING;
    vl53_stream_status = HAL_BUSY;
    vl53_stream_status = VL53L9_start_ranging(&sensor);
    vl53_dma_address = DCMIPP->P0PPM0AR1;
    if (vl53_stream_status != HAL_OK)
    {
        vl53l9_status_t status = {0};
        vl53_sensor_status_result = vl53l9_get_status(sensor.p_dev, &status);
        vl53_sensor_status = status;
        return;
    }

    last_status_tick = HAL_GetTick();
    for (;;)
    {
        vl53_stream_phase = VL53_STREAM_WAITING_FRAME;
        bool ready = false;
        platform_get_event_status(PLATFORM_CAM_PIPE_FRAME_EVT, &ready);
        if (!ready)
        {
            /* Read-only diagnostics: a sensor fault may stop CSI output
             * without completing the outstanding DCMIPP capture. */
            uint32_t now = HAL_GetTick();
            if ((uint32_t)(now - last_status_tick) >= 1000U)
            {
                vl53l9_status_t status = {0};
                vl53_sensor_status_result = vl53l9_get_status(sensor.p_dev, &status);
                vl53_sensor_status = status;
                last_status_tick = now;
                if (vl53_sensor_status_result == 0 &&
                    (status.firmware != 0 || status.error.internal_fw ||
                     status.error.ref_array))
                {
                    /* Preserve the fault readback; the sensor has stopped
                     * producing usable frames. Release the receiver too. */
                    vl53_stream_status = HAL_ERROR;
                    vl53_stream_phase = VL53_STREAM_SENSOR_FAULT;
                    (void)platform_stop_csi_pipe();
                    return;
                }
            }
            tx_thread_sleep(1);
            continue;
        }
        platform_acknowledge_event(PLATFORM_CAM_PIPE_FRAME_EVT);
        /* Snapshot capture has stopped: DMA cannot overwrite this frame. */
        SCB_InvalidateDCache_by_Addr((uint32_t *)sensor.g_csi_output_buffer,
                                    sizeof(sensor.g_csi_output_buffer));
        memcpy(stream_packet, "VL9F", 4);
        put_le32(stream_packet + 4, sequence++);
        ULONG payload = (ULONG)sensor.csi_width * sensor.csi_height;
        put_le32(stream_packet + 8, payload);
        stream_packet[12] = sensor.csi_width;
        stream_packet[13] = 0;
        stream_packet[14] = sensor.csi_height;
        stream_packet[15] = 0;
        /* HAL GetDataCounter rejects an ERROR handle, even when a completed
         * snapshot has a useful counter. Preserve hardware evidence directly. */
        uint32_t captured = READ_REG(hdcmipp.Instance->P0DCCNTR);
        /* Preserve the hardware count directly; do not multiply by word size. */
        vl53_capture_counter = captured;
        vl53_capture_bytes = captured;
        /* PIPE0 preserves the sensor payload as a contiguous byte stream. */
        memcpy(stream_packet + STREAM_HEADER_SIZE,
               (const void *)sensor.g_csi_output_buffer, payload);
        ULONG nonzero = 0;
        ULONG changed = 0;
        for (ULONG i = 0; i < payload; ++i)
        {
            nonzero += stream_packet[STREAM_HEADER_SIZE + i] != 0;
            changed += stream_packet[STREAM_HEADER_SIZE + i] != CSI_CAPTURE_MARKER;
        }
        vl53_frame_nonzero_bytes = nonzero;
        vl53_frame_changed_bytes = changed;
        stream_packet_length = STREAM_HEADER_SIZE + payload;
        ++vl53_frames_captured;
        tx_event_flags_set(&stream_events, STREAM_PACKET_READY, TX_OR);
        vl53_stream_phase = VL53_STREAM_WAITING_CONSUMER;
        tx_event_flags_get(&stream_events, STREAM_PACKET_CONSUMED,
                           TX_OR_CLEAR, &flags, TX_WAIT_FOREVER);
        /* The HAL disables PIPE0 frame interrupts after each snapshot.
         * Start again to restore interrupts as well as the capture request. */
        vl53_stream_phase = VL53_STREAM_REARMING;
        VL53L9_prepare_capture(&sensor);
        vl53_stream_status = platform_start_csi_pipe((uint8_t *)sensor.g_csi_output_buffer)
                             == 0 ? HAL_OK : HAL_ERROR;
        if (vl53_stream_status != HAL_OK)
            return;
    }
}
