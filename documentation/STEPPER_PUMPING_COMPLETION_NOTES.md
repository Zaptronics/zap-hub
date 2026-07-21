# Stepper and pumping completion pass

This pass incorporates the first real-hardware findings from the four-L6470 stepper module and maps the chamber's IPA pump processes onto motors 1–3.

## Assumptions resolved from the test notes

- The line `IPA Sump Stop: stepper run 2 fwd 50` was interpreted as `stepper stop 2`; running the motor cannot implement a stop operation.

## Stepper startup and reset behaviour

Every L6470 is reset and configured during `ztm_stepper_init()` with:

| Register | Value |
|---|---:|
| STEP_MODE | `0x00` |
| ACC | approximately 25 steps/s² |
| DEC | approximately 25 steps/s² |
| MAX_SPEED | approximately 500 steps/s |
| KVAL_HOLD | `0x00` |
| KVAL_RUN | `0x19` (decimal 25) |
| KVAL_ACC | `0x28` (decimal 40) |
| KVAL_DEC | `0x28` (decimal 40) |

Motor 4 is populated in the SPI chain but unused in this chamber. It is placed in hard HiZ after startup, configuration, and reset. Operational `run`, `move`, and synchronized `run all` commands only address motors 1–3. Diagnostic status/register access remains available for all four devices.

`stepper reset` now uses `busy_wait_ms(1)` rather than `sleep_ms(1)`, because console and HTTP commands may execute in the CYW43/lwIP callback context. It also reapplies the verified register configuration before returning.

## IPA pump mapping

| Process | Stepper command | Duration |
|---|---|---:|
| Filter normal | motor 1, forward, 50 steps/s | continuous |
| Filter fast | motor 1, forward, 100 steps/s | continuous |
| Wick prime | motor 3, forward, 50 steps/s | 5000 ms |
| Wick dose | motor 3, forward, 50 steps/s | 2000 ms |
| Wick drain | motor 3, reverse, 50 steps/s | 5000 ms |
| Sump run | motor 2, forward, 50 steps/s | continuous |

The former `pumping_drain_*` naming is replaced by `pumping_sump_*`, and `pumping_flood_cold_plate()` has been removed.

Timed wick processes use an absolute millisecond deadline. `pumping_update()` is now checked continuously in the main loop, instead of only on the one-second subsystem tick, so a two-second dose cannot terminate early depending on tick phase.

## Console commands

```text
pump status
pump filter-normal
pump filter-fast
pump filter-stop
pump wick-prime
pump wick-dose
pump wick-drain
pump wick-stop
pump sump-run
pump sump-stop
pump stop-all
```

The existing web aliases remain available:

```text
pump start        # filter normal
pump start 1      # filter normal
pump start 2      # sump run
pump start 3      # wick dose
pump stop         # all pumps
pump stop 1       # filter
pump stop 2       # sump
pump stop 3       # wick
```

## Suggested retest

```text
stepper info
stepper get 1 0x09 1
stepper get 1 0x0A 1
stepper get 1 0x0B 1
stepper get 1 0x0C 1
stepper reset all
stepper get 1 0x0A 1
stepper run all fwd 20
stepper stop all
stepper run 4 fwd 20
pump status
pump filter-normal
pump filter-fast
pump filter-stop
pump sump-run
pump sump-stop
pump wick-dose
pump status
```

Expected KVAL readbacks are `0x00`, `0x19`, `0x28`, and `0x28`. `stepper run 4 ...` should be rejected without changing motor 4 from hard HiZ. Wick dose should stop automatically after approximately 2000 ms.

## Host validation

- Strict C11 syntax checks passed for `Hub.c`, `pumping.c`, `modules/ztm_stepper.c`, and the console translation unit using Pico SDK interface stubs.
- Stepper tests verified startup configuration on all four drivers, motor-4 HiZ, three-slot synchronized run frames, rejection of motor-4 run, and configuration restoration after reset-all.
- Pumping tests verified all motor IDs, directions, speeds, and the absolute wick-dose deadline.


## Stepper fault monitoring update

- Every initialized L6470 is polled once per second with `GET_STATUS`.
- Active-low `STEP_LOSS_A` or `STEP_LOSS_B` reports `ERROR: Stepper N Stall!`.
- Active-low `TH_WRN` or `TH_SD` reports `ERROR: Stepper N Over Temp`.
- Reports are edge-triggered so a persistent condition does not print every second; a later recurrence after a clear status is reported again.
- The verified KVAL defaults are now HOLD=0, RUN=25, ACC=40 and DEC=40 for all four drivers.
