#pragma once

/*
 * Hub compatibility facade for the pre-ZapEE singleton API.
 *
 * New code should include <zaptronics/modules/ztm_adc.h> and pass an explicit
 * ztm_adc_t instance. Existing Hub application/CLI code may continue using the
 * historical singleton calls while the module implementation itself lives only
 * in ZapEE.
 */
#include "ztmb/ztm_adc_adapter.h"

#define ztm_adc_init              ztmb_ztm_adc_init
#define ztm_adc_poll              ztmb_ztm_adc_poll
#define ztm_adc_deinit            ztmb_ztm_adc_deinit
#define ztm_adc_read_test(module) ztmb_ztm_adc_read_test((module))

#define ztm_adc_ready() \
    ztm_adc_ready(&ztm_adc_1)
#define ztm_adc_read_cold_junction(sample) \
    ztm_adc_read_cold_junction(&ztm_adc_1, (sample))
#define ztm_adc_read_rtd(id, sample) \
    ztm_adc_read_rtd(&ztm_adc_1, (id), (sample))
#define ztm_adc_read_all_rtds(cjc, samples) \
    ztm_adc_read_all_rtds(&ztm_adc_1, (cjc), (samples))
#define ztm_adc_set_thermocouple_type(id, type) \
    ztm_adc_set_thermocouple_type(&ztm_adc_1, (id), (type))
#define ztm_adc_get_thermocouple_type(id, type) \
    ztm_adc_get_thermocouple_type(&ztm_adc_1, (id), (type))
#define ztm_adc_read_thermocouple(id, sample) \
    ztm_adc_read_thermocouple(&ztm_adc_1, (id), (sample))
#define ztm_adc_read_all_thermocouples(samples) \
    ztm_adc_read_all_thermocouples(&ztm_adc_1, (samples))
#define ztm_adc_read_all(cjc, rtds, tcs) \
    ztm_adc_read_all(&ztm_adc_1, (cjc), (rtds), (tcs))
#define ztm_adc_read_error_status(status, errors) \
    ztm_adc_read_error_status(&ztm_adc_1, (status), (errors))
#define ztm_adc_verify_configuration() \
    ztm_adc_verify_configuration(&ztm_adc_1)
#define ztm_adc_read_register(address, width, value) \
    ztm_adc_read_register(&ztm_adc_1, (address), (width), (value))
#define ztm_adc_calibrate_tc_setup() \
    ztm_adc_calibrate_tc_setup(&ztm_adc_1)
#define ztm_adc_calibrate_rtd_setup() \
    ztm_adc_calibrate_rtd_setup(&ztm_adc_1)
#define ztm_adc_calibrate_all() \
    ztm_adc_calibrate_all(&ztm_adc_1)
#define ztm_adc_iout_off() \
    ztm_adc_iout_off(&ztm_adc_1)
#define ztm_adc_get_status(status) \
    ztm_adc_get_status(&ztm_adc_1, (status))
#define ztm_adc_get_last_measurement(diag) \
    ztm_adc_get_last_measurement(&ztm_adc_1, (diag))
