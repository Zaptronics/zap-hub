#include "pico/stdlib.h"
#include "ztm_psu_tec.h"

psu_tec_t ztm_psu_tec_1;
psu_tec_t ztm_psu_tec_2;
psu_tec_t ztm_psu_tec_3;
psu_tec_t ztm_psu_tec_4;

int ztm_psu_tec_init(void *ctx, void *spi, int8_t cs_pin, int8_t en_pin)
{
    if (!ctx || !spi) {
        return -1;
    }

    static const bus_spi_config_t spi_config = {
        .baud_hz = MCP4901_SPI_BAUD,
        .data_bits = 8,
        .cpol = SPI_CPOL_0,
        .cpha = SPI_CPHA_0,
        .order = SPI_MSB_FIRST,
        .cs_setup_us = 0,
        .cs_hold_us = 0,
        .cs_high_us = 1,
    };

    psu_tec_t *dev = (psu_tec_t *)ctx;
    if (!bus_spi_init(&dev->spi_bus, (spi_inst_t *)spi, cs_pin, en_pin, &spi_config)) {
        return -1;
    }
    return mcp4901_init(dev) ? 0 : -2;
}
