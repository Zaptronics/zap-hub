#include <string.h>

#include "pico/stdlib.h"
#include "modules.h"
#include "ztmb/log.h"
#include "zaptronics/boards/ztpico2/pins.h"
#include "zaptronics/boards/ztpico2/io_exp.h"

#include "ztmb/ztm_adc_adapter.h"
#include "ztmb/ztm_psu_hv_adapter.h"
#include "ztmb/ztm_psu_tec_adapter.h"
#include "ztmb/ztm_pwm_adapter.h"
#include "ztmb/ztm_stepper_adapter.h"

static module_entry_t g_modules[] = {
    {
        .short_name = "ztm_adc",
        .long_name  = "AD7124-8 temperature ADC (3 TC, CJC Pt1000, 8 RTD)",
        .init       = ztmb_ztm_adc_init,
        .bus_ctx    = (void *)ZTMB_SPI1_PORT,
        .mod_inst   = &ztm_adc_1,
        .addr       = ZTMB_ADC_CS_2,
        .en_pin     = EX_NONE,
        .poll       = ztmb_ztm_adc_poll,
        .deinit     = ztmb_ztm_adc_deinit,
    },
    {
        .short_name = "ztm_psu_hv",
        .long_name  = "High Voltage PSU (AD5282 setpoint, PG/EN, TLA2518 VSENSE)",
        .init       = ztmb_ztm_psu_hv_init,
        .bus_ctx    = (void *)ZTMB_I2C0_PORT,
        .mod_inst   = &ztm_psu_hv_1,
        .addr       = 0x00,
        .en_pin     = EX_HV_EN_1,
        .poll       = ztmb_ztm_psu_hv_poll,
        .deinit     = ztmb_ztm_psu_hv_deinit,
    },
    {
        .short_name = "ztm_stepper",
        .long_name  = "Stepper motor controller (4 x L6470 SPI chain)",
        .init       = ztmb_stepper_init,
        .bus_ctx    = (void *)ZTMB_SPI0_PORT,
        .mod_inst   = &ztm_stepper_1,
        .addr       = ZTMB_STEP_CS_1,
        .en_pin     = EX_STEP_EN_1,
        .poll       = ztmb_stepper_poll,
        .deinit     = ztmb_stepper_deinit,
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
        .init       = ztmb_ztm_psu_tec_init,
        .bus_ctx    = (void *)ZTMB_SPI0_PORT,
        .mod_inst   = &ztm_psu_tec_1,
        .addr       = ZTMB_TEC_CS_1,
        .en_pin     = EX_TEC_EN_1,
        .poll       = NULL,
        .deinit     = ztmb_ztm_psu_tec_deinit_1,
    },
    {
        .short_name = "psu_tec_2",
        .long_name  = "Thermoelectric cooler 2 (MCP4901 DAC on SPI0)",
        .init       = ztmb_ztm_psu_tec_init,
        .bus_ctx    = (void *)ZTMB_SPI0_PORT,
        .mod_inst   = &ztm_psu_tec_2,
        .addr       = ZTMB_TEC_CS_2,
        .en_pin     = EX_TEC_EN_2,
        .poll       = NULL,
        .deinit     = ztmb_ztm_psu_tec_deinit_2,
    },
    {
        .short_name = "psu_tec_3",
        .long_name  = "Thermoelectric cooler 3 (MCP4901 DAC on SPI0)",
        .init       = ztmb_ztm_psu_tec_init,
        .bus_ctx    = (void *)ZTMB_SPI0_PORT,
        .mod_inst   = &ztm_psu_tec_3,
        .addr       = ZTMB_TEC_CS_3,
        .en_pin     = EX_TEC_EN_3,
        .poll       = NULL,
        .deinit     = ztmb_ztm_psu_tec_deinit_3,
    },
    {
        .short_name = "psu_tec_4",
        .long_name  = "Thermoelectric cooler 4 (MCP4901 DAC on SPI0)",
        .init       = ztmb_ztm_psu_tec_init,
        .bus_ctx    = (void *)ZTMB_SPI0_PORT,
        .mod_inst   = &ztm_psu_tec_4,
        .addr       = ZTMB_TEC_CS_4,
        .en_pin     = EX_TEC_EN_4,
        .poll       = NULL,
        .deinit     = ztmb_ztm_psu_tec_deinit_4,
    },
};

#define MODULE_COUNT (sizeof(g_modules) / sizeof(g_modules[0]))

static bool g_module_inited[MODULE_COUNT];
static void registry_event(void *context, const module_entry_t *entry, const char *operation, int rc)
{
    (void)context;
    log_printf("Module %s: %s rc=%d\n", operation, entry->short_name, rc);
}
static void registry_yield(void *context) { (void)context; log_poll(); }
static zap_registry_t registry = {
    .entries=g_modules, .initialized=g_module_inited, .count=MODULE_COUNT,
    .event=registry_event, .yield=registry_yield
};
int modules_init_all(bool rollback) { return zap_registry_init_all(&registry,rollback); }
int modules_poll_all(void) { return zap_registry_poll_all(&registry); }
int modules_deinit_all(void) { return zap_registry_deinit_all(&registry); }
int modules_reinitialize_by_short_name(const char *name) { return zap_registry_reinitialize(&registry,name); }
int modules_deinitialize_by_short_name(const char *name) { return zap_registry_deinitialize(&registry,name); }
module_entry_t *modules_find_by_short_name(const char *name) { return zap_registry_find(&registry,name); }
size_t modules_count(void) { return registry.count; }
const module_entry_t *modules_get(size_t i) { return zap_registry_get(&registry,i); }
bool modules_is_initialized(size_t i) { return zap_registry_is_initialized(&registry,i); }
