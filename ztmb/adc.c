#include "pico/stdlib.h"
#include "ztmb/log.h"
#include "ztmb/io_exp.h"
#include "ztmb/adc.h"

static tla2518_dev_t adc;

int ztmb_adc_init(spi_inst_t *spi)
{
    static const bus_spi_config_t spi_config = {
        .baud_hz = 1000000u,
        .data_bits = 8,
        .cpol = SPI_CPOL_0,
        .cpha = SPI_CPHA_0,
        .order = SPI_MSB_FIRST,
        .cs_setup_us = 0,
        .cs_hold_us = 0,
        .cs_high_us = 1,
    };

    if (!spi || !bus_spi_init(&adc.spi_bus, spi, ZTMB_ADC_CS_1, EX_NONE, &spi_config)) {
        return TLA2518_ERR_ARG;
    }

    int rc = tla2518_init(&adc);
    if (rc != TLA2518_OK) {
        return rc;
    }

    /* Used only by the conversion helper; the ADC remains in manual mode. */
    tla2518_set_vref_mv(&adc, 3300);

    rc = tla2518_set_sequence_mode(&adc, TLA2518_SEQ_MANUAL);
    if (rc != TLA2518_OK) {
        return rc;
    }

    return tla2518_set_seq_start(&adc, false);
}

bool ztmb_adc_read_channel(uint8_t channel,
                           uint32_t ratio_thousands,
                           uint16_t *code,
                           uint32_t *scaled_uV)
{
    if (channel > 7u || ratio_thousands == 0u || !scaled_uV) {
        return false;
    }

    uint16_t sample_code = 0;
    int rc = tla2518_read_channel_manual(&adc, channel, &sample_code, 50u);
    if (rc != TLA2518_OK) {
        return false;
    }

    if (code) {
        *code = sample_code;
    }
    *scaled_uV = tla2518_code_to_uV(&adc, sample_code, (int)ratio_thousands);
    return true;
}

uint32_t ztmb_adc_read(void)
{
    uint32_t scaled_uV = UINT32_MAX;
    if (!ztmb_adc_read_channel(1u, 7500u, NULL, &scaled_uV)) {
        return UINT32_MAX;
    }
    return scaled_uV;
}
