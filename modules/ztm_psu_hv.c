#include "modules/ztm_psu_hv.h"

#include <string.h>

#include "pico/stdlib.h"
#include "ztmb/adc.h"
#include "ztmb/io_exp.h"
#include "ztmb/log.h"

psu_hv_t ztm_psu_hv_1 = {
    .enable_pin = EX_HV_EN_1,
    .pgood_pin = EX_HV_PGOOD_1,
    .address_straps = 0u,
    .i2c_address = AD5282_ADDR(0u),
};

static const bus_i2c_config_t g_hv_i2c_config = {
    .baud_hz = 400000u,
    .timeout_us = 5000u,
};

typedef struct {
    uint8_t code;
    uint32_t drive_uV;
} ztm_psu_hv_cal_point_t;

/*
 * Measured on the assembled board by `hv sweep` in firmware 0.2.10.
 * Codes are descending because lower code requests higher voltage.
 */
static const ztm_psu_hv_cal_point_t g_hv_calibration[] = {
    {255u,  3922500u},
    {245u,  4043400u},
    {235u,  4170300u},
    {225u,  4321400u},
    {215u,  4430200u},
    {205u,  4611500u},
    {195u,  4804900u},
    {185u,  4950000u},
    {175u,  5167600u},
    {165u,  5457700u},
    {155u,  5705500u},
    {145u,  6001600u},
    {135u,  6370300u},
    {125u,  6805500u},
    {115u,  7264800u},
    {105u,  7857100u},
    { 95u,  8522000u},
    { 85u,  9368100u},
    { 75u, 10431900u},
    { 65u, 11822000u},
    { 55u, 13695600u},
};

#define HV_CAL_POINT_COUNT (sizeof(g_hv_calibration) / sizeof(g_hv_calibration[0]))

const char *ztm_psu_hv_init_stage_name(ztm_psu_hv_init_stage_t stage)
{
    switch (stage) {
        case ZTM_PSU_HV_INIT_SAFE_PINS: return "safe connector pins";
        case ZTM_PSU_HV_INIT_I2C: return "AD5282 I2C setup";
        case ZTM_PSU_HV_INIT_AD5282_PROBE: return "AD5282 probe";
        case ZTM_PSU_HV_INIT_SAFE_RDACS: return "set safe RDACs";
        case ZTM_PSU_HV_INIT_VERIFY_RDACS: return "verify RDACs";
        case ZTM_PSU_HV_INIT_STATUS: return "read safe-state status";
        case ZTM_PSU_HV_INIT_READY: return "ready";
        default: return "not started";
    }
}

const char *ztm_psu_hv_operation_stage_name(ztm_psu_hv_operation_stage_t stage)
{
    switch (stage) {
        case ZTM_PSU_HV_OP_VALIDATE_CODE: return "validate operating code";
        case ZTM_PSU_HV_OP_FORCE_OFF: return "force enable low";
        case ZTM_PSU_HV_OP_WRITE_RDAC1: return "write RDAC1";
        case ZTM_PSU_HV_OP_WRITE_RDAC2: return "write RDAC2";
        case ZTM_PSU_HV_OP_VERIFY_RDACS: return "verify RDAC readback";
        case ZTM_PSU_HV_OP_ENABLE: return "assert enable";
        case ZTM_PSU_HV_OP_WAIT_PGOOD: return "wait for power-good";
        case ZTM_PSU_HV_OP_READ_VSENSE: return "read E60 drive VSENSE";
        case ZTM_PSU_HV_OP_VALIDATE_E60_INPUT: return "validate E60 input";
        case ZTM_PSU_HV_OP_COMPLETE: return "complete";
        default: return "none";
    }
}

static int init_failed(psu_hv_t *device,
                       ztm_psu_hv_init_stage_t stage,
                       int rc)
{
    if (device) {
        device->ready = false;
        device->enabled = false;
        device->last_init_stage = stage;
        device->last_init_rc = rc;
        if (device->enable_pin >= 0) {
            (void)pcal6416a_write_pin(&ioexp, (uint8_t)device->enable_pin, false);
        }
    }
    log_printf("HV PSU init failed at %s, rc=%d\n",
               ztm_psu_hv_init_stage_name(stage),
               rc);
    return rc;
}

