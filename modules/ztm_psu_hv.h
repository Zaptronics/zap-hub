#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "hardware/i2c.h"
#include "drivers/ad5282.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ZTM_PSU_HV_VSENSE_CHANNEL             1u
#define ZTM_PSU_HV_DIVIDER_RATIO_THOUSANDS    7500u
#define ZTM_PSU_HV_DEFAULT_PGOOD_TIMEOUT_MS    300u

/*
 * Hardware sweep on 2026-07-22 established the usable guarded range:
 *   code 255 -> 3.9225 V E60 drive (about 1.569 kV nominal)
 *   code  55 -> 13.6956 V E60 drive (about 5.478 kV nominal)
 *   code  45 -> 16.33 V and was rejected as E60 input overvoltage.
 * Lower RDAC2 codes request higher LM5164 output.
 */
#define ZTM_PSU_HV_LOWEST_SAFE_CODE                55u
#define ZTM_PSU_HV_HIGHEST_SAFE_CODE              255u
/* Compatibility name retained for older code and console text. */
#define ZTM_PSU_HV_LOWEST_TESTED_CODE ZTM_PSU_HV_LOWEST_SAFE_CODE

#define ZTM_PSU_HV_CAL_MIN_DRIVE_UV            3922500u
#define ZTM_PSU_HV_CAL_MAX_DRIVE_UV           13695600u
#define ZTM_PSU_HV_CAL_MIN_ESTIMATED_HV_V          1569u
#define ZTM_PSU_HV_CAL_MAX_ESTIMATED_HV_V          5479u

/* Stop an automated downward-code sweep before reaching the E60 15 V limit. */
#define ZTM_PSU_HV_SWEEP_STOP_INPUT_UV       14000000u

/* Maximum resistance is the safe idle setpoint; EN low remains authoritative. */
#define ZTM_PSU_HV_SAFE_RDAC_CODE                  255u

/* XP Power E60 nominal transfer: 0..15 V drive -> 0..6000 V output. */
#define ZTM_PSU_HV_E60_MIN_LINEAR_INPUT_UV      700000u
#define ZTM_PSU_HV_E60_MAX_INPUT_UV           15000000u
#define ZTM_PSU_HV_E60_FULL_SCALE_OUTPUT_MV    6000000u

typedef enum {
    ZTM_PSU_HV_INIT_NOT_STARTED = 0,
    ZTM_PSU_HV_INIT_SAFE_PINS,
    ZTM_PSU_HV_INIT_I2C,
    ZTM_PSU_HV_INIT_AD5282_PROBE,
    ZTM_PSU_HV_INIT_SAFE_RDACS,
    ZTM_PSU_HV_INIT_VERIFY_RDACS,
    ZTM_PSU_HV_INIT_STATUS,
    ZTM_PSU_HV_INIT_READY,
} ztm_psu_hv_init_stage_t;

typedef enum {
    ZTM_PSU_HV_OP_NONE = 0,
    ZTM_PSU_HV_OP_VALIDATE_CODE,
    ZTM_PSU_HV_OP_FORCE_OFF,
    ZTM_PSU_HV_OP_WRITE_RDAC1,
    ZTM_PSU_HV_OP_WRITE_RDAC2,
    ZTM_PSU_HV_OP_VERIFY_RDACS,
    ZTM_PSU_HV_OP_ENABLE,
    ZTM_PSU_HV_OP_WAIT_PGOOD,
    ZTM_PSU_HV_OP_READ_VSENSE,
    ZTM_PSU_HV_OP_VALIDATE_E60_INPUT,
    ZTM_PSU_HV_OP_COMPLETE,
} ztm_psu_hv_operation_stage_t;

typedef struct {
    bool enable_valid;
    bool enabled;
    bool pgood_valid;
    bool pgood;
    bool rdac_readback_valid;
    uint8_t rdac1_code;
    uint8_t rdac2_code;
    bool vsense_valid;
    uint16_t vsense_adc_code;
    uint32_t drive_uV;
    uint32_t estimated_hv_mV;
    bool e60_input_overvoltage;
} ztm_psu_hv_snapshot_t;

typedef struct {
    bool pins_configured;
    bool forced_off;
    bool ad5282_present;
    uint8_t i2c_address;
    bool rdac_readback_valid;
    uint8_t rdac1_code;
    uint8_t rdac2_code;
    bool enable_valid;
    bool enabled;
    bool pgood_valid;
    bool pgood;
    bool vsense_valid;
    uint16_t vsense_adc_code;
    uint32_t drive_uV;
    uint32_t estimated_hv_mV;
    bool e60_input_overvoltage;
} ztm_psu_hv_probe_result_t;

