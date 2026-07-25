#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "hardware/i2c.h"
#include "drivers/emc2303.h"
#include "drivers/pac195x.h"
#include "drivers/sx1509.h"

#define ZTM_PWM_CHANNEL_COUNT      12u
#define ZTM_PWM_FAN_COUNT           6u
#define ZTM_PWM_GPIO_COUNT          4u
#define ZTM_PWM_PAC_COUNT           3u
#define ZTM_PWM_EMC_COUNT           2u

#define ZTM_PWM_SX1509_ADDR       SX1509_I2C_ADDR_3
#define ZTM_PWM_PAC1_ADDR         0x1Fu  /* Prototype: ADDRSEL resistor omitted; observed high-address latch. */
#define ZTM_PWM_PAC2_ADDR         0x11u
#define ZTM_PWM_PAC3_ADDR         0x12u
#define ZTM_PWM_EMC1_ADDR         0x2Eu
#define ZTM_PWM_EMC2_ADDR         0x2Fu
#define ZTM_PWM_SHUNT_UOHM        50000u

/* Logical Fan1..6 bit mask. The current board defaults all PWM pins to push-pull. */
#ifndef ZTM_PWM_FAN_DEFAULT_PUSH_PULL_MASK
#define ZTM_PWM_FAN_DEFAULT_PUSH_PULL_MASK 0x3Fu
#endif

typedef enum {
    ZTM_PWM_PAC_TOPOLOGY_NONE = 0,
    ZTM_PWM_PAC_TOPOLOGY_NATIVE_4CH,
    ZTM_PWM_PAC_TOPOLOGY_PAC1952_2_SALVAGE,
    ZTM_PWM_PAC_TOPOLOGY_UNSUPPORTED,
} ztm_pwm_pac_topology_t;

typedef enum {
    ZTM_PWM_CURRENT_SOURCE_NONE = 0,
    ZTM_PWM_CURRENT_SOURCE_VSENSE,
    ZTM_PWM_CURRENT_SOURCE_VBUS,
} ztm_pwm_current_source_t;

typedef struct {
    uint8_t pac_1based;
    uint8_t input_1based;
    ztm_pwm_current_source_t source;
    uint16_t raw;
    int32_t shunt_uV;
    int32_t current_uA;
    bool coarse_resolution;
} ztm_pwm_current_reading_t;


typedef enum {
    ZTM_PWM_INIT_NONE = 0,
    ZTM_PWM_INIT_CONNECTOR_PINS,
    ZTM_PWM_INIT_POWER_OFF,
    ZTM_PWM_INIT_POWER_ON,
    ZTM_PWM_INIT_SX1509,
    ZTM_PWM_INIT_LOW_SIDE_OUTPUTS,
    ZTM_PWM_INIT_USER_GPIOS,
    ZTM_PWM_INIT_PAC1,
    ZTM_PWM_INIT_PAC2,
    ZTM_PWM_INIT_PAC3,
    ZTM_PWM_INIT_EMC1,
    ZTM_PWM_INIT_EMC2,
    ZTM_PWM_INIT_FAN_SAFE_STATE,
    ZTM_PWM_INIT_FAN_OUTPUT_CONFIG,
    ZTM_PWM_INIT_FAN_INTERRUPTS,
    ZTM_PWM_INIT_CLEAR_STATUS,
    ZTM_PWM_INIT_READY,
} ztm_pwm_init_stage_t;

typedef struct {
    bool power_control_ok;
    bool power_was_enabled;
    bool power_temporarily_enabled;
    bool power_restored_off;
    bool enable_read_ok;
    bool enable_high;
    bool irq_read_ok;
    bool irq_asserted;

    bool sx_present;
    uint8_t sx_dir_a;

    bool pac_present[ZTM_PWM_PAC_COUNT];
    uint8_t pac_product_id[ZTM_PWM_PAC_COUNT];
    uint8_t pac_manufacturer_id[ZTM_PWM_PAC_COUNT];
    uint8_t pac_revision_id[ZTM_PWM_PAC_COUNT];
    uint8_t pac_channels[ZTM_PWM_PAC_COUNT];

    bool emc_present[ZTM_PWM_EMC_COUNT];
    uint8_t emc_product_id[ZTM_PWM_EMC_COUNT];
    uint8_t emc_manufacturer_id[ZTM_PWM_EMC_COUNT];
    uint8_t emc_revision_id[ZTM_PWM_EMC_COUNT];

    uint8_t devices_found;
} ztm_pwm_probe_result_t;

