/**
 * This File contains the thread for the control plane
 *
 
 */

#ifndef CONTROL_PLANE_H
#define CONTROL_PLANE_H

#include "main.h"
#include "app_events.h"
#include "nx_api.h"
#include "app_netxduo.h"

#define CONTROL_THREAD_STACK_SIZE    4096U
#define CONTROL_THREAD_PRIO          12U

#define CONTROL_SERVER_PORT       50000U
#define CONTROL_ACCEPT_TIMEOUT    (TX_TIMER_TICKS_PER_SECOND / 5U)
#define CONTROL_RX_TIMEOUT        (TX_TIMER_TICKS_PER_SECOND / 5U)



void control_thread_entry(ULONG argument);


#endif