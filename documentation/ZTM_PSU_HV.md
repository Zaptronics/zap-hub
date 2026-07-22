# ZTM High-Voltage PSU Module

Firmware version: **0.2.11**

## Purpose

`ztm_psu_hv` controls a two-stage high-voltage supply:

1. An LM5164 regulator produces an adjustable **0–15 V drive supply**.
2. That drive supply powers an XP Power **E60** DC-to-high-voltage converter.

The E60 has a nominal full-load transfer of:

```text
0–15 V input -> approximately 0–6000 V output
Nominal scale -> approximately 400 V/V
```

The E Series output is proportional to input and is specified as linear from approximately 0.7 V input to maximum input. The E60 output is unregulated and load dependent, so the firmware value is an **indirect nominal estimate**, not a measurement of the actual high-voltage output.

XP Power datasheet:

```text
https://www.xppower.com/storage/portals/0/pdfs/SF_E_Series.pdf
```

## Hardware map

| Function | Device / signal | Hub connection |
|---|---|---|
| LM5164 setpoint | AD5282 dual digital potentiometer | I2C0, address `0x2C` for AD1/AD0 = `00` |
| Active setpoint code | AD5282 RDAC2 | Raw code `0..255` |
| Unused/safe RDAC | AD5282 RDAC1 | Forced to code `0` during normal operation |
| LM5164 enable | LM5164 EN | PCAL6416 P1.6 / `EX_HV_EN_1` |
| LM5164 power good | LM5164 PG | PCAL6416 P1.5 / `EX_HV_PGOOD_1` |
| E60 input feedback | `HV_VSENSE_1` | TLA2518 AIN1 |
| Feedback divider | 130 kΩ / 20 kΩ | Scale factor 7.5:1 |
| HV converter | XP Power E60 | 0–15 V input, nominal 0–6000 V output |

Important distinctions:

- `PG` verifies the LM5164 drive supply only.
- `HV_VSENSE_1` measures the LM5164 output/E60 input only.
- Neither signal proves that the E60 is producing high voltage.
- There is currently no direct high-voltage divider or monitor connected to the Hub.

## Voltage estimate

Firmware calculates the nominal full-load E60 output magnitude from the measured drive voltage:

```text
estimated_HV = measured_E60_input × 6000 / 15
             = measured_E60_input × 400
```

Examples:

| Measured E60 input | Nominal estimated output |
|---:|---:|
| 0.7 V | approximately 280 V |
| 2.5 V | approximately 1.0 kV |
| 5.0 V | approximately 2.0 kV |
| 10.0 V | approximately 4.0 kV |
| 15.0 V | approximately 6.0 kV |

This estimate is most representative at rated load. Under light or no-load conditions, the actual E60 output can be higher than the nominal proportional value. Use a suitable high-voltage probe for calibration or safety-critical measurements.

## Safe default state

The complete high-voltage chain is deliberately **OFF by default**.

Startup performs the following sequence:

1. Write the LM5164 EN output latch low.
2. Configure EN as an output and PG as an input.
3. Configure the AD5282 I2C endpoint.
4. Probe the AD5282.
5. Write 255 to RDAC1 and RDAC2, selecting maximum resistance and minimum requested drive.
6. Read both RDACs back and verify 255/255.
7. Confirm EN reads low.
8. Read LM5164 PG and E60 input VSENSE for diagnostics.
9. Declare the module ready while keeping EN low.

Any initialization failure drives EN low before returning.

## Verified code direction and guarded test range

Hardware testing established that the feedback direction is inverse: **lower RDAC2 code requests a higher LM5164 output**.

- Code 1 did not reach PGOOD, consistent with a requested setpoint beyond what the converter could achieve.
- Code 55 reached PGOOD and produced 13.6956 V at the E60 input.
- Code 45 reached PGOOD but produced approximately 16.33 V, above the E60 15 V rating.
- Powered operation is therefore restricted to codes 55 through 255.

The OFF state now uses RDAC1=255 and RDAC2=255 in addition to EN low. EN low remains the authoritative shutdown; the maximum-resistance RDAC state reduces the requested output should EN ever be asserted unexpectedly.

`hv sweep` characterises the one-RDAC range from high code toward low code and stops at 14 V, leaving margin below the E60 15 V input rating.

The 0.2.10 hardware sweep is retained as a calibration table. Linear interpolation between the measured ten-code points provides a convenient nominal mapping from code to E60 drive and estimated high voltage. The guarded calibrated range is approximately 1.569 kV at code 255 through 5.478 kV at code 55.

## Measured code calibration

The 0.2.10 guarded sweep measured:

| RDAC2 code | E60 drive | Nominal estimated HV |
|---:|---:|---:|
| 255 | 3.9225 V | 1.569 kV |
| 225 | 4.3214 V | 1.729 kV |
| 195 | 4.8049 V | 1.922 kV |
| 165 | 5.4577 V | 2.183 kV |
| 135 | 6.3703 V | 2.548 kV |
| 105 | 7.8571 V | 3.143 kV |
| 85 | 9.3681 V | 3.747 kV |
| 65 | 11.8220 V | 4.729 kV |
| 55 | 13.6956 V | 5.478 kV |
| 45 | 16.33 V | rejected / overvoltage |

