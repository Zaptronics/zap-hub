#pragma once

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "ztmb/cli_cmds.h"
#include "ztmb/modules.h"

#include "modules/ztm_stepper.h"
#include "cooling.h"
#include "pumping.h"
#include "modules/ztm_pwm.h"
#include "modules/ztm_psu_hv.h"
#include "modules/ztm_psu_tec.h"

/* =========================================================
   Console output helpers
   ========================================================= */

static void console_printf(const char *fmt, ...)
{
    char buffer[256];
    va_list args;
    va_start(args, fmt);
    (void)vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    wifi_write(buffer);
}

static void help_group(const cli_group_t *grp)
{
    console_printf("\r\nGroup: %s - %s\r\nSubcommands:\r\n",
                   grp->name,
                   grp->help);
    for (size_t i = 0; i < grp->cmd_count; i++) {
        console_printf("  %s %s - %s\r\n",
                       grp->name,
                       grp->cmds[i].name,
                       grp->cmds[i].help);
    }
}

static void help_subcmd(const cli_group_t *grp, const cli_cmd_t *sub)
{
    console_printf("\r\n%s %s - %s\r\n",
                   grp->name,
                   sub->name,
                   sub->help);
}

static bool parse_stepper_id(const char *text, uint8_t *device_id)
{
    uint32_t value;
    if (!parse_u32(text, &value) || value < 1 ||
        value > ZTM_STEPPER_DRIVER_COUNT || !device_id) {
        return false;
    }
    *device_id = (uint8_t)value;
    return true;
}

static bool parse_direction(const char *text, bool *forward)
{
    if (!text || !forward) {
        return false;
    }
    if (strcmp(text, "fwd") == 0 || strcmp(text, "forward") == 0) {
        *forward = true;
        return true;
    }
    if (strcmp(text, "rev") == 0 || strcmp(text, "reverse") == 0) {
        *forward = false;
        return true;
    }
    return false;
}

static bool selector_is_all(const char *text)
{
    return text && strcmp(text, "all") == 0;
}

/* =========================================================
   Module diagnostics
   ========================================================= */

static int cmd_module_list(int argc, char **argv, char *out, size_t out_len)
{
    (void)argc;
    (void)argv;
    (void)out;
    (void)out_len;

    console_printf("\r\nModule initialization state:\r\n");
    for (size_t i = 0; i < modules_count(); i++) {
        const module_entry_t *module = modules_get(i);
        if (!module) {
            continue;
        }
        console_printf("  %-12s %-6s  %s\r\n",
                       module->short_name ? module->short_name : "?",
                       modules_is_initialized(i) ? "READY" : "FAILED",
                       module->long_name ? module->long_name : "");
    }
    return 0;
}

/* =========================================================
   Stepper/L6470 diagnostics and bench controls
   ========================================================= */

static int cmd_stepper_info(int argc, char **argv, char *out, size_t out_len)
{
    (void)argc;
    (void)argv;
    (void)out;
    (void)out_len;

    const ztm_stepper_t *module = &ztm_stepper_1;
    console_printf("\r\nStepper module: %s\r\n", ztm6470_ready() ? "READY" : "NOT READY");
    console_printf("  SPI: %u Hz, %u-bit, mode %u, CS line %u\r\n",
                   (unsigned)module->spi_bus.config.baud_hz,
                   (unsigned)module->spi_bus.config.data_bits,
                   (unsigned)((module->spi_bus.config.cpol ? 2u : 0u) |
                              (module->spi_bus.config.cpha ? 1u : 0u)),
                   (unsigned)module->spi_bus.cs_mux_id);
    console_printf("  Chain: %u slots, %u bytes/frame, finalized=%s\r\n",
                   (unsigned)module->chain.slot_count,
                   (unsigned)module->chain.total_frame_bytes,
                   module->chain.finalized ? "yes" : "no");
    console_printf("  Active motors: 1..%u; motor %u is unused and held in hard HiZ\r\n",
                   (unsigned)ZTM_STEPPER_ACTIVE_DRIVER_COUNT,
                   (unsigned)ZTM_STEPPER_UNUSED_DRIVER_ID);

    for (size_t i = 0; i < module->chain.slot_count; i++) {
        const bus_spi_chain_slot_t *slot = module->chain.slots[i];
        if (slot) {
            console_printf("    motor %u: logical=%u wire_offset=%u frame=%u byte(s)\r\n",
                           (unsigned)(i + 1u),
                           (unsigned)slot->logical_index,
                           (unsigned)slot->wire_offset,
                           (unsigned)slot->config.frame_bytes);
        }
    }
    return 0;
}

