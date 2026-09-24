/**
  ******************************************************************************
  * @file    s28hs512t.c
  * @modify  Takamura Sota
  * @brief   This file provides the S28HS512T XSPI drivers.
  ******************************************************************************
    * S28HS512T action :
  *   STR Octal IO protocol (SOPI) and DTR Octal IO protocol (DOPI) bits of
  *   Configuration Register 2 :
  *     DOPI = 1 and SOPI = 0: Operates in DTR Octal IO protocol (accepts 8-8-8 commands)
  *     DOPI = 0 and SOPI = 1: Operates in STR Octal IO protocol (accepts 8-8-8 commands)
  *     DOPI = 0 and SOPI = 0: Operates in Single IO protocol (accepts 1-1-1 commands)
  *   Enter SOPI mode by configuring DOPI = 0 and SOPI = 1 in CR2-Addr0
  *   Exit SOPI mode by configuring DOPI = 0 and SOPI = 0 in CR2-Addr0
  *   Enter DOPI mode by configuring DOPI = 1 and SOPI = 0 in CR2-Addr0
  *   Exit DOPI mode by configuring DOPI = 0 and SOPI = 0 in CR2-Addr0
  *
  *   Memory commands support STR(Single Transfer Rate) &
  *   DTR(Double Transfer Rate) modes in OPI
  *
  *   Memory commands support STR(Single Transfer Rate) &
  *   DTR(Double Transfer Rate) modes in SPI
  *
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "s28hs512t.h"

/** @addtogroup BSP
  * @{
  */

/** @addtogroup Components
  * @{
  */

/** @defgroup S28HS512T S28HS512T
  * @{
  */

/** @defgroup S28HS512T_Exported_Functions S28HS512T Exported Functions
  * @{
  */

/**
  * @brief  Get Flash information
  * @param  pInfo pointer to information structure
  * @retval error status
  */
int32_t S28HS512T_GetFlashInfo(S28HS512T_Info_t *pInfo)
{
  /* Configure the structure with the memory configuration */
  pInfo->FlashSize = S28HS512T_FLASH_SIZE;
  pInfo->EraseSectorSize = S28HS512T_SECTOR_256K;
  pInfo->EraseSectorsNumber = (S28HS512T_FLASH_SIZE / S28HS512T_SECTOR_256K);
  pInfo->EraseSubSectorSize = S28HS512T_SECTOR_4K;
  pInfo->EraseSubSectorNumber = 32U;
  pInfo->EraseSubSector1Size = S28HS512T_SECTOR_4K;
  pInfo->EraseSubSector1Number = 32U;
  pInfo->ProgPageSize = S28HS512T_PAGE_SIZE;
  pInfo->ProgPagesNumber = (S28HS512T_FLASH_SIZE / S28HS512T_PAGE_SIZE);

  return S28HS512T_OK;
};

/**
  * @brief  Polling WIP(Write In Progress) bit become to 0
  *         SPI/OPI;
  * @param  Ctx Component object pointer
  * @param  Mode Interface mode
  * @param  Rate Transfer rate
  * @retval error status
  */
