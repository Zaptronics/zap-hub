# Stepper KVAL and fault-monitoring update

## Verified KVAL defaults

All four L6470 devices are configured at startup and after reset with:

- `KVAL_HOLD = 0` (`0x00`)
- `KVAL_RUN = 25` (`0x19`)
- `KVAL_ACC = 40` (`0x28`)
- `KVAL_DEC = 40` (`0x28`)

Stepper 4 is still returned to hard Hi-Z after configuration.

## Periodic fault monitoring

The stepper module is registered with the module polling system and is checked on the existing one-second hub tick.

A single three-frame daisy-chain transaction reads all four L6470 STATUS registers:

1. `GET_STATUS` to all four devices.
2. Clock the status MSB from all four devices.
3. Clock the status LSB from all four devices.

The following active-low flags are monitored:

- `STEP_LOSS_A` or `STEP_LOSS_B`: `ERROR: Stepper N Stall!`
- `TH_WRN` or `TH_SD`: `ERROR: Stepper N Over Temp`

Messages are edge-triggered. A persistent fault is reported once rather than once per second. After a normal status poll clears the software fault state, a later recurrence is reported again.

`GET_STATUS` clears the L6470's latched fault flags as part of each poll.
