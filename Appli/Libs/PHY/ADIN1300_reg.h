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

    GE_SFT_RST           = 0xFF0C,
    GE_SFT_RST_CFG_EN    = 0xFF0D,
    GE_IRQ_EN            = 0xFF1D,
    GE_IRQ_LAT           = 0xFF1E,
    GE_CLK_CFG           = 0xFF1F,
    GE_RGMII_CFG         = 0xFF23,
    GE_RMII_CFG          = 0xFF24,
    GE_PHY_BASE_CFG      = 0xFF26,
    GE_PHY_IF_CFG        = 0xFF27,
    GE_B10_REGEN_PRE     = 0xFF38,
    GE_RGMII_IO_CNTRL    = 0xFF3A,
    GE_CLK_IO_CNTRL      = 0xFF3B,
    GE_LNK_STAT_INV_EN   = 0xFF3C,
    GE_IO_GP_CLK_OR_CNTRL= 0xFF3D,
    GE_IO_GP_OUT_OR_CNTRL= 0xFF3E,
    GE_IO_INT_N_OR_CNTRL = 0xFF3F,
    GE_IO_LED_A_OR_CNTRL = 0xFF41,

} adin1300_RegTypeDef_t;


// MII_CONTROL Fields
#define UNIDIR_EN               ((uint16_t)(1 << 5))
#define SPEED_SEL_10MBPS        ((uint16_t)(0))
#define SPEED_SEL_100MBPS       ((uint16_t)(1 << 13))
#define SPEED_SEL_1GBPS         ((uint16_t)(1 << 6))
#define COLTEST_EN              ((uint16_t)(1 << 7))
#define DPLX_MODE_FD            ((uint16_t)(1 << 8))
#define DPLX_MODE_HD            ((uint16_t)(0))
#define RESTART_ANEG            ((uint16_t)(1 << 9))
#define ISOLATE                 ((uint16_t)(1 << 10))
#define SFT_PD                  ((uint16_t)(1 << 11))
#define AUTONEG_EN              ((uint16_t)(1 << 12))
#define LOOPBACK                ((uint16_t)(1 << 14))
#define SFT_RST                 ((uint16_t)(1 << 15))


// AUTONEG_ADV Fields
#define APAUSE_ADV              ((uint16_t)(1 << 11))
#define PAUSE_ADV               ((uint16_t)(1 << 10))
#define T_4_ADV                 ((uint16_t)(1 << 9))
#define FD_100_ADV              ((uint16_t)(1 << 8))
#define HD_100_ADV              ((uint16_t)(1 << 7))
#define FD_10_ADV               ((uint16_t)(1 << 6))
#define HD_10_ADV               ((uint16_t)(1 << 5))
#define SELECTOR_ADV            ((uint16_t)(0b001))


// MSTR_SLV_CTRL
#define MAN_MSTR_SLV_EN_ADV     ((uint16_t)(1 << 11))
#define MAN_MSTR_ADV            ((uint16_t)(1 << 10))
#define PREF_MSTR_ADV           ((uint16_t)(1 << 9))
#define FD_1000_ADV             ((uint16_t)(1 << 8))
#define HD_1000_ADV             ((uint16_t)(1 << 7))


// PHY_CTRL_1
#define PHY_CTRL_1_RSRVD        ((uint16_t)(1 << 1))
#define DIAG_CLK_EN             ((uint16_t)(1 << 2))
#define MAN_MDIX                ((uint16_t)(1 << 9))
#define AUTO_MDI_EN             ((uint16_t)(1 << 10))


// PHY_CTRL_2
#define CLK_CNTRL_DEFAULT       ((uint16_t)(0b100 << 1))
#define GROUP_MDIO_EN           ((uint16_t)(1 << 6))
#define PHY_CTRL_2_RSRVD        ((uint16_t)(0b110 << 7))
#define DN_SPEED_TO_10_EN       ((uint16_t)(1 << 10))
#define DN_SPEED_TO_100_EN      ((uint16_t)(1 << 11))


// LED_CTRL_1
#define LED_A_EXT_CFG_EN        ((uint16_t)(1 << 10))
#define LED_PUL_STR_EN          ((uint16_t)(1))


// LED_CTRL_2
#define LED_A_CFG               ((uint16_t)(0b0100))
#define LED_CTRL_2_RSRVD        ((uint16_t)(0x210 << 4))


// GE_CLK_CFG
#define GE_CLK_25_EN            ((uint16_t)(1))
#define GE_CLK_HRT_FREE_EN      ((uint16_t)(1 << 1))
#define GE_CLK_HRT_RCVR_EN      ((uint16_t)(1 << 2))
#define GE_REF_CLK_EN           ((uint16_t)(1 << 3))
#define GE_CLK_FREE_125_EN      ((uint16_t)(1 << 4))
#define GE_CLK_RCVR_125_EN      ((uint16_t)(1 << 5))


// PHY_STATUS_1
#define HCD_TECH_MASK           (0b111 << 7)
#define AUTONEG_STAT_MASK       (1 << 12)
#define LINK_STAT_MASK          (1 << 6)






#endif