static bool set_operation_result(psu_hv_t *device,
                                 ztm_psu_hv_operation_stage_t stage,
                                 int rc)
{
    if (device) {
        device->last_operation_stage = stage;
        device->last_operation_rc = rc;
    }
    return rc == 0;
}

static bool configure_safe_pins(int8_t enable_pin, int8_t pgood_pin)
{
    if (enable_pin < 0 || pgood_pin < 0) {
        return false;
    }

    /* Program the output latch low before explicitly selecting output mode. */
    return pcal6416a_write_pin(&ioexp, (uint8_t)enable_pin, false) &&
           pcal6416a_set_pin_direction(&ioexp, (uint8_t)enable_pin, false) &&
           pcal6416a_set_pin_direction(&ioexp, (uint8_t)pgood_pin, true);
}

bool ztm_psu_hv_read_enable(psu_hv_t *ctx, bool *enabled)
{
    if (!ctx || !enabled || ctx->enable_pin < 0) {
        return false;
    }
    return pcal6416a_read_pin(&ioexp, (uint8_t)ctx->enable_pin, enabled);
}

bool ztm_psu_hv_read_pgood(psu_hv_t *ctx, bool *pgood)
{
    if (!ctx || !pgood || ctx->pgood_pin < 0) {
        return false;
    }
    return pcal6416a_read_pin(&ioexp, (uint8_t)ctx->pgood_pin, pgood);
}

uint32_t ztm_psu_hv_estimate_e60_output_mV(uint32_t drive_uV)
{
    /* Nominal full-load transfer for E60: 15 V in -> 6000 V out. */
    uint64_t numerator = (uint64_t)drive_uV *
                         (uint64_t)ZTM_PSU_HV_E60_FULL_SCALE_OUTPUT_MV;
    numerator += ZTM_PSU_HV_E60_MAX_INPUT_UV / 2u;
    return (uint32_t)(numerator / ZTM_PSU_HV_E60_MAX_INPUT_UV);
}

bool ztm_psu_hv_e60_input_within_rating(uint32_t drive_uV)
{
    return drive_uV <= ZTM_PSU_HV_E60_MAX_INPUT_UV;
}

bool ztm_psu_hv_calibrated_drive_for_code(uint8_t code,
                                           uint32_t *drive_uV)
{
    if (!drive_uV || code < ZTM_PSU_HV_LOWEST_SAFE_CODE) {
        return false;
    }

    if (code >= g_hv_calibration[0].code) {
        *drive_uV = g_hv_calibration[0].drive_uV;
        return true;
    }

    for (size_t i = 0u; i + 1u < HV_CAL_POINT_COUNT; ++i) {
        const ztm_psu_hv_cal_point_t *high_code = &g_hv_calibration[i];
        const ztm_psu_hv_cal_point_t *low_code = &g_hv_calibration[i + 1u];
        if (code <= high_code->code && code >= low_code->code) {
            uint32_t code_span = (uint32_t)high_code->code - (uint32_t)low_code->code;
            uint32_t code_offset = (uint32_t)high_code->code - (uint32_t)code;
            uint32_t voltage_span = low_code->drive_uV - high_code->drive_uV;
            uint64_t scaled = (uint64_t)voltage_span * code_offset;
            *drive_uV = high_code->drive_uV +
                        (uint32_t)((scaled + code_span / 2u) / code_span);
            return true;
        }
    }

    return false;
}

