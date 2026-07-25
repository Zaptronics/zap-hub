#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "drivers/l6470.h"
#include "ztmb/bus.h"

#define ZTM_STEPPER_DRIVER_COUNT        4u
#define ZTM_STEPPER_ACTIVE_DRIVER_COUNT 3u
#define ZTM_STEPPER_UNUSED_DRIVER_ID    4u

#define ZTM_STEPPER_DEFAULT_KVAL_HOLD 0u
#define ZTM_STEPPER_DEFAULT_KVAL_RUN  25u
#define ZTM_STEPPER_DEFAULT_KVAL_ACC  40u
#define ZTM_STEPPER_DEFAULT_KVAL_DEC  40u
typedef enum {
    ZTM_STEPPER_MICROSTEP_FULL = 0,
    ZTM_STEPPER_MICROSTEP_HALF = 1,
    ZTM_STEPPER_MICROSTEP_QUARTER = 2,
    ZTM_STEPPER_MICROSTEP_EIGHTH = 3,
    ZTM_STEPPER_MICROSTEP_SIXTEENTH = 4,
    ZTM_STEPPER_MICROSTEP_THIRTY_SECOND = 5,
    ZTM_STEPPER_MICROSTEP_SIXTY_FOURTH = 6,
    ZTM_STEPPER_MICROSTEP_ONE_TWENTY_EIGHTH = 7,
} ztm_stepper_microstep_t;

#define ZTM_STEPPER_DEFAULT_MICROSTEP ZTM_STEPPER_MICROSTEP_FULL

typedef struct {
    uint8_t hold;
    uint8_t run;
    uint8_t acc;
    uint8_t dec;
} ztm_stepper_kval_config_t;

typedef struct {
    bus_spi_t spi_bus;
    bus_spi_chain_t chain;
    bus_spi_chain_slot_t slots[ZTM_STEPPER_DRIVER_COUNT];
    bus_spi_target_t targets[ZTM_STEPPER_DRIVER_COUNT];
    l6470_t drivers[ZTM_STEPPER_DRIVER_COUNT];
    ztm_stepper_kval_config_t kval_config[ZTM_STEPPER_DRIVER_COUNT];
    ztm_stepper_microstep_t microstep_mode[ZTM_STEPPER_DRIVER_COUNT];
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
bool ztm_stepper_get_kval_config(uint8_t device_id,
                                 ztm_stepper_kval_config_t *config);
bool ztm_stepper_set_kval_config(uint8_t device_id,
                                 const ztm_stepper_kval_config_t *config);
bool ztm_stepper_set_kval_config_all(const ztm_stepper_kval_config_t *config);
bool ztm_stepper_get_microstep(uint8_t device_id,
                               ztm_stepper_microstep_t *mode);
bool ztm_stepper_set_microstep(uint8_t device_id,
                               ztm_stepper_microstep_t mode);
bool ztm_stepper_set_microstep_all(ztm_stepper_microstep_t mode);
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
 * Applies the verified module defaults: the stored runtime step mode,
 * gentle acceleration/deceleration, 500 step/s max, KVAL_HOLD=0 and
 * KVAL_RUN=25 and KVAL_ACC/KVAL_DEC=40. This also restores the
 * stored runtime KVAL and microstep configuration for the selected driver(s).
 */
bool ztm_stepper_apply_test_config(uint8_t device_id);
bool ztm_stepper_apply_test_config_all(void);

/* Legacy names retained for the existing pumping/controller code. */
void ztm6470_stop(uint8_t device_id);
void ztm6470_stop_all(void);
void ztm6470_test(void);
void ztm6470_start_all(bool dir_fwd, float steps_per_s);
void ztm6470_start(uint8_t device_id, bool dir_fwd, float steps_per_s);