static int cmd_stepper_nop(int argc, char **argv, char *out, size_t out_len)
{
    (void)out;
    (void)out_len;

    uint32_t count = 1;
    if (argc >= 3 && (!parse_u32(argv[2], &count) || count < 1 || count > 100)) {
        console_printf("\r\nUsage: stepper nop [count 1..100]\r\n");
        return -1;
    }

    for (uint32_t pass = 0; pass < count; pass++) {
        uint8_t responses[ZTM_STEPPER_DRIVER_COUNT];
        if (!ztm_stepper_nop(responses)) {
            console_printf("\r\nNOP frame failed on pass %u.\r\n", (unsigned)(pass + 1u));
            return -2;
        }
        console_printf("\r\nNOP %u RX logical motors 1..4: %02X %02X %02X %02X",
                       (unsigned)(pass + 1u),
                       responses[0], responses[1], responses[2], responses[3]);
    }
    console_printf("\r\n");
    return 0;
}

static int cmd_stepper_status(int argc, char **argv, char *out, size_t out_len)
{
    (void)out;
    (void)out_len;

    if (argc == 2 || selector_is_all(argv[2])) {
        console_printf("\r\nL6470 GET_STATUS results (reading clears latched status flags):\r\n");
        bool all_ok = true;
        for (uint8_t id = 1; id <= ZTM_STEPPER_DRIVER_COUNT; id++) {
            uint16_t status = 0;
            bool ok = ztm_stepper_get_status(id, &status);
            console_printf("  motor %u: %s0x%04X\r\n",
                           id,
                           ok ? "" : "ERROR/",
                           status);
            all_ok = all_ok && ok;
        }
        return all_ok ? 0 : -1;
    }

    uint8_t id;
    if (!parse_stepper_id(argv[2], &id)) {
        console_printf("\r\nUsage: stepper status [1..4|all]\r\n");
        return -2;
    }

    uint16_t status = 0;
    if (!ztm_stepper_get_status(id, &status)) {
        console_printf("\r\nFailed to read motor %u status.\r\n", id);
        return -3;
    }
    console_printf("\r\nMotor %u status: 0x%04X (GET_STATUS clears latched flags)\r\n",
                   id,
                   status);
    return 0;
}

static int cmd_stepper_reset(int argc, char **argv, char *out, size_t out_len)
{
    (void)out;
    (void)out_len;

    if (argc < 3) {
        console_printf("\r\nUsage: stepper reset <1..4|all>\r\n");
        return -1;
    }

    bool ok;
    if (selector_is_all(argv[2])) {
        ok = ztm_stepper_reset_all();
    } else {
        uint8_t id;
        if (!parse_stepper_id(argv[2], &id)) {
            console_printf("\r\nInvalid motor selector. Use 1..4 or all.\r\n");
            return -2;
        }
        ok = ztm_stepper_reset(id);
    }

    console_printf("\r\n%s\r\n", ok ? "OK" : "Stepper reset failed");
    return ok ? 0 : -3;
}

static int cmd_stepper_hiz(int argc, char **argv, char *out, size_t out_len)
{
    (void)out;
    (void)out_len;

    const char *selector = argc >= 3 ? argv[2] : "all";
    bool ok;
    if (selector_is_all(selector)) {
        ok = ztm_stepper_hard_hiz_all();
    } else {
        uint8_t id;
        if (!parse_stepper_id(selector, &id)) {
            console_printf("\r\nUsage: stepper hiz [1..4|all]\r\n");
            return -1;
        }
        ok = ztm_stepper_hard_hiz(id);
    }

    console_printf("\r\n%s\r\n", ok ? "Outputs placed in high impedance" : "HiZ command failed");
    return ok ? 0 : -2;
}

static int cmd_stepper_stop(int argc, char **argv, char *out, size_t out_len)
{
    (void)out;
    (void)out_len;

    const char *selector = argc >= 3 ? argv[2] : "all";
    bool ok;
    if (selector_is_all(selector)) {
        ok = ztm_stepper_soft_stop_all();
    } else {
        uint8_t id;
        if (!parse_stepper_id(selector, &id)) {
            console_printf("\r\nUsage: stepper stop [1..4|all]\r\n");
            return -1;
        }
        ok = ztm_stepper_soft_stop(id);
    }

    console_printf("\r\n%s\r\n", ok ? "OK" : "Soft-stop command failed");
    return ok ? 0 : -2;
}

static int cmd_stepper_config(int argc, char **argv, char *out, size_t out_len)
{
    (void)out;
    (void)out_len;

    if (argc < 3) {
        console_printf("\r\nUsage: stepper config <1..4|all>\r\n");
        return -1;
    }

    bool ok;
    if (selector_is_all(argv[2])) {
        ok = ztm_stepper_apply_test_config_all();
    } else {
        uint8_t id;
        if (!parse_stepper_id(argv[2], &id)) {
            console_printf("\r\nInvalid motor selector. Use 1..4 or all.\r\n");
            return -2;
        }
        ok = ztm_stepper_apply_test_config(id);
    }

    console_printf("\r\n%s\r\n",
                   ok ? "Verified configuration applied: HOLD=0, RUN=25, ACC/DEC=40"
                      : "Stepper configuration failed");
    return ok ? 0 : -3;
}