bool ztm_psu_hv_select_code_for_estimated_hv(uint32_t target_hv_V,
                                              uint8_t *code,
                                              uint32_t *calibrated_drive_uV,
                                              uint32_t *calibrated_hv_mV)
{
    if (!code || !calibrated_drive_uV || !calibrated_hv_mV ||
        target_hv_V < ZTM_PSU_HV_CAL_MIN_ESTIMATED_HV_V ||
        target_hv_V > ZTM_PSU_HV_CAL_MAX_ESTIMATED_HV_V) {
        return false;
    }

    /* 400 V of nominal E60 output per 1 V drive. */
    uint32_t target_drive_uV = target_hv_V * 2500u;
    uint8_t selected = ZTM_PSU_HV_HIGHEST_SAFE_CODE;
    uint32_t selected_drive = ZTM_PSU_HV_CAL_MIN_DRIVE_UV;

    /*
     * Walk toward higher output and retain the closest code whose calibrated
     * drive does not exceed the requested nominal target. This rounds toward
     * the safer, lower-output side of the calibration curve.
     */
    for (int candidate = (int)ZTM_PSU_HV_HIGHEST_SAFE_CODE;
         candidate >= (int)ZTM_PSU_HV_LOWEST_SAFE_CODE;
         --candidate) {
        uint32_t candidate_drive = 0u;
        if (!ztm_psu_hv_calibrated_drive_for_code((uint8_t)candidate,
                                                   &candidate_drive)) {
            continue;
        }
        if (candidate_drive > target_drive_uV) {
            break;
        }
        selected = (uint8_t)candidate;
        selected_drive = candidate_drive;
    }

    *code = selected;
    *calibrated_drive_uV = selected_drive;
    *calibrated_hv_mV = ztm_psu_hv_estimate_e60_output_mV(selected_drive);
    return true;
}

bool ztm_psu_hv_read_drive_voltage(psu_hv_t *ctx,
                                   uint16_t *adc_code,
                                   uint32_t *drive_uV)
{
    if (!ctx || !drive_uV) {
        return false;
    }

    uint16_t code = 0u;
    uint32_t measured_uV = 0u;
    bool ok = ztmb_adc_read_channel(ZTM_PSU_HV_VSENSE_CHANNEL,
                                    ZTM_PSU_HV_DIVIDER_RATIO_THOUSANDS,
                                    &code,
                                    &measured_uV);
    if (!ok) {
        ctx->vsense_valid = false;
        return false;
    }

    ctx->vsense_valid = true;
    ctx->vsense_adc_code = code;
    ctx->drive_uV = measured_uV;
    ctx->estimated_hv_mV = ztm_psu_hv_estimate_e60_output_mV(measured_uV);
    if (adc_code) {
        *adc_code = code;
    }
    *drive_uV = measured_uV;
    return true;
}

bool ztm_psu_hv_read_vout(psu_hv_t *ctx,
                          uint16_t *adc_code,
                          uint32_t *drive_uV)
{
    return ztm_psu_hv_read_drive_voltage(ctx, adc_code, drive_uV);
}

bool ztm_psu_hv_read_rdac(psu_hv_t *ctx,
                          ad5282_channel_t channel,
                          uint8_t *code)
{
    return ctx && ad5282_read_rdac(&ctx->potentiometer, channel, code);
}

static bool verify_rdacs(psu_hv_t *ctx,
                         uint8_t expected_rdac1,
                         uint8_t expected_rdac2)
{
    uint8_t rdac1 = 0u;
    uint8_t rdac2 = 0u;
    if (!ztm_psu_hv_read_rdac(ctx, AD5282_RDAC1, &rdac1) ||
        !ztm_psu_hv_read_rdac(ctx, AD5282_RDAC2, &rdac2)) {
        ctx->rdac_readback_valid = false;
        return false;
    }

    ctx->rdac1_code = rdac1;
    ctx->rdac2_code = rdac2;
    ctx->rdac_readback_valid = true;
    return rdac1 == expected_rdac1 && rdac2 == expected_rdac2;
}

bool ztm_psu_hv_enable(psu_hv_t *ctx, bool state)
{
    if (!ctx || ctx->enable_pin < 0) {
        return false;
    }

    bool ok = pcal6416a_write_pin(&ioexp, (uint8_t)ctx->enable_pin, state);
    if (ok) {
        ctx->enabled = state;
    }
    return ok;
}

