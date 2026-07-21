# Managed SPI and I2C/TWI Bus Pass

This pass turns `ztmb/bus` into the single hardware transaction layer for the Hub's active SPI and I2C/TWI drivers.

## SPI architecture

Each `bus_spi_t` now owns the configuration required by its device:

- SPI controller (`spi0` or `spi1`)
- decoded chip-select line
- optional module-enable line
- baud rate
- data width
- clock polarity and phase
- bit order
- chip-select setup, hold and minimum-high delays

The physical SPI controllers are initialized to a neutral 1 MHz, mode-0 state during Hub startup. The managed bus applies the device configuration immediately before a transaction and caches the last configuration applied to each SPI controller.

Both RP2350 SPI controllers share the same external chip-select decoder. For that reason they share one critical section: only one decoded SPI device can be selected at a time, even when the two devices are connected to different RP2350 SPI peripherals.

The critical section is IRQ- and multicore-safe, which is important because the current Wi-Fi raw TCP callbacks can dispatch commands from the SDK background context. It is intentionally held only for the short hardware transaction. Drivers must not log, sleep, allocate memory, or perform unrelated work between `bus_spi_begin()` and `bus_spi_end()`.

### Explicit transactions

Use the explicit transaction API when a device requires chip select to remain asserted across multiple phases:

```c
if (!bus_spi_begin(&dev->spi_bus)) return false;
bool ok = bus_spi_transfer(&dev->spi_bus, &command, NULL, 1);
if (ok) ok = bus_spi_transfer(&dev->spi_bus, NULL, data, data_len);
bool ended = bus_spi_end(&dev->spi_bus);
return ok && ended;
```

Every successful `bus_spi_begin()` must be paired with `bus_spi_end()` on the same core. The transaction API records the owning core and rejects transfer/end calls from another core.

### TLA2518 mode changes

The TLA2518 can change its SPI mode through its own configuration register. The driver writes the new device mode and stages the Pico-side mode for the next transaction before releasing the shared SPI critical section. A soft reset similarly stages mode 0 because the TLA2518 returns to SPI-00 after reset.

## Heterogeneous SPI chains

The SPI layer now also supports module-owned serial chains containing one or more potentially different chip drivers. Each slot supplies its native byte width and harmless idle/NOP frame. The chain composes the full farthest-first wire frame, inserts idle frames for untargeted slots, extracts each slot's response, and can hold the SPI fabric across several CS-pulsed frames.

Chip compatibility is intentionally a module-design decision rather than a bus policy. See `HETEROGENEOUS_SPI_CHAIN_NOTES.md` for the API and the L6470/stepper refactor.

## I2C/TWI architecture

Each `bus_i2c_t` now owns:

- I2C controller (`i2c0` or `i2c1`)
- 7-bit address
- optional module-enable line
- baud rate
- per-SDK-call timeout

I2C0 and I2C1 have separate critical sections, so independent controllers do not unnecessarily block one another. The requested baud rate is applied lazily and cached per controller.

Register writes are sent as one coherent frame:

```text
START + address(W) + register + payload... + STOP
```

Register reads use a repeated start:

```text
START + address(W) + register + RESTART + address(R) + data... + STOP
```

The managed write helper currently supports a maximum frame size of 64 bytes including the register byte. Current Hub devices use much smaller transfers.

## Current device configurations

| Device | Bus | Configuration |
|---|---|---|
| Four-slot L6470 stepper chain | SPI0 | 1 MHz, mode 3, MSB first |
| Four independent MCP4901 TEC DACs | SPI0 | 1 MHz, mode 0, MSB first; separate CS lines |
| TLA2518 onboard ADC | SPI1 | 1 MHz, initial mode 0, MSB first |
| AD7124 module ADC | SPI1 | 1 MHz, mode 3, MSB first |
| PCAL6416A | I2C1 | 400 kHz, 5 ms timeout |
| SX1509 | I2C | 400 kHz, 5 ms timeout |
| EMC2303 | I2C | 400 kHz, 5 ms timeout |
| AD5282 HV control | I2C | 400 kHz, 5 ms timeout |

The PAC195x driver uses the managed I2C calls once its `bus_i2c_t` has been initialized by its future module layer. Its previous bool-to-status-code inversion has also been corrected.

## Driver conversions

The active CMake build now routes direct SPI and I2C transfers through `ztmb/bus.c`. The legacy standalone `drivers/ad7124_scan_15pt1000.c` test program is not part of the Hub build and still contains its own direct SPI setup.

## Known limitations and deferred work

- A missing I2C device may keep interrupts masked for up to the configured transfer timeout. The present 5 ms value is deliberately bounded, but normal transactions should complete in a small fraction of that time.
- No automatic stuck-SDA/SCL recovery is implemented yet.
- SPI transfers have no timeout because the Pico SDK blocking SPI calls complete at a known clock rate.
- Several older driver APIs still return `void`; the refactored L6470 path now propagates transaction failures, but other drivers remain to be converted.
- Network commands still execute directly from raw lwIP callbacks. The bus is now safe against IRQ/main-loop transaction interleaving, but a later command-dispatch pass should queue commands into the main loop so higher-level control code also has one execution context.
- `bus_spi_begin()` / `bus_spi_end()` are intentionally non-recursive.

## Suggested hardware smoke test

1. Boot and confirm PCAL6416A initialization succeeds.
2. Read the onboard TLA2518 and confirm it survives soft reset and mode configuration.
3. Initialize/read the AD7124 ID and temperature channels.
4. Read L6470 status from all four chain positions.
5. Write several TEC DAC values and verify output scaling and enable behavior.
6. Exercise the SX1509/EMC2303/I2C modules.
7. Alternate commands rapidly between the TLA2518, AD7124, MCP4901 and L6470 to confirm automatic mode switching on both SPI peripherals.
8. Repeat the alternating test while issuing commands through Wi-Fi and while the one-second module poll is active.
