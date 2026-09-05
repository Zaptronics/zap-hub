# ZapEE library integration

**Zaptronics Embedded Electronics Library**

## Decision

Reusable Zaptronics chip drivers and ZapLink module implementations live in the
separate `Zaptronics/zap-ee` repository. The Hub is a reference consumer, not
the owner of portable driver/module source.

ZapEE has two first-class low-level integration paths:

1. standard CMake (`FetchContent`, `add_subdirectory`, or `find_package`);
2. native Zephyr external-module integration.

On top of those, projects may optionally use the small `zap` helper and a
human-readable `zap.yml` manifest. The helper never replaces CMake or Zephyr; it
generates their ordinary configuration.

## Hub `zap.yml`

Hub 0.2.23 uses:

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
    version: v0.2.1
    override_var: ZAPEE_SOURCE
    zephyr_module: true
    components:
      - zap::ztm_pwm
```

This is intentionally readable without knowing CMake syntax. The dependency
source, pinned version/ref, dependency cache location, application target, and
selected public component targets are all visible in one place.

## Generated CMake

`zap.ps1` turns the manifest into `cmake/zap_deps.cmake`. That file is committed
and consists only of normal CMake `FetchContent` declarations plus a small
`zap_apply_dependencies(Hub)` target-link function.

Therefore both of these are valid:

```powershell
# Friendly workflow
.\zap
.\build.ps1
```

```powershell
# Traditional workflow: use the committed generated CMake directly
.\build.ps1
```

An experienced user may also replace the generated include with hand-written
`FetchContent`/`target_link_libraries` if desired.

## Dependency source folder

The manifest defaults to:

```text
deps/
  zapee/
```

Only dependency **source** is kept there. CMake binary directories and generated
build bookkeeping stay under `build/`. This makes the dependency sources stable
and easy to browse in an IDE without mixing RP2350-specific object files into
the source tree.

`deps_dir` may be changed in `zap.yml`; it can be relative to the project root or
absolute. `deps/` is ignored by Git.

## Useful commands

```powershell
.\zap new
.\zap
.\zap generate
.\zap check
.\zap list
.\zap version zapee v0.2.1
.\zap add zapee zap::ztm_pwm
.\zap remove zapee zap::ztm_pwm
.\zap adapter stm32
```

A bare `.\zap` synchronises Git dependencies to their exact refs, regenerates the
CMake glue, and runs **configure only**. For this Pico SDK project it delegates to
`build.ps1 -ConfigureOnly`, so dependencies and IDE metadata are available
without compiling the firmware.

The helper deliberately parses a small YAML subset itself. It does not require
Python, PyYAML, a PowerShell module, Node.js, or a separate package manager.

## Local ZapEE development

The existing explicit override remains supported:

```powershell
.\build.ps1 -Clean -ZapEEPath ..\ZapEE
```

or:

```powershell
$env:ZAPEE_SOURCE = 'V:\path\to\ZapEE'
.\zap
```

The override is deliberate: normal builds continue using the released ref in
`zap.yml`, so a sibling checkout cannot silently replace released code.

## Standard CMake equivalent

What the generated file expresses is essentially:

```cmake
include(FetchContent)
FetchContent_Declare(
    zapee
    GIT_REPOSITORY git@github.com:Zaptronics/zap-ee.git
    GIT_TAG        v0.2.1
    SOURCE_DIR     "${CMAKE_SOURCE_DIR}/deps/zapee"
    BINARY_DIR     "${CMAKE_BINARY_DIR}/_zapdeps/zapee"
)
FetchContent_MakeAvailable(zapee)

target_link_libraries(Hub PRIVATE zap::ztm_pwm)
```

`zap::ztm_pwm` owns its transitive SX1509, PAC195x and EMC2303 dependencies, so
the Hub never lists or duplicates those chip drivers.

## Zephyr

For a Zephyr project, `zap.yml` can mark a dependency as a Zephyr module and
provide Kconfig fragments:

```yaml
    zephyr_module: true
    zephyr_config:
      - CONFIG_ZAPEE=y
      - CONFIG_ZAPEE_ZTM_PWM=y
```

The helper synchronises module source **before** configuring Zephyr, passes the
module paths through `ZEPHYR_MODULES`, writes `cmake/zap_zephyr.conf`, and uses
`west build --cmake-only`. Direct west-manifest/ZEPHYR_MODULES usage remains
fully supported without the helper.

## Migration rule

The Hub still owns components not yet migrated. ZTM_PWM and its SX1509, PAC195x
and EMC2303 dependencies are authoritative in ZapEE. For each future migration:

1. move generic driver/module code into ZapEE;
2. remove Hub/MCU-native types from its public API;
3. add host tests where practical;
4. expose one CMake target and appropriate Zephyr Kconfig;
5. add the target to the consuming project's `zap.yml` (or hand-written CMake);
6. test the Hub against the external code;
7. delete the old Hub copy only after hardware validation;
8. tag ZapEE and update the pinned ref.
