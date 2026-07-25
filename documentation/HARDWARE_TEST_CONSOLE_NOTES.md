# Hardware test console pass

This pass adds low-level console commands intended for incremental bench testing of the four-L6470 stepper module and improves partial-hardware startup behaviour.

## Startup behaviour

- Module initialization no longer has to be all-or-nothing.
- With `modules_init_all(false)`, every configured module gets an independent initialization attempt.
- Successfully initialized modules remain available even if another module fails.
- Each chamber subsystem then initializes independently and reports whether its own dependencies were found.
- A failed stepper initialization disables the stepper module enable line before returning.

## New console commands

### General

```text
module list
safe
```

`module list` shows every configured module and whether initialization succeeded.

`safe` requests the following safe state where the corresponding module is available:

- stop the pump processes, clear their software state, and place all L6470 outputs in hard HiZ;
- disable and shutdown all initialized TEC DAC modules;
- disable the HV module and write voltage code zero;
- set fan PWM to zero.

The MCP4901 write path now propagates managed-SPI failures. The DAC itself remains write-only, so output verification is performed through the corresponding TLA2518 TEC_VSENSE input.

### Stepper chain diagnostics

```text
stepper info
stepper nop [count]
stepper status [1..4|all]
stepper get <1..4> <register> <bytes>
stepper set <1..4> <register> <bytes> <value>
```

Numbers may be decimal or `0x`-prefixed hexadecimal.

`stepper nop` sends a complete four-slot chain frame in which every slot receives its driver-provided idle frame (`L6470 NOP`). It is the least invasive way to confirm that SPI transactions and the chain framing are active. The displayed RX bytes are raw shifted response bytes; they are not device IDs.

`stepper status` sends `GET_STATUS`. Reading status clears latched L6470 status flags.

`stepper set` is deliberately a raw register-write tool. The console validates field sizes but does not decide whether a register/value combination is electrically appropriate.

### Controlled motion

```text
stepper reset <1..4|all>
stepper hiz [1..4|all]
stepper stop [1..4|all]
stepper config <1..4|all>
stepper config <1..4|all> <hold> <run> <acc> <dec>
stepper config <1..4|all> defaults
stepper microstep <1..4|all>
stepper microstep <1..4|all> <full|1/2|1/4|1/8|1/16|1/32|1/64|1/128>
stepper microstep <1..4|all> defaults
stepper run <1..3|all> <fwd|rev> <steps/s>
stepper move <1..3> <fwd|rev> <steps>
```

The console limits `run` to 0–5000 steps/s and `move` to the L6470 22-bit step range.

`stepper config <selector>` reads the four KVAL registers. Supplying four values writes KVAL_HOLD, KVAL_RUN, KVAL_ACC, and KVAL_DEC respectively. Values may be decimal or `0x`-prefixed hexadecimal and must be 0..255. For example:

```text
stepper config 1
stepper config 1 0 10 10 10
stepper config all 0 25 40 40
stepper config all defaults
```

`stepper microstep <selector>` reads the STEP_SEL field from the L6470 STEP_MODE register. Supplying a mode changes one motor or all motors. Aliases `off`, `1`, `2`, `4`, `8`, `16`, `32`, `64`, and `128` are accepted. For example:

```text
stepper microstep all
stepper microstep 1 1/16
stepper microstep all 1/128
stepper microstep all defaults
```

Changing step mode first places the selected driver in hard HiZ because the L6470 only accepts STEP_MODE changes while its bridges are disabled. The command also resets ABS_POS to zero because changing STEP_SEL resets the electrical microstep position and makes the previous absolute-position count invalid. The selected mode is retained across `stepper reset`; a firmware restart or `defaults` restores full-step. `stepper move` counts are expressed in the selected step unit, so at 1/128 mode a move of 128 advances one full motor step.

The compiled hardware-tested defaults are:

| Register | Value |
|---|---:|
| STEP_MODE | `0x00` |
| ACC | approximately 25 steps/s² (`0x0003`) |
| DEC | approximately 25 steps/s² (`0x0003`) |
| MAX_SPEED | approximately 500 steps/s (`0x0021`) |
| KVAL_HOLD | `0x00` |
| KVAL_RUN | `0x19` (decimal 25) |
| KVAL_ACC | `0x28` (decimal 40) |
| KVAL_DEC | `0x28` (decimal 40) |

Runtime KVAL and microstep tuning are retained across `stepper reset` commands. A firmware restart restores the compiled defaults. `stepper config <selector> defaults` restores the full verified configuration, including full-step mode, immediately. Motor 4 is always returned to hard HiZ and operational `run`, `move`, and `run all` commands exclude it.

## Suggested first hardware sequence

With motor power controllable and current limited where practical:

```text
module list
stepper info
safe
stepper nop 5
stepper status all
stepper reset all
stepper get 1 0x16 1
stepper get 1 0x05 2
stepper get 1 0x09 1
stepper move 1 fwd 10
stepper status 1
stepper hiz all
```

Then repeat the short move for motors 2 and 3 individually. Motor 4 is intentionally unavailable for motion commands. Continuous running can follow at a low speed, for example:

```text
stepper run 1 fwd 20
stepper stop 1
stepper hiz all
```

Keep `safe` or `stepper hiz all` ready in a second console line during early testing.

## Validation performed

- Strict C11 syntax checks were run for all modified translation units using Pico SDK interface stubs.
- CLI host smoke tests covered help, module listing, chain info, NOP, status, register get/set, configuration, run, move, HiZ, and safe-state dispatch.
- Stepper/bus host functional tests covered reset broadcast, all-NOP frames, logical response extraction, single-device RUN padding, synchronized RUN-all, the conservative register sequence, and hard-HiZ broadcast.
- `git diff --check` passed.

A real Pico SDK build and hardware test remain the definitive validation.
