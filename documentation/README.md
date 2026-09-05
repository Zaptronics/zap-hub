# Zap Hub documentation

This directory documents Zap Hub as a generic modular embedded systems controller.

Start with:

1. [`ARCHITECTURE.md`](ARCHITECTURE.md)
2. [`MANAGED_BUSES.md`](MANAGED_BUSES.md)
3. [`MODULE_DEVELOPMENT.md`](MODULE_DEVELOPMENT.md)
4. [`PORTABILITY.md`](PORTABILITY.md)

Supporting documents:

- [`DRIVER_DEVELOPMENT.md`](DRIVER_DEVELOPMENT.md) — reusable chip-driver rules
- [`HETEROGENEOUS_SPI_CHAINS.md`](HETEROGENEOUS_SPI_CHAINS.md) — mixed-device serial chains
- [`CONSOLE_AND_DIAGNOSTICS.md`](CONSOLE_AND_DIAGNOSTICS.md) — service and bring-up interface
- [`SUPERVISOR_DESIGN.md`](SUPERVISOR_DESIGN.md) — optional coordinated-control framework
- [`FIRMWARE_VERSIONING.md`](FIRMWARE_VERSIONING.md) — release numbering
- [`ZAPEE_LIBRARY.md`](ZAPEE_LIBRARY.md) — reusable CMake/Zephyr library packaging and migration

Board-specific bring-up records and application-specific operating procedures are deliberately excluded from the main documentation set. They should live with the relevant hardware design or application repository rather than defining the Hub architecture.
