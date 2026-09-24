/**
  ******************************************************************************
  * @file    s28hs512t.h
  * @modify  MCD Application Team
  * @brief   This file contains all the description of the
  *          S28HS512T XSPI memory.
  ******************************************************************************
  */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef S28HS512T_H
#define S28HS512T_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "s28hs512t_conf.h"

/** @addtogroup BSP
  * @{
  */

/** @addtogroup Components
  * @{
  */

/** @addtogroup S28HS512T
  * @{
  */
/** @defgroup S28HS512T_Exported_Constants S28HS512T Exported Constants
  * @{
  */

/**
  * @brief  S28HS512T Size configuration
  */
#define S28HS512T_SECTOR_4K                    (uint32_t)(4 * 1024)              /* 32 sectors of 4KBytes + 1 sector of 128KBytes */
#define S28HS512T_SECTOR_128K                  (uint32_t)(128 * 1024)            /* 1 sectors of 128KBytes */
#define S28HS512T_SECTOR_256K                  (uint32_t)(256 * 1024)            /* 255 sectors of 256KBytes */

#define S28HS512T_FLASH_SIZE                   (uint32_t)(512 * 1024 * 1024 / 8) /* 512 Mbits => 64MBytes */
#define S28HS512T_PAGE_SIZE                    (uint32_t)256                     /* 262144 pages of 256 Bytes */

/**
  * @brief  S28HS512T Timing configuration
  */

#define S28HS512T_BULK_ERASE_MAX_TIME          696000U
#define S28HS512T_BLOCK_ERASE_MAX_TIME         2677U
#define S28HS512T_BLOCK_4K_ERASE_MAX_TIME      335U
#define S28HS512T_WRITE_REG_MAX_TIME           358U

#define S28HS512T_RESET_MAX_TIME               1U                 /* when SWreset during erase operation */

#define S28HS512T_AUTOPOLLING_INTERVAL_TIME    0x10U

/**
  * @brief  S28HS512T Error codes
  */
#define S28HS512T_OK                           (0)
#define S28HS512T_ERROR                        (-1)

/**
  * @brief  re-definition of legacy memory mapped functions
  */
#define S28HS512T_EnableMemoryMappedModeDTR S28HS512T_EnableDTRMemoryMappedMode
#define S28HS512T_EnableMemoryMappedModeSTR S28HS512T_EnableSTRMemoryMappedMode


/******************************************************************************
  * @brief  S28HS512T Commands
  ****************************************************************************/

/*******************************************************************/
/********************************* SPI  ****************************/
/*******************************************************************/

/***** READ/WRITE MEMORY Operations with 3-Byte Address ****************************/
#define S28HS512T_READ_CMD                             0x03U   /*!< Normal Read 3 Byte Address                            */
#define S28HS512T_FAST_READ_CMD                        0x0BU   /*!< Fast Read 3 Byte Address                              */
#define S28HS512T_BULK_ERASE_CMD                       0x60U   /*!< Bulk Erase                                            */

/***** READ/WRITE MEMORY Operations with 4-Byte Address ****************************/
#define S28HS512T_4_BYTE_ADDR_READ_CMD                 0x13U   /*!< Normal Read 4 Byte address                            */
#define S28HS512T_4_BYTE_ADDR_FAST_READ_CMD            0x0BU   /*!< Fast Read 4 Byte address                              */
#define S28HS512T_4_BYTE_PAGE_PROG_CMD                 0x12U   /*!< Page Program 4 Byte Address                           */
#define S28HS512T_4_BYTE_SECTOR_ERASE_4K_CMD           0x21U   /*!< Sector Erase 4KB 4 Byte Address                       */
#define S28HS512T_4_BYTE_BLOCK_ERASE_256K_CMD          0xDCU   /*!< Sector Erase 256KB 4 Byte Address                     */

