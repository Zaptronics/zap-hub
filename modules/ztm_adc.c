#include "ztm_adc.h"
#include <stdio.h>
#include "ztmb/log.h"

ztm_adc_t ztm_adc_1;

int ztm_adc_init(void *ztm_adc, void *spi, int8_t cs_pin, int8_t en_pin)
{
    if (!ztm_adc || !spi) {
        return -1;
    }

    static const bus_spi_config_t spi_config = {
        .baud_hz = 1000000u,
        .data_bits = 8,
        .cpol = SPI_CPOL_1,
        .cpha = SPI_CPHA_1,
        .order = SPI_MSB_FIRST,
        .cs_setup_us = 0,
        .cs_hold_us = 0,
        .cs_high_us = 1,
    };

    ad7124_dev_t *ctx = &((ztm_adc_t *)ztm_adc)->ctx1;
    if (!bus_spi_init(&ctx->spi_bus, (spi_inst_t *)spi, cs_pin, en_pin, &spi_config)) {
        return -1;
    }

    ad7124_init(ctx);
    if (!ad7124_init_temp_frontend(ctx, true, 200, 200, 8)) {
        return -2;
    }

    if (!ad7124_wait_ready(ctx, 200000)) {
        return -3;
    }

    ad7124_sanity_report_t report;
    if (ad7124_sanity_check_rtd(ctx, 1, 8, &report) && !report.ok) {
        log_printf("AD7124 RTD sanity check reported a configuration problem\n");
    }

    return 0;
}

bool ztm_adc_read_test(ztm_adc_t *ztm_adc)
{
    if (!ztm_adc) {
        return false;
    }

    ad7124_rtd_sample_t sample;
    bool ok = ad7124_read_one_rtd_safe(&ztm_adc->ctx1,
                                       AD7124_CH_RTD0,
                                       1,
                                       AD7124_IOUT_250UA,
                                       7,
                                       2000.0,
                                       500000,
                                       &sample);
    if (!ok) {
        return false;
    }

    double resistance = ad7124_rtd_resistance_from_code_ratiometric(sample.code, 2000.0);
    log_printf("RTD Resistance: %.2f ohms\n", resistance);

    resistance = sample.resistance_ohms;
    double temperature_c = (resistance / 1000.0 - 1.0) / 0.00385;
    log_printf("CJ RTD: R=%.2f ohms, approx T=%.1f C\n", resistance, temperature_c);
    return true;
}
