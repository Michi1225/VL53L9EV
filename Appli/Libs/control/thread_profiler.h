#ifndef THREAD_PROFILER_H
#define THREAD_PROFILER_H

#include <stdint.h>

/* ThreadX execution hooks; peripheral ISRs must bracket their work as well. */
/** @brief Enable the DWT cycle counter and initialize CPU time accounting. */
void _tx_execution_initialize(void);
/** @brief Account for the previous context and select the scheduled thread. */
void _tx_execution_thread_enter(void);
/** @brief Account for the departing thread and enter scheduler accounting. */
void _tx_execution_thread_exit(void);
/** @brief Account for the previous context and begin idle time accounting. */
void _tx_execution_idle_enter(void);
/** @brief Account for the interrupted context and track interrupt nesting. */
void _tx_execution_isr_enter(void);
/** @brief Account for interrupt time and restore the enclosing context. */
void _tx_execution_isr_exit(void);

/* Called only by the shared transport thread. Buffer lasts until next call. */
/** @brief Snapshot CPU usage into a PROF packet and reset interval counters. */
uint8_t *thread_profiler_packet(uint32_t *size);

#endif
