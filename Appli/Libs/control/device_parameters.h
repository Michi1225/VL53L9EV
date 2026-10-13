#ifndef DEVICE_PARAMETERS_H
#define DEVICE_PARAMETERS_H
#include <stdint.h>
#define DEVICE_PARAMETERS_MAX_JSON 16384U
void device_parameters_init(void);
uint8_t *device_parameters_packet(uint32_t *size);
#endif
