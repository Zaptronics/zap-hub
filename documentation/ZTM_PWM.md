# ZTM PWM Module

Firmware version: **0.2.6**

## Purpose

`ztm_pwm` provides twelve independent low-side PWM load channels, six independent four-wire fan PWM/TACH interfaces, four exposed GPIOs, module power control, shared interrupt-source handling, and current monitoring where a PAC device is available.

The module is an I2C0 assembly. The module owns the common enable line; individual chip drivers do not independently switch module power.

## Hardware devices

| Device | Address | Function |
|---|---:|---|
| SX1509 | `0x71` | Twelve low-side PWM gate controls and four user GPIOs |
| PAC195x 1 | `0x1F` | Current inputs associated with load channels 1–4; prototype ADDRSEL workaround |
| PAC195x 2 | `0x11` | Current inputs associated with load channels 5–8 |
| PAC195x 3 | `0x12` | Current inputs associated with load channels 9–12 |
| EMC2303 1 | `0x2E` | Fan PWM/TACH for fans 1–3 |
| EMC2303 2 | `0x2F` | Fan PWM/TACH for fans 4–6 |

- Bus: `ZTMB_I2C0_PORT`, 400 kHz
- Module enable: `EX_I2C_ALT_EN_1`
- Shared active-low IRQ: `EX_I2C_ALT_IO_1`
- Current shunts: 50 mΩ, four-wire

## PAC1952-2 assembly salvage

The PCB footprint and routing were designed for three four-channel PAC1954 devices, but the prototype was accidentally populated with PAC1952T-2E two-channel low-side devices. The PAC1952-2 has two differential VSENSE inputs and two independent differential VBUS inputs. Because the PCB routes all four intended shunts to those pins, firmware 0.2.6 uses each available ADC input directly as a shunt-voltage measurement.

Within each four-channel group, the recovered mapping is:

| Logical channel in group | PAC1952-2 input | Measurement quality |
|---:|---|---|
| 1 | VBUS1± | Coarse |
| 2 | VSENSE1± | High resolution |
| 3 | VSENSE2± | High resolution |
| 4 | VBUS2± | Coarse |

This repeats across all three PAC addresses:

| Load channel | PAC | ADC input |
|---:|---:|---|
| 1 | 1 | VBUS1 |
| 2 | 1 | VSENSE1 |
| 3 | 1 | VSENSE2 |
| 4 | 1 | VBUS2 |
| 5 | 2 | VBUS1 |
| 6 | 2 | VSENSE1 |
| 7 | 2 | VSENSE2 |
| 8 | 2 | VBUS2 |
| 9 | 3 | VBUS1 |
| 10 | 3 | VSENSE1 |
| 11 | 3 | VSENSE2 |
| 12 | 3 | VBUS2 |

With a 50 mΩ shunt and the configured unipolar ranges:

- VSENSE resolution is approximately 30.5 µA/count.
- VBUS resolution is approximately 9.77 mA/count.

The VBUS-derived channels are therefore suitable for coarse monitoring of fans, lamps, heaters, and similar loads, but not precision low-current measurement.

The PAC’s native `VPOWER` and accumulator results are **not valid for this salvaged routing** because each internal PAC power channel multiplies a VBUS input and a VSENSE input that now belong to different physical loads. Firmware uses only the selected raw VBUS or VSENSE input and calculates current from the 50 mΩ shunt. It does not report PAC VPOWER as load power.

A correctly populated PAC1954 is still accepted. For a native four-channel PAC1954, the intended mapping remains `VSENSE2, VSENSE1, VSENSE4, VSENSE3` within each four-load group.

## Degraded operation

A missing or unsupported PAC current monitor no longer prevents the rest of the PWM module from starting. The SX1509 outputs, EMC2303 fan controls, GPIOs, and any responding PAC devices remain available. Channels belonging to a missing PAC report `CURRENT UNAVAILABLE`.

This is important for the current prototype. PAC1's intended 0 Ω ADDRSEL pull-down was omitted, and the device currently latches address `0x1F` at power-up. Firmware 0.2.6 therefore addresses PAC1 at `0x1F`.

## PAC1 address workaround

The PAC195x data sheet defines the address using a resistor from ADDRSEL to ground, or by tying ADDRSEL to VDD. A floating ADDRSEL pin is **not a specified address condition**. The prototype currently responds consistently at `0x1F`, which is the address assigned when ADDRSEL is tied to VDD, so firmware 0.2.6 uses `0x1F` for PAC1.

This is suitable for continuing bench testing, but it should not be treated as a permanent production fix. The address is sampled and latched at each power-up, so an unconnected pin could select a different address after a power cycle, temperature change, contamination, or electrical noise. Fit the intended 0 Ω pull-down to ground to restore address `0x10`, or deliberately tie ADDRSEL to VDD if `0x1F` is to become the permanent board address. Firmware will then need to match that intentional hardware choice.

## Low-side channel mapping

The SX1509 LED engine drives the external low-side N-channel MOSFET gates.

| Load channel | SX1509 IO |
|---:|---:|
| 1 | 15 |
| 2 | 14 |
| 3 | 13 |
| 4 | 12 |
| 5 | 11 |
| 6 | 10 |
| 7 | 9 |
| 8 | 7 |
| 9 | 5 |
| 10 | 4 |
| 11 | 2 |
| 12 | 0 |

