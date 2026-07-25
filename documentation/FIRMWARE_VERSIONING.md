# Firmware Versioning

The Hub firmware now uses semantic versioning and has a single version definition in `CMakeLists.txt`:

```cmake
set(HUB_FIRMWARE_VERSION "0.2.18")
```

CMake uses this value for the Pico binary metadata and generates `firmware_version.h` for the C source. The version is printed at startup and is available through:

```text
version
```

Version policy:

- Patch: bug fixes and small corrections, for example `0.2.0` → `0.2.1`.
- Minor: a completed module or substantial feature, for example `0.2.x` → `0.3.0`.
- Major: incompatible system-level API or architecture changes.

- **0.2.2** — AD7124 RTD reference margin and detailed conversion diagnostics.
- **0.2.3** — Extended AD7124 conversion timing and mode-readback diagnostics.

- **0.2.4** — Exception-safe PWM module probing and initialization diagnostics.
- **0.2.5** — PAC1952-2 salvage current mapping and partial-monitor operation.
- **0.2.6** — PAC1 address changed to `0x1F` for the missing ADDRSEL resistor.
- **0.2.7** — Safe-default LM5164/AD5282 high-voltage drive module.
- **0.2.8** — XP Power E60 drive interpretation, nominal HV estimation, and 15 V input protection.
- **0.2.9** — HV operating-code floor and live startup VSENSE/PGOOD diagnostics.
- **0.2.10** — Guarded HV calibration sweep and E60 drive/output interpretation.
- **0.2.11** — Clean module-driver and hardware-test-console baseline.
- **0.2.12** — TCP CLI input framing fix, Telnet negotiation filtering, CR/LF support, and connection banner with firmware version.
- **0.2.13** — Per-fan EMC2303 PWM output mode control, with push-pull defaults and explicit external TACH pull-up documentation.
- **0.2.14** — Treat saturated EMC2303 tach readings (`0x1FFF`) as fan stall/no-valid-tach faults instead of reporting 480 RPM.
- **0.2.15** — Keep fan stall available through explicit CLI status without repeatedly logging it from the periodic IRQ scan; queue TCP CLI output, preserve prompts for long commands, and normalize bare LF output to CRLF.
- **0.2.16** — Configurable per-channel thermocouple types with Type K defaults, NIST cold-junction compensation, temperature output in degrees C, and disconnected detection for ADC rail codes.
- **0.2.17** — Query and set per-motor or all-motor L6470 KVAL configurations from the CLI, retain runtime tuning across stepper resets, and restore compiled defaults explicitly.
- **0.2.18** — Query and set per-motor or all-motor L6470 microstep resolution from full-step through 1/128, retain the runtime selection across stepper resets, and safely force HiZ/reset position when changing modes.
