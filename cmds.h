#pragma once

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "ztmb/cli_cmds.h"
#include "ztmb/modules.h"
#include "zaptronics/boards/ztpico2/io_exp.h"

#include "ztmb/ztm_stepper_adapter.h"
#include "ztmb/ztm_adc_adapter.h"
#include "cooling.h"
#include "pumping.h"
#include "ztmb/ztm_pwm_adapter.h"
#include "ztmb/ztm_psu_hv_adapter.h"
#include "ztmb/ztm_psu_tec_adapter.h"
#include "firmware_version.h"

/* =========================================================
   Console output helpers
   ========================================================= */

/* The dashboard owns terminal output while visible. Its renderer has a
 * separate buffer; ordinary command reports must not scroll over that screen. */
static bool console_screen_active;

static void console_printf(const char *fmt, ...)
{
    if (console_screen_active) return;
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

    const bus_spi_t *bus = ztmb_stepper_bus();
    console_printf("\r\nStepper module: %s\r\n", ztm_stepper_ready(&ztm_stepper_1) ? "READY" : "NOT READY");
    if (bus) {
        console_printf("  SPI: %u Hz, %u-bit, mode %u, CS line %u\r\n",
                       (unsigned)bus->config.baud_hz,
                       (unsigned)bus->config.data_bits,
                       (unsigned)((bus->config.cpol ? 2u : 0u) |
                                  (bus->config.cpha ? 1u : 0u)),
                       (unsigned)bus->cs_mux_id);
    }
    console_printf("  Chain: %u L6470 slots, %u bytes/frame; logical motor 1 is nearest to controller\r\n",
                   (unsigned)ZTM_STEPPER_DRIVER_COUNT,
                   (unsigned)ZTM_STEPPER_DRIVER_COUNT);
    console_printf("  Active motors: 1..%u; motor %u is unused and held in hard HiZ\r\n",
                   (unsigned)ZTM_STEPPER_ACTIVE_DRIVER_COUNT,
                   (unsigned)ZTM_STEPPER_UNUSED_DRIVER_ID);
    for (size_t i = 0; i < ZTM_STEPPER_DRIVER_COUNT; i++) {
        console_printf("    motor %u: wire_offset=%u frame=1 byte\r\n",
                       (unsigned)(i + 1u),
                       (unsigned)(ZTM_STEPPER_DRIVER_COUNT - 1u - i));
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
        if (!ztm_stepper_nop(&ztm_stepper_1, responses)) {
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
            bool ok = ztm_stepper_get_status(&ztm_stepper_1, id, &status);
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
    if (!ztm_stepper_get_status(&ztm_stepper_1, id, &status)) {
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
        ok = ztm_stepper_reset_all(&ztm_stepper_1);
    } else {
        uint8_t id;
        if (!parse_stepper_id(argv[2], &id)) {
            console_printf("\r\nInvalid motor selector. Use 1..4 or all.\r\n");
            return -2;
        }
        ok = ztm_stepper_reset(&ztm_stepper_1, id);
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
        ok = ztm_stepper_hard_hiz_all(&ztm_stepper_1);
    } else {
        uint8_t id;
        if (!parse_stepper_id(selector, &id)) {
            console_printf("\r\nUsage: stepper hiz [1..4|all]\r\n");
            return -1;
        }
        ok = ztm_stepper_hard_hiz(&ztm_stepper_1, id);
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
        ok = ztm_stepper_soft_stop_all(&ztm_stepper_1);
    } else {
        uint8_t id;
        if (!parse_stepper_id(selector, &id)) {
            console_printf("\r\nUsage: stepper stop [1..4|all]\r\n");
            return -1;
        }
        ok = ztm_stepper_soft_stop(&ztm_stepper_1, id);
    }

    console_printf("\r\n%s\r\n", ok ? "OK" : "Soft-stop command failed");
    return ok ? 0 : -2;
}

static bool cmd_stepper_print_kval_config(uint8_t id)
{
    ztm_stepper_kval_config_t config;
    if (!ztm_stepper_get_kval_config(&ztm_stepper_1, id, &config)) {
        console_printf("  motor %u: READ FAILED\r\n", id);
        return false;
    }

    console_printf("  motor %u: HOLD=%u RUN=%u ACC=%u DEC=%u\r\n",
                   id,
                   config.hold,
                   config.run,
                   config.acc,
                   config.dec);
    return true;
}

static int cmd_stepper_config(int argc, char **argv, char *out, size_t out_len)
{
    (void)out;
    (void)out_len;

    if (argc < 3) {
        console_printf("\r\nUsage:\r\n");
        console_printf("  stepper config <1..4|all>\r\n");
        console_printf("  stepper config <1..4|all> <hold> <run> <acc> <dec>\r\n");
        console_printf("  stepper config <1..4|all> defaults\r\n");
        return -1;
    }

    const bool all = selector_is_all(argv[2]);
    uint8_t id = 0;
    if (!all && !parse_stepper_id(argv[2], &id)) {
        console_printf("\r\nInvalid motor selector. Use 1..4 or all.\r\n");
        return -2;
    }

    if (argc == 3) {
        console_printf("\r\nL6470 KVAL configuration:\r\n");
        bool ok = true;
        if (all) {
            for (uint8_t motor = 1; motor <= ZTM_STEPPER_DRIVER_COUNT; motor++) {
                ok = cmd_stepper_print_kval_config(motor) && ok;
            }
        } else {
            ok = cmd_stepper_print_kval_config(id);
        }
        return ok ? 0 : -3;
    }

    bool ok = false;
    if (argc == 4 &&
        (strcmp(argv[3], "default") == 0 || strcmp(argv[3], "defaults") == 0)) {
        ok = all ? ztm_stepper_apply_test_config_all(&ztm_stepper_1)
                 : ztm_stepper_apply_test_config(&ztm_stepper_1, id);
        if (ok) {
            console_printf("\r\nCompiled defaults restored: HOLD=%u RUN=%u ACC=%u DEC=%u\r\n",
                           ZTM_STEPPER_DEFAULT_KVAL_HOLD,
                           ZTM_STEPPER_DEFAULT_KVAL_RUN,
                           ZTM_STEPPER_DEFAULT_KVAL_ACC,
                           ZTM_STEPPER_DEFAULT_KVAL_DEC);
        }
    } else if (argc == 7) {
        uint32_t hold;
        uint32_t run;
        uint32_t acc;
        uint32_t dec;
        if (!parse_u32(argv[3], &hold) || hold > UINT8_MAX ||
            !parse_u32(argv[4], &run) || run > UINT8_MAX ||
            !parse_u32(argv[5], &acc) || acc > UINT8_MAX ||
            !parse_u32(argv[6], &dec) || dec > UINT8_MAX) {
            console_printf("\r\nKVAL values must each be 0..255; decimal or hex is accepted.\r\n");
            return -4;
        }

        const ztm_stepper_kval_config_t config = {
            .hold = (uint8_t)hold,
            .run = (uint8_t)run,
            .acc = (uint8_t)acc,
            .dec = (uint8_t)dec,
        };
        ok = all ? ztm_stepper_set_kval_config_all(&ztm_stepper_1, &config)
                 : ztm_stepper_set_kval_config(&ztm_stepper_1, id, &config);
        if (ok) {
            console_printf("\r\nKVAL configuration applied: HOLD=%u RUN=%u ACC=%u DEC=%u\r\n",
                           config.hold,
                           config.run,
                           config.acc,
                           config.dec);
            console_printf("Retained across 'stepper reset'; a firmware restart restores compiled defaults.\r\n");
        }
    } else {
        console_printf("\r\nUsage:\r\n");
        console_printf("  stepper config <1..4|all>\r\n");
        console_printf("  stepper config <1..4|all> <hold> <run> <acc> <dec>\r\n");
        console_printf("  stepper config <1..4|all> defaults\r\n");
        return -5;
    }

    if (!ok) {
        console_printf("\r\nStepper configuration failed.\r\n");
        return -6;
    }
    return 0;
}

static const char *stepper_microstep_name(ztm_stepper_microstep_t mode)
{
    switch (mode) {
        case ZTM_STEPPER_MICROSTEP_FULL:            return "full";
        case ZTM_STEPPER_MICROSTEP_HALF:            return "1/2";
        case ZTM_STEPPER_MICROSTEP_QUARTER:         return "1/4";
        case ZTM_STEPPER_MICROSTEP_EIGHTH:          return "1/8";
        case ZTM_STEPPER_MICROSTEP_SIXTEENTH:       return "1/16";
        case ZTM_STEPPER_MICROSTEP_THIRTY_SECOND:   return "1/32";
        case ZTM_STEPPER_MICROSTEP_SIXTY_FOURTH:    return "1/64";
        case ZTM_STEPPER_MICROSTEP_ONE_TWENTY_EIGHTH: return "1/128";
        default:                                    return "invalid";
    }
}

static bool parse_stepper_microstep(const char *text,
                                    ztm_stepper_microstep_t *mode)
{
    if (!text || !mode) {
        return false;
    }

    if (strcmp(text, "full") == 0 || strcmp(text, "off") == 0 ||
        strcmp(text, "1") == 0 || strcmp(text, "1/1") == 0) {
        *mode = ZTM_STEPPER_MICROSTEP_FULL;
    } else if (strcmp(text, "half") == 0 || strcmp(text, "2") == 0 ||
               strcmp(text, "1/2") == 0) {
        *mode = ZTM_STEPPER_MICROSTEP_HALF;
    } else if (strcmp(text, "quarter") == 0 || strcmp(text, "4") == 0 ||
               strcmp(text, "1/4") == 0) {
        *mode = ZTM_STEPPER_MICROSTEP_QUARTER;
    } else if (strcmp(text, "8") == 0 || strcmp(text, "1/8") == 0) {
        *mode = ZTM_STEPPER_MICROSTEP_EIGHTH;
    } else if (strcmp(text, "16") == 0 || strcmp(text, "1/16") == 0) {
        *mode = ZTM_STEPPER_MICROSTEP_SIXTEENTH;
    } else if (strcmp(text, "32") == 0 || strcmp(text, "1/32") == 0) {
        *mode = ZTM_STEPPER_MICROSTEP_THIRTY_SECOND;
    } else if (strcmp(text, "64") == 0 || strcmp(text, "1/64") == 0) {
        *mode = ZTM_STEPPER_MICROSTEP_SIXTY_FOURTH;
    } else if (strcmp(text, "128") == 0 || strcmp(text, "1/128") == 0) {
        *mode = ZTM_STEPPER_MICROSTEP_ONE_TWENTY_EIGHTH;
    } else {
        return false;
    }
    return true;
}

static bool cmd_stepper_print_microstep(uint8_t id)
{
    ztm_stepper_microstep_t mode;
    if (!ztm_stepper_get_microstep(&ztm_stepper_1, id, &mode)) {
        console_printf("  motor %u: READ FAILED\r\n", id);
        return false;
    }

    console_printf("  motor %u: %s (STEP_SEL=%u)\r\n",
                   id,
                   stepper_microstep_name(mode),
                   (unsigned)mode);
    return true;
}

static int cmd_stepper_microstep(int argc, char **argv, char *out, size_t out_len)
{
    (void)out;
    (void)out_len;

    if (argc < 3 || argc > 4) {
        console_printf("\r\nUsage: stepper microstep <1..4|all> "
                       "[full|1/2|1/4|1/8|1/16|1/32|1/64|1/128|defaults]\r\n");
        return -1;
    }

    const bool all = selector_is_all(argv[2]);
    uint8_t id = 0;
    if (!all && !parse_stepper_id(argv[2], &id)) {
        console_printf("\r\nInvalid motor selector. Use 1..4 or all.\r\n");
        return -2;
    }

    if (argc == 3) {
        console_printf("\r\nL6470 microstep configuration:\r\n");
        bool ok = true;
        if (all) {
            for (uint8_t motor = 1; motor <= ZTM_STEPPER_DRIVER_COUNT; motor++) {
                ok = cmd_stepper_print_microstep(motor) && ok;
            }
        } else {
            ok = cmd_stepper_print_microstep(id);
        }
        return ok ? 0 : -3;
    }

    ztm_stepper_microstep_t mode;
    if (strcmp(argv[3], "default") == 0 || strcmp(argv[3], "defaults") == 0) {
        mode = ZTM_STEPPER_DEFAULT_MICROSTEP;
    } else if (!parse_stepper_microstep(argv[3], &mode)) {
        console_printf("\r\nMode must be full, 1/2, 1/4, 1/8, 1/16, "
                       "1/32, 1/64, or 1/128.\r\n");
        return -4;
    }

    const bool ok = all ? ztm_stepper_set_microstep_all(&ztm_stepper_1, mode)
                        : ztm_stepper_set_microstep(&ztm_stepper_1, id, mode);
    if (!ok) {
        console_printf("\r\nMicrostep configuration failed.\r\n");
        return -5;
    }

    console_printf("\r\nMicrostep mode set to %s. Selected motor(s) are now in hard HiZ; "
                   "ABS_POS was reset to zero.\r\n",
                   stepper_microstep_name(mode));
    console_printf("Retained across 'stepper reset'; a firmware restart restores the compiled 1/4-step default.\r\n");
    console_printf("MOVE counts use the selected step unit (full, half, quarter, etc.).\r\n");
    return 0;
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
        ok = ztm_stepper_run_all(&ztm_stepper_1, forward, speed);
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
        ok = ztm_stepper_run(&ztm_stepper_1, id, forward, speed);
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

    bool ok = ztm_stepper_move(&ztm_stepper_1, id, forward, steps);
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
    if (!ztm_stepper_get_param(&ztm_stepper_1, id, (uint8_t)reg, (uint8_t)nbytes, &value)) {
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

    bool ok = ztm_stepper_set_param(&ztm_stepper_1, id,
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
        case PUMP_WICK_WET:      return "wet";
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
    if (state == PUMP_WICK_PRIME || state == PUMP_WICK_DOSE || state == PUMP_WICK_WET) {
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
    console_printf("  IPA sump    motor 3: %-7s fwd %u steps/s\r\n",
                   pump_state_name(status.sump_state), (unsigned)status.sump_speed);
    console_printf("  IPA wick    motor 2: %-7s %s %u steps/s, %u ms remaining\r\n",
                   pump_state_name(status.wick_state),
                   pump_wick_direction_name(status.wick_state),
                   (unsigned)status.wick_speed,
                   (unsigned)status.wick_remaining_ms);
    if (status.wick_state == PUMP_WICK_WET)
        console_printf("  Wick wetting is continuous; no stop timer is active.\r\n");
    console_printf("  Unused      motor 4: hard HiZ\r\n");
    return pumping_ready() ? 0 : -1;
}

static int cmd_pump_filter_normal(int argc, char **argv, char *out, size_t out_len)
{
    (void)argc; (void)argv; (void)out; (void)out_len;
    return pump_command_result(pumping_filter_start_normal(),
                               "IPA filter normal: motor 1 fwd 30 steps/s");
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
                               "IPA wick prime: motor 2 fwd 100 steps/s for 5000 ms");
}

static int cmd_pump_wick_dose(int argc, char **argv, char *out, size_t out_len)
{
    (void)argc; (void)argv; (void)out; (void)out_len;
    return pump_command_result(pumping_wick_dose(),
                               "IPA wick dose: motor 2 fwd 30 steps/s for 2000 ms");
}

static int cmd_pump_wick_drain(int argc, char **argv, char *out, size_t out_len)
{
    (void)argc; (void)argv; (void)out; (void)out_len;
    return pump_command_result(pumping_wick_drain(),
                               "IPA wick drain: motor 2 rev 100 steps/s for 5000 ms");
}

static int cmd_pump_wick_wet(int argc, char **argv, char *out, size_t out_len)
{
    (void)argc; (void)argv; (void)out; (void)out_len;
    return pump_command_result(pumping_wick_wet(),
                               "IPA wick wet: motor 2 fwd 30 steps/s continuously; wick-stop to end");
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
                               "IPA sump running: motor 3 fwd 30 steps/s");
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
            return pump_command_result(ztm_stepper_hard_hiz(&ztm_stepper_1, ZTM_STEPPER_UNUSED_DRIVER_ID),
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

static int cmd_tec_permit(int argc, char **argv, char *out, size_t out_len)
{
    (void)out; (void)out_len;
    if (argc == 2) {
        console_printf("\r\nTEC operator permission: %s\r\n",
                       cooling_tec_permission_granted() ? "GRANTED" : "LOCKED");
        console_printf("Verify Process Chiller is at temperature and coolant is circulating, then enter:\r\n"
                       "  tec permit chiller-at-temperature-and-circulating\r\n"
                       "Without coolant, TEC operation can destroy the machine.\r\n");
        return 0;
    }
    if (argc != 3 || strcmp(argv[2], "chiller-at-temperature-and-circulating") != 0) {
        console_printf("\r\nPermission not granted: explicit chiller confirmation phrase required.\r\n");
        return -1;
    }
    bool ok = cooling_tec_confirm_chiller();
    console_printf("\r\n%s\r\n", ok
        ? "Operator confirmed chiller temperature and circulation. TEC permission granted; outputs unchanged."
        : "Permission denied; TECs locked and all-off attempted. Check module/enable faults.");
    return ok ? 0 : -1;
}

static int cmd_tec_revoke(int argc, char **argv, char *out, size_t out_len)
{
    (void)argc; (void)argv; (void)out; (void)out_len;
    bool ok = cooling_tec_revoke_permission();
    console_printf("\r\n%s\r\n", ok
        ? "TEC permission revoked; all enables low and DACs shut down."
        : "TEC permission revoked; STOP FAILED on one or more channels. Verify hardware OFF.");
    return ok ? 0 : -1;
}

static bool cmd_tec_require_permission(void)
{
    if (cooling_tec_permission_granted()) return true;
    console_printf("\r\nTEC LOCKED: operator must verify Process Chiller temperature and coolant circulation.\r\n"
                   "Use 'tec permit' for the explicit confirmation command.\r\n");
    return false;
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
    if (!cmd_tec_require_permission()) return -3;
    if (!cooling_tec_set_raw(channel_id, (uint8_t)value, &status)) {
        console_printf("\r\nTEC %u set/readback failed; permission revoked and all-off attempted. Verify hardware OFF.\r\n",
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
    if (!cmd_tec_require_permission()) return -3;
    if (!cooling_tec_set_all_raw((uint8_t)value, status)) {
        console_printf("\r\nTEC set-all/readback failed; permission revoked and all-off attempted. Verify hardware OFF.\r\n");
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

static bool parse_pwm_channel(const char *text, uint8_t *channel)
{
    uint32_t value = 0;
    if (!channel || !parse_u32(text, &value) || value < 1u || value > ZTM_PWM_CHANNEL_COUNT) {
        return false;
    }
    *channel = (uint8_t)value;
    return true;
}

static bool parse_pwm_fan(const char *text, uint8_t *fan)
{
    uint32_t value = 0;
    if (!fan || !parse_u32(text, &value) || value < 1u || value > ZTM_PWM_FAN_COUNT) {
        return false;
    }
    *fan = (uint8_t)value;
    return true;
}

static bool parse_pwm_fan_output_mode(const char *text, ztm_pwm_fan_output_mode_t *mode)
{
    if (!text || !mode) {
        return false;
    }
    if (strcmp(text, "open-drain") == 0 || strcmp(text, "open") == 0 ||
        strcmp(text, "od") == 0) {
        *mode = ZTM_PWM_FAN_OUTPUT_OPEN_DRAIN;
        return true;
    }
    if (strcmp(text, "push-pull") == 0 || strcmp(text, "push") == 0 ||
        strcmp(text, "pp") == 0) {
        *mode = ZTM_PWM_FAN_OUTPUT_PUSH_PULL;
        return true;
    }
    return false;
}

static bool parse_pwm_gpio(const char *text, uint8_t *gpio)
{
    uint32_t value = 0;
    if (!gpio || !parse_u32(text, &value) || value < 1u || value > ZTM_PWM_GPIO_COUNT) {
        return false;
    }
    *gpio = (uint8_t)value;
    return true;
}

static bool parse_percent(const char *text, uint8_t *percent)
{
    uint32_t value = 0;
    if (!percent || !parse_u32(text, &value) || value > 100u) {
        return false;
    }
    *percent = (uint8_t)value;
    return true;
}

static const char *pwm_pull_name(ztm_pwm_gpio_pull_t pull);
static const char *pwm_irq_sense_name(sx1509_sense_t sense);

static void print_current(uint8_t channel, int32_t current_uA)
{
    int64_t value = current_uA;
    const char *sign = "";
    if (value < 0) {
        sign = "-";
        value = -value;
    }
    console_printf("  CH%u: %s%lld.%03lld mA\r\n",
                   (unsigned)channel,
                   sign,
                   (long long)(value / 1000ll),
                   (long long)(value % 1000ll));
}

static int cmd_pwm_info(int argc, char **argv, char *out, size_t out_len)
{
    (void)argc; (void)argv; (void)out; (void)out_len;

    const ztm_pwm_t *module = &ztm_pwm_1;
    console_printf("\r\nPWM module: %s\r\n", ztm_pwm_ready(&ztm_pwm_1) ? "READY" : "NOT READY");
    console_printf("  I2C0: SX1509=0x%02X, PAC=0x%02X/0x%02X/0x%02X, EMC2303=0x%02X/0x%02X\r\n",
                   ZTM_PWM_SX1509_ADDR,
                   ZTM_PWM_PAC1_ADDR, ZTM_PWM_PAC2_ADDR, ZTM_PWM_PAC3_ADDR,
                   ZTM_PWM_EMC1_ADDR, ZTM_PWM_EMC2_ADDR);
    console_printf("  Enable: EX pin %d, shared IRQ: EX pin %d active-low\r\n",
                   (int)ztmb_ztm_pwm_enable_pin(), (int)ztmb_ztm_pwm_irq_pin());
    console_printf("  Low-side map:  CH1..12 -> SX IO 15,14,13,12,11,10,9,7,5,4,2,0\r\n");
    console_printf("  GPIO map:      GPIO1..4 -> SX IO 8,6,3,1\r\n");
    console_printf("  Fan map:       Fan1..6 -> EMC1[1..3], EMC2[1..3]\r\n");
    console_printf("  IRQ scans: %lu, scan errors: %lu\r\n",
                   (unsigned long)module->irq_count,
                   (unsigned long)module->irq_scan_errors);
    console_printf("  Last init: stage=%s, rc=%d\r\n",
                   ztm_pwm_init_stage_name(module->last_init_stage),
                   module->last_init_rc);

    static const uint8_t pac_addr[ZTM_PWM_PAC_COUNT] = {
        ZTM_PWM_PAC1_ADDR, ZTM_PWM_PAC2_ADDR, ZTM_PWM_PAC3_ADDR
    };
    console_printf("  Current monitors:\r\n");
    for (uint8_t index = 0u; index < ZTM_PWM_PAC_COUNT; index++) {
        const pac195x_t *pac = &module->current_monitor[index];
        uint8_t first_channel = (uint8_t)(index * 4u + 1u);
        uint8_t last_channel = (uint8_t)(first_channel + 3u);
        if (module->current_monitor_ready[index]) {
            console_printf("    PAC%u 0x%02X: READY PID=0x%02X, %s, CH%u..CH%u\r\n",
                           (unsigned)(index + 1u),
                           pac_addr[index],
                           pac->product_id,
                           ztm_pwm_pac_topology_name(module->current_monitor_topology[index]),
                           (unsigned)first_channel,
                           (unsigned)last_channel);
        } else {
            console_printf("    PAC%u 0x%02X: OFFLINE rc=%d, CH%u..CH%u current unavailable\r\n",
                           (unsigned)(index + 1u),
                           pac_addr[index],
                           module->current_monitor_rc[index],
                           (unsigned)first_channel,
                           (unsigned)last_channel);
        }
    }
    console_printf("  PAC1952-2 salvage route per group: VBUS1, VSENSE1, VSENSE2, VBUS2\r\n");
    console_printf("  VBUS-derived current is coarse (~9.77 mA/count at 50 mOhm); VPOWER is not used.\r\n");

    if (ztm_pwm_ready(&ztm_pwm_1)) {
        console_printf("  Low-side duty:");
        for (uint8_t channel = 1u; channel <= ZTM_PWM_CHANNEL_COUNT; channel++) {
            console_printf(" %u:%u%%", (unsigned)channel,
                           (unsigned)ztm_pwm_channel_get_duty(&ztm_pwm_1, channel));
        }
        console_printf("\r\n  Fan state:");
        for (uint8_t fan = 1u; fan <= ZTM_PWM_FAN_COUNT; fan++) {
            if (module->fan_closed_loop[fan - 1u]) {
                console_printf(" %u:target=%luRPM", (unsigned)fan,
                               (unsigned long)module->fan_target_rpm[fan - 1u]);
            } else {
                console_printf(" %u:%u%%", (unsigned)fan,
                               (unsigned)ztm_pwm_fan_get_duty(&ztm_pwm_1, fan));
            }
        }
        console_printf("\r\n  Fan output:");
        for (uint8_t fan = 1u; fan <= ZTM_PWM_FAN_COUNT; fan++) {
            ztm_pwm_fan_output_mode_t mode = ZTM_PWM_FAN_OUTPUT_OPEN_DRAIN;
            if (ztm_pwm_fan_get_output_mode(&ztm_pwm_1, fan, &mode)) {
                console_printf(" %u:%s", (unsigned)fan,
                               mode == ZTM_PWM_FAN_OUTPUT_PUSH_PULL ? "push-pull" : "open-drain");
            } else {
                console_printf(" %u:READ-ERROR", (unsigned)fan);
            }
        }
        console_printf("\r\n  GPIO state:");
        for (uint8_t gpio = 1u; gpio <= ZTM_PWM_GPIO_COUNT; gpio++) {
            console_printf(" %u:%s/pull-%s/irq-%s",
                           (unsigned)gpio,
                           module->gpio_mode[gpio - 1u] == ZTM_PWM_GPIO_OUTPUT ? "out" : "in",
                           pwm_pull_name(module->gpio_pull[gpio - 1u]),
                           pwm_irq_sense_name(module->gpio_irq_sense[gpio - 1u]));
        }
        console_printf("\r\n");
    }
    return 0;
}

static int cmd_pwm_set(int argc, char **argv, char *out, size_t out_len)
{
    (void)out; (void)out_len;
    uint8_t channel = 0, duty = 0;
    if (argc != 4 || !parse_pwm_channel(argv[2], &channel) || !parse_percent(argv[3], &duty)) {
        console_printf("\r\nUsage: pwm set <1..12> <0..100 percent>\r\n");
        return -1;
    }
    if (!ztm_pwm_channel_set(&ztm_pwm_1, channel, duty)) {
        console_printf("\r\nFailed to set PWM channel %u.\r\n", (unsigned)channel);
        return -2;
    }
    console_printf("\r\nPWM CH%u = %u%%\r\n", (unsigned)channel, (unsigned)duty);
    return 0;
}

static int cmd_pwm_set_all(int argc, char **argv, char *out, size_t out_len)
{
    (void)out; (void)out_len;
    uint8_t duty = 0;
    if (argc != 3 || !parse_percent(argv[2], &duty)) {
        console_printf("\r\nUsage: pwm set-all <0..100 percent>\r\n");
        return -1;
    }
    if (!ztm_pwm_channel_set_all(&ztm_pwm_1, duty)) {
        console_printf("\r\nFailed to set all PWM channels.\r\n");
        return -2;
    }
    console_printf("\r\nAll 12 low-side channels = %u%%\r\n", (unsigned)duty);
    return 0;
}

static int cmd_pwm_stop(int argc, char **argv, char *out, size_t out_len)
{
    (void)out; (void)out_len;
    if (argc == 2) {
        if (!ztm_pwm_stop_all(&ztm_pwm_1)) {
            console_printf("\r\nPWM stop failed: zero duty not confirmed on all outputs (POWER_SAFE rc=%d).\r\n", ztm_pwm_1.power_safe_rc);
            return -1;
        }
        console_printf("\r\nAll low-side and fan PWM duties set to 0%; physical fan stop not verified.\r\n");
        return 0;
    }

    uint8_t channel = 0;
    if (argc != 3 || !parse_pwm_channel(argv[2], &channel)) {
        console_printf("\r\nUsage: pwm stop [1..12]\r\n");
        return -2;
    }
    if (!ztm_pwm_channel_stop(&ztm_pwm_1, channel)) {
        console_printf("\r\nFailed to stop PWM channel %u.\r\n", (unsigned)channel);
        return -3;
    }
    console_printf("\r\nPWM CH%u stopped.\r\n", (unsigned)channel);
    return 0;
}

static void print_pwm_current_reading(uint8_t channel,
                                      const ztm_pwm_current_reading_t *reading)
{
    console_printf("  CH%u PAC%u/%s%u: raw=0x%04X, Vshunt=%+.3f mV, I=%+.3f mA%s\r\n",
                   (unsigned)channel,
                   (unsigned)reading->pac_1based,
                   ztm_pwm_current_source_name(reading->source),
                   (unsigned)reading->input_1based,
                   reading->raw,
                   (double)reading->shunt_uV / 1000.0,
                   (double)reading->current_uA / 1000.0,
                   reading->coarse_resolution ? " [COARSE]" : "");
}

static int cmd_pwm_current(int argc, char **argv, char *out, size_t out_len)
{
    (void)out; (void)out_len;
    if (argc != 3) {
        console_printf("\r\nUsage: pwm current <1..12|all>\r\n");
        return -1;
    }

    uint8_t first = 1u;
    uint8_t last = ZTM_PWM_CHANNEL_COUNT;
    if (!selector_is_all(argv[2])) {
        if (!parse_pwm_channel(argv[2], &first)) {
            console_printf("\r\nUsage: pwm current <1..12|all>\r\n");
            return -2;
        }
        last = first;
    }

    bool all_ok = true;
    console_printf("\r\nAveraged channel current (50 mOhm shunts):\r\n");
    for (uint8_t channel = first; channel <= last; channel++) {
        ztm_pwm_current_reading_t reading;
        if (ztm_pwm_read_channel(&ztm_pwm_1, channel, true, &reading)) {
            print_pwm_current_reading(channel, &reading);
        } else {
            uint8_t pac = (uint8_t)(((channel - 1u) / 4u) + 1u);
            console_printf("  CH%u PAC%u: CURRENT UNAVAILABLE\r\n",
                           (unsigned)channel, (unsigned)pac);
            all_ok = false;
        }
    }
    return all_ok ? 0 : -3;
}

static int cmd_pwm_fan_set(int argc, char **argv, char *out, size_t out_len)
{
    (void)out; (void)out_len;
    uint8_t fan = 0, duty = 0;
    if (argc != 4 || !parse_pwm_fan(argv[2], &fan) || !parse_percent(argv[3], &duty)) {
        console_printf("\r\nUsage: pwm fan <1..6> <0..100 percent>\r\n");
        return -1;
    }
    if (!ztm_pwm_fan_set(&ztm_pwm_1, fan, duty)) {
        console_printf("\r\nFailed to set fan %u PWM.\r\n", (unsigned)fan);
        return -2;
    }
    console_printf("\r\nFan %u PWM = %u%% (low-side CH%u remains independent)\r\n",
                   (unsigned)fan, (unsigned)duty, (unsigned)fan);
    return 0;
}

static int cmd_pwm_fan_output(int argc, char **argv, char *out, size_t out_len)
{
    (void)out; (void)out_len;
    if (argc != 3 && argc != 4) {
        console_printf("\r\nUsage: pwm fan-output <1..6|all> [open-drain|push-pull]\r\n");
        return -1;
    }

    uint8_t first = 1u;
    uint8_t last = ZTM_PWM_FAN_COUNT;
    if (!selector_is_all(argv[2])) {
        if (!parse_pwm_fan(argv[2], &first)) {
            console_printf("\r\nUsage: pwm fan-output <1..6|all> [open-drain|push-pull]\r\n");
            return -2;
        }
        last = first;
    }

    if (argc == 4) {
        ztm_pwm_fan_output_mode_t mode;
        if (!parse_pwm_fan_output_mode(argv[3], &mode)) {
            console_printf("\r\nOutput mode must be open-drain or push-pull.\r\n");
            return -3;
        }

        bool ok = selector_is_all(argv[2])
                      ? ztm_pwm_fan_set_output_mode_all(&ztm_pwm_1, mode)
                      : ztm_pwm_fan_set_output_mode(&ztm_pwm_1, first, mode);
        if (!ok) {
            console_printf("\r\nFailed to set fan PWM output mode.\r\n");
            return -4;
        }
    }

    console_printf("\r\nFan PWM output modes:\r\n");
    bool all_ok = true;
    for (uint8_t fan = first; fan <= last; fan++) {
        ztm_pwm_fan_output_mode_t mode;
        if (ztm_pwm_fan_get_output_mode(&ztm_pwm_1, fan, &mode)) {
            console_printf("  Fan %u: %s\r\n", (unsigned)fan,
                           ztm_pwm_fan_output_mode_name(mode));
        } else {
            console_printf("  Fan %u: READ ERROR\r\n", (unsigned)fan);
            all_ok = false;
        }
    }
    console_printf("  TACH inputs have no internal pull-up; external pull-ups are required.\r\n");
    return all_ok ? 0 : -5;
}

static int cmd_pwm_fan_stop(int argc, char **argv, char *out, size_t out_len)
{
    (void)out; (void)out_len;
    if (argc == 2) {
        if (!ztm_pwm_fan_stop_all(&ztm_pwm_1)) {
            console_printf("\r\nFailed to stop all fan PWM outputs.\r\n");
            return -1;
        }
        console_printf("\r\nAll six fan PWM outputs stopped.\r\n");
        return 0;
    }

    uint8_t fan = 0;
    if (argc != 3 || !parse_pwm_fan(argv[2], &fan)) {
        console_printf("\r\nUsage: pwm fan-stop [1..6]\r\n");
        return -2;
    }
    if (!ztm_pwm_fan_stop(&ztm_pwm_1, fan)) {
        console_printf("\r\nFailed to stop fan %u PWM.\r\n", (unsigned)fan);
        return -3;
    }
    console_printf("\r\nFan %u PWM stopped.\r\n", (unsigned)fan);
    return 0;
}

static int cmd_pwm_rpm(int argc, char **argv, char *out, size_t out_len)
{
    (void)out; (void)out_len;
    if (argc != 3) {
        console_printf("\r\nUsage: pwm rpm <1..6|all>\r\n");
        return -1;
    }

    if (selector_is_all(argv[2])) {
        bool all_ok = true;
        console_printf("\r\nFan tachometer readings:\r\n");
        for (uint8_t fan = 1u; fan <= ZTM_PWM_FAN_COUNT; fan++) {
            uint16_t count = 0u;
            uint32_t rpm = 0;
            ztm_pwm_fan_tach_status_t status =
                ztm_pwm_fan_get_tach_status(&ztm_pwm_1, fan, &count, &rpm);
            if (status == ZTM_PWM_FAN_TACH_READ_ERROR) {
                console_printf("  Fan %u: READ ERROR\r\n", (unsigned)fan);
                all_ok = false;
            } else if (status == ZTM_PWM_FAN_TACH_STALL) {
                console_printf("  Fan %u: NO VALID TACH (count=0x%04X; fan may still be spinning)\r\n",
                               (unsigned)fan, (unsigned)count);
                all_ok = false;
            } else {
                console_printf("  Fan %u: %lu RPM\r\n", (unsigned)fan, (unsigned long)rpm);
            }
        }
        return all_ok ? 0 : -3;
    }

    uint8_t fan = 0;
    uint16_t count = 0u;
    uint32_t rpm = 0;
    if (!parse_pwm_fan(argv[2], &fan)) {
        console_printf("\r\nUsage: pwm rpm <1..6|all>\r\n");
        return -2;
    }
    ztm_pwm_fan_tach_status_t status =
        ztm_pwm_fan_get_tach_status(&ztm_pwm_1, fan, &count, &rpm);
    if (status == ZTM_PWM_FAN_TACH_READ_ERROR) {
        console_printf("\r\nFailed to read fan %u tachometer.\r\n", (unsigned)fan);
        return -3;
    }
    if (status == ZTM_PWM_FAN_TACH_STALL) {
        console_printf("\r\nFan %u: NO VALID TACH (count=0x%04X; fan may still be spinning)\r\n",
                       (unsigned)fan, (unsigned)count);
        return -4;
    }
    console_printf("\r\nFan %u: %lu RPM\r\n", (unsigned)fan, (unsigned long)rpm);
    return 0;
}

static int cmd_pwm_gpio_mode(int argc, char **argv, char *out, size_t out_len)
{
    (void)out; (void)out_len;
    uint8_t gpio = 0;
    if (argc != 4 || !parse_pwm_gpio(argv[2], &gpio)) {
        console_printf("\r\nUsage: pwm gpio-mode <1..4> <in|out>\r\n");
        return -1;
    }

    ztm_pwm_gpio_mode_t mode;
    if (strcmp(argv[3], "in") == 0 || strcmp(argv[3], "input") == 0) {
        mode = ZTM_PWM_GPIO_INPUT;
    } else if (strcmp(argv[3], "out") == 0 || strcmp(argv[3], "output") == 0) {
        mode = ZTM_PWM_GPIO_OUTPUT;
    } else {
        console_printf("\r\nUsage: pwm gpio-mode <1..4> <in|out>\r\n");
        return -2;
    }

    if (!ztm_pwm_gpio_configure(&ztm_pwm_1, gpio, mode)) {
        console_printf("\r\nFailed to configure GPIO%u.\r\n", (unsigned)gpio);
        return -3;
    }
    console_printf("\r\nGPIO%u (SX IO%u) configured as %s.\r\n",
                   (unsigned)gpio,
                   (unsigned)ztm_pwm_gpio_get_sx_pin(gpio),
                   mode == ZTM_PWM_GPIO_INPUT ? "input" : "output LOW");
    return 0;
}

static int cmd_pwm_gpio_write(int argc, char **argv, char *out, size_t out_len)
{
    (void)out; (void)out_len;
    uint8_t gpio = 0;
    uint32_t level = 0;
    if (argc != 4 || !parse_pwm_gpio(argv[2], &gpio) ||
        !parse_u32(argv[3], &level) || level > 1u) {
        console_printf("\r\nUsage: pwm gpio-write <1..4> <0|1>\r\n");
        return -1;
    }
    if (!ztm_pwm_gpio_write(&ztm_pwm_1, gpio, level != 0u)) {
        console_printf("\r\nFailed to write GPIO%u; configure it as output first.\r\n", (unsigned)gpio);
        return -2;
    }
    console_printf("\r\nGPIO%u = %u\r\n", (unsigned)gpio, (unsigned)level);
    return 0;
}

static int cmd_pwm_gpio_read(int argc, char **argv, char *out, size_t out_len)
{
    (void)out; (void)out_len;
    if (argc != 3) {
        console_printf("\r\nUsage: pwm gpio-read <1..4|all>\r\n");
        return -1;
    }

    if (selector_is_all(argv[2])) {
        console_printf("\r\nUser GPIO levels:\r\n");
        for (uint8_t gpio = 1u; gpio <= ZTM_PWM_GPIO_COUNT; gpio++) {
            bool level = false;
            if (ztm_pwm_gpio_read(&ztm_pwm_1, gpio, &level)) {
                console_printf("  GPIO%u (SX IO%u): %u\r\n",
                               (unsigned)gpio,
                               (unsigned)ztm_pwm_gpio_get_sx_pin(gpio),
                               level ? 1u : 0u);
            } else {
                console_printf("  GPIO%u: READ ERROR\r\n", (unsigned)gpio);
            }
        }
        return 0;
    }

    uint8_t gpio = 0;
    bool level = false;
    if (!parse_pwm_gpio(argv[2], &gpio)) {
        console_printf("\r\nUsage: pwm gpio-read <1..4|all>\r\n");
        return -2;
    }
    if (!ztm_pwm_gpio_read(&ztm_pwm_1, gpio, &level)) {
        console_printf("\r\nFailed to read GPIO%u.\r\n", (unsigned)gpio);
        return -3;
    }
    console_printf("\r\nGPIO%u (SX IO%u) = %u\r\n",
                   (unsigned)gpio,
                   (unsigned)ztm_pwm_gpio_get_sx_pin(gpio),
                   level ? 1u : 0u);
    return 0;
}

static int cmd_pwm_irq(int argc, char **argv, char *out, size_t out_len)
{
    (void)argc; (void)argv; (void)out; (void)out_len;
    int rc = ztm_pwm_scan_irq_sources(&ztm_pwm_1, true);
    if (rc < 0) {
        console_printf("\r\nPWM IRQ scan completed with %d read error(s).\r\n", -rc);
        return rc;
    }
    console_printf("\r\nPWM IRQ source scan complete; see any logged ERROR lines above.\r\n");
    return 0;
}

static const char *pwm_pull_name(ztm_pwm_gpio_pull_t pull)
{
    switch (pull) {
        case ZTM_PWM_GPIO_PULL_UP: return "up";
        case ZTM_PWM_GPIO_PULL_DOWN: return "down";
        default: return "none";
    }
}

static const char *pwm_irq_sense_name(sx1509_sense_t sense)
{
    switch (sense) {
        case SX1509_SENSE_RISING: return "rising";
        case SX1509_SENSE_FALLING: return "falling";
        case SX1509_SENSE_BOTH: return "both";
        default: return "off";
    }
}

static void print_pwm_measurement(uint8_t channel,
                                  const ztm_pwm_current_reading_t *reading)
{
    print_pwm_current_reading(channel, reading);
}

static const char *pwm_probe_pac_support(uint8_t product_id, uint8_t channels)
{
    if (product_id == 0x7Du && channels == 2u) {
        return "PAC1952-2 salvage supported";
    }
    if (product_id == 0x7Bu && channels == 4u) {
        return "PAC1954 native supported";
    }
    return "unsupported topology";
}

static const char *pwm_power_safe_status(int rc)
{
    switch (rc) {
        case 0: return "HIGH verified";
        case ZTM_PWM_ERR_POWER_SAFE_TIMEOUT: return "ERROR: timeout waiting for HIGH";
        case ZTM_PWM_ERR_POWER_SAFE_READ: return "ERROR: input read/configuration unavailable";
        case ZTM_PWM_ERR_POWER_SAFE_LOW: return "ERROR: went LOW";
        default: return "ERROR";
    }
}

static bool pwm_raw_access_ready(void)
{
    if (ztm_pwm_operational(&ztm_pwm_1)) return true;
    console_printf("\r\nPWM unavailable: initialize successfully with POWER_SAFE HIGH before raw access.\r\n");
    return false;
}

static int cmd_pwm_probe(int argc, char **argv, char *out, size_t out_len)
{
    (void)argc; (void)argv; (void)out; (void)out_len;

    ztm_pwm_probe_result_t probe = {0};
    int rc = ztm_pwm_probe_devices(&ztm_pwm_1, &probe, true);

    console_printf("\r\nPWM raw device probe (safe even when module init has failed):\r\n");
    console_printf("  Power control: %s; before=%s; during probe=%s; restored-off=%s\r\n",
                   probe.power_control_ok ? "OK" : "ERROR",
                   probe.power_was_enabled ? "ON" : "OFF/unknown",
                   probe.enable_read_ok ? (probe.enable_high ? "ON" : "OFF") : "READ ERROR",
                   probe.power_temporarily_enabled ? (probe.power_restored_off ? "yes" : "ERROR") : "not required");
    console_printf("  POWER_SAFE: %s (rc=%d)\r\n",
                   pwm_power_safe_status(probe.power_safe_rc), probe.power_safe_rc);
    if (probe.power_safe_rc != 0) return rc;
    console_printf("  Shared IRQ: %s\r\n",
                   probe.irq_read_ok ? (probe.irq_asserted ? "ASSERTED" : "inactive") : "READ ERROR");

    console_printf("  SX1509 0x%02X: %s",
                   ZTM_PWM_SX1509_ADDR,
                   probe.sx_present ? "ACK" : "NO RESPONSE");
    if (probe.sx_present) {
        console_printf(" DIR_A=0x%02X", probe.sx_dir_a);
    }
    console_printf("\r\n");

    static const uint8_t pac_addr[ZTM_PWM_PAC_COUNT] = {
        ZTM_PWM_PAC1_ADDR, ZTM_PWM_PAC2_ADDR, ZTM_PWM_PAC3_ADDR
    };
    for (uint8_t index = 0u; index < ZTM_PWM_PAC_COUNT; index++) {
        console_printf("  PAC%u 0x%02X: %s",
                       (unsigned)(index + 1u),
                       pac_addr[index],
                       probe.pac_present[index] ? "ACK" : "NO RESPONSE");
        if (probe.pac_present[index]) {
            console_printf(" MID=0x%02X PID=0x%02X REV=0x%02X channels=%u (%s)",
                           probe.pac_manufacturer_id[index],
                           probe.pac_product_id[index],
                           probe.pac_revision_id[index],
                           (unsigned)probe.pac_channels[index],
                           pwm_probe_pac_support(probe.pac_product_id[index],
                                                 probe.pac_channels[index]));
        }
        console_printf("\r\n");
    }

    static const uint8_t emc_addr[ZTM_PWM_EMC_COUNT] = {
        ZTM_PWM_EMC1_ADDR, ZTM_PWM_EMC2_ADDR
    };
    for (uint8_t index = 0u; index < ZTM_PWM_EMC_COUNT; index++) {
        console_printf("  EMC%u 0x%02X: %s",
                       (unsigned)(index + 1u),
                       emc_addr[index],
                       probe.emc_present[index] ? "ACK" : "NO RESPONSE");
        if (probe.emc_present[index]) {
            console_printf(" PID=0x%02X MID=0x%02X REV=0x%02X",
                           probe.emc_product_id[index],
                           probe.emc_manufacturer_id[index],
                           probe.emc_revision_id[index]);
        }
        console_printf("\r\n");
    }

    console_printf("  Result: %u of 6 expected I2C devices responded.\r\n",
                   (unsigned)probe.devices_found);
    if (ztm_pwm_1.last_init_rc != 0) {
        console_printf("  Last full init failure: stage=%s rc=%d\r\n",
                       ztm_pwm_init_stage_name(ztm_pwm_1.last_init_stage),
                       ztm_pwm_1.last_init_rc);
    }
    return rc;
}

static int cmd_pwm_i2c_scan(int argc, char **argv, char *out, size_t out_len)
{
    (void)argv; (void)out; (void)out_len;
    if (argc != 2) {
        console_printf("\r\nUsage: pwm i2c-scan\r\n");
        return -1;
    }

    ztm_pwm_i2c_scan_result_t scan = {0};
    int rc = ztm_pwm_i2c_scan(&ztm_pwm_1, &scan, true);
    console_printf("\r\nI2C0 scan with PWM module powered:\r\n  ACK addresses:");
    bool any = false;
    for (uint8_t address = 0x08u; address <= 0x77u; address++) {
        if (scan.address_present[address]) {
            console_printf(" 0x%02X", address);
            any = true;
        }
    }
    if (!any) {
        console_printf(" none");
    }
    console_printf("\r\n  Devices found: %u\r\n", (unsigned)scan.devices_found);
    console_printf("  Power: before=%s, temporary=%s, restored-off=%s\r\n",
                   scan.power_was_enabled ? "ON" : "OFF/unknown",
                   scan.power_temporarily_enabled ? "yes" : "no",
                   scan.power_temporarily_enabled ? (scan.power_restored_off ? "yes" : "ERROR") : "not required");
    console_printf("  POWER_SAFE: %s (rc=%d)\r\n",
                   pwm_power_safe_status(scan.power_safe_rc), scan.power_safe_rc);
    console_printf("  Note: scan uses a dummy read; run 'pwm power cycle' before normal operation.\r\n");
    return rc < 0 ? rc : 0;
}

static int cmd_pwm_power(int argc, char **argv, char *out, size_t out_len)
{
    (void)out; (void)out_len;
    if (argc != 3) {
        console_printf("\r\nUsage: pwm power <off|on|cycle>\r\n");
        return -1;
    }

    if (strcmp(argv[2], "off") == 0) {
        int rc = modules_deinitialize_by_short_name("ztm_pwm");
        console_printf("\r\nPWM module power %s.\r\n", rc == 0 ? "OFF" : "off requested with an error");
        return rc;
    }
    if (strcmp(argv[2], "on") == 0 || strcmp(argv[2], "cycle") == 0) {
        int rc = modules_reinitialize_by_short_name("ztm_pwm");
        console_printf("\r\nPWM module %s: %s",
                       strcmp(argv[2], "cycle") == 0 ? "power cycle" : "power on/reinitialize",
                       rc == 0 ? "READY" : "FAILED");
        if (rc != 0) {
            console_printf(" at %s (rc=%d)",
                           ztm_pwm_init_stage_name(ztm_pwm_1.last_init_stage),
                           ztm_pwm_1.last_init_rc);
        }
        console_printf(".\r\n");
        return rc;
    }

    console_printf("\r\nUsage: pwm power <off|on|cycle>\r\n");
    return -4;
}

static int cmd_pwm_measure(int argc, char **argv, char *out, size_t out_len)
{
    (void)out; (void)out_len;
    if (argc < 3 || argc > 4) {
        console_printf("\r\nUsage: pwm measure <1..12|all> [avg|instant]\r\n");
        return -1;
    }
    bool averaged = true;
    if (argc == 4) {
        if (strcmp(argv[3], "avg") == 0 || strcmp(argv[3], "averaged") == 0) averaged = true;
        else if (strcmp(argv[3], "instant") == 0 || strcmp(argv[3], "raw") == 0) averaged = false;
        else {
            console_printf("\r\nUsage: pwm measure <1..12|all> [avg|instant]\r\n");
            return -2;
        }
    }

    if (selector_is_all(argv[2])) {
        bool all_ok = true;
        console_printf("\r\nPWM channel %s measurements:\r\n", averaged ? "averaged" : "instantaneous");
        for (uint8_t channel = 1u; channel <= ZTM_PWM_CHANNEL_COUNT; channel++) {
            ztm_pwm_current_reading_t reading;
            if (ztm_pwm_read_channel(&ztm_pwm_1, channel, averaged, &reading)) {
                print_pwm_measurement(channel, &reading);
            } else {
                uint8_t pac = (uint8_t)(((channel - 1u) / 4u) + 1u);
                console_printf("  CH%u PAC%u: CURRENT UNAVAILABLE\r\n",
                               (unsigned)channel, (unsigned)pac);
                all_ok = false;
            }
        }
        return all_ok ? 0 : -3;
    }

    uint8_t channel = 0;
    ztm_pwm_current_reading_t reading;
    if (!parse_pwm_channel(argv[2], &channel)) {
        console_printf("\r\nUsage: pwm measure <1..12|all> [avg|instant]\r\n");
        return -4;
    }
    if (!ztm_pwm_read_channel(&ztm_pwm_1, channel, averaged, &reading)) {
        console_printf("\r\nFailed to measure PWM channel %u.\r\n", (unsigned)channel);
        return -5;
    }
    console_printf("\r\n");
    print_pwm_measurement(channel, &reading);
    return 0;
}

static int cmd_pwm_fan_raw(int argc, char **argv, char *out, size_t out_len)
{
    (void)out; (void)out_len;
    uint8_t fan = 0;
    uint32_t raw = 0;
    if (argc != 4 || !parse_pwm_fan(argv[2], &fan) || !parse_u32(argv[3], &raw) || raw > 255u) {
        console_printf("\r\nUsage: pwm fan-raw <1..6> <0..255>\r\n");
        return -1;
    }
    if (!ztm_pwm_fan_set_raw(&ztm_pwm_1, fan, (uint8_t)raw)) {
        console_printf("\r\nFailed to set raw fan drive.\r\n");
        return -2;
    }
    console_printf("\r\nFan %u open-loop raw drive = %lu/255.\r\n",
                   (unsigned)fan, (unsigned long)raw);
    return 0;
}

static int cmd_pwm_tach(int argc, char **argv, char *out, size_t out_len)
{
    (void)out; (void)out_len;
    if (argc != 3) {
        console_printf("\r\nUsage: pwm tach <1..6|all>\r\n");
        return -1;
    }
    uint8_t first = 1u, last = ZTM_PWM_FAN_COUNT;
    if (!selector_is_all(argv[2])) {
        if (!parse_pwm_fan(argv[2], &first)) {
            console_printf("\r\nUsage: pwm tach <1..6|all>\r\n");
            return -2;
        }
        last = first;
    }

    bool all_ok = true;
    console_printf("\r\nFan tach readings:\r\n");
    for (uint8_t fan = first; fan <= last; fan++) {
        uint16_t count = 0;
        uint32_t rpm = 0;
        uint8_t emc = 0, local = 0;
        (void)ztm_pwm_fan_get_route(fan, &emc, &local);
        ztm_pwm_fan_tach_status_t status =
            ztm_pwm_fan_get_tach_status(&ztm_pwm_1, fan, &count, &rpm);
        if (status == ZTM_PWM_FAN_TACH_OK) {
            console_printf("  Fan %u EMC%u/FAN%u: count=%u (0x%04X), %lu RPM\r\n",
                           (unsigned)fan, (unsigned)emc, (unsigned)local,
                           (unsigned)count, (unsigned)count, (unsigned long)rpm);
        } else if (status == ZTM_PWM_FAN_TACH_STALL) {
            console_printf("  Fan %u EMC%u/FAN%u: count=%u (0x%04X), NO VALID TACH (fan may still be spinning)\r\n",
                           (unsigned)fan, (unsigned)emc, (unsigned)local,
                           (unsigned)count, (unsigned)count);
            all_ok = false;
        } else {
            console_printf("  Fan %u: READ ERROR\r\n", (unsigned)fan);
            all_ok = false;
        }
    }
    return all_ok ? 0 : -3;
}

static int cmd_pwm_fan_target(int argc, char **argv, char *out, size_t out_len)
{
    (void)out; (void)out_len;
    uint8_t fan = 0;
    uint32_t rpm = 0;
    if (argc != 4 || !parse_pwm_fan(argv[2], &fan) || !parse_u32(argv[3], &rpm) || rpm == 0u) {
        console_printf("\r\nUsage: pwm fan-target <1..6> <rpm>\r\n");
        return -1;
    }
    if (!ztm_pwm_fan_set_target_rpm(&ztm_pwm_1, fan, rpm)) {
        console_printf("\r\nFailed to enable closed-loop RPM control for fan %u.\r\n", (unsigned)fan);
        return -2;
    }
    console_printf("\r\nFan %u closed-loop target = %lu RPM. Use 'pwm fan-open' to return to direct PWM.\r\n",
                   (unsigned)fan, (unsigned long)rpm);
    return 0;
}

static int cmd_pwm_fan_open(int argc, char **argv, char *out, size_t out_len)
{
    (void)out; (void)out_len;
    uint8_t fan = 0, duty = 0;
    if (argc != 4 || !parse_pwm_fan(argv[2], &fan) || !parse_percent(argv[3], &duty)) {
        console_printf("\r\nUsage: pwm fan-open <1..6> <0..100>\r\n");
        return -1;
    }
    if (!ztm_pwm_fan_set_open_loop(&ztm_pwm_1, fan, duty)) {
        console_printf("\r\nFailed to set fan %u open-loop PWM.\r\n", (unsigned)fan);
        return -2;
    }
    console_printf("\r\nFan %u open-loop PWM = %u%%.\r\n", (unsigned)fan, (unsigned)duty);
    return 0;
}

static int cmd_pwm_fan_status(int argc, char **argv, char *out, size_t out_len)
{
    if (!pwm_raw_access_ready()) return -1;
    (void)out; (void)out_len;
    uint8_t first = 1u, last = ZTM_PWM_EMC_COUNT;
    if (argc > 3) {
        console_printf("\r\nUsage: pwm fan-status [1|2|all]\r\n");
        return -1;
    }
    if (argc == 3 && !selector_is_all(argv[2])) {
        uint32_t index = 0;
        if (!parse_u32(argv[2], &index) || index < 1u || index > ZTM_PWM_EMC_COUNT) {
            console_printf("\r\nUsage: pwm fan-status [1|2|all]\r\n");
            return -2;
        }
        first = last = (uint8_t)index;
    }

    bool all_ok = true;
    console_printf("\r\nEMC2303 status (reads may clear latched status):\r\n");
    for (uint8_t index = first; index <= last; index++) {
        uint8_t fs = 0, stall = 0, spin = 0, fail = 0;
        int rc = emc2303_read_status(&ztm_pwm_1.fan_controller[index - 1u],
                                     &fs, &stall, &spin, &fail);
        console_printf("  EMC%u: %s FAN_STATUS=%02X STALL=%02X SPIN=%02X DRIVE_FAIL=%02X\r\n",
                       (unsigned)index,
                       rc == EMC2303_OK ? "OK" : "READ ERROR",
                       fs, stall, spin, fail);
        all_ok = all_ok && rc == EMC2303_OK;
    }
    return all_ok ? 0 : -3;
}

static int cmd_pwm_gpio_pull(int argc, char **argv, char *out, size_t out_len)
{
    (void)out; (void)out_len;
    uint8_t gpio = 0;
    if (argc != 4 || !parse_pwm_gpio(argv[2], &gpio)) {
        console_printf("\r\nUsage: pwm gpio-pull <1..4> <none|up|down>\r\n");
        return -1;
    }
    ztm_pwm_gpio_pull_t pull;
    if (strcmp(argv[3], "none") == 0 || strcmp(argv[3], "off") == 0) pull = ZTM_PWM_GPIO_PULL_NONE;
    else if (strcmp(argv[3], "up") == 0) pull = ZTM_PWM_GPIO_PULL_UP;
    else if (strcmp(argv[3], "down") == 0) pull = ZTM_PWM_GPIO_PULL_DOWN;
    else {
        console_printf("\r\nUsage: pwm gpio-pull <1..4> <none|up|down>\r\n");
        return -2;
    }
    if (!ztm_pwm_gpio_configure_input(&ztm_pwm_1, gpio, pull)) {
        console_printf("\r\nFailed to configure GPIO%u input pull.\r\n", (unsigned)gpio);
        return -3;
    }
    console_printf("\r\nGPIO%u input pull = %s.\r\n", (unsigned)gpio, pwm_pull_name(pull));
    return 0;
}

static int cmd_pwm_gpio_irq(int argc, char **argv, char *out, size_t out_len)
{
    (void)out; (void)out_len;
    uint8_t gpio = 0;
    if (argc != 4 || !parse_pwm_gpio(argv[2], &gpio)) {
        console_printf("\r\nUsage: pwm gpio-irq <1..4> <off|rising|falling|both>\r\n");
        return -1;
    }
    sx1509_sense_t sense;
    if (strcmp(argv[3], "off") == 0 || strcmp(argv[3], "none") == 0) sense = SX1509_SENSE_NONE;
    else if (strcmp(argv[3], "rising") == 0 || strcmp(argv[3], "rise") == 0) sense = SX1509_SENSE_RISING;
    else if (strcmp(argv[3], "falling") == 0 || strcmp(argv[3], "fall") == 0) sense = SX1509_SENSE_FALLING;
    else if (strcmp(argv[3], "both") == 0) sense = SX1509_SENSE_BOTH;
    else {
        console_printf("\r\nUsage: pwm gpio-irq <1..4> <off|rising|falling|both>\r\n");
        return -2;
    }
    if (!ztm_pwm_gpio_configure_irq(&ztm_pwm_1, gpio, sense)) {
        console_printf("\r\nFailed to configure GPIO%u interrupt; it must be an input.\r\n", (unsigned)gpio);
        return -3;
    }
    console_printf("\r\nGPIO%u interrupt = %s.\r\n", (unsigned)gpio, pwm_irq_sense_name(sense));
    return 0;
}

static int cmd_pwm_gpio_toggle(int argc, char **argv, char *out, size_t out_len)
{
    (void)out; (void)out_len;
    uint8_t gpio = 0;
    if (argc != 3 || !parse_pwm_gpio(argv[2], &gpio)) {
        console_printf("\r\nUsage: pwm gpio-toggle <1..4>\r\n");
        return -1;
    }
    if (!ztm_pwm_gpio_toggle(&ztm_pwm_1, gpio)) {
        console_printf("\r\nFailed to toggle GPIO%u; configure it as output first.\r\n", (unsigned)gpio);
        return -2;
    }
    bool level = false;
    (void)ztm_pwm_gpio_read(&ztm_pwm_1, gpio, &level);
    console_printf("\r\nGPIO%u toggled to %u.\r\n", (unsigned)gpio, level ? 1u : 0u);
    return 0;
}

static int cmd_pwm_irq_line(int argc, char **argv, char *out, size_t out_len)
{
    (void)argc; (void)argv; (void)out; (void)out_len;
    bool asserted = false;
    if (!ztm_pwm_read_irq_asserted(&ztm_pwm_1, &asserted)) {
        console_printf("\r\nFailed to read shared PWM IRQ line.\r\n");
        return -1;
    }
    console_printf("\r\nPWM shared IRQ line: %s (active-low).\r\n",
                   asserted ? "ASSERTED/LOW" : "inactive/HIGH");
    return 0;
}

static int cmd_pwm_sx_read(int argc, char **argv, char *out, size_t out_len)
{
    if (!pwm_raw_access_ready()) return -1;
    (void)out; (void)out_len;
    uint32_t reg = 0, count = 1;
    if (argc < 3 || argc > 4 || !parse_u32(argv[2], &reg) || reg > 0x7Fu ||
        (argc == 4 && (!parse_u32(argv[3], &count) || count < 1u || count > 16u || reg + count > 0x80u))) {
        console_printf("\r\nUsage: pwm sx-read <reg 0x00..0x7F> [count 1..16]\r\n");
        return -1;
    }
    uint8_t data[16] = {0};
    int rc = sx1509_reg_read_n(&ztm_pwm_1.output_mux, (uint8_t)reg, data, count);
    if (rc != SX1509_OK) {
        console_printf("\r\nSX1509 register read failed, rc=%d.\r\n", rc);
        return -2;
    }
    console_printf("\r\nSX1509 0x%02lX:", (unsigned long)reg);
    for (uint32_t i = 0; i < count; i++) console_printf(" %02X", data[i]);
    console_printf("\r\n");
    return 0;
}

static int cmd_pwm_sx_write(int argc, char **argv, char *out, size_t out_len)
{
    if (!pwm_raw_access_ready()) return -1;
    (void)out; (void)out_len;
    uint32_t reg = 0, value = 0;
    if (argc != 4 || !parse_u32(argv[2], &reg) || reg > 0x7Du ||
        !parse_u32(argv[3], &value) || value > 0xFFu) {
        console_printf("\r\nUsage: pwm sx-write <reg 0x00..0x7D> <byte>\r\n");
        return -1;
    }
    int rc = sx1509_reg_write8(&ztm_pwm_1.output_mux, (uint8_t)reg, (uint8_t)value);
    console_printf("\r\nSX1509 write %02lX=%02lX: %s. Reinitialize after raw configuration changes.\r\n",
                   (unsigned long)reg, (unsigned long)value,
                   rc == SX1509_OK ? "OK" : "FAILED");
    return rc == SX1509_OK ? 0 : -2;
}

static bool parse_pwm_device_index(const char *text, uint8_t max, uint8_t *index_1based)
{
    uint32_t value = 0;
    if (!index_1based || !parse_u32(text, &value) || value < 1u || value > max) return false;
    *index_1based = (uint8_t)value;
    return true;
}

static int cmd_pwm_pac_read(int argc, char **argv, char *out, size_t out_len)
{
    if (!pwm_raw_access_ready()) return -1;
    (void)out; (void)out_len;
    uint8_t index = 0;
    uint32_t reg = 0, count = 0;
    if (argc != 5 || !parse_pwm_device_index(argv[2], ZTM_PWM_PAC_COUNT, &index) ||
        !parse_u32(argv[3], &reg) || reg > 0xFFu ||
        !parse_u32(argv[4], &count) || count < 1u || count > 8u) {
        console_printf("\r\nUsage: pwm pac-read <1..3> <reg> <count 1..8>\r\n");
        return -1;
    }
    uint8_t data[8] = {0};
    int rc = pac195x_reg_read(&ztm_pwm_1.current_monitor[index - 1u], (uint8_t)reg, data, count);
    if (rc != PAC195X_OK) {
        console_printf("\r\nPAC%u register read failed, rc=%d.\r\n", (unsigned)index, rc);
        return -2;
    }
    console_printf("\r\nPAC%u 0x%02lX:", (unsigned)index, (unsigned long)reg);
    for (uint32_t i = 0; i < count; i++) console_printf(" %02X", data[i]);
    console_printf("\r\n");
    return 0;
}

static int cmd_pwm_pac_write(int argc, char **argv, char *out, size_t out_len)
{
    if (!pwm_raw_access_ready()) return -1;
    (void)out; (void)out_len;
    uint8_t index = 0;
    uint32_t reg = 0;
    if (argc < 5 || argc > 12 || !parse_pwm_device_index(argv[2], ZTM_PWM_PAC_COUNT, &index) ||
        !parse_u32(argv[3], &reg) || reg > 0xFFu) {
        console_printf("\r\nUsage: pwm pac-write <1..3> <reg> <byte0> [byte1..byte7]\r\n");
        return -1;
    }
    uint8_t data[8];
    uint32_t count = (uint32_t)(argc - 4);
    for (uint32_t i = 0; i < count; i++) {
        uint32_t value = 0;
        if (!parse_u32(argv[i + 4], &value) || value > 0xFFu) {
            console_printf("\r\nEvery PAC write value must be a byte.\r\n");
            return -2;
        }
        data[i] = (uint8_t)value;
    }
    int rc = pac195x_reg_write(&ztm_pwm_1.current_monitor[index - 1u], (uint8_t)reg, data, count);
    console_printf("\r\nPAC%u raw write: %s. Use 'pwm power cycle' to restore module defaults.\r\n",
                   (unsigned)index, rc == PAC195X_OK ? "OK" : "FAILED");
    return rc == PAC195X_OK ? 0 : -3;
}

static int cmd_pwm_emc_read(int argc, char **argv, char *out, size_t out_len)
{
    if (!pwm_raw_access_ready()) return -1;
    (void)out; (void)out_len;
    uint8_t index = 0;
    uint32_t reg = 0;
    if (argc != 4 || !parse_pwm_device_index(argv[2], ZTM_PWM_EMC_COUNT, &index) ||
        !parse_u32(argv[3], &reg) || reg > 0xFFu) {
        console_printf("\r\nUsage: pwm emc-read <1..2> <reg>\r\n");
        return -1;
    }
    uint8_t value = 0;
    int rc = emc2303_reg_read8(&ztm_pwm_1.fan_controller[index - 1u], (uint8_t)reg, &value);
    if (rc != EMC2303_OK) {
        console_printf("\r\nEMC%u register read failed, rc=%d.\r\n", (unsigned)index, rc);
        return -2;
    }
    console_printf("\r\nEMC%u reg 0x%02lX = 0x%02X\r\n",
                   (unsigned)index, (unsigned long)reg, value);
    return 0;
}

static int cmd_pwm_emc_write(int argc, char **argv, char *out, size_t out_len)
{
    if (!pwm_raw_access_ready()) return -1;
    (void)out; (void)out_len;
    uint8_t index = 0;
    uint32_t reg = 0, value = 0;
    if (argc != 5 || !parse_pwm_device_index(argv[2], ZTM_PWM_EMC_COUNT, &index) ||
        !parse_u32(argv[3], &reg) || reg > 0xFFu ||
        !parse_u32(argv[4], &value) || value > 0xFFu) {
        console_printf("\r\nUsage: pwm emc-write <1..2> <reg> <byte>\r\n");
        return -1;
    }
    int rc = emc2303_reg_write8(&ztm_pwm_1.fan_controller[index - 1u],
                                (uint8_t)reg,
                                (uint8_t)value);
    console_printf("\r\nEMC%u raw write: %s. Use 'pwm power cycle' to restore module defaults.\r\n",
                   (unsigned)index, rc == EMC2303_OK ? "OK" : "FAILED");
    return rc == EMC2303_OK ? 0 : -2;
}


/* Compatibility alias: old raw 0..255 value becomes an all-channel percentage. */
static int cmd_pwm_start(int argc, char **argv, char *out, size_t out_len)
{
    (void)out; (void)out_len;
    uint32_t raw = 0;
    if (argc != 3 || !parse_u32(argv[2], &raw) || raw > 255u) {
        console_printf("\r\nUsage: pwm start <0..255>\r\n");
        return -1;
    }
    uint8_t duty = (uint8_t)((raw * 100u + 127u) / 255u);
    if (!ztm_pwm_channel_set_all(&ztm_pwm_1, duty)) {
        console_printf("\r\nPWM module is not ready.\r\n");
        return -2;
    }
    console_printf("\r\nCompatibility command: all low-side channels = %u%% (raw %lu).\r\n",
                   (unsigned)duty, (unsigned long)raw);
    return 0;
}


/* =========================================================
   High-voltage PSU diagnostics and bench controls
   ========================================================= */

static void print_e60_estimate(uint32_t drive_uV,
                               uint32_t estimated_hv_mV,
                               bool input_overvoltage)
{
    console_printf("  E60 drive input: %.6f V; estimated |HV|: ~%.3f kV (nominal/full-load)\r\n",
                   (double)drive_uV / 1000000.0,
                   (double)estimated_hv_mV / 1000000.0);
    if (drive_uV > 0u && drive_uV < ZTM_PSU_HV_E60_MIN_LINEAR_INPUT_UV) {
        console_printf("  Note: E60 proportional operation is specified from approximately 0.7 V input.\r\n");
    }
    if (input_overvoltage) {
        console_printf("  ERROR: E60 input exceeds its 15 V rating.\r\n");
    }
    console_printf("  Estimate is indirect; actual light/no-load HV can be higher than nominal.\r\n");
}

static void print_hv_snapshot(const ztm_psu_hv_snapshot_t *snapshot)
{
    if (!snapshot) {
        return;
    }
    console_printf("  EN: %s%s; LM5164 PG: %s%s\r\n",
                   snapshot->enable_valid ? "" : "READ ERROR/",
                   snapshot->enabled ? "HIGH (ON)" : "LOW (OFF)",
                   snapshot->pgood_valid ? "" : "READ ERROR/",
                   snapshot->pgood ? "HIGH (GOOD)" : "LOW (NOT GOOD)");
    if (snapshot->rdac_readback_valid) {
        console_printf("  AD5282 readback: RDAC1=%u, RDAC2=%u\r\n",
                       (unsigned)snapshot->rdac1_code,
                       (unsigned)snapshot->rdac2_code);
    } else {
        console_printf("  AD5282 readback: FAILED\r\n");
    }
    if (snapshot->vsense_valid) {
        console_printf("  HV_VSENSE_1: AIN%u raw=%u (measures LM5164/E60 input via 130k/20k divider)\r\n",
                       (unsigned)ZTM_PSU_HV_VSENSE_CHANNEL,
                       (unsigned)snapshot->vsense_adc_code);
        print_e60_estimate(snapshot->drive_uV,
                           snapshot->estimated_hv_mV,
                           snapshot->e60_input_overvoltage);
    } else {
        console_printf("  HV_VSENSE_1: READ FAILED\r\n");
    }
}

static int cmd_hv_info(int argc, char **argv, char *out, size_t out_len)
{
    (void)argc; (void)argv; (void)out; (void)out_len;
    ztm_psu_hv_t *device = &ztm_psu_hv_1;
    uint8_t address = device->i2c_address ? device->i2c_address : AD5282_ADDR(0u);

    console_printf("\r\nHV PSU module: %s\r\n", ztm_psu_hv_ready(&ztm_psu_hv_1) ? "READY" : "NOT READY");
    console_printf("  AD5282: I2C0 address 0x%02X; ON uses RDAC1=0/RDAC2=setpoint, OFF uses 255/255\r\n",
                   (unsigned)address);
    console_printf("  LM5164: EN=PCAL6416 P1.6 (EX pin %d), PG=PCAL6416 P1.5 (EX pin %d)\r\n",
                   EX_HV_EN_1,
                   EX_HV_PGOOD_1);
    console_printf("  Load: LM5164 drives XP Power E60 input; E60 nominal transfer is 15 V -> 6 kV\r\n");
    console_printf("  Feedback: TLA2518 AIN%u measures E60 input through 130k/20k divider (7.5:1)\r\n",
                   (unsigned)ZTM_PSU_HV_VSENSE_CHANNEL);
    console_printf("  PG proves only the LM5164 drive supply, not the actual E60 high-voltage output\r\n");
    console_printf("  Safety: EN is LOW by default; OFF state also sets RDAC1/RDAC2 to 255/255 (minimum requested drive)\r\n");
    console_printf("  RDAC direction: lower RDAC2 code requests higher LM5164/E60 drive\r\n");
    console_printf("  Guarded code range=%u..255; code 55 measured 13.6956 V, code 45 measured 16.33 V and was rejected\r\n",
                   (unsigned)ZTM_PSU_HV_LOWEST_SAFE_CODE);
    console_printf("  Calibrated nominal range: about %.3f..%.3f kV (codes 255..55); actual HV remains load-dependent\r\n",
                   (double)ZTM_PSU_HV_CAL_MIN_ESTIMATED_HV_V / 1000.0,
                   (double)ZTM_PSU_HV_CAL_MAX_ESTIMATED_HV_V / 1000.0);
    console_printf("  Staged code=%u; cached RDAC1=%u RDAC2=%u\r\n",
                   (unsigned)device->staged_code,
                   (unsigned)device->rdac1_code,
                   (unsigned)device->rdac2_code);
    console_printf("  Last init: stage=%s rc=%d; last operation: stage=%s rc=%d\r\n",
                   ztm_psu_hv_init_stage_name(device->last_init_stage),
                   device->last_init_rc,
                   ztm_psu_hv_operation_stage_name(device->last_operation_stage),
                   device->last_operation_rc);
    console_printf("  Polls=%lu, faults=%lu (PG=%lu, VSENSE=%lu, E60 overvoltage=%lu)\r\n",
                   (unsigned long)device->poll_count,
                   (unsigned long)device->fault_events,
                   (unsigned long)device->pgood_fault_events,
                   (unsigned long)device->vsense_fault_events,
                   (unsigned long)device->e60_overvoltage_fault_events);
    if (device->last_start_code != 0u ||
        device->last_operation_stage == ZTM_PSU_HV_OP_VALIDATE_CODE ||
        device->last_start_wait_ms != 0u) {
        console_printf("  Last start: code=%u, waited=%lu ms, PG=%s%s",
                       (unsigned)device->last_start_code,
                       (unsigned long)device->last_start_wait_ms,
                       device->last_start_pgood_read_valid ? "" : "READ ERROR/",
                       device->last_start_pgood ? "GOOD" : "NOT GOOD");
        if (device->last_start_vsense_valid) {
            console_printf(", live=%.6f V, peak=%.6f V\r\n",
                           (double)device->last_start_drive_uV / 1000000.0,
                           (double)device->last_start_peak_drive_uV / 1000000.0);
        } else {
            console_printf(", live VSENSE unavailable\r\n");
        }
    }

    if (device->config.i2c) {
        ztm_psu_hv_snapshot_t snapshot;
        (void)ztm_psu_hv_read_snapshot(device, &snapshot);
        print_hv_snapshot(&snapshot);
    }
    return 0;
}

static int cmd_hv_probe(int argc, char **argv, char *out, size_t out_len)
{
    (void)argc; (void)argv; (void)out; (void)out_len;
    ztm_psu_hv_probe_result_t result;
    int rc = ztm_psu_hv_probe(&ztm_psu_hv_1, &result);

    console_printf("\r\nHV PSU safe probe (converter forced OFF):\r\n");
    console_printf("  Control pins: %s; EN forced low: %s\r\n",
                   result.controls_prepared ? "OK" : "FAILED",
                   result.forced_off ? "yes" : "no");
    console_printf("  AD5282 0x%02X: %s\r\n",
                   (unsigned)result.i2c_address,
                   result.ad5282_present ? "ACK" : "NO RESPONSE");
    if (result.rdac_readback_valid) {
        console_printf("  RDAC readback: RDAC1=%u RDAC2=%u\r\n",
                       (unsigned)result.rdac1_code,
                       (unsigned)result.rdac2_code);
    } else {
        console_printf("  RDAC readback: unavailable\r\n");
    }
    console_printf("  EN input read: %s%s; PG: %s%s\r\n",
                   result.enable_valid ? "" : "ERROR/",
                   result.enabled ? "HIGH" : "LOW",
                   result.pgood_valid ? "" : "ERROR/",
                   result.pgood ? "HIGH" : "LOW");
    if (result.vsense_valid) {
        console_printf("  VSENSE AIN%u raw=%u (LM5164/E60 input)\r\n",
                       (unsigned)ZTM_PSU_HV_VSENSE_CHANNEL,
                       (unsigned)result.vsense_adc_code);
        print_e60_estimate(result.drive_uV,
                           result.estimated_hv_mV,
                           result.e60_input_overvoltage);
    } else {
        console_printf("  VSENSE: read failed\r\n");
    }
    return rc == 0 ? 0 : rc;
}

static int cmd_hv_set(int argc, char **argv, char *out, size_t out_len)
{
    (void)out; (void)out_len;
    uint32_t code = 0u;
    if (argc != 3 || !parse_u32(argv[2], &code) || code > 255u) {
        console_printf("\r\nUsage: hv set <0..255>\r\n");
        return -1;
    }
    if (!ztm_psu_hv_stage_code(&ztm_psu_hv_1, (uint8_t)code)) {
        console_printf("\r\nHV setpoint staging FAILED at %s (rc=%d); output is OFF.\r\n",
                       ztm_psu_hv_operation_stage_name(ztm_psu_hv_1.last_operation_stage),
                       ztm_psu_hv_1.last_operation_rc);
        return -2;
    }
    console_printf("\r\nHV setpoint code %lu staged and verified; converter remains OFF.\r\n",
                   (unsigned long)code);
    return 0;
}

static int cmd_hv_on(int argc, char **argv, char *out, size_t out_len)
{
    (void)out; (void)out_len;
    uint32_t code = 0u;
    if (argc != 3 || !parse_u32(argv[2], &code) || code > 255u) {
        console_printf("\r\nUsage: hv on <%u..255>\r\n",
                       (unsigned)ZTM_PSU_HV_LOWEST_TESTED_CODE);
        console_printf("Use 'hv set <0..255>' to test RDAC readback while EN remains low.\r\n");
        return -1;
    }

    if (!ztm_psu_hv_start(&ztm_psu_hv_1,
                          (uint8_t)code,
                          ZTM_PSU_HV_DEFAULT_PGOOD_TIMEOUT_MS)) {
        if (ztm_psu_hv_1.last_operation_stage == ZTM_PSU_HV_OP_VALIDATE_CODE) {
            console_printf("\r\nHV code %lu is below the lowest characterised code of %u.\r\n",
                           (unsigned long)code,
                           (unsigned)ZTM_PSU_HV_LOWEST_TESTED_CODE);
            console_printf("This feedback network is inverse: lower codes request MORE output. Code 55 measured 13.6956 V; code 45 produced 16.33 V and was rejected. Output remains OFF.\r\n");
            return -2;
        }

        console_printf("\r\nHV start FAILED at %s (rc=%d); converter has been disabled.\r\n",
                       ztm_psu_hv_operation_stage_name(ztm_psu_hv_1.last_operation_stage),
                       ztm_psu_hv_1.last_operation_rc);
        console_printf("  Attempt: code=%u, waited=%lu ms, PG=%s%s\r\n",
                       (unsigned)ztm_psu_hv_1.last_start_code,
                       (unsigned long)ztm_psu_hv_1.last_start_wait_ms,
                       ztm_psu_hv_1.last_start_pgood_read_valid ? "" : "READ ERROR/",
                       ztm_psu_hv_1.last_start_pgood ? "GOOD" : "NOT GOOD");
        if (ztm_psu_hv_1.last_start_vsense_valid) {
            uint32_t last_estimate =
                ztm_psu_hv_estimate_e60_output_mV(ztm_psu_hv_1.last_start_drive_uV);
            console_printf("  Last live VSENSE while EN was high: AIN%u raw=%u\r\n",
                           (unsigned)ZTM_PSU_HV_VSENSE_CHANNEL,
                           (unsigned)ztm_psu_hv_1.last_start_vsense_adc_code);
            print_e60_estimate(ztm_psu_hv_1.last_start_drive_uV,
                               last_estimate,
                               !ztm_psu_hv_e60_input_within_rating(
                                   ztm_psu_hv_1.last_start_drive_uV));
            console_printf("  Peak drive observed during startup: %.6f V\r\n",
                           (double)ztm_psu_hv_1.last_start_peak_drive_uV / 1000000.0);
        } else {
            console_printf("  No valid VSENSE sample was captured while EN was high.\r\n");
        }
        return -2;
    }

    console_printf("\r\nHV converter ON: code=%lu, LM5164 PG=GOOD after %lu ms.\r\n",
                   (unsigned long)code,
                   (unsigned long)ztm_psu_hv_1.last_start_wait_ms);
    print_e60_estimate(ztm_psu_hv_1.drive_uV,
                       ztm_psu_hv_1.estimated_hv_mV,
                       false);
    return 0;
}

static int cmd_hv_map(int argc, char **argv, char *out, size_t out_len)
{
    (void)out; (void)out_len;
    if (argc == 2) {
        console_printf("\r\nMeasured/interpolated HV calibration map:\r\n");
        console_printf("  code   E60 input    nominal |HV|\r\n");
        for (int code = (int)ZTM_PSU_HV_HIGHEST_SAFE_CODE;
             code >= (int)ZTM_PSU_HV_LOWEST_SAFE_CODE;
             code -= 10) {
            uint32_t drive_uV = 0u;
            (void)ztm_psu_hv_calibrated_drive_for_code((uint8_t)code, &drive_uV);
            uint32_t hv_mV = ztm_psu_hv_estimate_e60_output_mV(drive_uV);
            console_printf("  %4d   %8.4f V    %7.3f kV\r\n",
                           code,
                           (double)drive_uV / 1000000.0,
                           (double)hv_mV / 1000000.0);
        }
        console_printf("  Values between measured points use linear interpolation.\r\n");
        return 0;
    }

    uint32_t code = 0u;
    if (argc != 3 || !parse_u32(argv[2], &code) ||
        code < ZTM_PSU_HV_LOWEST_SAFE_CODE || code > 255u) {
        console_printf("\r\nUsage: hv map [%u..255]\r\n",
                       (unsigned)ZTM_PSU_HV_LOWEST_SAFE_CODE);
        return -1;
    }
    uint32_t drive_uV = 0u;
    if (!ztm_psu_hv_calibrated_drive_for_code((uint8_t)code, &drive_uV)) {
        return -2;
    }
    uint32_t hv_mV = ztm_psu_hv_estimate_e60_output_mV(drive_uV);
    console_printf("\r\nCode %lu calibration: E60 input ~%.6f V, nominal |HV| ~%.3f kV.\r\n",
                   (unsigned long)code,
                   (double)drive_uV / 1000000.0,
                   (double)hv_mV / 1000000.0);
    return 0;
}

static int cmd_hv_on_est(int argc, char **argv, char *out, size_t out_len)
{
    (void)out; (void)out_len;
    uint32_t target_hv_V = 0u;
    if (argc != 3 || !parse_u32(argv[2], &target_hv_V) ||
        target_hv_V < ZTM_PSU_HV_CAL_MIN_ESTIMATED_HV_V ||
        target_hv_V > ZTM_PSU_HV_CAL_MAX_ESTIMATED_HV_V) {
        console_printf("\r\nUsage: hv on-est <%u..%u nominal HV volts>\r\n",
                       (unsigned)ZTM_PSU_HV_CAL_MIN_ESTIMATED_HV_V,
                       (unsigned)ZTM_PSU_HV_CAL_MAX_ESTIMATED_HV_V);
        console_printf("This is an indirect full-load estimate, not a direct HV measurement.\r\n");
        return -1;
    }

    uint8_t code = 0u;
    uint32_t calibrated_drive_uV = 0u;
    uint32_t calibrated_hv_mV = 0u;
    if (!ztm_psu_hv_select_code_for_estimated_hv(target_hv_V,
                                                  &code,
                                                  &calibrated_drive_uV,
                                                  &calibrated_hv_mV)) {
        return -2;
    }

    console_printf("\r\nNominal target %lu V selects conservative code %u ",
                   (unsigned long)target_hv_V,
                   (unsigned)code);
    console_printf("(calibrated %.4f V drive, ~%.3f kV).\r\n",
                   (double)calibrated_drive_uV / 1000000.0,
                   (double)calibrated_hv_mV / 1000000.0);

    if (!ztm_psu_hv_start(&ztm_psu_hv_1,
                          code,
                          ZTM_PSU_HV_DEFAULT_PGOOD_TIMEOUT_MS)) {
        console_printf("HV start FAILED at %s (rc=%d); converter has been disabled.\r\n",
                       ztm_psu_hv_operation_stage_name(ztm_psu_hv_1.last_operation_stage),
                       ztm_psu_hv_1.last_operation_rc);
        if (ztm_psu_hv_1.last_start_vsense_valid) {
            uint32_t actual_hv_mV =
                ztm_psu_hv_estimate_e60_output_mV(ztm_psu_hv_1.last_start_drive_uV);
            print_e60_estimate(ztm_psu_hv_1.last_start_drive_uV,
                               actual_hv_mV,
                               !ztm_psu_hv_e60_input_within_rating(
                                   ztm_psu_hv_1.last_start_drive_uV));
        }
        return -3;
    }

    console_printf("HV converter ON: code=%u, LM5164 PG=GOOD after %lu ms.\r\n",
                   (unsigned)code,
                   (unsigned long)ztm_psu_hv_1.last_start_wait_ms);
    print_e60_estimate(ztm_psu_hv_1.drive_uV,
                       ztm_psu_hv_1.estimated_hv_mV,
                       false);
    return 0;
}

static int cmd_hv_sweep(int argc, char **argv, char *out, size_t out_len)
{
    (void)out; (void)out_len;
    uint32_t start = 255u;
    uint32_t end = ZTM_PSU_HV_LOWEST_TESTED_CODE;
    uint32_t step = 10u;

    if (argc > 5 ||
        (argc >= 3 && (!parse_u32(argv[2], &start) || start > 255u)) ||
        (argc >= 4 && (!parse_u32(argv[3], &end) || end > 255u)) ||
        (argc == 5 && (!parse_u32(argv[4], &step) || step == 0u || step > 64u)) ||
        start < end || end < ZTM_PSU_HV_LOWEST_TESTED_CODE) {
        console_printf("\r\nUsage: hv sweep [start-code [end-code [step]]]\r\n");
        console_printf("Codes are tested from high to low because lower code requests higher voltage.\r\n");
        console_printf("Allowed: 255 >= start >= end >= %u, step 1..64.\r\n",
                       (unsigned)ZTM_PSU_HV_LOWEST_TESTED_CODE);
        return -1;
    }

    console_printf("\r\nHV guarded code sweep: %lu down to %lu, step %lu\r\n",
                   (unsigned long)start,
                   (unsigned long)end,
                   (unsigned long)step);
    console_printf("  Each point starts from OFF, waits for PG, samples VSENSE, then turns OFF.\r\n");
    console_printf("  Sweep stops at %.3f V drive or on any fault; E60 remains OFF between points.\r\n",
                   (double)ZTM_PSU_HV_SWEEP_STOP_INPUT_UV / 1000000.0);
    console_printf("  code   PG wait    E60 input    estimated |HV|\r\n");

    int result = 0;
    uint32_t code = start;
    while (true) {
        bool ok = ztm_psu_hv_start(&ztm_psu_hv_1,
                                   (uint8_t)code,
                                   ZTM_PSU_HV_DEFAULT_PGOOD_TIMEOUT_MS);
        if (ok) {
            console_printf("  %4lu   %3lu ms    %8.4f V    %7.3f kV\r\n",
                           (unsigned long)code,
                           (unsigned long)ztm_psu_hv_1.last_start_wait_ms,
                           (double)ztm_psu_hv_1.drive_uV / 1000000.0,
                           (double)ztm_psu_hv_1.estimated_hv_mV / 1000000.0);
            uint32_t measured = ztm_psu_hv_1.drive_uV;
            (void)ztm_psu_hv_stop(&ztm_psu_hv_1);
            if (measured >= ZTM_PSU_HV_SWEEP_STOP_INPUT_UV) {
                console_printf("  Stopped at the guarded %.3f V limit.\r\n",
                               (double)ZTM_PSU_HV_SWEEP_STOP_INPUT_UV / 1000000.0);
                break;
            }
        } else {
            bool low_unregulated =
                ztm_psu_hv_1.last_operation_stage == ZTM_PSU_HV_OP_WAIT_PGOOD &&
                ztm_psu_hv_1.last_start_vsense_valid &&
                ztm_psu_hv_1.last_start_peak_drive_uV <
                    ZTM_PSU_HV_SWEEP_STOP_INPUT_UV;

            console_printf("  %4lu   %s at %s (rc=%d)",
                           (unsigned long)code,
                           low_unregulated ? "NO PG" : "FAILED",
                           ztm_psu_hv_operation_stage_name(
                               ztm_psu_hv_1.last_operation_stage),
                           ztm_psu_hv_1.last_operation_rc);
            if (ztm_psu_hv_1.last_start_vsense_valid) {
                console_printf(", live %.4f V, peak %.4f V",
                               (double)ztm_psu_hv_1.last_start_drive_uV / 1000000.0,
                               (double)ztm_psu_hv_1.last_start_peak_drive_uV / 1000000.0);
            }
            console_printf("\r\n");
            (void)ztm_psu_hv_stop(&ztm_psu_hv_1);

            /* A high code may request an output below the usable regulation
             * range. Continue downward until PGOOD appears. Other failures stop. */
            if (!low_unregulated) {
                result = -2;
                break;
            }
        }

        if (code == end) {
            break;
        }
        uint32_t next = code > step ? code - step : 0u;
        code = next < end ? end : next;
        busy_wait_ms(100u);
    }

    (void)ztm_psu_hv_stop(&ztm_psu_hv_1);
    console_printf("HV sweep complete; converter OFF.\r\n");
    return result;
}

static int cmd_hv_off(int argc, char **argv, char *out, size_t out_len)
{
    (void)argc; (void)argv; (void)out; (void)out_len;
    bool ok = ztm_psu_hv_stop(&ztm_psu_hv_1);
    bool enabled = true;
    bool enable_read = ztm_psu_hv_read_enable(&ztm_psu_hv_1, &enabled);
    console_printf("\r\nHV converter EN is %s.%s\r\n",
                   enable_read && !enabled ? "LOW (OFF)" : "NOT CONFIRMED LOW",
                   ok ? " RDACs set to 255/255 and verified." : " Secondary RDAC safe-state/verification reported an error.");
    return enable_read && !enabled ? 0 : -1;
}

static int cmd_hv_read(int argc, char **argv, char *out, size_t out_len)
{
    (void)argc; (void)argv; (void)out; (void)out_len;
    if (!ztm_psu_hv_1.config.i2c) {
        console_printf("\r\nHV module has no initialized I2C context; use 'hv probe' or 'hv reinit'.\r\n");
        return -1;
    }
    ztm_psu_hv_snapshot_t snapshot;
    bool ok = ztm_psu_hv_read_snapshot(&ztm_psu_hv_1, &snapshot);
    console_printf("\r\nHV PSU live snapshot:\r\n");
    print_hv_snapshot(&snapshot);
    return ok ? 0 : -2;
}

static int cmd_hv_pg(int argc, char **argv, char *out, size_t out_len)
{
    (void)argc; (void)argv; (void)out; (void)out_len;
    bool pgood = false;
    if (!ztm_psu_hv_read_pgood(&ztm_psu_hv_1, &pgood)) {
        console_printf("\r\nLM5164 PG read failed.\r\n");
        return -1;
    }
    console_printf("\r\nLM5164 PG: %s (does not verify E60 HV output)\r\n",
                   pgood ? "HIGH (GOOD)" : "LOW (NOT GOOD)");
    return 0;
}

static int cmd_hv_rdac_read(int argc, char **argv, char *out, size_t out_len)
{
    (void)out; (void)out_len;
    if (argc != 3) {
        console_printf("\r\nUsage: hv rdac-read <1|2|all>\r\n");
        return -1;
    }

    if (selector_is_all(argv[2])) {
        uint8_t code1 = 0u, code2 = 0u;
        bool ok = ztm_psu_hv_read_rdac(&ztm_psu_hv_1, AD5282_RDAC1, &code1) &&
                  ztm_psu_hv_read_rdac(&ztm_psu_hv_1, AD5282_RDAC2, &code2);
        console_printf("\r\nAD5282 RDAC1=%u RDAC2=%u%s\r\n",
                       (unsigned)code1,
                       (unsigned)code2,
                       ok ? "" : " (READ FAILED)");
        return ok ? 0 : -2;
    }

    uint32_t index = 0u;
    if (!parse_u32(argv[2], &index) || index < 1u || index > 2u) {
        console_printf("\r\nUsage: hv rdac-read <1|2|all>\r\n");
        return -1;
    }
    uint8_t code = 0u;
    bool ok = ztm_psu_hv_read_rdac(&ztm_psu_hv_1,
                                   index == 1u ? AD5282_RDAC1 : AD5282_RDAC2,
                                   &code);
    console_printf("\r\nAD5282 RDAC%lu=%u%s\r\n",
                   (unsigned long)index,
                   (unsigned)code,
                   ok ? "" : " (READ FAILED)");
    return ok ? 0 : -2;
}

static int cmd_hv_rdac_write(int argc, char **argv, char *out, size_t out_len)
{
    (void)out; (void)out_len;
    uint32_t index = 0u, code = 0u;
    if (argc != 4 || !parse_u32(argv[2], &index) || index < 1u || index > 2u ||
        !parse_u32(argv[3], &code) || code > 255u) {
        console_printf("\r\nUsage: hv rdac-write <1|2> <0..255>\r\n");
        console_printf("Raw RDAC writes are accepted only while EN reads LOW.\r\n");
        return -1;
    }

    bool ok = ztm_psu_hv_write_rdac_off(&ztm_psu_hv_1,
                                        index == 1u ? AD5282_RDAC1 : AD5282_RDAC2,
                                        (uint8_t)code);
    console_printf("\r\nAD5282 RDAC%lu raw write %s%s\r\n",
                   (unsigned long)index,
                   ok ? "OK" : "FAILED",
                   ok ? "; converter remains OFF." : " (EN must be LOW and readback must match)." );
    return ok ? 0 : -2;
}

static int cmd_hv_reinit(int argc, char **argv, char *out, size_t out_len)
{
    (void)argc; (void)argv; (void)out; (void)out_len;
    int rc = modules_reinitialize_by_short_name("ztm_psu_hv");
    console_printf("\r\nHV module reinitialize: %s",
                   rc == 0 ? "READY and OFF" : "FAILED");
    if (rc != 0) {
        console_printf(" at %s (rc=%d)",
                       ztm_psu_hv_init_stage_name(ztm_psu_hv_1.last_init_stage),
                       rc);
    }
    console_printf(".\r\n");
    return rc;
}

/* =========================================================
   AD7124 temperature ADC diagnostics and bench controls
   ========================================================= */

static void print_adc_rtd_sample(const char *name,
                                 const ztm_adc_rtd_sample_t *sample)
{
    const char *label = name ? name : "RTD";
    if (!sample || !sample->valid) {
        console_printf("  %-5s READ ERROR\r\n", label);
        return;
    }
    if (sample->status == ZTM_ADC_SENSOR_DISCONNECTED) {
        console_printf("  %-5s CH%u AIN%u raw=0x%06lX DISCONNECTED\r\n",
                       label,
                       (unsigned)sample->ad7124_channel,
                       (unsigned)sample->ain_positive,
                       (unsigned long)sample->raw_code);
        return;
    }
    if (sample->status == ZTM_ADC_SENSOR_OUT_OF_RANGE) {
        console_printf("  %-5s CH%u AIN%u raw=0x%06lX R=%.3f ohm OUT OF RANGE\r\n",
                       label,
                       (unsigned)sample->ad7124_channel,
                       (unsigned)sample->ain_positive,
                       (unsigned long)sample->raw_code,
                       sample->resistance_ohms);
        return;
    }
    console_printf("  %-5s CH%u AIN%u raw=0x%06lX R=%.3f ohm T=%.3f C\r\n",
                   label,
                   (unsigned)sample->ad7124_channel,
                   (unsigned)sample->ain_positive,
                   (unsigned long)sample->raw_code,
                   sample->resistance_ohms,
                   sample->temperature_c);
}

static void print_adc_tc_sample(const ztm_adc_tc_sample_t *sample)
{
    if (!sample || !sample->valid) {
        console_printf("  TC?   READ ERROR\r\n");
        return;
    }

    const char *type = thermocouple_type_name(sample->type);
    if (sample->status == ZTM_ADC_SENSOR_DISCONNECTED) {
        console_printf("  TC%u   Type %s CH%u AIN%u/AIN%u raw=0x%06lX DISCONNECTED\r\n",
                       (unsigned)sample->logical_index, type,
                       (unsigned)sample->ad7124_channel,
                       (unsigned)sample->ain_positive,
                       (unsigned)sample->ain_negative,
                       (unsigned long)sample->raw_code);
        return;
    }
    if (sample->status == ZTM_ADC_SENSOR_CJC_UNAVAILABLE) {
        console_printf("  TC%u   Type %s CH%u AIN%u/AIN%u raw=0x%06lX dV=%+.6f mV, CJC unavailable\r\n",
                       (unsigned)sample->logical_index, type,
                       (unsigned)sample->ad7124_channel,
                       (unsigned)sample->ain_positive,
                       (unsigned)sample->ain_negative,
                       (unsigned long)sample->raw_code,
                       sample->differential_mv);
        return;
    }
    if (sample->status == ZTM_ADC_SENSOR_OUT_OF_RANGE) {
        console_printf("  TC%u   Type %s CH%u AIN%u/AIN%u raw=0x%06lX dV=%+.6f mV, CJC=%.3f C, OUT OF RANGE\r\n",
                       (unsigned)sample->logical_index, type,
                       (unsigned)sample->ad7124_channel,
                       (unsigned)sample->ain_positive,
                       (unsigned)sample->ain_negative,
                       (unsigned long)sample->raw_code,
                       sample->differential_mv,
                       sample->cold_junction_c);
        return;
    }

    console_printf("  TC%u   Type %s CH%u AIN%u/AIN%u raw=0x%06lX dV=%+.6f mV, "
                   "CJC=%.3f C, E0=%+.6f mV, T=%.3f C\r\n",
                   (unsigned)sample->logical_index, type,
                   (unsigned)sample->ad7124_channel,
                   (unsigned)sample->ain_positive,
                   (unsigned)sample->ain_negative,
                   (unsigned long)sample->raw_code,
                   sample->differential_mv,
                   sample->cold_junction_c,
                   sample->compensated_mv,
                   sample->temperature_c);
}

static const char *adc_measure_stage_name(ztm_adc_measure_stage_t stage)
{
    switch (stage) {
        case ZTM_ADC_MEASURE_NONE:             return "none";
        case ZTM_ADC_MEASURE_IOUT_ENABLE:      return "IOUT enable/write";
        case ZTM_ADC_MEASURE_CHANNEL_ENABLE:   return "channel enable";
        case ZTM_ADC_MEASURE_MODE_START:       return "single-conversion start";
        case ZTM_ADC_MEASURE_MODE_VERIFY:      return "single-conversion mode readback";
        case ZTM_ADC_MEASURE_READY_TIMEOUT:    return "conversion ready timeout";
        case ZTM_ADC_MEASURE_DATA_READ:        return "data register read";
        case ZTM_ADC_MEASURE_CHANNEL_MISMATCH: return "returned channel mismatch";
        case ZTM_ADC_MEASURE_STANDBY:          return "return to standby";
        case ZTM_ADC_MEASURE_CHANNEL_DISABLE:  return "channel disable";
        case ZTM_ADC_MEASURE_IOUT_DISABLE:     return "IOUT disable";
        case ZTM_ADC_MEASURE_SENSOR_RANGE:     return "sensor resistance/range";
        case ZTM_ADC_MEASURE_OK:               return "OK";
        default:                               return "unknown";
    }
}

static void print_adc_last_measurement(void)
{
    ztm_adc_measure_diag_t diag;
    if (!ztm_adc_get_last_measurement(&ztm_adc_1, &diag)) {
        console_printf("  Measurement diagnostics unavailable.\r\n");
        return;
    }

    console_printf("  Stage: %s\r\n", adc_measure_stage_name(diag.stage));
    if (diag.active_channel == 0xFFu) {
        console_printf("  Expected CH%u, returned --; STATUS before=0x%02X ready=0x%02X\r\n",
                       (unsigned)diag.expected_channel,
                       (unsigned)diag.status_before,
                       (unsigned)diag.status_ready);
    } else {
        console_printf("  Expected CH%u, returned CH%u; STATUS before=0x%02X ready=0x%02X\r\n",
                       (unsigned)diag.expected_channel,
                       (unsigned)diag.active_channel,
                       (unsigned)diag.status_before,
                       (unsigned)diag.status_ready);
    }
    console_printf("  CHANNEL=0x%04X FILTER=0x%06lX IO_CONTROL_1=0x%06lX\r\n",
                   (unsigned)diag.channel_register,
                   (unsigned long)diag.filter_register,
                   (unsigned long)diag.io_control_1);
    console_printf("  ADC_CONTROL start=0x%04X result=0x%04X cleanup=0x%04X; "
                   "STATUS cleanup=0x%02X\r\n",
                   (unsigned)diag.adc_control_after_start,
                   (unsigned)diag.adc_control_at_result,
                   (unsigned)diag.adc_control_after_cleanup,
                   (unsigned)diag.status_after_cleanup);
    console_printf("  Waited %.3f ms in %lu status polls; DATA=0x%06lX ERROR=0x%06lX\r\n",
                   (double)diag.elapsed_us / 1000.0,
                   (unsigned long)diag.ready_polls,
                   (unsigned long)diag.raw_code,
                   (unsigned long)diag.error_mask);
    if (diag.resistance_ohms > 0.0) {
        console_printf("  Calculated RTD resistance: %.3f ohm\r\n",
                       diag.resistance_ohms);
    }
}


static int cmd_adc_info(int argc, char **argv, char *out, size_t out_len)
{
    (void)argc; (void)argv; (void)out; (void)out_len;
    ztm_adc_status_t module_status;
    if (!ztm_adc_get_status(&ztm_adc_1, &module_status)) {
        console_printf("\r\nAD7124 module: diagnostics unavailable\r\n");
        return -1;
    }
    const bus_spi_t *adc_bus = ztmb_ztm_adc_bus();

    console_printf("\r\nAD7124 module: %s\r\n", module_status.ready ? "READY" : "NOT READY");
    console_printf("  ID: 0x%02X; SPI%u, %lu Hz, mode %u, CS line %u\r\n",
                   (unsigned)module_status.device_id,
                   adc_bus && adc_bus->spi == spi0 ? 0u : 1u,
                   adc_bus ? (unsigned long)adc_bus->config.baud_hz : 0ul,
                   adc_bus ? (unsigned)((adc_bus->config.cpol ? 2u : 0u) |
                                        (adc_bus->config.cpha ? 1u : 0u)) : 0u,
                   adc_bus ? (unsigned)adc_bus->cs_mux_id : 0u);
    console_printf("  TC1..3: AIN0/1, AIN2/3, AIN4/5; internal 2.5 V, bipolar, PGA32\r\n");
    console_printf("  CJC: CH3 AIN6/AIN15 Pt1000; RTD1..8: CH4..11 AIN7..14/AIN15\r\n");
    console_printf("  RTD return: RTD_R is sensed directly by REFIN1+ and through 100 ohm by AIN15\r\n");
    console_printf("  RTD: REFIN1 ratiometric, Rref=%.1f ohm, IOUT0=500 uA, one sensor at a time\r\n",
                   module_status.reference_resistor_ohms);
    console_printf("  At a 1 kohm Pt1000: expect RTD_R/REFIN1+=~1.0 V and sensor AIN+=~1.5 V\r\n");
    console_printf("  TC types: TC1=%s TC2=%s TC3=%s; default Type K; runtime configurable\r\n",
                   thermocouple_type_name(module_status.thermocouple_types[0]),
                   thermocouple_type_name(module_status.thermocouple_types[1]),
                   thermocouple_type_name(module_status.thermocouple_types[2]));
    console_printf("  Configuration: %s; verify failures=%lu\r\n",
                   module_status.configuration_verified ? "VERIFIED" : "NOT VERIFIED",
                   (unsigned long)module_status.configuration_error_count);
    console_printf("  Error events=%lu, communication errors=%lu, last mask=0x%06lX\r\n",
                   (unsigned long)module_status.error_event_count,
                   (unsigned long)module_status.communication_error_count,
                   (unsigned long)module_status.last_error_mask);

    if (!module_status.ready) {
        return -1;
    }

    uint32_t adc_control = 0u, tc_config = 0u, rtd_config = 0u;
    uint32_t tc_filter = 0u, rtd_filter = 0u, io1 = 0u, io2 = 0u;
    bool ok = ztm_adc_read_register(&ztm_adc_1, AD7124_REG_ADC_CONTROL, 2u, &adc_control) &&
              ztm_adc_read_register(&ztm_adc_1, AD7124_REG_CONFIG(ZTM_ADC_SETUP_TC), 2u, &tc_config) &&
              ztm_adc_read_register(&ztm_adc_1, AD7124_REG_CONFIG(ZTM_ADC_SETUP_RTD), 2u, &rtd_config) &&
              ztm_adc_read_register(&ztm_adc_1, AD7124_REG_FILTER(ZTM_ADC_SETUP_TC), 3u, &tc_filter) &&
              ztm_adc_read_register(&ztm_adc_1, AD7124_REG_FILTER(ZTM_ADC_SETUP_RTD), 3u, &rtd_filter) &&
              ztm_adc_read_register(&ztm_adc_1, AD7124_REG_IO_CONTROL_1, 3u, &io1) &&
              ztm_adc_read_register(&ztm_adc_1, AD7124_REG_IO_CONTROL_2, 2u, &io2);
    if (ok) {
        console_printf("  Registers: ADC_CONTROL=0x%04lX TC_CFG=0x%04lX RTD_CFG=0x%04lX\r\n",
                       (unsigned long)adc_control,
                       (unsigned long)tc_config,
                       (unsigned long)rtd_config);
        console_printf("             TC_FILTER=0x%06lX RTD_FILTER=0x%06lX IO1=0x%06lX IO2=0x%04lX\r\n",
                       (unsigned long)tc_filter,
                       (unsigned long)rtd_filter,
                       (unsigned long)io1,
                       (unsigned long)io2);
    } else {
        console_printf("  Register readback failed or ADC is busy.\r\n");
    }
    return 0;
}

static int cmd_adc_cjc(int argc, char **argv, char *out, size_t out_len)
{
    (void)argc; (void)argv; (void)out; (void)out_len;
    ztm_adc_rtd_sample_t sample;
    if (!ztm_adc_read_cold_junction(&ztm_adc_1, &sample)) {
        console_printf("\r\nCold-junction RTD read failed.\r\n");
        print_adc_last_measurement();
        return -1;
    }
    console_printf("\r\nCold-junction measurement:\r\n");
    print_adc_rtd_sample("CJC", &sample);
    return 0;
}

static int cmd_adc_rtd(int argc, char **argv, char *out, size_t out_len)
{
    (void)out; (void)out_len;
    if (argc != 3) {
        console_printf("\r\nUsage: adc rtd <1..8|all>\r\n");
        return -1;
    }

    if (selector_is_all(argv[2])) {
        ztm_adc_rtd_sample_t cjc;
        ztm_adc_rtd_sample_t rtds[ZTM_ADC_RTD_COUNT];
        if (!ztm_adc_read_all_rtds(&ztm_adc_1, &cjc, rtds)) {
            console_printf("\r\nRTD scan failed; excitation has been turned off.\r\n");
            print_adc_last_measurement();
            return -2;
        }
        console_printf("\r\nPt1000 measurements:\r\n");
        print_adc_rtd_sample("CJC", &cjc);
        for (uint8_t i = 0u; i < ZTM_ADC_RTD_COUNT; i++) {
            char name[8];
            (void)snprintf(name, sizeof(name), "RTD%u", (unsigned)(i + 1u));
            print_adc_rtd_sample(name, &rtds[i]);
        }
        return 0;
    }

    uint32_t id = 0u;
    if (!parse_u32(argv[2], &id) || id < 1u || id > ZTM_ADC_RTD_COUNT) {
        console_printf("\r\nUsage: adc rtd <1..8|all>\r\n");
        return -3;
    }

    ztm_adc_rtd_sample_t sample;
    if (!ztm_adc_read_rtd(&ztm_adc_1, (uint8_t)id, &sample)) {
        console_printf("\r\nRTD%lu read failed; excitation has been turned off.\r\n",
                       (unsigned long)id);
        print_adc_last_measurement();
        return -4;
    }
    char name[8];
    (void)snprintf(name, sizeof(name), "RTD%lu", (unsigned long)id);
    console_printf("\r\nPt1000 measurement:\r\n");
    print_adc_rtd_sample(name, &sample);
    return 0;
}

static void print_adc_tc_types(void)
{
    for (uint8_t tc = 1u; tc <= ZTM_ADC_TC_COUNT; tc++) {
        thermocouple_type_t type;
        if (ztm_adc_get_thermocouple_type(&ztm_adc_1, tc, &type)) {
            console_printf("  TC%u: Type %s\r\n",
                           (unsigned)tc, thermocouple_type_name(type));
        } else {
            console_printf("  TC%u: unavailable\r\n", (unsigned)tc);
        }
    }
}

static int cmd_adc_tc_type(int argc, char **argv, char *out, size_t out_len)
{
    (void)out; (void)out_len;
    if (argc != 3 && argc != 4) {
        console_printf("\r\nUsage: adc tc-type <1..3|all> [B|E|J|K|N|R|S|T]\r\n");
        return -1;
    }

    bool all = selector_is_all(argv[2]);
    uint32_t id = 0u;
    if (!all && (!parse_u32(argv[2], &id) || id < 1u || id > ZTM_ADC_TC_COUNT)) {
        console_printf("\r\nUsage: adc tc-type <1..3|all> [B|E|J|K|N|R|S|T]\r\n");
        return -2;
    }

    if (argc == 3) {
        console_printf("\r\nThermocouple channel types:\r\n");
        if (all) {
            print_adc_tc_types();
        } else {
            thermocouple_type_t type;
            if (!ztm_adc_get_thermocouple_type(&ztm_adc_1, (uint8_t)id, &type)) {
                console_printf("  TC%lu type unavailable.\r\n", (unsigned long)id);
                return -3;
            }
            console_printf("  TC%lu: Type %s\r\n",
                           (unsigned long)id, thermocouple_type_name(type));
        }
        return 0;
    }

    thermocouple_type_t type;
    if (!thermocouple_type_parse(argv[3], &type)) {
        console_printf("\r\nUnknown thermocouple type '%s'. Use B, E, J, K, N, R, S, or T.\r\n",
                       argv[3]);
        return -4;
    }

    if (all) {
        for (uint8_t tc = 1u; tc <= ZTM_ADC_TC_COUNT; tc++) {
            if (!ztm_adc_set_thermocouple_type(&ztm_adc_1, tc, type)) {
                console_printf("\r\nFailed to set TC%u type.\r\n", (unsigned)tc);
                return -5;
            }
        }
        console_printf("\r\nTC1..3 set to Type %s (runtime setting).\r\n",
                       thermocouple_type_name(type));
    } else {
        if (!ztm_adc_set_thermocouple_type(&ztm_adc_1, (uint8_t)id, type)) {
            console_printf("\r\nFailed to set TC%lu type.\r\n", (unsigned long)id);
            return -6;
        }
        console_printf("\r\nTC%lu set to Type %s (runtime setting).\r\n",
                       (unsigned long)id, thermocouple_type_name(type));
    }
    return 0;
}

static int cmd_adc_tc(int argc, char **argv, char *out, size_t out_len)
{
    (void)out; (void)out_len;
    if (argc != 3) {
        console_printf("\r\nUsage: adc tc <1..3|all>\r\n");
        return -1;
    }

    if (selector_is_all(argv[2])) {
        ztm_adc_tc_sample_t samples[ZTM_ADC_TC_COUNT];
        if (!ztm_adc_read_all_thermocouples(&ztm_adc_1, samples)) {
            console_printf("\r\nThermocouple scan failed.\r\n");
            print_adc_last_measurement();
            return -2;
        }
        console_printf("\r\nCold-junction compensated thermocouple readings:\r\n");
        for (uint8_t i = 0u; i < ZTM_ADC_TC_COUNT; i++) {
            print_adc_tc_sample(&samples[i]);
        }
        return 0;
    }

    uint32_t id = 0u;
    if (!parse_u32(argv[2], &id) || id < 1u || id > ZTM_ADC_TC_COUNT) {
        console_printf("\r\nUsage: adc tc <1..3|all>\r\n");
        return -3;
    }

    ztm_adc_tc_sample_t sample;
    if (!ztm_adc_read_thermocouple(&ztm_adc_1, (uint8_t)id, &sample)) {
        console_printf("\r\nTC%lu read failed.\r\n", (unsigned long)id);
        print_adc_last_measurement();
        return -4;
    }
    console_printf("\r\nCold-junction compensated thermocouple reading:\r\n");
    print_adc_tc_sample(&sample);
    return 0;
}

static int cmd_adc_read_all(int argc, char **argv, char *out, size_t out_len)
{
    (void)argc; (void)argv; (void)out; (void)out_len;
    ztm_adc_rtd_sample_t cjc;
    ztm_adc_rtd_sample_t rtds[ZTM_ADC_RTD_COUNT];
    ztm_adc_tc_sample_t tcs[ZTM_ADC_TC_COUNT];
    if (!ztm_adc_read_all(&ztm_adc_1, &cjc, rtds, tcs)) {
        console_printf("\r\nComplete ADC scan failed; RTD excitation has been turned off.\r\n");
        print_adc_last_measurement();
        return -1;
    }

    console_printf("\r\nAD7124 complete temperature-front-end scan:\r\n");
    print_adc_rtd_sample("CJC", &cjc);
    for (uint8_t i = 0u; i < ZTM_ADC_RTD_COUNT; i++) {
        char name[8];
        (void)snprintf(name, sizeof(name), "RTD%u", (unsigned)(i + 1u));
        print_adc_rtd_sample(name, &rtds[i]);
    }
    for (uint8_t i = 0u; i < ZTM_ADC_TC_COUNT; i++) {
        print_adc_tc_sample(&tcs[i]);
    }
    return 0;
}

static int cmd_adc_verify(int argc, char **argv, char *out, size_t out_len)
{
    (void)argc; (void)argv; (void)out; (void)out_len;
    bool ok = ztm_adc_verify_configuration(&ztm_adc_1);
    console_printf("\r\nAD7124 configuration %s.\r\n",
                   ok ? "matches the ZTM ADC board map" : "verification FAILED");
    return ok ? 0 : -1;
}

static int cmd_adc_errors(int argc, char **argv, char *out, size_t out_len)
{
    (void)argc; (void)argv; (void)out; (void)out_len;
    uint8_t status = 0u;
    uint32_t errors = 0u;
    if (!ztm_adc_read_error_status(&ztm_adc_1, &status, &errors)) {
        console_printf("\r\nAD7124 status/error read failed.\r\n");
        return -1;
    }
    console_printf("\r\nAD7124 STATUS=0x%02X ERROR=0x%06lX\r\n",
                   (unsigned)status,
                   (unsigned long)errors);
    console_printf("  Reading ERROR clears the aggregate ERROR_FLAG and some latched diagnostics.\r\n");
    return 0;
}

static int cmd_adc_reg(int argc, char **argv, char *out, size_t out_len)
{
    (void)out; (void)out_len;
    uint32_t address = 0u, width = 0u;
    if (argc != 4 || !parse_u32(argv[2], &address) || address > 0x38u ||
        !parse_u32(argv[3], &width) || width < 1u || width > 3u) {
        console_printf("\r\nUsage: adc reg <address 0..0x38> <width 1|2|3>\r\n");
        return -1;
    }

    uint32_t value = 0u;
    if (!ztm_adc_read_register(&ztm_adc_1, (uint8_t)address, (uint8_t)width, &value)) {
        console_printf("\r\nAD7124 register read failed. Check address and register width.\r\n");
        return -2;
    }
    console_printf("\r\nAD7124 register 0x%02lX = 0x%0*lX\r\n",
                   (unsigned long)address,
                   (int)(width * 2u),
                   (unsigned long)value);
    return 0;
}

static int cmd_adc_diag(int argc, char **argv, char *out, size_t out_len)
{
    (void)argc; (void)argv; (void)out; (void)out_len;
    console_printf("\r\nLast AD7124 measurement:\r\n");
    print_adc_last_measurement();
    return 0;
}

static int cmd_adc_calibrate(int argc, char **argv, char *out, size_t out_len)
{
    (void)out; (void)out_len;
    if (argc != 3) {
        console_printf("\r\nUsage: adc calibrate <tc|rtd|all>\r\n");
        return -1;
    }

    bool ok = false;
    if (strcmp(argv[2], "tc") == 0) {
        ok = ztm_adc_calibrate_tc_setup(&ztm_adc_1);
    } else if (strcmp(argv[2], "rtd") == 0) {
        ok = ztm_adc_calibrate_rtd_setup(&ztm_adc_1);
    } else if (strcmp(argv[2], "all") == 0) {
        ok = ztm_adc_calibrate_all(&ztm_adc_1);
    } else {
        console_printf("\r\nUsage: adc calibrate <tc|rtd|all>\r\n");
        return -2;
    }

    console_printf("\r\n%s\r\n", ok ? "AD7124 internal calibration complete"
                                      : "AD7124 calibration failed");
    return ok ? 0 : -3;
}

static int cmd_adc_iout_off(int argc, char **argv, char *out, size_t out_len)
{
    (void)argc; (void)argv; (void)out; (void)out_len;
    bool ok = ztm_adc_iout_off(&ztm_adc_1);
    console_printf("\r\n%s\r\n", ok ? "AD7124 excitation currents are off"
                                      : "Failed to disable AD7124 excitation currents");
    return ok ? 0 : -1;
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

    /* Revoke TEC permission first, even if other subsystems fail to stop. */
    bool tec_ok = cooling_tec_revoke_permission();
    bool pump_state_ok = !pumping_ready() || pumping_all_stop();
    bool hiz_ok = !ztm_stepper_ready(&ztm_stepper_1) || ztm_stepper_hard_hiz_all(&ztm_stepper_1);
    bool stepper_ok = pump_state_ok && hiz_ok;

    /* EN-low is authoritative even when the AD5282 did not initialize. */
    bool hv_ok = ztm_psu_hv_stop(&ztm_psu_hv_1);
    bool hv_enabled = true;
    bool hv_enable_read = ztm_psu_hv_read_enable(&ztm_psu_hv_1, &hv_enabled);
    hv_ok = hv_enable_read && !hv_enabled;

    bool pwm_ok = !modules_find_by_short_name("ztm_pwm") || ztm_pwm_stop_all(&ztm_pwm_1);
    bool adc_ok = !ztm_adc_ready(&ztm_adc_1) || ztm_adc_iout_off(&ztm_adc_1);

    console_printf("  steppers: %s\r\n", stepper_ok ? "HiZ/not fitted" : "FAILED");
    console_printf("  TECs:     %s\r\n", tec_ok ? "disabled/not fitted" : "FAILED");
    console_printf("  HV:       %s\r\n", hv_ok ? "disabled/not fitted" : "FAILED");
    console_printf("  PWM:      %s\r\n", pwm_ok ? "zero/not fitted" : "FAILED");
    console_printf("  ADC IOUT: %s\r\n", adc_ok ? "off/not fitted" : "FAILED");

    return stepper_ok && tec_ok && hv_ok && pwm_ok && adc_ok ? 0 : -1;
}

#include "run_console.h"

static int cmd_version(int argc, char **argv, char *out, size_t out_len)
{
    (void)argc; (void)argv; (void)out; (void)out_len;
    console_printf("\r\n%s firmware %s\r\n", HUB_FIRMWARE_NAME, HUB_FIRMWARE_VERSION);
    return 0;
}

/* =========================================================
   Command tables and dispatcher
   ========================================================= */

static int cmd_help(int argc, char **argv, char *out, size_t out_len);

static const cli_cmd_t hv_cmds[] = {
    { "info",       "show AD5282, LM5164 drive, E60 estimate, state, and faults", cmd_hv_info },
    { "probe",      "safe probe: force EN low, check AD5282, LM5164 PG, and drive VSENSE", cmd_hv_probe },
    { "reinit",     "reinitialize the HV module and leave it OFF", cmd_hv_reinit },
    { "set",        "hv set <0..255> - stage and verify RDAC2 while EN remains low", cmd_hv_set },
    { "on",         "hv on <55..255> - raw guarded code; lower code requests higher output", cmd_hv_on },
    { "on-est",     "hv on-est <1569..5479> - select code from measured nominal-HV calibration", cmd_hv_on_est },
    { "map",        "hv map [55..255] - show measured/interpolated code-to-voltage calibration", cmd_hv_map },
    { "sweep",      "hv sweep [start [end [step]]] - guarded high-code-to-low-code characterisation", cmd_hv_sweep },
    { "off",        "drive EN low, then set both RDACs to safe maximum resistance", cmd_hv_off },
    { "read",       "read EN, LM5164 PG/drive, and estimated E60 high voltage", cmd_hv_read },
    { "pg",         "read the LM5164 power-good input", cmd_hv_pg },
    { "rdac-read",  "hv rdac-read <1|2|all> - raw AD5282 readback", cmd_hv_rdac_read },
    { "rdac-write", "hv rdac-write <1|2> <0..255> - raw write, only while OFF", cmd_hv_rdac_write },
};

static const cli_cmd_t adc_cmds[] = {
    { "info",      "show AD7124 identity, topology, assumptions, and register summary", cmd_adc_info },
    { "cjc",       "read the PCB cold-junction Pt1000 on AIN6", cmd_adc_cjc },
    { "rtd",       "adc rtd <1..8|all> - excite one Pt1000 at a time and convert to C", cmd_adc_rtd },
    { "tc",        "adc tc <1..3|all> - compensated thermocouple temperature in C", cmd_adc_tc },
    { "tc-type",   "adc tc-type <1..3|all> [B|E|J|K|N|R|S|T] - query/set probe type", cmd_adc_tc_type },
    { "read-all",  "read CJC, eight external RTDs, and three thermocouples", cmd_adc_read_all },
    { "verify",    "read back and verify all ADC, setup, filter, channel, bias, and IOUT registers", cmd_adc_verify },
    { "errors",    "read raw STATUS and ERROR diagnostic registers", cmd_adc_errors },
    { "diag",      "show the stage and raw registers from the last measurement", cmd_adc_diag },
    { "reg",       "adc reg <address> <1|2|3 bytes> - raw register read", cmd_adc_reg },
    { "calibrate", "adc calibrate <tc|rtd|all> - internal setup calibration", cmd_adc_calibrate },
    { "iout-off",  "immediately turn both AD7124 excitation currents off", cmd_adc_iout_off },
};

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
    { "config", "stepper config <1..4|all> [hold run acc dec|defaults] - query/set KVALs", cmd_stepper_config },
    { "microstep", "stepper microstep <1..4|all> [mode|defaults] - full through 1/128", cmd_stepper_microstep },
    { "run",    "stepper run <1..3|all> <fwd|rev> <steps/s>; all excludes motor 4", cmd_stepper_run },
    { "move",   "stepper move <1..3> <fwd|rev> <steps>", cmd_stepper_move },
    { "get",    "stepper get <1..4> <register> <bytes>", cmd_stepper_get },
    { "set",    "stepper set <1..4> <register> <bytes> <value> - raw write", cmd_stepper_set },
};

static const cli_cmd_t pump_cmds[] = {
    { "status",        "show pump mapping, state, speed, and wick timer", cmd_pump_status },
    { "filter-normal", "IPA filter: motor 1 fwd 30 steps/s", cmd_pump_filter_normal },
    { "filter-fast",   "IPA filter: motor 1 fwd 100 steps/s", cmd_pump_filter_fast },
    { "filter-stop",   "stop IPA filter motor 1", cmd_pump_filter_stop },
    { "wick-prime",    "IPA wick: motor 2 fwd 100 steps/s for 5000 ms", cmd_pump_wick_prime },
    { "wick-dose",     "IPA wick: motor 2 fwd 30 steps/s for 2000 ms", cmd_pump_wick_dose },
    { "wick-drain",    "IPA wick: motor 2 rev 100 steps/s for 5000 ms", cmd_pump_wick_drain },
    { "wick-wet",      "IPA wick: motor 2 fwd 30 steps/s continuously until stopped", cmd_pump_wick_wet },
    { "wick-stop",     "stop IPA wick motor 2", cmd_pump_wick_stop },
    { "sump-run",      "IPA sump: motor 3 fwd 30 steps/s", cmd_pump_sump_run },
    { "sump-stop",     "stop IPA sump motor 3", cmd_pump_sump_stop },
    { "stop-all",      "stop motors 1..3 and keep motor 4 in HiZ", cmd_pump_stop_all },
    { "start",         "legacy role ID (not motor): [1=filter, 2=sump, 3=wick dose]", cmd_pump_start },
    { "stop",          "legacy role ID (not motor): stop [1=filter, 2=sump, 3=wick]", cmd_pump_stop },
};

static const cli_cmd_t tec_cmds[] = {
    { "permit", "show lock or explicitly confirm: tec permit chiller-at-temperature-and-circulating", cmd_tec_permit },
    { "revoke", "revoke operator permission and stop every TEC", cmd_tec_revoke },
    { "set",      "tec set <1..4> <0..255> - set one DAC and verify VSENSE", cmd_tec_set },
    { "set-all",  "tec set-all <0..255> - set all DACs and read all VSENSE inputs", cmd_tec_set_all },
    { "read",     "tec read <1..4|all> - read output voltage through 130k/20k divider", cmd_tec_read },
    { "stop",     "tec stop [1..4] - no channel stops all; otherwise stop one", cmd_tec_stop },
    { "start",    "compatibility alias: tec start [channel] <0..255>", cmd_tec_start },
};

static const cli_cmd_t pwm_cmds[] = {
    { "info",        "show addresses, mappings, software state, and IRQ counters", cmd_pwm_info },
    { "probe",       "probe expected I2C devices and identify PAC topology", cmd_pwm_probe },
    { "i2c-scan",    "scan I2C0 addresses 0x08..0x77 with PWM module powered", cmd_pwm_i2c_scan },
    { "power",       "pwm power <off|on|cycle> - exercise module enable and reinitialize", cmd_pwm_power },
    { "set",         "pwm set <1..12> <0..100> - set one low-side PWM channel", cmd_pwm_set },
    { "set-all",     "pwm set-all <0..100> - set all low-side PWM channels", cmd_pwm_set_all },
    { "stop",        "pwm stop [1..12] - no channel stops all low-side and fan outputs", cmd_pwm_stop },
    { "current",     "pwm current <1..12|all> - averaged current through 50mOhm shunts", cmd_pwm_current },
    { "measure",     "pwm measure <1..12|all> [avg|instant] - source, raw shunt voltage, current", cmd_pwm_measure },
    { "fan",         "pwm fan <1..6> <0..100> - direct/open-loop EMC2303 PWM", cmd_pwm_fan_set },
    { "fan-output",  "pwm fan-output <1..6|all> [open-drain|push-pull] - set/read output type", cmd_pwm_fan_output },
    { "fan-raw",     "pwm fan-raw <1..6> <0..255> - direct EMC2303 drive register", cmd_pwm_fan_raw },
    { "fan-target",  "pwm fan-target <1..6> <rpm> - enable closed-loop RPM control", cmd_pwm_fan_target },
    { "fan-open",    "pwm fan-open <1..6> <0..100> - return to direct PWM mode", cmd_pwm_fan_open },
    { "fan-stop",    "pwm fan-stop [1..6] - no fan stops all six", cmd_pwm_fan_stop },
    { "rpm",         "pwm rpm <1..6|all> - read EMC2303 RPM", cmd_pwm_rpm },
    { "tach",        "pwm tach <1..6|all> - raw 13-bit tach counts and RPM", cmd_pwm_tach },
    { "fan-status",  "pwm fan-status [1|2|all] - read/clear EMC2303 latched status", cmd_pwm_fan_status },
    { "gpio-mode",   "pwm gpio-mode <1..4> <in|out>", cmd_pwm_gpio_mode },
    { "gpio-pull",   "pwm gpio-pull <1..4> <none|up|down> - configure as input", cmd_pwm_gpio_pull },
    { "gpio-irq",    "pwm gpio-irq <1..4> <off|rising|falling|both>", cmd_pwm_gpio_irq },
    { "gpio-write",  "pwm gpio-write <1..4> <0|1>", cmd_pwm_gpio_write },
    { "gpio-toggle", "pwm gpio-toggle <1..4>", cmd_pwm_gpio_toggle },
    { "gpio-read",   "pwm gpio-read <1..4|all>", cmd_pwm_gpio_read },
    { "irq-line",    "read the shared active-low module IRQ input without clearing sources", cmd_pwm_irq_line },
    { "irq",         "scan SX1509, all PAC195x, and both EMC2303 status sources", cmd_pwm_irq },
    { "sx-read",     "pwm sx-read <reg> [count] - raw SX1509 register read", cmd_pwm_sx_read },
    { "sx-write",    "pwm sx-write <reg> <byte> - raw SX1509 register write", cmd_pwm_sx_write },
    { "pac-read",    "pwm pac-read <1..3> <reg> <count> - raw PAC195x register read", cmd_pwm_pac_read },
    { "pac-write",   "pwm pac-write <1..3> <reg> <bytes...> - raw PAC195x write", cmd_pwm_pac_write },
    { "emc-read",    "pwm emc-read <1..2> <reg> - raw EMC2303 register read", cmd_pwm_emc_read },
    { "emc-write",   "pwm emc-write <1..2> <reg> <byte> - raw EMC2303 write", cmd_pwm_emc_write },
    { "start",       "compatibility alias: pwm start <raw 0..255> sets all low-side channels", cmd_pwm_start },
};

static const cli_group_t groups[] = {
    { "hv",      "High-voltage PSU safe control and diagnostics", hv_cmds, sizeof(hv_cmds) / sizeof(hv_cmds[0]) },
    { "adc",     "AD7124 thermocouple, cold-junction, RTD, and diagnostic controls", adc_cmds, sizeof(adc_cmds) / sizeof(adc_cmds[0]) },
    { "module",  "Module discovery and initialization diagnostics", module_cmds, sizeof(module_cmds) / sizeof(module_cmds[0]) },
    { "stepper", "Four-device L6470 chain diagnostics and bench control", stepper_cmds, sizeof(stepper_cmds) / sizeof(stepper_cmds[0]) },
    { "pump",    "IPA filter, wick, and sump process controls", pump_cmds, sizeof(pump_cmds) / sizeof(pump_cmds[0]) },
    { "tec",     "Thermoelectric cooler controls", tec_cmds, sizeof(tec_cmds) / sizeof(tec_cmds[0]) },
    { "pwm",     "12-channel low-side PWM, current, fan, GPIO, and IRQ controls", pwm_cmds, sizeof(pwm_cmds) / sizeof(pwm_cmds[0]) },
};

static const cli_cmd_t root_cmds[] = {
    { "run",     "Open chamber single-key dashboard", cmd_run },
    { "help",    "Show help", cmd_help },
    { "version", "Show firmware version", cmd_version },
    { "safe",    "Disable hazardous outputs and place steppers in hard HiZ", cmd_safe },
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
