# ZapEE library integration

**Zaptronics Embedded Electronics Library**

Reusable Zaptronics chip drivers and ZapLink module implementations live in the
separate `Zaptronics/zap-ee` repository. The Hub is a reference consumer, not
the owner of portable driver/module source.

ZapEE remains ordinary CMake plus native Zephyr metadata. The optional native
`Zaptronics/zap` executable adds a friendly `zap.yml` control plane without
replacing either build system.

## Hub manifest

The Hub pins an exact release and deliberately selects portable module targets
because its motherboard supplies custom bus/enable/IRQ adapters:

```yaml
schema: 1
project:
  environment: pico-sdk
  target: Hub
  board: pico2_w
  build_dir: build
  deps_dir: deps
  adapters_dir: adapters
dependencies:
  zapee:
    type: git
    uri: git@github.com:Zaptronics/zap-ee.git
    version: v0.5.0
    override_var: ZAPEE_SOURCE
    zephyr_module: true
    components:
      - zap::ztm_pwm
      - zap::ztm_adc
```

## Native workflow

Install the `zap` executable once and put it on `PATH`, then from the Hub root:

```text
zap sync
.\build.ps1 -Clean
```

Bare `zap` means `zap sync`. The tool reads ZapEE's `zap-package.yml`, resolves
the selected component graph, and creates a depth-1 Git sparse checkout in
`deps/zapee`. Only required module/driver source directories plus package-common
paths are materialised. CMake objects remain under `build/`.

Useful commands include:

```text
zap status
zap list
zap update zapee
zap version zapee v0.5.0
zap add zapee zap::ztm_adc
zap remove zapee zap::ztm_adc
zap adapter stm32
```

Managed dependency checkouts are never revision-switched while dirty.

## Traditional CMake workflow

`zap` is optional. `cmake/zap_deps.cmake` is committed. It consumes
`deps/zapee` when that sparse checkout exists; otherwise it falls back to normal
shallow CMake `FetchContent` using the URI/ref from the generated file. An
experienced user can therefore ignore `zap` entirely or replace the generated
include with hand-written CMake.

## Local ZapEE development

The explicit source override remains supported:

```powershell
.\build.ps1 -Clean -ZapEEPath ..\ZapEE
```

or:

```powershell
$env:ZAPEE_SOURCE = 'V:\path\to\ZapEE'
.\build.ps1 -Clean
```

Normal builds continue using the release pinned in `zap.yml`.
