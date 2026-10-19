#include "fw_flash.h"

/** @brief Wait for flash readiness with a timeout and an optional idle callback. */
static int fw_flash_wait(fw_flash_t *f)
{
    uint32_t start=HAL_GetTick();
    for(;;){
        SEMPER_Status_t status=SEMPER_GetStatus(f->flash);
        if(status==SEMPER_OK)return 0;
        if(status!=SEMPER_BUSY || HAL_GetTick()-start>4000U)return -1;
        if(f->idle)f->idle();
    }
}
/** @brief Check that a write fits entirely inside an allowed image or metadata range. */
static bool fw_flash_writable(uint32_t address,uint32_t size)
{
    const uint32_t base[]={FW_SLOT_A,FW_SLOT_B,FW_META_A,FW_META_B};
    const uint32_t limit[]={FW_MAX_IMAGE,FW_MAX_IMAGE,FW_RECORD_SIZE,FW_RECORD_SIZE};
    for(unsigned i=0;i<4;++i)
        if(address>=base[i] && address-base[i]<limit[i] && size<=limit[i]-(address-base[i]))return true;
    return false;
}
/** @brief Read flash bytes after checking device bounds and readiness. */
static int fw_flash_read(void *ctx,uint32_t address,void *data,uint32_t size)
{
    fw_flash_t *f=ctx;
    if(address>=0x04000000U || size>0x04000000U-address)return -1;
    if(fw_flash_wait(f))return -1;
    return SEMPER_Read(f->flash,address,data,size)==SEMPER_OK?0:-1;
}
/** @brief Program an allowed update range and wait for flash completion. */
static int fw_flash_write(void *ctx,uint32_t address,const void *data,uint32_t size)
{
    fw_flash_t *f=ctx;
    if(!size || !fw_flash_writable(address,size) || fw_flash_wait(f))return -1;
    if(SEMPER_Write(f->flash,address,data,size)!=SEMPER_OK)return -1;
    return fw_flash_wait(f);
}
/** @brief Erase an aligned update sector after checking the allowed flash ranges. */
static int fw_flash_erase(void *ctx,uint32_t address)
{
    fw_flash_t *f=ctx;
    if((address%FW_ERASE_SIZE) ||
       !((address>=FW_SLOT_A && address<FW_SLOT_A+FW_MAX_IMAGE) ||
         (address>=FW_SLOT_B && address<FW_SLOT_B+FW_MAX_IMAGE) ||
          address==FW_META_A || address==FW_META_B) || fw_flash_wait(f))return -1;
    if(SEMPER_Erase256K(f->flash,address)!=SEMPER_OK)return -1;
    return fw_flash_wait(f);
}
/** @brief Build the bounded flash callbacks used by the portable updater. */
fw_io_t fw_flash_io(fw_flash_t *f)
{ return (fw_io_t){f,fw_flash_read,fw_flash_write,fw_flash_erase}; }
/** @brief Reload the independent watchdog if it is already running. */
void fw_watchdog_refresh(void)
{
    if(IWDG->SR&IWDG_SR_ONF)IWDG->KR=0xAAAAU;
}
/** @brief Start the independent watchdog with a nominal 32-second trial boot timeout. */
int fw_watchdog_start(void)
{
    /* Independent clock; PR=6 divides LSI by 256. With nominal 32 kHz LSI,
     * a 4095 reload gives about 32 seconds for trial startup/confirmation. */
    LL_RCC_LSI_Enable();
    uint32_t start=HAL_GetTick();
    while(!LL_RCC_LSI_IsReady())if(HAL_GetTick()-start>1000U)return -1;
    IWDG->KR=0xCCCCU;IWDG->KR=0x5555U;
    IWDG->PR=6U;IWDG->RLR=4095U;IWDG->WINR=4095U;
    start=HAL_GetTick();
    while(IWDG->SR&(IWDG_SR_PVU|IWDG_SR_RVU|IWDG_SR_WVU))
        if(HAL_GetTick()-start>1000U)return -1;
    IWDG->KR=0xAAAAU;return 0;
}
