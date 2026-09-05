# Generic system supervisor design

## Purpose

The managed buses and module registry make individual hardware operations reliable. They do not decide whether several valid operations are safe or sensible when combined.

A supervisor is an optional higher-level component for systems that require coordinated startup, interlocks, sequencing, fault latching, or one authoritative owner for actuators.

This document defines a reusable pattern rather than an application-specific state machine.

## Architectural rule: one actuator owner

When a supervisor is active, it should be the only component that directly commands normal actuator state.

```text
CLI / network / application request
              -> command queue
              -> supervisor
              -> subsystem services
              -> modules
              -> drivers and managed buses
```

Other components may read state and submit requests. They should not independently overwrite outputs controlled by the supervisor.

## Service mode

Low-level commands remain essential during hardware development. Provide an explicit service mode with rules such as:

- automatic operation is stopped;
- the system first enters a defined inactive state;
- one authenticated or local operator owns service control;
- hazardous functions may require a physical interlock or additional enable;
- raw writes mark affected module state as unknown;
- leaving service mode reinitialises or verifies affected modules.

Do not let service commands quietly coexist with automatic control.

## Generic states

A useful starting set is:

```text
BOOT
SAFE_IDLE
PRECHECK
STARTING
ACTIVE
STOPPING
FAULT_LATCHED
ESTOP_LATCHED
SERVICE
```

Projects may add process-specific states without changing the framework.

### `BOOT`

- initialise platform services and modules;
- command outputs inactive as early as possible;
- collect module readiness and reset reasons;
- do not automatically energise the system.

### `SAFE_IDLE`

- outputs are in the defined inactive state;
- telemetry and diagnostics remain available;
- start requests may be accepted;
- latched faults must be cleared according to policy before leaving.

### `PRECHECK`

- capture a fresh input snapshot;
- verify mandatory modules and capabilities;
- verify interlocks, supply conditions, communication health, and configured limits;
- reject start without altering normal outputs when any condition is unsatisfied.

### `STARTING`

- perform the ordered startup sequence;
- confirm each commanded state where feedback exists;
- apply step deadlines;
- abort to stopping or fault on failure.

### `ACTIVE`

- run normal control loops;
- arbitrate output requests;
- monitor freshness, limits, and faults;
- accept controlled stop requests.

### `STOPPING`

- apply an ordered normal shutdown;
- remove stored or hazardous energy where possible;
- verify inactive state;
- proceed to `SAFE_IDLE` or `FAULT_LATCHED` depending on the initiating cause and verification result.

### `FAULT_LATCHED`

- request safe state;
- prevent automatic restart;
- preserve fault cause and relevant snapshot;
- require an explicit clear action after the underlying condition is acceptable.

### `ESTOP_LATCHED`

- represent a hardware or software emergency-stop condition;
- use the fastest authoritative shutdown paths;
- do not rely solely on communication with output devices;
- require the project-defined physical and software reset procedure.

### `SERVICE`

- disable automatic output ownership;
- permit bounded diagnostic control according to the service policy;
- record that the system has been manually altered.

## Input snapshot and freshness

Supervisory decisions should use one coherent snapshot rather than reading values opportunistically during each rule.

A generic snapshot may contain:

```c
typedef struct {
    uint64_t captured_at_ms;
    bool module_ready[MAX_MODULES];
    bool interlock_closed[MAX_INTERLOCKS];
    measurement_t measurements[MAX_MEASUREMENTS];
    output_feedback_t feedback[MAX_OUTPUTS];
    uint32_t live_faults;
    uint32_t latched_faults;
} system_snapshot_t;
```

Each measurement should distinguish:

- valid value;
- stale value;
- unavailable capability;
- disconnected sensor;
- transport error;
- device-reported fault.

Do not convert all of these states into zero.

Define freshness limits per signal class. A slowly changing environmental sensor and a fast actuator-feedback signal may have different acceptable ages.

## Fault model

A fault record should preserve enough information to explain what happened:

```c
typedef struct {
    fault_code_t code;
    fault_severity_t severity;
    uint64_t first_seen_ms;
    uint64_t last_seen_ms;
    uint32_t occurrence_count;
    int source_module;
    int source_channel;
    int detail;
} fault_record_t;
```

