#include <string.h>

#include "pico/stdlib.h"
#include "modules.h"
#include "ztmb/log.h"
#include "ztmb/cs_lines.h"
#include "ztmb/io_exp.h"

#include "modules/ztm_adc.h"
#include "modules/ztm_psu_hv.h"
#include "modules/ztm_psu_tec.h"
#include "ztmb/ztm_pwm_adapter.h"
#include "modules/ztm_stepper.h"

static module_entry_t g_modules[] = {
    {
        .short_name = "ztm_adc",
        .long_name  = "AD7124-8 temperature ADC (3 TC, CJC Pt1000, 8 RTD)",
        .init       = ztm_adc_init,
        .bus_ctx    = (void *)ZTMB_SPI1_PORT,
        .mod_inst   = &ztm_adc_1,
        .addr       = ZTMB_ADC_CS_2,
        .en_pin     = EX_NONE,
        .poll       = ztm_adc_poll,
        .deinit     = ztm_adc_deinit,
    },
    {
        .short_name = "ztm_psu_hv",
        .long_name  = "High Voltage PSU (AD5282 setpoint, PG/EN, TLA2518 VSENSE)",
        .init       = ztm_psu_hv_init,
        .bus_ctx    = (void *)ZTMB_I2C0_PORT,
        .mod_inst   = &ztm_psu_hv_1,
        .addr       = 0x00,
        .en_pin     = EX_HV_EN_1,
        .poll       = ztm_psu_hv_poll,
        .deinit     = ztm_psu_hv_deinit,
    },
    {
        .short_name = "ztm_stepper",
        .long_name  = "Stepper motor controller (4 x L6470 SPI chain)",
        .init       = ztm_stepper_init,
        .bus_ctx    = (void *)ZTMB_SPI0_PORT,
        .mod_inst   = &ztm_stepper_1,
        .addr       = ZTMB_STEP_CS_1,
        .en_pin     = EX_STEP_EN_1,
        .poll       = ztm_stepper_poll,
        .deinit     = NULL,
    },
    {
        .short_name = "ztm_pwm",
        .long_name  = "12-channel PWM/current/fan/GPIO module (SX1509, PAC195x, EMC2303)",
        .init       = ztmb_ztm_pwm_init,
        .bus_ctx    = (void *)ZTMB_I2C0_PORT,
        .mod_inst   = &ztm_pwm_1,
        .addr       = 0x00,
        .en_pin     = EX_I2C_ALT_EN_1,
        .poll       = ztmb_ztm_pwm_poll,
        .deinit     = ztmb_ztm_pwm_deinit,
    },
    {
        .short_name = "psu_tec_1",
        .long_name  = "Thermoelectric cooler 1 (MCP4901 DAC on SPI0)",
        .init       = ztm_psu_tec_init,
        .bus_ctx    = (void *)ZTMB_SPI0_PORT,
        .mod_inst   = &ztm_psu_tec_1,
        .addr       = ZTMB_TEC_CS_1,
        .en_pin     = EX_TEC_EN_1,
        .poll       = NULL,
        .deinit     = NULL,
    },
    {
        .short_name = "psu_tec_2",
        .long_name  = "Thermoelectric cooler 2 (MCP4901 DAC on SPI0)",
        .init       = ztm_psu_tec_init,
        .bus_ctx    = (void *)ZTMB_SPI0_PORT,
        .mod_inst   = &ztm_psu_tec_2,
        .addr       = ZTMB_TEC_CS_2,
        .en_pin     = EX_TEC_EN_2,
        .poll       = NULL,
        .deinit     = NULL,
    },
    {
        .short_name = "psu_tec_3",
        .long_name  = "Thermoelectric cooler 3 (MCP4901 DAC on SPI0)",
        .init       = ztm_psu_tec_init,
        .bus_ctx    = (void *)ZTMB_SPI0_PORT,
        .mod_inst   = &ztm_psu_tec_3,
        .addr       = ZTMB_TEC_CS_3,
        .en_pin     = EX_TEC_EN_3,
        .poll       = NULL,
        .deinit     = NULL,
    },
    {
        .short_name = "psu_tec_4",
        .long_name  = "Thermoelectric cooler 4 (MCP4901 DAC on SPI0)",
        .init       = ztm_psu_tec_init,
        .bus_ctx    = (void *)ZTMB_SPI0_PORT,
        .mod_inst   = &ztm_psu_tec_4,
        .addr       = ZTMB_TEC_CS_4,
        .en_pin     = EX_TEC_EN_4,
        .poll       = NULL,
        .deinit     = NULL,
    },
};