static int cmd_stepper_run(int argc, char **argv, char *out, size_t out_len)
{
    (void)out;
    (void)out_len;

    if (argc < 5) {
        console_printf("\r\nUsage: stepper run <1..3|all> <fwd|rev> <steps/s>\r\n");
        return -1;
    }

    bool forward;
    float speed;
    if (!parse_direction(argv[3], &forward) ||
        !parse_float(argv[4], &speed) || speed < 0.0f || speed > 5000.0f) {
        console_printf("\r\nDirection must be fwd/rev and speed must be 0..5000 steps/s.\r\n");
        return -2;
    }

    bool ok;
    if (selector_is_all(argv[2])) {
        ok = ztm_stepper_run_all(forward, speed);
    } else {
        uint8_t id;
        if (!parse_stepper_id(argv[2], &id)) {
            console_printf("\r\nInvalid motor selector. Use 1..4 or all.\r\n");
            return -3;
        }
        if (id == ZTM_STEPPER_UNUSED_DRIVER_ID) {
            console_printf("\r\nMotor %u is unused and is intentionally held in hard HiZ.\r\n", id);
            return -4;
        }
        ok = ztm_stepper_run(id, forward, speed);
    }

    console_printf("\r\n%s\r\n", ok ? "OK" : "RUN command failed");
    return ok ? 0 : -4;
}

static int cmd_stepper_move(int argc, char **argv, char *out, size_t out_len)
{
    (void)out;
    (void)out_len;

    if (argc < 5) {
        console_printf("\r\nUsage: stepper move <1..3> <fwd|rev> <steps>\r\n");
        return -1;
    }

    uint8_t id;
    bool forward;
    uint32_t steps;
    if (!parse_stepper_id(argv[2], &id) ||
        !parse_direction(argv[3], &forward) ||
        !parse_u32(argv[4], &steps) || steps > 0x3FFFFFu) {
        console_printf("\r\nMotor must be 1..3, direction fwd/rev, steps 0..0x3FFFFF.\r\n");
        return -2;
    }

    if (id == ZTM_STEPPER_UNUSED_DRIVER_ID) {
        console_printf("\r\nMotor %u is unused and is intentionally held in hard HiZ.\r\n", id);
        return -3;
    }

    bool ok = ztm_stepper_move(id, forward, steps);
    console_printf("\r\n%s\r\n", ok ? "OK" : "MOVE command failed");
    return ok ? 0 : -3;
}

static int cmd_stepper_get(int argc, char **argv, char *out, size_t out_len)
{
    (void)out;
    (void)out_len;

    if (argc < 5) {
        console_printf("\r\nUsage: stepper get <1..4> <register> <bytes>\r\n");
        return -1;
    }

    uint8_t id;
    uint32_t reg;
    uint32_t nbytes;
    if (!parse_stepper_id(argv[2], &id) ||
        !parse_u32(argv[3], &reg) || reg > 0x1Fu ||
        !parse_u32(argv[4], &nbytes) || nbytes < 1 || nbytes > 4) {
        console_printf("\r\nRegister must be 0..0x1F and byte count 1..4. Hex is accepted.\r\n");
        return -2;
    }

    uint32_t value = 0;
    if (!ztm_stepper_get_param(id, (uint8_t)reg, (uint8_t)nbytes, &value)) {
        console_printf("\r\nRegister read failed.\r\n");
        return -3;
    }

    console_printf("\r\nMotor %u register 0x%02X = 0x%0*X (%u)\r\n",
                   id,
                   (unsigned)reg,
                   (int)(nbytes * 2u),
                   (unsigned)value,
                   (unsigned)value);
    return 0;
}

static int cmd_stepper_set(int argc, char **argv, char *out, size_t out_len)
{
    (void)out;
    (void)out_len;

    if (argc < 6) {
        console_printf("\r\nUsage: stepper set <1..4> <register> <bytes> <value>\r\n");
        return -1;
    }

    uint8_t id;
    uint32_t reg;
    uint32_t nbytes;
    uint32_t value;
    if (!parse_stepper_id(argv[2], &id) ||
        !parse_u32(argv[3], &reg) || reg > 0x1Fu ||
        !parse_u32(argv[4], &nbytes) || nbytes < 1 || nbytes > 4 ||
        !parse_u32(argv[5], &value)) {
        console_printf("\r\nRegister 0..0x1F, byte count 1..4, numeric value required.\r\n");
        return -2;
    }

    if (nbytes < 4u && value >= (1u << (nbytes * 8u))) {
        console_printf("\r\nValue does not fit in %u byte(s).\r\n", (unsigned)nbytes);
        return -3;
    }

    bool ok = ztm_stepper_set_param(id,
                                    (uint8_t)reg,
                                    (uint8_t)nbytes,
                                    value);
    console_printf("\r\n%s\r\n", ok ? "OK" : "Register write failed");
    return ok ? 0 : -4;
}

