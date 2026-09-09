#include "ztmb/ztm_psu_tec_adapter.h"

#include <stddef.h>

#include "hardware/spi.h"
#include "ztmb/bus.h"
#include "ztmb/io_exp.h"
#include "ztmb/zap_adapter.h"

ztm_psu_tec_t ztm_psu_tec_1;
ztm_psu_tec_t ztm_psu_tec_2;
ztm_psu_tec_t ztm_psu_tec_3;
ztm_psu_tec_t ztm_psu_tec_4;

typedef struct {
    ztm_psu_tec_t *module;
    ztmb_zap_spi_t spi;
    ztmb_zap_lock_t lock;
    int8_t enable_pin;
} tec_binding_t;

static tec_binding_t g_tec[] = {
    { .module = &ztm_psu_tec_1, .enable_pin = EX_NONE },
    { .module = &ztm_psu_tec_2, .enable_pin = EX_NONE },
    { .module = &ztm_psu_tec_3, .enable_pin = EX_NONE },
    { .module = &ztm_psu_tec_4, .enable_pin = EX_NONE },
};

static tec_binding_t *binding_for(ztm_psu_tec_t *module)
{
    for (size_t i = 0u; i < sizeof(g_tec) / sizeof(g_tec[0]); ++i) {
        if (g_tec[i].module == module) {
            return &g_tec[i];
        }
    }
    return NULL;
}

static int hub_enable_set(void *context, bool enabled)
{
    tec_binding_t *binding = (tec_binding_t *)context;
    return binding != NULL && bus_spi_en(&binding->spi.bus, enabled) ? 0 : -1;
}

static int hub_enable_get(void *context, bool *enabled)
{
    tec_binding_t *binding = (tec_binding_t *)context;
    return binding != NULL && enabled != NULL && binding->enable_pin >= 0 &&
                   pcal6416a_read_pin(&ioexp, (uint8_t)binding->enable_pin, enabled)
               ? 0
               : -1;
}

int ztmb_ztm_psu_tec_init(void *ctx, void *spi, int8_t cs_pin, int8_t en_pin)
{
    ztm_psu_tec_t *module = (ztm_psu_tec_t *)ctx;
    tec_binding_t *binding = binding_for(module);
    if (binding == NULL || spi == NULL || cs_pin < 0 || en_pin < 0) {
        return -1;
    }

    const bus_spi_config_t spi_config = {
        .baud_hz = MCP4901_SPI_BAUD_HZ,
        .data_bits = 8u,
        .cpol = SPI_CPOL_0,
        .cpha = SPI_CPHA_0,
        .order = SPI_MSB_FIRST,
        .cs_setup_us = 0u,
        .cs_hold_us = 0u,
        .cs_high_us = 1u,
    };

    binding->enable_pin = en_pin;
    if (!pcal6416a_write_pin(&ioexp, (uint8_t)en_pin, false) ||
        !pcal6416a_set_pin_direction(&ioexp, (uint8_t)en_pin, false)) {
        return -2;
    }

    if (!ztmb_zap_spi_init(&binding->spi,
                           (spi_inst_t *)spi,
                           (uint)cs_pin,
                           en_pin,
                           &spi_config,
                           ZAP_SPI_DEFAULT_TIMEOUT_US,
                           0u)) {
        return -3;
    }
    ztmb_zap_lock_init(&binding->lock, 500000u);

    const ztm_psu_tec_config_t config = {
        .spi = ztmb_zap_spi_device(&binding->spi),
        .time = ztmb_zap_time(),
        .lock = ztmb_zap_lock(&binding->lock),
        .control_context = binding,
        .enable_set = hub_enable_set,
        .enable_get = hub_enable_get,
        .ref_buffered = true,
        .gain_1x = true,
    };
    return ztm_psu_tec_init(module, &config);
}

static int deinit_instance(ztm_psu_tec_t *module)
{
    return ztm_psu_tec_deinit(module);
}

int ztmb_ztm_psu_tec_deinit_1(void) { return deinit_instance(&ztm_psu_tec_1); }
int ztmb_ztm_psu_tec_deinit_2(void) { return deinit_instance(&ztm_psu_tec_2); }
int ztmb_ztm_psu_tec_deinit_3(void) { return deinit_instance(&ztm_psu_tec_3); }
int ztmb_ztm_psu_tec_deinit_4(void) { return deinit_instance(&ztm_psu_tec_4); }