The firmware contains all measured ten-code points, not just the abbreviated table above. Interpolated values are board-specific and should be recalibrated if the feedback components, AD5282, LM5164, or supply conditions change materially.

## Normal enable sequence

`hv on <code>` performs:

1. Drive EN low.
2. Write RDAC1 = 0.
3. Write RDAC2 = requested code.
4. Read both RDACs back and verify them.
5. Drive EN high.
6. Wait up to 300 ms for LM5164 PG while sampling AIN1 every 10 ms.
7. Retain the live and peak E60-input voltage observed during startup, including failed attempts.
8. Read the final measured E60 input voltage from AIN1.
9. Reject and shut down if the measured E60 input exceeds 15 V.
10. Calculate and report the nominal E60 high-voltage estimate.

If PG does not assert, VSENSE cannot be read, or measured E60 input exceeds 15 V, EN is immediately driven low and both RDACs are returned to 255.

`hv off` drives EN low first. Setting both RDACs to maximum resistance is a secondary safe-state action; EN-low is the authoritative hardware shutdown.

## Periodic diagnostics

The one-second module poll checks EN and LM5164 PG whenever the module is ready.

When enabled, it also reads E60 input VSENSE and checks the 15 V limit.

Possible console errors are:

```text
ERROR: LM5164 Power Good lost; E60 drive disabled
ERROR: E60 drive VSENSE read failed; output disabled
ERROR: E60 input exceeds 15 V; output disabled
```

These faults are edge-triggered and cause immediate shutdown.

## Console commands

### Safe discovery

```text
hv probe
```

Forces EN low, then checks:

- AD5282 acknowledgement and RDAC readback
- EN state
- LM5164 PG state
- TLA2518 AIN1 E60 input feedback
- Nominal E60 output estimate

This command can be used even when normal module initialization failed.

```text
hv reinit
```

Reinitializes the module and leaves it off.

### Status and measurement

```text
hv info
hv read
hv pg
```

`hv read` reports:

- EN state
- LM5164 PG state
- RDAC1 and RDAC2 readback
- raw TLA2518 AIN1 code
- measured LM5164/E60 input voltage
- nominal E60 high-voltage estimate
- an overvoltage warning if the E60 input exceeds 15 V

`hv pg` explicitly reports LM5164 PG and reminds the operator that this does not prove the E60 high-voltage output.

### Normal control

```text
hv set <0..255>
hv on <55..255>
hv on-est <1569..5479>
hv map [55..255]
hv off
```

`hv set` stages and verifies the requested code while keeping EN low. The measured drive voltage remains near zero while EN is low, so no useful high-voltage estimate is available until the supply is enabled.

`hv on` always requires an explicit raw code. It will not silently enable a previously staged value.

`hv on-est` accepts a nominal E60 output target in volts and selects the closest calibrated code that does not exceed the requested value. It remains an indirect estimate because there is no direct high-voltage monitor.

`hv map` prints the measured/interpolated calibration table or the estimate for one code.

`hv off` is the immediate safe shutdown command.

### Raw AD5282 diagnostics

```text
hv rdac-read <1|2|all>
hv rdac-write <1|2> <0..255>
```

Raw writes are rejected unless EN reads low. They are intended only for board bring-up.

Normal operation expects:

- RDAC1 = 0
- RDAC2 = requested setpoint code

## Suggested first hardware test

Keep the E60 high-voltage output safely terminated and use appropriate high-voltage spacing and measurement equipment.

```text
version
module list
hv probe
hv reinit
hv info
hv rdac-read all
hv pg
hv read
```

Expected off-state observations:

- EN low
- RDAC1 = 255
- RDAC2 = 255
- LM5164 PG normally low
- measured E60 input close to zero
- estimated HV close to zero

For calibration, start at the high-code/low-output end and move downward under VSENSE protection:

```text
hv sweep
hv sweep 255 100 10
```

After identifying a known-safe code, test it directly:

```text
hv on <known-safe-code>
hv read
hv off
```

Record measured E60 input and actual high voltage using a suitably rated probe to create a calibration table for the real load.

## Known limitations

- The E60 high-voltage output is not measured directly.
- The reported high voltage is a nominal proportional estimate derived from E60 input voltage.
- The E60 is unregulated and load dependent.
- Light/no-load high voltage can exceed the nominal estimate.
- Output polarity is determined by E60 output wiring; firmware reports magnitude only.
- The code-to-drive calibration is specific to this assembled board and bench conditions.
- The calibrated target command rounds toward lower output, but the high-voltage result remains indirect and load dependent.

## Firmware 0.2.11 changes

- Restricted powered operation to the measured safe code range `55..255`.
- Added the complete 0.2.10 sweep as a board calibration table with linear interpolation.
- Added `hv map [code]` and `hv on-est <nominal HV volts>`.
- Corrected `hv info` so it no longer displays a stale last-live drive voltage as current state after shutdown.
- Corrected the AD5282 status text to distinguish ON (`RDAC1=0`) from OFF (`255/255`).

## Firmware 0.2.10 changes

- Corrected the interpretation of the AD5282 code direction.
- Changed the OFF-state RDAC values from 0/0 to 255/255.
- Added `hv sweep [start [end [step]]]`, defaulting to a guarded 255-to-55 scan in firmware 0.2.11.
- The sweep continues past high-code points that have not yet asserted PGOOD, but stops on other faults or at 14 V measured E60 input.
