#pragma once

#include "hardware/i2c.h"
#include "drivers/emc2303.h"
#include "drivers/sx1509.h"

typedef struct {
    emc2303_t fan_controller_1;
    emc2303_t fan_controller_2;
    sx1509_t  output_mux;
} pwm_t;

extern pwm_t ztm_pwm_1;

int ztm_pwm_init(void *ctx, void *i2c, int8_t addr, int8_t en_pin);
bool ztm_pwm_test(uint16_t value);
