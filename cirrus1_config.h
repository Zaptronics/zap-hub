#pragma once

/* Application wiring from notes/cirrus1-configuration.md, not module defaults. */
#define CIRRUS1_FILTER_MOTOR 1u
#define CIRRUS1_WICK_MOTOR   2u
#define CIRRUS1_RETURN_MOTOR 3u
#define CIRRUS1_FILTER_SPEED 30.0f
#define CIRRUS1_WICK_WET_SPEED 30.0f
#define CIRRUS1_WICK_PRIME_SPEED 100.0f
#define CIRRUS1_WICK_DRAIN_SPEED 100.0f
#define CIRRUS1_RETURN_SPEED 30.0f

#define CIRRUS1_TEC_FANS_LOAD 4u
#define CIRRUS1_LED_LEFT_LOAD 9u
#define CIRRUS1_LED_RIGHT_LOAD 10u
#define CIRRUS1_GLASS_HEATER_LOAD 12u
#define CIRRUS1_EXHAUST_FAN 3u
#define CIRRUS1_INTAKE_FAN 6u
/* Intake and exhaust must receive the same RPM target, not just equal duty. */

/* Document labels RT0..RT7 are zero-based; the ADC API uses 1..8. */
#define CIRRUS1_W2_RTD 5u
#define CIRRUS1_W1_RTD 6u
#define CIRRUS1_L1_RTD 7u
#define CIRRUS1_L2_RTD 8u
/* No operating temperatures, heat duties or fan RPM targets supplied yet. */
