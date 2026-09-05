# Zap Hub architecture

## Purpose

Zap Hub is infrastructure for modular embedded controllers. It allows independently designed ZapLink modules to share processing, communication, diagnostic, and physical bus resources without each module inventing its own system architecture.

The architecture separates five concerns:

1. platform services;
2. managed hardware resources;
3. reusable chip drivers;
4. board-level modules;
5. optional subsystem and application policy.

This separation is what allows the same Hub concepts to be reused across different products, experiments, machines, and microcontroller families.

## Layer model

```text
+-------------------------------------------------------------+
| Application policy, automation, UI and remote integration   |
+-------------------------------------------------------------+
| Optional supervisor: states, interlocks, faults, arbitration|
+-------------------------------------------------------------+
| Reusable subsystem services                                 |
+-------------------------------------------------------------+
| ZapLink modules: board topology and module behaviour        |
+-------------------------------------------------------------+
| Chip drivers: silicon protocol and register behaviour       |
+-------------------------------------------------------------+
| Managed buses, GPIO routing, timing, logging and platform   |
+-------------------------------------------------------------+
```

### Platform adaptation

The platform layer supplies the operations that cannot be portable by themselves:

- peripheral initialisation;
- SPI and I2C/TWI transfers;
- GPIO configuration and access;
- critical sections, mutexes, or scheduler primitives;
- monotonic time and delays;
- console and log transports;
- optional network services;
- reset and bootloader operations.

The current reference port still exposes some native SDK types in core headers. Removing those types from portable interfaces is a planned refactor, described in [`PORTABILITY.md`](PORTABILITY.md).

### Managed resources

Managed resources turn low-level peripherals into shareable system services.

The bus layer owns:

- transaction locking;
- device-specific bus configuration;
- chip-select routing;
- transfer timeouts;
- chain-frame composition;
- enable-line control associated with a device endpoint.

Drivers should not directly manipulate a shared bus or external decoder when an equivalent managed operation exists.

### Chip drivers

A chip driver represents one silicon device family. It should know:

- commands and register layout;
- transfer framing;
- conversion equations;
- status and error interpretation;
- harmless idle frames for chain operation;
- chip-level reset or shutdown operations.

It should not know:

- connector names;
- application channel numbering;
- which other chips are fitted on the board;
- product-specific safe sequences;
- user-interface policy.

### ZapLink modules

A module represents a physical user-designed board or a separately controlled hardware assembly. It owns:

- instances of its chip drivers;
- bus endpoints and serial-chain layout;
- board addresses and chip-select identifiers;
- enable, reset, power-good, and interrupt routing;
- physical-to-logical channel mapping;
- startup validation;
- module-level safe state;
- health polling and diagnostics.

A module may contain one chip, several unrelated chips, or a chain of devices. The module is the correct place to express those board-specific relationships.

### Subsystem services

A subsystem combines one or more module capabilities into a reusable function. Examples include a motion service using several motor channels, a measurement service aggregating sensors, or a power service coordinating multiple outputs.

Subsystems should depend on module interfaces, not on low-level bus operations. They should remain reusable where possible and avoid embedding one product's operating sequence.

### Supervisor and application

The optional supervisor owns coordinated behaviour:

- operating states;
- preconditions and interlocks;
- output arbitration;
- fault severity and latching;
- startup and shutdown sequences;
- command acceptance rules.

Simple systems may not require this layer. Systems with hazardous, high-energy, timing-sensitive, or mutually dependent outputs usually do.

## Ownership rules

### One owner for each mutable resource

Every actuator, shared bus, routing decoder, and long-running process should have one authoritative owner. Other components request operations through that owner rather than modifying the resource directly.

### Drivers do not own boards

The same driver should be usable on two boards with different connector maps, addresses, and enable wiring.

### Modules do not own application policy

A module may implement `enable`, `set`, `measure`, `stop`, and `status`. It should not decide when the complete machine is allowed to run unless the rule is intrinsic to that board's electrical safety.

### Diagnostics use the same managed paths

Raw diagnostic access is valuable, but it must still use bus ownership and routing rules. A debug command is not an exemption from contention control.

## Startup model

The current reference flow is:

```text
platform startup
    -> Hub base services
    -> managed buses and routing
    -> module registry initialisation
    -> optional subsystem initialisation
    -> console/network service
    -> periodic module and subsystem polling
```

The module registry supports two policies:

- **strict startup** — stop on the first failure and roll back modules already initialised;
- **diagnostic startup** — attempt every module, retain successful modules, and report the first error after the pass.

Diagnostic startup is useful during development and with optional hardware. Production applications may choose strict startup or apply their own acceptance policy in a supervisor.

## Runtime model

The reference implementation combines:

- a foreground loop;
- periodic timer events;
- background network activity;
- console callbacks.

The managed bus layer protects individual hardware transactions against interleaving. That does not automatically make higher-level state changes atomic. A command that performs several module operations may still require application-level serialisation.

The intended final model is:

```text
external command sources
        -> command queue
        -> one control context
        -> supervisor/subsystem/module requests
        -> managed hardware transactions
```

Until that command queue exists, modules and applications must be conservative about multi-step operations invoked from different contexts.

## Failure model

Failures should be represented at the layer that can interpret them:

- transfer timeout or framing failure — managed bus;
- invalid device identity or register fault — driver;
- missing board component or power-good failure — module;
- stale sensor data or unavailable capability — subsystem;
- unsafe combination of otherwise valid conditions — supervisor.

Do not collapse every failure into a generic boolean too early. Preserve useful error codes and context through the layers, while ensuring that public APIs remain stable.

## Safe-state model

Each output-capable module should define:

- the state it enters before probing hardware;
- the state it enters after successful initialisation;
- the authoritative shutdown mechanism;
- any secondary shutdown mechanism;
- what happens when communication fails during shutdown;
- whether deinitialisation is safe and idempotent.

A system-wide safe-state command may call each module's shutdown operation, but the module remains responsible for its own electrical sequence.

## Static and dynamic configuration

The current module registry is compile-time static. This is suitable when the controller hardware and attached modules are known at build time.

Future projects may require:

- board-identification EEPROMs;
- runtime module discovery;
- generated registry tables;
- device-tree-style configuration;
- optional module packages;
- persistent per-module settings.

These can be added without changing the core ownership rules.
