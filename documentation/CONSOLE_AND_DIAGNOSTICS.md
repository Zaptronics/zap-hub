# Console and diagnostics

## Purpose

The Zap Hub console is a service and bring-up interface for the embedded controller. It exists so developers can inspect buses, initialise modules incrementally, exercise one capability at a time, and recover useful information when normal startup is incomplete.

It is not, by itself, the final application control architecture.

## Availability during failure

The reference startup keeps the console available even when base hardware or one or more modules fail. This is intentional.

A modular controller should make it possible to determine:

- which modules were expected;
- which modules initialised;
- where initialisation stopped;
- whether a bus route responds;
- whether the module can be reinitialised;
- whether outputs are inactive;
- which diagnostic data remains trustworthy.

Avoid fatal loops for recoverable peripheral failures when a console can still help diagnose the system.

## Command hierarchy

Use stable, capability-oriented groups:

```text
help
version
module list
module reinit <name>
module deinit <name>
safe
<module-or-capability> info
<module-or-capability> status
<module-or-capability> ...
```

The exact commands vary by project, but naming should distinguish:

- discovery and information;
- normal bounded operation;
- safe shutdown;
- destructive or raw service actions.

## `module list`

The module list should report both the static registry and current initialisation state. An absent or failed module should not disappear from diagnostics merely because normal lookup returns ready modules only.

Useful fields include:

- short name;
- descriptive name;
- expected bus or route;
- ready/failed/not-attempted state;
- last initialisation error;
- optional capability summary.

## Global safe command

A root `safe` command is useful during development. It should request the safe state from every available output-capable module.

The command must not become a substitute for:

- hardware emergency-stop design;
- watchdog-enforced shutdown;
- module-local fault responses;
- a supervisor's fault-latched state;
- verified removal of hazardous energy.

A software safe command is one layer of defence.

Its output should state which actions succeeded, which failed, and which modules were unavailable. Do not print one unconditional success message after a partial failure.

## Diagnostic command classes

### Safe discovery

These commands should not energise outputs:

- bus scan or address probe;
- device identity read;
- module presence check;
- register configuration verification;
- interrupt-line state;
- power-good state where readable while off;
- firmware and hardware revision display.

### Controlled functional tests

These commands exercise a bounded operation:

- set one output to a validated low test value;
- move one logical channel a bounded distance;
- read one sensor;
- toggle one GPIO;
- enable a module for a specified test and then disable it.

The command should reject unsafe ranges and report the actual final state.

### Raw access

Raw register reads are valuable. Raw writes are more dangerous because they bypass module invariants and cached state.

Raw write commands should:

- be clearly labelled service-only;
- still use managed bus ownership;
- validate address and transfer length;
- not silently reinterpret data;
- warn that software state may be stale;
- recommend module reinitialisation before returning to normal control.

## Output and logging

Separate command response from asynchronous logs where possible. The reference network CLI queues output so long commands do not corrupt prompts, but a stronger transport-independent console abstraction is desirable.

Logs should include:

- module or subsystem name;
- logical channel where relevant;
- operation or failed stage;
- error code;
- newly changed fault state.

Avoid repeatedly logging a persistent condition on every poll. Report the transition into the fault and, where useful, the transition out.

## Network command bridge

The current reference port exposes the CLI through a network transport and maps simple HTTP paths into command strings. This is useful for development, but it has important architectural limits:

- authentication and authorisation are not yet part of the core;
- remote commands may execute from a background network context;
- bus transactions are protected, but multi-step control ownership is not automatically serialised;
- translating arbitrary URL paths into CLI commands is not a stable public API.

Production systems should use:

```text
network transport
    -> parser and authentication
    -> validated command object
    -> command queue
    -> one authoritative control context
    -> module or supervisor API
```

## Bring-up workflow

A generic incremental sequence is:

```text
version
safe
module list

inspect base routing and buses
probe one module without enabling outputs
verify device identity and configuration
read passive inputs
exercise one bounded output
stop that output
reinitialise the module
safe
```

Add one module and one capability at a time. Record the expected inactive state before every output test.

## Command implementation rules

1. Validate argument count and range before hardware access.
2. Resolve a module only if it is ready, unless the command is explicitly a failed-init diagnostic.
3. Use public module or driver diagnostic APIs rather than duplicating bus protocol in the parser.
4. Keep command handlers bounded.
5. Do not hold bus locks while formatting output.
6. Report unavailable capability distinctly from a measured zero.
7. Make stop/off commands idempotent.
8. Do not conceal partial failure.
9. Keep application policy out of low-level service commands.
10. Route production control through a queue or supervisor rather than direct callback execution.
