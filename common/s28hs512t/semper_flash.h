#ifndef SEMPER_FLASH_H
#define SEMPER_FLASH_H

#include "s28hs512t.h"
#include "stm32n6xx_hal.h"

typedef enum
{
    SEMPER_OK = 0,
    SEMPER_ERROR,
    SEMPER_ERROR_PARAM,
    SEMPER_ERROR_MAPPED_LOCKED,
    SEMPER_BUSY
} SEMPER_Status_t;

typedef enum
{
    SEMPER_ACCESS_NONE = 0,
    SEMPER_ACCESS_INDIRECT,
    SEMPER_ACCESS_MAPPED
} SEMPER_Access_t;

typedef struct
{
    XSPI_HandleTypeDef *hxspi;

    SEMPER_Access_t access;

    S28HS512T_Interface_t interface;
    S28HS512T_Transfer_t transfer;
    S28HS512T_AddressWidth_t address_width;

    S28HS512T_Info_t  info;

} SEMPER_Handle_t;


/** @brief Reset and identify the flash, then configure octal DDR access. */
SEMPER_Status_t SEMPER_Init(
    SEMPER_Handle_t *dev,
    XSPI_HandleTypeDef *hxspi);

/** @brief Read flash bytes using the current indirect interface and transfer mode. */
SEMPER_Status_t SEMPER_Read(
    SEMPER_Handle_t *dev,
    uint32_t address,
    uint8_t *data,
    uint32_t size);

/** @brief Program flash bytes while splitting writes at physical page boundaries. */
SEMPER_Status_t SEMPER_Write(
    SEMPER_Handle_t *dev,
    uint32_t address,
    const uint8_t *data,
    uint32_t size);

/** @brief Issue a 4 KiB sector erase in indirect access mode. */
SEMPER_Status_t SEMPER_Erase4K(
    SEMPER_Handle_t *dev,
    uint32_t address);

/** @brief Issue a 256 KiB sector erase in indirect access mode. */
SEMPER_Status_t SEMPER_Erase256K(
    SEMPER_Handle_t *dev,
    uint32_t address);

/** @brief Issue a whole-device erase in indirect access mode. */
SEMPER_Status_t SEMPER_ChipErase(
    SEMPER_Handle_t *dev);

/** @brief Enable memory-mapped flash reads and update the handle's access state. */
SEMPER_Status_t SEMPER_EnableMemoryMappedMode(
    SEMPER_Handle_t *dev);

/** @brief Abort memory-mapped access and restore indirect flash operations. */
SEMPER_Status_t SEMPER_DisableMemoryMappedMode(
    SEMPER_Handle_t *dev);

/** @brief Switch the flash interface and transfer mode while updating its access state. */
SEMPER_Status_t SEMPER_ConfigDevice(
    SEMPER_Handle_t *dev,
    S28HS512T_Interface_t interface,
    S28HS512T_Transfer_t transfer);

/** @brief Configure the flash for octal DDR transfers. */
SEMPER_Status_t SEMPER_EnterDOPIMode(
    SEMPER_Handle_t *dev);

/** @brief Configure the flash for octal SDR transfers. */
SEMPER_Status_t SEMPER_EnterSOPIMode(
    SEMPER_Handle_t *dev);

/** @brief Return the flash from octal access to SPI mode. */
SEMPER_Status_t SEMPER_ExitOPIMode(
    SEMPER_Handle_t *dev);

/** @brief Read the flash status and report readiness, busy state, or a device error. */
SEMPER_Status_t SEMPER_GetStatus(
    SEMPER_Handle_t *dev);

/** @brief Declare the accessor for the flash memory-mapped base address. */
uint32_t SEMPER_GetMappedAddress(void);

#endif