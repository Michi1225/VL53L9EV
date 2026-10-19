#include "fw_wire.h"
#include <string.h>
/** @brief Decode a little-endian 32-bit protocol field. */
static uint32_t fw_wire_u32(const uint8_t *p)
{ return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
/** @brief Reassemble bounded DFUC and CTRL frames and dispatch complete requests. */
void fw_wire_feed(fw_parser_t *p,const uint8_t *data,size_t size,fw_frame_fn callback,void *ctx)
{
    while(size--){
        p->frame[p->used++]=*data++;
        if(p->used<4)continue;
        bool dfu=memcmp(p->frame,"DFUC",4)==0,control=memcmp(p->frame,"CTRL",4)==0;
        if(!dfu && !control){memmove(p->frame,p->frame+1,--p->used);continue;}
        if(p->used<16)continue;
        uint32_t length=fw_wire_u32(p->frame+8);
        uint32_t version=p->frame[12]|((uint32_t)p->frame[13]<<8);
        if((dfu && (length>FW_WIRE_MAX-16 || version!=1)) ||
           (control && (length>12 || (version!=1 && version!=2)))){
            memmove(p->frame,p->frame+1,--p->used);continue;
        }
        if(p->used<16+length)continue;
        callback(ctx,p->frame,p->used);p->used=0;
    }
}