typedef struct {
    bool power_control_ok;
    bool power_was_enabled;
    bool power_temporarily_enabled;
    bool power_restored_off;
    bool address_present[128];
    uint8_t devices_found;
} ztm_pwm_i2c_scan_result_t;

typedef enum {
    ZTM_PWM_FAN_OUTPUT_OPEN_DRAIN = 0,
    ZTM_PWM_FAN_OUTPUT_PUSH_PULL,
} ztm_pwm_fan_output_mode_t;

typedef enum {
    ZTM_PWM_FAN_TACH_OK = 0,
    ZTM_PWM_FAN_TACH_STALL,
    ZTM_PWM_FAN_TACH_READ_ERROR,
} ztm_pwm_fan_tach_status_t;

typedef enum {
    ZTM_PWM_GPIO_INPUT = 0,
    ZTM_PWM_GPIO_OUTPUT = 1,
} ztm_pwm_gpio_mode_t;

typedef enum {
    ZTM_PWM_GPIO_PULL_NONE = 0,
    ZTM_PWM_GPIO_PULL_UP,
    ZTM_PWM_GPIO_PULL_DOWN,
} ztm_pwm_gpio_pull_t;

typedef struct {
    emc2303_t fan_controller[ZTM_PWM_EMC_COUNT];
    pac195x_t current_monitor[ZTM_PWM_PAC_COUNT];
    bool current_monitor_ready[ZTM_PWM_PAC_COUNT];
    int current_monitor_rc[ZTM_PWM_PAC_COUNT];
    ztm_pwm_pac_topology_t current_monitor_topology[ZTM_PWM_PAC_COUNT];
    sx1509_t output_mux;

    i2c_inst_t *i2c;
    int8_t enable_pin;
    int8_t irq_pin;
    bool ready;
    ztm_pwm_init_stage_t last_init_stage;
    int last_init_rc;

    uint8_t channel_duty_pct[ZTM_PWM_CHANNEL_COUNT];
    uint8_t fan_duty_pct[ZTM_PWM_FAN_COUNT];
    bool fan_closed_loop[ZTM_PWM_FAN_COUNT];
    uint32_t fan_target_rpm[ZTM_PWM_FAN_COUNT];
    ztm_pwm_gpio_mode_t gpio_mode[ZTM_PWM_GPIO_COUNT];
    ztm_pwm_gpio_pull_t gpio_pull[ZTM_PWM_GPIO_COUNT];
    sx1509_sense_t gpio_irq_sense[ZTM_PWM_GPIO_COUNT];
    bool gpio_level[ZTM_PWM_GPIO_COUNT];

    uint32_t irq_count;
    uint32_t irq_scan_errors;
    bool irq_unresolved_reported;
} pwm_t;

extern pwm_t ztm_pwm_1;

int ztm_pwm_init(void *ctx, void *i2c, int8_t addr, int8_t en_pin);
int ztm_pwm_poll(void);
int ztm_pwm_deinit(void);
bool ztm_pwm_ready(void);
bool ztm_pwm_power_off(void);
bool ztm_pwm_reinitialize(void);
bool ztm_pwm_read_enable(bool *enabled);
bool ztm_pwm_read_irq_asserted(bool *asserted);
const char *ztm_pwm_init_stage_name(ztm_pwm_init_stage_t stage);
int ztm_pwm_probe_devices(ztm_pwm_probe_result_t *result, bool ensure_power_on);
int ztm_pwm_i2c_scan(ztm_pwm_i2c_scan_result_t *result, bool ensure_power_on);
const char *ztm_pwm_pac_topology_name(ztm_pwm_pac_topology_t topology);
const char *ztm_pwm_current_source_name(ztm_pwm_current_source_t source);

