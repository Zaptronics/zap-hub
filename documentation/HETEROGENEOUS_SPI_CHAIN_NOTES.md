# Heterogeneous SPI Chain Refactor

This pass separates a chip driver from the physical serial chain in which the chip is installed.

## Ownership model

- `ztmb/bus` owns physical SPI selection, locking, frame composition and wire ordering.
- A chip driver owns its command protocol, native frame width and harmless idle/NOP frame.
- A module owns the list of silicon devices, their physical order and whether that proposed combination is actually compatible.

The bus deliberately does not contain a table of approved chip combinations. A module designer may construct any chain that is valid for the selected hardware.

## Driver-facing target

A chain-capable driver receives a `bus_spi_target_t`. The target may represent either:

- a device with its own chip-select line; or
- one slot in a heterogeneous serial chain.

The driver uses the same framed calls in both cases:

```c
bus_spi_target_begin(target);
bus_spi_target_frame(target, command_frame, response_frame);
bus_spi_target_frame(target, argument_frame, response_frame);
bus_spi_target_end(target);
```

Every `bus_spi_target_frame()` is one complete CS-low frame. The target retains the shared SPI-fabric lock across the sequence, allowing devices such as the L6470 to pulse CS between command bytes without another SPI user being inserted halfway through the command.

## Chain slots

Each driver exposes a slot descriptor:

```c
typedef struct {
    size_t frame_bytes;
    const uint8_t *idle_tx;
} bus_spi_chain_slot_config_t;
```

`frame_bytes` may differ between slots. `idle_tx` is a persistent frame that is safe to send when another device in the chain is targeted.

Slots are added in physical order, with logical slot 0 closest to the Pico. The bus calculates wire offsets and transmits the farthest slot first.

Unspecified slots are automatically filled with their own idle frame. A module can also supply several `bus_spi_chain_transfer_t` entries in the same call to command several different devices in one physical frame.

Example shape:

```c
bus_spi_chain_init(&module->chain, &module->spi_bus);
bus_spi_chain_add_slot(&module->chain, &module->motor_slot,
                       &l6470_chain_slot_config);
bus_spi_chain_add_slot(&module->chain, &module->dac_slot,
                       &some_dac_chain_slot_config);
bus_spi_chain_finalize(&module->chain);

bus_spi_target_init_chain_slot(&module->motor_target, &module->motor_slot);
bus_spi_target_init_chain_slot(&module->dac_target, &module->dac_slot);

l6470_init(&module->motor, &module->motor_target);
some_dac_init(&module->dac, &module->dac_target);
```

Whether those two particular devices can share clock mode, CS framing and serial pass-through is the module designer's responsibility.

## L6470 and stepper module

`drivers/l6470` now represents one L6470 only. It no longer owns:

- a chain count;
- a chip-select line;
- device indexing;
- NOP padding for neighbouring devices; or
- four-device board initialization.

The driver exports:

- its recommended SPI configuration;
- its one-byte NOP slot descriptor;
- normal one-chip commands; and
- RUN/MOVE frame encoders for synchronized module-level commands.

`modules/ztm_stepper` now owns:

- the physical SPI port and decoder selection;
- four chain slots in nearest-to-farthest order;
- four generic SPI targets;
- four independent `l6470_t` driver objects;
- broadcast reset and stop; and
- synchronized start-all frames.

A single-motor RUN command now produces four chain frames under one shared lock. The selected L6470 receives its command/argument byte in each frame, while the other three receive NOP.

## Current structural limits

The implementation is statically allocated for embedded use:

- up to `BUS_SPI_CHAIN_MAX_SLOTS` slots, currently 16;
- up to `BUS_SPI_CHAIN_MAX_FRAME_BYTES` total bytes per physical frame, currently 128;
- byte-aligned native slot frames.

The constants can be increased without changing the API. Supporting non-byte-aligned slot widths would require a bitstream packer and is deliberately deferred until a real module needs it.

## Validation performed

Host-side tests cover:

- a mixed one-byte/two-byte chain;
- automatic idle-frame insertion;
- farthest-first wire ordering;
- logical response extraction;
- commanding multiple heterogeneous slots in one frame;
- one L6470 RUN sequence across four slots;
- one lock acquisition across the complete multi-frame L6470 command; and
- module-level broadcast reset and synchronized start-all operations.

A Pico SDK build and hardware-chain test are still required on the target project.