bool ztm_psu_hv_stop(psu_hv_t *ctx)
{
    if (!ctx) {
        return false;
    }

    ctx->last_operation_stage = ZTM_PSU_HV_OP_FORCE_OFF;
    bool disabled = ztm_psu_hv_enable(ctx, false);
    if (!disabled) {
        return set_operation_result(ctx, ZTM_PSU_HV_OP_FORCE_OFF, -1);
    }

    /* Current live state is now OFF. Last-start diagnostics remain separate. */
    ctx->enabled = false;
    ctx->pgood = false;
    ctx->pgood_valid = false;
    ctx->vsense_valid = false;
    ctx->vsense_adc_code = 0u;
    ctx->drive_uV = 0u;
    ctx->estimated_hv_mV = 0u;

    /* Enable-low is authoritative; maximum-resistance RDACs are the secondary safe state. */
    if (!ctx->potentiometer.i2c_bus.i2c) {
        ctx->staged_code = ZTM_PSU_HV_SAFE_RDAC_CODE;
        ctx->rdac_readback_valid = false;
        return set_operation_result(ctx, ZTM_PSU_HV_OP_COMPLETE, 0);
    }

    ctx->last_operation_stage = ZTM_PSU_HV_OP_WRITE_RDAC1;
    bool rdac1_ok = ad5282_write_rdac(&ctx->potentiometer,
                                      AD5282_RDAC1,
                                      ZTM_PSU_HV_SAFE_RDAC_CODE,
                                      false,
                                      false);
    ctx->last_operation_stage = ZTM_PSU_HV_OP_WRITE_RDAC2;
    bool rdac2_ok = ad5282_write_rdac(&ctx->potentiometer,
                                      AD5282_RDAC2,
                                      ZTM_PSU_HV_SAFE_RDAC_CODE,
                                      false,
                                      false);

    ctx->staged_code = ZTM_PSU_HV_SAFE_RDAC_CODE;
    if (rdac1_ok && rdac2_ok) {
        ctx->last_operation_stage = ZTM_PSU_HV_OP_VERIFY_RDACS;
        if (!verify_rdacs(ctx,
                          ZTM_PSU_HV_SAFE_RDAC_CODE,
                          ZTM_PSU_HV_SAFE_RDAC_CODE)) {
            return set_operation_result(ctx, ZTM_PSU_HV_OP_VERIFY_RDACS, -4);
        }
        return set_operation_result(ctx, ZTM_PSU_HV_OP_COMPLETE, 0);
    }

    /* The hardware is still off even if the secondary I2C safe-state write failed. */
    ctx->rdac_readback_valid = false;
    return set_operation_result(ctx,
                                rdac1_ok ? ZTM_PSU_HV_OP_WRITE_RDAC2
                                         : ZTM_PSU_HV_OP_WRITE_RDAC1,
                                -3);
}

bool ztm_psu_hv_stage_code(psu_hv_t *ctx, uint8_t voltage_code)
{
    if (!ctx || !ctx->ready) {
        return false;
    }

    /* Never change the potentiometer while the converter is enabled. */
    ctx->last_operation_stage = ZTM_PSU_HV_OP_FORCE_OFF;
    if (!ztm_psu_hv_enable(ctx, false)) {
        return set_operation_result(ctx, ZTM_PSU_HV_OP_FORCE_OFF, -1);
    }

    ctx->last_operation_stage = ZTM_PSU_HV_OP_WRITE_RDAC1;
    if (!ad5282_write_rdac(&ctx->potentiometer,
                           AD5282_RDAC1,
                           0u,
                           false,
                           false)) {
        return set_operation_result(ctx, ZTM_PSU_HV_OP_WRITE_RDAC1, -2);
    }

    ctx->last_operation_stage = ZTM_PSU_HV_OP_WRITE_RDAC2;
    if (!ad5282_write_rdac(&ctx->potentiometer,
                           AD5282_RDAC2,
                           voltage_code,
                           false,
                           false)) {
        (void)ztm_psu_hv_stop(ctx);
        return set_operation_result(ctx, ZTM_PSU_HV_OP_WRITE_RDAC2, -3);
    }

    ctx->last_operation_stage = ZTM_PSU_HV_OP_VERIFY_RDACS;
    if (!verify_rdacs(ctx, 0u, voltage_code)) {
        (void)ztm_psu_hv_stop(ctx);
        return set_operation_result(ctx, ZTM_PSU_HV_OP_VERIFY_RDACS, -4);
    }

    ctx->staged_code = voltage_code;
    return set_operation_result(ctx, ZTM_PSU_HV_OP_COMPLETE, 0);
}

