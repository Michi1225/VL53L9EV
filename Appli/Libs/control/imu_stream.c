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
volatile HAL_StatusTypeDef imu_stream_status = HAL_BUSY;
volatile ULONG imu_samples_published;
volatile ULONG imu_packets_dropped;
volatile ULONG imu_deadlines_missed;
volatile imu_stream_phase_t imu_stream_phase;
volatile ULONG imu_valid_data;
volatile ULONG imu_fifo_level;
volatile ULONG imu_fifo_overrun;
volatile ULONG imu_fifo_overruns;
volatile ULONG imu_usb_packets_sent;
volatile ULONG imu_usb_packets_dropped;
volatile ULONG imu_fault_during_init;

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
    ULONG sequence = 0;
    (void)argument;
    imu_stream_phase = IMU_STREAM_INITIALIZING;
    imu_stream_status = imu_init(&imu);
    if (imu_stream_status != HAL_OK)
    {
        imu_fault_during_init = 1;
        imu_stream_phase = IMU_STREAM_FAULT;
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
            ++imu_deadlines_missed;
            next = tx_time_get();
        }
        next += period;
        imu_stream_phase = IMU_STREAM_SAMPLING;
        imu_stream_status = imu_get_data_blocking(&imu);
        imu_valid_data = imu.valid_data;
        imu_fifo_level = imu.fifo_status.fifo_level;
        imu_fifo_overrun = imu.fifo_status.fifo_ovr;
        if (imu_stream_status == HAL_BUSY && imu_fifo_overrun)
        {
            ++imu_fifo_overruns;
            ++imu_packets_dropped; /* Report the discontinuity in diagnostics. */
            ++sequence; /* A sequence gap also makes it visible to the host. */
            imu_stream_status = imu_reset_fifo(&imu);
            imu_valid_data = imu.valid_data;
            if (imu_stream_status == HAL_OK)
            {
                imu_stream_phase = IMU_STREAM_WAITING_DATA;
                continue;
            }
        }
        if (imu_stream_status != HAL_OK)
        {
            imu_stream_phase = IMU_STREAM_FAULT;
            return;
        }
        if (imu.valid_data != IMU_DATA_ALL)
        {
            imu_stream_phase = IMU_STREAM_WAITING_DATA;
            continue; /* Wait for first acc, gyro and SFLP outputs. */
        }
        imu_stream_phase = IMU_STREAM_RUNNING;
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
        ++imu_samples_published;
        /* Sampling never waits on USB; a full queue drops this sample. */
        if (tx_queue_send(&imu_packet_queue, words, TX_NO_WAIT) == TX_SUCCESS)
            tx_event_flags_set(&stream_events, STREAM_IMU_READY, TX_OR);
        else
            ++imu_packets_dropped;
    }
}
