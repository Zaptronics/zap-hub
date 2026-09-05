# Hub 0.2.20 baseline review

## Scope

This revision is a maintenance baseline before portable ZapLink module extraction. It intentionally preserves the current `ztmb` / `modules` / `drivers` architecture and avoids changing module behaviour or public module APIs.

## Maintenance changes

- Added `build.ps1` for Pico SDK VS Code extension installations on Windows.
- Added `upload.ps1` with BOOTSEL UF2, picotool, and CMSIS-DAP/OpenOCD upload paths.
- Removed the non-functional `xxd` fallback and made web-asset generation self-contained in CMake, avoiding a separate Python dependency.
- Removed Wi-Fi credentials from source headers; credentials are now local CMake build settings.
- Fixed `log_printf()` so it uses the project's queued logging path rather than bypassing it via the C library `vprintf()`.
- Fixed HTTP response lifetime for transient stack-built bodies when the lwIP send buffer cannot accept the complete response immediately.
- Added HTTP request/header bounds handling and several defensive error checks.
- Fixed Wi-Fi status formatting to avoid repeated use of lwIP's shared `ip4addr_ntoa()` buffer in one formatted output call.
- Corrected generated web-asset declarations and C prototypes.
- Removed unused private helpers and stale generated citation markers from driver source comments.
- Added a check for failure to start the one-second repeating timer.

## Validation performed

- Web-asset generator was executed and its generated C compiled with host C11 warnings-as-errors.
- `ztmb/http_server.c` was syntax-compiled against a small lwIP API stub with host C11 warnings-as-errors.
- `ztmb/web_assets.c` was syntax-compiled with host C11 warnings-as-errors.
- Static scans were run for stale generated citation markers, TODO/FIXME markers, old-style empty parameter prototypes, and obvious unused private functions.

A complete RP2350 firmware build was not possible in the review environment because the Raspberry Pi Pico SDK/Arm embedded toolchain and PowerShell are not installed there. The first bench action should therefore be a clean Windows build with the normal VS Code Pico SDK installation, followed by a BOOTSEL or SWD upload.

## Portability boundary for the next phase

The existing conceptual layering is sound, but the reusable layers are not yet platform-independent. In particular:

- several chip driver headers expose `i2c_inst_t` or otherwise depend on `ztmb/bus.h`;
- several drivers call Pico timing/delay primitives directly;
- `modules/ztm_pwm.*` exposes Pico I2C types and owns motherboard enable/interrupt GPIO assumptions;
- `ztmb/bus.h` currently combines the useful managed-bus abstraction with RP2350/Pico SDK types and motherboard pin definitions.

The recommended first extraction is ZTM_PWM. Introduce a small portable transport contract (opaque context plus I2C transfer, delay/time, optional lock, and GPIO callbacks), adapt SX1509/PAC195x/EMC2303 to it, then move ZTM_PWM board topology onto those portable drivers. The Hub should become one adapter/consumer of that library rather than the owner of the library API.

Do not force an RTOS or thread implementation into the reusable package. The transport/platform adapter should own locking policy so the same module library can run bare-metal, under an RTOS, or in a host-side test harness.