A stopped channel remains configured in the SX1509 LED/PWM engine at 0% duty. Do not use `sx1509_pwm_pin_stop()` for these outputs: it releases the open-drain line high and may turn the external N-channel stage on.

## Fan mapping

Fan PWM/TACH is independent of the low-side load output. A four-wire fan normally needs both its load channel powered and its EMC2303 PWM signal active.

| Logical fan | EMC2303 | Local fan |
|---:|---:|---:|
| 1 | 1 | 1 |
| 2 | 1 | 2 |
| 3 | 1 | 3 |
| 4 | 2 | 1 |
| 5 | 2 | 2 |
| 6 | 2 | 3 |

The console supports direct percentage drive, raw 0–255 drive, raw tach counts, RPM conversion, and closed-loop RPM targets. Issuing `pwm fan`, `pwm fan-open`, `pwm fan-raw`, or `pwm fan-stop` disables closed-loop control for that fan.

## User GPIO mapping

| User GPIO | SX1509 IO |
|---:|---:|
| 1 | 8 |
| 2 | 6 |
| 3 | 3 |
| 4 | 1 |

GPIOs initialize as inputs with no pull and no interrupt. They can be configured with internal pull-up or pull-down resistors and rising, falling, or both-edge interrupt detection.

## Shared IRQ handling

`EX_I2C_ALT_IO_1` is active low and shared by the module devices. When asserted, `ztm_pwm_poll()` checks every available source:

- SX1509 interrupt source;
- alert status from each initialized PAC device;
- EMC2303 1 and EMC2303 2 status blocks.

Missing PAC devices are skipped rather than generating a repeated I2C error. An asserted shared IRQ with no readable source is logged once per assertion episode instead of once per poll. No I2C operation is performed in a GPIO interrupt handler. The source scan runs from the normal module poll.

## Console command reference

### Module state and discovery

```text
pwm info
pwm probe
pwm i2c-scan
pwm power off
pwm power on
pwm power cycle
pwm irq-line
pwm irq
```

`pwm probe` checks the six expected addresses and identifies PAC product variants. It is safe to run even when normal module initialization has failed.

`pwm i2c-scan` scans addresses `0x08` through `0x77` while the module is powered. It is intended to locate a device whose address strap differs from the schematic. The scan performs a one-byte dummy read, so run `pwm power cycle` afterward before normal operation.

`pwm power off` first commands all low-side and fan outputs to zero, then lowers the physical module enable. `pwm power on` and `pwm power cycle` perform a complete module reinitialization.

### Low-side outputs

```text
pwm set <1..12> <0..100>
pwm set-all <0..100>
pwm stop [1..12]
```

With no channel argument, `pwm stop` stops all twelve low-side outputs and all six fan PWM outputs.

### Current measurements

```text
pwm current <1..12|all>
pwm measure <1..12|all> [avg|instant]
```

Both commands show the PAC number and the actual source input. VBUS-derived readings are marked `COARSE`. Missing PAC groups are printed as unavailable while readings from the other groups continue.

### Fan PWM and tachometer

```text
pwm fan <1..6> <0..100>
pwm fan-raw <1..6> <0..255>
pwm fan-target <1..6> <rpm>
pwm fan-open <1..6> <0..100>
pwm fan-stop [1..6]
pwm rpm <1..6|all>
pwm tach <1..6|all>
pwm fan-status [1|2|all]
```

### User GPIO tests

```text
pwm gpio-mode <1..4> <in|out>
pwm gpio-pull <1..4> <none|up|down>
pwm gpio-irq <1..4> <off|rising|falling|both>
pwm gpio-write <1..4> <0|1>
pwm gpio-toggle <1..4>
pwm gpio-read <1..4|all>
```

### Raw register diagnostics

```text
pwm sx-read <reg> [count]
pwm sx-write <reg> <byte>
pwm pac-read <1..3> <reg> <count>
pwm pac-write <1..3> <reg> <byte0> [byte1..byte7]
pwm emc-read <1..2> <reg>
pwm emc-write <1..2> <reg> <byte>
```

Raw writes bypass normal module-state tracking. Run `pwm power cycle` afterward to restore the standard configuration.

## Recommended prototype test

```text
version
pwm probe
pwm i2c-scan
pwm power on
pwm info
module list
pwm current all
pwm measure all avg
```

Expected prototype behavior with the missing PAC1 ADDRSEL resistor:

- PAC1 is found at `0x1F`.
- PAC2 and PAC3 remain at `0x11` and `0x12`.
- All three are identified as PAC1952-2 salvage devices.
- CH1–CH12 current inputs are available, with the documented coarse/fine resolution split.

For a loaded VBUS-derived channel, expect the displayed current to change in steps of about 9.77 mA. For a VSENSE-derived channel, the step is about 30.5 µA.

## Safety behavior

- Startup sets all twelve low-side channels to 0%.
- Startup writes all six EMC2303 drive registers to zero.
- `safe` and `pwm stop` stop both low-side outputs and fan PWM outputs.
- `pwm power off` lowers the module’s physical enable after stopping outputs.
- Raw writes are intentionally not interpreted or corrected by the module.
