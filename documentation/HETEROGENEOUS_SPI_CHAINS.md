# Heterogeneous SPI chains

## Purpose

Some serial peripherals can be wired as one shift chain under a shared chip-select signal. Every transaction clocks a frame through every device, even when software intends to address only one of them.

Zap Hub supports chains in which slots may represent different driver types or native frame widths. This is called a heterogeneous chain.

The bus layer provides frame composition and ownership. The module designer remains responsible for determining that the selected devices are electrically and protocol compatible in one chain.

## Ownership model

A chain belongs to one module because the module owns the physical topology.

```text
module
  -> managed SPI endpoint and route
  -> chain descriptor
  -> ordered slot descriptors
  -> chip-driver instances bound to slots
```

A generic chip driver does not decide which slot it occupies or which devices are adjacent to it.

## Slot descriptor

Each `bus_spi_chain_slot_config_t` defines:

- `frame_bytes` — the number of bytes the device consumes in every chain frame;
- `idle_tx` — a persistent, harmless native frame to transmit when that slot is not targeted.

The idle frame is supplied by the chip driver because only the driver can determine a command that does not alter device state.

A slot instance also records:

- its logical index;
- its offset in the composed wire frame;
- its owning chain;
- whether registration completed successfully.

## Physical and wire order

Slots are registered in physical order, nearest to the controller first.

Serial data for the farthest device must be shifted onto the wire first. Finalisation calculates each slot's wire offset so module and driver code do not repeat this ordering logic.

```text
Controller -> Slot 0 -> Slot 1 -> Slot 2 -> serial return

Wire transmit order:
    native frame for Slot 2
    native frame for Slot 1
    native frame for Slot 0
```

Receive data is copied back into each slot's native response buffer.

## Chain lifecycle

```text
bus_spi_chain_init()
    -> bus_spi_chain_add_slot() for every physical device
    -> bus_spi_chain_finalize()
    -> begin/frame/end or one-shot xfer
```

A chain cannot be used until finalised. After finalisation, its slot count, frame size, and offsets are treated as stable.

## Addressing one slot

A chain frame starts as the concatenation of every slot's idle frame. The requested slot's native frame then replaces its idle bytes.

Conceptually:

```text
compose idle frame for all slots
replace targeted slot region
claim shared SPI fabric
select chain
clock complete frame
extract targeted response
release fabric
```

Several slots may be replaced in the same frame when their device protocols permit simultaneous operations.

## Driver-facing target

A chain-capable driver receives a `bus_spi_target_t` rather than knowing whether it is directly selected.

For a direct target, `bus_spi_target_frame()` performs one native frame on the endpoint.

For a chain-slot target, the same call inserts that native frame into the complete chain frame and supplies idle frames for all other slots.

This keeps register and command logic inside one reusable driver.

## Multi-frame sequences

Some devices require several consecutive chain frames while ownership remains continuous. Use the target or chain `begin`, `frame`, and `end` sequence.

Do not assume that chip select can remain low across frames unless every device in the chain permits it. A module may need to use several complete selected frames while retaining the fabric lock, rather than one continuously asserted chip select.

## Compatibility questions for module designers

Before combining devices in one chain, verify:

- common voltage levels;
- common SPI mode and bit order;
- acceptable shared clock rate;
- compatible chip-select behaviour;
- known frame width for every device;
- harmless idle command for every slot;
- deterministic response alignment;
- startup state before the first valid frame;
- behaviour after reset, fault, or power loss;
- whether one device can hold or corrupt the serial return path.

The chain compositor cannot make incompatible devices compatible.

## Current structural limits

The reference implementation uses fixed storage limits for deterministic embedded operation:

- maximum number of slots;
- maximum total frame length;
- eight-bit, most-significant-bit-first native frames in the current port;
- one managed SPI configuration for the complete chain.

These are implementation limits, not fundamental Zap Hub requirements. A platform port may select different compile-time bounds, but should retain explicit validation.

## Example

A user-designed motion module could contain three identical motor-control devices and one small configuration DAC in a compatible serial chain. The module would:

1. create the managed SPI endpoint for the shared route;
2. register three motor-driver slots and one DAC slot in physical order;
3. bind each generic driver instance to its slot target;
4. use the module API to expose logical axes and configuration functions;
5. keep all chain composition out of the application layer.

The example is illustrative; compatibility must be verified from the actual device protocols.
