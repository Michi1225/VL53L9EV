#include "semper_flash.h"
#include "s28hs512t.h"


static SEMPER_Status_t SEMPER_Reset(SEMPER_Handle_t *dev)
{
    int32_t ret = BSP_ERROR_NONE;

    if (S28HS512T_ResetEnable(dev->hxspi, S28HS512T_SPI_MODE,
                        S28HS512T_STR_TRANSFER) != S28HS512T_OK)
    {
        ret = BSP_ERROR_COMPONENT_FAILURE;
    }
    else if (S28HS512T_ResetMemory(dev->hxspi, S28HS512T_SPI_MODE,
                                S28HS512T_STR_TRANSFER) != S28HS512T_OK)
    {
        ret = BSP_ERROR_COMPONENT_FAILURE;
    }
    else if (S28HS512T_ResetEnable(dev->hxspi, S28HS512T_OPI_MODE,
                                S28HS512T_STR_TRANSFER) != S28HS512T_OK)
    {
        ret = BSP_ERROR_COMPONENT_FAILURE;
    }
    else if (S28HS512T_ResetMemory(dev->hxspi, S28HS512T_OPI_MODE,
                                S28HS512T_STR_TRANSFER) != S28HS512T_OK)
    {
        ret = BSP_ERROR_COMPONENT_FAILURE;
    }
    else if (S28HS512T_ResetEnable(dev->hxspi, S28HS512T_OPI_MODE,
                                S28HS512T_DTR_TRANSFER) != S28HS512T_OK)
    {
        ret = BSP_ERROR_COMPONENT_FAILURE;
    }
    else if (S28HS512T_ResetMemory(dev->hxspi, S28HS512T_OPI_MODE,
                                S28HS512T_DTR_TRANSFER) != S28HS512T_OK)
    {
        ret = BSP_ERROR_COMPONENT_FAILURE;
    }
    else
    {
    dev->access = XSPI_ACCESS_INDIRECT;     /* After reset S/W setting to indirect access  */
    dev->interface = S28HS512T_SPI_MODE;    /* After reset H/W back to SPI mode by default */
    dev->transfer = S28HS512T_STR_TRANSFER; /* After reset S/W setting to STR mode        */

    HAL_Delay(S28HS512T_RESET_MAX_TIME);
    }

    /* Return BSP status */
    return ret;
}




SEMPER_Status_t SEMPER_Init(
    SEMPER_Handle_t *dev,
    XSPI_HandleTypeDef *hxspi)
{
    if (dev == NULL || hxspi == NULL)
    {
        return SEMPER_ERROR_PARAM;
    }


    (void)S28HS512T_GetFlashInfo(&dev->info);

    // Get Flash into known state by performing a reset sequence
    if( SEMPER_Reset(dev) != SEMPER_OK)
    {
        return SEMPER_ERROR;
    }
    // Wait for memory ready
    else if(S28HS512T_AutoPollingMemReady(dev->hxspi, dev->interface, dev->transfer) != S28HS512T_OK)
    {
        return SEMPER_ERROR;
    }
    // Configure device to OPI mode and DTR transfer for normal operation
    else if(SEMPER_ConfigDevice(dev, S28HS512T_OPI_MODE, S28HS512T_DTR_TRANSFER) != SEMPER_OK)
    {
        return SEMPER_ERROR;
    }

    // Set XSPI CLK to 200MHz
    HAL_XSPI_SetClockPrescaler(dev->hxspi, 0);

    return SEMPER_OK;
}


SEMPER_Status_t SEMPER_ConfigDevice(
    SEMPER_Handle_t *dev,
    S28HS512T_Interface_t interface,
    S28HS512T_Transfer_t transfer)
{
    if (dev == NULL)
    {
        return SEMPER_ERROR_PARAM;
    }

    if(dev->access == SEMPER_ACCESS_MAPPED)
    {
        return SEMPER_ERROR_MAPPED_LOCKED;
    }


    switch (dev->interface)
    {
        case S28HS512T_SPI_MODE:
            //Device is currently in SPI mode, so we can switch to OPI mode if requested
            if(interface == S28HS512T_OPI_MODE)
            {
                // OPI Mode was requested
                if(transfer == S28HS512T_DTR_TRANSFER)
                {
                    // DTR transfer was requested, so we need to enter DOPI mode
                    if(SEMPER_EnterDOPIMode(dev) != SEMPER_OK)
                    {
                        return SEMPER_ERROR;
                    }
                }
                else
                {
                    // STR transfer was requested, so we need to enter SOPI mode
                    if(SEMPER_EnterSOPIMode(dev) != SEMPER_OK)
                    {
                        return SEMPER_ERROR;
                    }
                }
            }
            break;
        case S28HS512T_OPI_MODE:
            // Device is currently in OPI mode
            if(S28HS512T_OPI_MODE == interface && dev->transfer == transfer)
            {
                // Device is already in the requested mode, so nothing to do
                return SEMPER_OK;
            }
            else
            {
                // Either interface mode or transfer mode is different, go to SPI mode first, then enter the requested mode
                if(SEMPER_ExitOPIMode(dev) != SEMPER_OK)
                {
                    return SEMPER_ERROR;
                }

                if(S28HS512T_OPI_MODE == interface)
                {
                    //Device had wrong transfer rate, so we need to enter the requested OPI mode
                    if(transfer == S28HS512T_DTR_TRANSFER)
                    {
                        // DTR transfer was requested, so we need to enter DOPI mode
                        if(SEMPER_EnterDOPIMode(dev) != SEMPER_OK)
                        {
                            return SEMPER_ERROR;
                        }
                    }
                    else
                    {
                        // STR transfer was requested, so we need to enter SOPI mode
                        if(SEMPER_EnterSOPIMode(dev) != SEMPER_OK)
                        {
                            return SEMPER_ERROR;
                        }
                    }
                }
            }
            break;
        default:
            break;
    }

    dev->interface = interface;
    dev->transfer = transfer;

    return SEMPER_OK;
}

