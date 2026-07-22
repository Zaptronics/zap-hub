# Firmware Versioning

The Hub firmware now uses semantic versioning and has a single version definition in `CMakeLists.txt`:

```cmake
set(HUB_FIRMWARE_VERSION "0.2.8")
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
