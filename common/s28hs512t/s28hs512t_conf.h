/**
  ******************************************************************************
  * @file    s28hs512t_conf.h
  * @author  Takamura Sota
  * @brief   S28HS512T OctoSPI memory configuration template file.
  *          This file should be copied to the application folder and renamed
  *          to s28hs512t_conf.h
  ******************************************************************************
  */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef S28HS512T_CONF_H
#define S28HS512T_CONF_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32n6xx_hal.h"

/** @addtogroup BSP
  * @{
  */
#define CONF_OSPI_ODS                S28HS512T_CR4_IOIMPD_30   /* S28HS512T Output Driver Strength */

#define DUMMY_CYCLES_READ            8U
#define DUMMY_CYCLES_READ_OCTAL      10U
#define DUMMY_CYCLES_READ_OCTAL_DTR  23U
#define DUMMY_CYCLES_REG             1U
#define DUMMY_CYCLES_REG_OCTAL       5U
#define DUMMY_CYCLES_REG_OCTAL_DTR   5U

/**
  * @}
  */

#ifdef __cplusplus
}
#endif

#endif /* MX66UW1G45G_CONF_H */