/* =========================================================
   Existing pump / TEC / PWM controls
   ========================================================= */

static const char *pump_state_name(pump_state_t state)
{
    switch (state) {
        case PUMP_FILTER_NORMAL: return "normal";
        case PUMP_FILTER_FAST:   return "fast";
        case PUMP_WICK_PRIME:    return "prime";
        case PUMP_WICK_DOSE:     return "dose";
        case PUMP_WICK_DRAIN:    return "drain";
        case PUMP_SUMP_ON:       return "running";
        case PUMP_OFF:
        default:                 return "off";
    }
}

static const char *pump_wick_direction_name(pump_state_t state)
{
    if (state == PUMP_WICK_DRAIN) {
        return "rev";
    }
    if (state == PUMP_WICK_PRIME || state == PUMP_WICK_DOSE) {
        return "fwd";
    }
    return "---";
}

static int pump_command_result(bool ok, const char *success)
{
    console_printf("\r\n%s\r\n", ok ? success : "Pump command failed");
    return ok ? 0 : -1;
}

static int cmd_pump_status(int argc, char **argv, char *out, size_t out_len)
{
    (void)argc;
    (void)argv;
    (void)out;
    (void)out_len;

    pumping_status_t status;
    pumping_get_status(&status);
    console_printf("\r\nPumping subsystem: %s\r\n", pumping_ready() ? "READY" : "NOT READY");
    console_printf("  IPA filter  motor 1: %-7s fwd %u steps/s\r\n",
                   pump_state_name(status.filter_state), (unsigned)status.filter_speed);
    console_printf("  IPA sump    motor 2: %-7s fwd %u steps/s\r\n",
                   pump_state_name(status.sump_state), (unsigned)status.sump_speed);
    console_printf("  IPA wick    motor 3: %-7s %s %u steps/s, %u ms remaining\r\n",
                   pump_state_name(status.wick_state),
                   pump_wick_direction_name(status.wick_state),
                   (unsigned)status.wick_speed,
                   (unsigned)status.wick_remaining_ms);
    console_printf("  Unused      motor 4: hard HiZ\r\n");
    return pumping_ready() ? 0 : -1;
}

static int cmd_pump_filter_normal(int argc, char **argv, char *out, size_t out_len)
{
    (void)argc; (void)argv; (void)out; (void)out_len;
    return pump_command_result(pumping_filter_start_normal(),
                               "IPA filter normal: motor 1 fwd 50 steps/s");
}

static int cmd_pump_filter_fast(int argc, char **argv, char *out, size_t out_len)
{
    (void)argc; (void)argv; (void)out; (void)out_len;
    return pump_command_result(pumping_filter_start_fast(),
                               "IPA filter fast: motor 1 fwd 100 steps/s");
}

static int cmd_pump_filter_stop(int argc, char **argv, char *out, size_t out_len)
{
    (void)argc; (void)argv; (void)out; (void)out_len;
    return pump_command_result(pumping_filter_stop(), "IPA filter stopped");
}

static int cmd_pump_wick_prime(int argc, char **argv, char *out, size_t out_len)
{
    (void)argc; (void)argv; (void)out; (void)out_len;
    return pump_command_result(pumping_wick_prime(),
                               "IPA wick prime: motor 3 fwd 50 steps/s for 5000 ms");
}

static int cmd_pump_wick_dose(int argc, char **argv, char *out, size_t out_len)
{
    (void)argc; (void)argv; (void)out; (void)out_len;
    return pump_command_result(pumping_wick_dose(),
                               "IPA wick dose: motor 3 fwd 50 steps/s for 2000 ms");
}

static int cmd_pump_wick_drain(int argc, char **argv, char *out, size_t out_len)
{
    (void)argc; (void)argv; (void)out; (void)out_len;
    return pump_command_result(pumping_wick_drain(),
                               "IPA wick drain: motor 3 rev 50 steps/s for 5000 ms");
}

static int cmd_pump_wick_stop(int argc, char **argv, char *out, size_t out_len)
{
    (void)argc; (void)argv; (void)out; (void)out_len;
    return pump_command_result(pumping_wick_stop(), "IPA wick stopped");
}

static int cmd_pump_sump_run(int argc, char **argv, char *out, size_t out_len)
{
    (void)argc; (void)argv; (void)out; (void)out_len;
    return pump_command_result(pumping_sump_run(),
                               "IPA sump running: motor 2 fwd 50 steps/s");
}

