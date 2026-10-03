#include "pico/stdlib.h"

#include "ztmb/ztmb.h"
#include "ztmb/modules.h"
#include "ztmb/ztm_stepper_adapter.h"
#include "pumping.h"
#include "cirrus1_config.h"

/* Physical pump mapping for this chamber. */
#define PUMP_FILTER_STEPPER_ID CIRRUS1_FILTER_MOTOR
#define PUMP_SUMP_STEPPER_ID   CIRRUS1_RETURN_MOTOR
#define PUMP_WICK_STEPPER_ID   CIRRUS1_WICK_MOTOR

#define PUMP_FILTER_SPEED_FAST    100.0f
#define PUMP_FILTER_SPEED_NORMAL   CIRRUS1_FILTER_SPEED
#define PUMP_FILTER_DIRECTION      true

#define PUMP_WICK_SPEED             CIRRUS1_WICK_WET_SPEED
#define PUMP_WICK_PRIME_TIME_MS   5000u
#define PUMP_WICK_DOSE_TIME_MS    2000u
#define PUMP_WICK_DRAIN_TIME_MS   5000u
#define PUMP_WICK_DIRECTION_PRIME  true
#define PUMP_WICK_DIRECTION_DOSE   true
#define PUMP_WICK_DIRECTION_DRAIN  false

#define PUMP_SUMP_SPEED             CIRRUS1_RETURN_SPEED
#define PUMP_SUMP_DIRECTION         true

static module_entry_t *g_stepper_module;
static bool g_pumping_ready;

static pump_state_t filter_state = PUMP_OFF;
static float filter_speed;

static pump_state_t wick_state = PUMP_OFF;
static uint32_t wick_deadline_ms;
static bool wick_timer_active;
static float wick_speed;

static pump_state_t sump_state = PUMP_OFF;
static float sump_speed;

static bool pumping_dependencies_ready(void)
{
    return g_pumping_ready && g_stepper_module && g_stepper_module->mod_inst && ztm_stepper_ready(&ztm_stepper_1);
}

bool pumping_ready(void)
{
    return pumping_dependencies_ready();
}

static bool pumping_filter_start(float speed, pump_state_t state)
{
    if (!pumping_dependencies_ready() ||
        !ztm_stepper_run(&ztm_stepper_1, PUMP_FILTER_STEPPER_ID, PUMP_FILTER_DIRECTION, speed)) {
        return false;
    }

    filter_speed = speed;
    filter_state = state;
    return true;
}

bool pumping_filter_start_normal(void)
{
    return pumping_filter_start(PUMP_FILTER_SPEED_NORMAL, PUMP_FILTER_NORMAL);
}

bool pumping_filter_start_fast(void)
{
    return pumping_filter_start(PUMP_FILTER_SPEED_FAST, PUMP_FILTER_FAST);
}

bool pumping_filter_stop(void)
{
    if (!pumping_dependencies_ready() ||
        !ztm_stepper_soft_stop(&ztm_stepper_1, PUMP_FILTER_STEPPER_ID)) {
        return false;
    }

    filter_state = PUMP_OFF;
    filter_speed = 0.0f;
    return true;
}

static bool pumping_wick_start(bool direction, float speed, uint32_t duration_ms, pump_state_t state)
{
    if (!pumping_dependencies_ready() ||
        !ztm_stepper_run(&ztm_stepper_1, PUMP_WICK_STEPPER_ID, direction, speed)) {
        return false;
    }

    wick_deadline_ms = to_ms_since_boot(get_absolute_time()) + duration_ms;
    wick_timer_active = duration_ms != 0u;
    wick_speed = speed;
    wick_state = state;
    return true;
}

bool pumping_wick_prime(void)
{
    return pumping_wick_start(PUMP_WICK_DIRECTION_PRIME, CIRRUS1_WICK_PRIME_SPEED,
                              PUMP_WICK_PRIME_TIME_MS,
                              PUMP_WICK_PRIME);
}

bool pumping_wick_dose(void)
{
    return pumping_wick_start(PUMP_WICK_DIRECTION_DOSE, PUMP_WICK_SPEED,
                              PUMP_WICK_DOSE_TIME_MS,
                              PUMP_WICK_DOSE);
}

bool pumping_wick_drain(void)
{
    return pumping_wick_start(PUMP_WICK_DIRECTION_DRAIN, CIRRUS1_WICK_DRAIN_SPEED,
                              PUMP_WICK_DRAIN_TIME_MS,
                              PUMP_WICK_DRAIN);
}

