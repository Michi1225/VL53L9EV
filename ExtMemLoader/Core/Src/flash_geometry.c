/* Board-specific override for the loader's uniform-sector weak wrapper. */
#include "memory_wrapper.h"
#include "stm32_extmemloader_conf.h"
#include "stm32_extmem.h"
#include <stdint.h>

static uint32_t sector_size(uint32_t offset)
{
    if (offset < 0x20000U) return 0x1000U;
    if (offset < 0x40000U) return 0x20000U;
    return 0x40000U;
}

MEM_STATUS memory_sectorerase(uint32_t first, uint32_t last, uint32_t ignored)
{
    (void)ignored;
    const uint32_t base = STM32EXTLOADER_DEVICE_ADDR;
    if (first < base || last < first || last - base >= STM32EXTLOADER_DEVICE_SIZE)
        return MEM_FAIL;
    uint32_t offset = first - base;
    uint32_t end = last - base;
    if (offset % sector_size(offset)) return MEM_FAIL;
    /* CubeProgrammer's inclusive end may be the start of the final sector.
     * Erase each intersecting physical sector exactly once, never add a fixed
     * 256 KiB to a hybrid-sector end address (that can erase the FSBL). */
    do
    {
        uint32_t size = sector_size(offset);
        if (EXTMEM_EraseSector(STM32EXTLOADER_DEVICE_MEMORY_ID, offset, size) != EXTMEM_OK)
            return MEM_FAIL;
        offset += size;
    } while (offset <= end);
    return MEM_OK;
}
