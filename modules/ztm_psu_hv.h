#pragma once

#include "hardware/i2c.h"
#include "drivers/ad5282.h"

typedef ad5282_t psu_hv_t;

/* The hub currently has one high-voltage PSU module. */
extern psu_hv_t ztm_psu_hv_1;

int ztm_psu_hv_init(void *ctx, void *i2c, int8_t addr, int8_t en_pin);
bool ztm_psu_hv_set_voltage(psu_hv_t *ctx, uint8_t voltage_code);
bool ztm_psu_hv_enable(psu_hv_t *ctx, bool state);
