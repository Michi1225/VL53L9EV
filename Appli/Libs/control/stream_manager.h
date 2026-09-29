/**
 * This File contains the thread for the control plane
 *
 
 */

#ifndef STREAM_MANAGER_H
#define STREAM_MANAGER_H

#include "main.h"
#include "app_threadx.h" // For typedefs

#define STREAM_THREAD_STACK_SIZE     4096U
#define STREAM_THREAD_PRIO           12U



void stream_thread_entry(ULONG argument);


#endif