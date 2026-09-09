# Cloud chamber Hub

Hub is the cloud chamber application running on the Zaptronics ZTPICO2 board.
ZapEE supplies reusable Zaptronics chip drivers and module libraries. The sibling
Zap repository is the canonical source for package-manager formats and behavior.

## Development build

Hub 0.2.29 pins ZapEE v0.6.0 from GitHub. Zap records the exact commit in
`zap.lock` and downloads the selected components. Hardware regression testing
of the migrated modules is still pending.

With the current Zap executable on PATH:

```powershell
zap sync --no-configure
zap verify --offline
zap make --offline --build-dir build-migration
```

`zap.yml` is project intent; `zap.lock` and `cmake/zap_deps.cmake` are generated
by Zap. Do not edit generated files manually. ZapEE's `zap-package.yml` is the
separate package component manifest; that filename remains correct.

The Pico SDK, Arm compiler, Ninja and programming tools must be installed.
Set `ZAP_HUB_WIFI_SSID` and `ZAP_HUB_WIFI_PASSWORD` locally before building when
network access is needed. Do not commit credentials. Build caches from the old
`Documents/Electronics/ZTPICO2/Firmware` location must not be reused.

## Current ownership

- `Hub.c`, `sensing`, `cooling`, `pumping`, `sweeping`: chamber behavior.
- `ztmb/modules.c`: the chamber's fitted module inventory.
- `ztmb/ztm_*_adapter.c`: bind ZapEE modules to motherboard buses and controls.
- `ztmb/bus`, `cs_mux`, `io_exp`, `adc`, `ztmb`: board support still awaiting extraction.
- `drivers/pcal6416` and `drivers/tla2518`: remaining local chip drivers.
- ZapEE: PWM, ADC, stepper, HV PSU and TEC PSU module implementations and their drivers.

See [migration status](documentation/MIGRATION_STATUS.md) for validation and the
next extraction boundary. Older architecture documents describe the original
combined tree and should be read in that context.
