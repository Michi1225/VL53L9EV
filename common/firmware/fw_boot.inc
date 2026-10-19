/* Included once from FSBL main.c's USER CODE section. Keeping the integration
 * here avoids edits to CubeMX-generated source lists and boot middleware. */
#include "fw_update.c"
#include "fw_flash.c"
extern SEMPER_Handle_t Flash;
extern BOOTStatus_TypeDef JumpToApplication(void);
static fw_update_t fw_boot_state;

/** @brief Select and verify a boot slot, load its image into SRAM, and jump to the application. */
static int fw_boot_run(void)
{
    fw_flash_t flash={&Flash,fw_watchdog_refresh};
    fw_io_t io=fw_flash_io(&flash);
    int result=fw_open(&fw_boot_state,&io);
    if(result)return result;
    uint32_t slot;bool trial;
    result=fw_boot_select(&fw_boot_state,&slot,&trial);
    if(result)return result;
    if(trial && fw_watchdog_start())return FW_IO_ERROR;
    uint32_t size=fw_boot_state.meta.image[slot].size;
    uint8_t *destination=(uint8_t *)FW_RAM_BASE;
    /* The application is LRUN: load its signed header/padding plus payload,
     * leaving the vector table at 0x34000400 for the existing jump helper. */
    for(uint32_t offset=0;offset<size;){
        uint32_t count=size-offset;if(count>512U)count=512U;
        if(io.read(io.context,fw_slot_address(slot)+offset,destination+offset,count))return FW_IO_ERROR;
        fw_watchdog_refresh();offset+=count;
    }
    uint8_t digest[32];fw_sha256(destination,size,digest);
    if(memcmp(digest,fw_boot_state.meta.image[slot].sha256,32))return FW_BAD_HASH;
    fw_watchdog_refresh();
    __DSB();__ISB();
    (void)JumpToApplication();
    return FW_BAD_IMAGE; /* A valid application's reset handler never returns. */
}