int32_t S28HS512T_AutoPollingMemReady(XSPI_HandleTypeDef *Ctx, S28HS512T_Interface_t Mode,
                                        S28HS512T_Transfer_t Rate)
{
  XSPI_RegularCmdTypeDef s_command = {0};
  XSPI_AutoPollingTypeDef s_config = {0};

  /* SPI mode and DTR transfer not supported by memory */
  if ((Mode == S28HS512T_SPI_MODE) && (Rate == S28HS512T_DTR_TRANSFER))
  {
    return S28HS512T_ERROR;
  }

  /* Configure automatic polling mode to wait for memory ready */
  s_command.OperationType = HAL_XSPI_OPTYPE_COMMON_CFG;
  s_command.InstructionMode = (Mode == S28HS512T_SPI_MODE)
                                  ? HAL_XSPI_INSTRUCTION_1_LINE
                                  : HAL_XSPI_INSTRUCTION_8_LINES;
  s_command.InstructionDTRMode = (Rate == S28HS512T_DTR_TRANSFER)
                                     ? HAL_XSPI_INSTRUCTION_DTR_ENABLE
                                     : HAL_XSPI_INSTRUCTION_DTR_DISABLE;
  s_command.InstructionWidth = (Mode == S28HS512T_SPI_MODE)
                                   ? HAL_XSPI_INSTRUCTION_8_BITS
                                   : HAL_XSPI_INSTRUCTION_16_BITS;
  s_command.Instruction = (Mode == S28HS512T_SPI_MODE)
                              ? S28HS512T_READ_STATUS_REG1_CMD
                              : S28HS512T_OCTA_READ_STATUS_REG1_CMD;
  s_command.AddressMode = (Mode == S28HS512T_SPI_MODE) ? HAL_XSPI_ADDRESS_NONE : HAL_XSPI_ADDRESS_8_LINES;
  s_command.AddressDTRMode = (Rate == S28HS512T_DTR_TRANSFER)
                                 ? HAL_XSPI_ADDRESS_DTR_ENABLE
                                 : HAL_XSPI_ADDRESS_DTR_DISABLE;
  s_command.AddressWidth = HAL_XSPI_ADDRESS_32_BITS;
  s_command.Address = 0U;
  s_command.AlternateBytesMode = HAL_XSPI_ALT_BYTES_NONE;
  s_command.DataMode = (Mode == S28HS512T_SPI_MODE) ? HAL_XSPI_DATA_1_LINE : HAL_XSPI_DATA_8_LINES;
  s_command.DataDTRMode = (Rate == S28HS512T_DTR_TRANSFER)
                              ? HAL_XSPI_DATA_DTR_ENABLE
                              : HAL_XSPI_DATA_DTR_DISABLE;
  s_command.DummyCycles = (Mode == S28HS512T_SPI_MODE)
                              ? 0U
                              : ((Rate == S28HS512T_DTR_TRANSFER)
                                     ? DUMMY_CYCLES_REG_OCTAL_DTR
                                     : DUMMY_CYCLES_REG_OCTAL);
  s_command.DataLength = (Rate == S28HS512T_DTR_TRANSFER) ? 2U : 1U;
  s_command.DQSMode = (Rate == S28HS512T_DTR_TRANSFER) ? HAL_XSPI_DQS_ENABLE : HAL_XSPI_DQS_DISABLE;
 #if defined (XSPI_CCR_SIOO)
  s_command.SIOOMode            = HAL_XSPI_SIOO_INST_EVERY_CMD;
 #endif

  s_config.MatchValue = 0x00U;
  s_config.MatchMask = S28HS512T_SR1_RDYBSY;
  s_config.MatchMode = HAL_XSPI_MATCH_MODE_AND;
  s_config.IntervalTime = S28HS512T_AUTOPOLLING_INTERVAL_TIME;
  s_config.AutomaticStop = HAL_XSPI_AUTOMATIC_STOP_ENABLE;

  if (HAL_XSPI_Command(Ctx, &s_command, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return S28HS512T_ERROR;
  }

  if (HAL_XSPI_AutoPolling(Ctx, &s_config, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return S28HS512T_ERROR;
  }

  return S28HS512T_OK;
}

/* Read/Write Array Commands (3/4 Byte Address Command Set) *********************/
/**
  * @brief  Reads an amount of data from the XSPI memory on STR mode.
  *         SPI/OPI; 1-1-1/8-8-8
  * @param  Ctx Component object pointer
  * @param  Mode Interface mode
  * @param  AddressWidth Address size
  * @param  pData Pointer to data to be read
  * @param  ReadAddr Read start address
  * @param  Size Size of data to read
  * @retval XSPI memory status
  */
int32_t S28HS512T_ReadSTR(XSPI_HandleTypeDef *Ctx, S28HS512T_Interface_t Mode,
                            S28HS512T_AddressWidth_t AddressWidth, uint8_t *pData, uint32_t ReadAddr, uint32_t Size)
{
  XSPI_RegularCmdTypeDef s_command = {0};

  /* OPI mode and 3-bytes address size not supported by memory */
  if ((Mode == S28HS512T_OPI_MODE) && (AddressWidth == S28HS512T_3BYTES_SIZE))
  {
    return S28HS512T_ERROR;
  }

  /* Initialize the read command */
  s_command.OperationType = HAL_XSPI_OPTYPE_COMMON_CFG;
  s_command.InstructionMode = (Mode == S28HS512T_SPI_MODE)
                                  ? HAL_XSPI_INSTRUCTION_1_LINE
                                  : HAL_XSPI_INSTRUCTION_8_LINES;
  s_command.InstructionDTRMode = HAL_XSPI_INSTRUCTION_DTR_DISABLE;
  s_command.InstructionWidth = (Mode == S28HS512T_SPI_MODE)
                                   ? HAL_XSPI_INSTRUCTION_8_BITS
                                   : HAL_XSPI_INSTRUCTION_16_BITS;
  s_command.Instruction = (Mode == S28HS512T_SPI_MODE)
                              ? ((AddressWidth == S28HS512T_3BYTES_SIZE)
                                     ? S28HS512T_FAST_READ_CMD
                                     : S28HS512T_4_BYTE_ADDR_FAST_READ_CMD)
                              : S28HS512T_OCTA_READ_CMD;
  s_command.AddressMode = (Mode == S28HS512T_SPI_MODE)
                              ? HAL_XSPI_ADDRESS_1_LINE
                              : HAL_XSPI_ADDRESS_8_LINES;
  s_command.AddressDTRMode = HAL_XSPI_ADDRESS_DTR_DISABLE;
  s_command.AddressWidth = (AddressWidth == S28HS512T_3BYTES_SIZE)
                               ? HAL_XSPI_ADDRESS_24_BITS
                               : HAL_XSPI_ADDRESS_32_BITS;
  s_command.Address = ReadAddr;
  s_command.AlternateBytesMode = HAL_XSPI_ALT_BYTES_NONE;
  s_command.DataMode = (Mode == S28HS512T_SPI_MODE) ? HAL_XSPI_DATA_1_LINE : HAL_XSPI_DATA_8_LINES;
  s_command.DataDTRMode = HAL_XSPI_DATA_DTR_DISABLE;
  s_command.DummyCycles = (Mode == S28HS512T_SPI_MODE) ? DUMMY_CYCLES_READ : DUMMY_CYCLES_READ_OCTAL;
  s_command.DataLength = Size;
  s_command.DQSMode = HAL_XSPI_DQS_DISABLE;
 #if defined (XSPI_CCR_SIOO)
  s_command.SIOOMode            = HAL_XSPI_SIOO_INST_EVERY_CMD;
 #endif

  /* Send the command */
  if (HAL_XSPI_Command(Ctx, &s_command, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return S28HS512T_ERROR;
  }

  /* Reception of the data */
  if (HAL_XSPI_Receive(Ctx, pData, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return S28HS512T_ERROR;
  }

  return S28HS512T_OK;
}

/**
  * @brief  Reads an amount of data from the XSPI memory on DTR mode.
  *         OPI
  * @param  Ctx Component object pointer
  * @param  AddressWidth Address size
  * @param  pData Pointer to data to be read
  * @param  ReadAddr Read start address
  * @param  Size Size of data to read
  * @note   Only OPI mode support DTR transfer rate
  * @retval XSPI memory status
  */
int32_t S28HS512T_ReadDTR(XSPI_HandleTypeDef *Ctx, uint8_t *pData, uint32_t ReadAddr, uint32_t Size)
{
  XSPI_RegularCmdTypeDef s_command = {0};

  /* Initialize the read command */
  s_command.OperationType = HAL_XSPI_OPTYPE_COMMON_CFG;
  s_command.InstructionMode = HAL_XSPI_INSTRUCTION_8_LINES;
  s_command.InstructionDTRMode = HAL_XSPI_INSTRUCTION_DTR_ENABLE;
  s_command.InstructionWidth = HAL_XSPI_INSTRUCTION_16_BITS;
  s_command.Instruction = S28HS512T_OCTA_READ_DTR_CMD;
  s_command.AddressMode = HAL_XSPI_ADDRESS_8_LINES;
  s_command.AddressDTRMode = HAL_XSPI_ADDRESS_DTR_ENABLE;
  s_command.AddressWidth = HAL_XSPI_ADDRESS_32_BITS;
  s_command.Address = ReadAddr;
  s_command.AlternateBytesMode = HAL_XSPI_ALT_BYTES_NONE;
  s_command.DataMode = HAL_XSPI_DATA_8_LINES;
  s_command.DataDTRMode = HAL_XSPI_DATA_DTR_ENABLE;
  s_command.DummyCycles = DUMMY_CYCLES_READ_OCTAL_DTR;
  s_command.DataLength = Size;
  s_command.DQSMode = HAL_XSPI_DQS_ENABLE;
 #if defined (XSPI_CCR_SIOO)
  s_command.SIOOMode            = HAL_XSPI_SIOO_INST_EVERY_CMD;
 #endif

  /* Send the command */
  if (HAL_XSPI_Command(Ctx, &s_command, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return S28HS512T_ERROR;
  }

  /* Reception of the data */
  if (HAL_XSPI_Receive(Ctx, pData, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return S28HS512T_ERROR;
  }

  return S28HS512T_OK;
}

/**
  * @brief  Writes an amount of data to the XSPI memory.
  *         SPI/OPI
  * @param  Ctx Component object pointer
  * @param  Mode Interface mode
  * @param  Rate Transfer rate
  * @param  pData Pointer to data to be written
  * @param  WriteAddr Write start address
  * @param  Size Size of data to write. Range 1 ~ S28HS512T_PAGE_SIZE
  * @note   Only OPI mode support DTR transfer rate, and SPI mode with STR transfer rate. The page program
  * @retval XSPI memory status
  */
int32_t S28HS512T_PageProgram(XSPI_HandleTypeDef *Ctx, S28HS512T_Interface_t Mode, S28HS512T_Transfer_t Rate,
                                uint8_t *pData, uint32_t WriteAddr, uint32_t Size)
{
  XSPI_RegularCmdTypeDef s_command = {0};

  /* SPI mode and DTR transfer not supported by memory */
  if ((Mode == S28HS512T_SPI_MODE) && (Rate == S28HS512T_DTR_TRANSFER))
  {
    return S28HS512T_ERROR;
  }

  /* Initialize the program command */
  s_command.OperationType = HAL_XSPI_OPTYPE_COMMON_CFG;
  s_command.InstructionMode = (Mode == S28HS512T_SPI_MODE)
                                  ? HAL_XSPI_INSTRUCTION_1_LINE
                                  : HAL_XSPI_INSTRUCTION_8_LINES;
  s_command.InstructionDTRMode = (Rate == S28HS512T_DTR_TRANSFER)
                                     ? HAL_XSPI_INSTRUCTION_DTR_ENABLE
                                     : HAL_XSPI_INSTRUCTION_DTR_DISABLE;
  s_command.InstructionWidth = (Mode == S28HS512T_SPI_MODE)
                                   ? HAL_XSPI_INSTRUCTION_8_BITS
                                   : HAL_XSPI_INSTRUCTION_16_BITS;
  s_command.Instruction = (Mode == S28HS512T_SPI_MODE)
                              ? S28HS512T_4_BYTE_PAGE_PROG_CMD
                              : S28HS512T_OCTA_PAGE_PROG_CMD;
  s_command.AddressMode = (Mode == S28HS512T_SPI_MODE)
                              ? HAL_XSPI_ADDRESS_1_LINE
                              : HAL_XSPI_ADDRESS_8_LINES;
  s_command.AddressDTRMode = (Rate == S28HS512T_DTR_TRANSFER)
                                 ? HAL_XSPI_ADDRESS_DTR_ENABLE
                                 : HAL_XSPI_ADDRESS_DTR_DISABLE;
  s_command.AddressWidth = HAL_XSPI_ADDRESS_32_BITS;
  s_command.Address = WriteAddr;
  s_command.AlternateBytesMode = HAL_XSPI_ALT_BYTES_NONE;
  s_command.DataMode = (Mode == S28HS512T_SPI_MODE) ? HAL_XSPI_DATA_1_LINE : HAL_XSPI_DATA_8_LINES;
  s_command.DataDTRMode = (Rate == S28HS512T_DTR_TRANSFER) ? HAL_XSPI_DATA_DTR_ENABLE : HAL_XSPI_DATA_DTR_DISABLE;
  s_command.DummyCycles = 0U;
  s_command.DataLength = Size;
  s_command.DQSMode = HAL_XSPI_DQS_DISABLE;
 #if defined (XSPI_CCR_SIOO)
  s_command.SIOOMode            = HAL_XSPI_SIOO_INST_EVERY_CMD;
 #endif

  /* Configure the command */
  if (HAL_XSPI_Command(Ctx, &s_command, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return S28HS512T_ERROR;
  }

  /* Transmission of the data */
  if (HAL_XSPI_Transmit(Ctx, pData, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return S28HS512T_ERROR;
  }

  return S28HS512T_OK;
}

/**
  * @brief  Erases the specified block of the XSPI memory.
  *         S28HS512T support 4K, 256K size block erase commands.
  *         SPI/OPI; 1-1-1/8-8-8
  * @param  Ctx Component object pointer
  * @param  Mode Interface mode
  * @param  BlockAddress Block address to erase
  * @param  BlockSize Block size to erase
  * @retval XSPI memory status
  */

int32_t S28HS512T_BlockErase(XSPI_HandleTypeDef *Ctx, S28HS512T_Interface_t Mode, S28HS512T_Transfer_t Rate,
                               uint32_t BlockAddress, S28HS512T_Erase_t BlockSize)
{

  XSPI_RegularCmdTypeDef s_command = {0};

  /* SPI mode and DTR transfer not supported by memory */
  if ((Mode == S28HS512T_SPI_MODE) && (Rate == S28HS512T_DTR_TRANSFER))
  {
    return S28HS512T_ERROR;
  }

  /* Initialize the erase command */
  s_command.OperationType      = HAL_XSPI_OPTYPE_COMMON_CFG;
  s_command.InstructionMode    = (Mode == S28HS512T_SPI_MODE)
                                     ? HAL_XSPI_INSTRUCTION_1_LINE
                                     : HAL_XSPI_INSTRUCTION_8_LINES;
  s_command.InstructionDTRMode = (Rate == S28HS512T_DTR_TRANSFER)
                                     ? HAL_XSPI_INSTRUCTION_DTR_ENABLE
                                     : HAL_XSPI_INSTRUCTION_DTR_DISABLE;
  s_command.InstructionWidth   = (Mode == S28HS512T_SPI_MODE)
                                     ? HAL_XSPI_INSTRUCTION_8_BITS
                                     : HAL_XSPI_INSTRUCTION_16_BITS;
  s_command.AddressMode        = (Mode == S28HS512T_SPI_MODE) ? HAL_XSPI_ADDRESS_1_LINE : HAL_XSPI_ADDRESS_8_LINES;
  s_command.AddressDTRMode     = (Rate == S28HS512T_DTR_TRANSFER)
                                     ? HAL_XSPI_ADDRESS_DTR_ENABLE
                                     : HAL_XSPI_ADDRESS_DTR_DISABLE;
  s_command.AddressWidth       = HAL_XSPI_ADDRESS_32_BITS;
  s_command.Address            = BlockAddress;
  s_command.AlternateBytesMode = HAL_XSPI_ALT_BYTES_NONE;
  s_command.DataMode           = HAL_XSPI_DATA_NONE;
  s_command.DummyCycles        = 0U;
  s_command.DQSMode            = HAL_XSPI_DQS_DISABLE;
 #if defined (XSPI_CCR_SIOO)
  s_command.SIOOMode            = HAL_XSPI_SIOO_INST_EVERY_CMD;
 #endif

  switch (Mode)
  {
    case S28HS512T_OPI_MODE :
      if (BlockSize == S28HS512T_ERASE_256K)
      {
        s_command.Instruction = S28HS512T_OCTA_BLOCK_ERASE_256K_CMD;
      }
      else
      {
        s_command.Instruction = S28HS512T_OCTA_SECTOR_ERASE_4K_CMD;
      }
      break;

    case S28HS512T_SPI_MODE :
    default:
      if (BlockSize == S28HS512T_ERASE_256K)
      {
        s_command.Instruction = S28HS512T_4_BYTE_BLOCK_ERASE_256K_CMD;
      }
      else
      {
        s_command.Instruction = S28HS512T_4_BYTE_SECTOR_ERASE_4K_CMD;
      }
      break;
  }

  if (HAL_XSPI_Command(Ctx, &s_command, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return S28HS512T_ERROR;
  }


  return S28HS512T_OK;
}

/**
  * @brief  Whole chip erase.
  *         SPI/OPI; 1-0-0/8-0-0
  * @param  Ctx Component object pointer
  * @param  Mode Interface mode
  * @retval error status
  */
int32_t S28HS512T_ChipErase(XSPI_HandleTypeDef *Ctx, S28HS512T_Interface_t Mode, S28HS512T_Transfer_t Rate)
{
  XSPI_RegularCmdTypeDef s_command = {0};

  /* SPI mode and DTR transfer not supported by memory */
  if ((Mode == S28HS512T_SPI_MODE) && (Rate == S28HS512T_DTR_TRANSFER))
  {
    return S28HS512T_ERROR;
  }

  /* Initialize the erase command */
  s_command.OperationType = HAL_XSPI_OPTYPE_COMMON_CFG;
  s_command.InstructionMode = (Mode == S28HS512T_SPI_MODE)
                                  ? HAL_XSPI_INSTRUCTION_1_LINE
                                  : HAL_XSPI_INSTRUCTION_8_LINES;
  s_command.InstructionDTRMode = (Rate == S28HS512T_DTR_TRANSFER)
                                     ? HAL_XSPI_INSTRUCTION_DTR_ENABLE
                                     : HAL_XSPI_INSTRUCTION_DTR_DISABLE;
  s_command.InstructionWidth = (Mode == S28HS512T_SPI_MODE)
                                   ? HAL_XSPI_INSTRUCTION_8_BITS
                                   : HAL_XSPI_INSTRUCTION_16_BITS;
  s_command.Instruction = (Mode == S28HS512T_SPI_MODE)
                              ? S28HS512T_BULK_ERASE_CMD
                              : S28HS512T_OCTA_BULK_ERASE_CMD;
  s_command.AddressMode = HAL_XSPI_ADDRESS_NONE;
  s_command.AlternateBytesMode = HAL_XSPI_ALT_BYTES_NONE;
  s_command.DataMode = HAL_XSPI_DATA_NONE;
  s_command.DummyCycles = 0U;
  s_command.DQSMode = HAL_XSPI_DQS_DISABLE;
 #if defined (XSPI_CCR_SIOO)
  s_command.SIOOMode            = HAL_XSPI_SIOO_INST_EVERY_CMD;
 #endif

  /* Send the command */
  if (HAL_XSPI_Command(Ctx, &s_command, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return S28HS512T_ERROR;
  }

  return S28HS512T_OK;
}

/**
  * @brief  Enable memory mapped mode for the XSPI memory on STR mode.
  *         SPI/OPI; 1-1-1/8-8-8
  * @param  Ctx Component object pointer
  * @param  Mode Interface mode
  * @param  AddressWidth Address size
  * @retval XSPI memory status
  */
int32_t S28HS512T_EnableSTRMemoryMappedMode(XSPI_HandleTypeDef *Ctx, S28HS512T_Interface_t Mode,
                                              S28HS512T_AddressWidth_t AddressWidth)
{
  XSPI_RegularCmdTypeDef s_command = {0};
  XSPI_MemoryMappedTypeDef s_mem_mapped_cfg = {0};

  /* OPI mode and 3-bytes address size not supported by memory */
  if ((Mode == S28HS512T_OPI_MODE) && (AddressWidth == S28HS512T_3BYTES_SIZE))
  {
    return S28HS512T_ERROR;
  }

  /* Initialize the read command */
  s_command.OperationType = HAL_XSPI_OPTYPE_READ_CFG;
  s_command.InstructionMode = (Mode == S28HS512T_SPI_MODE)
                                  ? HAL_XSPI_INSTRUCTION_1_LINE
                                  : HAL_XSPI_INSTRUCTION_8_LINES;
  s_command.InstructionDTRMode = HAL_XSPI_INSTRUCTION_DTR_DISABLE;
  s_command.InstructionWidth = (Mode == S28HS512T_SPI_MODE)
                                   ? HAL_XSPI_INSTRUCTION_8_BITS
                                   : HAL_XSPI_INSTRUCTION_16_BITS;
  s_command.Instruction = (Mode == S28HS512T_SPI_MODE)
                              ? (AddressWidth == S28HS512T_3BYTES_SIZE)
                                    ? S28HS512T_FAST_READ_CMD
                                    : S28HS512T_4_BYTE_ADDR_FAST_READ_CMD
                              : S28HS512T_OCTA_READ_CMD;
  s_command.AddressMode = (Mode == S28HS512T_SPI_MODE) ? HAL_XSPI_ADDRESS_1_LINE : HAL_XSPI_ADDRESS_8_LINES;
  s_command.AddressDTRMode = HAL_XSPI_ADDRESS_DTR_DISABLE;
  s_command.AddressWidth = (AddressWidth == S28HS512T_3BYTES_SIZE)
                               ? HAL_XSPI_ADDRESS_24_BITS
                               : HAL_XSPI_ADDRESS_32_BITS;
  s_command.AlternateBytesMode = HAL_XSPI_ALT_BYTES_NONE;
  s_command.DataMode = (Mode == S28HS512T_SPI_MODE) ? HAL_XSPI_DATA_1_LINE : HAL_XSPI_DATA_8_LINES;
  s_command.DataDTRMode = HAL_XSPI_DATA_DTR_DISABLE;
  s_command.DummyCycles = (Mode == S28HS512T_SPI_MODE) ? DUMMY_CYCLES_READ : DUMMY_CYCLES_READ_OCTAL;
  s_command.DQSMode = HAL_XSPI_DQS_DISABLE;
 #if defined (XSPI_CCR_SIOO)
  s_command.SIOOMode            = HAL_XSPI_SIOO_INST_EVERY_CMD;
 #endif

  /* Send the read command */
  if (HAL_XSPI_Command(Ctx, &s_command, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return S28HS512T_ERROR;
  }

  /* Initialize the program command */
  s_command.OperationType = HAL_XSPI_OPTYPE_WRITE_CFG;
  s_command.Instruction = (Mode == S28HS512T_SPI_MODE)
                              ? S28HS512T_4_BYTE_PAGE_PROG_CMD
                              : S28HS512T_OCTA_PAGE_PROG_CMD;
  s_command.DummyCycles = 0U;

  /* Send the write command */
  if (HAL_XSPI_Command(Ctx, &s_command, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return S28HS512T_ERROR;
  }

  /* Configure the memory mapped mode */
  s_mem_mapped_cfg.TimeOutActivation = HAL_XSPI_TIMEOUT_COUNTER_DISABLE;

  if (HAL_XSPI_MemoryMapped(Ctx, &s_mem_mapped_cfg) != HAL_OK)
  {
    return S28HS512T_ERROR;
  }

  return S28HS512T_OK;
}

/**
  * @brief  Enable memory mapped mode for the XSPI memory on DTR mode.
  * @param  Ctx Component object pointer
  * @param  Mode Interface mode
  * @param  AddressWidth Address size
  * @note   Only OPI mode support DTR transfer rate
  * @retval XSPI memory status
  */
int32_t S28HS512T_EnableDTRMemoryMappedMode(XSPI_HandleTypeDef *Ctx, S28HS512T_Interface_t Mode)
{
  /* Prevent unused argument(s) compilation warning */
  UNUSED(Mode);

  XSPI_RegularCmdTypeDef s_command = {0};
  XSPI_MemoryMappedTypeDef s_mem_mapped_cfg = {0};

  /* Initialize the read command */
  s_command.OperationType = HAL_XSPI_OPTYPE_READ_CFG;
  s_command.InstructionMode = HAL_XSPI_INSTRUCTION_8_LINES;
  s_command.InstructionDTRMode = HAL_XSPI_INSTRUCTION_DTR_ENABLE;
  s_command.InstructionWidth = HAL_XSPI_INSTRUCTION_16_BITS;
  s_command.Instruction = S28HS512T_OCTA_READ_DTR_CMD;
  s_command.AddressMode = HAL_XSPI_ADDRESS_8_LINES;
  s_command.AddressDTRMode = HAL_XSPI_ADDRESS_DTR_ENABLE;
  s_command.AddressWidth = HAL_XSPI_ADDRESS_32_BITS;
  s_command.AlternateBytesMode = HAL_XSPI_ALT_BYTES_NONE;
  s_command.DataMode = HAL_XSPI_DATA_8_LINES;
  s_command.DataDTRMode = HAL_XSPI_DATA_DTR_ENABLE;
  s_command.DummyCycles = DUMMY_CYCLES_READ_OCTAL_DTR;
  s_command.DQSMode = HAL_XSPI_DQS_ENABLE;
 #if defined (XSPI_CCR_SIOO)
  s_command.SIOOMode            = HAL_XSPI_SIOO_INST_EVERY_CMD;
 #endif

  /* Send the command */
  if (HAL_XSPI_Command(Ctx, &s_command, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return S28HS512T_ERROR;
  }

  /* Initialize the program command */
  s_command.OperationType = HAL_XSPI_OPTYPE_WRITE_CFG;
  s_command.Instruction = S28HS512T_OCTA_PAGE_PROG_CMD;
  s_command.DummyCycles = 0U;
  s_command.DQSMode = HAL_XSPI_DQS_DISABLE;

  /* Send the command */
  if (HAL_XSPI_Command(Ctx, &s_command, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return S28HS512T_ERROR;
  }
  /* Configure the memory mapped mode */
  s_mem_mapped_cfg.TimeOutActivation = HAL_XSPI_TIMEOUT_COUNTER_DISABLE;

  if (HAL_XSPI_MemoryMapped(Ctx, &s_mem_mapped_cfg) != HAL_OK)
  {
    return S28HS512T_ERROR;
  }

  return S28HS512T_OK;
}

/**
  * @brief  Flash suspend program or erase command
  *         SPI/OPI
  * @param  Ctx Component object pointer
  * @param  Mode Interface select
  * @param  Rate Transfer rate STR or DTR
  * @retval error status
  */
int32_t S28HS512T_Suspend(XSPI_HandleTypeDef *Ctx, S28HS512T_Interface_t Mode, S28HS512T_Transfer_t Rate)
{
  XSPI_RegularCmdTypeDef s_command = {0};

  /* SPI mode and DTR transfer not supported by memory */
  if ((Mode == S28HS512T_SPI_MODE) && (Rate == S28HS512T_DTR_TRANSFER))
  {
    return S28HS512T_ERROR;
  }

  /* Initialize the suspend command */
  s_command.OperationType = HAL_XSPI_OPTYPE_COMMON_CFG;
  s_command.InstructionMode = (Mode == S28HS512T_SPI_MODE)
                                  ? HAL_XSPI_INSTRUCTION_1_LINE
                                  : HAL_XSPI_INSTRUCTION_8_LINES;
  s_command.InstructionDTRMode = (Rate == S28HS512T_DTR_TRANSFER)
                                     ? HAL_XSPI_INSTRUCTION_DTR_ENABLE
                                     : HAL_XSPI_INSTRUCTION_DTR_DISABLE;
  s_command.InstructionWidth = (Mode == S28HS512T_SPI_MODE)
                                   ? HAL_XSPI_INSTRUCTION_8_BITS
                                   : HAL_XSPI_INSTRUCTION_16_BITS;
  s_command.Instruction = (Mode == S28HS512T_SPI_MODE)
                              ? S28HS512T_PROG_ERASE_SUSPEND_CMD
                              : S28HS512T_OCTA_PROG_ERASE_SUSPEND_CMD;
  s_command.AddressMode = HAL_XSPI_ADDRESS_NONE;
  s_command.AlternateBytesMode = HAL_XSPI_ALT_BYTES_NONE;
  s_command.DataMode = HAL_XSPI_DATA_NONE;
  s_command.DummyCycles = 0U;
  s_command.DQSMode = HAL_XSPI_DQS_DISABLE;
 #if defined (XSPI_CCR_SIOO)
  s_command.SIOOMode            = HAL_XSPI_SIOO_INST_EVERY_CMD;
 #endif

  /* Send the command */
  if (HAL_XSPI_Command(Ctx, &s_command, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return S28HS512T_ERROR;
  }

  return S28HS512T_OK;
}

/**
  * @brief  Flash resume program or erase command
  *         SPI/OPI
  * @param  Ctx Component object pointer
  * @param  Mode Interface select
  * @param  Rate Transfer rate STR or DTR
  * @retval error status
  */
int32_t S28HS512T_Resume(XSPI_HandleTypeDef *Ctx, S28HS512T_Interface_t Mode, S28HS512T_Transfer_t Rate)
{
  XSPI_RegularCmdTypeDef s_command = {0};

  /* SPI mode and DTR transfer not supported by memory */
  if ((Mode == S28HS512T_SPI_MODE) && (Rate == S28HS512T_DTR_TRANSFER))
  {
    return S28HS512T_ERROR;
  }

  /* Initialize the resume command */
  s_command.OperationType = HAL_XSPI_OPTYPE_COMMON_CFG;
  s_command.InstructionMode = (Mode == S28HS512T_SPI_MODE)
                                  ? HAL_XSPI_INSTRUCTION_1_LINE
                                  : HAL_XSPI_INSTRUCTION_8_LINES;
  s_command.InstructionDTRMode = (Rate == S28HS512T_DTR_TRANSFER)
                                     ? HAL_XSPI_INSTRUCTION_DTR_ENABLE
                                     : HAL_XSPI_INSTRUCTION_DTR_DISABLE;
  s_command.InstructionWidth = (Mode == S28HS512T_SPI_MODE)
                                   ? HAL_XSPI_INSTRUCTION_8_BITS
                                   : HAL_XSPI_INSTRUCTION_16_BITS;
  s_command.Instruction = (Mode == S28HS512T_SPI_MODE)
                              ? S28HS512T_PROG_ERASE_RESUME_CMD
                              : S28HS512T_OCTA_PROG_ERASE_RESUME_CMD;
  s_command.AddressMode = HAL_XSPI_ADDRESS_NONE;
  s_command.AlternateBytesMode = HAL_XSPI_ALT_BYTES_NONE;
  s_command.DataMode = HAL_XSPI_DATA_NONE;
  s_command.DummyCycles = 0U;
  s_command.DQSMode = HAL_XSPI_DQS_DISABLE;
 #if defined (XSPI_CCR_SIOO)
  s_command.SIOOMode            = HAL_XSPI_SIOO_INST_EVERY_CMD;
 #endif

  /* Send the command */
  if (HAL_XSPI_Command(Ctx, &s_command, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return S28HS512T_ERROR;
  }

  return S28HS512T_OK;
}

/* Register/Setting Commands **************************************************/
/**
  * @brief  This function send a Write Enable and wait it is effective.
  *         SPI/OPI
  * @param  Ctx Component object pointer
  * @param  Mode Interface mode
  * @param  Rate Transfer rate STR or DTR
  * @retval error status
  */
int32_t S28HS512T_WriteEnable(XSPI_HandleTypeDef *Ctx, S28HS512T_Interface_t Mode, S28HS512T_Transfer_t Rate)
{
  XSPI_RegularCmdTypeDef s_command = {0};
  XSPI_AutoPollingTypeDef s_config = {0};

  /* SPI mode and DTR transfer not supported by memory */
  if ((Mode == S28HS512T_SPI_MODE) && (Rate == S28HS512T_DTR_TRANSFER))
  {
    return S28HS512T_ERROR;
  }

  /* Initialize the write enable command */
  s_command.OperationType = HAL_XSPI_OPTYPE_COMMON_CFG;
  s_command.InstructionMode = (Mode == S28HS512T_SPI_MODE)
                                  ? HAL_XSPI_INSTRUCTION_1_LINE
                                  : HAL_XSPI_INSTRUCTION_8_LINES;
  s_command.InstructionDTRMode = (Rate == S28HS512T_DTR_TRANSFER)
                                     ? HAL_XSPI_INSTRUCTION_DTR_ENABLE
                                     : HAL_XSPI_INSTRUCTION_DTR_DISABLE;
  s_command.InstructionWidth = (Mode == S28HS512T_SPI_MODE)
                                   ? HAL_XSPI_INSTRUCTION_8_BITS
                                   : HAL_XSPI_INSTRUCTION_16_BITS;
  s_command.Instruction = (Mode == S28HS512T_SPI_MODE)
                              ? S28HS512T_WRITE_ENABLE_CMD
                              : S28HS512T_OCTA_WRITE_ENABLE_CMD;
  s_command.AddressMode = HAL_XSPI_ADDRESS_NONE;
  s_command.AlternateBytesMode = HAL_XSPI_ALT_BYTES_NONE;
  s_command.DataMode = HAL_XSPI_DATA_NONE;
  s_command.DummyCycles = 0U;
  s_command.DQSMode = HAL_XSPI_DQS_DISABLE;
 #if defined (XSPI_CCR_SIOO)
  s_command.SIOOMode            = HAL_XSPI_SIOO_INST_EVERY_CMD;
 #endif

  /* Send the command */
  if (HAL_XSPI_Command(Ctx, &s_command, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return S28HS512T_ERROR;
  }

  /* Configure automatic polling mode to wait for write enabling */
  s_command.Instruction = (Mode == S28HS512T_SPI_MODE)
                              ? S28HS512T_READ_STATUS_REG1_CMD
                              : S28HS512T_OCTA_READ_STATUS_REG1_CMD;
  s_command.AddressMode = (Mode == S28HS512T_SPI_MODE) ? HAL_XSPI_ADDRESS_NONE : HAL_XSPI_ADDRESS_8_LINES;
  s_command.AddressDTRMode = (Rate == S28HS512T_DTR_TRANSFER)
                                 ? HAL_XSPI_ADDRESS_DTR_ENABLE
                                 : HAL_XSPI_ADDRESS_DTR_DISABLE;
  s_command.AddressWidth = HAL_XSPI_ADDRESS_32_BITS;
  s_command.Address = 0U;
  s_command.DataMode = (Mode == S28HS512T_SPI_MODE) ? HAL_XSPI_DATA_1_LINE : HAL_XSPI_DATA_8_LINES;
  s_command.DataDTRMode = (Rate == S28HS512T_DTR_TRANSFER) ? HAL_XSPI_DATA_DTR_ENABLE : HAL_XSPI_DATA_DTR_DISABLE;
  s_command.DummyCycles = (Mode == S28HS512T_SPI_MODE)
                              ? 0U
                              : ((Rate == S28HS512T_DTR_TRANSFER)
                                     ? DUMMY_CYCLES_REG_OCTAL_DTR
                                     : DUMMY_CYCLES_REG_OCTAL);
  s_command.DataLength = (Rate == S28HS512T_DTR_TRANSFER) ? 2U : 1U;
  s_command.DQSMode = (Rate == S28HS512T_DTR_TRANSFER) ? HAL_XSPI_DQS_ENABLE : HAL_XSPI_DQS_DISABLE;

  /* Send the command */
  if (HAL_XSPI_Command(Ctx, &s_command, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return S28HS512T_ERROR;
  }

  s_config.MatchValue = 0x02U;
  s_config.MatchMask = S28HS512T_SR1_WRPGEN;
  s_config.MatchMode = HAL_XSPI_MATCH_MODE_AND;
  s_config.IntervalTime = S28HS512T_AUTOPOLLING_INTERVAL_TIME;
  s_config.AutomaticStop = HAL_XSPI_AUTOMATIC_STOP_ENABLE;

  if (HAL_XSPI_AutoPolling(Ctx, &s_config, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return S28HS512T_ERROR;
  }

  return S28HS512T_OK;
}

/**
  * @brief  This function reset the (WEN) Write Enable Latch bit.
  *         SPI/OPI
  * @param  Ctx Component object pointer
  * @param  Mode Interface mode
  * @param  Rate Transfer rate STR or DTR
  * @retval error status
  */
int32_t S28HS512T_WriteDisable(XSPI_HandleTypeDef *Ctx, S28HS512T_Interface_t Mode, S28HS512T_Transfer_t Rate)
{
  XSPI_RegularCmdTypeDef s_command = {0};

  /* SPI mode and DTR transfer not supported by memory */
  if ((Mode == S28HS512T_SPI_MODE) && (Rate == S28HS512T_DTR_TRANSFER))
  {
    return S28HS512T_ERROR;
  }

  /* Initialize the write disable command */
  s_command.OperationType = HAL_XSPI_OPTYPE_COMMON_CFG;
  s_command.InstructionMode = (Mode == S28HS512T_SPI_MODE)
                                  ? HAL_XSPI_INSTRUCTION_1_LINE
                                  : HAL_XSPI_INSTRUCTION_8_LINES;
  s_command.InstructionDTRMode = (Rate == S28HS512T_DTR_TRANSFER)
                                     ? HAL_XSPI_INSTRUCTION_DTR_ENABLE
                                     : HAL_XSPI_INSTRUCTION_DTR_DISABLE;
  s_command.InstructionWidth = (Mode == S28HS512T_SPI_MODE)
                                   ? HAL_XSPI_INSTRUCTION_8_BITS
                                   : HAL_XSPI_INSTRUCTION_16_BITS;
  s_command.Instruction = (Mode == S28HS512T_SPI_MODE)
                              ? S28HS512T_WRITE_DISABLE_CMD
                              : S28HS512T_OCTA_WRITE_DISABLE_CMD;
  s_command.AddressMode = HAL_XSPI_ADDRESS_NONE;
  s_command.AlternateBytesMode = HAL_XSPI_ALT_BYTES_NONE;
  s_command.DataMode = HAL_XSPI_DATA_NONE;
  s_command.DummyCycles = 0U;
  s_command.DQSMode = HAL_XSPI_DQS_DISABLE;
 #if defined (XSPI_CCR_SIOO)
  s_command.SIOOMode            = HAL_XSPI_SIOO_INST_EVERY_CMD;
 #endif

  /* Send the command */
  if (HAL_XSPI_Command(Ctx, &s_command, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return S28HS512T_ERROR;
  }

  return S28HS512T_OK;
}

/**
  * @brief  Read Flash Status register 1 value
  *         SPI/OPI
  * @param  Ctx Component object pointer
  * @param  Mode Interface mode
  * @param  Rate Transfer rate STR or DTR
  * @param  Value Status register value pointer
  * @retval error status
  */
int32_t S28HS512T_ReadStatusRegister1(XSPI_HandleTypeDef *Ctx, S28HS512T_Interface_t Mode,
                                       S28HS512T_Transfer_t Rate, uint8_t *Value)
{
  XSPI_RegularCmdTypeDef s_command = {0};

  /* SPI mode and DTR transfer not supported by memory */
  if ((Mode == S28HS512T_SPI_MODE) && (Rate == S28HS512T_DTR_TRANSFER))
  {
    return S28HS512T_ERROR;
  }

  /* Initialize the reading of status register */
  s_command.OperationType = HAL_XSPI_OPTYPE_COMMON_CFG;
  s_command.InstructionMode = (Mode == S28HS512T_SPI_MODE)
                                  ? HAL_XSPI_INSTRUCTION_1_LINE
                                  : HAL_XSPI_INSTRUCTION_8_LINES;
  s_command.InstructionDTRMode = (Rate == S28HS512T_DTR_TRANSFER)
                                     ? HAL_XSPI_INSTRUCTION_DTR_ENABLE
                                     : HAL_XSPI_INSTRUCTION_DTR_DISABLE;
  s_command.InstructionWidth = (Mode == S28HS512T_SPI_MODE)
                                   ? HAL_XSPI_INSTRUCTION_8_BITS
                                   : HAL_XSPI_INSTRUCTION_16_BITS;
  s_command.Instruction = (Mode == S28HS512T_SPI_MODE)
                              ? S28HS512T_READ_STATUS_REG1_CMD
                              : S28HS512T_OCTA_READ_STATUS_REG1_CMD;
  s_command.AddressMode = (Mode == S28HS512T_SPI_MODE) ? HAL_XSPI_ADDRESS_NONE : HAL_XSPI_ADDRESS_8_LINES;
  s_command.AddressDTRMode = (Rate == S28HS512T_DTR_TRANSFER)
                                 ? HAL_XSPI_ADDRESS_DTR_ENABLE
                                 : HAL_XSPI_ADDRESS_DTR_DISABLE;
  s_command.AddressWidth = HAL_XSPI_ADDRESS_32_BITS;
  s_command.Address = 0U;
  s_command.AlternateBytesMode = HAL_XSPI_ALT_BYTES_NONE;
  s_command.DataMode = (Mode == S28HS512T_SPI_MODE) ? HAL_XSPI_DATA_1_LINE : HAL_XSPI_DATA_8_LINES;
  s_command.DataDTRMode = (Rate == S28HS512T_DTR_TRANSFER)
                              ? HAL_XSPI_DATA_DTR_ENABLE
                              : HAL_XSPI_DATA_DTR_DISABLE;
  s_command.DummyCycles = (Mode == S28HS512T_SPI_MODE)
                              ? 0U
                              : ((Rate == S28HS512T_DTR_TRANSFER)
                                     ? DUMMY_CYCLES_REG_OCTAL_DTR
                                     : DUMMY_CYCLES_REG_OCTAL);
  s_command.DataLength = (Rate == S28HS512T_DTR_TRANSFER) ? 2U : 1U;
  s_command.DQSMode = (Rate == S28HS512T_DTR_TRANSFER) ? HAL_XSPI_DQS_ENABLE : HAL_XSPI_DQS_DISABLE;
 #if defined (XSPI_CCR_SIOO)
  s_command.SIOOMode            = HAL_XSPI_SIOO_INST_EVERY_CMD;
 #endif

  /* Send the command */
  if (HAL_XSPI_Command(Ctx, &s_command, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return S28HS512T_ERROR;
  }

  /* Reception of the data */
  if (HAL_XSPI_Receive(Ctx, Value, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return S28HS512T_ERROR;
  }

  return S28HS512T_OK;
}

/**
  * @brief  Read Flash Status register 2 value
  *         SPI/OPI
  * @param  Ctx Component object pointer
  * @param  Mode Interface mode
  * @param  Rate Transfer rate STR or DTR
  * @param  Value Status register value pointer
  * @retval error status
  */
int32_t S28HS512T_ReadStatusRegister2(XSPI_HandleTypeDef *Ctx, S28HS512T_Interface_t Mode,
                                       S28HS512T_Transfer_t Rate, uint8_t *Value)
{
  XSPI_RegularCmdTypeDef s_command = {0};

  /* SPI mode and DTR transfer not supported by memory */
  if ((Mode == S28HS512T_SPI_MODE) && (Rate == S28HS512T_DTR_TRANSFER))
  {
    return S28HS512T_ERROR;
  }

  /* Initialize the reading of status register */
  s_command.OperationType = HAL_XSPI_OPTYPE_COMMON_CFG;
  s_command.InstructionMode = (Mode == S28HS512T_SPI_MODE)
                                  ? HAL_XSPI_INSTRUCTION_1_LINE
                                  : HAL_XSPI_INSTRUCTION_8_LINES;
  s_command.InstructionDTRMode = (Rate == S28HS512T_DTR_TRANSFER)
                                     ? HAL_XSPI_INSTRUCTION_DTR_ENABLE
                                     : HAL_XSPI_INSTRUCTION_DTR_DISABLE;
  s_command.InstructionWidth = (Mode == S28HS512T_SPI_MODE)
                                   ? HAL_XSPI_INSTRUCTION_8_BITS
                                   : HAL_XSPI_INSTRUCTION_16_BITS;
  s_command.Instruction = (Mode == S28HS512T_SPI_MODE)
                              ? S28HS512T_READ_STATUS_REG2_CMD
                              : S28HS512T_OCTA_READ_STATUS_REG2_CMD;
  s_command.AddressMode = (Mode == S28HS512T_SPI_MODE) ? HAL_XSPI_ADDRESS_NONE : HAL_XSPI_ADDRESS_8_LINES;
  s_command.AddressDTRMode = (Rate == S28HS512T_DTR_TRANSFER)
                                 ? HAL_XSPI_ADDRESS_DTR_ENABLE
                                 : HAL_XSPI_ADDRESS_DTR_DISABLE;
  s_command.AddressWidth = HAL_XSPI_ADDRESS_32_BITS;
  s_command.Address = 0U;
  s_command.AlternateBytesMode = HAL_XSPI_ALT_BYTES_NONE;
  s_command.DataMode = (Mode == S28HS512T_SPI_MODE) ? HAL_XSPI_DATA_1_LINE : HAL_XSPI_DATA_8_LINES;
  s_command.DataDTRMode = (Rate == S28HS512T_DTR_TRANSFER)
                              ? HAL_XSPI_DATA_DTR_ENABLE
                              : HAL_XSPI_DATA_DTR_DISABLE;
  s_command.DummyCycles = (Mode == S28HS512T_SPI_MODE)
                              ? 0U
                              : ((Rate == S28HS512T_DTR_TRANSFER)
                                     ? DUMMY_CYCLES_REG_OCTAL_DTR
                                     : DUMMY_CYCLES_REG_OCTAL);
  s_command.DataLength = (Rate == S28HS512T_DTR_TRANSFER) ? 2U : 1U;
  s_command.DQSMode = (Rate == S28HS512T_DTR_TRANSFER) ? HAL_XSPI_DQS_ENABLE : HAL_XSPI_DQS_DISABLE;
 #if defined (XSPI_CCR_SIOO)
  s_command.SIOOMode            = HAL_XSPI_SIOO_INST_EVERY_CMD;
 #endif

  /* Send the command */
  if (HAL_XSPI_Command(Ctx, &s_command, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return S28HS512T_ERROR;
  }

  /* Reception of the data */
  if (HAL_XSPI_Receive(Ctx, Value, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return S28HS512T_ERROR;
  }

  return S28HS512T_OK;
}

/**
  * @brief  Read Flash any register value
  *         SPI/OPI
  * @param  Ctx Component object pointer
  * @param  Mode Interface mode
  * @param  Rate Transfer rate STR or DTR
  * @param  AddressWidth Address size
  * @param  ReadAddr Address to read from
  * @param  Value any register value pointer
  * @retval error status
  */
int32_t S28HS512T_ReadAnyRegister(XSPI_HandleTypeDef *Ctx, S28HS512T_Interface_t Mode,
                                     S28HS512T_Transfer_t Rate, S28HS512T_AddressWidth_t AddressWidth,
                                     uint32_t ReadAddr, uint8_t *Value)
{
  XSPI_RegularCmdTypeDef s_command = {0};

  /* SPI mode and DTR transfer not supported by memory */
  if ((Mode == S28HS512T_SPI_MODE) && (Rate == S28HS512T_DTR_TRANSFER))
  {
    return S28HS512T_ERROR;
  }

  /* Initialize the reading of status register */
  s_command.OperationType = HAL_XSPI_OPTYPE_COMMON_CFG;
  s_command.InstructionMode = (Mode == S28HS512T_SPI_MODE)
                                  ? HAL_XSPI_INSTRUCTION_1_LINE
                                  : HAL_XSPI_INSTRUCTION_8_LINES;
  s_command.InstructionDTRMode = (Rate == S28HS512T_DTR_TRANSFER)
                                     ? HAL_XSPI_INSTRUCTION_DTR_ENABLE
                                     : HAL_XSPI_INSTRUCTION_DTR_DISABLE;
  s_command.InstructionWidth = (Mode == S28HS512T_SPI_MODE)
                                   ? HAL_XSPI_INSTRUCTION_8_BITS
                                   : HAL_XSPI_INSTRUCTION_16_BITS;
  s_command.Instruction = (Mode == S28HS512T_SPI_MODE)
                              ? S28HS512T_READ_ANY_REG_CMD
                              : S28HS512T_OCTA_READ_ANY_REG_CMD;
  s_command.AddressMode = (Mode == S28HS512T_SPI_MODE) ? HAL_XSPI_ADDRESS_1_LINE : HAL_XSPI_ADDRESS_8_LINES;
  s_command.AddressDTRMode = (Rate == S28HS512T_DTR_TRANSFER)
                                 ? HAL_XSPI_ADDRESS_DTR_ENABLE
                                 : HAL_XSPI_ADDRESS_DTR_DISABLE;
  s_command.AddressWidth = (Mode == S28HS512T_SPI_MODE)
                               ? ((AddressWidth == S28HS512T_3BYTES_SIZE)
                                      ? HAL_XSPI_ADDRESS_24_BITS
                                      : HAL_XSPI_ADDRESS_32_BITS)
                               : HAL_XSPI_ADDRESS_32_BITS;
  s_command.Address = ReadAddr;
  s_command.AlternateBytesMode = HAL_XSPI_ALT_BYTES_NONE;
  s_command.DataMode = (Mode == S28HS512T_SPI_MODE) ? HAL_XSPI_DATA_1_LINE : HAL_XSPI_DATA_8_LINES;
  s_command.DataDTRMode = (Rate == S28HS512T_DTR_TRANSFER)
                              ? HAL_XSPI_DATA_DTR_ENABLE
                              : HAL_XSPI_DATA_DTR_DISABLE;
  s_command.DummyCycles = (Mode == S28HS512T_SPI_MODE)
                              ? 0U
                              : ((Rate == S28HS512T_DTR_TRANSFER)
                                     ? DUMMY_CYCLES_REG_OCTAL_DTR
                                     : DUMMY_CYCLES_REG_OCTAL);
  s_command.DataLength = (Rate == S28HS512T_DTR_TRANSFER) ? 2U : 1U;
  s_command.DQSMode = (Rate == S28HS512T_DTR_TRANSFER) ? HAL_XSPI_DQS_ENABLE : HAL_XSPI_DQS_DISABLE;
 #if defined (XSPI_CCR_SIOO)
  s_command.SIOOMode            = HAL_XSPI_SIOO_INST_EVERY_CMD;
 #endif

  /* Send the command */
  if (HAL_XSPI_Command(Ctx, &s_command, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return S28HS512T_ERROR;
  }

  /* Reception of the data */
  if (HAL_XSPI_Receive(Ctx, Value, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return S28HS512T_ERROR;
  }

  return S28HS512T_OK;
}

/**
  * @brief  Write Flash any register
  *         SPI/OPI
  * @param  Ctx Component object pointer
  * @param  Mode Interface mode
  * @param  Rate Transfer rate STR or DTR
  * @param  AddressWidth Address size
  * @param  WriteAddr Address to write to
  * @param  Value Value to write to any register
  * @retval error status
  */
int32_t S28HS512T_WriteAnyRegister(XSPI_HandleTypeDef *Ctx, S28HS512T_Interface_t Mode,
                                      S28HS512T_Transfer_t Rate,  S28HS512T_AddressWidth_t AddressWidth,
                                      uint32_t WriteAddr, uint8_t Value)
{
  XSPI_RegularCmdTypeDef s_command = {0};

  /* SPI mode and DTR transfer not supported by memory */
  if ((Mode == S28HS512T_SPI_MODE) && (Rate == S28HS512T_DTR_TRANSFER))
  {
    return S28HS512T_ERROR;
  }

  /* Initialize the writing of configuration register 2 */
  s_command.OperationType = HAL_XSPI_OPTYPE_COMMON_CFG;
  s_command.InstructionMode = (Mode == S28HS512T_SPI_MODE)
                                  ? HAL_XSPI_INSTRUCTION_1_LINE
                                  : HAL_XSPI_INSTRUCTION_8_LINES;
  s_command.InstructionDTRMode = (Rate == S28HS512T_DTR_TRANSFER)
                                     ? HAL_XSPI_INSTRUCTION_DTR_ENABLE
                                     : HAL_XSPI_INSTRUCTION_DTR_DISABLE;
  s_command.InstructionWidth = (Mode == S28HS512T_SPI_MODE)
                                   ? HAL_XSPI_INSTRUCTION_8_BITS
                                   : HAL_XSPI_INSTRUCTION_16_BITS;
  s_command.Instruction = (Mode == S28HS512T_SPI_MODE)
                              ? S28HS512T_WRITE_ANY_REG_CMD
                              : S28HS512T_OCTA_WRITE_ANY_REG_CMD;
  s_command.AddressMode = (Mode == S28HS512T_SPI_MODE) ? HAL_XSPI_ADDRESS_1_LINE : HAL_XSPI_ADDRESS_8_LINES;
  s_command.AddressDTRMode = (Rate == S28HS512T_DTR_TRANSFER)
                                 ? HAL_XSPI_ADDRESS_DTR_ENABLE
                                 : HAL_XSPI_ADDRESS_DTR_DISABLE;
  s_command.AddressWidth = (Mode == S28HS512T_SPI_MODE)
                               ? ((AddressWidth == S28HS512T_3BYTES_SIZE)
                                      ? HAL_XSPI_ADDRESS_24_BITS
                                      : HAL_XSPI_ADDRESS_32_BITS)
                               : HAL_XSPI_ADDRESS_32_BITS;
  s_command.Address = WriteAddr;
  s_command.AlternateBytesMode = HAL_XSPI_ALT_BYTES_NONE;
  s_command.DataMode = (Mode == S28HS512T_SPI_MODE) ? HAL_XSPI_DATA_1_LINE : HAL_XSPI_DATA_8_LINES;
  s_command.DataDTRMode = (Rate == S28HS512T_DTR_TRANSFER)
                              ? HAL_XSPI_DATA_DTR_ENABLE
                              : HAL_XSPI_DATA_DTR_DISABLE;
  s_command.DummyCycles = 0U;
  s_command.DataLength = (Rate == S28HS512T_DTR_TRANSFER) ? 2U : 1U;
  s_command.DQSMode = HAL_XSPI_DQS_DISABLE;
 #if defined (XSPI_CCR_SIOO)
  s_command.SIOOMode            = HAL_XSPI_SIOO_INST_EVERY_CMD;
 #endif

  /* Send the command */
  if (HAL_XSPI_Command(Ctx, &s_command, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return S28HS512T_ERROR;
  }

  if (HAL_XSPI_Transmit(Ctx, &Value, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return S28HS512T_ERROR;
  }

  return S28HS512T_OK;
}

/* ID Commands ****************************************************************/
/**
  * @brief  Read Flash 3 Byte IDs.
  *         Manufacturer ID, Memory type, Memory density
  *         SPI/OPI; 1-0-1/8-8-8
  * @param  Ctx Component object pointer
  * @param  Mode Interface mode
  * @param  ID 3 bytes IDs pointer
  * @param  DualFlash Dual flash mode state
  * @retval error status
  */
int32_t S28HS512T_ReadID(XSPI_HandleTypeDef *Ctx, S28HS512T_Interface_t Mode, S28HS512T_Transfer_t Rate,
                           uint8_t *ID)
{
  XSPI_RegularCmdTypeDef s_command = {0};

  /* SPI mode and DTR transfer not supported by memory */
  if ((Mode == S28HS512T_SPI_MODE) && (Rate == S28HS512T_DTR_TRANSFER))
  {
    return S28HS512T_ERROR;
  }

  /* Initialize the read ID command */
  s_command.OperationType = HAL_XSPI_OPTYPE_COMMON_CFG;
  s_command.InstructionMode = (Mode == S28HS512T_SPI_MODE)
                                  ? HAL_XSPI_INSTRUCTION_1_LINE
                                  : HAL_XSPI_INSTRUCTION_8_LINES;
  s_command.InstructionDTRMode = (Rate == S28HS512T_DTR_TRANSFER)
                                     ? HAL_XSPI_INSTRUCTION_DTR_ENABLE
                                     : HAL_XSPI_INSTRUCTION_DTR_DISABLE;
  s_command.InstructionWidth = (Mode == S28HS512T_SPI_MODE)
                                   ? HAL_XSPI_INSTRUCTION_8_BITS
                                   : HAL_XSPI_INSTRUCTION_16_BITS;
  s_command.Instruction = (Mode == S28HS512T_SPI_MODE)
                              ? S28HS512T_READ_ID_CMD
                              : S28HS512T_OCTA_READ_ID_CMD;
  s_command.AddressMode = (Mode == S28HS512T_SPI_MODE)
                              ? HAL_XSPI_ADDRESS_NONE
                              : HAL_XSPI_ADDRESS_8_LINES;
  s_command.AddressDTRMode = (Rate == S28HS512T_DTR_TRANSFER)
                                 ? HAL_XSPI_ADDRESS_DTR_ENABLE
                                 : HAL_XSPI_ADDRESS_DTR_DISABLE;
  s_command.AddressWidth = HAL_XSPI_ADDRESS_32_BITS;
  s_command.Address = 0U;
  s_command.AlternateBytesMode = HAL_XSPI_ALT_BYTES_NONE;
  s_command.DataMode = (Mode == S28HS512T_SPI_MODE) ? HAL_XSPI_DATA_1_LINE : HAL_XSPI_DATA_8_LINES;
  s_command.DataDTRMode = (Rate == S28HS512T_DTR_TRANSFER)
                              ? HAL_XSPI_DATA_DTR_ENABLE
                              : HAL_XSPI_DATA_DTR_DISABLE;
  s_command.DummyCycles = (Mode == S28HS512T_SPI_MODE)
                              ? 0U
                              : DUMMY_CYCLES_REG_OCTAL;
  s_command.DataLength = 3U;
  s_command.DQSMode = (Rate == S28HS512T_DTR_TRANSFER) ? HAL_XSPI_DQS_ENABLE : HAL_XSPI_DQS_DISABLE;
 #if defined (XSPI_CCR_SIOO)
  s_command.SIOOMode            = HAL_XSPI_SIOO_INST_EVERY_CMD;
 #endif

  /* Configure the command */
  if (HAL_XSPI_Command(Ctx, &s_command, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return S28HS512T_ERROR;
  }

  /* Reception of the data */
  if (HAL_XSPI_Receive(Ctx, ID, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return S28HS512T_ERROR;
  }

  return S28HS512T_OK;
}

/* Reset Commands *************************************************************/
/**
  * @brief  Flash reset enable command
  *         SPI/OPI
  * @param  Ctx Component object pointer
  * @param  Mode Interface select
  * @param  Rate Transfer rate STR or DTR
  * @retval error status
  */
int32_t S28HS512T_ResetEnable(XSPI_HandleTypeDef *Ctx, S28HS512T_Interface_t Mode, S28HS512T_Transfer_t Rate)
{
  XSPI_RegularCmdTypeDef s_command = {0};

  /* SPI mode and DTR transfer not supported by memory */
  if ((Mode == S28HS512T_SPI_MODE) && (Rate == S28HS512T_DTR_TRANSFER))
  {
    return S28HS512T_ERROR;
  }

  /* Initialize the reset enable command */
  s_command.OperationType = HAL_XSPI_OPTYPE_COMMON_CFG;
  s_command.InstructionMode = (Mode == S28HS512T_SPI_MODE)
                                  ? HAL_XSPI_INSTRUCTION_1_LINE
                                  : HAL_XSPI_INSTRUCTION_8_LINES;
  s_command.InstructionDTRMode = (Rate == S28HS512T_DTR_TRANSFER)
                                     ? HAL_XSPI_INSTRUCTION_DTR_ENABLE
                                     : HAL_XSPI_INSTRUCTION_DTR_DISABLE;
  s_command.InstructionWidth = (Mode == S28HS512T_SPI_MODE)
                                   ? HAL_XSPI_INSTRUCTION_8_BITS
                                   : HAL_XSPI_INSTRUCTION_16_BITS;
  s_command.Instruction = (Mode == S28HS512T_SPI_MODE)
                              ? S28HS512T_RESET_ENABLE_CMD
                              : S28HS512T_OCTA_RESET_ENABLE_CMD;
  s_command.AddressMode = HAL_XSPI_ADDRESS_NONE;
  s_command.AlternateBytesMode = HAL_XSPI_ALT_BYTES_NONE;
  s_command.DataMode = HAL_XSPI_DATA_NONE;
  s_command.DummyCycles = 0U;
  s_command.DQSMode = HAL_XSPI_DQS_DISABLE;

  /* Send the command */
  if (HAL_XSPI_Command(Ctx, &s_command, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return S28HS512T_ERROR;
  }

  return S28HS512T_OK;
}

/**
  * @brief  Flash reset memory command
  *         SPI/OPI
  * @param  Ctx Component object pointer
  * @param  Mode Interface select
  * @param  Rate Transfer rate STR or DTR
  * @retval error status
  */
int32_t S28HS512T_ResetMemory(XSPI_HandleTypeDef *Ctx, S28HS512T_Interface_t Mode, S28HS512T_Transfer_t Rate)
{
  XSPI_RegularCmdTypeDef s_command = {0};

  /* SPI mode and DTR transfer not supported by memory */
  if ((Mode == S28HS512T_SPI_MODE) && (Rate == S28HS512T_DTR_TRANSFER))
  {
    return S28HS512T_ERROR;
  }

  /* Initialize the reset enable command */
  s_command.OperationType = HAL_XSPI_OPTYPE_COMMON_CFG;
  s_command.InstructionMode = (Mode == S28HS512T_SPI_MODE)
                                  ? HAL_XSPI_INSTRUCTION_1_LINE
                                  : HAL_XSPI_INSTRUCTION_8_LINES;
  s_command.InstructionDTRMode = (Rate == S28HS512T_DTR_TRANSFER)
                                     ? HAL_XSPI_INSTRUCTION_DTR_ENABLE
                                     : HAL_XSPI_INSTRUCTION_DTR_DISABLE;
  s_command.InstructionWidth = (Mode == S28HS512T_SPI_MODE)
                                   ? HAL_XSPI_INSTRUCTION_8_BITS
                                   : HAL_XSPI_INSTRUCTION_16_BITS;
  s_command.Instruction = (Mode == S28HS512T_SPI_MODE)
                              ? S28HS512T_RESET_MEMORY_CMD
                              : S28HS512T_OCTA_RESET_MEMORY_CMD;
  s_command.AddressMode = HAL_XSPI_ADDRESS_NONE;
  s_command.AlternateBytesMode = HAL_XSPI_ALT_BYTES_NONE;
  s_command.DataMode = HAL_XSPI_DATA_NONE;
  s_command.DummyCycles = 0U;
  s_command.DQSMode = HAL_XSPI_DQS_DISABLE;
 #if defined (XSPI_CCR_SIOO)
  s_command.SIOOMode            = HAL_XSPI_SIOO_INST_EVERY_CMD;
 #endif

  /* Send the command */
  if (HAL_XSPI_Command(Ctx, &s_command, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return S28HS512T_ERROR;
  }

  return S28HS512T_OK;
}

/**
  * @brief  Flash no operation command
  *         SPI/OPI
  * @param  Ctx Component object pointer
  * @param  Mode Interface select
  * @param  Rate Transfer rate STR or DTR
  * @retval error status
  */
int32_t S28HS512T_NoOperation(XSPI_HandleTypeDef *Ctx, S28HS512T_Interface_t Mode, S28HS512T_Transfer_t Rate)
{
  XSPI_RegularCmdTypeDef s_command = {0};

  /* SPI mode and DTR transfer not supported by memory */
  if ((Mode == S28HS512T_SPI_MODE) && (Rate == S28HS512T_DTR_TRANSFER))
  {
    return S28HS512T_ERROR;
  }

  /* Initialize the no operation command */
  s_command.OperationType = HAL_XSPI_OPTYPE_COMMON_CFG;
  s_command.InstructionMode = (Mode == S28HS512T_SPI_MODE)
                                  ? HAL_XSPI_INSTRUCTION_1_LINE
                                  : HAL_XSPI_INSTRUCTION_8_LINES;
  s_command.InstructionDTRMode = (Rate == S28HS512T_DTR_TRANSFER)
                                     ? HAL_XSPI_INSTRUCTION_DTR_ENABLE
                                     : HAL_XSPI_INSTRUCTION_DTR_DISABLE;
  s_command.InstructionWidth = (Mode == S28HS512T_SPI_MODE)
                                   ? HAL_XSPI_INSTRUCTION_8_BITS
                                   : HAL_XSPI_INSTRUCTION_16_BITS;
  s_command.Instruction = (Mode == S28HS512T_SPI_MODE) ? S28HS512T_NOP_CMD : S28HS512T_OCTA_NOP_CMD;
  s_command.AddressMode = HAL_XSPI_ADDRESS_NONE;
  s_command.AlternateBytesMode = HAL_XSPI_ALT_BYTES_NONE;
  s_command.DataMode = HAL_XSPI_DATA_NONE;
  s_command.DummyCycles = 0U;
  s_command.DQSMode = HAL_XSPI_DQS_DISABLE;
 #if defined (XSPI_CCR_SIOO)
  s_command.SIOOMode            = HAL_XSPI_SIOO_INST_EVERY_CMD;
 #endif

  /* Send the command */
  if (HAL_XSPI_Command(Ctx, &s_command, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return S28HS512T_ERROR;
  }

  return S28HS512T_OK;
}

/**
  * @brief  Flash enter deep power-down command
  *         SPI/OPI
  * @param  Ctx Component object pointer
  * @param  Mode Interface select
  * @param  Rate Transfer rate STR or DTR
  * @retval error status
  */
int32_t S28HS512T_EnterPowerDown(XSPI_HandleTypeDef *Ctx, S28HS512T_Interface_t Mode,
                                   S28HS512T_Transfer_t Rate)
{
  XSPI_RegularCmdTypeDef s_command = {0};

  /* SPI mode and DTR transfer not supported by memory */
  if ((Mode == S28HS512T_SPI_MODE) && (Rate == S28HS512T_DTR_TRANSFER))
  {
    return S28HS512T_ERROR;
  }

  /* Initialize the enter power down command */
  s_command.OperationType = HAL_XSPI_OPTYPE_COMMON_CFG;

  s_command.InstructionMode = (Mode == S28HS512T_SPI_MODE)
                                  ? HAL_XSPI_INSTRUCTION_1_LINE
                                  : HAL_XSPI_INSTRUCTION_8_LINES;
  s_command.InstructionDTRMode = (Rate == S28HS512T_DTR_TRANSFER)
                                     ? HAL_XSPI_INSTRUCTION_DTR_ENABLE
                                     : HAL_XSPI_INSTRUCTION_DTR_DISABLE;
  s_command.InstructionWidth = (Mode == S28HS512T_SPI_MODE)
                                   ? HAL_XSPI_INSTRUCTION_8_BITS
                                   : HAL_XSPI_INSTRUCTION_16_BITS;
  s_command.Instruction = (Mode == S28HS512T_SPI_MODE)
                              ? S28HS512T_ENTER_DEEP_POWER_DOWN_CMD
                              : S28HS512T_OCTA_ENTER_DEEP_POWER_DOWN_CMD;
  s_command.AddressMode = HAL_XSPI_ADDRESS_NONE;
  s_command.AlternateBytesMode = HAL_XSPI_ALT_BYTES_NONE;
  s_command.DataMode = HAL_XSPI_DATA_NONE;
  s_command.DummyCycles = 0U;
  s_command.DQSMode = HAL_XSPI_DQS_DISABLE;
 #if defined (XSPI_CCR_SIOO)
  s_command.SIOOMode            = HAL_XSPI_SIOO_INST_EVERY_CMD;
 #endif

  /* Send the command */
  if (HAL_XSPI_Command(Ctx, &s_command, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return S28HS512T_ERROR;
  }

  return S28HS512T_OK;
}

/**
  * @}
  */

/**
  * @}
  */

/**
  * @}
  */

/**
  * @}
  */
