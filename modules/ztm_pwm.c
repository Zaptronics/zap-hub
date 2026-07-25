#include "modules/ztm_pwm.h"

#include <string.h>

#include "pico/stdlib.h"
#include "ztmb/io_exp.h"
#include "ztmb/log.h"

pwm_t ztm_pwm_1;

/* Logical channel -> SX1509 IO pin. */
static const uint8_t g_channel_to_sx_pin[ZTM_PWM_CHANNEL_COUNT] = {
    15u, 14u, 13u, 12u, 11u, 10u, 9u, 7u, 5u, 4u, 2u, 0u
};

/* GPIO1..4 -> SX1509 IO pin. */
static const uint8_t g_gpio_to_sx_pin[ZTM_PWM_GPIO_COUNT] = {
    8u, 6u, 3u, 1u
};

typedef struct {
    ztm_pwm_current_source_t source;
    uint8_t input_channel;
} current_route_t;

/*
 * Intended PAC1954 routing within each four-load group:
 *   logical 1,2,3,4 -> VSENSE2, VSENSE1, VSENSE4, VSENSE3.
 */
static const current_route_t g_native_route[4] = {
    {ZTM_PWM_CURRENT_SOURCE_VSENSE, 2u},
    {ZTM_PWM_CURRENT_SOURCE_VSENSE, 1u},
    {ZTM_PWM_CURRENT_SOURCE_VSENSE, 4u},
    {ZTM_PWM_CURRENT_SOURCE_VSENSE, 3u},
};

/*
 * Salvage routing when PAC1952-2 is fitted in the PAC1954 VQFN footprint:
 *   logical 1 -> VBUS1, logical 2 -> VSENSE1,
 *   logical 3 -> VSENSE2, logical 4 -> VBUS2.
 * VBUS-derived current is valid but about 320x coarser than VSENSE.
 */
static const current_route_t g_pac1952_2_route[4] = {
    {ZTM_PWM_CURRENT_SOURCE_VBUS,   1u},
    {ZTM_PWM_CURRENT_SOURCE_VSENSE, 1u},
    {ZTM_PWM_CURRENT_SOURCE_VSENSE, 2u},
    {ZTM_PWM_CURRENT_SOURCE_VBUS,   2u},
};

typedef struct {
    uint8_t emc_index;
    uint8_t emc_fan;
} fan_route_t;

static const fan_route_t g_fan_route[ZTM_PWM_FAN_COUNT] = {
    {0u, 1u}, {0u, 2u}, {0u, 3u},
    {1u, 1u}, {1u, 2u}, {1u, 3u},
};

static bool valid_channel(uint8_t channel)
{
    return channel >= 1u && channel <= ZTM_PWM_CHANNEL_COUNT;
}

static bool valid_fan(uint8_t fan)
{
    return fan >= 1u && fan <= ZTM_PWM_FAN_COUNT;
}

static bool valid_gpio(uint8_t gpio)
{
    return gpio >= 1u && gpio <= ZTM_PWM_GPIO_COUNT;
}

static bool module_set_enable(pwm_t *device, bool enabled);
static bool fan_set_output_mode_hw(pwm_t *device,
                                   uint8_t fan_1based,
                                   ztm_pwm_fan_output_mode_t mode);

const char *ztm_pwm_init_stage_name(ztm_pwm_init_stage_t stage)
{
    switch (stage) {
        case ZTM_PWM_INIT_CONNECTOR_PINS: return "connector pins";
        case ZTM_PWM_INIT_POWER_OFF: return "power off";
        case ZTM_PWM_INIT_POWER_ON: return "power on";
        case ZTM_PWM_INIT_SX1509: return "SX1509";
        case ZTM_PWM_INIT_LOW_SIDE_OUTPUTS: return "low-side outputs";
        case ZTM_PWM_INIT_USER_GPIOS: return "user GPIOs";
        case ZTM_PWM_INIT_PAC1: return "PAC1";
        case ZTM_PWM_INIT_PAC2: return "PAC2";
        case ZTM_PWM_INIT_PAC3: return "PAC3";
        case ZTM_PWM_INIT_EMC1: return "EMC1";
        case ZTM_PWM_INIT_EMC2: return "EMC2";
        case ZTM_PWM_INIT_FAN_SAFE_STATE: return "fan safe state";
        case ZTM_PWM_INIT_FAN_OUTPUT_CONFIG: return "fan output config";
        case ZTM_PWM_INIT_FAN_INTERRUPTS: return "fan interrupts";
        case ZTM_PWM_INIT_CLEAR_STATUS: return "clear status";
        case ZTM_PWM_INIT_READY: return "ready";
        default: return "not started";
    }
}

const char *ztm_pwm_pac_topology_name(ztm_pwm_pac_topology_t topology)
{
    switch (topology) {
        case ZTM_PWM_PAC_TOPOLOGY_NATIVE_4CH: return "PAC1954 native 4-channel";
        case ZTM_PWM_PAC_TOPOLOGY_PAC1952_2_SALVAGE: return "PAC1952-2 salvage";
        case ZTM_PWM_PAC_TOPOLOGY_UNSUPPORTED: return "unsupported PAC195x";
        default: return "offline";
    }
}

const char *ztm_pwm_current_source_name(ztm_pwm_current_source_t source)
{
    switch (source) {
        case ZTM_PWM_CURRENT_SOURCE_VSENSE: return "VSENSE";
        case ZTM_PWM_CURRENT_SOURCE_VBUS: return "VBUS";
        default: return "none";
    }
}

const char *ztm_pwm_fan_output_mode_name(ztm_pwm_fan_output_mode_t mode)
{
    switch (mode) {
        case ZTM_PWM_FAN_OUTPUT_OPEN_DRAIN: return "open-drain";
        case ZTM_PWM_FAN_OUTPUT_PUSH_PULL: return "push-pull";
        default: return "unknown";
    }
}

static bool fan_set_output_mode_hw(pwm_t *device,
                                   uint8_t fan_1based,
                                   ztm_pwm_fan_output_mode_t mode)
{
    if (!device || !valid_fan(fan_1based) ||
        (mode != ZTM_PWM_FAN_OUTPUT_OPEN_DRAIN &&
         mode != ZTM_PWM_FAN_OUTPUT_PUSH_PULL)) {
        return false;
    }

    fan_route_t route = g_fan_route[fan_1based - 1u];
    emc2303_pwm_output_type_t type = mode == ZTM_PWM_FAN_OUTPUT_PUSH_PULL
                                         ? EMC2303_PWM_OUTPUT_PUSH_PULL
                                         : EMC2303_PWM_OUTPUT_OPEN_DRAIN;
    return emc2303_fan_set_pwm_output_type(&device->fan_controller[route.emc_index],
                                            route.emc_fan,
                                            type) == EMC2303_OK;
}