/***** Setting commands ************************************************************/
#define S28HS512T_WRITE_ENABLE_CMD                     0x06U   /*!< Write Enable                                          */
#define S28HS512T_WRITE_DISABLE_CMD                    0x04U   /*!< Write Disable                                         */
#define S28HS512T_PROG_ERASE_SUSPEND_CMD               0xB0U   /*!< Program/Erase suspend                                 */
#define S28HS512T_PROG_ERASE_RESUME_CMD                0x7AU   /*!< Program/Erase resume                                  */
#define S28HS512T_ENTER_DEEP_POWER_DOWN_CMD            0xB9U   /*!< Enter deep power down                                 */

/***** RESET commands ************************************************************/
#define S28HS512T_NOP_CMD                              0x00U   /*!< No operation                                          */
#define S28HS512T_RESET_ENABLE_CMD                     0x66U   /*!< Reset Enable                                          */
#define S28HS512T_RESET_MEMORY_CMD                     0x99U   /*!< Reset Memory                                          */

/***** Register Commands (SPI) ****************************************************/
#define S28HS512T_READ_ID_CMD                          0x9FU   /*!< Read IDentification                                   */
#define S28HS512T_READ_SERIAL_FLASH_DISCO_PARAM_CMD    0x5AU   /*!< Read Serial Flash Discoverable Parameter              */
#define S28HS512T_READ_STATUS_REG1_CMD                 0x05U   /*!< Read Status Register1                                 */
#define S28HS512T_READ_STATUS_REG2_CMD                 0x07U   /*!< Read Status Register2                                 */
#define S28HS512T_READ_ANY_REG_CMD                     0x65U   /*!< Read Any Register                                     */
#define S28HS512T_WRITE_ANY_REG_CMD                    0x71U   /*!< Write Any Register                                    */

#define S28HS512T_READ_DPB_REG_CMD                     0xE0U   /*!< Read DPB register                                     */
#define S28HS512T_WRITE_DPB_REG_CMD                    0xE1U   /*!< Write DPB register                                    */
#define S28HS512T_READ_PPB_STATUS_CMD                  0xE2U   /*!< Read PPB status                                       */
#define S28HS512T_WRITE_PPB_BIT_CMD                    0xE3U   /*!< PPB bit program                                       */
#define S28HS512T_ERASE_ALL_PPB_CMD                    0xE4U   /*!< Erase all PPB bit                                     */


/*******************************************************************/
/********************************* OPI  ****************************/
/*******************************************************************/

/***** READ/WRITE MEMORY Operations  ****************************/
#define S28HS512T_OCTA_READ_CMD                             0xECECU  /*!< Octa IO Read                                     */
#define S28HS512T_OCTA_READ_DTR_CMD                         0xEEEEU  /*!< Octa IO Read DTR                                 */
#define S28HS512T_OCTA_PAGE_PROG_CMD                        0x1212U  /*!< Octa Page Program                                */
#define S28HS512T_OCTA_SECTOR_ERASE_4K_CMD                  0x2121U  /*!< Octa SubSector Erase 4KB                         */
#define S28HS512T_OCTA_BLOCK_ERASE_256K_CMD                 0xDCDCU  /*!< Octa Sector Erase 256KB                          */
#define S28HS512T_OCTA_BULK_ERASE_CMD                       0x6060U  /*!< Octa Bulk Erase                                  */

/***** Setting commands ************************************************************/
#define S28HS512T_OCTA_WRITE_ENABLE_CMD                     0x0606U   /*!< Octa Write Enable                               */
#define S28HS512T_OCTA_WRITE_DISABLE_CMD                    0x0404U   /*!< Octa Write Disable                              */
#define S28HS512T_OCTA_PROG_ERASE_SUSPEND_CMD               0xB0B0U   /*!< Octa Program/Erase suspend                      */
#define S28HS512T_OCTA_PROG_ERASE_RESUME_CMD                0x3030U   /*!< Octa Program/Erase resume                       */
#define S28HS512T_OCTA_ENTER_DEEP_POWER_DOWN_CMD            0xB9B9U   /*!< Octa Enter deep power down                      */

