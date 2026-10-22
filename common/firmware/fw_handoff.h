#ifndef FW_HANDOFF_H
#define FW_HANDOFF_H
#include "stm32n6xx.h"

/** @brief Quiesce the FSBL and remove ROM memory restrictions before secure LRUN entry.
 * The application owns its MPU configuration. Keep the FSBL's clocks running:
 * the persistent application intentionally inherits them. IRQs remain masked
 * until the assembly branch has installed the application's stack.
 */
static inline void fw_prepare_handoff(uint32_t vector)
{
    __disable_irq();
    SysTick->CTRL = 0U;
    SysTick->LOAD = 0U;
    SysTick->VAL = 0U;
    for (uint32_t i = 0; i < ((ICB->ICTR & ICB_ICTR_INTLINESNUM_Msk) + 1U); ++i) {
        NVIC->ICER[i] = UINT32_MAX;
        NVIC->ICPR[i] = UINT32_MAX;
    }
    SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk | SCB_ICSR_PENDSVCLR_Msk;
    /* Clean copied instructions out of D-cache before invalidating I-cache. */
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
    if (SCB->CCR & SCB_CCR_DC_Msk) SCB_DisableDCache();
#endif
#if defined(__ICACHE_PRESENT) && (__ICACHE_PRESENT == 1U)
    SCB_DisableICache();
#endif
#if defined(__MPU_PRESENT) && (__MPU_PRESENT == 1U)
    ARM_MPU_Disable();
#endif
    SCB->VTOR = vector;
    __DSB();
    __ISB();
}
#endif
