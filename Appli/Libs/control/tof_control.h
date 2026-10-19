#ifndef TOF_CONTROL_H
#define TOF_CONTROL_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TOF_GET 1U
#define TOF_START 2U
#define TOF_STOP 3U
#define TOF_SET 4U
#define TOF_RESET 5U
#define TOF_POWER_DOWN 6U
#define TOF_OK 0U
#define TOF_BAD_COMMAND 1U
#define TOF_BAD_PARAMETER 2U
#define TOF_BUSY 3U
#define TOF_SENSOR_ERROR 4U
#define TOF_MUST_STOP 5U
#define TOF_FAULT_LATCHED 6U
#define TOF_POWERED_DOWN 7U
#define TOF_INITIALIZING 0U
#define TOF_STOPPED 1U
#define TOF_STREAMING 2U
#define TOF_FAULT 3U
#define TOF_OFF 4U
#define TOF_USB 1U
#define TOF_TCP 2U
#define TOF_STATUS_PACKET_SIZE 52U

typedef struct {
    uint32_t period_us, exposure_ms, binning, power, context, dss_enabled;
} tof_config_t;
typedef struct {
    uint32_t request_id, operation, result;
    tof_config_t config;
    uint32_t route, session;
} tof_command_t;
typedef struct {
    uint32_t revision;
    int32_t error;
    tof_config_t config;
    uint32_t state, width, height, frames;
} tof_status_t;
typedef struct { uint8_t data[28]; unsigned used; } tof_parser_t;
typedef struct { uint32_t route, session; uint8_t packet[TOF_STATUS_PACKET_SIZE]; } tof_reply_t;

/** @brief Check supported binning, timing, power, context, and DSS settings. */
bool tof_config_valid(const tof_config_t *config);
/** @brief Create the bounded ToF command and reply queues. */
unsigned tof_control_init(void);
/* One parser per incoming byte stream; route/session identify its reply path. */
/** @brief Parse fragmented CTRL input and queue validated ToF commands. */
void tof_control_feed(tof_parser_t *parser, const uint8_t *data, size_t length,
                      uint32_t route, uint32_t session);
/** @brief Take the next queued ToF command without blocking. */
bool tof_control_next(tof_command_t *command);
/** @brief Publish an atomic snapshot of the current ToF status. */
void tof_control_publish(const tof_status_t *status);
/** @brief Queue a command acknowledgement for the originating transport session. */
void tof_control_reply(const tof_command_t *command, uint32_t result);
/** @brief Take the next queued ToF acknowledgement without blocking. */
bool tof_control_next_reply(tof_reply_t *reply);
/** @brief Build an unsolicited ToF status packet. */
void tof_control_status_packet(uint8_t packet[TOF_STATUS_PACKET_SIZE]);
#endif
