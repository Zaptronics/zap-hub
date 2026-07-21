#include "ztm_pwm.h"
#include "ztmb/io_exp.h"

pwm_t ztm_pwm_1;

int ztm_pwm_init(void *ctx, void *i2c, int8_t addr, int8_t en_pin)
{
    if (!ctx || !i2c) {
        return -1;
    }
    (void)addr;
    (void)en_pin;

    pwm_t *device = (pwm_t *)ctx;
    emc2303_t *fan_controller_1 = &device->fan_controller_1;
    emc2303_t *fan_controller_2 = &device->fan_controller_2;
    sx1509_t *output_mux = &device->output_mux;

    if (sx1509_init(output_mux, (i2c_inst_t *)i2c, SX1509_I2C_ADDR_3) != SX1509_OK) {
        return -2;
    }
    if (emc2303_init(fan_controller_1, (i2c_inst_t *)i2c, 0x2E) != EMC2303_OK) {
        return -3;
    }
    if (emc2303_init(fan_controller_2, (i2c_inst_t *)i2c, 0x2F) != EMC2303_OK) {
        return -4;
    }

    for (uint8_t pin = 0; pin < 16; pin++) {
        if (sx1509_pin_configure(output_mux, pin, SX1509_PIN_OUTPUT) != SX1509_OK ||
            sx1509_digital_write(output_mux, pin, false) != SX1509_OK) {
            return -5;
        }
    }

    const uint8_t static_high_pins[] = {15, 14, 13, 12, 11, 10};
    for (size_t i = 0; i < sizeof(static_high_pins) / sizeof(static_high_pins[0]); i++) {
        if (sx1509_digital_write(output_mux, static_high_pins[i], true) != SX1509_OK) {
            return -6;
        }
    }

    if (emc2303_fan_set_pwm_percent_x100(fan_controller_1, 2, 1000) != EMC2303_OK ||
        emc2303_fan_set_pwm_percent_x100(fan_controller_2, 3, 1000) != EMC2303_OK ||
        emc2303_fan_set_pwm_percent_x100(fan_controller_2, 2, 1000) != EMC2303_OK) {
        return -7;
    }

    if (!bus_i2c_en(&fan_controller_1->i2c_bus, true)) {
        return -8;
    }

    const uint8_t pwm_pins[] = {9, 7, 5, 4, 2, 0};
    for (size_t i = 0; i < sizeof(pwm_pins) / sizeof(pwm_pins[0]); i++) {
        if (sx1509_pwm_pin_start(output_mux, pwm_pins[i], 25) != SX1509_OK ||
            sx1509_pwm_pin_set_duty(output_mux, pwm_pins[i], 5) != SX1509_OK) {
            return -9;
        }
    }

    return 0;
}

bool ztm_pwm_test(uint16_t value)
{
    emc2303_t *fan_controller_1 = &ztm_pwm_1.fan_controller_1;
    emc2303_t *fan_controller_2 = &ztm_pwm_1.fan_controller_2;
    sx1509_t *output_mux = &ztm_pwm_1.output_mux;

    if (!fan_controller_1->i2c_bus.i2c || !fan_controller_2->i2c_bus.i2c || !output_mux->present) {
        return false;
    }

    uint8_t raw = value > UINT8_MAX ? UINT8_MAX : (uint8_t)value;
    bool ok = true;

    for (uint8_t fan = 1; fan <= 3; fan++) {
        ok = emc2303_fan_set_pwm_raw(fan_controller_1, fan, raw) == EMC2303_OK && ok;
        ok = emc2303_fan_set_pwm_raw(fan_controller_2, fan, raw) == EMC2303_OK && ok;
    }

    ok = sx1509_digital_write(output_mux, 15, false) == SX1509_OK && ok;
    ok = sx1509_digital_write(output_mux, 14, false) == SX1509_OK && ok;
    ok = sx1509_digital_write(output_mux, 13, false) == SX1509_OK && ok;
    return ok;
}