static int cmd_pump_sump_stop(int argc, char **argv, char *out, size_t out_len)
{
    (void)argc; (void)argv; (void)out; (void)out_len;
    return pump_command_result(pumping_sump_stop(), "IPA sump stopped");
}

static int cmd_pump_stop_all(int argc, char **argv, char *out, size_t out_len)
{
    (void)argc; (void)argv; (void)out; (void)out_len;
    return pump_command_result(pumping_all_stop(), "All IPA pumps stopped; motor 4 remains HiZ");
}

/* Compatibility aliases retained for the existing /api/pump/start[/id] UI. */
static int cmd_pump_start(int argc, char **argv, char *out, size_t out_len)
{
    (void)out;
    (void)out_len;

    if (argc == 2) {
        return cmd_pump_filter_normal(argc, argv, out, out_len);
    }

    uint8_t id;
    if (!parse_stepper_id(argv[2], &id)) {
        console_printf("\r\nUsage: pump start [1..3]\r\n");
        return -2;
    }

    switch (id) {
        case 1: return cmd_pump_filter_normal(argc, argv, out, out_len);
        case 2: return cmd_pump_sump_run(argc, argv, out, out_len);
        case 3: return cmd_pump_wick_dose(argc, argv, out, out_len);
        default:
            console_printf("\r\nMotor 4 is unused and remains in hard HiZ.\r\n");
            return -3;
    }
}

static int cmd_pump_stop(int argc, char **argv, char *out, size_t out_len)
{
    (void)out;
    (void)out_len;

    if (argc == 2) {
        return cmd_pump_stop_all(argc, argv, out, out_len);
    }

    uint8_t id;
    if (!parse_stepper_id(argv[2], &id)) {
        console_printf("\r\nUsage: pump stop [1..3]\r\n");
        return -2;
    }

    switch (id) {
        case 1: return cmd_pump_filter_stop(argc, argv, out, out_len);
        case 2: return cmd_pump_sump_stop(argc, argv, out, out_len);
        case 3: return cmd_pump_wick_stop(argc, argv, out, out_len);
        default:
            return pump_command_result(ztm_stepper_hard_hiz(ZTM_STEPPER_UNUSED_DRIVER_ID),
                                       "Motor 4 remains in hard HiZ");
    }
}

static bool parse_tec_id(const char *text, uint8_t *channel_id)
{
    uint32_t value;
    if (!channel_id || !parse_u32(text, &value) ||
        value < 1u || value > COOLING_TEC_COUNT) {
        return false;
    }
    *channel_id = (uint8_t)value;
    return true;
}

static void console_print_tec_status(uint8_t channel_id,
                                     const cooling_tec_status_t *status)
{
    if (!status) {
        return;
    }

    if (!status->reading_valid) {
        console_printf("  TEC %u: DAC=%u, %s, VSENSE read FAILED\r\n",
                       (unsigned)channel_id,
                       (unsigned)status->dac_code,
                       status->enabled ? "enabled" : "stopped");
        return;
    }

    uint32_t volts = status->output_uV / 1000000u;
    uint32_t millivolts = (status->output_uV % 1000000u + 500u) / 1000u;
    if (millivolts >= 1000u) {
        volts++;
        millivolts -= 1000u;
    }

    console_printf("  TEC %u: DAC=%u, %s, ADC=%u, output=%lu.%03lu V\r\n",
                   (unsigned)channel_id,
                   (unsigned)status->dac_code,
                   status->enabled ? "enabled" : "stopped",
                   (unsigned)status->adc_code,
                   (unsigned long)volts,
                   (unsigned long)millivolts);
}

static int cmd_tec_set(int argc, char **argv, char *out, size_t out_len)
{
    (void)out;
    (void)out_len;

    uint8_t channel_id;
    uint32_t value;
    if (argc < 4 || !parse_tec_id(argv[2], &channel_id) ||
        !parse_u32(argv[3], &value) || value > UINT8_MAX) {
        console_printf("\r\nUsage: tec set <1..4> <0..255>\r\n");
        return -1;
    }

    cooling_tec_status_t status;
    if (!cooling_tec_set_raw(channel_id, (uint8_t)value, &status)) {
        console_printf("\r\nTEC %u set/readback failed; channel was stopped.\r\n",
                       (unsigned)channel_id);
        return -2;
    }

    console_printf("\r\nTEC command accepted and VSENSE verified:\r\n");
    console_print_tec_status(channel_id, &status);
    return 0;
}

static int cmd_tec_set_all(int argc, char **argv, char *out, size_t out_len)
{
    (void)out;
    (void)out_len;

    uint32_t value;
    if (argc < 3 || !parse_u32(argv[2], &value) || value > UINT8_MAX) {
        console_printf("\r\nUsage: tec set-all <0..255>\r\n");
        return -1;
    }

    cooling_tec_status_t status[COOLING_TEC_COUNT];
    if (!cooling_tec_set_all_raw((uint8_t)value, status)) {
        console_printf("\r\nTEC set-all/readback failed; all TEC channels were stopped.\r\n");
        return -2;
    }

    console_printf("\r\nAll TEC DACs set to %u; VSENSE readback:\r\n", (unsigned)value);
    for (uint8_t id = 1u; id <= COOLING_TEC_COUNT; id++) {
        console_print_tec_status(id, &status[id - 1u]);
    }
    return 0;
}