bool ztm_psu_hv_set_voltage(psu_hv_t *ctx, uint8_t voltage_code)
{
    return ztm_psu_hv_stage_code(ctx, voltage_code);
}

bool ztm_psu_hv_start(psu_hv_t *ctx,
                      uint8_t voltage_code,
                      uint32_t pgood_timeout_ms)
{
    if (!ctx || !ctx->ready) {
        return false;
    }

    ctx->last_start_code = voltage_code;
    ctx->last_start_wait_ms = 0u;
    ctx->last_start_pgood_read_valid = false;
    ctx->last_start_pgood = false;
    ctx->last_start_vsense_valid = false;
    ctx->last_start_vsense_adc_code = 0u;
    ctx->last_start_drive_uV = 0u;
    ctx->last_start_peak_drive_uV = 0u;

    /*
     * The feedback direction is inverse: lower RDAC2 codes request more LM5164
     * output. The measured sweep found code 55 at 13.6956 V and code 45 at
     * 16.33 V. Keep lower values available to 'hv set' while EN is low, but
     * do not energize the E60 below the characterised safe code of 55.
     */
    ctx->last_operation_stage = ZTM_PSU_HV_OP_VALIDATE_CODE;
    if (voltage_code < ZTM_PSU_HV_LOWEST_TESTED_CODE) {
        (void)ztm_psu_hv_stop(ctx);
        return set_operation_result(ctx, ZTM_PSU_HV_OP_VALIDATE_CODE, -9);
    }

    if (!ztm_psu_hv_stage_code(ctx, voltage_code)) {
        return false;
    }

    ctx->last_operation_stage = ZTM_PSU_HV_OP_ENABLE;
    if (!ztm_psu_hv_enable(ctx, true)) {
        (void)ztm_psu_hv_stop(ctx);
        return set_operation_result(ctx, ZTM_PSU_HV_OP_ENABLE, -5);
    }

    ctx->last_operation_stage = ZTM_PSU_HV_OP_WAIT_PGOOD;
    uint32_t timeout_ms = pgood_timeout_ms == 0u
                            ? ZTM_PSU_HV_DEFAULT_PGOOD_TIMEOUT_MS
                            : pgood_timeout_ms;
    bool pgood = false;
    bool read_ok = false;
    uint32_t elapsed = 0u;
    for (elapsed = 0u; elapsed <= timeout_ms; elapsed += 5u) {
        read_ok = ztm_psu_hv_read_pgood(ctx, &pgood);
        ctx->last_start_pgood_read_valid = read_ok;
        ctx->last_start_pgood = read_ok && pgood;

        /* Capture the live rail while EN is high, even when PGOOD never rises. */
        if ((elapsed % 10u) == 0u || (read_ok && pgood)) {
            uint16_t adc_code = 0u;
            uint32_t drive_uV = 0u;
            if (ztm_psu_hv_read_drive_voltage(ctx, &adc_code, &drive_uV)) {
                ctx->last_start_vsense_valid = true;
                ctx->last_start_vsense_adc_code = adc_code;
                ctx->last_start_drive_uV = drive_uV;
                if (drive_uV > ctx->last_start_peak_drive_uV) {
                    ctx->last_start_peak_drive_uV = drive_uV;
                }
            }
        }

        if (read_ok && pgood) {
            break;
        }
        if (elapsed < timeout_ms) {
            busy_wait_ms(5u);
        }
    }
    ctx->last_start_wait_ms = elapsed > timeout_ms ? timeout_ms : elapsed;
    ctx->pgood_valid = read_ok;
    ctx->pgood = read_ok && pgood;
    if (!read_ok || !pgood) {
        (void)ztm_psu_hv_stop(ctx);
        return set_operation_result(ctx, ZTM_PSU_HV_OP_WAIT_PGOOD, -6);
    }

    ctx->last_operation_stage = ZTM_PSU_HV_OP_READ_VSENSE;
    uint16_t adc_code = 0u;
    uint32_t drive_uV = 0u;
    if (!ztm_psu_hv_read_drive_voltage(ctx, &adc_code, &drive_uV)) {
        (void)ztm_psu_hv_stop(ctx);
        return set_operation_result(ctx, ZTM_PSU_HV_OP_READ_VSENSE, -7);
    }
    ctx->last_start_vsense_valid = true;
    ctx->last_start_vsense_adc_code = adc_code;
    ctx->last_start_drive_uV = drive_uV;
    if (drive_uV > ctx->last_start_peak_drive_uV) {
        ctx->last_start_peak_drive_uV = drive_uV;
    }

    ctx->last_operation_stage = ZTM_PSU_HV_OP_VALIDATE_E60_INPUT;
    if (!ztm_psu_hv_e60_input_within_rating(drive_uV)) {
        (void)ztm_psu_hv_stop(ctx);
        return set_operation_result(ctx, ZTM_PSU_HV_OP_VALIDATE_E60_INPUT, -8);
    }

    return set_operation_result(ctx, ZTM_PSU_HV_OP_COMPLETE, 0);
}

