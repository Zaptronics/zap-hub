#pragma once

#include "zaptronics/modules/ztm_psu_hv.h"
#include "zaptronics/boards/ztpico2/pins.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ZTMB-owned physical instance of the portable ZapEE HV PSU module. */
extern ztm_psu_hv_t ztm_psu_hv_1;

int ztmb_ztm_psu_hv_init(void *ctx, void *i2c, int8_t addr, int8_t en_pin);
int ztmb_ztm_psu_hv_poll(void);
int ztmb_ztm_psu_hv_deinit(void);

#ifdef __cplusplus
}
#endif
