#ifndef __ADIN_H
#define __ADIN_H


#include "eth.h"
#include "main.h"
#include "ADIN1300_reg.h"
extern ETH_HandleTypeDef heth1;

#define ADIN_ETH_HANDLE (&heth1)

#define ADIN1300_PHY_ID 0x0283bc30      //Fixed ID for ADIN1300
#define ADIN1300_PHY_ADDR (1)           //Set by Straps
#define ADIN1300_RST_TIMEOUT 500        //Reset Timeout in ms


typedef struct
{
    uint32_t PHY_ADDR;


}adin1300_Object_t;

typedef enum
{
    ADIN1300_STATUS_ERROR = -1,
    ADIN1300_STATUS_OK = 0,
}adin1300_Status_t;

HAL_StatusTypeDef ADIN1300_Init(adin1300_Object_t *pPhy);
HAL_StatusTypeDef ADIN1300_Reset(adin1300_Object_t *pPhy);
int32_t ADIN1300_GetLinkStatus(adin1300_Object_t *pPhy);


HAL_StatusTypeDef ADIN1300_GetPhyID(adin1300_Object_t *pPhy, uint32_t *id);

extern adin1300_Object_t phy;

#endif