# ZTM_PWM external library migration (Hub 0.2.21+)

Hub 0.2.21 no longer owns the ZTM_PWM board implementation or its SX1509,
PAC195x and EMC2303 chip drivers. Those production sources live in the separate
ZapEE library and are linked as the CMake target `zap::ztm_pwm`.

## Dependency selection

Hub 0.2.23 describes the private GitHub dependency in `zap.yml` and pins the exact ZapEE
release `v0.2.1` over SSH:

```text
git@github.com:Zaptronics/zap-ee.git
```

The build machine therefore needs Git and authorization to that repository.
For active ZapEE development, explicitly override the release with a local
checkout:

```powershell
.\build.ps1 -Clean -ZapEEPath ..\ZapEE
```

or set:

```powershell
$env:ZAPEE_SOURCE = 'V:\path\to\ZapEE'
.\build.ps1 -Clean
```

There is intentionally no sibling-directory auto-detection: an ordinary build
should always use the released, reproducible dependency unless a developer
explicitly opts into local source.

## Ownership boundary

Portable library:

- ZTM_PWM board behavior and public API;
- SX1509, PAC195x and EMC2303 drivers;
- generic I2C transaction contract;
- generic delay contract;
- module fault/event definitions.

Hub adapter (`ztmb/ztm_pwm_adapter.c` / `ztmb/ztm_pwm_adapter.h`):

- maps the RP2350/Pico SDK I2C controller to `zap_i2c_bus_t`;
- preserves the Hub's existing I2C critical-section/configuration machinery;
- maps PCAL6416 connector enable/IRQ pins to ZTM_PWM callbacks;
- maps structured portable module events back into the Hub logger;
- owns the Hub's convenience singleton `ztm_pwm_1` for its CLI/application.

The portable library has no dependency on Pico SDK, PCAL6416, the Hub logger or
the Hub module registry.

There is deliberately no `modules/ztm_pwm.*` implementation in the Hub. The
`ztm_pwm` implementation is supplied only by ZapEE through the `zap::ztm_pwm`
CMake target; the local `ztm_pwm_adapter.*` files contain platform glue only.

## Release path

Hub releases consume tagged ZapEE releases. Development can temporarily point
at a local checkout, but before a Hub release the corresponding ZapEE changes
are committed, tagged, pushed, and the Hub manifest is advanced with, for example:

```powershell
.\zap version zapee v0.2.1
```

The generated CMake then continues to link the same `zap::ztm_pwm` target.