bool ztm_psu_hv_read_snapshot(psu_hv_t *ctx, ztm_psu_hv_snapshot_t *snapshot)
{
    if (!ctx || !snapshot) {
        return false;
    }

    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->enable_valid = ztm_psu_hv_read_enable(ctx, &snapshot->enabled);
    snapshot->pgood_valid = ztm_psu_hv_read_pgood(ctx, &snapshot->pgood);
    snapshot->rdac_readback_valid =
        ztm_psu_hv_read_rdac(ctx, AD5282_RDAC1, &snapshot->rdac1_code) &&
        ztm_psu_hv_read_rdac(ctx, AD5282_RDAC2, &snapshot->rdac2_code);
    snapshot->vsense_valid = ztm_psu_hv_read_drive_voltage(ctx,
                                                 &snapshot->vsense_adc_code,
                                                 &snapshot->drive_uV);
    if (snapshot->vsense_valid) {
        snapshot->estimated_hv_mV =
            ztm_psu_hv_estimate_e60_output_mV(snapshot->drive_uV);
        snapshot->e60_input_overvoltage =
            !ztm_psu_hv_e60_input_within_rating(snapshot->drive_uV);
    }

    if (snapshot->enable_valid) {
        ctx->enabled = snapshot->enabled;
    }
    if (snapshot->pgood_valid) {
        ctx->pgood_valid = true;
        ctx->pgood = snapshot->pgood;
    }
    if (snapshot->rdac_readback_valid) {
        ctx->rdac_readback_valid = true;
        ctx->rdac1_code = snapshot->rdac1_code;
        ctx->rdac2_code = snapshot->rdac2_code;
    }
    return snapshot->enable_valid && snapshot->pgood_valid &&
           snapshot->rdac_readback_valid && snapshot->vsense_valid;
}

bool ztm_psu_hv_write_rdac_off(psu_hv_t *ctx,
                               ad5282_channel_t channel,
                               uint8_t code)
{
    if (!ctx || !ctx->ready) {
        return false;
    }

    bool enabled = false;
    if (!ztm_psu_hv_read_enable(ctx, &enabled) || enabled) {
        return false;
    }
    if (!ad5282_write_rdac(&ctx->potentiometer, channel, code, false, false)) {
        return false;
    }

    uint8_t readback = 0u;
    if (!ztm_psu_hv_read_rdac(ctx, channel, &readback) || readback != code) {
        return false;
    }

    if (channel == AD5282_RDAC1) {
        ctx->rdac1_code = code;
    } else {
        ctx->rdac2_code = code;
        ctx->staged_code = code;
    }
    ctx->rdac_readback_valid = true;
    return true;
}