Suggested severity classes:

- **advisory** — operation may continue; report degraded capability;
- **controlled stop** — leave active operation through the normal stop sequence;
- **immediate safe state** — command authoritative shutdown immediately;
- **emergency stop** — invoke independent emergency mechanisms and latch restart prevention.

The same low-level error may have different severity in different states. A missing optional sensor in `SAFE_IDLE` may be advisory; loss of required feedback in `ACTIVE` may require an immediate safe state.

## Output arbitration

A supervisor should calculate desired outputs and then apply them through one output stage.

```text
read snapshot
process commands
update state
calculate requested outputs
apply limits and interlocks
issue module commands
read or infer actual outputs
publish status
```

Represent requested and actual state separately. A successful command submission is not proof that the physical output changed.

An output request structure may include:

```c
typedef struct {
    bool enable;
    int32_t setpoint;
    uint32_t reason;
    uint64_t valid_until_ms;
} output_request_t;
```

The arbitration layer can then apply:

- state-based permission;
- interlocks;
- rate and range limits;
- mutual exclusion;
- priority;
- timeout to inactive state;
- fallback when feedback is unavailable.

## Scheduling

Use separate cadences for different work:

- fast actuator or deadline service;
- regular sensor acquisition;
- supervisory state update;
- slow diagnostics and logging.

Avoid performing long conversions or network formatting inside a timer interrupt. Timer callbacks should usually set flags or enqueue work.

Every timed state or startup step needs an absolute deadline. Do not depend on counting loop iterations.

## Command API

External commands should become validated request objects:

```c
typedef enum {
    SYSTEM_CMD_NONE,
    SYSTEM_CMD_START,
    SYSTEM_CMD_STOP,
    SYSTEM_CMD_CLEAR_FAULT,
    SYSTEM_CMD_ENTER_SERVICE,
    SYSTEM_CMD_EXIT_SERVICE,
    SYSTEM_CMD_ESTOP
} system_command_kind_t;

typedef struct {
    system_command_kind_t kind;
    uint32_t source;
    uint32_t sequence;
    uint64_t submitted_at_ms;
} system_command_t;
```

The supervisor should publish acceptance or rejection with a reason. A transport-level `OK` only means the request was parsed, not necessarily accepted by system policy.

## Safe-state operation

Define one reusable safe-state operation that:

1. requests authoritative shutdown from every output-capable module;
2. attempts independent shutdown paths even after one error;
3. records requested, confirmed, and unknown output states separately;
4. verifies feedback where available;
5. captures failures without blocking later shutdown actions;
6. can be called from startup, normal stop, fault handling, service entry, and emergency handling.

Software cannot guarantee safety beyond the hardware mechanisms available. Critical outputs require hardware design that fails inactive without depending on normal firmware execution.

## Implementation sequence

### Stage 1 — command boundary

- introduce a command queue;
- route CLI and network requests through it;
- keep existing manual behaviour through one execution context;
- add request IDs and acknowledgements.

### Stage 2 — authoritative snapshot

- collect module readiness, measurements, and output feedback;
- track validity and freshness;
- publish one system status structure.

### Stage 3 — safe state and faults

- centralise safe-state requests;
- add fault records and severity;
- latch fault causes and reset reasons.

### Stage 4 — state machine

- add `BOOT`, `SAFE_IDLE`, `PRECHECK`, `STARTING`, `ACTIVE`, `STOPPING`, and fault states;
- keep application-specific steps in configuration or an application layer.

### Stage 5 — service mode and hardening

- formalise service ownership;
- add authentication where remote access exists;
- add watchdog and brownout policy;
- add fault-injection and transition tests.

## Required project decisions

Before a supervisor controls real hardware, define:

- mandatory modules and capabilities;
- physical emergency-stop mechanisms;
- inactive state for every output;
- startup and shutdown ordering;
- feedback required to confirm an output;
- stale-data limits;
- fault severity and clearing policy;
- behaviour after reset or watchdog restart;
- service-mode authority;
- remote authentication and authorisation;
- whether automatic restart is ever permitted.
