/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    usb_otg.c
  * @brief   This file provides code for the configuration
  *          of the USB_OTG instances.
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
#include "usb_otg.h"

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

PCD_HandleTypeDef hpcd_USB_OTG_HS2;

/* USB2_OTG_HS init function */

void MX_USB2_OTG_HS_PCD_Init(void)
{

  /* USER CODE BEGIN USB2_OTG_HS_Init 0 */

  /* USER CODE END USB2_OTG_HS_Init 0 */

  /* USER CODE BEGIN USB2_OTG_HS_Init 1 */

  /* USER CODE END USB2_OTG_HS_Init 1 */
  hpcd_USB_OTG_HS2.Instance = USB2_OTG_HS;
  hpcd_USB_OTG_HS2.Init.dev_endpoints = 9;
  hpcd_USB_OTG_HS2.Init.speed = PCD_SPEED_HIGH;
  hpcd_USB_OTG_HS2.Init.phy_itface = USB_OTG_HS_EMBEDDED_PHY;
  hpcd_USB_OTG_HS2.Init.Sof_enable = DISABLE;
  hpcd_USB_OTG_HS2.Init.low_power_enable = DISABLE;
  hpcd_USB_OTG_HS2.Init.lpm_enable = DISABLE;
  hpcd_USB_OTG_HS2.Init.use_dedicated_ep1 = DISABLE;
  hpcd_USB_OTG_HS2.Init.vbus_sensing_enable = DISABLE;
  hpcd_USB_OTG_HS2.Init.dma_enable = DISABLE;
  if (HAL_PCD_Init(&hpcd_USB_OTG_HS2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USB2_OTG_HS_Init 2 */

  /* USER CODE END USB2_OTG_HS_Init 2 */

}

void HAL_PCD_MspInit(PCD_HandleTypeDef* pcdHandle)
{

  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};
  if(pcdHandle->Instance==USB2_OTG_HS)
  {
  /* USER CODE BEGIN USB2_OTG_HS_MspInit 0 */

  /* USER CODE END USB2_OTG_HS_MspInit 0 */

  /** Initializes the peripherals clock
  */
    PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_USBOTGHS2;
    PeriphClkInitStruct.UsbPhy2ClockSelection = RCC_USBPHY2CLKSOURCE_CLKP;
    PeriphClkInitStruct.UsbOtgHs2ClockSelection = RCC_USBOTGHS2CLKSOURCE_OTGPHY2;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
    {
      Error_Handler();
    }

    /* Enable VDDUSB */
    HAL_PWREx_EnableVddUSB();
    /* USB2_OTG_HS clock enable */
    __HAL_RCC_USB2_OTG_HS_CLK_ENABLE();
    __HAL_RCC_USB2_OTG_HS_PHY_CLK_ENABLE();
  /* USER CODE BEGIN USB2_OTG_HS_MspInit 1 */

  /* USER CODE END USB2_OTG_HS_MspInit 1 */
  }
}

void HAL_PCD_MspDeInit(PCD_HandleTypeDef* pcdHandle)
{

  if(pcdHandle->Instance==USB2_OTG_HS)
  {
  /* USER CODE BEGIN USB2_OTG_HS_MspDeInit 0 */

  /* USER CODE END USB2_OTG_HS_MspDeInit 0 */
    /* Peripheral clock disable */
    __HAL_RCC_USB2_OTG_HS_CLK_DISABLE();
    __HAL_RCC_USB2_OTG_HS_PHY_CLK_DISABLE();

    /* Disable VDDUSB */
      HAL_PWREx_DisableVddUSB();
  /* USER CODE BEGIN USB2_OTG_HS_MspDeInit 1 */

  /* USER CODE END USB2_OTG_HS_MspDeInit 1 */
  }
}

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */

