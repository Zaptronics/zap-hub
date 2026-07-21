#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "drivers/l6470.h"
#include "ztmb/bus.h"

#define ZTM_STEPPER_DRIVER_COUNT        4u
#define ZTM_STEPPER_ACTIVE_DRIVER_COUNT 3u
#define ZTM_STEPPER_UNUSED_DRIVER_ID    4u

typedef struct {
    bus_spi_t spi_bus;
    bus_spi_chain_t chain;
    bus_spi_chain_slot_t slots[ZTM_STEPPER_DRIVER_COUNT];
    bus_spi_target_t targets[ZTM_STEPPER_DRIVER_COUNT];
    l6470_t drivers[ZTM_STEPPER_DRIVER_COUNT];
    bool stall_reported[ZTM_STEPPER_DRIVER_COUNT];
    bool over_temp_reported[ZTM_STEPPER_DRIVER_COUNT];
    bool ready;
} ztm_stepper_t;

extern ztm_stepper_t ztm_stepper_1;

int ztm_stepper_init(void *ztm_stepper, void *spi, int8_t cs_pin, int8_t en_pin);
int ztm_stepper_poll(void);
bool ztm6470_ready(void);

/* Diagnostic and low-level module operations. Device IDs are 1..4. */
bool ztm_stepper_nop(uint8_t responses[ZTM_STEPPER_DRIVER_COUNT]);
bool ztm_stepper_get_status(uint8_t device_id, uint16_t *status);
bool ztm_stepper_get_param(uint8_t device_id,
                           uint8_t reg,
                           uint8_t nbytes,
                           uint32_t *value);
bool ztm_stepper_set_param(uint8_t device_id,
                           uint8_t reg,
                           uint8_t nbytes,
                           uint32_t value);
bool ztm_stepper_reset(uint8_t device_id);
bool ztm_stepper_reset_all(void);
bool ztm_stepper_hard_hiz(uint8_t device_id);
bool ztm_stepper_hard_hiz_all(void);
bool ztm_stepper_soft_stop(uint8_t device_id);
bool ztm_stepper_soft_stop_all(void);
bool ztm_stepper_run(uint8_t device_id, bool dir_fwd, float steps_per_s);
bool ztm_stepper_run_all(bool dir_fwd, float steps_per_s);
bool ztm_stepper_move(uint8_t device_id, bool dir_fwd, uint32_t steps);

/*
 * Applies the verified module defaults: full-step mode, gentle
 * acceleration/deceleration, 500 step/s max, KVAL_HOLD=0 and
 * KVAL_RUN=25 and KVAL_ACC/KVAL_DEC=40.
 */
bool ztm_stepper_apply_test_config(uint8_t device_id);
bool ztm_stepper_apply_test_config_all(void);

/* Legacy names retained for the existing pumping/controller code. */
void ztm6470_stop(uint8_t device_id);
void ztm6470_stop_all(void);
void ztm6470_test(void);
void ztm6470_start_all(bool dir_fwd, float steps_per_s);
void ztm6470_start(uint8_t device_id, bool dir_fwd, float steps_per_s);
