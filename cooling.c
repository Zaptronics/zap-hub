#include <stddef.h>
#include <stdint.h>

#include "pico/stdlib.h"
#include "ztmb/modules.h"
#include "ztmb/log.h"
#include "ztmb/adc.h"
#include "ztmb/ztm_psu_tec_adapter.h"
#include "cooling.h"

#define COOLING_TEC_SETTLE_US 100000u

static module_entry_t *g_peltiers[COOLING_TEC_COUNT];
static uint8_t g_dac_codes[COOLING_TEC_COUNT];
static bool g_enabled[COOLING_TEC_COUNT];
static bool g_ready;

/* PCB mapping: TEC1..4 VSENSE are connected to TLA2518 AIN5,4,3,2. */
static const uint8_t g_vsense_channels[COOLING_TEC_COUNT] = { 5u, 4u, 3u, 2u };

static bool cooling_index(uint8_t channel_id, size_t *index)
{
    if (!index || channel_id < 1u || channel_id > COOLING_TEC_COUNT) {
        return false;
    }
    *index = (size_t)(channel_id - 1u);
    return true;
}

static ztm_psu_tec_t *cooling_device(size_t index)
{
    if (index >= COOLING_TEC_COUNT || !g_peltiers[index]) {
        return NULL;
    }
    return (ztm_psu_tec_t *)g_peltiers[index]->mod_inst;
}

static void cooling_fill_status(size_t index,
                                uint16_t adc_code,
                                uint32_t output_uV,
                                bool reading_valid,
                                cooling_tec_status_t *status)
{
    if (!status || index >= COOLING_TEC_COUNT) {
        return;
    }

    status->dac_code = g_dac_codes[index];
    status->adc_code = adc_code;
    status->output_uV = output_uV;
    status->enabled = g_enabled[index];
    status->reading_valid = reading_valid;
}

int cooling_init(void)
{
    static const char *const names[COOLING_TEC_COUNT] = {
        "psu_tec_1",
        "psu_tec_2",
        "psu_tec_3",
        "psu_tec_4",
    };

    g_ready = false;
    for (size_t i = 0; i < COOLING_TEC_COUNT; i++) {
        g_peltiers[i] = modules_find_by_short_name(names[i]);
        g_dac_codes[i] = 0u;
        g_enabled[i] = false;
        if (!g_peltiers[i] || !g_peltiers[i]->mod_inst) {
            log_printf("Error: TEC module %u not found or not initialized\n",
                       (unsigned)(i + 1u));
            return -1;
        }
    }

    g_ready = true;
    if (!cooling_tec_stop_all()) {
        g_ready = false;
        log_printf("Error: failed to place all TEC modules in safe state\n");
        return -2;
    }

    log_printf("Cooling ready: 4 independent MCP4901 TEC channels on SPI0\n");
    return 0;
}

int cooling_update(void)
{
    return 0;
}

bool cooling_ready(void)
{
    return g_ready;
}

bool cooling_tec_read(uint8_t channel_id, cooling_tec_status_t *status)
{
    size_t index;
    if (!g_ready || !status || !cooling_index(channel_id, &index)) {
        return false;
    }

    uint16_t adc_code = 0u;
    uint32_t output_uV = 0u;
    bool ok = ztmb_adc_read_channel(g_vsense_channels[index],
                                    COOLING_TEC_VSENSE_DIVIDER_RATIO_THOUSANDS,
                                    &adc_code,
                                    &output_uV);
    cooling_fill_status(index, adc_code, output_uV, ok, status);
    return ok;
}

bool cooling_tec_read_all(cooling_tec_status_t status[COOLING_TEC_COUNT])
{
    if (!g_ready || !status) {
        return false;
    }

    bool all_ok = true;
    for (uint8_t channel_id = 1u; channel_id <= COOLING_TEC_COUNT; channel_id++) {
        all_ok = cooling_tec_read(channel_id, &status[channel_id - 1u]) && all_ok;
    }
    return all_ok;
}

bool cooling_tec_set_raw(uint8_t channel_id,
                         uint8_t dac_code,
                         cooling_tec_status_t *status)
{
    size_t index;
    if (!g_ready || !cooling_index(channel_id, &index)) {
        return false;
    }

    ztm_psu_tec_t *device = cooling_device(index);
    if (!device) {
        return false;
    }

    if (!ztm_psu_tec_start(device, dac_code, COOLING_TEC_SETTLE_US)) {
        /* Best-effort recovery through the authoritative enable-off path. */
        (void)cooling_tec_stop(channel_id);
        return false;
    }

    g_dac_codes[index] = dac_code;
    g_enabled[index] = true;

    if (!status) {
        return true;
    }

    if (!cooling_tec_read(channel_id, status)) {
        (void)cooling_tec_stop(channel_id);
        return false;
    }
    return true;
}

bool cooling_tec_set_all_raw(uint8_t dac_code,
                             cooling_tec_status_t status[COOLING_TEC_COUNT])
{
    if (!g_ready) {
        return false;
    }

    /* Disable all power stages before changing any DAC code. */
    for (size_t i = 0; i < COOLING_TEC_COUNT; i++) {
        ztm_psu_tec_t *device = cooling_device(i);
        if (!device || !ztm_psu_tec_set_enable(device, false)) {
            (void)cooling_tec_stop_all();
            return false;
        }
        g_enabled[i] = false;
    }

    for (size_t i = 0; i < COOLING_TEC_COUNT; i++) {
        ztm_psu_tec_t *device = cooling_device(i);
        if (!device || !ztm_psu_tec_set_raw(device, dac_code)) {
            (void)cooling_tec_stop_all();
            return false;
        }
        g_dac_codes[i] = dac_code;
    }

    for (size_t i = 0; i < COOLING_TEC_COUNT; i++) {
        ztm_psu_tec_t *device = cooling_device(i);
        if (!device || !ztm_psu_tec_set_enable(device, true)) {
            (void)cooling_tec_stop_all();
            return false;
        }
        g_enabled[i] = true;
    }

    busy_wait_us_32(COOLING_TEC_SETTLE_US);
    if (status && !cooling_tec_read_all(status)) {
        (void)cooling_tec_stop_all();
        return false;
    }
    return true;
}

bool cooling_tec_stop(uint8_t channel_id)
{
    size_t index;
    if (!g_ready || !cooling_index(channel_id, &index)) {
        return false;
    }

    ztm_psu_tec_t *device = cooling_device(index);
    if (!device) {
        return false;
    }

    const bool stopped = ztm_psu_tec_stop(device);
    ztm_psu_tec_status_t state;
    if (ztm_psu_tec_get_status(device, &state)) {
        /* EN can be off even when the secondary DAC shutdown fails. */
        if (state.enable_valid) {
            g_enabled[index] = state.enabled;
        }
        g_dac_codes[index] = state.dac_code;
    }
    return stopped;
}

bool cooling_tec_stop_all(void)
{
    if (!g_ready) {
        return false;
    }

    bool all_ok = true;
    for (uint8_t channel_id = 1u; channel_id <= COOLING_TEC_COUNT; channel_id++) {
        all_ok = cooling_tec_stop(channel_id) && all_ok;
    }
    return all_ok;
}
