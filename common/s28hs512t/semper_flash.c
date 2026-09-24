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