static int cmd_tec_read(int argc, char **argv, char *out, size_t out_len)
{
    (void)out;
    (void)out_len;

    if (argc < 3) {
        console_printf("\r\nUsage: tec read <1..4|all>\r\n");
        return -1;
    }

    if (selector_is_all(argv[2])) {
        cooling_tec_status_t status[COOLING_TEC_COUNT];
        bool ok = cooling_tec_read_all(status);
        console_printf("\r\nTEC VSENSE readback:\r\n");
        for (uint8_t id = 1u; id <= COOLING_TEC_COUNT; id++) {
            console_print_tec_status(id, &status[id - 1u]);
        }
        return ok ? 0 : -2;
    }

    uint8_t channel_id;
    if (!parse_tec_id(argv[2], &channel_id)) {
        console_printf("\r\nUsage: tec read <1..4|all>\r\n");
        return -3;
    }

    cooling_tec_status_t status;
    if (!cooling_tec_read(channel_id, &status)) {
        console_printf("\r\nTEC %u VSENSE read failed.\r\n", (unsigned)channel_id);
        return -4;
    }
    console_printf("\r\n");
    console_print_tec_status(channel_id, &status);
    return 0;
}

static int cmd_tec_stop(int argc, char **argv, char *out, size_t out_len)
{
    (void)out;
    (void)out_len;

    if (argc == 2) {
        if (!cooling_tec_stop_all()) {
            console_printf("\r\nFailed to stop one or more TEC channels.\r\n");
            return -1;
        }
        console_printf("\r\nAll TEC channels stopped; enables low and DACs shut down.\r\n");
        return 0;
    }

    if (argc != 3) {
        console_printf("\r\nUsage: tec stop [1..4]\r\n");
        return -2;
    }

    uint8_t channel_id;
    if (!parse_tec_id(argv[2], &channel_id)) {
        console_printf("\r\nUsage: tec stop [1..4]\r\n");
        return -3;
    }

    if (!cooling_tec_stop(channel_id)) {
        console_printf("\r\nFailed to fully stop TEC %u; verify its enable line is low.\r\n",
                       (unsigned)channel_id);
        return -4;
    }
    console_printf("\r\nTEC %u stopped; enable low and DAC placed in shutdown.\r\n",
                   (unsigned)channel_id);
    return 0;
}

/* Compatibility with the original single-channel test syntax. */
static int cmd_tec_start(int argc, char **argv, char *out, size_t out_len)
{
    if (argc == 3) {
        char *compat_argv[] = { argv[0], "set", "1", argv[2] };
        return cmd_tec_set(4, compat_argv, out, out_len);
    }
    if (argc == 4) {
        char *compat_argv[] = { argv[0], "set", argv[2], argv[3] };
        return cmd_tec_set(4, compat_argv, out, out_len);
    }

    console_printf("\r\nUsage: tec start [channel] <0..255>\r\n");
    return -1;
}

static int cmd_pwm_start(int argc, char **argv, char *out, size_t out_len)
{
    (void)out;
    (void)out_len;

    uint32_t value = 0;
    if (argc >= 3 && (!parse_u32(argv[2], &value) || value > UINT8_MAX)) {
        console_printf("\r\nUsage: pwm start [0..255]\r\n");
        return -2;
    }

    if (!ztm_pwm_test((uint16_t)value)) {
        console_printf("\r\nPWM module is not ready.\r\n");
        return -4;
    }
    console_printf("\r\nOK, PWM %u\r\n", (unsigned)value);
    return 0;
}

static int cmd_pwm_stop(int argc, char **argv, char *out, size_t out_len)
{
    (void)argc;
    (void)argv;
    (void)out;
    (void)out_len;

    if (!ztm_pwm_test(0)) {
        console_printf("\r\nPWM module is not ready.\r\n");
        return -1;
    }
    console_printf("\r\nPWM stopped.\r\n");
    return 0;
}

/* =========================================================
   Global safe-state command
   ========================================================= */