#define MODULE_COUNT (sizeof(g_modules) / sizeof(g_modules[0]))

static bool g_module_inited[MODULE_COUNT];

int modules_init_all(bool rollback_on_fail)
{
    memset(g_module_inited, 0, sizeof(g_module_inited));
    int first_error = 0;

    for (size_t i = 0; i < MODULE_COUNT; i++) {
        module_entry_t *module = &g_modules[i];
        if (!module->init) {
            continue;
        }

        int rc = module->init(module->mod_inst, module->bus_ctx, module->addr, module->en_pin);
        if (rc != 0) {
            log_printf("Module init failed: %s, rc=%d\n", module->short_name, rc);
            if (first_error == 0) {
                first_error = rc;
            }

            if (rollback_on_fail) {
                for (size_t j = i; j-- > 0;) {
                    module_entry_t *previous = &g_modules[j];
                    if (g_module_inited[j] && previous->deinit) {
                        (void)previous->deinit();
                    }
                    g_module_inited[j] = false;
                }
                return rc;
            }

            /* Continue so independently fitted modules remain testable. */
            continue;
        }

        g_module_inited[i] = true;
        log_printf("Module ready: %s\n", module->short_name);
    }

    return first_error;
}

int modules_poll_all(void)
{
    int errors = 0;

    for (size_t i = 0; i < MODULE_COUNT; i++) {
        module_entry_t *module = &g_modules[i];
        if (!g_module_inited[i] || !module->poll) {
            continue;
        }

        int rc = module->poll();
        if (rc != 0) {
            errors++;
            log_printf("Module poll failed: %s, rc=%d\n", module->short_name, rc);
        }
        log_poll();
    }

    return errors;
}

int modules_deinit_all(void)
{
    int errors = 0;

    for (size_t i = MODULE_COUNT; i-- > 0;) {
        module_entry_t *module = &g_modules[i];
        if (!g_module_inited[i]) {
            continue;
        }

        if (module->deinit && module->deinit() != 0) {
            errors++;
        }
        g_module_inited[i] = false;
    }

    return errors;
}


static int module_index_by_short_name(const char *short_name)
{
    if (!short_name) {
        return -1;
    }
    for (size_t i = 0; i < MODULE_COUNT; i++) {
        if (g_modules[i].short_name && strcmp(g_modules[i].short_name, short_name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

int modules_deinitialize_by_short_name(const char *short_name)
{
    int index = module_index_by_short_name(short_name);
    if (index < 0) {
        return -1;
    }

    module_entry_t *module = &g_modules[index];
    int rc = 0;
    if (module->deinit) {
        rc = module->deinit();
    }
    g_module_inited[index] = false;
    return rc;
}

int modules_reinitialize_by_short_name(const char *short_name)
{
    int index = module_index_by_short_name(short_name);
    if (index < 0) {
        return -1;
    }

    module_entry_t *module = &g_modules[index];
    if (module->deinit) {
        (void)module->deinit();
    }
    g_module_inited[index] = false;

    if (!module->init) {
        return -2;
    }

    int rc = module->init(module->mod_inst, module->bus_ctx, module->addr, module->en_pin);
    if (rc == 0) {
        g_module_inited[index] = true;
        log_printf("Module ready: %s\n", module->short_name);
    } else {
        log_printf("Module reinit failed: %s, rc=%d\n", module->short_name, rc);
    }
    return rc;
}

module_entry_t *modules_find_by_short_name(const char *short_name)
{
    if (!short_name) {
        return NULL;
    }

    for (size_t i = 0; i < MODULE_COUNT; i++) {
        if (g_module_inited[i] &&
            g_modules[i].short_name &&
            strcmp(g_modules[i].short_name, short_name) == 0) {
            return &g_modules[i];
        }
    }

    return NULL;
}


size_t modules_count(void)
{
    return MODULE_COUNT;
}

const module_entry_t *modules_get(size_t index)
{
    return index < MODULE_COUNT ? &g_modules[index] : NULL;
}

bool modules_is_initialized(size_t index)
{
    return index < MODULE_COUNT && g_module_inited[index];
}