/***** RESET commands ************************************************************/
#define S28HS512T_OCTA_NOP_CMD                              0x0000U   /*!< Octa No operation                               */
#define S28HS512T_OCTA_RESET_ENABLE_CMD                     0x6666U   /*!< Octa Reset Enable                               */
#define S28HS512T_OCTA_RESET_MEMORY_CMD                     0x9999U   /*!< Octa Reset Memory                               */

/***** Register Commands (OPI) ****************************************************/
#define S28HS512T_OCTA_READ_ID_CMD                          0x9F9FU   /*!< Octa Read IDentification                        */
#define S28HS512T_OCTA_READ_SERIAL_FLASH_DISCO_PARAM_CMD    0x5A5AU   /*!< Octa Read Serial Flash Discoverable Parameter   */
#define S28HS512T_OCTA_READ_STATUS_REG1_CMD                 0x0505U   /*!< Octa Read Status Register1                      */
#define S28HS512T_OCTA_READ_STATUS_REG2_CMD                 0x0707U   /*!< Octa Read Status Register2                      */
#define S28HS512T_OCTA_READ_ANY_REG_CMD                     0x6565U   /*!< Octa Read Any Register                          */
#define S28HS512T_OCTA_WRITE_ANY_REG_CMD                    0x7171U   /*!< Octa Write Any Register                         */
#define S28HS512T_OCTA_READ_DPB_REG_CMD                     0xE0E0U   /*!< Octa Read DPB register                          */
#define S28HS512T_OCTA_WRITE_DPB_REG_CMD                    0xE1E1U   /*!< Octa Write DPB register                         */
#define S28HS512T_OCTA_READ_PPB_STATUS_CMD                  0xE2E2U   /*!< Octa Read PPB status                            */
#define S28HS512T_OCTA_WRITE_PPB_BIT_CMD                    0xE3E3U   /*!< Octa PPB bit program                            */
#define S28HS512T_OCTA_ERASE_ALL_PPB_CMD                    0xE4E4U   /*!< Octa Erase all PPB bit                          */

/******************************************************************************
  * @brief  S28HS512T Registers
  ****************************************************************************/
/* Status Register 1 */
#define S28HS512T_SR1_ADDR                             0x00800000U  /*!< SR1 register address 0x00800000                                                          */
#define S28HS512T_SR1_RDYBSY                           0x01U        /*!< Device Ready/Busy Status Flag                                                            */
#define S28HS512T_SR1_WRPGEN                           0x02U        /*!< Write/Program Enable Status Flag                                                         */
#define S28HS512T_SR1_LBPROT                           0x1CU        /*!< Legacy Block Protection based Memory Array size selection                                */
#define S28HS512T_SR1_ERSERR                           0x20U        /*!< Erasing Error Status Flag                                                                */
#define S28HS512T_SR1_PRGERR                           0x40U        /*!< Programming Error Status Flag                                                            */

/* Status Register 2 */
#define S28HS512T_SR2_ADDR                             0x00800001U  /*!< SR1 register address 0x00800001                                                          */
#define S28HS512T_SR2_PROGMS                           0x01U        /*!< Program operation Suspend Status Flag                                                    */
#define S28HS512T_SR2_ERASES                           0x02U        /*!< Erase operation Suspend Status Flag                                                      */
#define S28HS512T_SR2_SESTAT                           0x04U        /*!< Sector Erase Success/Failure Status Flag                                                 */
#define S28HS512T_SR2_DICRCA                           0x08U        /*!< Memory Array Data Integrity Cyclic Redundancy Check Abort Status Flag                    */
#define S28HS512T_SR2_DICRCS                           0x10U        /*!< Memory Array Data Integrity Cyclic Redundancy Check Suspend Status Flag                  */

/* Configuration Register 1 */
#define S28HS512T_CR1_ADDR                             0x00800002U  /*!< CR1 register address 0x00800002                                                          */
#define S28HS512T_CR1_TLPROT                           0x01U        /*!< Temporary Locking selection of Legacy Block Protection and Sector Architecture           */
#define S28HS512T_CR1_TB4KBS                           0x04U        /*!< Top or Bottom Address Range selection for 4KB Sector Block                               */
#define S28HS512T_CR1_PLPROT                           0x10U        /*!< Permanent Locking selection of Legacy Block Protection and 4KB Sector Architecture       */
#define S28HS512T_CR1_TBPROT                           0x20U        /*!< Top or Bottom Protection selection between top and bottom address space                  */
#define S28HS512T_CR1_SP4KBS                           0x40U        /*!< Split 4KB Sectors selection between top and bottom address space                         */

/* Configuration Register 2 */
#define S28HS512T_CR2_ADDR                             0x00800003U  /*!< CR2 register address 0x00800003                                                          */
#define S28HS512T_CR2_MEMLAT                           0x0FU        /*!< Memory Array Read Latency selection - Dummy cycles required for initial data access      */
#define S28HS512T_CR2_MEMLAT_0_5_CYCLES                0x00U        /*!< 0 (1-1-1, 1-1-8) / 5 (8-8-8) Dummy cycles                                                */
#define S28HS512T_CR2_MEMLAT_1_6_CYCLES                0x01U        /*!< 1 (1-1-1, 1-1-8) / 6 (8-8-8) Dummy cycles                                                */
#define S28HS512T_CR2_MEMLAT_2_8_CYCLES                0x02U        /*!< 2 (1-1-1, 1-1-8) / 8 (8-8-8) Dummy cycles                                                */
#define S28HS512T_CR2_MEMLAT_3_10_CYCLES               0x03U        /*!< 3 (1-1-1, 1-1-8) / 10 (8-8-8) Dummy cycles                                               */
#define S28HS512T_CR2_MEMLAT_4_12_CYCLES               0x04U        /*!< 4 (1-1-1, 1-1-8) / 12 (8-8-8) Dummy cycles                                               */
#define S28HS512T_CR2_MEMLAT_5_14_CYCLES               0x05U        /*!< 5 (1-1-1, 1-1-8) / 14 (8-8-8) Dummy cycles                                               */
#define S28HS512T_CR2_MEMLAT_6_16_CYCLES               0x06U        /*!< 6 (1-1-1, 1-1-8) / 16 (8-8-8) Dummy cycles                                               */
#define S28HS512T_CR2_MEMLAT_7_18_CYCLES               0x07U        /*!< 7 (1-1-1, 1-1-8) / 18 (8-8-8) Dummy cycles                                               */
#define S28HS512T_CR2_MEMLAT_8_20_CYCLES               0x08U        /*!< 8 (1-1-1, 1-1-8) / 20 (8-8-8) Dummy cycles                                               */
#define S28HS512T_CR2_MEMLAT_9_22_CYCLES               0x09U        /*!< 9 (1-1-1, 1-1-8) / 22 (8-8-8) Dummy cycles                                               */
#define S28HS512T_CR2_MEMLAT_10_23_CYCLES              0x0AU        /*!< 10 (1-1-1, 1-1-8) / 23 (8-8-8) Dummy cycles                                              */
#define S28HS512T_CR2_MEMLAT_11_24_CYCLES              0x0BU        /*!< 11 (1-1-1, 1-1-8) / 24 (8-8-8) Dummy cycles                                              */
#define S28HS512T_CR2_MEMLAT_12_25_CYCLES              0x0CU        /*!< 12 (1-1-1, 1-1-8) / 25 (8-8-8) Dummy cycles                                              */
#define S28HS512T_CR2_MEMLAT_13_26_CYCLES              0x0DU        /*!< 13 (1-1-1, 1-1-8) / 26 (8-8-8) Dummy cycles                                              */
#define S28HS512T_CR2_MEMLAT_14_27_CYCLES              0x0EU        /*!< 14 (1-1-1, 1-1-8) / 27 (8-8-8) Dummy cycles                                              */
#define S28HS512T_CR2_MEMLAT_15_28_CYCLES              0x0FU        /*!< 15 (1-1-1, 1-1-8) / 28 (8-8-8) Dummy cycles                                              */
#define S28HS512T_CR2_ADRBYT                           0x80U        /*!< Address Byte Length selection between 3 or 4 bytes for Instructions                      */

/* Configuration Register 3 */
#define S28HS512T_CR3_ADDR                             0x00800004U  /*!< CR2 register address 0x00800004                                                          */
#define S28HS512T_CR3_UNHYSA                           0x08U        /*!< Uniform or Hybrid Sector Architecture Selection                                          */
#define S28HS512T_CR3_PGMBUF                           0x10U        /*!< Program Buffer Size selection                                                            */
#define S28HS512T_CR3_BLKCHK                           0x20U        /*!< Blank Check selection during Erase operation for better endurance                        */
#define S28HS512T_CR3_VRGLAT                           0xC0U        /*!< Volatile Register Read Latency selection - Dummy cycles required for initial data access */
#define S28HS512T_CR3_VRGLAT_00                        0x00U        /*!< 0 (1S-1S-1S) / 3 (8S-8S-8S) / 3 (8D-8D-8D) Dummy cycles                                  */
#define S28HS512T_CR3_VRGLAT_01                        0x40U        /*!< 1/0 (1S-1S-1S) / 4 (8S-8S-8S) / 4 (8D-8D-8D) Dummy cycles                                */
#define S28HS512T_CR3_VRGLAT_10                        0x80U        /*!< 1 (1S-1S-1S) / 5 (8S-8S-8S) / 5 (8D-8D-8D) Dummy cycles                                  */
#define S28HS512T_CR3_VRGLAT_11                        0xC0U        /*!< 2 (1S-1S-1S) / 6 (8S-8S-8S) / 6 (8D-8D-8D) Dummy cycles                                  */

/* Configuration Register 4 */
#define S28HS512T_CR4_ADDR                             0x00800005U  /*!< CR2 register address 0x00800005                                                          */
#define S28HS512T_CR4_RBSTWL                           0x03U        /*!< Read Burst Wrap Length selection                                                         */
#define S28HS512T_CR4_RBSTWL_8B                        0x00U        /*!< 8 Bytes Wrap length                                                                      */
#define S28HS512T_CR4_RBSTWL_16B                       0x01U        /*!< 16 Bytes Wrap length                                                                     */
#define S28HS512T_CR4_RBSTWL_32B                       0x02U        /*!< 32 Bytes Wrap length                                                                     */
#define S28HS512T_CR4_RBSTWL_64B                       0x03U        /*!< 64 Bytes Wrap length                                                                     */
#define S28HS512T_CR4_DPDPOR                           0x04U        /*!< Deep Power Down power saving mode entry selection upon POR                               */
#define S28HS512T_CR4_ECC12S                           0x08U        /*!< Error Correction Code (ECC) 1-bit or 1-bit/2-bit error correction selection              */
#define S28HS512T_CR4_RBSTWP                           0x10U        /*!< Read Burst Wrap Enable selection                                                         */
#define S28HS512T_CR4_IOIMPD                           0xD0U        /*!< I/O Driver Output Impedance selection                                                    */
#define S28HS512T_CR4_IOIMPD_120                       0x20U        /*!< 120 Ohm                                                                                  */
#define S28HS512T_CR4_IOIMPD_90                        0x40U        /*!< 90 Ohm                                                                                   */
#define S28HS512T_CR4_IOIMPD_60                        0x60U        /*!< 60 Ohm                                                                                   */
#define S28HS512T_CR4_IOIMPD_45                        0x80U        /*!< 45 Ohm                                                                                   */
#define S28HS512T_CR4_IOIMPD_30                        0xA0U        /*!< 30 Ohm                                                                                   */
#define S28HS512T_CR4_IOIMPD_20                        0xC0U        /*!< 20 Ohm                                                                                   */
#define S28HS512T_CR4_IOIMPD_15                        0xE0U        /*!< 15 Ohm                                                                                   */

/* Configuration Register 5 */
#define S28HS512T_CR5_ADDR                             0x00800006U  /*!< CR2 register address 0x00800006                                                          */
#define S28HS512T_CR5_OPI_IT                           0x01U        /*!< Octal Interface and Protocol Selection - I/O width set to 8bits (8-8-8)                  */
#define S28HS512T_CR5_SDRDDR                           0x02U        /*!< Octal SPI SDR or DDR selection                                                           */

#define XSPI_ALTERNATE_BYTE_PATTERN                    0x00U

/**
  * @}
  */

/** @defgroup S28HS512T_Exported_Types S28HS512T Exported Types
  * @{
  */
typedef struct
{
  uint32_t FlashSize;                        /*!< Size of the flash                             */
  uint32_t EraseSectorSize;                  /*!< Size of sectors for the erase operation       */
  uint32_t EraseSectorsNumber;               /*!< Number of sectors for the erase operation     */
  uint32_t EraseSubSectorSize;               /*!< Size of subsector for the erase operation     */
  uint32_t EraseSubSectorNumber;             /*!< Number of subsector for the erase operation   */
  uint32_t EraseSubSector1Size;              /*!< Size of subsector 1 for the erase operation   */
  uint32_t EraseSubSector1Number;            /*!< Number of subsector 1 for the erase operation */
  uint32_t ProgPageSize;                     /*!< Size of pages for the program operation       */
  uint32_t ProgPagesNumber;                  /*!< Number of pages for the program operation     */
} S28HS512T_Info_t;

typedef enum
{
  S28HS512T_SPI_MODE = 0,                 /*!< 1-1-1 commands, Power on H/W default setting  */
  S28HS512T_OPI_MODE                      /*!< 8-8-8 commands                                */
} S28HS512T_Interface_t;

typedef enum
{
  S28HS512T_STR_TRANSFER = 0,             /*!< Single Transfer Rate                          */
  S28HS512T_DTR_TRANSFER                  /*!< Double Transfer Rate                          */
} S28HS512T_Transfer_t;

typedef enum
{
  S28HS512T_ERASE_4K = 0,                 /*!< 4K size Sector erase                          */
  S28HS512T_ERASE_256K,                   /*!< 64K size Block erase                          */
  S28HS512T_ERASE_BULK                    /*!< Whole bulk erase                              */
} S28HS512T_Erase_t;

typedef enum
{
  S28HS512T_3BYTES_SIZE = 0,              /*!< 3 Bytes address mode                           */
  S28HS512T_4BYTES_SIZE                   /*!< 4 Bytes address mode                           */
} S28HS512T_AddressWidth_t;

/**
  * @}
  */

/** @defgroup S28HS512T_Exported_Functions S28HS512T Exported Functions
  * @{
  */
/* Function by commands combined */
int32_t S28HS512T_GetFlashInfo(S28HS512T_Info_t *pInfo);
int32_t S28HS512T_AutoPollingMemReady(XSPI_HandleTypeDef *Ctx, S28HS512T_Interface_t Mode,
                                         S28HS512T_Transfer_t Rate);

/* Read/Write Array Commands **************************************************/
int32_t S28HS512T_ReadSTR(XSPI_HandleTypeDef *Ctx, S28HS512T_Interface_t Mode,
                             S28HS512T_AddressWidth_t AddressWidth, uint8_t *pData, uint32_t ReadAddr, uint32_t Size);
int32_t S28HS512T_ReadDTR(XSPI_HandleTypeDef *Ctx, uint8_t *pData, uint32_t ReadAddr, uint32_t Size);
int32_t S28HS512T_PageProgram(XSPI_HandleTypeDef *Ctx, S28HS512T_Interface_t Mode,
                                 S28HS512T_Transfer_t Rate, uint8_t *pData, uint32_t WriteAddr,
                                 uint32_t Size);