/* Twelve independent low-side PWM channels, expressed as 0..100 percent. */
bool ztm_pwm_channel_set(uint8_t channel_1based, uint8_t duty_pct);
bool ztm_pwm_channel_set_all(uint8_t duty_pct);
bool ztm_pwm_channel_stop(uint8_t channel_1based);
bool ztm_pwm_stop_all(void);
uint8_t ztm_pwm_channel_get_duty(uint8_t channel_1based);
uint8_t ztm_pwm_channel_get_sx_pin(uint8_t channel_1based);

/* Current measurement through the three PAC195x devices and 50 mOhm shunts. */
bool ztm_pwm_read_channel(uint8_t channel_1based, bool averaged, ztm_pwm_current_reading_t *reading);
bool ztm_pwm_read_current_uA(uint8_t channel_1based, bool averaged, int32_t *current_uA);
bool ztm_pwm_read_all_currents_uA(bool averaged, int32_t current_uA[ZTM_PWM_CHANNEL_COUNT]);
bool ztm_pwm_channel_get_current_route(uint8_t channel_1based,
                                       uint8_t *pac_1based,
                                       ztm_pwm_current_source_t *source,
                                       uint8_t *input_1based);

/* Independent fan PWM/TACH control for channels 1..6. */
bool ztm_pwm_fan_set(uint8_t fan_1based, uint8_t duty_pct);
bool ztm_pwm_fan_set_raw(uint8_t fan_1based, uint8_t raw);
bool ztm_pwm_fan_set_target_rpm(uint8_t fan_1based, uint32_t rpm);
bool ztm_pwm_fan_set_open_loop(uint8_t fan_1based, uint8_t duty_pct);
bool ztm_pwm_fan_set_all(uint8_t duty_pct);
bool ztm_pwm_fan_set_output_mode(uint8_t fan_1based, ztm_pwm_fan_output_mode_t mode);
bool ztm_pwm_fan_set_output_mode_all(ztm_pwm_fan_output_mode_t mode);
bool ztm_pwm_fan_get_output_mode(uint8_t fan_1based, ztm_pwm_fan_output_mode_t *mode);
const char *ztm_pwm_fan_output_mode_name(ztm_pwm_fan_output_mode_t mode);
bool ztm_pwm_fan_stop(uint8_t fan_1based);
bool ztm_pwm_fan_stop_all(void);
bool ztm_pwm_fan_get_rpm(uint8_t fan_1based, uint32_t *rpm);
bool ztm_pwm_fan_get_tach(uint8_t fan_1based, uint16_t *tach_count, uint32_t *rpm);
ztm_pwm_fan_tach_status_t ztm_pwm_fan_get_tach_status(uint8_t fan_1based,
                                                       uint16_t *tach_count,
                                                       uint32_t *rpm);
bool ztm_pwm_fan_get_route(uint8_t fan_1based, uint8_t *emc_1based, uint8_t *local_fan_1based);
uint8_t ztm_pwm_fan_get_duty(uint8_t fan_1based);

/* Four user GPIOs exposed by the SX1509. */
bool ztm_pwm_gpio_configure(uint8_t gpio_1based, ztm_pwm_gpio_mode_t mode);
bool ztm_pwm_gpio_configure_input(uint8_t gpio_1based, ztm_pwm_gpio_pull_t pull);
bool ztm_pwm_gpio_configure_irq(uint8_t gpio_1based, sx1509_sense_t sense);
bool ztm_pwm_gpio_toggle(uint8_t gpio_1based);
bool ztm_pwm_gpio_write(uint8_t gpio_1based, bool level);
bool ztm_pwm_gpio_read(uint8_t gpio_1based, bool *level);
uint8_t ztm_pwm_gpio_get_sx_pin(uint8_t gpio_1based);

/* Scan every device sharing the module's active-low interrupt line. */
int ztm_pwm_scan_irq_sources(bool force_scan);

/* Legacy bench helper retained as a set-all percentage alias. */
bool ztm_pwm_test(uint16_t value);
