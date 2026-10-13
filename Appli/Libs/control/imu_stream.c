#include "imu_stream.h"
#include "IMU.h"
#include "stream_manager.h"
#include <string.h>

#if TX_TIMER_TICKS_PER_SECOND % IMU_STREAM_RATE_HZ != 0
#error "ThreadX tick rate must be a multiple of the IMU publish rate"
#endif

_Static_assert(sizeof(float_t) == 4 && sizeof(ULONG) == 4,
               "IMUF packets require 32-bit float and ULONG");
TX_QUEUE imu_packet_queue;
static ULONG queue_storage[IMU_QUEUE_DEPTH * IMU_PACKET_WORDS];
static IMU imu;

static void put_u32(UCHAR *dst, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i)
        dst[i] = (UCHAR)(value >> (8U * i));
}

static void put_float(UCHAR *dst, float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    put_u32(dst, bits);
}

UINT imu_stream_queue_init(void)
{
    return tx_queue_create(&imu_packet_queue, "IMU packets", IMU_PACKET_WORDS,
                           queue_storage, sizeof(queue_storage));
}

void imu_stream_thread_entry(ULONG argument)
{
    const ULONG period = TX_TIMER_TICKS_PER_SECOND / IMU_STREAM_RATE_HZ;
    HAL_StatusTypeDef status;
    ULONG sequence = 0;
    (void)argument;

    status = imu_init(&imu);
    if (status != HAL_OK)
    {
        return;
    }
    ULONG next = tx_time_get() + period;
    for (;;)
    {
        LONG remaining = (LONG)(next - tx_time_get());
        if (remaining > 0)
            tx_thread_sleep((ULONG)remaining);
        else
        {
            next = tx_time_get();
        }
        next += period;

        status = imu_get_data_blocking(&imu);

        if (status == HAL_BUSY && imu.fifo_status.fifo_ovr)
        {
            ++sequence; /* A sequence gap also makes it visible to the host. */
            status = imu_reset_fifo(&imu);

            if (status == HAL_OK)
            {
                continue;
            }
        }
        if (status != HAL_OK)
        {
            return;
        }
        if (imu.valid_data != IMU_DATA_ALL)
        {
            continue; /* Wait for first acc, gyro and SFLP outputs. */
        }

        ULONG words[IMU_PACKET_WORDS] = {0};
        UCHAR *packet = (UCHAR *)words;
        memcpy(packet, "IMUF", 4);
        put_u32(packet + 4, sequence++);
        put_u32(packet + 8, 44U);
        packet[12] = 10U;
        packet[14] = 1U;
        put_u32(packet + 16, HAL_GetTick());
        for (unsigned i = 0; i < 3; ++i)
        {
            put_float(packet + 20 + 4 * i, imu.acc_mg[i]);
            put_float(packet + 32 + 4 * i, imu.gyro_mdps[i]);
        }
        for (unsigned i = 0; i < 4; ++i)
            put_float(packet + 44 + 4 * i, imu.quat[i]);

        /* Sampling never waits on USB; a full queue drops this sample. */
        if (tx_queue_send(&imu_packet_queue, words, TX_NO_WAIT) == TX_SUCCESS)
            tx_event_flags_set(&stream_events, STREAM_IMU_READY, TX_OR);
    }
}
