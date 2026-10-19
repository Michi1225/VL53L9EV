/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    app_threadx.c
  * @author  MCD Application Team
  * @brief   ThreadX applicative file
  ******************************************************************************
    * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "app_threadx.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "LEDController.h"
#include "control_server.h"
#include "housekeeping.h"
#include "app_events.h"
#include "stream_manager.h"
#include "tof_control.h"
#include "imu_stream.h"
#include "ethernet_stream.h"
#include "app_netxduo.h"
#include "tx_api.h"
#include "firmware_update.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
TX_THREAD tx_app_thread;
/* USER CODE BEGIN PV */
TX_THREAD control_thread;
TX_THREAD stream_thread;
TX_THREAD imu_stream_thread;
TX_THREAD ethernet_stream_thread;
// TX_THREAD housekeeping_thread;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/**
  * @brief  Application ThreadX Initialization.
  * @param memory_ptr: memory pointer
  * @retval int
  */
UINT App_ThreadX_Init(VOID *memory_ptr)
{
  UINT ret = TX_SUCCESS;
  TX_BYTE_POOL *byte_pool = (TX_BYTE_POOL*)memory_ptr;

  /* USER CODE BEGIN App_ThreadX_MEM_POOL */
  LEDController_Init(&led_controller);
  LEDController_SetAnimation(&led_controller, LED_ANIMATION_CYAN);
  /* USER CODE END App_ThreadX_MEM_POOL */
  CHAR *pointer;

  /* Allocate the stack for tx app thread  */
  if (tx_byte_allocate(byte_pool, (VOID**) &pointer,
                       TX_APP_STACK_SIZE, TX_NO_WAIT) != TX_SUCCESS)
  {
    return TX_POOL_ERROR;
  }
  /* Create tx app thread.  */
  if (tx_thread_create(&tx_app_thread, "tx app thread", tx_app_thread_entry, 0, pointer,
                       TX_APP_STACK_SIZE, TX_APP_THREAD_PRIO, TX_APP_THREAD_PREEMPTION_THRESHOLD,
                       TX_APP_THREAD_TIME_SLICE, TX_APP_THREAD_AUTO_START) != TX_SUCCESS)
  {
    return TX_THREAD_ERROR;
  }

  /* USER CODE BEGIN App_ThreadX_Init */
  CHAR *network_stack = TX_NULL;
  CHAR *control_stack = TX_NULL;

  /*
    * Global application events
    */
  if (tx_event_flags_create(&app_events,
                            "app events") != TX_SUCCESS)
  {
      return TX_GROUP_ERROR;
  }

  /*
    * Control thread stack
    */
  UINT status = tx_byte_allocate(byte_pool,
                        (VOID **)&control_stack,
                        CONTROL_THREAD_STACK_SIZE,
                        TX_NO_WAIT);
  if(status != TX_SUCCESS)
  {
      return TX_POOL_ERROR;
  }

  /*
    * Control thread
    */
  if (tx_thread_create(&control_thread,
                        "control server",
                        control_thread_entry,
                        0,
                        control_stack,
                        CONTROL_THREAD_STACK_SIZE,
                        CONTROL_THREAD_PRIO,
                        CONTROL_THREAD_PRIO,
                        TX_NO_TIME_SLICE,
                        TX_AUTO_START) != TX_SUCCESS)
  {
      return TX_THREAD_ERROR;
  }
  if (tof_control_init() != TX_SUCCESS)
      return TX_QUEUE_ERROR;
  if (firmware_update_init() != TX_SUCCESS)
      return TX_THREAD_ERROR;
  CHAR *stream_stack = TX_NULL;
  if (tx_event_flags_create(&stream_events, "sensor stream") != TX_SUCCESS)
      return TX_GROUP_ERROR;
  if (tx_byte_allocate(byte_pool, (VOID **)&stream_stack,
                       STREAM_THREAD_STACK_SIZE, TX_NO_WAIT) != TX_SUCCESS)
      return TX_POOL_ERROR;
  if (tx_thread_create(&stream_thread, "VL53 stream", stream_thread_entry, 0,
                       stream_stack, STREAM_THREAD_STACK_SIZE, STREAM_THREAD_PRIO,
                       STREAM_THREAD_PRIO, TX_NO_TIME_SLICE, TX_AUTO_START) != TX_SUCCESS)
      return TX_THREAD_ERROR;
  CHAR *imu_stack = TX_NULL;
  if (imu_stream_queue_init() != TX_SUCCESS)
      return TX_QUEUE_ERROR;
  if (tx_byte_allocate(byte_pool, (VOID **)&imu_stack,
                       IMU_STREAM_STACK_SIZE, TX_NO_WAIT) != TX_SUCCESS)
      return TX_POOL_ERROR;
  if (tx_thread_create(&imu_stream_thread, "IMU stream", imu_stream_thread_entry, 0,
                       imu_stack, IMU_STREAM_STACK_SIZE, IMU_STREAM_PRIO,
                       IMU_STREAM_PRIO, TX_NO_TIME_SLICE, TX_AUTO_START) != TX_SUCCESS)
      return TX_THREAD_ERROR;
  CHAR *ethernet_stream_stack = TX_NULL;
  if (tx_byte_allocate(byte_pool, (VOID **)&ethernet_stream_stack,
                       ETHERNET_STREAM_STACK_SIZE, TX_NO_WAIT) != TX_SUCCESS)
      return TX_POOL_ERROR;
  if (tx_thread_create(&ethernet_stream_thread, "Ethernet sensor stream", ethernet_stream_thread_entry, 0,
                       ethernet_stream_stack, ETHERNET_STREAM_STACK_SIZE, ETHERNET_STREAM_PRIO,
                       ETHERNET_STREAM_PRIO, TX_NO_TIME_SLICE, TX_AUTO_START) != TX_SUCCESS)
      return TX_THREAD_ERROR;
  /* USER CODE END App_ThreadX_Init */

  return ret;
}
/**
  * @brief  Function implementing the tx_app_thread_entry thread.
  * @param  thread_input: Not used.
  * @retval None
  */
void tx_app_thread_entry(ULONG thread_input)
{
  /* USER CODE BEGIN tx_app_thread_entry */

  /* USER CODE END tx_app_thread_entry */
}

  /**
  * @brief  Function that implements the kernel's initialization.
  * @param  None
  * @retval None
  */
void MX_ThreadX_Init(void)
{
  /* USER CODE BEGIN Before_Kernel_Start */

  /* USER CODE END Before_Kernel_Start */

  tx_kernel_enter();

  /* USER CODE BEGIN Kernel_Start_Error */

  /* USER CODE END Kernel_Start_Error */
}

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */
