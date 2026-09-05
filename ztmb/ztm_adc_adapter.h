#pragma once

#include "zaptronics/modules/ztm_adc.h"
#include "ztmb/bus.h"

#ifdef __cplusplus
extern "C" {
#endif

extern ztm_adc_t ztm_adc_1;

int ztmb_ztm_adc_init(void *ctx, void *spi, int8_t cs_pin, int8_t en_pin);
int ztmb_ztm_adc_poll(void);
int ztmb_ztm_adc_deinit(void);
bool ztmb_ztm_adc_read_test(ztm_adc_t *module);
const bus_spi_t *ztmb_ztm_adc_bus(void);

#ifdef __cplusplus
}
#endif
