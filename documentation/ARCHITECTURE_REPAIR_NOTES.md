# Hub Architecture Repair Pass

This pass stabilizes the existing module framework and startup path without redesigning the chamber control system or web API.

## Main changes

- Moved all module instance storage out of headers and into one owning `.c` file per module.
- Moved the module registry into `ztmb/modules.c`; lookups now return only successfully initialized modules.
- Added checked startup sequencing for base hardware, modules, and chamber subsystems.
- Removed startup infinite loops for I/O-expander and onboard-ADC failures so the console can still be brought up for diagnosis.
- Corrected chamber controllers to use `module_entry_t.mod_inst` rather than treating `bus_ctx` as a device instance.
- Initialized the L6470 chain with its actual device count and mapped public pump IDs 1..4 to driver indices 0..3.
- Corrected pump timer initialization and stop behaviour.
- Made HV control operate on the registered module instance and leave the output at code zero and disabled after initialization.
- Made the PWM module own both EMC2303 devices and the SX1509, with checked initialization results.
- Corrected the EMC2303 init return type so negative driver errors are not converted to boolean success.
- Corrected I2C multi-byte read/write result checks and mutable read-buffer types.
- Corrected the onboard ADC return-value shadowing bug.
- Corrected the AD5282 address composition macro.
- Added command range/readiness checks for pump, TEC, and PWM console commands.

## Deliberately deferred

- Per-device SPI mode and baud-rate switching on shared SPI buses.
- Bus locking/transaction ownership.
- A central authoritative system-state and fault model.
- Real telemetry and command acknowledgement in the web interface.
- Authentication and credential provisioning.
- Full module deinitializers and rollback of partially initialized hardware.

## Validation performed

- All files modified in this pass were parsed with Clang in C11/GNU11 mode using Pico SDK interface stubs.
- Return-type checking and diff whitespace checks passed.
- Searches confirmed that module instances are no longer defined as `static` in headers and chamber controllers no longer dereference `bus_ctx` as a module object.

A complete Pico SDK build and hardware test were not possible in the analysis environment. Build the project from a clean build directory before flashing.

## Suggested first hardware smoke test

1. Boot with all modules connected and confirm each `Module ready:` line appears.
2. Confirm the four subsystem readiness results are reported.
3. Test `pump start 1`, `pump stop 1`, then IDs 2 through 4.
4. Test `tec start 0`, a low non-zero code, then `tec stop` while monitoring the enable line and DAC output.
5. Test `pwm start 0`, then a low value, then `pwm stop`.
6. Confirm the HV enable remains inactive after boot and the programmed control code is zero before testing any non-zero sweeper command.
