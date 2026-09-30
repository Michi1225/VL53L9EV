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
  /* Shared RX FIFO */
  HAL_PCDEx_SetRxFiFo(&hpcd_USB_OTG_HS2, 0x200);

  /* EP0 control IN: 64 bytes */
  HAL_PCDEx_SetTxFiFo(&hpcd_USB_OTG_HS2, 0, 0x10);

  /* EP1 CDC notification IN */
  HAL_PCDEx_SetTxFiFo(&hpcd_USB_OTG_HS2, 1, 0x10);

  /* EP2 CDC bulk IN: 512 bytes */
  HAL_PCDEx_SetTxFiFo(&hpcd_USB_OTG_HS2, 2, 0x80);
  /* USER CODE END USB2_OTG_HS_Init 2 */

}

void HAL_PCD_MspInit(PCD_HandleTypeDef* pcdHandle)
{

  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};
  if(pcdHandle->Instance==USB2_OTG_HS)
  {
  /* USER CODE BEGIN USB2_OTG_HS_MspInit 0 */
  __HAL_RCC_PWR_CLK_ENABLE();

  HAL_PWREx_EnableVddUSBVMEN();

  while (__HAL_PWR_GET_FLAG(PWR_FLAG_USB33RDY) == 0U)
  {
      /* wait */
  }

  RCC_PeriphCLKInitTypeDef UsbPhyClkInit = {0};

  UsbPhyClkInit.PeriphClockSelection = RCC_PERIPHCLK_USBPHY2;
  UsbPhyClkInit.UsbPhy2ClockSelection = RCC_USBPHY2CLKSOURCE_CLKP;

  if (HAL_RCCEx_PeriphCLKConfig(&UsbPhyClkInit) != HAL_OK)
  {
      Error_Handler();
  }

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

    /* USB2_OTG_HS interrupt Init */
    HAL_NVIC_SetPriority(USB2_OTG_HS_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(USB2_OTG_HS_IRQn);
  /* USER CODE BEGIN USB2_OTG_HS_MspInit 1 */

    /*
      * Reset USB2 OTG core, PHY, and HS PHY controller.
      */
      LL_AHB5_GRP1_ForceReset(0x00800000U);

      __HAL_RCC_USB2_OTG_HS_FORCE_RESET();
      __HAL_RCC_USB2_OTG_HS_PHY_FORCE_RESET();

      /*
      * Do NOT blindly select HSE/2 here.
      * Your USBPHY2 reference is configured as 20 MHz through CubeMX.
      */

      /*
      * Release HS PHY controller reset first.
      */
      LL_AHB5_GRP1_ReleaseReset(0x00800000U);

      /*
      * OTG register clock must be available.
      */
      __HAL_RCC_USB2_OTG_HS_CLK_ENABLE();

      /*
      * ST recommends a real settling interval before touching PHYC.
      */
      for (volatile uint32_t i = 0; i < 10U; i++)
      {
          __NOP();
      }

      /*
      * Internal HS PHY configuration.
      *
      * FSEL = 001 for 20 MHz.
      */
      USB2_HS_PHYC->USBPHYC_CR &= ~(0x7U << 4);

      USB2_HS_PHYC->USBPHYC_CR |=
            (1U << 16)
          | (1U << 4)     /* 20 MHz */
          | (1U << 2)
          |  1U;

      /*
      * Release PHY reset.
      */
      __HAL_RCC_USB2_OTG_HS_PHY_RELEASE_RESET();

      for (volatile uint32_t i = 0; i < 10U; i++)
      {
          __NOP();
      }

      /*
      * Release OTG core reset.
      */
      __HAL_RCC_USB2_OTG_HS_RELEASE_RESET();

      /*
      * Enable PHY clock.
      */
      __HAL_RCC_USB2_OTG_HS_PHY_CLK_ENABLE();

      /*
      * Important: allow synchronization before HAL continues
      * into USB_CoreReset().
      */
      volatile uint32_t per_source =
          __HAL_RCC_GET_CLKP_SOURCE();

      volatile uint32_t per_freq =
          HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_CKPER);

      volatile uint32_t ic5_enabled =
          LL_RCC_IC5_IsEnabled();

      volatile uint32_t ic5_source =
          LL_RCC_IC5_GetSource();

      volatile uint32_t ic5_div =
          LL_RCC_IC5_GetDivider();

      for (volatile uint32_t i = 0; i < 10U; i++)
      {
          __NOP();
      }


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

    /* USB2_OTG_HS interrupt Deinit */
    HAL_NVIC_DisableIRQ(USB2_OTG_HS_IRQn);
  /* USER CODE BEGIN USB2_OTG_HS_MspDeInit 1 */

  /* USER CODE END USB2_OTG_HS_MspDeInit 1 */
  }
}

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */

