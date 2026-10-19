#ifndef __ADIN_H
#define __ADIN_H


#include "eth.h"
#include "main.h"
#include "ADIN1300_reg.h"
extern ETH_HandleTypeDef heth1;

#define ADIN_ETH_HANDLE (&heth1)

#define ADIN1300_PHY_ID 0x0283bc30      //Fixed ID for ADIN1300
#define ADIN1300_PHY_ADDR (1)           //Set by Straps
#define ADIN1300_RST_TIMEOUT 500        //Reset Timeout in ms


typedef struct
{
    uint32_t PHY_ADDR;


}adin1300_Object_t;

typedef enum
{
    ADIN1300_STATUS_ERROR = -1,
    ADIN1300_STATUS_OK = 0,
}adin1300_Status_t;

/** @brief Provide the auto-negotiation hook, currently returning success without register writes. */
HAL_StatusTypeDef ADIN1300_StartAutoNeg(adin1300_Object_t *pPhy);
/** @brief Find the PHY on MDIO, reset it, and apply the board's Ethernet configuration. */
HAL_StatusTypeDef ADIN1300_Init(adin1300_Object_t *pPhy);
/** @brief Issue a PHY software reset and wait for reset completion. */
HAL_StatusTypeDef ADIN1300_Reset(adin1300_Object_t *pPhy);
/** @brief Reset the selected ADIN1300 subsystem. */
HAL_StatusTypeDef ADIN1300_SubSysReset(adin1300_Object_t *pPhy);
/** @brief Write a standard ADIN1300 register through the Ethernet MDIO interface. */
HAL_StatusTypeDef ADIN1300_WriteReg(adin1300_Object_t *pPhy, adin1300_RegTypeDef_t reg, uint16_t val);
/** @brief Read a standard ADIN1300 register through the Ethernet MDIO interface. */
HAL_StatusTypeDef ADIN1300_ReadReg(adin1300_Object_t *pPhy, adin1300_RegTypeDef_t reg, uint16_t *retval);
/** @brief Write an extended ADIN1300 register through its indirect register window. */
HAL_StatusTypeDef ADIN1300_WriteExtReg(adin1300_Object_t *pPhy, adin1300_RegTypeDef_t reg, uint16_t val);
/** @brief Read an extended ADIN1300 register through its indirect register window. */
HAL_StatusTypeDef ADIN1300_ReadExtReg(adin1300_Object_t *pPhy, adin1300_RegTypeDef_t reg, uint32_t *val);
/** @brief Translate the PHY's negotiated link speed and duplex into NetX link status. */
int32_t ADIN1300_GetLinkStatus(adin1300_Object_t *pPhy);


/** @brief Read the PHY identifier with its revision bits masked out. */
HAL_StatusTypeDef ADIN1300_GetPhyID(adin1300_Object_t *pPhy, uint32_t *id);

extern adin1300_Object_t phy;

#endif