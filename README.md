# Cloud chamber Hub

Hub 0.6.4 requires local ZapEE 0.13.1 for normal-console server echo and
backspace editing. Keep PuTTY Local echo and Local line editing both Force off;
the same settings now support the command prompt and Run dashboard.


Run dashboard rendering (0.6.2) updates changed rows and fully redraws every
10 seconds to recover after resizing. The temporary input diagnostic line was
removed in 0.6.4. Disable local echo and local line editing in the client. Use a terminal
at least 100 columns by 48 rows; SSH is not required. Validation not run.


TEC operator interlock (0.6.0): TEC enables are locked on boot. After physically
checking Process Chiller temperature and coolant circulation, explicitly enter
`tec permit chiller-at-temperature-and-circulating`. Permission alone starts
nothing. `tec revoke`, `tec stop`, `safe` and dashboard STOP relock and attempt
all-off. See docs/TEC_OPERATOR_INTERLOCK.md. No build or tests have been run.


Hub 0.6.4 adds the `run` terminal dashboard, requiring local ZapEE 0.13.1.
Single keys, active STOP arrow, ESC return and safe-off dispatch are connected.
Cirrus1 wiring and pump speeds now follow notes/cirrus1-configuration.md:
filter motor 1, wick motor 2, return motor 3; normal flow is 30 steps/s.
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

Hub 0.6.4 consumes the local ../ZapEE source. After applying both migration
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