static uint8_t pac_channels_from_product_id(uint8_t product_id)
{
    switch (product_id) {
        case 0x78u: case 0x7Cu: return 1u;
        case 0x79u: case 0x7Du: return 2u;
        case 0x7Au: case 0x7Eu: return 3u;
        case 0x7Bu: case 0x7Fu: return 4u;
        default: return 0u;
    }
}

static ztm_pwm_pac_topology_t pac_topology_from_identity(uint8_t product_id,
                                                          uint8_t channels)
{
    if (product_id == 0x7Du && channels == 2u) {
        return ZTM_PWM_PAC_TOPOLOGY_PAC1952_2_SALVAGE;
    }
    if (product_id == 0x7Bu && channels == 4u) {
        return ZTM_PWM_PAC_TOPOLOGY_NATIVE_4CH;
    }
    return ZTM_PWM_PAC_TOPOLOGY_UNSUPPORTED;
}

static int pwm_init_failed(pwm_t *device, ztm_pwm_init_stage_t stage, int rc)
{
    if (device) {
        device->ready = false;
        device->last_init_stage = stage;
        device->last_init_rc = rc;
        if (device->enable_pin >= 0) {
            (void)module_set_enable(device, false);
        }
    }
    log_printf("PWM init failed at %s, rc=%d\n", ztm_pwm_init_stage_name(stage), rc);
    return rc;
}

static bool module_set_enable(pwm_t *device, bool enabled)
{
    if (!device || device->enable_pin < 0) {
        return false;
    }
    return pcal6416a_write_pin(&ioexp, (uint8_t)device->enable_pin, enabled);
}

static int init_current_monitor(pac195x_t *monitor,
                                i2c_inst_t *i2c,
                                uint8_t address,
                                ztm_pwm_pac_topology_t *topology)
{
    static const bus_i2c_config_t config = {
        .baud_hz = 400000u,
        .timeout_us = 5000u,
    };

    if (!topology) {
        return PAC195X_ERR_ARG;
    }
    *topology = ZTM_PWM_PAC_TOPOLOGY_NONE;
    memset(monitor, 0, sizeof(*monitor));
    if (!bus_i2c_init(&monitor->i2c_bus, i2c, address, EX_NONE, &config)) {
        return PAC195X_ERR_ARG;
    }

    monitor->set_smbus_settings = true;
    monitor->no_skip = false;
    monitor->byte_count = false;
    monitor->i2c_hispeed = false;

    monitor->set_ctrl = true;
    monitor->sample_mode = PAC195X_SAMPLE_ADAPTIVE_1024;
    /* Zero lets the generic driver enable only channels implemented by the detected part. */
    monitor->enabled_mask = 0u;
    monitor->slow_alert1_cfg = 0u;
    monitor->gpio_alert2_cfg = 0u;

    monitor->set_ranges = true;
    for (size_t i = 0; i < 4u; i++) {
        monitor->vbus_range[i] = PAC195X_VBUS_UNIPOLAR_32V;
        monitor->vsense_range[i] = PAC195X_VSENSE_UNIPOLAR_100mV;
        monitor->shunt_uohm[i] = ZTM_PWM_SHUNT_UOHM;
    }

    monitor->init_refresh_kind = PAC195X_REFRESH_NO_RESET;

    int rc = pac195x_init(monitor);
    if (rc != PAC195X_OK) {
        return rc;
    }

    *topology = pac_topology_from_identity(monitor->product_id, monitor->channels);
    return *topology == ZTM_PWM_PAC_TOPOLOGY_UNSUPPORTED ? PAC195X_ERR_ID : PAC195X_OK;
}

static bool channel_start_at_zero(pwm_t *device, uint8_t channel)
{
    uint8_t pin = g_channel_to_sx_pin[channel - 1u];
    int rc = sx1509_pwm_pin_start(&device->output_mux, pin, 0u);
    if (rc == SX1509_OK) {
        device->channel_duty_pct[channel - 1u] = 0u;
        return true;
    }
    return false;
}