typedef struct {
    ad5282_t potentiometer;
    i2c_inst_t *i2c;
    int8_t enable_pin;
    int8_t pgood_pin;
    uint8_t address_straps;
    uint8_t i2c_address;

    bool ready;
    bool enabled;
    bool pgood;
    bool pgood_valid;
    uint8_t staged_code;
    uint8_t rdac1_code;
    uint8_t rdac2_code;
    bool rdac_readback_valid;
    uint16_t vsense_adc_code;
    uint32_t drive_uV;
    uint32_t estimated_hv_mV;
    bool vsense_valid;

    ztm_psu_hv_init_stage_t last_init_stage;
    int last_init_rc;
    ztm_psu_hv_operation_stage_t last_operation_stage;
    int last_operation_rc;

    uint32_t poll_count;
    uint32_t fault_events;
    uint32_t pgood_fault_events;
    uint32_t vsense_fault_events;
    uint32_t e60_overvoltage_fault_events;
    bool pgood_fault_active;
    bool vsense_fault_active;
    bool e60_overvoltage_fault_active;

    /* Last start attempt, captured while EN was still high. */
    uint8_t last_start_code;
    uint32_t last_start_wait_ms;
    bool last_start_pgood_read_valid;
    bool last_start_pgood;
    bool last_start_vsense_valid;
    uint16_t last_start_vsense_adc_code;
    uint32_t last_start_drive_uV;
    uint32_t last_start_peak_drive_uV;
} psu_hv_t;

extern psu_hv_t ztm_psu_hv_1;

int ztm_psu_hv_init(void *ctx, void *i2c, int8_t addr, int8_t en_pin);
int ztm_psu_hv_deinit(void);
int ztm_psu_hv_poll(void);

bool ztm_psu_hv_ready(void);
const char *ztm_psu_hv_init_stage_name(ztm_psu_hv_init_stage_t stage);
const char *ztm_psu_hv_operation_stage_name(ztm_psu_hv_operation_stage_t stage);

/* Safe operational API. */
bool ztm_psu_hv_stage_code(psu_hv_t *ctx, uint8_t voltage_code);
bool ztm_psu_hv_start(psu_hv_t *ctx,
                      uint8_t voltage_code,
                      uint32_t pgood_timeout_ms);
bool ztm_psu_hv_stop(psu_hv_t *ctx);
bool ztm_psu_hv_read_snapshot(psu_hv_t *ctx, ztm_psu_hv_snapshot_t *snapshot);
bool ztm_psu_hv_read_drive_voltage(psu_hv_t *ctx,
                                   uint16_t *adc_code,
                                   uint32_t *drive_uV);
uint32_t ztm_psu_hv_estimate_e60_output_mV(uint32_t drive_uV);
bool ztm_psu_hv_e60_input_within_rating(uint32_t drive_uV);

/* Bench calibration captured by the guarded 0.2.10 sweep. */
bool ztm_psu_hv_calibrated_drive_for_code(uint8_t code,
                                           uint32_t *drive_uV);
bool ztm_psu_hv_select_code_for_estimated_hv(uint32_t target_hv_V,
                                              uint8_t *code,
                                              uint32_t *calibrated_drive_uV,
                                              uint32_t *calibrated_hv_mV);

/* Deprecated compatibility name: this reads the E60 input drive, not HV output. */
bool ztm_psu_hv_read_vout(psu_hv_t *ctx,
                          uint16_t *adc_code,
                          uint32_t *drive_uV);
bool ztm_psu_hv_read_pgood(psu_hv_t *ctx, bool *pgood);
bool ztm_psu_hv_read_enable(psu_hv_t *ctx, bool *enabled);

/* Bench/diagnostic helpers. */
int ztm_psu_hv_probe(ztm_psu_hv_probe_result_t *result);
bool ztm_psu_hv_read_rdac(psu_hv_t *ctx,
                          ad5282_channel_t channel,
                          uint8_t *code);
bool ztm_psu_hv_write_rdac_off(psu_hv_t *ctx,
                               ad5282_channel_t channel,
                               uint8_t code);

/* Compatibility aliases retained for older subsystem code. */
bool ztm_psu_hv_set_voltage(psu_hv_t *ctx, uint8_t voltage_code);
bool ztm_psu_hv_enable(psu_hv_t *ctx, bool state);

#ifdef __cplusplus
}
#endif
