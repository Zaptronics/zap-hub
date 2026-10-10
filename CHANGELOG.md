# Changelog

## 0.6.31

- CB interlock threshold moved from 0 C to 4 C (Dave): CB below 4 C unlocks the TECs (auto permit); above 4 C, open circuit or faulty re-locks and stops all four TECs. K and the chiller-ready start check accept CB at or below 4 C, and CB turns green at or below 4 C. Only cirrus1_config.h values changed (CIRRUS1_CB_UNLOCK_C, CIRRUS1_CB_LOCK_C, CIRRUS1_CB_READY_C), plus docs.
- Validation not run: no builds, tests or rollback application.


## 0.6.30

- TEC2 heat-sink above 40 C (CIRRUS1_TEC2_FANS_ON_C) turns the TEC fans (L4) on automatically, Run open or not; TEC2 is read at least every 4 s (CIRRUS1_TEC2_PERIOD_MS) alongside the 2 s CB read. Never turns them off.
- Temperature colours: green to 40 C, orange (256-colour) 40-60 C, flashing red above 60 C; CB orange until at or below 0 C, then green.
- Flashing red (ESC[5m) for sensor faults (open circuit now shown as O/C), a slow fan, a fan that tripped the stall stop (STALL until the next preset), and glass current under 10 mA at 20% duty or more.
- Validation not run: no builds, tests or rollback application.


## 0.6.29

- Easter-egg tune: x plays it one octave higher than z (A4-F#5, 440-740 Hz), same rules. Loudness unchanged: CIRRUS1_SONG_LEVEL_PCT is already 100, a full on/off square wave.
- Validation not run: no builds, tests or rollback application.


## 0.6.28

- Easter-egg tune stutter: while it plays (about 9 s), Run's screen tick skips its sensor, fan, VSENSE and current reads, protection checks and redraw; the main-loop CB read and Hub.c's 1 s module/sensing/cooling/sweeping polls pause too (new cli_run_song_playing()). All resume when the tune ends. STOP, safe and keys still work during it.
- Validation not run: no builds, tests or rollback application.


## 0.6.27

- Easter-egg tune: plays once through with no fade, z while playing does nothing (STOP, safe or a mode change still end it), and the melody is raised one octave (A3-F#4, 220-370 Hz).
- Validation not run: no builds, tests or rollback application.


## 0.6.26

- Easter-egg tune replaced with Dave's vocal melody from notes/particle_man_vocal_melody.mid (190 bpm, 29 notes, about 8.7 s), converted note for note into run_song[] in run_console.h.
- Validation not run: no builds, tests or rollback application.


## 0.6.25

- Easter egg (deliberately left out of the Run docs and footer): in Standby or Shutdown, z plays the Particle Man chorus hook on the ITO glass heater (L12) as a square wave, gating it on and off over I2C at each note's pitch, since the SX1509 PWM frequency only changes in shared octave steps. z again fades it out over 2 s; STOP, safe or a mode change stops it. L12's previous duty is restored afterwards. Melody transcribed from memory; it lives in run_song[] in run_console.h. New CIRRUS1_SONG_LEVEL_PCT (100).
- Validation not run: no builds, tests or rollback application.


## 0.6.24

- Reverts the 0.6.23 tightening at Dave's request: Run key K again accepts CB at or below CIRRUS1_CB_LOCK_C (exactly 0.0 C is fine). K still refuses without a valid CB reading or with CB above 0 C.
- Validation not run: no builds, tests or rollback application.


## 0.6.23

- Run key K now requires CB strictly below CIRRUS1_CB_UNLOCK_C (0 C), the same rule as the CB interlock's unlock; 0.6.22 also accepted exactly 0.0 C.
- Validation not run: no builds, tests or rollback application.


## 0.6.22

- Run key K grants the TEC permit without leaving Run (equivalent of `tec permit chiller-at-temperature-and-circulating`), only with a valid CB reading at or below CIRRUS1_CB_LOCK_C (0 C).
- A locked `t` with a non-zero value now waits up to CIRRUS1_TEC_CONFIRM_MS (10 s) for K, then grants the permit and starts the TEC at that value. STOP and safe clear the pending value.
- Footer shows k TEC permit; RUN_CONSOLE.md and TEC_OPERATOR_INTERLOCK.md updated.
- Validation not run: no builds, tests or rollback application.


## 0.6.21

- CB interlock (Dave's request): CB below 0 C grants the TEC permit automatically (once per cold spell); CB above 0 C, open circuit, or a CB fault on two reads in a row revokes it and stops all four TECs through the existing TEC trip. Runs from boot via cli_run_background, whether or not Run is open; outside Run, changes print as a [run] line.
- CB is read every 2 s (CIRRUS1_CB_PERIOD_MS); the other sensors rotate through the remaining Run slots. New CIRRUS1_CB_UNLOCK_C / CIRRUS1_CB_LOCK_C (0.0). CIRRUS1_CB_READY_C changed from 25 to 0 to match; the Status panel shows CB > 0 as the CB trip.
- RUN_CONSOLE.md gains a CB interlock section; TEC_OPERATOR_INTERLOCK.md notes the automatic grant and revoke.
- Validation not run: no builds, tests or rollback application.


## 0.6.20

- Glass heater 50% cap removed at Dave's request: CIRRUS1_GLASS_MAX_PCT is now 100, so the Run `g` prompt accepts 0-100%. The glass still has no temperature sensor; its current shows in the Outputs panel (0.6.19).
- RUN_CONSOLE.md and CIRRUS1_CONFIGURATION.md updated.
- Validation not run: no builds, tests or rollback application.


## 0.6.19

- Run Outputs panel shows the measured current of LED L (L9), LED R (L10), Glass (L12) and TEC fans (L4) beside each value, sampled through ztm_pwm_read_current_uA (averaged, as `pwm current`), one load every 250 ms. Display only; no current limits. BLED channels not shown.
- docs/RUN_CONSOLE.md gains a Load currents section.
- Validation not run: no builds, tests or rollback application.


## 0.6.18

- Cirrus1 wiring from Dave's updated notes: IPO wick is stepper 3 and IPO return is stepper 2 (they were reversed); CIRRUS1_WICK_MOTOR 3, CIRRUS1_RETURN_MOTOR 2.
- Filter and return normal speed raised from 30 to 100 steps/s (CIRRUS1_FILTER_SPEED, CIRRUS1_RETURN_SPEED). Wick speeds unchanged (wet 30, prime and drain 100).
- `pump status` and pump command help text in cmds.h now name the new motor numbers and speeds (text only).
- CIRRUS1_CONFIGURATION.md and RUN_CONSOLE.md updated to match.
- Validation not run: no builds, tests or rollback application.


## 0.6.17

- Documentation only: RUN_CONSOLE.md remaining-work section rewritten for the current state (placeholders, DAC direction, Tests/Go, loose-PWM-wire caveat) and a stale sensor-rate sentence fixed; CIRRUS1_CONFIGURATION.md wiring table brought up to date (TEC fans, lights, glass, fan presets, all fitted RTDs and thermocouples); TEC_OPERATOR_INTERLOCK.md notes Run's extra TEC checks.
- No firmware behaviour change.
- Validation not run: no builds, tests or rollback application.


## 0.6.16

- Fan low preset raised from 20% to 30% and the closed-loop minimum drive from 10% to 20%: at 20% the fans audibly surged (the RPM loop hunting near the bottom of its range). The floor stays below the low preset so the shared RPM target can still lock.
- Validation not run: no builds, tests or rollback application.


## 0.6.15

- Fix (review of 0.6.14): Run's background work now holds the lwIP/cyw43 lock. Without it, main-loop TCP writes ([run] messages) and ADC/PWM bus traffic could interleave with commands running in the network context and corrupt either.
- Fix: a safe typed at the normal prompt is now mirrored in Run (mode STOP, TEC/HV/fan state cleared, sequences cancelled). Before, a running Prime could re-apply Standby after safe and turn the filter, fans and LEDs back on.
- cmd_safe counts its requests (cmd_safe_requests) so Run can tell an outside safe from its own STOP.
- Validation not run: no builds, tests or rollback application.


## 0.6.14

- Leaving Run no longer stops the TECs. With TECs, HV or a timed sequence running, ESC asks for a second ESC within 3 s before leaving; any other key cancels.
- While Run is off screen, a new main-loop hook (cli_run_background, called from Hub.c) keeps reading sensors and fan tachs, applies the TEC trips and fan stall stop, and advances Prime and the Shutdown ramp. Any trip, stall or step is printed on the normal console as [run] <message>.
- Losing the connection also leaves outputs running under the same background protection.
- Re-entering Run keeps the mode, outputs, min..max ranges and the last notice (including a trip that happened while away).
- Validation not run: no builds, tests or rollback application.


## 0.6.13

- Draft Run sequences on keys 2, 3, 4 and 6; a mode key must be pressed twice within 3 s. All values are placeholders (CIRRUS1_SEQ_* in cirrus1_config.h).
- 2 Standby: TECs, HV, wick and return off; filter on; fans low; both LEDs 20%; glass 0%.
- 3 Prime: filter and return on and a 5 s wick prime, then Standby after 60 s. Blocked while TECs are on.
- 4 Runup (from Standby): wick wetting, filter and return on, TEC 30% through the normal TEC checks; the Sequence panel shows plate B against the -20 C ready mark. If the TECs do not start, the wick and return stop and the mode stays Standby.
- 6 Shutdown: HV and glass off, wick drain, filter and return on, TECs ramp down 10% every 30 s to off (permission kept); lights and fans unchanged.
- 1 Tests and 5 Go stay unavailable. Sequences advance only while Run is on screen.
- Validation not run: no builds, tests or rollback application.


## 0.6.12

- Run protection (placeholder limits in cirrus1_config.h, to be confirmed): a non-zero TEC turns the TEC fans (L4) on first and only starts with valid CB and TEC2 readings and CB at or below 25 C (chiller ready).
- While any TEC is on from Run, a TEC trip turns all TECs off and revokes TEC permission when: TEC fans are off, TEC2 heat-sink is above 60 C, CB is above 35 C, any plate RTD is outside -50..45 C, or CB/TEC2 read faulty twice in a row. Fans and other outputs keep running. The reason stays on screen until the next TEC start.
- Leaving Run (ESC) or losing the connection with a TEC on stops all four TECs (permission kept), because protection only runs while Run is on screen.
- Fan stall: a settled intake or exhaust fan reading below 200 RPM on three 1 s reads in a row stops both fans.
- New Run prompts: g glass heater (0-50%, capped because the glass has no sensor), c TEC fans (0/1; cannot go off while a TEC is on), p pumps (0 off, 1 filter, 2 return, 3 both), w wick (0 stop, 1 prime, 2 wet, 3 drain).
- Outputs panel shows live VSENSE for TEC boards A-D (1 Hz), HV as estimated kV, and TEC fans ON/off. Status panel shows chiller READY/NOT READY, the trip limits and the last TEC trip. Pumps panel names the wick mode.
- Footer now lists all keys on two lines; the terminal settings line moved to docs only.
- Validation not run: no builds, tests or rollback application.


## 0.6.11

- Add the yellow Zaptronics bolt (U+26A1) before CIRRUS1 in the Run title bar. It is treated as two columns wide so the bar still fits 120 columns.
- Validation not run: no builds, tests or rollback application.


## 0.6.10

- Requires local ZapEE 0.13.2.
- Fix the low fan preset not locking: before closed loop, set both fans' minimum drive to 10% (EMC2303 default 40% held them above the 20% preset, so they ran past 2000 RPM).
- Show free heap RAM in the Run title bar (yellow below 32 KB).
- Validation not run: no builds, tests or rollback application.


## 0.6.9

- Remove the timed full redraw. Run clears and redraws the whole screen on entry and when F5 (ESC [ 1 5 ~) is pressed; otherwise only changed rows are sent.
- F5 is documented in docs/RUN_CONSOLE.md only, not on screen; the footer no longer mentions redraws.
- Validation not run: no builds, tests or rollback application.


## 0.6.8

- Redesign Run as coloured, bordered panels (Sequence, Temperatures, Status, Outputs, Fans, Pumps, Modules and loads) with a title bar showing version, TEC permit and STOP state.
- Screen size is now 120 columns by 40 rows; box lines use UTF-8, so the terminal must use UTF-8.
- Temperatures are colour-banded (blue < -20, cyan < 5, green < 30, yellow < 45, red above) for reading at a glance; these are not limits.
- Full redraw interval changed from 10 s to 60 s.
- Row changes are tracked by hash instead of a full copy; each refresh sends at most 12000 bytes and finishes remaining rows on later ticks, staying under the 16 KiB network buffer.
- Validation not run: no builds, tests or rollback application.


## 0.6.7

- Replace the Run temperature lines with a panel: plate map (W2 / L1 B | C L2 / W1) laid out as in notes/cirrus1-configuration.md.
- Show CB chiller return, TEC2 heat-sink and the ZTM_ADC board (CJC) temperatures.
- Show plate spread (hottest minus coldest plate RTD) and min..max per sensor since Run was opened.
- Compact the module initialization list to four per row to make room.
- Validation not run: no builds, tests or rollback application.


## 0.6.6

- Add Run fan presets from notes/cirrus1-configuration.md: f then 0 off, 1 low 20%, 2 moderate 50%, 3 high 90%.
- Presets start intake (fan 6) and exhaust (fan 3) open loop, then after five seconds lock both to the slower fan's RPM (closed loop).
- If tach reads or targets fail, both fans stay at the same open-loop duty; a failed preset write stops both.
- Show both fan RPMs, the preset, phase and shared RPM target on the dashboard; STOP resets the preset.
- Map RT2 = C and RT3 = B RTDs, and K-type TC0 = CB (chiller return pipe) and TC1 = PSU_TEC 2 heat-sink.
- Run reads one temperature sensor every 0.5 s (eight sensors).
- Validation not run: no builds, tests or rollback application.


## 0.6.5

- Run keys: only Space and 7 request STOP; other unassigned keys, Enter and arrow/function keys are ignored.
- Add Run manual prompts from notes/cirrus1-configuration.md: t (TEC, all four), h (HV), l and r (LEDs), entered as 0-100%.
- TEC % maps linearly to DAC 0-255 on all four channels; requires operator permission; 0% stops channels and keeps permission.
- HV 1-100% spans the calibrated nominal range; 0% turns the converter off.
- ESC inside a prompt cancels it; Space still requests STOP while a prompt is open.
- Show W1, W2, L1 and L2 RTD temperatures, reading one sensor per second.
- Validation not run: no builds, tests or rollback application.


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