int ztm_pwm_init(void *ctx, void *i2c, int8_t addr, int8_t en_pin)
{
    (void)addr;

    if (!ctx || !i2c || en_pin < 0) {
        return -1;
    }

    pwm_t *device = (pwm_t *)ctx;
    memset(device, 0, sizeof(*device));
    device->i2c = (i2c_inst_t *)i2c;
    device->enable_pin = en_pin;
    device->irq_pin = EX_I2C_ALT_IO_1;
    device->last_init_stage = ZTM_PWM_INIT_CONNECTOR_PINS;

    /* Make the connector controls explicit even though io_exp_init sets these defaults. */
    if (!pcal6416a_set_pin_direction(&ioexp, (uint8_t)device->enable_pin, false) ||
        !pcal6416a_set_pin_direction(&ioexp, (uint8_t)device->irq_pin, true)) {
        return pwm_init_failed(device, ZTM_PWM_INIT_CONNECTOR_PINS, -2);
    }

    device->last_init_stage = ZTM_PWM_INIT_POWER_OFF;
    if (!module_set_enable(device, false)) {
        return pwm_init_failed(device, ZTM_PWM_INIT_POWER_OFF, -3);
    }
    busy_wait_ms(1);

    device->last_init_stage = ZTM_PWM_INIT_POWER_ON;
    if (!module_set_enable(device, true)) {
        return pwm_init_failed(device, ZTM_PWM_INIT_POWER_ON, -4);
    }
    busy_wait_ms(10);

    device->last_init_stage = ZTM_PWM_INIT_SX1509;
    int rc = sx1509_init(&device->output_mux, device->i2c, ZTM_PWM_SX1509_ADDR);
    if (rc != SX1509_OK) {
        return pwm_init_failed(device, ZTM_PWM_INIT_SX1509, -10);
    }

    device->last_init_stage = ZTM_PWM_INIT_LOW_SIDE_OUTPUTS;
    /* Configure all twelve power channels as PWM outputs in their safe 0% state. */
    for (uint8_t channel = 1u; channel <= ZTM_PWM_CHANNEL_COUNT; channel++) {
        if (!channel_start_at_zero(device, channel)) {
            return pwm_init_failed(device, ZTM_PWM_INIT_LOW_SIDE_OUTPUTS, -11);
        }
    }

    device->last_init_stage = ZTM_PWM_INIT_USER_GPIOS;
    /* User GPIOs are inputs with no internal pulls until explicitly configured. */
    for (uint8_t gpio = 1u; gpio <= ZTM_PWM_GPIO_COUNT; gpio++) {
        uint8_t pin = g_gpio_to_sx_pin[gpio - 1u];
        if (sx1509_pin_configure(&device->output_mux, pin, SX1509_PIN_INPUT) != SX1509_OK) {
            return pwm_init_failed(device, ZTM_PWM_INIT_USER_GPIOS, -12);
        }
        device->gpio_mode[gpio - 1u] = ZTM_PWM_GPIO_INPUT;
    }

    static const uint8_t pac_addresses[ZTM_PWM_PAC_COUNT] = {
        ZTM_PWM_PAC1_ADDR, ZTM_PWM_PAC2_ADDR, ZTM_PWM_PAC3_ADDR
    };
    static const ztm_pwm_init_stage_t pac_stages[ZTM_PWM_PAC_COUNT] = {
        ZTM_PWM_INIT_PAC1, ZTM_PWM_INIT_PAC2, ZTM_PWM_INIT_PAC3
    };

    size_t pac_ready_count = 0u;
    for (size_t index = 0; index < ZTM_PWM_PAC_COUNT; index++) {
        device->last_init_stage = pac_stages[index];
        rc = init_current_monitor(&device->current_monitor[index],
                                  device->i2c,
                                  pac_addresses[index],
                                  &device->current_monitor_topology[index]);
        device->current_monitor_rc[index] = rc;
        device->current_monitor_ready[index] = rc == PAC195X_OK;
        if (device->current_monitor_ready[index]) {
            pac_ready_count++;
            log_printf("PWM PAC%u ready: PID=0x%02X, %s\n",
                       (unsigned)(index + 1u),
                       device->current_monitor[index].product_id,
                       ztm_pwm_pac_topology_name(device->current_monitor_topology[index]));
        } else {
            log_printf("ERROR: PWM PAC%u unavailable at 0x%02X, rc=%d; CH%u..CH%u current unavailable\n",
                       (unsigned)(index + 1u),
                       pac_addresses[index],
                       rc,
                       (unsigned)(index * 4u + 1u),
                       (unsigned)(index * 4u + 4u));
        }
    }

    device->last_init_stage = ZTM_PWM_INIT_EMC1;
    rc = emc2303_init(&device->fan_controller[0], device->i2c, ZTM_PWM_EMC1_ADDR);
    if (rc != EMC2303_OK) {
        log_printf("PWM EMC1 init detail rc=%d\n", rc);
        return pwm_init_failed(device, ZTM_PWM_INIT_EMC1, -30);
    }

    device->last_init_stage = ZTM_PWM_INIT_EMC2;
    rc = emc2303_init(&device->fan_controller[1], device->i2c, ZTM_PWM_EMC2_ADDR);
    if (rc != EMC2303_OK) {
        log_printf("PWM EMC2 init detail rc=%d\n", rc);
        return pwm_init_failed(device, ZTM_PWM_INIT_EMC2, -31);
    }

    device->last_init_stage = ZTM_PWM_INIT_FAN_SAFE_STATE;
    /* A zero drive write also disables the EMC2303 power-up watchdog. */
    for (uint8_t fan = 1u; fan <= ZTM_PWM_FAN_COUNT; fan++) {
        fan_route_t route = g_fan_route[fan - 1u];
        if (emc2303_fan_set_pwm_raw(&device->fan_controller[route.emc_index],
                                    route.emc_fan,
                                    0u) != EMC2303_OK) {
            return pwm_init_failed(device, ZTM_PWM_INIT_FAN_SAFE_STATE, -32);
        }
    }

    device->last_init_stage = ZTM_PWM_INIT_FAN_OUTPUT_CONFIG;
    for (uint8_t fan = 1u; fan <= ZTM_PWM_FAN_COUNT; fan++) {
        ztm_pwm_fan_output_mode_t mode =
            (ZTM_PWM_FAN_DEFAULT_PUSH_PULL_MASK & (1u << (fan - 1u))) != 0u
                ? ZTM_PWM_FAN_OUTPUT_PUSH_PULL
                : ZTM_PWM_FAN_OUTPUT_OPEN_DRAIN;
        if (!fan_set_output_mode_hw(device, fan, mode)) {
            return pwm_init_failed(device, ZTM_PWM_INIT_FAN_OUTPUT_CONFIG, -33);
        }
    }

    device->last_init_stage = ZTM_PWM_INIT_FAN_INTERRUPTS;
    /* Enable ALERT reporting for all implemented fan channels. */
    if (emc2303_reg_write8(&device->fan_controller[0], EMC2303_REG_FAN_INT_ENABLE, 0x07u) != EMC2303_OK ||
        emc2303_reg_write8(&device->fan_controller[1], EMC2303_REG_FAN_INT_ENABLE, 0x07u) != EMC2303_OK) {
        return pwm_init_failed(device, ZTM_PWM_INIT_FAN_INTERRUPTS, -34);
    }

    device->last_init_stage = ZTM_PWM_INIT_CLEAR_STATUS;
    /* Clear any power-up status before declaring the module ready. */
    uint8_t unused = 0;
    (void)emc2303_read_status(&device->fan_controller[0], &unused, NULL, NULL, NULL);
    (void)emc2303_read_status(&device->fan_controller[1], &unused, NULL, NULL, NULL);
    for (size_t i = 0; i < ZTM_PWM_PAC_COUNT; i++) {
        if (device->current_monitor_ready[i]) {
            uint32_t alert = 0;
            (void)pac195x_read_alert_status(&device->current_monitor[i], &alert);
        }
    }
    uint16_t sx_source = 0;
    if (sx1509_interrupt_get_source(&device->output_mux, &sx_source) == SX1509_OK && sx_source != 0u) {
        (void)sx1509_interrupt_clear_source(&device->output_mux, sx_source);
    }

    device->ready = true;
    device->last_init_stage = ZTM_PWM_INIT_READY;
    device->last_init_rc = 0;
    log_printf("PWM module ready: 12 low-side channels, 6 fan PWM/TACH channels, 4 GPIOs; %u/3 PAC monitors\n",
               (unsigned)pac_ready_count);
    return 0;
}

