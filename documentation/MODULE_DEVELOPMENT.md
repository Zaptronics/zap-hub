# Developing ZapLink modules

## Module responsibility

A ZapLink module is the firmware representation of one user-designed hardware module. It is the boundary between generic chip support and a board's physical design.

A module should answer questions such as:

- Which devices are fitted?
- Which managed buses and routes do they use?
- How are logical channels mapped to physical pins or registers?
- Which components are optional?
- How is the board enabled, reset, or powered down?
- What is the safe inactive state?
- Which faults should be polled?
- What diagnostic operations are useful during bring-up?

It should not define the complete product workflow.

## Module instance

Keep mutable module state in one owning `.c` file or in an explicitly allocated instance. Headers should declare types and public functions, not define shared storage.

A typical instance contains:

```c
typedef struct {
    bool ready;
    bool output_enabled;
    bus_spi_t spi_endpoint;
    bus_i2c_t control_device;
    some_driver_t primary;
    other_driver_t secondary;
    uint32_t fault_flags;
} example_module_t;
```

Use explicit instance pointers in module APIs where practical. Avoid hidden coupling through unrelated globals.

## Registry contract

The current registry uses `module_entry_t`:

```c
typedef struct module_entry {
    const char         *short_name;
    const char         *long_name;
    module_init_fn_t    init;
    void               *bus_ctx;
    void               *mod_inst;
    module_poll_fn_t    poll;
    module_deinit_fn_t  deinit;
    int8_t              addr;
    int8_t              en_pin;
} module_entry_t;
```

The fields provide:

- stable diagnostic names;
- the initialisation entry point;
- one module-owned instance;
- platform or bus context used by the current registry;
- optional polling and deinitialisation;
- compact routing parameters for the current reference port.

The exact descriptor is expected to evolve as portability improves. Module code should not depend on registry internals beyond the public contract.

## Initialisation

A good module initialiser follows this order:

1. validate pointers and configuration;
2. clear software state;
3. force hardware-control pins to an inactive state where possible;
4. initialise managed bus endpoints;
5. enable or power the module only when required;
6. wait the documented startup interval;
7. identify devices and validate critical configuration;
8. configure every device into an inactive operating state;
9. clear stale status where appropriate;
10. mark the instance ready only after all mandatory checks pass.

Optional components may fail without failing the complete module, provided the module can clearly report reduced capability.

Never energise an output merely to discover whether a device exists unless the hardware design requires it and the operation is explicitly controlled.

## Strict and degraded operation

Classify components as:

- **mandatory** — the module cannot provide its defined service without them;
- **optional** — absence removes a feature but leaves the module useful;
- **diagnostic-only** — used for telemetry or bring-up but not normal control;
- **redundant** — one of several components may satisfy the requirement.

Expose capability state rather than pretending every logical channel is available.

For example, a combined I/O module may keep digital outputs operational even when an optional current-monitoring device is absent. Commands for unavailable measurements should return a precise unavailable status, not fabricated zero values.

## Safe state

Document and implement the module's safe state.

Prefer an authoritative hardware mechanism such as:

- enable low;
- gate disable;
- output-enable inactive;
- relay open;
- driver Hi-Z;
- power switch off.

A register write that commands zero may be useful as a secondary state, but communication failure can prevent it. Apply the authoritative mechanism first during emergency shutdown unless the hardware requires another order.

The module shutdown function should:

- attempt all independent safe actions even when one fails;
- preserve the first or most useful error;
- update software state to reflect what is known, not what was merely requested;
- be safe to call more than once;
- avoid depending on the application still running normally.

## Polling

`poll()` is for short, nonblocking health work such as:

- reading latched fault status;
- checking power-good or interrupt sources;
- detecting unexpected reset;
- updating lightweight cached telemetry;
- logging newly observed faults.

Polling should not:

- contain long delays;
- perform a complete application sequence;
- monopolise a shared bus;
- repeatedly log an unchanged persistent condition;
- silently clear a fault before higher layers can observe it.

Use edge detection or remembered status to avoid log flooding.

## Public module API

A module API should use logical operations and board-level channel numbers. Examples:

```c
int example_module_set_output(example_module_t *module,
                              unsigned channel,
                              uint16_t value);

int example_module_read_input(example_module_t *module,
                              unsigned channel,
                              int32_t *value);

int example_module_stop_all(example_module_t *module);

int example_module_get_status(const example_module_t *module,
                              example_module_status_t *status);
```

Return errors for:

- invalid arguments;
- module not ready;
- unsupported or unavailable capability;
- bus failure;
- device fault;
- unsafe request rejected by module-intrinsic constraints.

Do not require application code to know chip register addresses.

## Driver use

Instantiate generic drivers inside the module. Configure them with managed endpoints and board-specific constants.

```text
generic driver: command/register protocol
module: address, route, channel map, fitted options
application: desired system behaviour
```

If a board requires a workaround for a prototype assembly error, keep that workaround in the module with a clear comment and configuration constant. Do not contaminate the generic driver.

## Commands and diagnostics

Module commands should be grouped under the module's short name or a stable capability name.

Provide, where useful:

- `info` — identity, readiness, capabilities, and configuration;
- `status` — live and latched faults;
- `probe` — non-destructive discovery;
- `verify` — register or wiring checks;
- `read` — normal measurements;
- `set` — bounded normal control;
- `stop` or `off` — explicit safe action;
- raw register access — clearly marked service-only.

Raw writes should warn that software state may no longer match hardware and should recommend reinitialising the module afterward.

## Registration checklist

Before adding a module to the registry:

- [ ] one `.c` file owns each module instance;
- [ ] all shared bus access uses the managed layer;
- [ ] SPI mode and rate are explicit;
- [ ] I2C/TWI timeout is explicit;
- [ ] startup begins inactive;
- [ ] mandatory and optional devices are identified;
- [ ] partial capability is reported accurately;
- [ ] output ranges are validated;
- [ ] shutdown is idempotent;
- [ ] polling is bounded;
- [ ] errors include useful context;
- [ ] the console can diagnose failure without energising outputs;
- [ ] application-specific sequencing remains outside the module.
