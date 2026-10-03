# Cirrus1 wiring integration (Hub 0.5.0)

Source: ../notes/cirrus1-configuration.md. Chamber-specific constants live in
../cirrus1_config.h; they do not change ZapEE module defaults.

| Function | Assignment | Normal operation |
|---|---|---|
| Filter | motor 1 | forward 30 steps/s |
| Wick | motor 2 | forward 30 continuously; prime forward 100; drain reverse 100 |
| Return/sump | motor 3 | forward 30 steps/s |
| TEC fans | PWM L4 | duty not specified |
| Left/right lights | PWM L9/L10 | low duty not specified |
| ITO glass heater | PWM L12 | standby/running duty not specified |
| Exhaust/intake | FAN3/FAN6 | equal RPM targets; RPM not specified |
| W2/W1/L1/L2 | document RT4/RT5/RT6/RT7 | ADC API channels 5/6/7/8 |

Unassigned temperature inputs are not given invented sensor roles. TEC PSU to
plate sensor relationships and temperature control targets remain unspecified.

`pump wick-wet` runs until `pump wick-stop`, `pump stop-all`, `safe`, or another
wick command. It cancels any previous wick timer. Timed prime/drain retain the
existing five seconds; timed dose retains two seconds at the new 30 steps/s.
These retained durations are manual-command defaults, not approved run recipes.
The existing filter-fast diagnostic remains 100 steps/s in the forward direction.

Legacy `pump start/stop` API IDs remain 1=filter, 2=sump, 3=wick, preserving the
existing web API. They are role IDs: role 2 now operates motor 3 and role 3 motor 2.
Prefer named pump commands. Status/help show the actual motor assignments.

Run modes remain blocked pending operating targets, prime/shutdown policy,
board checks and monitoring/regulation implementation. No hardware validation,
builds, tests or rollback application were run.
