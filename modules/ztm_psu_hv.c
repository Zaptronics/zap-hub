#include "pico/stdlib.h"
#include <inttypes.h>
#include <stdio.h>

#include "drivers/ad5282.h"
#include "ztmb/log.h"
#include "ztmb/adc.h"
#include "modules/ztm_psu_hv.h"

psu_hv_t ztm_psu_hv_1;

bool ztm_psu_hv_set_voltage(psu_hv_t *ctx, uint8_t voltage_code)
{
    if (!ctx) {
        return false;
    }

    bool ok_rdac2 = ad5282_write_rdac(ctx, AD5282_RDAC2, voltage_code, false, false);
    bool ok_rdac1 = ad5282_write_rdac(ctx, AD5282_RDAC1, 0, false, false);
    if (!ok_rdac1 || !ok_rdac2) {
        return false;
    }

    sleep_ms(100);
    uint32_t adc_value = ztmb_adc_read();
    log_printf("ztm_psu_hv_set_voltage(%u) voltage=%" PRIu32 "\n", voltage_code, adc_value);
    return true;
}

bool ztm_psu_hv_enable(psu_hv_t *ctx, bool state)
{
    return ctx && bus_i2c_en(&ctx->i2c_bus, state);
}

int ztm_psu_hv_init(void *ctx, void *i2c, int8_t addr, int8_t en_pin)
{
    if (!ctx || !i2c) {
        return -1;
    }

    static const bus_i2c_config_t i2c_config = {
        .baud_hz = 400000u,
        .timeout_us = 5000u,
    };

    psu_hv_t *dev = (psu_hv_t *)ctx;
    if (!bus_i2c_init(&dev->i2c_bus,
                      (i2c_inst_t *)i2c,
                      AD5282_ADDR(addr),
                      en_pin,
                      &i2c_config)) {
        return -1;
    }

    if (!ad5282_init(dev)) {
        return -2;
    }

    /* Program zero before disabling, so a later enable cannot restore a stale code. */
    if (!ztm_psu_hv_set_voltage(dev, 0)) {
        return -3;
    }
    if (!ztm_psu_hv_enable(dev, false)) {
        return -4;
    }

    return 0;
}

void test_psu_hv(void)
{
    for (;;) {
        for (uint16_t value = 45; value <= 255; value++) {
            (void)ztm_psu_hv_set_voltage(&ztm_psu_hv_1, (uint8_t)value);
            char line[128];
            (void)fgets(line, sizeof(line), stdin);
        }
    }
}