int ztm_psu_hv_probe(ztm_psu_hv_probe_result_t *result)
{
    if (!result) {
        return -1;
    }
    memset(result, 0, sizeof(*result));

    psu_hv_t *device = &ztm_psu_hv_1;
    i2c_inst_t *i2c = device->i2c ? device->i2c : ZTMB_I2C0_PORT;
    bool has_context = device->i2c != NULL;
    int8_t enable_pin = has_context ? device->enable_pin : EX_HV_EN_1;
    int8_t pgood_pin = has_context ? device->pgood_pin : EX_HV_PGOOD_1;
    uint8_t straps = device->address_straps & 0x03u;
    result->i2c_address = AD5282_ADDR(straps);

    result->pins_configured = configure_safe_pins(enable_pin, pgood_pin);
    if (!result->pins_configured) {
        return -2;
    }
    result->forced_off = true;

    ad5282_t probe = {0};
    if (!bus_i2c_init(&probe.i2c_bus,
                      i2c,
                      result->i2c_address,
                      EX_NONE,
                      &g_hv_i2c_config)) {
        return -3;
    }

    result->ad5282_present = ad5282_probe(&probe);
    if (result->ad5282_present) {
        result->rdac_readback_valid =
            ad5282_read_rdac(&probe, AD5282_RDAC1, &result->rdac1_code) &&
            ad5282_read_rdac(&probe, AD5282_RDAC2, &result->rdac2_code);
    }

    result->enable_valid = pcal6416a_read_pin(&ioexp,
                                              (uint8_t)enable_pin,
                                              &result->enabled);
    result->pgood_valid = pcal6416a_read_pin(&ioexp,
                                             (uint8_t)pgood_pin,
                                             &result->pgood);
    result->vsense_valid = ztmb_adc_read_channel(ZTM_PSU_HV_VSENSE_CHANNEL,
                                                 ZTM_PSU_HV_DIVIDER_RATIO_THOUSANDS,
                                                 &result->vsense_adc_code,
                                                 &result->drive_uV);
    if (result->vsense_valid) {
        result->estimated_hv_mV =
            ztm_psu_hv_estimate_e60_output_mV(result->drive_uV);
        result->e60_input_overvoltage =
            !ztm_psu_hv_e60_input_within_rating(result->drive_uV);
    }

    return result->ad5282_present ? 0 : -4;
}

int ztm_psu_hv_init(void *ctx, void *i2c, int8_t addr, int8_t en_pin)
{
    if (!ctx || !i2c || en_pin < 0) {
        return -1;
    }

    psu_hv_t *device = (psu_hv_t *)ctx;
    memset(device, 0, sizeof(*device));
    device->i2c = (i2c_inst_t *)i2c;
    device->enable_pin = en_pin;
    device->pgood_pin = EX_HV_PGOOD_1;
    device->address_straps = (uint8_t)addr & 0x03u;
    device->i2c_address = AD5282_ADDR(device->address_straps);
    device->last_init_stage = ZTM_PSU_HV_INIT_SAFE_PINS;

    if (!configure_safe_pins(device->enable_pin, device->pgood_pin)) {
        return init_failed(device, ZTM_PSU_HV_INIT_SAFE_PINS, -2);
    }

    device->last_init_stage = ZTM_PSU_HV_INIT_I2C;
    if (!bus_i2c_init(&device->potentiometer.i2c_bus,
                      device->i2c,
                      device->i2c_address,
                      EX_NONE,
                      &g_hv_i2c_config)) {
        return init_failed(device, ZTM_PSU_HV_INIT_I2C, -3);
    }

    device->last_init_stage = ZTM_PSU_HV_INIT_AD5282_PROBE;
    if (!ad5282_init(&device->potentiometer)) {
        return init_failed(device, ZTM_PSU_HV_INIT_AD5282_PROBE, -4);
    }

    device->last_init_stage = ZTM_PSU_HV_INIT_SAFE_RDACS;
    if (!ad5282_write_rdac(&device->potentiometer,
                           AD5282_RDAC1,
                           ZTM_PSU_HV_SAFE_RDAC_CODE,
                           false,
                           false) ||
        !ad5282_write_rdac(&device->potentiometer,
                           AD5282_RDAC2,
                           ZTM_PSU_HV_SAFE_RDAC_CODE,
                           false,
                           false)) {
        return init_failed(device, ZTM_PSU_HV_INIT_SAFE_RDACS, -5);
    }

    device->last_init_stage = ZTM_PSU_HV_INIT_VERIFY_RDACS;
    if (!verify_rdacs(device,
                      ZTM_PSU_HV_SAFE_RDAC_CODE,
                      ZTM_PSU_HV_SAFE_RDAC_CODE)) {
        return init_failed(device, ZTM_PSU_HV_INIT_VERIFY_RDACS, -6);
    }

    device->last_init_stage = ZTM_PSU_HV_INIT_STATUS;
    bool enabled = false;
    bool pgood = false;
    uint32_t drive_uV = 0u;
    if (!ztm_psu_hv_read_enable(device, &enabled) || enabled ||
        !ztm_psu_hv_read_pgood(device, &pgood) ||
        !ztm_psu_hv_read_drive_voltage(device, NULL, &drive_uV)) {
        return init_failed(device, ZTM_PSU_HV_INIT_STATUS, -7);
    }

    device->enabled = false;
    device->pgood_valid = true;
    device->pgood = pgood;
    device->staged_code = ZTM_PSU_HV_SAFE_RDAC_CODE;
    device->ready = true;
    device->last_init_stage = ZTM_PSU_HV_INIT_READY;
    device->last_init_rc = 0;
    device->last_operation_stage = ZTM_PSU_HV_OP_COMPLETE;
    device->last_operation_rc = 0;

    log_printf("HV PSU ready and OFF: AD5282=0x%02X, RDACs=255/255, LM5164 EN=P1.6 low, PG=P1.5, E60 drive VSENSE=AIN1\n",
               device->i2c_address);
    return 0;
}

