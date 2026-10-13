#include "stream_manager.h"
#include "dcmipp.h"
#include <stdbool.h>
#include <string.h>

TX_EVENT_FLAGS_GROUP stream_events;
UCHAR stream_packet[STREAM_HEADER_SIZE + CSI_OUTPUT_PAYLOAD_SIZE];
ULONG stream_packet_length;
static VL53L9_TypeDef_t sensor;

static void put_le32(UCHAR *dst, ULONG value)
{
    for (UINT i = 0; i < 4U; ++i)
        dst[i] = (UCHAR)(value >> (8U * i));
}

void stream_thread_entry(ULONG argument)
{
    HAL_StatusTypeDef stream_status;
    ULONG flags;
    ULONG sequence = 0;
    uint32_t last_status_tick;
    (void)argument;

    stream_status = VL53L9_init(&sensor);
    if (stream_status != HAL_OK)
        return;

    if (sensor.csi_width > CSI_OUTPUT_PITCH ||
        (ULONG)sensor.csi_width * sensor.csi_height > CSI_OUTPUT_PAYLOAD_SIZE ||
        (ULONG)CSI_OUTPUT_PITCH * sensor.csi_height > CSI_OUTPUT_BUFFER_SIZE)
    {
        return;
    }

    stream_status = VL53L9_start_ranging(&sensor);

    if (stream_status != HAL_OK)
        return;

    last_status_tick = HAL_GetTick();
    for (;;)
    {
        bool ready = false;
        platform_get_event_status(PLATFORM_CAM_PIPE_FRAME_EVT, &ready);
        if (!ready)
        {
            /* A sensor fault may stop CSI output
             * without completing the outstanding DCMIPP capture. */
            uint32_t now = HAL_GetTick();
            if ((uint32_t)(now - last_status_tick) >= 1000U)
            {
                vl53l9_status_t status = {0};
                int status_result = vl53l9_get_status(sensor.p_dev, &status);

                last_status_tick = now;
                if (status_result == 0 &&
                    (status.firmware != 0 || status.error.internal_fw ||
                     status.error.ref_array))
                {
                    /* The sensor has stopped
                     * producing usable frames. Release the receiver too. */

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
        /* PIPE0 preserves the sensor payload as a contiguous byte stream. */
        memcpy(stream_packet + STREAM_HEADER_SIZE,
               (const void *)sensor.g_csi_output_buffer, payload);
        stream_packet_length = STREAM_HEADER_SIZE + payload;
        tx_event_flags_set(&stream_events, STREAM_PACKET_READY, TX_OR);

        tx_event_flags_get(&stream_events, STREAM_PACKET_CONSUMED,
                           TX_OR_CLEAR, &flags, TX_WAIT_FOREVER);
        /* The HAL disables PIPE0 frame interrupts after each snapshot.
         * Start again to restore interrupts as well as the capture request. */

        VL53L9_prepare_capture(&sensor);
        stream_status = platform_start_csi_pipe((uint8_t *)sensor.g_csi_output_buffer)
                             == 0 ? HAL_OK : HAL_ERROR;
        if (stream_status != HAL_OK)
            return;
    }
}