SEMPER_Status_t SEMPER_EnterDOPIMode(SEMPER_Handle_t *dev) 
{


    if( S28HS512T_WriteEnable(dev->hxspi, dev->interface, dev->transfer) != S28HS512T_OK) return SEMPER_ERROR;
    // Write new CR2 configuration
    else if(S28HS512T_WriteAnyRegister(dev->hxspi, dev->interface, dev->transfer, dev->transfer,
                                       S28HS512T_CR2_ADDR, S28HS512T_CR2_MEMLAT_10_23_CYCLES) != S28HS512T_OK) return SEMPER_ERROR;
    else if(S28HS512T_WriteEnable(dev->hxspi, dev->interface, dev->transfer) != S28HS512T_OK) return SEMPER_ERROR;
    //Write new CR3 configuration
    else if(S28HS512T_WriteAnyRegister(dev->hxspi, dev->interface, dev->transfer, dev->address_width,
                                       S28HS512T_CR3_ADDR, S28HS512T_CR3_VRGLAT_10) != S28HS512T_OK) return SEMPER_ERROR;
    else if(S28HS512T_WriteEnable(dev->hxspi, dev->interface, dev->transfer) != S28HS512T_OK) return SEMPER_ERROR;
    // Write to CR5 to change to OPI mode
    else if(S28HS512T_WriteAnyRegister(dev->hxspi, dev->interface, dev->transfer, dev->address_width,
                                       S28HS512T_CR5_ADDR, (S28HS512T_CR5_OPI_IT | S28HS512T_CR5_SDRDDR)) != S28HS512T_OK) return SEMPER_ERROR;

    else
    {
        HAL_Delay(S28HS512T_WRITE_REG_MAX_TIME);

        if(S28HS512T_AutoPollingMemReady(dev->hxspi, S28HS512T_OPI_MODE, S28HS512T_DTR_TRANSFER) != S28HS512T_OK) return SEMPER_ERROR;
        
        //TODO: Check if config was successful
        dev->interface = S28HS512T_OPI_MODE;
        dev->transfer = S28HS512T_DTR_TRANSFER;
    }
  return SEMPER_OK;
}

EMPER_Status_t SEMPER_ExitOPIMode(
    SEMPER_Handle_t *dev)
{
    if (dev == NULL)
    {
        return SEMPER_ERROR_PARAM;
    }

    uint8_t reg[2] = {0};
    reg[0] = 0x40; // SPI, STR, and reserved bit set to 1 where needed
    const uint8_t CFR5_SPI_STR_MODE = 0x40;


    // Enable Register Write
    if(S28HS512T_WriteEnable(dev->hxspi, dev->interface, dev->transfer) != S28HS512T_OK)
    {
        return SEMPER_ERROR;
    }
    // Write to CR5 to exit OPI mode
    else if (S28HS512T_WriteAnyRegister(dev->hxspi, dev->interface, dev->transfer,
              dev->address_width, S28HS512T_CR5_ADDR, CFR5_SPI_STR_MODE) != S28HS512T_OK)
    {
        return SEMPER_ERROR;
    }
    // Wait for Memor to be ready
    else if(S28HS512T_AutoPollingMemReady(dev->hxspi, S28HS512T_SPI_MODE, S28HS512T_STR_TRANSFER) != S28HS512T_OK)
    {
        return SEMPER_ERROR;
    }
    // Enable Register Write
    else if(S28HS512T_WriteEnable(dev->hxspi, S28HS512T_SPI_MODE, S28HS512T_STR_TRANSFER) != S28HS512T_OK)
    {
        return SEMPER_ERROR;
    }
    // Write to CR2 with new dummy cycles (20 Cycles in 8-8-8)
    else if (S28HS512T_WriteAnyRegister(dev->hxspi, S28HS512T_SPI_MODE, S28HS512T_STR_TRANSFER, S28HS512T_3BYTES_SIZE,
                                        S28HS512T_CR2_ADDR, S28HS512T_CR2_MEMLAT_8_20_CYCLES) != S28HS512T_OK)
    {
        return SEMPER_ERROR;
    }
    // Enable Register Write
    else if(S28HS512T_WriteEnable(dev->hxspi, S28HS512T_SPI_MODE, S28HS512T_STR_TRANSFER) != S28HS512T_OK)
    {
        return SEMPER_ERROR;
    }
    // Write to CR3 
    else if (S28HS512T_WriteAnyRegister(dev->hxspi, S28HS512T_SPI_MODE, S28HS512T_STR_TRANSFER, S28HS512T_3BYTES_SIZE,
                                        S28HS512T_CR3_ADDR, 0x00) != S28HS512T_OK)
    {
        return SEMPER_ERROR;
    }
    else
    {
        HAL_Delay(S28HS512T_WRITE_REG_MAX_TIME);

        if(S28HS512T_AutoPollingMemReady(dev->hxspi, S28HS512T_SPI_MODE, S28HS512T_STR_TRANSFER) != S28HS512T_OK)
        {
            return SEMPER_ERROR;
        }
        //TODO: Verify data


        dev->transfer = S28HS512T_STR_TRANSFER;
        dev->interface = S28HS512T_SPI_MODE;
        dev->address_width = S28HS512T_3BYTES_SIZE;
    }
}