#ifndef FW_RESET_H
#define FW_RESET_H
#include "stm32n6xx.h"

/** @brief Restore ROM clock selections immediately before a software system reset.
 * STM32N6 ES0620 section 2.2.22: source clocks must be running before switching.
 * The caller has enabled HSI/LSI, completed flash preparation and disconnected
 * USB. Execute from internal SRAM; call NVIC_SystemReset immediately afterward.
 */
static inline void fw_prepare_reset_clocks(void)
{
    __disable_irq();
    /* Restore PER first so downstream defaults have a live HSI input. */
    MODIFY_REG(RCC->CCIPR7,RCC_CCIPR7_PERSEL|RCC_CCIPR7_RTCSEL,RCC_CCIPR7_RTCSEL_1);
    CLEAR_BIT(RCC->CCIPR6,RCC_CCIPR6_XSPI1SEL|RCC_CCIPR6_OTGPHY1CKREFSEL);
    CLEAR_BIT(RCC->CCIPR8,RCC_CCIPR8_SDMMC1SEL|RCC_CCIPR8_SDMMC2SEL);
    CLEAR_BIT(RCC->CCIPR13,RCC_CCIPR13_USART1SEL|RCC_CCIPR13_USART2SEL|
              RCC_CCIPR13_UART4SEL|RCC_CCIPR13_UART5SEL);
    (void)RCC->CCIPR7;(void)RCC->CCIPR6;(void)RCC->CCIPR8;(void)RCC->CCIPR13;
    __DSB();__ISB();
}
#endif
