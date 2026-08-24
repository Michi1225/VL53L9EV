#include "ADIN1300.h"
#include "ADIN1300_reg.h"
#include "nx_stm32_phy_driver.h"
#include "stm32n6xx_hal.h"
#include "stm32n6xx_hal_def.h"
#include "stm32n6xx_hal_eth.h"
#include <stdint.h>


static HAL_StatusTypeDef ADIN1300_ReadReg(adin1300_Object_t *pPhy, adin1300_RegTypeDef_t reg, uint16_t *retval)
{
    uint32_t temp = 0;

    HAL_StatusTypeDef error = HAL_ETH_ReadPHYRegister(ADIN_ETH_HANDLE, pPhy->PHY_ADDR, (uint32_t)reg, &temp);

    *retval = (uint16_t) temp;

    return error;
}

static HAL_StatusTypeDef ADIN1300_WriteReg(adin1300_Object_t *pPhy, adin1300_RegTypeDef_t reg, uint16_t val)
{
    uint32_t tx_val = val;
    HAL_StatusTypeDef error = HAL_ETH_WritePHYRegister(ADIN_ETH_HANDLE, pPhy->PHY_ADDR, (uint32_t)reg, tx_val);

    return error;
}



uint32_t ADIN1300_GetPhyID(adin1300_Object_t *pPhy)
{

    uint16_t phy_id_msb = 0;
    uint16_t phy_id_lsb = 0;
    (void)ADIN1300_ReadReg(pPhy, PHY_ID_1, &phy_id_msb);
    (void)ADIN1300_ReadReg(pPhy, PHY_ID_2, &phy_id_lsb);

    uint32_t id = ((uint32_t)phy_id_lsb) | ((uint32_t)phy_id_msb << 16);

    return id;
}


HAL_StatusTypeDef ADIN1300_Init(adin1300_Object_t *pPhy)
{
    pPhy->PHY_ADDR = ADIN1300_PHY_ADDR;


    if(ADIN1300_GetPhyID(pPhy) != ADIN1300_PHY_ID)
    {
        // Wrong Phy ID or none at all --> sweep IDs
        uint8_t found = 0;
        for(uint8_t addr = 0; addr <= 31; addr++)
        {
            pPhy->PHY_ADDR = addr;
            if(ADIN1300_GetPhyID(pPhy) == ADIN1300_PHY_ID)
            {
                found++;
                break;
            }
        }
        if(found == 0) return HAL_ERROR;
    }

    // PHY found add ADDR = pPhy->PHY_ADDR
    //SW reset PHY
    HAL_StatusTypeDef error = HAL_OK;
    error |= ADIN1300_Reset(pPhy);

    // Set MII_CONTROL register
    uint16_t reg_val = 0b0001'0001'0100'0000; //1Gbps, full duplex, autonegotiation
    error |= ADIN1300_WriteReg(pPhy, MII_CONTROL, reg_val);

    // 
}



int32_t ADIN1300_GetLinkStatus(adin1300_Object_t *pPhy)
{
    uint16_t phy_stat = 0;
    if(ADIN1300_ReadReg(pPhy, PHY_STATUS_1, &phy_stat) != HAL_OK) return ETH_PHY_STATUS_LINK_ERROR;

    uint16_t autoneg_done = phy_stat & 0b0001'0000'0000'0000;
    if(autoneg_done == 0) return ETH_PHY_STATUS_AUTONEGO_NOT_DONE;

    uint16_t link = phy_stat & 0b000'0000'0100'0000;
    if(link == 0) return ETH_PHY_STATUS_LINK_DOWN;

    uint16_t speed = phy_stat & 0b0000'0011'1000'0000;
    switch (speed >> 7) {
        case 0:
            return ETH_PHY_STATUS_10MBITS_HALFDUPLEX;
        case 1:
            return ETH_PHY_STATUS_10MBITS_FULLDUPLEX;
        case 2:
            return ETH_PHY_STATUS_100MBITS_HALFDUPLEX;
        case 3:
            return ETH_PHY_STATUS_100MBITS_FULLDUPLEX;
        case 4:
            return ETH_PHY_STATUS_1000MBITS_HALFDUPLEX;
        case 5:
            return ETH_PHY_STATUS_1000MBITS_FULLDUPLEX;
        default:
            return ETH_PHY_STATUS_LINK_ERROR;
    }
}


HAL_StatusTypeDef ADIN1300_Reset(adin1300_Object_t *pPhy)
{
    HAL_StatusTypeDef error = HAL_OK;
    // Set SFT_RST bit in MII_CONTROL
    error |= ADIN1300_WriteReg(pPhy, MII_CONTROL, 0x8000);

    uint16_t reg_val = 0;
    uint16_t timeout = 0;
    do
    {
        HAL_Delay(0);
        timeout++;
        error |= ADIN1300_ReadReg(pPhy, MII_CONTROL, &reg_val);
    }while((reg_val & 0x8000) && timeout < ADIN1300_RST_TIMEOUT);

    if(timeout >= ADIN1300_RST_TIMEOUT) return HAL_TIMEOUT;
    return error;
}