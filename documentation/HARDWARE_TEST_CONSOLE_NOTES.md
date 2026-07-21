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
stepper run <1..3|all> <fwd|rev> <steps/s>
stepper move <1..3> <fwd|rev> <steps>
```

The console limits `run` to 0–5000 steps/s and `move` to the L6470 22-bit step range.

`stepper config` applies the hardware-tested values, with the ACC and DEC register widths set to two bytes:

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

These values are now applied automatically to all four L6470s at startup. Motor 4 is then placed in hard HiZ and operational `run`, `move`, and `run all` commands exclude it. Reset commands automatically restore this configuration and return motor 4 to hard HiZ.

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