static int cmd_safe(int argc, char **argv, char *out, size_t out_len)
{
    (void)argc;
    (void)argv;
    (void)out;
    (void)out_len;

    console_printf("\r\nRequesting safe state...\r\n");

    bool pump_state_ok = !pumping_ready() || pumping_all_stop();
    bool hiz_ok = !ztm6470_ready() || ztm_stepper_hard_hiz_all();
    bool stepper_ok = pump_state_ok && hiz_ok;

    bool tec_ok = true;
    if (cooling_ready()) {
        /* Keeps cooling.c's tracked enable state consistent with hardware. */
        tec_ok = cooling_tec_stop_all();
    } else {
        /* Cooling may be unavailable because one module failed; stop every fitted TEC. */
        const char *tec_names[] = { "psu_tec_1", "psu_tec_2", "psu_tec_3", "psu_tec_4" };
        for (size_t i = 0; i < sizeof(tec_names) / sizeof(tec_names[0]); i++) {
            module_entry_t *entry = modules_find_by_short_name(tec_names[i]);
            if (!entry || !entry->mod_inst) {
                continue;
            }
            psu_tec_t *tec = (psu_tec_t *)entry->mod_inst;
            bool disabled = mcp4901_set_enable(tec, false);
            bool shutdown = mcp4901_shutdown(tec);
            tec_ok = disabled && shutdown && tec_ok;
        }
    }

    bool hv_ok = true;
    module_entry_t *hv_entry = modules_find_by_short_name("ztm_psu_hv");
    if (hv_entry && hv_entry->mod_inst) {
        psu_hv_t *hv = (psu_hv_t *)hv_entry->mod_inst;
        hv_ok = ztm_psu_hv_enable(hv, false);
        if (hv_ok) {
            hv_ok = ztm_psu_hv_set_voltage(hv, 0);
        }
    }

    bool pwm_ok = !modules_find_by_short_name("ztm_pwm") || ztm_pwm_test(0);

    console_printf("  steppers: %s\r\n", stepper_ok ? "HiZ/not fitted" : "FAILED");
    console_printf("  TECs:     %s\r\n", tec_ok ? "disabled/not fitted" : "FAILED");
    console_printf("  HV:       %s\r\n", hv_ok ? "disabled/not fitted" : "FAILED");
    console_printf("  PWM:      %s\r\n", pwm_ok ? "zero/not fitted" : "FAILED");

    return stepper_ok && tec_ok && hv_ok && pwm_ok ? 0 : -1;
}

/* =========================================================
   Command tables and dispatcher
   ========================================================= */

static int cmd_help(int argc, char **argv, char *out, size_t out_len);

static const cli_cmd_t module_cmds[] = {
    { "list", "list each configured module and initialization result", cmd_module_list },
};

static const cli_cmd_t stepper_cmds[] = {
    { "info",   "show SPI-chain topology and configuration", cmd_stepper_info },
    { "nop",    "stepper nop [count] - harmless all-slot chain frames", cmd_stepper_nop },
    { "status", "stepper status [1..4|all] - raw GET_STATUS", cmd_stepper_status },
    { "reset",  "stepper reset <1..4|all> - reset, restore verified config, keep 4 HiZ", cmd_stepper_reset },
    { "hiz",    "stepper hiz [1..4|all] - immediate hard HiZ", cmd_stepper_hiz },
    { "stop",   "stepper stop [1..4|all] - all stops 1..3 and keeps 4 HiZ", cmd_stepper_stop },
    { "config", "stepper config <1..4|all> - HOLD=0, RUN=25, ACC/DEC=40", cmd_stepper_config },
    { "run",    "stepper run <1..3|all> <fwd|rev> <steps/s>; all excludes motor 4", cmd_stepper_run },
    { "move",   "stepper move <1..3> <fwd|rev> <steps>", cmd_stepper_move },
    { "get",    "stepper get <1..4> <register> <bytes>", cmd_stepper_get },
    { "set",    "stepper set <1..4> <register> <bytes> <value> - raw write", cmd_stepper_set },
};

static const cli_cmd_t pump_cmds[] = {
    { "status",        "show pump mapping, state, speed, and wick timer", cmd_pump_status },
    { "filter-normal", "IPA filter: motor 1 fwd 50 steps/s", cmd_pump_filter_normal },
    { "filter-fast",   "IPA filter: motor 1 fwd 100 steps/s", cmd_pump_filter_fast },
    { "filter-stop",   "stop IPA filter motor 1", cmd_pump_filter_stop },
    { "wick-prime",    "IPA wick: motor 3 fwd 50 steps/s for 5000 ms", cmd_pump_wick_prime },
    { "wick-dose",     "IPA wick: motor 3 fwd 50 steps/s for 2000 ms", cmd_pump_wick_dose },
    { "wick-drain",    "IPA wick: motor 3 rev 50 steps/s for 5000 ms", cmd_pump_wick_drain },
    { "wick-stop",     "stop IPA wick motor 3", cmd_pump_wick_stop },
    { "sump-run",      "IPA sump: motor 2 fwd 50 steps/s", cmd_pump_sump_run },
    { "sump-stop",     "stop IPA sump motor 2", cmd_pump_sump_stop },
    { "stop-all",      "stop motors 1..3 and keep motor 4 in HiZ", cmd_pump_stop_all },
    { "start",         "compatibility alias: [1=filter, 2=sump, 3=wick dose]", cmd_pump_start },
    { "stop",          "compatibility alias: stop [1=filter, 2=sump, 3=wick]", cmd_pump_stop },
};

