#ifndef DEVICE_PARAMETERS_H
#define DEVICE_PARAMETERS_H
#include <stdint.h>
#define DEVICE_PARAMETERS_MAX_JSON 16384U
/** @brief Read device parameters from flash and prepare JSON telemetry or an error payload. */
void device_parameters_init(void);
/** @brief Return the cached PARM packet and its byte length. */
uint8_t *device_parameters_packet(uint32_t *size);
#endif