bool ztm_pwm_ready(void)
{
    return ztm_pwm_1.ready;
}

bool ztm_pwm_read_enable(bool *enabled)
{
    pwm_t *device = &ztm_pwm_1;
    if (!enabled || device->enable_pin < 0) {
        return false;
    }
    return pcal6416a_read_pin(&ioexp, (uint8_t)device->enable_pin, enabled);
}

bool ztm_pwm_read_irq_asserted(bool *asserted)
{
    pwm_t *device = &ztm_pwm_1;
    bool line_high = true;
    if (!asserted || device->irq_pin < 0 ||
        !pcal6416a_read_pin(&ioexp, (uint8_t)device->irq_pin, &line_high)) {
        return false;
    }
    *asserted = !line_high;
    return true;
}

int ztm_pwm_probe_devices(ztm_pwm_probe_result_t *result, bool ensure_power_on)
{
    if (!result) {
        return -1;
    }

    memset(result, 0, sizeof(*result));
    pwm_t *device = &ztm_pwm_1;
    bool has_context = device->i2c != NULL;
    i2c_inst_t *i2c = has_context ? device->i2c : ZTMB_I2C0_PORT;
    int8_t enable_pin = has_context ? device->enable_pin : EX_I2C_ALT_EN_1;
    int8_t irq_pin = has_context ? device->irq_pin : EX_I2C_ALT_IO_1;

    if (!pcal6416a_set_pin_direction(&ioexp, (uint8_t)enable_pin, false) ||
        !pcal6416a_set_pin_direction(&ioexp, (uint8_t)irq_pin, true)) {
        return -2;
    }

    bool original_enable = false;
    bool original_enable_valid = pcal6416a_read_pin(&ioexp, (uint8_t)enable_pin, &original_enable);
    result->power_was_enabled = original_enable_valid && original_enable;
    result->power_control_ok = true;

    if (ensure_power_on && (!original_enable_valid || !original_enable)) {
        if (!pcal6416a_write_pin(&ioexp, (uint8_t)enable_pin, true)) {
            result->power_control_ok = false;
            return -3;
        }
        result->power_temporarily_enabled = true;
        busy_wait_ms(10);
    }

    result->enable_read_ok = pcal6416a_read_pin(&ioexp, (uint8_t)enable_pin, &result->enable_high);
    bool irq_high = true;
    result->irq_read_ok = pcal6416a_read_pin(&ioexp, (uint8_t)irq_pin, &irq_high);
    result->irq_asserted = result->irq_read_ok && !irq_high;

    static const bus_i2c_config_t config = {
        .baud_hz = 400000u,
        .timeout_us = 5000u,
    };

    sx1509_t sx = {0};
    if (bus_i2c_init(&sx.i2c_bus, i2c, ZTM_PWM_SX1509_ADDR, EX_NONE, &config) &&
        sx1509_reg_read8(&sx, SX1509_REG_DIR_A, &result->sx_dir_a) == SX1509_OK) {
        result->sx_present = true;
        result->devices_found++;
    }

    static const uint8_t pac_addresses[ZTM_PWM_PAC_COUNT] = {
        ZTM_PWM_PAC1_ADDR, ZTM_PWM_PAC2_ADDR, ZTM_PWM_PAC3_ADDR
    };
    for (size_t index = 0; index < ZTM_PWM_PAC_COUNT; index++) {
        pac195x_t pac = {0};
        if (!bus_i2c_init(&pac.i2c_bus, i2c, pac_addresses[index], EX_NONE, &config)) {
            continue;
        }
        int rc = pac195x_reg_read_u8(&pac, PAC195X_REG_MANUFACTURER_ID,
                                     &result->pac_manufacturer_id[index]);
        if (rc == PAC195X_OK) {
            rc = pac195x_reg_read_u8(&pac, PAC195X_REG_PRODUCT_ID,
                                     &result->pac_product_id[index]);
        }
        if (rc == PAC195X_OK) {
            rc = pac195x_reg_read_u8(&pac, PAC195X_REG_REVISION_ID,
                                     &result->pac_revision_id[index]);
        }
        if (rc == PAC195X_OK) {
            result->pac_present[index] = true;
            result->pac_channels[index] = pac_channels_from_product_id(result->pac_product_id[index]);
            result->devices_found++;
        }
    }

    static const uint8_t emc_addresses[ZTM_PWM_EMC_COUNT] = {
        ZTM_PWM_EMC1_ADDR, ZTM_PWM_EMC2_ADDR
    };
    for (size_t index = 0; index < ZTM_PWM_EMC_COUNT; index++) {
        emc2303_t emc = {0};
        if (!bus_i2c_init(&emc.i2c_bus, i2c, emc_addresses[index], EX_NONE, &config)) {
            continue;
        }
        if (emc2303_read_ids(&emc,
                             &result->emc_product_id[index],
                             &result->emc_manufacturer_id[index],
                             &result->emc_revision_id[index]) == EMC2303_OK) {
            result->emc_present[index] = true;
            result->devices_found++;
        }
    }

    if (result->power_temporarily_enabled) {
        result->power_restored_off = pcal6416a_write_pin(&ioexp, (uint8_t)enable_pin, false);
        if (!result->power_restored_off) {
            result->power_control_ok = false;
            return -4;
        }
    }

    return result->devices_found == 6u ? 0 : -(int)(6u - result->devices_found);
}

