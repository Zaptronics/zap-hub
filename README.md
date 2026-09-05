# Zap Hub

Zap Hub is a modular embedded systems controller for user-designed hardware connected through ZapLink.

It provides the common infrastructure needed when many independent modules must share a small number of physical resources: SPI and I2C/TWI controllers, chip-select routing, enable lines, interrupts, diagnostic interfaces, and network access. Module authors can concentrate on the behaviour of their own hardware without each module reimplementing bus arbitration, startup sequencing, fault reporting, or service tooling.

The current repository contains one reference platform port and several example modules. The architecture is intended to remain portable across microcontrollers and embedded operating environments; Zap Hub is not defined by a particular MCU, development board, or end application.


## Building and uploading on Windows

The repository includes PowerShell helpers that work with the tool layout installed by the Raspberry Pi Pico VS Code extension. The reference target is `pico2_w`.

```powershell
# Configure and build a Release image
.\build.ps1

# Clean and rebuild
.\build.ps1 -Clean

# Build, then upload using the best available method
.\upload.ps1 -Build

# Explicit upload methods
.\upload.ps1 -Method Uf2       # Pico 2 W in BOOTSEL mode
.\upload.ps1 -Method OpenOcd   # CMSIS-DAP / Raspberry Pi Debug Probe over SWD
.\upload.ps1 -Method Picotool  # picotool, when the target is accessible over USB
```

`build.ps1` discovers the Pico SDK, Arm toolchain, CMake, Ninja, and picotool from the normal `~/.pico-sdk` extension installation, while still respecting `PICO_SDK_PATH` and `PICO_TOOLCHAIN_PATH` when they are already set. Web assets are embedded by CMake itself, so no separate Python or `xxd` installation is required. Build products are written to `build/` by default.

### Optional Zap dependency helper

The Hub also demonstrates the optional YAML workflow for ZapEE and other CMake dependencies:

```powershell
.\zap list
.\zap                 # sync dependencies + CMake configure only
.\build.ps1           # normal firmware build
```

`zap.yml` is the human-readable source of truth. `zap.ps1` generates ordinary `cmake/zap_deps.cmake`, checks out dependency sources under the configurable `deps/` folder, and then performs a configure-only pass so VS Code/IntelliSense can immediately see headers such as `<zaptronics/modules/ztm_pwm.h>`. The helper is optional; `cmake/zap_deps.cmake` is committed and remains normal CMake for users who prefer to work directly with CMake.

`upload.ps1` first looks for a mounted `RP2350`/`RPI-RP2` BOOTSEL volume, then tries picotool, then the Raspberry Pi OpenOCD/CMSIS-DAP path. The current firmware uses UART stdio and has USB stdio disabled, so a running Hub cannot normally be forced into BOOTSEL by picotool; BOOTSEL mass-storage or SWD are the dependable upload paths.

Wi-Fi credentials are intentionally not stored in the source tree. Set them for a build using environment variables (preferred because they do not land in shell history), or pass them to `build.ps1`:

```powershell
$env:ZAP_HUB_WIFI_SSID = 'your-ssid'
$env:ZAP_HUB_WIFI_PASSWORD = 'your-password'
.\build.ps1

# Equivalent one-off build:
.\build.ps1 -WifiSsid 'your-ssid' -WifiPassword 'your-password'
```

CMake stores those values only in the ignored local `build/` cache and generates `wifi_config.h` there.

## What Zap Hub solves

Modular embedded systems become difficult when independently developed boards compete for shared resources. Typical problems include:

- two execution contexts accessing the same peripheral at once;
- devices on one SPI controller requiring different modes or clock rates;
- multiple SPI controllers sharing an external chip-select decoder;
- I2C/TWI devices requiring bounded transfer times and independent enable control;
- daisy-chained devices requiring complete frames even when only one device is addressed;
- partial hardware population preventing otherwise healthy modules from starting;
- diagnostic commands bypassing normal ownership rules;
- application logic becoming tightly coupled to individual chip drivers.

Zap Hub addresses these problems with a managed bus layer, a module lifecycle, reusable chip drivers, and a clear boundary between hardware services and application policy.

## Design goals

- **Modular** — user-created ZapLink modules can be added without restructuring the whole firmware.
- **Contention-safe** — shared buses are claimed and released as complete transactions.
- **Device-aware** — bus speed, SPI mode, timing, addressing, and enable control belong to the device endpoint.
- **Degraded-operation friendly** — one failed or absent module does not need to hide every other module.
- **Diagnosable** — the console remains available during partial startup failures.
- **Portable** — drivers and module contracts are kept separate from the platform implementation.
- **Policy-neutral** — the Hub exposes hardware capabilities; an application or supervisor decides how they are coordinated.
- **Safe by construction** — modules should initialise into an inactive state and expose explicit shutdown behaviour.

## Architecture

```text
Application or system policy
        |
Optional supervisor / state machine
        |
Reusable subsystem services
        |
ZapLink module implementations
        |
Generic chip drivers
        |
Managed SPI, I2C/TWI, GPIO and routing services
        |
Platform adaptation layer
```

The important ownership rule is that each layer only knows what it needs:

- a **chip driver** knows the silicon protocol;
- a **module** knows the board topology and connector mapping;
- a **subsystem service** combines module capabilities into a reusable function;
- a **supervisor or application** owns sequencing, interlocks, and operating policy;
- the **platform layer** supplies timers, locks, peripheral access, and networking.

See [`documentation/ARCHITECTURE.md`](documentation/ARCHITECTURE.md) for the complete model.

## Managed buses

The managed bus layer is the core of Zap Hub.

For SPI it provides:

- exclusive ownership of the complete physical SPI fabric;
- per-device clock rate, mode, bit order, and chip-select timing;
- one-shot framed transfers;
- explicit transactions where chip select remains asserted across several transfers;
- support for an external chip-select decoder shared by more than one SPI controller;
- homogeneous or heterogeneous serial chains;
- a driver-facing target that can represent either a direct device or a chain slot.

For I2C/TWI it provides:

- per-controller locking;
- per-device bus speed and timeout configuration;
- bounded reads and writes;
- optional module-enable control;
- consistent error propagation.

See [`documentation/MANAGED_BUSES.md`](documentation/MANAGED_BUSES.md).

## Module model

A ZapLink module owns its physical design. This includes:

- which chips are fitted;
- how they are connected;
- bus addresses and chip-select routing;
- enable and interrupt lines;
- board-specific channel maps;
- startup checks;
- safe-state behaviour;
- periodic health polling;
- module-level diagnostic commands.

The module registry provides a common lifecycle:

```text
register -> initialise -> ready/failed -> poll -> deinitialise/reinitialise
```

Initialisation can be strict, with rollback after a failure, or permissive, where every module is attempted independently and healthy modules remain available for diagnosis.

See [`documentation/MODULE_DEVELOPMENT.md`](documentation/MODULE_DEVELOPMENT.md).

## Portability

The core design is intended to be cross-platform, but the current reference build still contains direct dependencies on its present SDK in the platform and bus implementation. Treat that build as the first platform port, not as the definition of Zap Hub.

Portable code should avoid direct platform calls outside the adaptation boundary. The reusable chip drivers and ZapLink modules are now planned to live in the separate `zap-ee` CMake/Zephyr library; the copies in this repository remain authoritative only until each component is deliberately extracted and hardware-validated.

See [`documentation/PORTABILITY.md`](documentation/PORTABILITY.md) for the current boundary and porting plan, and [`documentation/ZAPEE_LIBRARY.md`](documentation/ZAPEE_LIBRARY.md) for the package/integration contract.

## Repository layout

```text
./
├── Hub.c                    Reference application entry point
├── drivers/                 Reusable chip-level drivers
├── modules/                 ZapLink board/module implementations
├── ztmb/                    Hub core, managed buses, registry, CLI and services
├── documentation/           Architecture and development documentation
├── http/                    Embedded web assets for the reference port
├── tools/                   Build and asset-generation utilities
└── CMakeLists.txt           Current reference-platform build
```

The files at the repository root that implement named subsystems are application-facing examples. They are not part of the fundamental module or bus contract.

## Current status

The current codebase provides:

- managed SPI and I2C/TWI transactions;
- shared chip-select routing;
- serial SPI chain composition;
- a static module registry with partial-startup support;
- module polling and reinitialisation;
- UART and network-accessible diagnostic CLI;
- embedded HTTP assets and a simple command bridge;
- reusable drivers and example ZapLink modules.

The main architectural work still to complete is:

- extracting a formal platform/HAL interface from the current SDK-specific implementation;
- queuing external commands into one authoritative execution context;
- adding a generic supervisor framework for systems that require coordinated control and fault latching;
- formalising module descriptors and discovery for projects that need runtime rather than compile-time registration;
- adding host-side unit tests for bus framing, module lifecycle, and command parsing.

## Documentation

- [`documentation/ARCHITECTURE.md`](documentation/ARCHITECTURE.md) — layers, ownership, startup, and execution model
- [`documentation/MANAGED_BUSES.md`](documentation/MANAGED_BUSES.md) — contention management and transaction rules
- [`documentation/HETEROGENEOUS_SPI_CHAINS.md`](documentation/HETEROGENEOUS_SPI_CHAINS.md) — serial-chain composition model
- [`documentation/MODULE_DEVELOPMENT.md`](documentation/MODULE_DEVELOPMENT.md) — creating and registering a ZapLink module
- [`documentation/DRIVER_DEVELOPMENT.md`](documentation/DRIVER_DEVELOPMENT.md) — writing reusable chip drivers
- [`documentation/CONSOLE_AND_DIAGNOSTICS.md`](documentation/CONSOLE_AND_DIAGNOSTICS.md) — service interface and diagnostic conventions
- [`documentation/SUPERVISOR_DESIGN.md`](documentation/SUPERVISOR_DESIGN.md) — optional coordinated-control layer
- [`documentation/PORTABILITY.md`](documentation/PORTABILITY.md) — platform boundaries and porting guidance
- [`documentation/FIRMWARE_VERSIONING.md`](documentation/FIRMWARE_VERSIONING.md) — release numbering and version source
- [`documentation/ZAPEE_LIBRARY.md`](documentation/ZAPEE_LIBRARY.md) — reusable library, CMake/Zephyr integration, and extraction workflow

## Building the current reference port

The checked-in CMake configuration builds the current reference target and requires its SDK and toolchain. This is the only platform port presently supplied in the repository.

A new target should provide equivalents for:

- SPI and I2C/TWI access;
- GPIO and external routing control;
- critical sections or mutexes;
- monotonic time and delays;
- logging and console transport;
- optional network services;
- build-time generation of version and embedded assets.

Platform-independent drivers and module logic should not need to be rewritten when those interfaces are preserved.

## Licence

No licence file is currently included. Add an explicit licence before distributing the project or accepting external contributions.

## Reusable Zaptronics modules

From Hub 0.2.21, ZTM_PWM is consumed from the separate ZapEE C11 library
rather than duplicated in the Hub source tree. Hub 0.2.23 describes ZapEE `v0.2.1` in `zap.yml` and generated CMake
from the private GitHub repository over SSH. Pass `-ZapEEPath` to `build.ps1` only when deliberately testing a local checkout, or use the equivalent `ZAPEE_SOURCE` override. See
`documentation/ZTM_PWM_EXTERNAL_LIBRARY.md`.
