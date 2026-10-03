# Run console, Hub 0.6.4

Requires local ZapEE 0.13.1. Enter `run` in the TCP terminal on port 2323.
Use an ANSI terminal of at least 100 columns by 48 rows. Disable local line
editing and local echo (PuTTY Raw) so keys transmit immediately. Refresh is
approximately twice per second, skipping frames while output is pending.
The screen clears on entry and every ten seconds for resize recovery (deferred
while network output is pending). Between full redraws it overwrites only changed rows
using ANSI cursor positioning and erase obsolete trailing characters. All row
updates are sent together; an unchanged refresh sends no bytes unless a full
redraw is due. Long lines are
clipped to 99 columns to prevent wrapping. Keep the terminal at the stated
minimum size; resize negotiation is not implemented. Exit and re-enter `run`
to redraw immediately, or wait for the periodic redraw. Redrawing does not adapt
the fixed layout to a terminal smaller than the stated minimum.

Ordinary command output is suppressed while the dashboard owns the terminal,
so STOP cannot scroll the screen. The STOP success/failure result still appears
in the dashboard status row. Control commands and TEC permission logic are
unchanged. No SSH or network transport changes are included.

7 and any other data key except 1-6 and ESC request the existing global safe
state, including Enter. An arrow marks STOP with an explicit failure message
if unsuccessful. Successful writes do not verify physical standstill.
ESC alone returns to the prompt after a short network-tick delay without
changing outputs. A promptly received ESC-prefixed arrow/function-key sequence
requests STOP. Connection loss detaches the screen without output changes.
Reconnecting starts at the normal prompt. Reopening run clears the arrow because
manual commands may have changed outputs. HTTP cannot enter screen mode.

## Keys appear locally or do not respond

A visible `^[` when pressing ESC suggests the client is echoing control keys
and may be buffering input until Enter. Changing the exit key would not fix
buffering of the numbered keys or emergency STOP. In PuTTY, open Terminal and
set both Local echo and Local line editing to Force off. Leave them off at the
normal prompt too: ZapEE 0.13.1 echoes typed command characters and handles
Backspace/Delete on the server. Enter executes the normal command line, while
Run consumes keys immediately without echo. No setting changes are needed when
entering or leaving Run. This is line editing, not line-ending configuration;
CR, LF, CRLF and Telnet CR-NUL remain accepted by the normal prompt. For another client,
select its character-at-a-time/no-local-echo mode. Raw TCP has no negotiation
that can force these client settings; the current Telnet service also does not
negotiate character mode.

Modes 1-6 still only show unavailable/blocked messages; they do not operate the
machine. ESC is byte 0x1B and exits after the existing short delay. Do not press
Enter to send keys: Enter remains an emergency-stop key on this screen.

## Remaining implementation

This is the console foundation. Keys 1-6 do not change outputs. The project has
raw TEC controls and pump primitives, but no chamber temperature regulator,
or ZapLink all-board test service. Wiring is now recorded in cirrus1_config.h. Temperatures and currents
are explicitly unavailable. No Runup completion can yet be recorded, and Go is locked.
Hub 0.6.0 separately requires explicit TEC operator permission; see
TEC_OPERATOR_INTERLOCK.md. The dashboard displays permission status.

Required for the next change:
- Low light percentage, glass heater operating levels and intake/exhaust target RPM.
- Glass standby/running settings and sensor mapping.
- TEC sensor mapping, runup/running targets and operating/current limits.
- Wick wetting is continuous at 30 steps/s; no dosing interval is required.
- Prime sequence (wick prime now uses 100 steps/s; existing five-second timer retained); specify filter
  and sump behavior and duration for IPA loop priming.
- Shutdown ramp duration and policy; retain lights as requested.
- ZapLink check interface, fitted sensors and pass criteria.

Intended stages: Tests; Standby (filter, low fans/lights, ADC, mild glass heat);
Prime (IPA loop); Runup (wick, low TEC, monitoring and user chiller confirmation);
Go (only after Runup: full lights, glass/TEC running targets, wick and sump);
Shutdown (thermal ramp, retain lights); STOP. A second press of 4 can confirm the
chiller without giving any alphabetic key an exception to emergency-stop behavior.

Validation not run: no builds, tests, hardware regression or rollback application.
No Git operations. Reversible unified patches are in each project's patches folder.
