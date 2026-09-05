#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum pump_state_t {
    PUMP_OFF = 0,
    PUMP_WICK_PRIME,
    PUMP_WICK_DOSE,
    PUMP_WICK_DRAIN,
    PUMP_FILTER_FAST,
    PUMP_FILTER_NORMAL,
    PUMP_SUMP_ON,
} pump_state_t;

typedef struct {
    pump_state_t filter_state;
    float filter_speed;
    pump_state_t wick_state;
    float wick_speed;
    uint32_t wick_remaining_ms;
    pump_state_t sump_state;
    float sump_speed;
} pumping_status_t;

int pumping_init(void);
int pumping_update(void);
bool pumping_ready(void);

bool pumping_all_stop(void);
bool pumping_filter_start_normal(void);
bool pumping_filter_start_fast(void);
bool pumping_filter_stop(void);
bool pumping_wick_prime(void);
bool pumping_wick_dose(void);
bool pumping_wick_drain(void);
bool pumping_wick_stop(void);
bool pumping_sump_run(void);
bool pumping_sump_stop(void);

pump_state_t pumping_set_state(pump_state_t new_state);
void pumping_get_status(pumping_status_t *status);
