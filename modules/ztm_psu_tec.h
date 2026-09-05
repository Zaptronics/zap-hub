#pragma once

#include "hardware/spi.h"
#include "ztmb/bus.h"
#include "drivers/mpc4901.h"

extern psu_tec_t ztm_psu_tec_1;
extern psu_tec_t ztm_psu_tec_2;
extern psu_tec_t ztm_psu_tec_3;
extern psu_tec_t ztm_psu_tec_4;

int ztm_psu_tec_init(void *ctx, void *spi, int8_t cs_pin, int8_t en_pin);
