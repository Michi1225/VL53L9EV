/*
 * stm32_s28hs512t_driver.c
 *
 *  Created on: Mar 19, 2026
 *      Author: TakamuraSota
 */

/* Includes ------------------------------------------------------------------*/


#include "stm32_extmem.h"
#include "stm32_extmem_conf.h"
#if EXTMEM_DRIVER_USER == 1
#include "stm32_user_driver_api.h"
#include "stm32_user_driver_type.h"
#include "stm32_extmemloader_conf.h"
#include "semper_flash.h"
#include <string.h>

/** @defgroup USER USER driver
  * @ingroup EXTMEM_DRIVER
  * @{
  */

/* Private Macro ------------------------------------------------------------*/
/** @defgroup USER_Private_Macro Private Macro
  * @{
  */

/**
  * @}
  */
/* Private typedefs ---------------------------------------------------------*/

/* Private variables ---------------------------------------------------------*/
SEMPER_Handle_t Flash;
extern XSPI_HandleTypeDef hxspi1;
/* Private functions ---------------------------------------------------------*/

/** @defgroup USER_Exported_Functions Exported Functions
  * @{
  */

/**
  * @brief Initializes the USER driver.
  * @param MemoryId Memory ID.
  * @param UserObject Pointer to the USER driver object.
  * @retval @ref EXTMEM_DRIVER_USER_StatusTypeDef
  */
EXTMEM_DRIVER_USER_StatusTypeDef EXTMEM_DRIVER_USER_Init(uint32_t MemoryId,
                                                                EXTMEM_DRIVER_USER_ObjectTypeDef *UserObject)
{
  UNUSED(MemoryId);
  EXTMEM_DRIVER_USER_StatusTypeDef retr = EXTMEM_DRIVER_USER_OK;

  UserObject->MemID = 0;               /* Store the memory ID; can be used to control multiple user memories. */
  UserObject->PtrUserDriver = (void *)&Flash; /* Can be used to link data with the memory ID. */

  memset(&Flash, 0, sizeof(Flash));

  /* Initialize XSPI */
  Flash.interface = S28HS512T_OPI_MODE;
  Flash.transfer  = S28HS512T_DTR_TRANSFER;

  if (SEMPER_Init(&Flash, &hxspi1) != SEMPER_OK)
  {
    retr = EXTMEM_DRIVER_USER_ERROR_3;
  }

  return retr;
}

/**
  * @brief Reads data from the USER memory.
  * @param UserObject Pointer to the USER driver object.
  * @param Address Memory address.
  * @param Data Pointer to the data buffer to store the read data.
  * @param Size Size of data to read (in bytes).
  * @retval @ref EXTMEM_DRIVER_USER_StatusTypeDef
  */
EXTMEM_DRIVER_USER_StatusTypeDef EXTMEM_DRIVER_USER_Read(EXTMEM_DRIVER_USER_ObjectTypeDef *UserObject,
                                                                uint32_t Address, uint8_t *Data, uint32_t Size)
{
  EXTMEM_DRIVER_USER_StatusTypeDef retr = EXTMEM_DRIVER_USER_OK;
  //TODO: validate address
  Address = Address & 0x03FFFFFF;
  __IO uint8_t *memAddr;

  SEMPER_Handle_t *pHandle = UserObject->PtrUserDriver;

 	if (pHandle->access != SEMPER_ACCESS_MAPPED)
 	{
 		if (SEMPER_EnableMemoryMappedMode(UserObject->PtrUserDriver) != SEMPER_OK)
 		{
 			retr = EXTMEM_DRIVER_USER_ERROR_1;
 		}
 	}

 	if (retr == EXTMEM_DRIVER_USER_OK)
 	{
 		memAddr = (__IO uint8_t *)(XSPI1_BASE + Address);
 		memcpy((void *)Data, (void *)memAddr, (size_t)Size);
 	}

  return retr;
}

/**
  * @brief Writes data to the USER memory.
  * @param UserObject Pointer to the USER driver object.
  * @param Address Memory address.
  * @param Data Pointer to the data buffer to be written.
  * @param Size Size of data to be written (in bytes).
  * @retval @ref EXTMEM_DRIVER_USER_StatusTypeDef
  */
EXTMEM_DRIVER_USER_StatusTypeDef EXTMEM_DRIVER_USER_Write(EXTMEM_DRIVER_USER_ObjectTypeDef *UserObject,
                                                                 uint32_t Address, const uint8_t *Data, uint32_t Size)
{
  EXTMEM_DRIVER_USER_StatusTypeDef retr = EXTMEM_DRIVER_USER_OK;
  Address = Address & 0x03FFFFFF;

  SEMPER_Handle_t *pHandle = UserObject->PtrUserDriver;

  if (pHandle->access != SEMPER_ACCESS_INDIRECT)
  {
  	if (SEMPER_DisableMemoryMappedMode(pHandle) != SEMPER_OK)
  	{
  		retr = EXTMEM_DRIVER_USER_ERROR_1;
  	}
  }

  if (retr == EXTMEM_DRIVER_USER_OK)
  {
  	if (SEMPER_Write(pHandle, Address, Data, Size) != SEMPER_OK)
  	{
  		retr = EXTMEM_DRIVER_USER_ERROR_2;
  	}
  }

  return retr;
}

/**
  * @brief Erases sectors in the USER memory.
  * @param UserObject Pointer to the USER driver object.
  * @param Address Memory address.
  * @param Size Size of data to erase (in bytes).
  * @retval @ref EXTMEM_DRIVER_USER_StatusTypeDef
  */
