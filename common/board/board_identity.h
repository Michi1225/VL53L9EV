#ifndef MGS_BOARD_IDENTITY_H
#define MGS_BOARD_IDENTITY_H

#define MGS_BOARD_NAME "Magnecko Sense"
#define MGS_PCB_IDENTIFIER "MGS"
#define MGS_HARDWARE_VERSION "1.0"
/* Revision 0 is '-', then A, B, ...; no separator before the revision. */
#define MGS_PCB_REVISION "-"
#define MGS_BOARD_VERSION MGS_PCB_IDENTIFIER "-" MGS_HARDWARE_VERSION MGS_PCB_REVISION
#define MGS_USB_PRODUCT_STRING MGS_BOARD_NAME " (" MGS_BOARD_VERSION ")"

#endif /* MGS_BOARD_IDENTITY_H */
