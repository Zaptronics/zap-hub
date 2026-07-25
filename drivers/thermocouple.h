#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Letter-designated ITS-90 thermocouple types from NIST SRD 60. */
typedef enum {
    THERMOCOUPLE_TYPE_B = 0,
    THERMOCOUPLE_TYPE_E,
    THERMOCOUPLE_TYPE_J,
    THERMOCOUPLE_TYPE_K,
    THERMOCOUPLE_TYPE_N,
    THERMOCOUPLE_TYPE_R,
    THERMOCOUPLE_TYPE_S,
    THERMOCOUPLE_TYPE_T,
    THERMOCOUPLE_TYPE_COUNT,
} thermocouple_type_t;

const char *thermocouple_type_name(thermocouple_type_t type);
bool thermocouple_type_parse(const char *text, thermocouple_type_t *type);

/*
 * Convert temperature to thermoelectric voltage relative to a 0 C reference.
 * Voltage is returned in millivolts.
 */
bool thermocouple_emf_mv(thermocouple_type_t type,
                          double temperature_c,
                          double *emf_mv);

/*
 * Apply cold-junction compensation and convert measured differential mV to C.
 * compensated_emf_mv may be NULL; when supplied it receives the equivalent
 * thermoelectric voltage relative to a 0 C reference junction.
 */
bool thermocouple_compensated_temperature_c(thermocouple_type_t type,
                                              double measured_mv,
                                              double cold_junction_c,
                                              double *temperature_c,
                                              double *compensated_emf_mv);

#ifdef __cplusplus
}
#endif
