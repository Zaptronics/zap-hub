# Changelog

## 0.6.4

- Remove the temporary Run input diagnostic line and its unused tracking state.
- Preserve the ten-second full redraw and existing screen spacing.
- Validation not run: no builds, tests or rollback application.


## 0.6.3

- Require ZapEE 0.13.1 server-side console echo/editing so one PuTTY setup works at the prompt and in Run.
- Show the required local-echo/local-line-editing settings in the connection banner.
- Validation not run: no builds, tests or rollback application.


## 0.6.2

- Fully redraw the run dashboard every ten seconds, deferred while network output is pending.
- Show received-key count and last byte to distinguish client buffering from blocked modes.
- Show immediate-input client settings; preserve ESC exit and emergency-stop key behavior.
- Validation not run: no builds, tests or rollback application.


## 0.6.1

- Reduce run-screen flicker: clear on entry, then overwrite only changed rows and erase trailing text.
- Batch each refresh into one bounded write; send nothing when the display is unchanged.
- Keep ordinary command output from scrolling over the dashboard, including STOP reports; its result remains in the status row.
- Reset the display cache on entry/exit/disconnect; clip long rows to avoid wrapping.
- Validation not run: no builds, tests or rollback application; user will review and test.


## 0.6.0

- Default-lock all Cirrus1 TEC enable paths until explicit operator chiller confirmation.
- Gate the physical enable callback as well as single/all-channel cooling APIs and CLI aliases.
- Serialize permission changes with enable writes; revoke on all-stop, safe/STOP, reinitialization, deinitialization and failed start/readback.
- Attempt every TEC disable independently and report failures without claiming verified shutdown.
- Add tec permit/revoke commands and dashboard permission state.
- Validation not run: no builds, tests, hardware regression or rollback application.


## 0.5.0

- Apply confirmed Cirrus1 pump mapping: filter 1, wick 2, return/sump 3.
- Use 30 steps/s normal flow and 100 steps/s wick prime/drain; retain existing timed command durations.
- Add continuous pump wick-wet command and distinguish it from timed dosing in status.
- Record load, fan and RTD assignments in cirrus1_config.h and show wiring on the run dashboard.
- Preserve legacy API role IDs; document remaining operating-target requirements.
- Validation not run: no builds, tests or rollback application.


## 0.4.0

- Add run ANSI dashboard and TCP single-key input through ZapEE 0.13.0.
- Show initialization, pump/PWM commanded states, STOP arrow and explicit telemetry gaps.
- Route 7 and unknown keys to global safe-off; report failures. ESC leaves outputs unchanged.
- Keep Tests and process recipes unavailable pending ZapLink checks, mappings, regulation and operating settings; Go remains locked.
- Validation not run: no builds, tests or rollback application.


## 0.3.9

- Report unsuccessful PWM stop with the latched POWER_SAFE code; describe success as zero-duty writes rather than verified standstill.
- Label the tach sentinel as no valid tach, since the fan may still be spinning.
- Require ZapEE 0.12.3 for corrected stop return values; document the unresolved POWER_SAFE incident in docs.
- Validation not run: no builds, tests, or rollback application; hardware testing remains with the user.

## 0.3.8

- Report PWM POWER_SAFE startup timeout, input read failure, and runtime loss.
- Guard direct PWM register diagnostics with live POWER_SAFE checks.
- Consume ZapEE 0.12.0 for corrected PAC1 addressing and power sequencing.
- Validation not run: builds and tests remain with the user.

## 0.3.7

- Use ZapEE 0.11.0's optional ZTPICO2 stepper adapter for framed SPI, locking and power callbacks.
- Retain Hub's module instance, wiring, event text and registry/bus interfaces.
- Preserve SPI timing, timeouts and portable startup behavior.
- Update the README and migration status to the current source versions.
- Validation not run: building and testing are performed by the user.

## 0.3.6

- Select the managed Pico SDK before SDK import (existing source baseline).

## 0.3.5

- Extract ADC transport and operation-lock setup into ZapEE's optional board adapter.