int ztm_pwm_i2c_scan(ztm_pwm_i2c_scan_result_t *result, bool ensure_power_on)
{
    if (!result) {
        return -1;
    }

    memset(result, 0, sizeof(*result));
    pwm_t *device = &ztm_pwm_1;
    bool has_context = device->i2c != NULL;
    i2c_inst_t *i2c = has_context ? device->i2c : ZTMB_I2C0_PORT;
    int8_t enable_pin = has_context ? device->enable_pin : EX_I2C_ALT_EN_1;

    if (!pcal6416a_set_pin_direction(&ioexp, (uint8_t)enable_pin, false)) {
        return -2;
    }

    bool original_enable = false;
    bool original_enable_valid = pcal6416a_read_pin(&ioexp, (uint8_t)enable_pin, &original_enable);
    result->power_was_enabled = original_enable_valid && original_enable;
    result->power_control_ok = true;

    if (ensure_power_on && (!original_enable_valid || !original_enable)) {
        if (!pcal6416a_write_pin(&ioexp, (uint8_t)enable_pin, true)) {
            result->power_control_ok = false;
            return -3;
        }
        result->power_temporarily_enabled = true;
        busy_wait_ms(10);
    }

    static const bus_i2c_config_t config = {
        .baud_hz = 400000u,
        .timeout_us = 2000u,
    };
    for (uint8_t address = 0x08u; address <= 0x77u; address++) {
        bus_i2c_t probe = {0};
        if (bus_i2c_init(&probe, i2c, address, EX_NONE, &config) &&
            bus_i2c_probe(&probe)) {
            result->address_present[address] = true;
            result->devices_found++;
        }
    }

    if (result->power_temporarily_enabled) {
        result->power_restored_off = pcal6416a_write_pin(&ioexp, (uint8_t)enable_pin, false);
        if (!result->power_restored_off) {
            result->power_control_ok = false;
            return -4;
        }
    }

    return (int)result->devices_found;
}

bool ztm_pwm_power_off(void)
{
    pwm_t *device = &ztm_pwm_1;
    bool stopped = !device->ready || ztm_pwm_stop_all();
    bool disabled = device->enable_pin >= 0 && module_set_enable(device, false);
    device->ready = false;
    return stopped && disabled;
}

bool ztm_pwm_reinitialize(void)
{
    pwm_t *device = &ztm_pwm_1;
    i2c_inst_t *i2c = device->i2c;
    int8_t enable_pin = device->enable_pin;
    if (!i2c || enable_pin < 0) {
        return false;
    }

    (void)ztm_pwm_power_off();
    return ztm_pwm_init(device, i2c, 0, enable_pin) == 0;
}

uint8_t ztm_pwm_channel_get_sx_pin(uint8_t channel_1based)
{
    return valid_channel(channel_1based) ? g_channel_to_sx_pin[channel_1based - 1u] : UINT8_MAX;
}

uint8_t ztm_pwm_gpio_get_sx_pin(uint8_t gpio_1based)
{
    return valid_gpio(gpio_1based) ? g_gpio_to_sx_pin[gpio_1based - 1u] : UINT8_MAX;
}

static bool resolve_current_route(const pwm_t *device,
                                  uint8_t channel_1based,
                                  size_t *pac_index,
                                  current_route_t *route)
{
    if (!device || !valid_channel(channel_1based) || !pac_index || !route) {
        return false;
    }

    size_t pac = (size_t)((channel_1based - 1u) / 4u);
    size_t local = (size_t)((channel_1based - 1u) % 4u);
    const current_route_t *table = NULL;
    switch (device->current_monitor_topology[pac]) {
        case ZTM_PWM_PAC_TOPOLOGY_NATIVE_4CH:
            table = g_native_route;
            break;
        case ZTM_PWM_PAC_TOPOLOGY_PAC1952_2_SALVAGE:
            table = g_pac1952_2_route;
            break;
        default:
            return false;
    }

    *pac_index = pac;
    *route = table[local];
    return true;
}

bool ztm_pwm_channel_get_current_route(uint8_t channel_1based,
                                       uint8_t *pac_1based,
                                       ztm_pwm_current_source_t *source,
                                       uint8_t *input_1based)
{
    if (!pac_1based || !source || !input_1based) {
        return false;
    }

    size_t pac = 0u;
    current_route_t route;
    if (!resolve_current_route(&ztm_pwm_1, channel_1based, &pac, &route)) {
        return false;
    }

    *pac_1based = (uint8_t)(pac + 1u);
    *source = route.source;
    *input_1based = route.input_channel;
    return true;
}

bool ztm_pwm_channel_set(uint8_t channel_1based, uint8_t duty_pct)
{
    pwm_t *device = &ztm_pwm_1;
    if (!device->ready || !valid_channel(channel_1based) || duty_pct > 100u) {
        return false;
    }

    uint8_t pin = g_channel_to_sx_pin[channel_1based - 1u];
    if (sx1509_pwm_pin_set_duty(&device->output_mux, pin, duty_pct) != SX1509_OK) {
        return false;
    }

    device->channel_duty_pct[channel_1based - 1u] = duty_pct;
    return true;
}

bool ztm_pwm_channel_set_all(uint8_t duty_pct)
{
    if (!ztm_pwm_1.ready || duty_pct > 100u) {
        return false;
    }

    bool ok = true;
    for (uint8_t channel = 1u; channel <= ZTM_PWM_CHANNEL_COUNT; channel++) {
        ok = ztm_pwm_channel_set(channel, duty_pct) && ok;
    }
    return ok;
}

bool ztm_pwm_channel_stop(uint8_t channel_1based)
{
    /* Keep the LED/PWM engine enabled at 0%; disabling it releases the open-drain output high. */
    return ztm_pwm_channel_set(channel_1based, 0u);
}

bool ztm_pwm_stop_all(void)
{
    bool outputs_ok = !ztm_pwm_1.ready || ztm_pwm_channel_set_all(0u);
    bool fans_ok = !ztm_pwm_1.ready || ztm_pwm_fan_stop_all();
    return outputs_ok && fans_ok;
}

uint8_t ztm_pwm_channel_get_duty(uint8_t channel_1based)
{
    return valid_channel(channel_1based) ? ztm_pwm_1.channel_duty_pct[channel_1based - 1u] : 0u;
}