bool pumping_wick_wet(void)
{
    /* Zero duration is explicitly continuous, until stop or another wick command. */
    return pumping_wick_start(true, CIRRUS1_WICK_WET_SPEED, 0u, PUMP_WICK_WET);
}

bool pumping_wick_stop(void)
{
    if (!pumping_dependencies_ready() ||
        !ztm_stepper_soft_stop(&ztm_stepper_1, PUMP_WICK_STEPPER_ID)) {
        return false;
    }

    wick_timer_active = false;
    wick_deadline_ms = 0u;
    wick_speed = 0.0f;
    wick_state = PUMP_OFF;
    return true;
}

bool pumping_sump_run(void)
{
    if (!pumping_dependencies_ready() ||
        !ztm_stepper_run(&ztm_stepper_1, PUMP_SUMP_STEPPER_ID, PUMP_SUMP_DIRECTION, PUMP_SUMP_SPEED)) {
        return false;
    }

    sump_speed = PUMP_SUMP_SPEED;
    sump_state = PUMP_SUMP_ON;
    return true;
}

bool pumping_sump_stop(void)
{
    if (!pumping_dependencies_ready() ||
        !ztm_stepper_soft_stop(&ztm_stepper_1, PUMP_SUMP_STEPPER_ID)) {
        return false;
    }

    sump_speed = 0.0f;
    sump_state = PUMP_OFF;
    return true;
}

bool pumping_all_stop(void)
{
    if (!pumping_dependencies_ready()) {
        return false;
    }

    bool ok = ztm_stepper_soft_stop_all(&ztm_stepper_1);

    filter_state = PUMP_OFF;
    filter_speed = 0.0f;
    wick_state = PUMP_OFF;
    wick_timer_active = false;
    wick_deadline_ms = 0u;
    wick_speed = 0.0f;
    sump_state = PUMP_OFF;
    sump_speed = 0.0f;

    return ok;
}

int pumping_init(void)
{
    g_stepper_module = modules_find_by_short_name("ztm_stepper");
    if (!g_stepper_module || !g_stepper_module->mod_inst || !ztm_stepper_ready(&ztm_stepper_1)) {
        log_printf("Error: initialized ztm_stepper module not found\n");
        g_pumping_ready = false;
        return -1;
    }

    g_pumping_ready = true;
    if (!pumping_all_stop()) {
        g_pumping_ready = false;
        return -2;
    }
    return 0;
}

pump_state_t pumping_set_state(pump_state_t new_state)
{
    switch (new_state) {
        case PUMP_OFF:
            (void)pumping_all_stop();
            return PUMP_OFF;
        case PUMP_FILTER_NORMAL:
            (void)pumping_filter_start_normal();
            return filter_state;
        case PUMP_FILTER_FAST:
            (void)pumping_filter_start_fast();
            return filter_state;
        case PUMP_WICK_PRIME:
            (void)pumping_wick_prime();
            return wick_state;
        case PUMP_WICK_WET:
            (void)pumping_wick_wet();
            return wick_state;
        case PUMP_WICK_DOSE:
            (void)pumping_wick_dose();
            return wick_state;
        case PUMP_WICK_DRAIN:
            (void)pumping_wick_drain();
            return wick_state;
        case PUMP_SUMP_ON:
            (void)pumping_sump_run();
            return sump_state;
        default:
            return new_state;
    }
}

void pumping_get_status(pumping_status_t *status)
{
    if (!status) {
        return;
    }

    uint32_t remaining_ms = 0u;
    if (wick_timer_active) {
        uint32_t now_ms = to_ms_since_boot(get_absolute_time());
        int32_t remaining = (int32_t)(wick_deadline_ms - now_ms);
        if (remaining > 0) {
            remaining_ms = (uint32_t)remaining;
        }
    }

    *status = (pumping_status_t) {
        .filter_state = filter_state,
        .filter_speed = filter_speed,
        .wick_state = wick_state,
        .wick_speed = wick_speed,
        .wick_remaining_ms = remaining_ms,
        .sump_state = sump_state,
        .sump_speed = sump_speed,
    };
}

int pumping_update(void)
{
    if (!pumping_dependencies_ready()) {
        return -1;
    }

    if (wick_timer_active) {
        uint32_t now_ms = to_ms_since_boot(get_absolute_time());
        if ((int32_t)(now_ms - wick_deadline_ms) >= 0) {
            if (!pumping_wick_stop()) {
                return -2;
            }
        }
    }

    return 0;
}
