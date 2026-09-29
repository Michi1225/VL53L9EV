/**
 * This File contains the thread for the control plane
 *
 
 */

#ifndef HOUSEKEEPING_H
#define HOUSEKEEPING_H

#include "main.h"
#include "app_threadx.h" // For typedefs

#define HOUSEKEEPING_STACK_SIZE      2048U
#define HOUSEKEEPING_PRIO            20U



void housekeeping_thread_entry(ULONG argument);


#endif