int32_t S28HS512T_BlockErase(XSPI_HandleTypeDef *Ctx, S28HS512T_Interface_t Mode, S28HS512T_Transfer_t Rate,
                                uint32_t BlockAddress, S28HS512T_Erase_t BlockSize);
int32_t S28HS512T_ChipErase(XSPI_HandleTypeDef *Ctx, S28HS512T_Interface_t Mode, S28HS512T_Transfer_t Rate);
int32_t S28HS512T_EnableMemoryMappedModeSTR(XSPI_HandleTypeDef *Ctx, S28HS512T_Interface_t Mode,
                                               S28HS512T_AddressWidth_t AddressWidth);
int32_t S28HS512T_EnableMemoryMappedModeDTR(XSPI_HandleTypeDef *Ctx, S28HS512T_Interface_t Mode);
int32_t S28HS512T_Suspend(XSPI_HandleTypeDef *Ctx, S28HS512T_Interface_t Mode, S28HS512T_Transfer_t Rate);
int32_t S28HS512T_Resume(XSPI_HandleTypeDef *Ctx, S28HS512T_Interface_t Mode, S28HS512T_Transfer_t Rate);

/* Register/Setting Commands **************************************************/
int32_t S28HS512T_WriteEnable(XSPI_HandleTypeDef *Ctx, S28HS512T_Interface_t Mode, S28HS512T_Transfer_t Rate);
int32_t S28HS512T_WriteDisable(XSPI_HandleTypeDef *Ctx, S28HS512T_Interface_t Mode, S28HS512T_Transfer_t Rate);
int32_t S28HS512T_ReadStatusRegister1(XSPI_HandleTypeDef *Ctx, S28HS512T_Interface_t Mode,
                                         S28HS512T_Transfer_t Rate, uint8_t *Value);
int32_t S28HS512T_ReadStatusRegister2(XSPI_HandleTypeDef *Ctx, S28HS512T_Interface_t Mode,
                                         S28HS512T_Transfer_t Rate, uint8_t *Value);
int32_t S28HS512T_ReadAnyRegister(XSPI_HandleTypeDef *Ctx, S28HS512T_Interface_t Mode,
                                     S28HS512T_Transfer_t Rate, S28HS512T_AddressWidth_t AddressWidth,
                                     uint32_t ReadAddr, uint8_t *Value);
int32_t S28HS512T_WriteAnyRegister(XSPI_HandleTypeDef *Ctx, S28HS512T_Interface_t Mode,
                                      S28HS512T_Transfer_t Rate,  S28HS512T_AddressWidth_t AddressWidth,
                                      uint32_t WriteAddr, uint8_t Value);

/* ID/Security Commands *******************************************************/
int32_t S28HS512T_ReadID(XSPI_HandleTypeDef *Ctx, S28HS512T_Interface_t Mode, S28HS512T_Transfer_t Rate,
                            uint8_t *ID);

/* Reset Commands *************************************************************/
int32_t S28HS512T_ResetEnable(XSPI_HandleTypeDef *Ctx, S28HS512T_Interface_t Mode, S28HS512T_Transfer_t Rate);
int32_t S28HS512T_ResetMemory(XSPI_HandleTypeDef *Ctx, S28HS512T_Interface_t Mode, S28HS512T_Transfer_t Rate);
int32_t S28HS512T_NoOperation(XSPI_HandleTypeDef *Ctx, S28HS512T_Interface_t Mode, S28HS512T_Transfer_t Rate);
int32_t S28HS512T_EnterPowerDown(XSPI_HandleTypeDef *Ctx, S28HS512T_Interface_t Mode,
                                    S28HS512T_Transfer_t Rate);

/**
  * @}
  */

#ifdef __cplusplus
}
#endif

#endif /* S28HS512T_H */

/**
  * @}
  */

/**
  * @}
  */

/**
  * @}
  */
