# Hub 0.2.29

- Consume ZapEE v0.6.0 stepper, HV PSU and TEC PSU modules through motherboard adapters.
- Replace local chip/module implementations with shared ZapEE components.
- Regenerate dependency integration using the current Zap package-manager source.
- Fix TEC status reporting when enable is off but DAC shutdown fails.

Validation: ZapEE's 14 host tests pass at v0.6.0. The Hub integration build
passed with Pico SDK 2.3.0 and GCC 15.2.1 before the release version bump.
The fresh build against the published dependency was not run because permission
was declined. Hardware regression remains pending; no firmware was uploaded.
