#ifndef __ADIN_REG_H
#define __ADIN_REG_H


typedef enum
{
    MII_CONTROL          = 0x0000,
    MII_STATUS           = 0x0001,
    PHY_ID_1             = 0x0002,
    PHY_ID_2             = 0x0003,
    AUTONEG_ADV          = 0x0004,
    LP_ABILITY           = 0x0005,
    AUTONEG_EXP          = 0x0006,
    TX_NEXT_PAGE         = 0x0007,
    LP_RX_NEXT_PAGE      = 0x0008,
    MSTR_SLV_CONTROL     = 0x0009,
    MSTR_SLV_STATUS      = 0x000A,

    EXT_STATUS           = 0x000F,

    EXT_REG_PTR          = 0x0010,
    EXT_REG_DATA         = 0x0011,
    PHY_CTRL_1           = 0x0012,
    PHY_CTRL_STATUS_1    = 0x0013,
    RX_ERR_CNT           = 0x0014,
    PHY_CTRL_STATUS_2    = 0x0015,
    PHY_CTRL_2           = 0x0016,
    PHY_CTRL_3           = 0x0017,
    IRQ_MASK             = 0x0018,
    IRQ_STATUS           = 0x0019,
    PHY_STATUS_1         = 0x001A,
    LED_CTRL_1           = 0x001B,
    LED_CTRL_2           = 0x001C,
    LED_CTRL_3           = 0x001D,

    PHY_STATUS_2         = 0x001F,

} adin1300_RegTypeDef_t;



#endif