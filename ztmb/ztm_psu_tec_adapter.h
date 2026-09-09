#pragma once

#include "zaptronics/modules/ztm_psu_tec.h"

#ifdef __cplusplus
extern "C" {
#endif

extern ztm_psu_tec_t ztm_psu_tec_1;
extern ztm_psu_tec_t ztm_psu_tec_2;
extern ztm_psu_tec_t ztm_psu_tec_3;
extern ztm_psu_tec_t ztm_psu_tec_4;

int ztmb_ztm_psu_tec_init(void *ctx, void *spi, int8_t cs_pin, int8_t en_pin);
int ztmb_ztm_psu_tec_deinit_1(void);
int ztmb_ztm_psu_tec_deinit_2(void);
int ztmb_ztm_psu_tec_deinit_3(void);
int ztmb_ztm_psu_tec_deinit_4(void);

#ifdef __cplusplus
}
#endif
