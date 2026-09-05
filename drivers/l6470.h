#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ztmb/bus.h"

/* ---- L6470 command opcodes ---- */
#define L6470_CMD_NOP          0x00
#define L6470_CMD_GET_STATUS   0xD0
#define L6470_CMD_RESET_DEVICE 0xC0
#define L6470_CMD_SOFT_STOP    0xB0
#define L6470_CMD_HARD_STOP    0xB8
#define L6470_CMD_SOFT_HIZ     0xA0
#define L6470_CMD_HARD_HIZ     0xA8

/* Commands with extra data. */
#define L6470_CMD_SET_PARAM    0x00
#define L6470_CMD_GET_PARAM    0x20
#define L6470_CMD_RUN          0x50
#define L6470_CMD_MOVE         0x40
#define L6470_CMD_GOTO         0x60
#define L6470_CMD_GOTO_DIR     0x68
#define L6470_CMD_GO_HOME      0x70
#define L6470_CMD_GO_MARK      0x78
#define L6470_CMD_RESET_POS    0xD8

/* Selected parameter addresses. */
#define L6470_REG_ABS_POS      0x01
#define L6470_REG_EL_POS       0x02
#define L6470_REG_MARK         0x03
#define L6470_REG_SPEED        0x04
#define L6470_REG_ACC          0x05
#define L6470_REG_DEC          0x06
#define L6470_REG_MAX_SPEED    0x07
#define L6470_REG_MIN_SPEED    0x08
#define L6470_REG_KVAL_HOLD    0x09
#define L6470_REG_KVAL_RUN     0x0A
#define L6470_REG_KVAL_ACC     0x0B
#define L6470_REG_KVAL_DEC     0x0C
#define L6470_REG_STEP_MODE    0x16
#define L6470_REG_ALARM_EN     0x17
#define L6470_REG_CONFIG       0x18

/* STATUS register flags. These fault flags are active-low. */
#define L6470_STATUS_STEP_LOSS_B (1u << 14)
#define L6470_STATUS_STEP_LOSS_A (1u << 13)
#define L6470_STATUS_TH_SD       (1u << 11)
#define L6470_STATUS_TH_WRN      (1u << 10)

#define L6470_STATUS_STALL_MASK \
    (L6470_STATUS_STEP_LOSS_A | L6470_STATUS_STEP_LOSS_B)
#define L6470_STATUS_THERMAL_MASK \
    (L6470_STATUS_TH_WRN | L6470_STATUS_TH_SD)

#define L6470_RUN_FRAME_COUNT  4u
#define L6470_MOVE_FRAME_COUNT 4u

/* Recommended physical bus setup and the harmless frame used when this slot is idle. */
extern const bus_spi_config_t l6470_spi_config;
extern const bus_spi_chain_slot_config_t l6470_chain_slot_config;

/* One driver object represents exactly one L6470 silicon device. */
typedef struct {
    bus_spi_target_t *spi;
} l6470_t;

bool l6470_init(l6470_t *device, bus_spi_target_t *spi_target);
bool l6470_ready(const l6470_t *device);

bool l6470_get_status(l6470_t *device, uint16_t *status);

/* GET_STATUS fault helpers. Fault bits are active-low in the L6470. */
static inline bool l6470_status_has_stall(uint16_t status)
{
    return (status & L6470_STATUS_STALL_MASK) != L6470_STATUS_STALL_MASK;
}

static inline bool l6470_status_has_over_temp(uint16_t status)
{
    return (status & L6470_STATUS_THERMAL_MASK) != L6470_STATUS_THERMAL_MASK;
}

bool l6470_reset_device(l6470_t *device);
bool l6470_soft_stop(l6470_t *device);
bool l6470_hard_hiz(l6470_t *device);

bool l6470_set_param(l6470_t *device,
                     uint8_t reg,
                     uint32_t value,
                     uint8_t nbytes);
bool l6470_get_param(l6470_t *device,
                     uint8_t reg,
                     uint8_t nbytes,
                     uint32_t *value);

bool l6470_run(l6470_t *device, bool dir_fwd, uint32_t speed_20bit);
bool l6470_move(l6470_t *device, bool dir_fwd, uint32_t nsteps_22bit);
bool l6470_reset_pos(l6470_t *device);

/* Encoders are available to modules that want to command several chain slots together. */
void l6470_encode_run(bool dir_fwd,
                      uint32_t speed_20bit,
                      uint8_t frames[L6470_RUN_FRAME_COUNT]);
void l6470_encode_move(bool dir_fwd,
                       uint32_t nsteps_22bit,
                       uint8_t frames[L6470_MOVE_FRAME_COUNT]);

/* L6470 helpers: convert human units to register units. */
static inline uint32_t l6470_speed_from_steps_per_s(float sps)
{
    float v = sps * 67.108864f;
    if (v < 0) v = 0;
    if (v > 0xFFFFF) v = 0xFFFFF;
    return (uint32_t)(v + 0.5f);
}

static inline uint16_t l6470_acc_from_sps2(float a)
{
    float v = a * 0.137438f;
    if (v < 0) v = 0;
    if (v > 0x0FFF) v = 0x0FFF;
    return (uint16_t)(v + 0.5f);
}
