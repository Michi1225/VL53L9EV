#ifndef THREAD_PROFILER_H
#define THREAD_PROFILER_H

#include <stdint.h>

/* ThreadX execution hooks; peripheral ISRs must bracket their work as well. */
void _tx_execution_initialize(void);
void _tx_execution_thread_enter(void);
void _tx_execution_thread_exit(void);
void _tx_execution_idle_enter(void);
void _tx_execution_isr_enter(void);
void _tx_execution_isr_exit(void);

/* Called only by the shared transport thread. Buffer lasts until next call. */
uint8_t *thread_profiler_packet(uint32_t *size);

#endif
