#include "tof_control.h"
#include "main.h"
#include "tx_api.h"
#include <string.h>

#define QUEUE_DEPTH 8U
static TX_QUEUE commands, replies;
static ULONG command_storage[QUEUE_DEPTH * (sizeof(tof_command_t) / 4U)];
static ULONG reply_storage[QUEUE_DEPTH * (sizeof(tof_reply_t) / 4U)];
static tof_status_t current;
_Static_assert(sizeof(tof_command_t) % 4U == 0 && sizeof(tof_command_t) <= 64U, "ThreadX message size");
_Static_assert(sizeof(tof_reply_t) % 4U == 0 && sizeof(tof_reply_t) <= 64U, "ThreadX message size");

/** @brief Decode a little-endian 16-bit integer. */
static uint32_t get16(const uint8_t *p) { return p[0] | ((uint32_t)p[1] << 8); }
/** @brief Decode a little-endian 32-bit integer. */
static uint32_t get32(const uint8_t *p) { return get16(p) | (get16(p + 2) << 16); }
/** @brief Encode a 16-bit integer in little-endian byte order. */
static void put16(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; }
/** @brief Encode a 32-bit integer in little-endian byte order. */
static void put32(uint8_t *p, uint32_t v) { put16(p, v); put16(p + 2, v >> 16); }

/** @brief Check supported binning, timing, power, context, and DSS settings. */
bool tof_config_valid(const tof_config_t *c)
{
    bool binning = c->binning == 2 || c->binning == 4 || c->binning == 6 ||
                   c->binning == 8 || c->binning == 12 || c->binning == 24;
    return binning && c->period_us >= 10000U && c->period_us <= 1000000U &&
        c->exposure_ms >= 1U && c->exposure_ms <= 30U &&
        c->exposure_ms * 1000U <= c->period_us && c->power <= 2U && c->context <= 1U && c->dss_enabled <= 1U &&
        (c->dss_enabled || c->context == 0U);
}

/** @brief Create the bounded ToF command and reply queues. */
unsigned tof_control_init(void)
{
    UINT result = tx_queue_create(&commands, "ToF commands", sizeof(tof_command_t) / 4U,
                                  command_storage, sizeof(command_storage));
    if (result != TX_SUCCESS) return result;
    return tx_queue_create(&replies, "ToF replies", sizeof(tof_reply_t) / 4U,
                           reply_storage, sizeof(reply_storage));
}

/** @brief Publish an atomic snapshot of the current ToF status. */
void tof_control_publish(const tof_status_t *status)
{
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    current = *status;
    __set_PRIMASK(mask);
}

/** @brief Encode a consistent ToF status snapshot and command result in a TOFS packet. */
static void status_packet(uint8_t *p, uint32_t id, uint32_t operation, uint32_t result)
{
    tof_status_t s;
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    s = current;
    __set_PRIMASK(mask);
    memcpy(p, "TOFS", 4);
    put32(p + 4, id); put32(p + 8, 36U);
    put16(p + 12, 2U); put16(p + 14, operation);
    put32(p + 16, HAL_GetTick()); put32(p + 20, s.revision);
    put32(p + 24, (uint32_t)s.error); put32(p + 28, s.config.period_us);
    put16(p + 32, s.config.exposure_ms); put16(p + 34, s.config.binning);
    p[36] = s.config.power; p[37] = s.config.context;
    p[38] = s.state; p[39] = result;
    put16(p + 40, s.width); put16(p + 42, s.height); put32(p + 44, s.frames);
    put16(p + 48, s.config.dss_enabled ? 0U : 1U); put16(p + 50, 0U);
}

/** @brief Build an unsolicited ToF status packet. */
void tof_control_status_packet(uint8_t packet[TOF_STATUS_PACKET_SIZE]) { status_packet(packet, 0, 0, TOF_OK); }
/** @brief Queue a command acknowledgement for the originating transport session. */
void tof_control_reply(const tof_command_t *command, uint32_t result)
{
    tof_reply_t reply = {.route = command->route, .session = command->session};
    status_packet(reply.packet, command->request_id, command->operation, result);
    /* Bounded queues: callers must allow one outstanding request per client. */
    (void)tx_queue_send(&replies, &reply, TX_NO_WAIT);
}
/** @brief Take the next queued ToF command without blocking. */
bool tof_control_next(tof_command_t *command)
{
    return tx_queue_receive(&commands, command, TX_NO_WAIT) == TX_SUCCESS;
}
/** @brief Take the next queued ToF acknowledgement without blocking. */
bool tof_control_next_reply(tof_reply_t *reply)
{
    return tx_queue_receive(&replies, reply, TX_NO_WAIT) == TX_SUCCESS;
}

/** @brief Parse fragmented CTRL input and queue validated ToF commands. */
void tof_control_feed(tof_parser_t *parser, const uint8_t *data, size_t length,
                      uint32_t route, uint32_t session)
{
    while (length--)
    {
        parser->data[parser->used++] = *data++;
        if (parser->used < 4U) continue;
        if (memcmp(parser->data, "CTRL", 4) != 0)
        {
            memmove(parser->data, parser->data + 1, --parser->used);
            continue;
        }
        if (parser->used < 16U) continue;
        uint32_t size = get32(parser->data + 8);
        uint32_t version = get16(parser->data + 12);
        if (size > 12U || (version != 1U && version != 2U))
        {
            memmove(parser->data, parser->data + 1, --parser->used);
            continue;
        }
        if (parser->used < 16U + size) continue;
        tof_command_t c = {.request_id = get32(parser->data + 4),
            .operation = get16(parser->data + 14), .route = route, .session = session};
        if (c.operation < TOF_GET || c.operation > TOF_POWER_DOWN) c.result = TOF_BAD_COMMAND;
        else if (!c.request_id || size != (c.operation == TOF_SET ? 12U : 0U)) c.result = TOF_BAD_PARAMETER;
        else if (c.operation == TOF_SET)
        {
            const uint8_t *p = parser->data + 16;
            uint32_t flags = get16(p + 10);
            c.config = (tof_config_t){get32(p), get16(p + 4), get16(p + 6), p[8], p[9], !(flags & 1U)};
            if ((flags & ~(version == 2U ? 1U : 0U)) || !tof_config_valid(&c.config)) c.result = TOF_BAD_PARAMETER;
        }
        parser->used = 0;
        if (tx_queue_send(&commands, &c, TX_NO_WAIT) != TX_SUCCESS)
            tof_control_reply(&c, TOF_BUSY);
    }
}