EXTMEM_DRIVER_USER_StatusTypeDef EXTMEM_DRIVER_USER_EraseSector(EXTMEM_DRIVER_USER_ObjectTypeDef *UserObject,
                                                                       uint32_t Address, uint32_t Size)
{
  EXTMEM_DRIVER_USER_StatusTypeDef retr = EXTMEM_DRIVER_USER_OK;
  Address = Address & 0x03FFFFFF;

  SEMPER_Handle_t *pHandle = UserObject->PtrUserDriver;


	if (pHandle->access != SEMPER_ACCESS_INDIRECT)
	{
  	if (SEMPER_DisableMemoryMappedMode(pHandle) != SEMPER_OK)
  	{
  		retr = EXTMEM_DRIVER_USER_ERROR_1;
  	}
	}

	if (retr == EXTMEM_DRIVER_USER_OK)
	{
		if (Size == STM32EXTLOADER_DEVICE_4K_SECTOR_SIZE)
		{
			if (SEMPER_Erase4K(pHandle, Address) != SEMPER_OK)
			{
				retr = EXTMEM_DRIVER_USER_ERROR_2;
			}
		}
		else if ((Size == STM32EXTLOADER_DEVICE_128K_SECTOR_SIZE) || (Size == STM32EXTLOADER_DEVICE_256K_SECTOR_SIZE))
		{
			if (SEMPER_Erase256K(pHandle, Address) != SEMPER_OK)
			{
				retr = EXTMEM_DRIVER_USER_ERROR_2;
			}
		}
		else
		{
			retr = EXTMEM_DRIVER_USER_ERROR_3;
		}
    
		while (SEMPER_GetStatus(pHandle) != SEMPER_OK);
	}

  return retr;
}

/**
  * @brief Performs a mass erase of the USER memory.
  * @param UserObject Pointer to the USER driver object.
  * @retval @ref EXTMEM_DRIVER_USER_StatusTypeDef
  */
EXTMEM_DRIVER_USER_StatusTypeDef EXTMEM_DRIVER_USER_MassErase(EXTMEM_DRIVER_USER_ObjectTypeDef *UserObject)
{
  EXTMEM_DRIVER_USER_StatusTypeDef retr = EXTMEM_DRIVER_USER_OK;

  SEMPER_Handle_t *pHandle = UserObject->PtrUserDriver;

  if (pHandle->access != SEMPER_ACCESS_INDIRECT)
  {
  	if (SEMPER_DisableMemoryMappedMode(pHandle) != SEMPER_OK)
  	{
  		retr = EXTMEM_DRIVER_USER_ERROR_1;
  	}
  }

  if (retr == EXTMEM_DRIVER_USER_OK)
  {
  	if (SEMPER_ChipErase(pHandle) != SEMPER_OK)
  	{
  		retr = EXTMEM_DRIVER_USER_ERROR_2;
  	}

  	while (SEMPER_GetStatus(pHandle) != SEMPER_OK);
  }

  return retr;
}

/**
  * @brief Enables memory-mapped mode for the USER device.
  * @param UserObject Pointer to the USER driver object.
  * @retval @ref EXTMEM_DRIVER_USER_StatusTypeDef
  */
EXTMEM_DRIVER_USER_StatusTypeDef EXTMEM_DRIVER_USER_Enable_MemoryMappedMode(EXTMEM_DRIVER_USER_ObjectTypeDef
    *UserObject)
{
  EXTMEM_DRIVER_USER_StatusTypeDef retr = EXTMEM_DRIVER_USER_OK;

  SEMPER_Handle_t *pHandle = UserObject->PtrUserDriver;

  if (SEMPER_EnableMemoryMappedMode(pHandle) != SEMPER_OK)
  {
  	retr = EXTMEM_DRIVER_USER_ERROR_1;
  }

  return retr;
}

/**
  * @brief Disables memory-mapped mode for the USER device.
  * @param UserObject Pointer to the USER driver object.
  * @retval @ref EXTMEM_DRIVER_USER_StatusTypeDef
  */
EXTMEM_DRIVER_USER_StatusTypeDef EXTMEM_DRIVER_USER_Disable_MemoryMappedMode(
  EXTMEM_DRIVER_USER_ObjectTypeDef *UserObject)
{
  EXTMEM_DRIVER_USER_StatusTypeDef retr = EXTMEM_DRIVER_USER_OK;

  SEMPER_Handle_t *pHandle = UserObject->PtrUserDriver;

  if (SEMPER_DisableMemoryMappedMode(pHandle) != SEMPER_OK)
  {
  	retr = EXTMEM_DRIVER_USER_ERROR_1;
  }

  return retr;
}

/**
  * @brief Retrieves the mapped address.
  * @param UserObject Pointer to the USER driver object.
  * @param BaseAddress Pointer to store the mapped base address.
  * @retval @ref EXTMEM_DRIVER_USER_StatusTypeDef
  */
EXTMEM_DRIVER_USER_StatusTypeDef EXTMEM_DRIVER_USER_GetMapAddress(EXTMEM_DRIVER_USER_ObjectTypeDef *UserObject,
                                                                         uint32_t *BaseAddress)
{
  EXTMEM_DRIVER_USER_StatusTypeDef retr = EXTMEM_DRIVER_USER_OK;

  *BaseAddress = XSPI1_BASE;

  return retr;
}

/**
 *  @}
 */

/** @addtogroup USER_Private_Functions
  * @{
  */

/**
 *  @}
 */

/**
 *  @}
 */
#endif /* EXTMEM_DRIVER_USER == 1 */
