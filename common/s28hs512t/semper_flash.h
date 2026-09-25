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


SEMPER_Status_t SEMPER_Init(
    SEMPER_Handle_t *dev,
    XSPI_HandleTypeDef *hxspi);

SEMPER_Status_t SEMPER_Read(
    SEMPER_Handle_t *dev,
    uint32_t address,
    uint8_t *data,
    uint32_t size);

SEMPER_Status_t SEMPER_Write(
    SEMPER_Handle_t *dev,
    uint32_t address,
    const uint8_t *data,
    uint32_t size);

SEMPER_Status_t SEMPER_Erase4K(
    SEMPER_Handle_t *dev,
    uint32_t address);

SEMPER_Status_t SEMPER_Erase256K(
    SEMPER_Handle_t *dev,
    uint32_t address);

SEMPER_Status_t SEMPER_ChipErase(
    SEMPER_Handle_t *dev);

SEMPER_Status_t SEMPER_EnableMemoryMappedMode(
    SEMPER_Handle_t *dev);

SEMPER_Status_t SEMPER_DisableMemoryMappedMode(
    SEMPER_Handle_t *dev);

SEMPER_Status_t SEMPER_ConfigDevice(
    SEMPER_Handle_t *dev,
    S28HS512T_Interface_t interface,
    S28HS512T_Transfer_t transfer);

SEMPER_Status_t SEMPER_EnterDOPIMode(
    SEMPER_Handle_t *dev);

SEMPER_Status_t SEMPER_EnterSOPIMode(
    SEMPER_Handle_t *dev);

SEMPER_Status_t SEMPER_ExitOPIMode(
    SEMPER_Handle_t *dev);

SEMPER_Status_t SEMPER_GetStatus(
    SEMPER_Handle_t *dev);

uint32_t SEMPER_GetMappedAddress(void);

#endif