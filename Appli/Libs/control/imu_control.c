#include "imu_control.h"
#include "tof_control.h"
#include "main.h"
#include "tx_api.h"
#include <string.h>
#define QUEUE_DEPTH 8U
static TX_QUEUE commands, replies;
static ULONG command_storage[QUEUE_DEPTH * (sizeof(imu_command_t) / 4U)];
static ULONG reply_storage[QUEUE_DEPTH * (sizeof(imu_reply_t) / 4U)];
static imu_status_t current = {.config = IMU_CONFIG_DEFAULT};
_Static_assert(sizeof(imu_command_t) % 4U == 0 && sizeof(imu_command_t) <= 64U, "ThreadX command size");
_Static_assert(sizeof(imu_reply_t) % 4U == 0 && sizeof(imu_reply_t) <= 64U, "ThreadX reply size");
static uint32_t get16(const uint8_t *p) { return p[0] | ((uint32_t)p[1] << 8); }
static void put16(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; }
static void put32(uint8_t *p, uint32_t v) { put16(p, v); put16(p + 2, v >> 16); }
unsigned imu_control_init(void)
{
    UINT result = tx_queue_create(&commands, "IMU commands", sizeof(imu_command_t) / 4U,
                                  command_storage, sizeof(command_storage));
    if (result != TX_SUCCESS) return result;
    return tx_queue_create(&replies, "IMU replies", sizeof(imu_reply_t) / 4U,
                           reply_storage, sizeof(reply_storage));
}
void imu_control_publish(const imu_status_t *s)
{
    uint32_t mask = __get_PRIMASK(); __disable_irq(); current = *s; __set_PRIMASK(mask);
}
static void status_packet(uint8_t *p, uint32_t id, uint32_t op, uint32_t result)
{
    uint32_t mask = __get_PRIMASK(); __disable_irq();
    imu_status_t s = current; __set_PRIMASK(mask);
    memcpy(p, "IMUS", 4); put32(p + 4, id); put32(p + 8, 36U);
    put16(p + 12, 1U); put16(p + 14, op);
    put32(p + 16, HAL_GetTick()); put32(p + 20, s.revision); put32(p + 24, (uint32_t)s.error);
    put16(p + 28, s.config.sample_hz); put16(p + 30, s.config.publish_hz);
    put16(p + 32, s.config.acc_g); put16(p + 34, s.config.gyro_dps);
    put32(p + 36, s.state); put32(p + 40, result);
    put32(p + 44, s.published); put32(p + 48, s.dropped);
}
void imu_control_status_packet(uint8_t p[IMU_STATUS_PACKET_SIZE]) { status_packet(p, 0, 0, TOF_OK); }
void imu_control_reply(const imu_command_t *c, uint32_t result)
{
    imu_reply_t reply = {.route = c->route, .session = c->session};
    status_packet(reply.packet, c->request_id, c->operation, result);
    (void)tx_queue_send(&replies, &reply, TX_NO_WAIT);
}
bool imu_control_next(imu_command_t *c) { return tx_queue_receive(&commands, c, TX_NO_WAIT) == TX_SUCCESS; }
bool imu_control_next_reply(imu_reply_t *r) { return tx_queue_receive(&replies, r, TX_NO_WAIT) == TX_SUCCESS; }
void imu_control_submit(uint32_t id, uint32_t op, const uint8_t *p, uint32_t size,
                        uint32_t route, uint32_t session)
{
    imu_command_t c = {.request_id = id, .operation = op, .route = route, .session = session};
    if (!id || size != (op == IMU_SET ? 12U : 0U)) c.result = TOF_BAD_PARAMETER;
    else if (op == IMU_SET) {
        c.config = (imu_config_t){get16(p), get16(p + 2), get16(p + 4), get16(p + 6)};
        if (p[8] || p[9] || p[10] || p[11] || !imu_config_valid(&c.config)) c.result = TOF_BAD_PARAMETER;
    }
    if (tx_queue_send(&commands, &c, TX_NO_WAIT) != TX_SUCCESS) imu_control_reply(&c, TOF_BUSY);
}
