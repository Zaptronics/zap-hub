# Hub 0.3.7 / ZapEE 0.11.0 migration status

## Previous ADC migration

ADC SPI and operation-lock setup now lives in zap::pico_ztpico2_ztm_adc.
Hub retains its instance, event/error text, registry callbacks, bus accessor
and chamber measurement assignments. SPI timing, timeouts and portable module
initialization remain unchanged. No power sequencing is added; EX_NONE is
preserved. Each consumer owns persistent binding and module storage.

## Previous PWM migration

PWM I2C and connector setup now lives in zap::pico_ztpico2_ztm_pwm.
Hub retains its singleton, active-low EX_I2C_ALT_IO_1 IRQ assignment, event
messages and registry callbacks. The Hub wrapper rejects other module pointers
to prevent reuse of its singleton binding; other consumers own their bindings.
Bus defaults and connector preparation ordering are preserved. No new operation
locking or module startup policy is introduced.

## Previous TEC migration

The ZTPICO2 TEC PSU SPI/lock/connector setup now lives in the optional
zap::pico_ztpico2_ztm_psu_tec component. Hub retains its four module instances,
their registry callbacks, connector assignments and buffered-reference/1x-gain
selection. No output-control policy moved out of Hub.

The adapter takes explicit board, module and binding storage; it has no Hub
includes, module inventory, logging or fixed instance count. Each module needs
a distinct binding with a stable address. The underlying motherboard still
supports one active physical board per MCU.

SPI mode 0, MCP4901 baud rate, one-microsecond CS-high time, default transport
timeout, 500000-us operation lock and connector-disable-before-init sequence
are preserved for Hub. Invalid board/endpoints are rejected before GPIO access.

## Remaining adapter work

- HV PSU: separate its transport/connector binding from Hub VSENSE selection,
  divider ratio and event reporting before moving it.
- ztmb/adc.c: the reusable conversion already calls ztpico2_adc_read. Its legacy
  channel-1/7500-ratio convenience entry point is application-specific; review
  its callers before removing or replacing it.

Chamber logic, inventory, credentials and web assets remain in Hub.

## Stepper migration in this change

Framed SPI, operation locking and context-based power control now live in the
optional zap::pico_ztpico2_ztm_stepper component. Hub retains its instance,
connector assignments, event text, registry callbacks and bus accessor.
The portable stepper module and shared transport implementation are unchanged.
SPI mode 3, 1 MHz, 8-bit MSB-first, zero setup/hold, 1-us CS-high,
default transport timeout, zero read-fill and 5000-us operation lock are retained.
The existing off/1-ms/on/1-ms/reset/1-ms/defaults sequence remains in the module.

## User handoff

Run your current Zap executable from Hub: zap sync --no-configure.
Generated dependency files were not edited; sync refreshes the selected component
and package metadata before your normal build. Hub's 0.3.6 managed SDK setup is retained.

The previous notes record user-confirmed TEC and PWM builds. ADC build confirmation
is still unverified. Validation of this change was not run: no configure, build,
test, Git operation or rollback-patch application. Hardware regression is skipped.

Rollback this change using Hub_0.3.7_stepper-adapter-migration.patch and
ZapEE_0.11.0_stepper-adapter-migration.patch in the respective patches folders.
Reverse both patches; if dependency files were synced, sync again after rollback.
