#pragma once

#include <stdbool.h>
#include <stdint.h>

#define COOLING_TEC_COUNT 4u
#define COOLING_TEC_VSENSE_DIVIDER_RATIO_THOUSANDS 7500u

typedef struct {
    uint8_t dac_code;
    uint16_t adc_code;
    uint32_t output_uV;
    bool enabled;
    bool reading_valid;
} cooling_tec_status_t;

int cooling_init(void);
int cooling_update(void);
bool cooling_ready(void);
/* Confirmation asserts that the operator checked chiller temperature and flow.
 * Grant does not start a TEC. Revocation stops all and remains locked on error. */
bool cooling_tec_permission_granted(void);
bool cooling_tec_confirm_chiller(void);
bool cooling_tec_revoke_permission(void);

bool cooling_tec_set_raw(uint8_t channel_id,
                         uint8_t dac_code,
                         cooling_tec_status_t *status);
bool cooling_tec_set_all_raw(uint8_t dac_code,
                             cooling_tec_status_t status[COOLING_TEC_COUNT]);
bool cooling_tec_read(uint8_t channel_id, cooling_tec_status_t *status);
bool cooling_tec_read_all(cooling_tec_status_t status[COOLING_TEC_COUNT]);
bool cooling_tec_stop(uint8_t channel_id);
bool cooling_tec_stop_all(void);
