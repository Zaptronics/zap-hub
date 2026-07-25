# ZTM ADC / AD7124-8 completion pass

## Implemented board topology

The generic `drivers/ad7124` layer now represents one AD7124-8 and contains no
ZTM board-specific sensor map. The `modules/ztm_adc` layer owns the physical
front end:

| Logical input | AD7124 channel | Analogue pins | Setup |
| --- | ---: | --- | --- |
| Thermocouple 1 | CH0 | AIN0 / AIN1 | TC |
| Thermocouple 2 | CH1 | AIN2 / AIN3 | TC |
| Thermocouple 3 | CH2 | AIN4 / AIN5 | TC |
| PCB cold-junction Pt1000 | CH3 | AIN6 / AIN15 | RTD |
| External RTD 1..8 | CH4..CH11 | AIN7..AIN14 / AIN15 | RTD |

All channels are normally disabled. A measurement enables one ADC channel for
one single conversion and disables it afterward.

## Thermocouple setup

- Internal 2.5 V reference enabled.
- Bipolar input mode.
- PGA gain 32, giving a nominal +/-78.125 mV input range.
- AIN input buffers enabled.
- VBIAS enabled on AIN1, AIN3, and AIN5 so each floating thermocouple pair has a
  defined common-mode voltage.
- Each thermocouple channel has a runtime-configurable standard probe type.
- Type K is the power-on default for TC1..TC3. Types B, E, J, K, N, R, S, and T
  are supported using the NIST reference functions.
- The measured differential voltage is cold-junction compensated using the PCB
  Pt1000, then converted to degrees C.
- Exact ADC rail codes `0x000000` and `0xFFFFFF` are reported as disconnected
  for thermocouple, CJC, and external RTD channels.

## RTD setup

- Pt1000 IEC 60751 conversion using the full Callendar-Van Dusen equation.
- AIN6 is the PCB/cold-junction Pt1000.
- AIN7..AIN14 are external RTD 1..8.
- One IOUT0 excitation path is enabled at a time at 500 uA.
- IOUT1 is kept off.
- Excitation is switched off after every reading, including error exits.
- Unipolar, gain 1, REFIN1 ratiometric conversion.
- The AD7124 `REF_DET_ERR_EN` diagnostic is deliberately disabled. With the
  reference is absent whenever the one-shot RTD excitation current is off, so
  the detector would still report a false fault while idle. The board now uses
  500 uA during a measurement, producing about 1.0 V across the 2 kohm
  reference and giving useful margin above the AD7124 minimum external
  reference voltage. The current is switched off immediately after the one-shot
  conversion to limit sensor self-heating.
- Existing board values retained as visible module constants:
  - common sensed return input: AIN15
  - precision ratiometric reference: 2000 ohm
  - 100 ohm series components are treated as input/reference protection rather
    than the ratiometric reference value.

The supplied schematic confirms the physical return arrangement:

- the sensor return node `RTD_R` connects directly to `REFIN1(+)`;
- the 2 kohm reference resistor connects from `RTD_R` to VSSA/`REFIN1(-)`;
- AIN15 senses the same `RTD_R` node through the 100 ohm series resistor R6.

Therefore the measurement channel correctly uses AIN15 as AINM while Setup 1
uses REFIN1 as the ratiometric reference. If the fitted precision reference is
not 2 kohm, update `ZTM_ADC_RREF_OHMS` before trusting temperature values.

## Driver improvements

`drivers/ad7124` now provides:

- checked 8-, 16-, and 24-bit register access;
- reset and revision-tolerant AD7124-8 ID checking;
- ADC control, setup, filter, channel, VBIAS, and IOUT configuration;
- CS-framed SPI diagnostics support;
- status and error register helpers;
- complete board-configuration register readback verification;
- one-shot conversion and internal calibration helpers;
- generic bipolar voltage and unipolar-ratio conversion helpers.

The old unused `ad7124_scan_15pt1000.c` standalone prototype was removed. It
contained a second `main()`, obsolete pin assignments, and incomplete code.

