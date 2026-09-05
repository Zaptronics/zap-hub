# Developing reusable chip drivers

## Scope

A Zap Hub chip driver implements one silicon protocol. Its goal is reuse across different ZapLink modules and platform ports.

The driver should be testable without knowing the final connector names, product purpose, or application state machine.

## Driver responsibilities

A driver may own:

- register and command definitions;
- device identity checks;
- serialisation and byte order;
- conversion between raw codes and engineering values;
- chip-level configuration structures;
- status and fault decoding;
- chip-level reset, shutdown, or calibration commands;
- a harmless idle frame for serial-chain use.

A driver should not own:

- external module-enable policy;
- board connector maps;
- logical product channel numbering;
- relationships between unrelated chips;
- system-wide safety policy;
- CLI formatting;
- network transport.

## Bus abstraction

Prefer Zap Hub managed endpoints over native peripheral handles.

For I2C/TWI devices, store or receive a `bus_i2c_t` endpoint.

For SPI devices that may be direct or chained, use `bus_spi_target_t`. The driver should send and receive only its native frame.

This is the primary mechanism that keeps a driver portable across board topologies.

## No assumed global configuration

Never assume that SPI mode, speed, or chip-select state remains from the previous call. The endpoint must describe the required configuration, and every operation must use the managed API.

Likewise, do not change an I2C/TWI controller's global rate directly from a driver.

## State and instances

Use explicit driver instances:

```c
typedef struct {
    bus_spi_target_t target;
    uint8_t revision;
    bool configured;
} example_driver_t;
```

Avoid file-level mutable state unless the chip is truly a singleton imposed by the platform. Multiple modules may use the same chip family with different addresses and configurations.

## Error handling

Driver APIs should distinguish at least:

- invalid argument;
- transport failure;
- unexpected device identity;
- invalid or unsupported device state;
- timeout;
- conversion or calibration failure.

Do not log routine transport errors inside a low-level transfer while a bus lock is held. Return the error so the module can add board and channel context.

For complex devices, retain diagnostic details in the instance so a later `status` or `diag` call can explain the failed stage.

## Deterministic behaviour

Embedded driver operations should have bounded execution time.

- Use explicit timeouts for ready polling.
- Avoid unbounded retry loops.
- Avoid dynamic allocation in normal control paths.
- Avoid long sleeps while owning a shared bus.
- Validate transfer lengths against fixed buffers.
- Clean up temporary device state on every exit path.

## Read-modify-write operations

Protect read-modify-write sequences at the correct level. Two separate managed transactions are not atomic as a pair unless the module or driver provides additional ownership.

Where possible:

- use device set/clear registers;
- cache configuration under one owner;
- provide a driver-level operation that performs the complete sequence;
- document any concurrency assumptions.

## Serial-chain idle frames

A chain-capable driver must provide a persistent frame that is harmless when clocked through an untargeted device.

The idle frame must:

- have the exact native frame width;
- not change output state;
- not clear important status unintentionally;
- be valid in every normal device state;
- remain available for the lifetime of the chain descriptor.

Do not use all-zero or all-one bytes without verifying the device protocol.

## Conversion code

Keep mathematical conversions separate from bus access where practical. Pure conversion functions are easier to unit test on a host.

Examples include:

- ADC code to voltage;
- resistance to temperature;
- raw tach period to speed;
- register field packing;
- fixed-point scaling.

Document units in function names, parameter names, or comments. Prefer explicit suffixes such as `_mv`, `_ua`, `_hz`, or `_us`.

## Driver test checklist

- [ ] multiple independent instances work;
- [ ] all public arguments are validated;
- [ ] transport failures are propagated;
- [ ] timeout paths leave the device and bus usable;
- [ ] byte order is tested;
- [ ] boundary register values are tested;
- [ ] conversion functions have host-side vectors;
- [ ] direct SPI operation is tested;
- [ ] chain-slot operation is tested when supported;
- [ ] idle frames are verified harmless;
- [ ] no board-specific mapping appears in the driver;
- [ ] no platform SDK call bypasses the adaptation boundary without justification.
