# Cloud chamber Hub

Hub 0.6.31 requires local ZapEE 0.13.2 for normal-console server echo and
backspace editing. Keep PuTTY Local echo and Local line editing both Force off;
the same settings now support the command prompt and Run dashboard.


CB interlock at 4 C (0.6.31): CB below 4 C unlocks the TECs; above 4 C or open
circuit locks them and stops all four.


Colours and TEC fan auto-on (0.6.30): TEC2 above 40 C turns the TEC fans on;
new temperature colours and flashing red alerts in Run.


0.6.29: small undocumented Run update; see CHANGELOG.


0.6.28: small undocumented Run update; see CHANGELOG.


0.6.27: small undocumented Run update; see CHANGELOG.


0.6.26: small undocumented Run update; see CHANGELOG.


0.6.25: a small undocumented addition to Run; see CHANGELOG.


K permit (0.6.24): K accepts CB at or below 0 C.


K permit (0.6.23): K requires CB strictly below 0 C.


TEC permit in Run (0.6.22): after `t`, press K to confirm the chiller and start
the TECs without leaving Run.


CB interlock (0.6.21): CB below 0 C unlocks the TECs automatically; CB above
0 C or open circuit locks them and stops all four TEC modules.


Glass heater (0.6.20): the Run `g` prompt now accepts 0-100% (50% cap removed).


Load currents (0.6.19): Run shows LED L/R, glass heater and TEC fan current
next to each output, refreshed about once a second.


Pump wiring (0.6.18): filter motor 1, return motor 2, wick motor 3; filter and
return run at 100 steps/s, wick wet at 30.


Docs refresh (0.6.17): Run, wiring and TEC interlock docs updated to match
0.6.16. No firmware change.


Fan low preset (0.6.16): low is now 30% with a 20% closed-loop floor, after
the 20% preset surged. Validation not run.


Review fixes (0.6.15): Run's background protection now locks against the
network context, and a safe typed outside Run cancels Run sequences.
Validation not run.


Run keeps running (0.6.14): ESC leaves TECs, HV and sequences as they are
(after a second ESC), and protection continues in the background. Re-entering
Run shows the same mode and state. Validation not run.


Run sequences (0.6.13): keys 2 Standby, 3 Prime, 4 Runup and 6 Shutdown now
act after a second press within 3 s, using placeholder values in
cirrus1_config.h. Tests and Go stay unavailable. Validation not run.


Run protection and more keys (0.6.12): TECs now need the TEC fans, a valid
CB at or below 25 C and a valid TEC2 reading, and trip off on heat-sink, CB,
plate or sensor faults. New keys g glass, c TEC fans, p pumps, w wick. All
limits are placeholders in cirrus1_config.h. Validation not run.


Run title (0.6.11): a yellow Zaptronics bolt now precedes CIRRUS1 in the
title bar. If PuTTY's font lacks the glyph it shows a box or question mark.
Validation not run.


Run fans and RAM (0.6.10, ZapEE 0.13.2): the low fan preset now locks to its
RPM target (closed-loop minimum drive lowered to 10%). The title bar shows free
heap RAM. Validation not run.


Run colour layout (0.6.8): `run` now draws coloured, bordered panels on a
120x40 terminal using UTF-8 box characters. Set PuTTY Window to 120 columns x 40
rows and Translation to UTF-8. Press F5 in Run for a full redraw (0.6.9).
Validation not run.


Run temperature panel (0.6.7): `run` shows a plate map of the RTDs, CB, TEC2 and
ADC board temperatures, plate spread and min..max since Run opened. Validation not run.


Run fans and sensors (0.6.6): in `run`, f then 0-3 selects fans off/low/moderate/high
(20/50/90%). Intake and exhaust start open loop, then lock to one shared RPM target.
B, C, W1, W2, L1, L2 RTDs and the CB and TEC2 K-type probes are shown. Validation not run.


Run manual controls (0.6.5): in `run`, Space or 7 requests STOP and other
unassigned keys are ignored. t, h, l and r open 0-100% prompts for TEC (all four),
HV, LED left and LED right. W1/W2/L1/L2 RTD temperatures are shown. Validation not run.