bool ztm_pwm_read_channel(uint8_t channel_1based,
                          bool averaged,
                          ztm_pwm_current_reading_t *reading)
{
    pwm_t *device = &ztm_pwm_1;
    if (!device->ready || !valid_channel(channel_1based) || !reading) {
        return false;
    }

    size_t pac = 0u;
    current_route_t route;
    if (!resolve_current_route(device, channel_1based, &pac, &route) ||
        !device->current_monitor_ready[pac]) {
        return false;
    }

    pac195x_t *monitor = &device->current_monitor[pac];
    if (pac195x_refresh(monitor, PAC195X_REFRESH_NO_RESET, true) != PAC195X_OK) {
        return false;
    }

    pac195x_shunt_reading_t shunt = {0};
    int rc;
    if (route.source == ZTM_PWM_CURRENT_SOURCE_VBUS) {
        rc = pac195x_read_vbus_shunt(monitor,
                                     route.input_channel,
                                     averaged,
                                     ZTM_PWM_SHUNT_UOHM,
                                     &shunt);
    } else if (route.source == ZTM_PWM_CURRENT_SOURCE_VSENSE) {
        rc = pac195x_read_vsense_shunt(monitor,
                                       route.input_channel,
                                       averaged,
                                       ZTM_PWM_SHUNT_UOHM,
                                       &shunt);
    } else {
        return false;
    }
    if (rc != PAC195X_OK) {
        return false;
    }

    reading->pac_1based = (uint8_t)(pac + 1u);
    reading->input_1based = route.input_channel;
    reading->source = route.source;
    reading->raw = shunt.raw;
    reading->shunt_uV = shunt.shunt_uV;
    reading->current_uA = shunt.current_uA;
    reading->coarse_resolution = route.source == ZTM_PWM_CURRENT_SOURCE_VBUS;
    return true;
}

bool ztm_pwm_read_current_uA(uint8_t channel_1based, bool averaged, int32_t *current_uA)
{
    if (!current_uA) {
        return false;
    }
    ztm_pwm_current_reading_t reading;
    if (!ztm_pwm_read_channel(channel_1based, averaged, &reading)) {
        return false;
    }
    *current_uA = reading.current_uA;
    return true;
}

bool ztm_pwm_read_all_currents_uA(bool averaged, int32_t current_uA[ZTM_PWM_CHANNEL_COUNT])
{
    if (!ztm_pwm_1.ready || !current_uA) {
        return false;
    }

    bool all_ok = true;
    for (uint8_t channel = 1u; channel <= ZTM_PWM_CHANNEL_COUNT; channel++) {
        if (!ztm_pwm_read_current_uA(channel, averaged, &current_uA[channel - 1u])) {
            current_uA[channel - 1u] = 0;
            all_ok = false;
        }
    }
    return all_ok;
}

bool ztm_pwm_fan_get_route(uint8_t fan_1based,
                           uint8_t *emc_1based,
                           uint8_t *local_fan_1based)
{
    if (!valid_fan(fan_1based) || !emc_1based || !local_fan_1based) {
        return false;
    }
    fan_route_t route = g_fan_route[fan_1based - 1u];
    *emc_1based = (uint8_t)(route.emc_index + 1u);
    *local_fan_1based = route.emc_fan;
    return true;
}

bool ztm_pwm_fan_set_raw(uint8_t fan_1based, uint8_t raw)
{
    pwm_t *device = &ztm_pwm_1;
    if (!device->ready || !valid_fan(fan_1based)) {
        return false;
    }

    fan_route_t route = g_fan_route[fan_1based - 1u];
    if (emc2303_fan_enable_closed_loop(&device->fan_controller[route.emc_index],
                                        route.emc_fan,
                                        false) != EMC2303_OK ||
        emc2303_fan_set_pwm_raw(&device->fan_controller[route.emc_index],
                                route.emc_fan,
                                raw) != EMC2303_OK) {
        return false;
    }

    device->fan_duty_pct[fan_1based - 1u] =
        (uint8_t)(((uint32_t)raw * 100u + 127u) / 255u);
    device->fan_closed_loop[fan_1based - 1u] = false;
    device->fan_target_rpm[fan_1based - 1u] = 0u;
    return true;
}

bool ztm_pwm_fan_set_open_loop(uint8_t fan_1based, uint8_t duty_pct)
{
    return ztm_pwm_fan_set(fan_1based, duty_pct);
}

bool ztm_pwm_fan_set_target_rpm(uint8_t fan_1based, uint32_t rpm)
{
    pwm_t *device = &ztm_pwm_1;
    if (!device->ready || !valid_fan(fan_1based) || rpm == 0u) {
        return false;
    }
    fan_route_t route = g_fan_route[fan_1based - 1u];
    if (emc2303_fan_set_target_rpm(&device->fan_controller[route.emc_index],
                                   route.emc_fan,
                                   rpm) != EMC2303_OK) {
        return false;
    }
    device->fan_closed_loop[fan_1based - 1u] = true;
    device->fan_target_rpm[fan_1based - 1u] = rpm;
    return true;
}

bool ztm_pwm_fan_set(uint8_t fan_1based, uint8_t duty_pct)
{
    pwm_t *device = &ztm_pwm_1;
    if (!device->ready || !valid_fan(fan_1based) || duty_pct > 100u) {
        return false;
    }

    fan_route_t route = g_fan_route[fan_1based - 1u];
    if (emc2303_fan_enable_closed_loop(&device->fan_controller[route.emc_index],
                                        route.emc_fan,
                                        false) != EMC2303_OK ||
        emc2303_fan_set_pwm_percent_x100(&device->fan_controller[route.emc_index],
                                         route.emc_fan,
                                         (uint16_t)duty_pct * 100u) != EMC2303_OK) {
        return false;
    }

    device->fan_duty_pct[fan_1based - 1u] = duty_pct;
    device->fan_closed_loop[fan_1based - 1u] = false;
    device->fan_target_rpm[fan_1based - 1u] = 0u;
    return true;
}

bool ztm_pwm_fan_set_all(uint8_t duty_pct)
{
    if (!ztm_pwm_1.ready || duty_pct > 100u) {
        return false;
    }

    bool ok = true;
    for (uint8_t fan = 1u; fan <= ZTM_PWM_FAN_COUNT; fan++) {
        ok = ztm_pwm_fan_set(fan, duty_pct) && ok;
    }
    return ok;
}

bool ztm_pwm_fan_set_output_mode(uint8_t fan_1based, ztm_pwm_fan_output_mode_t mode)
{
    pwm_t *device = &ztm_pwm_1;
    return device->ready && fan_set_output_mode_hw(device, fan_1based, mode);
}

bool ztm_pwm_fan_set_output_mode_all(ztm_pwm_fan_output_mode_t mode)
{
    if (!ztm_pwm_1.ready ||
        (mode != ZTM_PWM_FAN_OUTPUT_OPEN_DRAIN &&
         mode != ZTM_PWM_FAN_OUTPUT_PUSH_PULL)) {
        return false;
    }

    bool ok = true;
    for (uint8_t fan = 1u; fan <= ZTM_PWM_FAN_COUNT; fan++) {
        ok = fan_set_output_mode_hw(&ztm_pwm_1, fan, mode) && ok;
    }
    return ok;
}

