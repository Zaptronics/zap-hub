Release update: ZapEE v0.6.0 is published and Hub v0.2.29 now pins that Git tag.
Hardware regression remains pending. The notes below record the development integration.

# Migration status — 2026-09-10

## Current integration

The working tree consumes five ZapEE modules: PWM, ADC, stepper, HV PSU and TEC
PSU. The last three are unreleased local changes. Their old Hub implementations
have been removed, and Hub adapters provide routing, enable controls, sensing,
and logging. The L6470 module uses framed SPI: reserve the fabric for a sequence,
with a distinct CS pulse per physical frame.

Zap's local source is authoritative for tooling. It still uses `zap-package.yml`
for package metadata (schema 1 remains valid without package dependencies),
and `zap.yml` schema 5 for project configuration. The new driver/module entries
are now present in ZapEE's package manifest. Hub uses a sibling path dependency;
Zap regenerated the lockfile and CMake integration from it.

## Validation

- Current Zap source compiled locally into the ignored Hub/zap.exe.
- Zap sync and offline verification succeed against ../ZapEE.
- ZapEE's 14 host tests pass in a fresh MSVC Debug build, with assertions enabled.
- Hub Release builds for pico2_w using Pico SDK 2.3.0 and GCC 15.2.1.
- No hardware upload or electrical regression test has been performed in this session.

The migration review also found a TEC status regression: if EN went low but DAC
shutdown failed, Hub retained the old enabled state. Hub now reads the module
status after stop, retaining the distinction between EN and DAC shutdown.

## Hardware gate before release

Exercise stepper slot order, run/move/stop, reset/configuration restore, KVAL and
microstep changes, fault polling, and inactive driver 4. Verify HV enable,
power-good and voltage feedback behavior, plus all four TEC outputs and their
shutdown behavior. After validation, release ZapEE and replace the development
path with the actual Git tag/commit through Zap, regenerating the lockfile.

## Next boundary: ZTPICO2 support

The schematic is locally available at ../ZapEE/schematics/ZTPICO2.kicad_sch.
It is currently ignored by Git. Component/net inspection confirms the onboard
PCAL6416, TLA2518, CS routing and the module connector functions; a complete
pin-by-pin audit remains to be done before changing board initialization.

Extract PCAL6416 and TLA2518 against portable transport contracts, then put
ZTPICO2 pin definitions, bus setup, CS routing, onboard ADC/expander and module
bindings into a dedicated board-support component. Keep chamber module inventory,
TEC voltage-divider mappings where application-specific, pump assignments, and
control sequencing in Hub. Do not move the whole ztmb directory wholesale:
it also contains application registration, command handling and network services.

After that boundary is proven, separate reusable lifecycle/console services.
Sensor acquisition is still a placeholder in sensing_update(); chamber state,
closed-loop cooling and coordinated supervision remain application work.
