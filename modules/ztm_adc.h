#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "drivers/ad7124.h"
#include "drivers/thermocouple.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ZTM_ADC_TC_COUNT                 3u
#define ZTM_ADC_RTD_COUNT                8u
#define ZTM_ADC_SENSOR_CHANNEL_COUNT    12u

/* AD7124 channel/setup allocation owned by this module. */
#define ZTM_ADC_SETUP_TC                 0u
#define ZTM_ADC_SETUP_RTD                1u
#define ZTM_ADC_CHANNEL_TC_FIRST         0u
#define ZTM_ADC_CHANNEL_CJC              3u
#define ZTM_ADC_CHANNEL_RTD_FIRST        4u

/* Analogue board topology. */
#define ZTM_ADC_AIN_CJC                  6u
#define ZTM_ADC_AIN_RTD_FIRST            7u
#define ZTM_ADC_AIN_RTD_RETURN          15u

/*
 * Existing board design values. Keep these in the module layer so a board
 * revision can change them without changing the generic AD7124 driver.
 */
#define ZTM_ADC_RTD_R0_OHMS           1000.0
#define ZTM_ADC_RREF_OHMS             2000.0
#define ZTM_ADC_INTERNAL_REF_VOLTS       2.5
#define ZTM_ADC_RTD_EXCITATION        AD7124_IOUT_500UA
#define ZTM_ADC_FILTER_FS              200u
#define ZTM_ADC_CONVERSION_TIMEOUT_US 2000000u
#define ZTM_ADC_IOUT_SETTLE_US          2000u

/* VBIAS is applied to the negative input of each floating thermocouple. */
#define ZTM_ADC_TC_VBIAS_MASK ((uint16_t)((1u << 1) | (1u << 3) | (1u << 5)))

typedef enum {
    ZTM_ADC_SENSOR_OK = 0,
    ZTM_ADC_SENSOR_DISCONNECTED,
    ZTM_ADC_SENSOR_CJC_UNAVAILABLE,
    ZTM_ADC_SENSOR_OUT_OF_RANGE,
} ztm_adc_sensor_status_t;

typedef struct {
    bool valid;                 /* ADC transaction completed. */
    ztm_adc_sensor_status_t status;
    uint8_t logical_index;      /* CJC = 0; external RTDs = 1..8. */
    uint8_t ad7124_channel;
    uint8_t ain_positive;
    uint32_t raw_code;
    double resistance_ohms;
    double temperature_c;
} ztm_adc_rtd_sample_t;

typedef struct {
    bool valid;                 /* ADC transaction completed. */
    ztm_adc_sensor_status_t status;
    uint8_t logical_index;      /* Thermocouple = 1..3. */
    uint8_t ad7124_channel;
    uint8_t ain_positive;
    uint8_t ain_negative;
    uint32_t raw_code;
    thermocouple_type_t type;
    double differential_mv;
    bool cold_junction_valid;
    double cold_junction_c;
    double compensated_mv;
    double temperature_c;
} ztm_adc_tc_sample_t;


typedef enum {
    ZTM_ADC_MEASURE_NONE = 0,
    ZTM_ADC_MEASURE_IOUT_ENABLE,
    ZTM_ADC_MEASURE_CHANNEL_ENABLE,
    ZTM_ADC_MEASURE_MODE_START,
    ZTM_ADC_MEASURE_MODE_VERIFY,
    ZTM_ADC_MEASURE_READY_TIMEOUT,
    ZTM_ADC_MEASURE_DATA_READ,
    ZTM_ADC_MEASURE_CHANNEL_MISMATCH,
    ZTM_ADC_MEASURE_STANDBY,
    ZTM_ADC_MEASURE_CHANNEL_DISABLE,
    ZTM_ADC_MEASURE_IOUT_DISABLE,
    ZTM_ADC_MEASURE_SENSOR_RANGE,
    ZTM_ADC_MEASURE_OK,
} ztm_adc_measure_stage_t;

typedef struct {
    ztm_adc_measure_stage_t stage;
    uint8_t expected_channel;
    uint8_t active_channel;
    uint8_t status_before;
    uint8_t status_ready;
    uint8_t status_after_cleanup;
    uint16_t channel_register;
    uint16_t adc_control_after_start;
    uint16_t adc_control_at_result;
    uint16_t adc_control_after_cleanup;
    uint32_t io_control_1;
    uint32_t filter_register;
    uint32_t elapsed_us;
    uint32_t ready_polls;
    uint32_t raw_code;
    uint32_t error_mask;
    double resistance_ohms;
} ztm_adc_measure_diag_t;

typedef struct {
    ad7124_dev_t adc;
    bool ready;
    volatile bool busy;
    uint8_t device_id;

    double reference_resistor_ohms;
    ad7124_iout_val_t rtd_excitation;
    thermocouple_type_t thermocouple_types[ZTM_ADC_TC_COUNT];

    uint32_t last_error_mask;
    uint32_t error_event_count;
    uint32_t communication_error_count;
    uint32_t configuration_error_count;
    bool configuration_verified;
    bool por_fault_active;
    ztm_adc_measure_diag_t last_measurement;
} ztm_adc_t;

extern ztm_adc_t ztm_adc_1;

int ztm_adc_init(void *ctx, void *spi, int8_t cs_pin, int8_t en_pin);
int ztm_adc_poll(void);
int ztm_adc_deinit(void);
bool ztm_adc_ready(void);

bool ztm_adc_read_cold_junction(ztm_adc_rtd_sample_t *sample);
bool ztm_adc_read_rtd(uint8_t rtd_1based, ztm_adc_rtd_sample_t *sample);
bool ztm_adc_read_all_rtds(ztm_adc_rtd_sample_t *cold_junction,
                            ztm_adc_rtd_sample_t samples[ZTM_ADC_RTD_COUNT]);

bool ztm_adc_set_thermocouple_type(uint8_t tc_1based, thermocouple_type_t type);
bool ztm_adc_get_thermocouple_type(uint8_t tc_1based, thermocouple_type_t *type);
bool ztm_adc_read_thermocouple(uint8_t tc_1based, ztm_adc_tc_sample_t *sample);
bool ztm_adc_read_all_thermocouples(ztm_adc_tc_sample_t samples[ZTM_ADC_TC_COUNT]);

bool ztm_adc_read_all(ztm_adc_rtd_sample_t *cold_junction,
                      ztm_adc_rtd_sample_t rtds[ZTM_ADC_RTD_COUNT],
                      ztm_adc_tc_sample_t thermocouples[ZTM_ADC_TC_COUNT]);

bool ztm_adc_read_error_status(uint8_t *status, uint32_t *error_mask);
bool ztm_adc_verify_configuration(void);
bool ztm_adc_read_register(uint8_t address, uint8_t width_bytes, uint32_t *value);
bool ztm_adc_calibrate_tc_setup(void);
bool ztm_adc_calibrate_rtd_setup(void);
bool ztm_adc_calibrate_all(void);
bool ztm_adc_iout_off(void);
bool ztm_adc_get_last_measurement(ztm_adc_measure_diag_t *diag);

/* Compatibility with the original one-channel debugging hook. */
bool ztm_adc_read_test(ztm_adc_t *module);

/* Public conversion helpers are useful to host tests and future sensing code. */
bool ztm_adc_pt1000_temperature(double resistance_ohms, double *temperature_c);
double ztm_adc_rtd_resistance_from_code(uint32_t code, double reference_resistor_ohms);

#ifdef __cplusplus
}
#endif