## Diagnostics and polling

Initialization reads back the complete ADC configuration, including all 16 channel registers, before declaring the module ready. The module registry then polls the ADC once per second. It checks the status and
error registers and logs new events such as:

- conversion or calibration failure;
- ADC saturation;
- missing/low reference;
- SPI command, address, or clock-count faults;
- memory-map or ROM CRC faults;
- unexpected ADC reset.

Console measurements and polling use a nonblocking module-level ownership flag,
so a multi-register measurement cannot be interleaved by the Wi-Fi console
callback. Polling skips an occupied ADC rather than waiting inside a callback.

## Console commands

```text
adc info
adc cjc
adc rtd <1..8|all>
adc tc <1..3|all>
adc tc-type <1..3|all> [B|E|J|K|N|R|S|T]
adc read-all
adc verify
adc errors
adc diag
adc reg <address 0..0x38> <width 1|2|3>
adc calibrate <tc|rtd|all>
adc iout-off
```

The root `safe` command also disables both AD7124 excitation currents.

## Suggested hardware test sequence

```text
module list
adc info
adc verify
adc errors
adc iout-off

adc cjc
adc rtd 1
adc rtd 8
adc rtd all

adc tc-type all
adc tc 1
adc tc all
adc read-all
adc errors
```

Before trusting absolute RTD temperatures, measure the fitted precision
reference resistor and confirm the shared return node. For initial thermocouple
checks, short each TC input pair locally; the differential result should be near
0 mV while the CJC Pt1000 reports the PCB temperature.

`adc calibrate all` is available, but initial hardware bring-up should first
verify register setup, reference voltage, excitation routing, and raw readings.

## Firmware 0.2.2 diagnostic changes

Firmware 0.2.2 records the exact stage of the latest conversion. Failed `adc cjc`
and single-channel `adc rtd` commands now print:

- the failure stage;
- expected and returned AD7124 channel numbers;
- STATUS before conversion and when ready was detected;
- the configured CHANNEL register;
- the active IO_CONTROL_1 current routing;
- raw DATA and ERROR values;
- calculated resistance when a conversion completed but the result was outside
  the accepted Pt1000 range.

The same data can be displayed later with `adc diag`. Thermocouple differential
readings no longer fail merely because the cold-junction RTD is unavailable; in
that case the raw millivolt reading is reported with `CJC unavailable`.


## Firmware 0.2.3 conversion-timing diagnostics

Bench output from 0.2.2 showed RDY still high at the old 500 ms timeout, but
RDY low on a later console command with no AD7124 error bits set. Firmware
0.2.3 therefore treats the next run as a timing investigation rather than an
analogue-reference failure:

- the one-shot conversion timeout is temporarily increased to 2 seconds;
- STATUS is polled at a 1 ms cadence rather than continuously;
- the elapsed conversion time and poll count are recorded;
- ADC_CONTROL is read back immediately after requesting single conversion, at
  ready/timeout, and after cleanup;
- the active FILTER register is included in `adc diag`.

For the configured sinc4 filter, FS=200, and mid-power mode, the data-sheet
settling-time calculation is about 167 ms. If hardware reports a materially
longer time, the new diagnostics distinguish a slow conversion from a mode
write/readback or cleanup problem.

## Firmware 0.2.16 thermocouple conversion

TC1..TC3 default to Type K. The setting is runtime-only and can be queried or
changed independently:

```text
adc tc-type all
adc tc-type 1 K
adc tc-type 2 T
adc tc-type 3 J
```

The conversion follows the standard cold-junction compensation sequence: the
configured type's equivalent voltage at the CJC temperature is added to the
measured differential voltage, then the combined 0 C-referenced voltage is
converted back to temperature. `adc tc` and `adc read-all` retain the raw
differential voltage and CJC value in their output for diagnostics.

ADC endpoint codes `0x000000` and `0xFFFFFF` are classified as `DISCONNECTED`
instead of being converted into full-scale temperatures. A disconnected input
does not abort `adc read-all`; the scan continues through the remaining inputs.
