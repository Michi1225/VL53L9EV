#include "thread_profiler.h"
#include "main.h"
#include "tx_api.h"
#include "tx_thread.h"
#include <stdbool.h>
#include <string.h>

/* PROF v1: 16-byte stream header, then LE timestamp_ms, clock_hz (u32),
 * interval/idle/ISR/scheduler cycles (u64), count/flags (u16).
 * Each 48-byte record: thread ID, priority (u32), cycles (u64), name[32].
 * Flag bit 0 means the thread list exceeded the packet capacity. */
#define MAX_THREADS 64U
#define PAYLOAD_HEADER_SIZE 44U
#define RECORD_SIZE 48U
static uint8_t packet[16U + PAYLOAD_HEADER_SIZE + MAX_THREADS * RECORD_SIZE];
static uint32_t sequence, last_cycle;
static uint64_t interval_cycles, idle_cycles, isr_cycles, scheduler_cycles;
static TX_THREAD *running;
static unsigned isr_depth;
static bool initialized, idle;

/* All accounting and snapshots run with interrupts masked. The 1 kHz tick
 * updates the timestamp even with no context switches, extending CYCCNT past
 * its 32-bit wrap (about 5 seconds at 800 MHz). No counter resets are needed. */
/** @brief Charge elapsed CPU cycles to the current thread, interrupt, idle, or scheduler context. */
static void account(void)
{
    uint32_t now = DWT->CYCCNT;
    uint32_t elapsed = now - last_cycle;
    last_cycle = now;
    interval_cycles += elapsed;
    if (isr_depth) isr_cycles += elapsed;
    else if (running) running->profiler_cycles += elapsed;
    else if (idle) idle_cycles += elapsed;
    else scheduler_cycles += elapsed;
}

/** @brief Enable the DWT cycle counter and initialize CPU time accounting. */
void _tx_execution_initialize(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    last_cycle = DWT->CYCCNT;
    initialized = true;
}

/** @brief Account for the previous context and select the scheduled thread. */
void _tx_execution_thread_enter(void)
{
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    if (initialized)
    {
        account();
        running = _tx_thread_current_ptr;
        idle = false;
    }
    __set_PRIMASK(mask);
}

/** @brief Account for the departing thread and enter scheduler accounting. */
void _tx_execution_thread_exit(void)
{
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    if (initialized)
    {
        account();
        running = TX_NULL;
        idle = false;
    }
    __set_PRIMASK(mask);
}

/** @brief Account for the previous context and begin idle time accounting. */
void _tx_execution_idle_enter(void)
{
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    if (initialized)
    {
        account();
        idle = true;
    }
    __set_PRIMASK(mask);
}

/** @brief Account for the interrupted context and track interrupt nesting. */
void _tx_execution_isr_enter(void)
{
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    if (initialized)
    {
        account();
        ++isr_depth;
    }
    __set_PRIMASK(mask);
}

/** @brief Account for interrupt time and restore the enclosing context. */
void _tx_execution_isr_exit(void)
{
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    if (initialized && isr_depth)
    {
        account();
        --isr_depth;
    }
    __set_PRIMASK(mask);
}

/** @brief Snapshot CPU usage into a PROF packet and reset interval counters. */
uint8_t *thread_profiler_packet(uint32_t *size)
{
    uint16_t count = 0U, flags = 0U, version = 1U;
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    if (!initialized)
    {
        __set_PRIMASK(mask);
        *size = 0U;
        return packet;
    }
    account();
    uint32_t timestamp = tx_time_get();
    memcpy(packet + 16U, &timestamp, 4U);
    memcpy(packet + 20U, &SystemCoreClock, 4U);
    memcpy(packet + 24U, &interval_cycles, 8U);
    memcpy(packet + 32U, &idle_cycles, 8U);
    memcpy(packet + 40U, &isr_cycles, 8U);
    memcpy(packet + 48U, &scheduler_cycles, 8U);
    TX_THREAD *thread = _tx_thread_created_ptr;
    ULONG remaining = _tx_thread_created_count;
    while (remaining-- && thread)
    {
        if (count < MAX_THREADS)
        {
            uint8_t *record = packet + 16U + PAYLOAD_HEADER_SIZE + count * RECORD_SIZE;
            uint32_t id = (uint32_t)(uintptr_t)thread;
            uint32_t priority = thread->tx_thread_priority;
            memcpy(record, &id, 4U);
            memcpy(record + 4U, &priority, 4U);
            memcpy(record + 8U, &thread->profiler_cycles, 8U);
            memset(record + 16U, 0, 32U);
            if (thread->tx_thread_name)
            {
                for (unsigned i = 0; i < 31U && thread->tx_thread_name[i]; ++i)
                    record[16U + i] = (uint8_t)thread->tx_thread_name[i];
            }
            ++count;
        }
        else flags |= 1U;
        thread->profiler_cycles = 0U;
        thread = thread->tx_thread_created_next;
    }
    interval_cycles = idle_cycles = isr_cycles = scheduler_cycles = 0U;
    __set_PRIMASK(mask);

    uint32_t length = PAYLOAD_HEADER_SIZE + count * RECORD_SIZE;
    memcpy(packet, "PROF", 4U);
    memcpy(packet + 4U, &sequence, 4U);
    memcpy(packet + 8U, &length, 4U);
    memcpy(packet + 12U, &version, 2U);
    memcpy(packet + 14U, &count, 2U);
    memcpy(packet + 56U, &count, 2U);
    memcpy(packet + 58U, &flags, 2U);
    ++sequence;
    *size = 16U + length;
    return packet;
}
