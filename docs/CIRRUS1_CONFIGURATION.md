# Cirrus1 wiring integration (Hub 0.5.0)

Source: ../notes/cirrus1-configuration.md. Chamber-specific constants live in
../cirrus1_config.h; they do not change ZapEE module defaults.

| Function | Assignment | Normal operation |
|---|---|---|
| Filter | motor 1 | forward 100 steps/s |
| Wick | motor 3 | forward 30 continuously; prime forward 100; drain reverse 100 |
| Return/sump | motor 2 | forward 100 steps/s |
| TEC fans | PWM L4 | 0 or 100% only; Run turns them on before any TEC start and keeps them on while a TEC runs |
| Left/right lights | PWM L9/L10 | Standby low light 20% (placeholder) |
| ITO glass heater | PWM L12 | Run caps at 50%, Standby 0% (placeholders; no glass sensor) |
| Exhaust/intake | FAN3/FAN6 | presets 30/50/90% open loop, then both locked to the slower fan's RPM (closed-loop floor 20%) |
| C/B/W2/W1/L1/L2 | document RT2/RT3/RT4/RT5/RT6/RT7 | ADC API channels 3/4/5/6/7/8 |
| CB / TEC2 | document TC0/TC1, K-type | ADC API channels 1/2: chiller return pipe, PSU_TEC 2 heat-sink |

Unassigned temperature inputs are not given invented sensor roles. TEC PSU to
plate sensor relationships and temperature control targets remain unspecified.

`pump wick-wet` runs until `pump wick-stop`, `pump stop-all`, `safe`, or another
wick command. It cancels any previous wick timer. Timed prime/drain retain the
existing five seconds; timed dose retains two seconds at the new 30 steps/s.
These retained durations are manual-command defaults, not approved run recipes.
The filter-fast diagnostic is 100 steps/s forward, now the same as normal filter flow.

Legacy `pump start/stop` API IDs remain 1=filter, 2=sump, 3=wick, preserving the
existing web API. They are role IDs; since Hub 0.6.18 role 2 (sump) is motor 2 and role 3 (wick) motor 3.
Prefer named pump commands. Status/help show the actual motor assignments.

Run (docs/RUN_CONSOLE.md) now drives these outputs manually, applies placeholder
protection limits and runs draft Standby, Prime, Runup and Shutdown sequences;
Tests and Go remain unavailable. Updated for Hub 0.6.17. No builds, tests or
rollback application were run by Claude.