bool ztm_pwm_fan_get_output_mode(uint8_t fan_1based, ztm_pwm_fan_output_mode_t *mode)
{
    pwm_t *device = &ztm_pwm_1;
    if (!device->ready || !valid_fan(fan_1based) || !mode) {
        return false;
    }

    fan_route_t route = g_fan_route[fan_1based - 1u];
    emc2303_pwm_output_type_t type = EMC2303_PWM_OUTPUT_OPEN_DRAIN;
    if (emc2303_fan_get_pwm_output_type(&device->fan_controller[route.emc_index],
                                         route.emc_fan,
                                         &type) != EMC2303_OK) {
        return false;
    }

    *mode = type == EMC2303_PWM_OUTPUT_PUSH_PULL
                ? ZTM_PWM_FAN_OUTPUT_PUSH_PULL
                : ZTM_PWM_FAN_OUTPUT_OPEN_DRAIN;
    return true;
}

bool ztm_pwm_fan_stop(uint8_t fan_1based)
{
    return ztm_pwm_fan_set(fan_1based, 0u);
}

bool ztm_pwm_fan_stop_all(void)
{
    return ztm_pwm_fan_set_all(0u);
}

bool ztm_pwm_fan_get_rpm(uint8_t fan_1based, uint32_t *rpm)
{
    uint16_t tach_count = 0u;
    return ztm_pwm_fan_get_tach_status(fan_1based, &tach_count, rpm) !=
           ZTM_PWM_FAN_TACH_READ_ERROR;
}

bool ztm_pwm_fan_get_tach(uint8_t fan_1based, uint16_t *tach_count, uint32_t *rpm)
{
    return ztm_pwm_fan_get_tach_status(fan_1based, tach_count, rpm) !=
           ZTM_PWM_FAN_TACH_READ_ERROR;
}

ztm_pwm_fan_tach_status_t ztm_pwm_fan_get_tach_status(uint8_t fan_1based,
                                                       uint16_t *tach_count,
                                                       uint32_t *rpm)
{
    pwm_t *device = &ztm_pwm_1;
    if (!device->ready || !valid_fan(fan_1based) || !tach_count || !rpm) {
        return ZTM_PWM_FAN_TACH_READ_ERROR;
    }

    fan_route_t route = g_fan_route[fan_1based - 1u];
    emc2303_t *controller = &device->fan_controller[route.emc_index];
    if (emc2303_fan_get_tach_counts(controller, route.emc_fan, tach_count) != EMC2303_OK) {
        return ZTM_PWM_FAN_TACH_READ_ERROR;
    }

    if (*tach_count == EMC2303_TACH_COUNT_STALL) {
        *rpm = 0u;
        return ZTM_PWM_FAN_TACH_STALL;
    }

    if (emc2303_fan_tach_counts_to_rpm(controller,
                                         route.emc_fan,
                                         *tach_count,
                                         rpm) != EMC2303_OK) {
        return ZTM_PWM_FAN_TACH_READ_ERROR;
    }

    return ZTM_PWM_FAN_TACH_OK;
}

uint8_t ztm_pwm_fan_get_duty(uint8_t fan_1based)
{
    return valid_fan(fan_1based) ? ztm_pwm_1.fan_duty_pct[fan_1based - 1u] : 0u;
}

bool ztm_pwm_gpio_configure_input(uint8_t gpio_1based, ztm_pwm_gpio_pull_t pull)
{
    pwm_t *device = &ztm_pwm_1;
    if (!device->ready || !valid_gpio(gpio_1based)) {
        return false;
    }

    uint32_t flags = SX1509_PIN_INPUT;
    if (pull == ZTM_PWM_GPIO_PULL_UP) {
        flags |= SX1509_PIN_PULLUP;
    } else if (pull == ZTM_PWM_GPIO_PULL_DOWN) {
        flags |= SX1509_PIN_PULLDOWN;
    } else if (pull != ZTM_PWM_GPIO_PULL_NONE) {
        return false;
    }

    uint8_t pin = g_gpio_to_sx_pin[gpio_1based - 1u];
    if (sx1509_pin_configure(&device->output_mux, pin, flags) != SX1509_OK) {
        return false;
    }
    device->gpio_mode[gpio_1based - 1u] = ZTM_PWM_GPIO_INPUT;
    device->gpio_pull[gpio_1based - 1u] = pull;
    return true;
}

bool ztm_pwm_gpio_configure(uint8_t gpio_1based, ztm_pwm_gpio_mode_t mode)
{
    pwm_t *device = &ztm_pwm_1;
    if (!device->ready || !valid_gpio(gpio_1based)) {
        return false;
    }

    if (mode == ZTM_PWM_GPIO_INPUT) {
        return ztm_pwm_gpio_configure_input(gpio_1based, ZTM_PWM_GPIO_PULL_NONE);
    }

    uint8_t pin = g_gpio_to_sx_pin[gpio_1based - 1u];
    (void)sx1509_interrupt_configure(&device->output_mux, pin, false, SX1509_SENSE_NONE);
    int rc = sx1509_digital_write(&device->output_mux, pin, false);
    if (rc == SX1509_OK) {
        rc = sx1509_pin_configure(&device->output_mux, pin, SX1509_PIN_OUTPUT);
    }
    if (rc != SX1509_OK) {
        return false;
    }

    device->gpio_mode[gpio_1based - 1u] = ZTM_PWM_GPIO_OUTPUT;
    device->gpio_pull[gpio_1based - 1u] = ZTM_PWM_GPIO_PULL_NONE;
    device->gpio_irq_sense[gpio_1based - 1u] = SX1509_SENSE_NONE;
    device->gpio_level[gpio_1based - 1u] = false;
    return true;
}

bool ztm_pwm_gpio_configure_irq(uint8_t gpio_1based, sx1509_sense_t sense)
{
    pwm_t *device = &ztm_pwm_1;
    if (!device->ready || !valid_gpio(gpio_1based) ||
        device->gpio_mode[gpio_1based - 1u] != ZTM_PWM_GPIO_INPUT ||
        sense > SX1509_SENSE_BOTH) {
        return false;
    }
    uint8_t pin = g_gpio_to_sx_pin[gpio_1based - 1u];
    if (sx1509_interrupt_configure(&device->output_mux,
                                    pin,
                                    sense != SX1509_SENSE_NONE,
                                    sense) != SX1509_OK) {
        return false;
    }
    device->gpio_irq_sense[gpio_1based - 1u] = sense;
    return true;
}