static const cli_cmd_t tec_cmds[] = {
    { "set",      "tec set <1..4> <0..255> - set one DAC and verify VSENSE", cmd_tec_set },
    { "set-all",  "tec set-all <0..255> - set all DACs and read all VSENSE inputs", cmd_tec_set_all },
    { "read",     "tec read <1..4|all> - read output voltage through 130k/20k divider", cmd_tec_read },
    { "stop",     "tec stop [1..4] - no channel stops all; otherwise stop one", cmd_tec_stop },
    { "start",    "compatibility alias: tec start [channel] <0..255>", cmd_tec_start },
};

static const cli_cmd_t pwm_cmds[] = {
    { "start", "pwm start [value] - set raw fan PWM (0..255)", cmd_pwm_start },
    { "stop",  "pwm stop - set raw fan PWM to zero", cmd_pwm_stop },
};

static const cli_group_t groups[] = {
    { "module",  "Module discovery and initialization diagnostics", module_cmds, sizeof(module_cmds) / sizeof(module_cmds[0]) },
    { "stepper", "Four-device L6470 chain diagnostics and bench control", stepper_cmds, sizeof(stepper_cmds) / sizeof(stepper_cmds[0]) },
    { "pump",    "IPA filter, wick, and sump process controls", pump_cmds, sizeof(pump_cmds) / sizeof(pump_cmds[0]) },
    { "tec",     "Thermoelectric cooler controls", tec_cmds, sizeof(tec_cmds) / sizeof(tec_cmds[0]) },
    { "pwm",     "PWM fan controls", pwm_cmds, sizeof(pwm_cmds) / sizeof(pwm_cmds[0]) },
};

static const cli_cmd_t root_cmds[] = {
    { "help", "Show help", cmd_help },
    { "safe", "Disable hazardous outputs and place steppers in hard HiZ", cmd_safe },
};

static int handle_command_internal(char *cmd, char *out, size_t out_len)
{
    while (*cmd && isspace((unsigned char)*cmd)) cmd++;
    if (*cmd == '\0') return -1;

    char *argv[CLI_MAX_ARGS];
    int argc = cli_tokenize_simple(cmd, argv, CLI_MAX_ARGS);
    if (argc == 0) return -1;

    const cli_cmd_t *root = find_cmd(root_cmds,
                                     sizeof(root_cmds) / sizeof(root_cmds[0]),
                                     argv[0]);
    if (root) {
        return root->fn(argc, argv, out, out_len);
    }

    const cli_group_t *grp = find_group(groups,
                                        sizeof(groups) / sizeof(groups[0]),
                                        argv[0]);
    if (!grp) {
        console_printf("\r\nUnknown command/group. Type 'help'.\r\n");
        return -2;
    }

    if (argc < 2) {
        help_group(grp);
        return -3;
    }

    const cli_cmd_t *sub = find_cmd(grp->cmds, grp->cmd_count, argv[1]);
    if (!sub) {
        console_printf("\r\nUnknown subcommand for %s. Try 'help %s'.\r\n",
                       grp->name,
                       grp->name);
        return -1;
    }

    return sub->fn(argc, argv, out, out_len);
}

static int cmd_help(int argc, char **argv, char *out, size_t out_len)
{
    (void)out;
    (void)out_len;

    if (argc == 1) {
        console_printf("\r\nTop-level commands:\r\n");
        for (size_t i = 0; i < sizeof(root_cmds) / sizeof(root_cmds[0]); i++) {
            console_printf("  %s - %s\r\n", root_cmds[i].name, root_cmds[i].help);
        }

        console_printf("\r\nCommand groups:\r\n");
        for (size_t g = 0; g < sizeof(groups) / sizeof(groups[0]); g++) {
            console_printf("  %s - %s\r\n", groups[g].name, groups[g].help);
        }
        console_printf("\r\nTry: help <group> or help <group> <subcmd>\r\n");
        return 0;
    }

    const cli_group_t *grp = find_group(groups,
                                        sizeof(groups) / sizeof(groups[0]),
                                        argv[1]);
    if (!grp) {
        console_printf("\r\nUnknown group. Type 'help'.\r\n");
        return -1;
    }

    if (argc == 2) {
        help_group(grp);
        return 0;
    }

    const cli_cmd_t *sub = find_cmd(grp->cmds, grp->cmd_count, argv[2]);
    if (!sub) {
        console_printf("\r\nUnknown subcommand. Try 'help %s'.\r\n", grp->name);
        return -2;
    }
    help_subcmd(grp, sub);
    return 0;
}
