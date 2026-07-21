#pragma once

#include <hardware/spi.h>
#include "ztmb/bus.h"
#include "drivers/ad7124.h"

typedef struct {
    ad7124_dev_t ctx1;
} ztm_adc_t;

/* The hub currently has one ZTM ADC module. Storage is defined once in ztm_adc.c. */
extern ztm_adc_t ztm_adc_1;

enum {
    AD7124_CH_TC0 = 0,
    AD7124_CH_TC1 = 1,
    AD7124_CH_TC2 = 2,
    AD7124_CH_CJ  = 3,
    AD7124_CH_RTD0 = 4,
    AD7124_CH_RTD1 = 5,
    AD7124_CH_RTD2 = 6,
    AD7124_CH_RTD3 = 7,
    AD7124_CH_RTD4 = 8,
    AD7124_CH_RTD5 = 9,
    AD7124_CH_RTD6 = 10,
    AD7124_CH_RTD7 = 11,
};

int ztm_adc_init(void *ztm_adc, void *spi, int8_t cs_pin, int8_t en_pin);
bool ztm_adc_read_test(ztm_adc_t *ztm_adc);