bool ztm_pwm_gpio_toggle(uint8_t gpio_1based)
{
    pwm_t *device = &ztm_pwm_1;
    if (!device->ready || !valid_gpio(gpio_1based) ||
        device->gpio_mode[gpio_1based - 1u] != ZTM_PWM_GPIO_OUTPUT) {
        return false;
    }
    uint8_t pin = g_gpio_to_sx_pin[gpio_1based - 1u];
    if (sx1509_digital_toggle(&device->output_mux, pin) != SX1509_OK) {
        return false;
    }
    bool level = false;
    if (sx1509_digital_read(&device->output_mux, pin, &level) != SX1509_OK) {
        return false;
    }
    device->gpio_level[gpio_1based - 1u] = level;
    return true;
}

bool ztm_pwm_gpio_write(uint8_t gpio_1based, bool level)
{
    pwm_t *device = &ztm_pwm_1;
    if (!device->ready || !valid_gpio(gpio_1based) ||
        device->gpio_mode[gpio_1based - 1u] != ZTM_PWM_GPIO_OUTPUT) {
        return false;
    }

    uint8_t pin = g_gpio_to_sx_pin[gpio_1based - 1u];
    if (sx1509_digital_write(&device->output_mux, pin, level) != SX1509_OK) {
        return false;
    }
    device->gpio_level[gpio_1based - 1u] = level;
    return true;
}

bool ztm_pwm_gpio_read(uint8_t gpio_1based, bool *level)
{
    pwm_t *device = &ztm_pwm_1;
    if (!device->ready || !valid_gpio(gpio_1based) || !level) {
        return false;
    }

    uint8_t pin = g_gpio_to_sx_pin[gpio_1based - 1u];
    if (sx1509_digital_read(&device->output_mux, pin, level) != SX1509_OK) {
        return false;
    }
    device->gpio_level[gpio_1based - 1u] = *level;
    return true;
}

static void log_emc_status(uint8_t controller_index,
                           uint8_t fan_status,
                           uint8_t stall,
                           uint8_t spin,
                           uint8_t drive_fail)
{
    uint8_t fan_base = (uint8_t)(controller_index * 3u);
    if ((fan_status & EMC2303_FAN_STATUS_WATCH) != 0u) {
        log_printf("ERROR: PWM fan controller %u Watchdog\n", (unsigned)(controller_index + 1u));
    }

    /* A missing, disconnected or deliberately unmonitored TACH input can
     * hold the EMC2303 stall condition active indefinitely. Do not spam the
     * serial monitor from the periodic IRQ scan. Explicit CLI status commands
     * still report the stall bit and saturated 0x1FFF count. */
    (void)stall;

    for (uint8_t local = 1u; local <= 3u; local++) {
        uint8_t bit = EMC2303_FAN_BIT(local);
        uint8_t fan = (uint8_t)(fan_base + local);
        if ((spin & bit) != 0u) {
            log_printf("ERROR: PWM Fan %u Spin-up Failure\n", (unsigned)fan);
        }
        if ((drive_fail & bit) != 0u) {
            log_printf("ERROR: PWM Fan %u Drive Failure\n", (unsigned)fan);
        }
    }
}

int ztm_pwm_scan_irq_sources(bool force_scan)
{
    pwm_t *device = &ztm_pwm_1;
    if (!device->ready) {
        return -1;
    }

    bool irq_high = true;
    if (!pcal6416a_read_pin(&ioexp, (uint8_t)device->irq_pin, &irq_high)) {
        device->irq_scan_errors++;
        return -2;
    }
    if (irq_high) {
        device->irq_unresolved_reported = false;
        if (!force_scan) {
            return 0;
        }
    }

    device->irq_count++;
    bool source_found = false;
    int errors = 0;

    uint16_t sx_source = 0;
    int sx_rc = sx1509_interrupt_get_source(&device->output_mux, &sx_source);
    if (sx_rc != SX1509_OK) {
        errors++;
    } else if (sx_source != 0u) {
        source_found = true;
        log_printf("ERROR: PWM SX1509 IRQ source 0x%04X\n", (unsigned)sx_source);
        if (sx1509_interrupt_clear_source(&device->output_mux, sx_source) != SX1509_OK) {
            errors++;
        }
    }

    for (size_t pac = 0; pac < ZTM_PWM_PAC_COUNT; pac++) {
        if (!device->current_monitor_ready[pac]) {
            continue;
        }
        uint32_t status = 0;
        int rc = pac195x_read_alert_status(&device->current_monitor[pac], &status);
        if (rc != PAC195X_OK) {
            errors++;
        } else if (status != 0u) {
            source_found = true;
            log_printf("ERROR: PWM PAC%u alert 0x%06lX\n",
                       (unsigned)(pac + 1u),
                       (unsigned long)(status & 0xFFFFFFu));
        }
    }

    for (size_t emc = 0; emc < ZTM_PWM_EMC_COUNT; emc++) {
        uint8_t fan_status = 0, stall = 0, spin = 0, drive_fail = 0;
        int rc = emc2303_read_status(&device->fan_controller[emc],
                                     &fan_status,
                                     &stall,
                                     &spin,
                                     &drive_fail);
        if (rc != EMC2303_OK) {
            errors++;
        } else if (fan_status != 0u || stall != 0u || spin != 0u || drive_fail != 0u) {
            source_found = true;
            log_emc_status((uint8_t)emc, fan_status, stall, spin, drive_fail);
        }
    }

    if (!irq_high && !source_found && errors == 0 &&
        !device->irq_unresolved_reported) {
        log_printf("ERROR: PWM module IRQ asserted but no source was latched\n");
        device->irq_unresolved_reported = true;
    }

    if (errors != 0) {
        device->irq_scan_errors += (uint32_t)errors;
        return -errors;
    }
    return 0;
}

int ztm_pwm_poll(void)
{
    /* An intentional console power-off is not a periodic poll failure. */
    return ztm_pwm_1.ready ? ztm_pwm_scan_irq_sources(false) : 0;
}

int ztm_pwm_deinit(void)
{
    return ztm_pwm_power_off() ? 0 : -1;
}

bool ztm_pwm_test(uint16_t value)
{
    uint8_t duty = value > 100u ? 100u : (uint8_t)value;
    return ztm_pwm_channel_set_all(duty);
}