Run dashboard rendering (0.6.2) updates changed rows and fully redraws every
10 seconds to recover after resizing. The temporary input diagnostic line was
removed in 0.6.4. Disable local echo and local line editing in the client. Use a terminal
at least 120 columns by 40 rows (0.6.8); SSH is not required. Validation not run.


TEC operator interlock (0.6.0): TEC enables are locked on boot. After physically
checking Process Chiller temperature and coolant circulation, explicitly enter
`tec permit chiller-at-temperature-and-circulating`. Permission alone starts
nothing. `tec revoke`, `tec stop`, `safe` and dashboard STOP relock and attempt
all-off. See docs/TEC_OPERATOR_INTERLOCK.md. No build or tests have been run.


Hub 0.6.4 added the `run` terminal dashboard, requiring local ZapEE 0.13.1.
Single keys, active STOP arrow, ESC return and safe-off dispatch are connected.
Cirrus1 wiring and pump speeds now follow notes/cirrus1-configuration.md:
filter motor 1, wick motor 2, return motor 3; normal flow is 30 steps/s
(since 0.6.18: return motor 2, wick motor 3, filter and return 100 steps/s).
`pump wick-wet` runs continuously; prime/drain use 100 steps/s with the existing
five-second timer. Legacy pump start/stop IDs remain role IDs, not motor numbers.
Modes 1-6 remain unavailable pending board checks and chamber operating targets.
This is the console foundation; see docs/RUN_CONSOLE.md. Validation not run.


PWM fault reporting in 0.3.9 requires ZapEE 0.12.3. Unavailable outputs now
fail the stop command instead of reporting success. See docs/PWM_FAN_FAULT.md
for the reported fan incident and the remaining POWER_SAFE investigation.

Hub owns the cloud chamber application running on ZTPICO2. ZapEE supplies the
portable module/chip drivers, Pico motherboard support and optional shared services.
The sibling Zap project is authoritative for package-manager behavior.

Hub 0.6.31 consumes the local ../ZapEE source. After applying both migration
patches, regenerate dependency files with your current Zap executable:

```powershell
zap sync --no-configure
```

Then use your normal build/test workflow. This change has not been built or tested
by the assistant. Hardware regression is skipped. No Git operations were performed.

Keep WiFi credentials in local build settings (`ZAP_HUB_WIFI_SSID` and
`ZAP_HUB_WIFI_PASSWORD`). Application credentials, web assets, module inventory,
event messages and chamber policy stay in Hub. `ztmb` contains application bindings and services configured by Hub. Board and
driver headers are included directly from ZapEE; forwarding headers were removed.

See documentation/MIGRATION_STATUS.md and ../ZapEE/docs/ZTPICO2.md.

TEC transport and connector setup now uses ZapEE 0.8.0's optional
zap::pico_ztpico2_ztm_psu_tec component. Hub retains its four instances and
application configuration. Run zap sync --no-configure before your next build.

PWM transport and connector setup now uses ZapEE 0.9.0's optional
zap::pico_ztpico2_ztm_pwm component. Hub retains the module instance, IRQ
assignment/polarity and event messages. Sync before your next build.

ADC transport and operation-lock setup now uses ZapEE 0.10.0's optional
zap::pico_ztpico2_ztm_adc component. Diagnostics and measurement assignments
remain in Hub. Run zap sync --no-configure before your next build.

Stepper framed SPI, operation locking and power callbacks now use ZapEE 0.11.0's
optional zap::pico_ztpico2_ztm_stepper component. Hub retains its instance,
connector assignments and event messages. Run zap sync --no-configure before
your next build. Building and testing remain with the user.

PWM bring-up in Hub 0.3.8 requires ZapEE 0.12.0. PAC1 now uses 0x10.
Startup, probe and scan wait up to five seconds for POWER_SAFE on motherboard
TLA2518 physical pin 15 (GPIO0). A detected low or read failure disables the
module and reports an error; recover using `pwm power cycle`. The timeout is
reported separately from missing I2C devices. See ../ZapEE/docs/PWM_POWER_SAFE.md.
