#include "ADIN1300.h"
#include "ADIN1300_reg.h"
#include "nx_stm32_phy_driver.h"
#include "stm32n6xx_hal.h"
#include "stm32n6xx_hal_def.h"
#include "stm32n6xx_hal_eth.h"
#include <stdint.h>


adin1300_Object_t phy = 
{
    .PHY_ADDR = ADIN1300_PHY_ADDR
};


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

static HAL_StatusTypeDef ADIN1300_WriteExtReg(adin1300_Object_t *pPhy, adin1300_RegTypeDef_t reg, uint16_t val)
{
    uint32_t tx_val = (uint16_t)reg;
    HAL_StatusTypeDef error = HAL_ETH_WritePHYRegister(ADIN_ETH_HANDLE, pPhy->PHY_ADDR, (uint32_t)EXT_REG_PTR, tx_val);
    if(error != HAL_OK) return error;
    tx_val = val;
    error |= HAL_ETH_WritePHYRegister(ADIN_ETH_HANDLE, pPhy->PHY_ADDR, (uint32_t)EXT_REG_DATA, tx_val);

    return error;
}



HAL_StatusTypeDef ADIN1300_GetPhyID(adin1300_Object_t *pPhy, uint32_t *id)
{

    uint16_t phy_id_msb = 0;
    uint16_t phy_id_lsb = 0;

    HAL_StatusTypeDef error = ADIN1300_ReadReg(pPhy, PHY_ID_1, &phy_id_msb);
    if(error != HAL_OK) return error;
    error = ADIN1300_ReadReg(pPhy, PHY_ID_2, &phy_id_lsb);
    if(error != HAL_OK) return error;

    *id = ((uint32_t)phy_id_lsb) | ((uint32_t)phy_id_msb << 16);
    *id &= 0xFFFFFFF0; // Remove Revision Number

    return HAL_OK;
}


HAL_StatusTypeDef ADIN1300_Init(adin1300_Object_t *pPhy)
{
    pPhy->PHY_ADDR = ADIN1300_PHY_ADDR;

    uint32_t id = 0;
    HAL_StatusTypeDef error = ADIN1300_GetPhyID(pPhy, &id);

    if(id != ADIN1300_PHY_ID || error != HAL_OK)
    {
        // Wrong Phy ID or none at all --> sweep IDs
        uint8_t found = 0;
        for(uint8_t addr = 0; addr <= 31; addr++)
        {
            pPhy->PHY_ADDR = addr;
            error = ADIN1300_GetPhyID(pPhy, &id);
            if(id == ADIN1300_PHY_ID || error == HAL_OK)
            {
                found++;
                break;
            }
        }
        if(found == 0) return HAL_ERROR;
    }

    // PHY found add ADDR = pPhy->PHY_ADDR
    //SW reset PHY
    error = HAL_OK;
    error |= ADIN1300_Reset(pPhy);

    // AUTONEG_DEV
    uint16_t reg_val = SELECTOR_ADV | FD_10_ADV | FD_100_ADV | PAUSE_ADV | APAUSE_ADV;
    error |= ADIN1300_WriteReg(pPhy, AUTONEG_ADV, reg_val);

    // MSTR_SLV_CTRL
    reg_val = FD_1000_ADV;
    error |= ADIN1300_WriteReg(pPhy, MSTR_SLV_CONTROL, reg_val);

    // PHY_CTRL_1
    reg_val = PHY_CTRL_1_RSRVD | DIAG_CLK_EN | AUTO_MDI_EN;
    error |= ADIN1300_WriteReg(pPhy, PHY_CTRL_1, reg_val);

    // PHY_CTRL_1
    reg_val = CLK_CNTRL_DEFAULT | PHY_CTRL_2_RSRVD | DN_SPEED_TO_10_EN | DN_SPEED_TO_100_EN;
    error |= ADIN1300_WriteReg(pPhy, PHY_CTRL_2, reg_val);

    // LED_CTRL_1
    reg_val = LED_PUL_STR_EN | LED_A_EXT_CFG_EN;
    error |= ADIN1300_WriteReg(pPhy, LED_CTRL_1, reg_val);

    // LED_CTRL_2
    reg_val = LED_CTRL_2_RSRVD | LED_A_CFG;
    error |= ADIN1300_WriteReg(pPhy, LED_CTRL_2, reg_val);

    // GE_CLK_CFG
    reg_val = GE_CLK_FREE_125_EN;
    error |= ADIN1300_WriteExtReg(pPhy, GE_CLK_CFG, reg_val);

    // Set MII_CONTROL register
    reg_val = SPEED_SEL_1GBPS | AUTONEG_EN | DPLX_MODE_FD | RESTART_ANEG;
    error |= ADIN1300_WriteReg(pPhy, MII_CONTROL, reg_val);

    return error;
}



int32_t ADIN1300_GetLinkStatus(adin1300_Object_t *pPhy)
{
    uint16_t phy_stat = 0;
    if(ADIN1300_ReadReg(pPhy, PHY_STATUS_1, &phy_stat) != HAL_OK) return ETH_PHY_STATUS_LINK_ERROR;

    uint16_t link = phy_stat & LINK_STAT_MASK;
    if(link == 0) return ETH_PHY_STATUS_LINK_DOWN;

    uint16_t autoneg_done = phy_stat & AUTONEG_STAT_MASK;
    if(autoneg_done == 0) return ETH_PHY_STATUS_AUTONEGO_NOT_DONE;

    uint16_t speed = phy_stat & HCD_TECH_MASK;
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
    error |= ADIN1300_WriteReg(pPhy, MII_CONTROL, SFT_RST);

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