int ztm_psu_hv_deinit(void)
{
    psu_hv_t *device = &ztm_psu_hv_1;
    bool ok = true;
    if (device->enable_pin >= 0) {
        ok = ztm_psu_hv_enable(device, false);
    }
    if (device->potentiometer.i2c_bus.i2c) {
        ok = ad5282_write_rdac(&device->potentiometer,
                               AD5282_RDAC1,
                               ZTM_PSU_HV_SAFE_RDAC_CODE,
                               false,
                               false) && ok;
        ok = ad5282_write_rdac(&device->potentiometer,
                               AD5282_RDAC2,
                               ZTM_PSU_HV_SAFE_RDAC_CODE,
                               false,
                               false) && ok;
    }
    device->ready = false;
    device->enabled = false;
    return ok ? 0 : -1;
}

bool ztm_psu_hv_ready(void)
{
    return ztm_psu_hv_1.ready;
}

int ztm_psu_hv_poll(void)
{
    psu_hv_t *device = &ztm_psu_hv_1;
    if (!device->ready) {
        return 0;
    }

    device->poll_count++;
    bool enabled = false;
    bool pgood = false;
    bool enable_ok = ztm_psu_hv_read_enable(device, &enabled);
    bool pgood_ok = ztm_psu_hv_read_pgood(device, &pgood);
    device->enabled = enable_ok && enabled;
    device->pgood_valid = pgood_ok;
    device->pgood = pgood_ok && pgood;

    if (device->enabled && (!pgood_ok || !pgood)) {
        if (!device->pgood_fault_active) {
            device->pgood_fault_active = true;
            device->fault_events++;
            device->pgood_fault_events++;
            log_printf("ERROR: LM5164 Power Good lost; E60 drive disabled\n");
        }
        (void)ztm_psu_hv_stop(device);
    } else {
        device->pgood_fault_active = false;
    }

    if (device->enabled) {
        uint32_t drive_uV = 0u;
        bool vsense_ok = ztm_psu_hv_read_drive_voltage(device, NULL, &drive_uV);
        if (!vsense_ok) {
            if (!device->vsense_fault_active) {
                device->vsense_fault_active = true;
                device->fault_events++;
                device->vsense_fault_events++;
                log_printf("ERROR: E60 drive VSENSE read failed; output disabled\n");
            }
            (void)ztm_psu_hv_stop(device);
        } else {
            device->vsense_fault_active = false;
            if (!ztm_psu_hv_e60_input_within_rating(drive_uV)) {
                if (!device->e60_overvoltage_fault_active) {
                    device->e60_overvoltage_fault_active = true;
                    device->fault_events++;
                    device->e60_overvoltage_fault_events++;
                    log_printf("ERROR: E60 input exceeds 15 V; output disabled\n");
                }
                (void)ztm_psu_hv_stop(device);
            } else {
                device->e60_overvoltage_fault_active = false;
            }
        }
    } else {
        device->vsense_fault_active = false;
        device->e60_overvoltage_fault_active = false;
    }

    return 0;
}
