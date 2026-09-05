# Managed SPI and I2C/TWI buses

## Why buses need system-level management

A peripheral driver often appears to need only a native SPI or I2C handle. In a modular controller that is not enough.

Several devices may share:

- one controller;
- one external chip-select decoder;
- one module-enable rail;
- different bus speeds;
- different SPI clock polarity or phase;
- different chip-select timing;
- multiple execution contexts.

Without a central owner, a transfer can begin with the configuration left behind by another device, select the wrong route, or be interrupted by a second command. Zap Hub treats the complete physical path as the resource, not merely the MCU peripheral instance.

## SPI fabric ownership

A `bus_spi_t` describes one direct SPI endpoint. It contains:

- the underlying controller;
- the chip-select route;
- an optional enable route;
- the endpoint's bus configuration;
- transaction state.

The current reference hardware routes more than one SPI controller through the same external chip-select decoder. For that reason, one lock protects the complete SPI fabric. Two controllers cannot safely select independent routes at the same time when selection is physically shared.

A different platform may use one lock per independent fabric. The portable rule is:

> Lock every physical resource that must remain coherent for the complete transaction.

## Per-device configuration

Each SPI endpoint carries:

- baud rate;
- data width;
- clock polarity;
- clock phase;
- bit order;
- chip-select setup time;
- chip-select hold time;
- minimum chip-select-high time.

The bus layer applies the endpoint configuration while it owns the fabric. A small cache avoids reprogramming the controller when the requested configuration has not changed.

Drivers must not assume that the controller still has the mode used by their previous transfer.

## One-shot SPI frames

Use a one-shot frame when one chip-select assertion encloses the complete operation:

```c
bool ok = bus_spi_xfer(&endpoint, tx, rx, length);
```

The managed operation performs the conceptual sequence:

```text
claim fabric
apply endpoint configuration
apply required CS-high delay
select route
wait CS-setup time
transfer bytes
wait CS-hold time
deselect route
release fabric
```

The exact implementation is platform-specific; the ownership and timing semantics are not.

## Explicit SPI transactions

Some devices require chip select to remain active across several transfers. Use:

```c
bus_spi_begin(&endpoint);
bus_spi_transfer(&endpoint, command, NULL, command_length);
bus_spi_transfer(&endpoint, NULL, response, response_length);
bus_spi_end(&endpoint);
```

Between `begin` and `end`:

- the caller owns the fabric;
- the selected route remains active;
- unrelated work must not be performed;
- logging, sleeping, allocation, and callbacks should be avoided;
- every error path must still call `end` or an equivalent cleanup operation.

Keep explicit transactions short. They protect hardware coherence, not arbitrary application logic.

## SPI claim and framed primitives

Serial-chain support needs to compose a complete wire frame before selecting the chain. The lower-level operations therefore separate ownership from framing:

- `bus_spi_claim()` acquires the fabric without selecting a device;
- `bus_spi_frame_locked()` performs one complete selected frame while ownership is held;
- `bus_spi_release()` returns the fabric.

Module and chain code may use these primitives. Normal chip drivers should prefer the direct or target-level API.

## Driver-facing SPI targets

`bus_spi_target_t` allows one driver to communicate with either:

- a directly selected device; or
- one slot inside a serial chain.

The driver supplies its native frame and receives its native response. The target implementation handles direct selection or chain composition.

This prevents separate "direct" and "daisy-chain" versions of the same chip driver.

## I2C/TWI ownership

A `bus_i2c_t` describes one addressed endpoint with:

- the underlying controller;
- the device address;
- an optional module-enable route;
- bus speed;
- transfer timeout.

The current implementation uses one lock for each independent I2C/TWI controller. Every read, write, and probe:

1. claims the controller;
2. applies the endpoint bus speed if required;
3. performs a bounded transfer;
4. releases the controller.

A timeout is part of the endpoint configuration because a missing, held, or unpowered device must not stall the complete controller indefinitely.

## Enable control

Some ZapLink modules expose a hardware enable, reset, or power-control line through shared GPIO routing. The endpoint can carry that route so module code can use a consistent API.

The bus layer only changes the line on request. It does not invent module-specific startup delays or power sequencing; those remain module responsibilities.

## Contention boundaries

The managed bus layer prevents transfer-level interleaving. It does not by itself protect:

- a multi-command configuration sequence separated by unlocked computation;
- two callers changing module state through different APIs;
- a diagnostic command racing a supervisor decision;
- network and foreground code issuing logically conflicting actuator requests.

Use a module lock, subsystem owner, command queue, or supervisor where the unit of consistency is larger than one bus transaction.

## Error handling

Managed operations should:

- validate endpoint configuration before touching hardware;
- reject nested or mismatched transaction state;
- apply a bounded timeout where the platform supports one;
- always release locks and deselect routes on failure;
- return failure without logging while a low-level lock is held;
- let the caller add device or module context to the error report.

## Rules for new code

1. Do not call native shared-bus functions directly from a module or command handler.
2. Store configuration with the endpoint, not in an assumed global bus state.
3. Lock the complete physical routing fabric.
4. Keep transactions short and deterministic.
5. Use explicit transactions only when chip-select continuity is required.
6. Use a `bus_spi_target_t` for drivers that may appear directly or in a chain.
7. Give every I2C/TWI endpoint a real timeout.
8. Do not perform logging or unrelated work while a bus lock is held.
9. Make cleanup paths idempotent.
10. Test contention from every execution context used by the platform.
