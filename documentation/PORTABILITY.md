# Portability and platform adaptation

## Intent

Zap Hub is intended to be a cross-platform embedded architecture. The present repository contains one working reference port, and some core files still use native SDK types directly.

Portability therefore describes both a design rule and an unfinished refactor. The documentation must not imply that the current source already builds unchanged on arbitrary platforms.

## Portable and platform-specific areas

### Intended portable areas

- chip register definitions and conversion functions;
- driver state machines;
- module topology and logical channel mapping;
- module registry semantics;
- serial-chain composition rules;
- command parsing independent of transport;
- subsystem and supervisor logic;
- error and status models.

### Currently platform-coupled areas

- peripheral instance types in bus descriptors;
- GPIO pin and function configuration;
- critical-section implementation;
- monotonic timer and repeating-timer setup;
- delay functions;
- UART setup;
- Wi-Fi and network stack integration;
- bootloader/reset commands;
- build system and binary metadata;
- reference-board pin assignments.

## Target platform interface

A future platform contract should provide opaque handles and operations similar to:

```c
typedef struct hub_spi_controller hub_spi_controller_t;
typedef struct hub_i2c_controller hub_i2c_controller_t;
typedef struct hub_lock hub_lock_t;

typedef struct {
    bool (*spi_configure)(hub_spi_controller_t *, const hub_spi_config_t *);
    bool (*spi_transfer)(hub_spi_controller_t *, const uint8_t *, uint8_t *, size_t);
    bool (*i2c_write)(hub_i2c_controller_t *, uint8_t, const uint8_t *, size_t, uint32_t);
    bool (*i2c_read)(hub_i2c_controller_t *, uint8_t, uint8_t *, size_t, uint32_t);
    void (*gpio_write)(hub_gpio_t, bool);
    bool (*gpio_read)(hub_gpio_t);
    uint64_t (*monotonic_ms)(void);
    void (*delay_us)(uint32_t);
} hub_platform_ops_t;
```

The final interface need not use this exact shape. The goals are:

- no native SDK types in reusable driver headers;
- explicit timeout and timing semantics;
- test doubles for host-side unit tests;
- platform-specific routing isolated from module logic;
- no performance penalty that encourages drivers to bypass the layer.

## Porting sequence

### 1. Identify physical fabrics

List independent resources rather than only controller peripherals:

- SPI controllers;
- shared external chip-select or bus switches;
- I2C/TWI controllers and multiplexers;
- shared enable/reset routing;
- interrupt aggregation;
- DMA channels if used.

Define the lock boundary around each complete physical fabric.

### 2. Implement platform primitives

Provide:

- lock initialisation, claim, and release;
- monotonic time;
- bounded SPI and I2C/TWI transfers;
- GPIO access;
- microsecond-scale timing needed by device protocols;
- logging sink;
- console transport.

### 3. Port managed buses

Map the generic endpoint configuration onto the platform. Verify:

- every SPI mode combination used by modules;
- clock-rate switching;
- chip-select setup/hold/high timing;
- timeout and error cleanup;
- shared-route exclusion;
- behaviour from each execution context.

### 4. Port module routing

Move board pin, address, and route definitions into the platform or board configuration. Avoid scattering native pin numbers through generic modules.

### 5. Port optional services

Networking, embedded web assets, persistent storage, and bootloader access should remain optional. A headless controller with only a serial console must still be able to use the module and bus architecture.

### 6. Add host tests

Build portable portions on a desktop with fake buses and clocks. Test:

- transaction cleanup after injected failures;
- configuration switching;
- chain-frame composition;
- module startup rollback and degraded startup;
- timeouts and deadline logic;
- command parsing;
- supervisor state transitions.

## C language boundary

The project currently uses C11 for firmware sources. Keep portable interfaces compatible with freestanding or embedded C environments where practical.

Guidelines:

- use fixed-width integer types;
- make units explicit;
- avoid compiler-specific packed layouts for wire protocols when byte serialisation is clearer;
- avoid relying on host endianness;
- avoid unbounded recursion;
- prefer fixed-capacity storage for real-time paths;
- make thread-safety assumptions explicit;
- keep C++ optional rather than required by the core.

## Build-system boundary

The current `CMakeLists.txt` is a reference-platform build and includes SDK discovery, platform libraries, pin definitions, and image generation.

A more portable repository layout would separate:

```text
core library target
platform adapter target
board configuration target
application target
host test target
```

Each platform can then provide its own top-level build while sharing the core sources.

## Networking boundary

Do not make network availability a prerequisite for Hub operation. The core should expose commands, telemetry, and status through transport-independent interfaces.

Possible transports include:

- UART;
- USB serial;
- TCP console;
- HTTP or WebSocket;
- CAN gateway;
- another host processor;
- no interactive transport in a production appliance.

Authentication and authorisation belong above the transport parser and must not be assumed from a trusted development network.

## Definition of a successful port

A new platform port is successful when:

- the same generic drivers compile without protocol changes;
- module code changes only for board configuration or genuinely different hardware;
- managed-bus contention tests pass;
- safe-state behaviour is preserved;
- console diagnostics remain available after partial startup failure;
- the application can be replaced without modifying core bus or driver code.
