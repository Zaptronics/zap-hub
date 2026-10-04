#pragma once

/* Application wiring from notes/cirrus1-configuration.md, not module defaults. */
#define CIRRUS1_FILTER_MOTOR 1u
#define CIRRUS1_WICK_MOTOR   3u /* wick and return swapped on Cirrus1 (2026-10-04) */
#define CIRRUS1_RETURN_MOTOR 2u
#define CIRRUS1_FILTER_SPEED 100.0f
#define CIRRUS1_WICK_WET_SPEED 30.0f
#define CIRRUS1_WICK_PRIME_SPEED 100.0f
#define CIRRUS1_WICK_DRAIN_SPEED 100.0f
#define CIRRUS1_RETURN_SPEED 100.0f

#define CIRRUS1_TEC_FANS_LOAD 4u
#define CIRRUS1_LED_LEFT_LOAD 9u
#define CIRRUS1_LED_RIGHT_LOAD 10u
#define CIRRUS1_GLASS_HEATER_LOAD 12u
#define CIRRUS1_EXHAUST_FAN 3u
#define CIRRUS1_INTAKE_FAN 6u
/* Intake and exhaust must receive the same RPM target, not just equal duty.
 * Presets start both open loop, then lock both to the slower fan's RPM. */
#define CIRRUS1_FAN_LOW_PCT      30u /* 20% surged on Cirrus1 (2026-10-04) */
#define CIRRUS1_FAN_MODERATE_PCT 50u
#define CIRRUS1_FAN_HIGH_PCT     90u
#define CIRRUS1_FAN_SETTLE_MS    5000u
/* Closed-loop floor. The EMC2303 default (40%) is above the low preset's
 * 20%, so the low target could not lock and both fans ran past 2000 RPM.
 * 20% keeps the loop from hunting down near stall yet stays below the 30%
 * low preset, so the low target can still lock. */
#define CIRRUS1_FAN_MIN_DRIVE_PCT 20u

/* Document labels RT0..RT7 are zero-based; the ADC API uses 1..8. */
#define CIRRUS1_C_RTD  3u
#define CIRRUS1_B_RTD  4u
#define CIRRUS1_W2_RTD 5u
#define CIRRUS1_W1_RTD 6u
#define CIRRUS1_L1_RTD 7u
#define CIRRUS1_L2_RTD 8u
/* Document labels TC0..TC2 are zero-based; the ADC API uses 1..3. Both K-type. */
#define CIRRUS1_CB_TC           1u /* Process Chiller loop return pipe */
#define CIRRUS1_TEC_HEATSINK_TC 2u /* PSU_TEC 2 heat-sink */

/* Run protection. PLACEHOLDER values chosen to be conservative; replace with
 * confirmed operating limits. While any TEC is on, crossing a trip turns all
 * TECs off and revokes TEC permission; fans and other outputs keep running. */
#define CIRRUS1_TRIP_HEATSINK_C    60.0 /* TEC2 (PSU_TEC 2 heat-sink) above */
#define CIRRUS1_TRIP_CB_C          35.0 /* CB (chiller return) above */
#define CIRRUS1_TRIP_PLATE_HIGH_C  45.0 /* any plate RTD above: plate heating */
#define CIRRUS1_TRIP_PLATE_LOW_C  -50.0 /* any plate RTD below */
/* TEC start needs a valid CB reading at or below this ("chiller ready"). */
#define CIRRUS1_CB_READY_C         25.0
/* A CB or TEC2 fault on this many reads in a row trips the TECs. */
#define CIRRUS1_SENSOR_FAULT_READS 2u
/* A running fan below this RPM on this many 1 s reads in a row stops both. */
#define CIRRUS1_FAN_STALL_RPM      200u
#define CIRRUS1_FAN_STALL_READS    3u
/* The ITO glass heater has no sensor; Run caps its prompt here until rated. */
#define CIRRUS1_GLASS_MAX_PCT      50u

/* Run sequences (keys 2-6, pressed twice). PLACEHOLDER values chosen to be
 * mild; replace with confirmed operating targets. Go has no values yet. */
#define CIRRUS1_SEQ_CONFIRM_MS         3000u  /* second press window */
#define CIRRUS1_SEQ_STANDBY_FAN_LEVEL  1u     /* 0-3 preset: low */
#define CIRRUS1_SEQ_STANDBY_LED_PCT    20u    /* low light, both LEDs */
#define CIRRUS1_SEQ_STANDBY_GLASS_PCT  0u     /* mild glass heat: unset */
#define CIRRUS1_SEQ_PRIME_MS           60000u /* filter + return + wick prime */
#define CIRRUS1_SEQ_RUNUP_TEC_PCT      30u    /* low TEC during Runup */
#define CIRRUS1_SEQ_RUNUP_READY_C     -20.0   /* plate B at or below: Runup ready */
#define CIRRUS1_SEQ_RAMP_STEP_PCT      10u    /* Shutdown TEC ramp step */
#define CIRRUS1_SEQ_RAMP_STEP_MS       30000u
