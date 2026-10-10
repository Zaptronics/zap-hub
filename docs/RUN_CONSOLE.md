# Run console, Hub 0.6.31

Requires local ZapEE 0.13.2. Enter `run` in the TCP terminal on port 2323.
Use an ANSI terminal of at least 120 columns by 40 rows with UTF-8 character
translation (PuTTY: Window > Columns 120, Rows 40; Window > Translation > UTF-8). Disable local line
editing and local echo (PuTTY Raw) so keys transmit immediately. Refresh is
approximately twice per second, skipping frames while output is pending.
The screen clears and fully redraws on entry and when F5 is pressed (PuTTY's
default ESC [ 1 5 ~). There is no timed full redraw. Otherwise each refresh
overwrites only changed rows using ANSI cursor positioning and erases obsolete
trailing characters; an unchanged refresh sends no bytes. Rows are padded or
clipped to 120 visible columns to prevent wrapping. Resize negotiation is not
implemented: after resizing or any screen corruption, press F5. F5 is not shown
on screen. Other function and arrow keys are ignored.

Ordinary command output is suppressed while the dashboard owns the terminal,
so STOP cannot scroll the screen. The STOP success/failure result still appears
in the dashboard status row. Control commands and TEC permission logic are
unchanged. No SSH or network transport changes are included.

Space and 7 request the existing global safe state. Space also works while a
prompt is open (where 7 is a digit). Every other key, including Enter and
arrow/function keys, is ignored, so a stray key cannot trigger STOP or change
outputs. An arrow marks STOP with an explicit failure message if unsuccessful.
Successful writes do not verify physical standstill. ESC alone returns to the
prompt after a short network-tick delay without changing outputs.
Connection loss detaches the screen without output changes. Reconnecting starts
at the normal prompt. Reopening run keeps the mode arrow and state (0.6.14). HTTP cannot enter screen mode.

## Manual prompts (0.6.5)

From notes/cirrus1-configuration.md. Press the key, type 0-100, then Enter.
Non-zero enables; 0 turns that output off. Backspace edits, ESC cancels.

- `t` TEC [n%]> : all four TEC channels together; % maps linearly to DAC 0-255.
  Requires `tec permit` first. 0% stops each channel but keeps permission.
  The result line shows the four VSENSE readbacks.
- `h` HV [n%]> : 1-100% spans the calibrated nominal range (about 1.57-5.48 kV,
  an estimate, not a direct measurement). 0% turns the converter off.
- `l` LED LEFT [n%]> and `r` LED RIGHT [n%]> : PWM loads L9 and L10.

The dashboard shows the TEC and HV values set from Run (reset by STOP). Since
0.6.6 it reads one sensor every 0.5 s (see Temperature panel), so a
conversion never delays STOP for long.

## Load currents (0.6.19)

The Outputs panel shows the measured current next to LED L (L9), LED R (L10),
Glass (L12) and TEC fan (L4), read the same way as `pwm current` (PAC195x,
averaged, 50 mOhm shunts). Run reads one load every 250 ms, so each value
refreshes about once a second while Run is on screen. Values under 1 A show in
mA, larger ones in A; `--` means the read failed. Currents are display only:
nothing trips on them yet. The BLED channels are not shown.

## Colours, alerts and TEC fan auto-on (0.6.30)

- Temperatures: green up to 40 C, orange 40-60 C, flashing red above 60 C
  (CIRRUS1_TEMP_ORANGE_C, CIRRUS1_TEMP_RED_C). CB is the exception: orange until
  it reaches the safe TEC run zone (4 C or below), then green.
- Flashing red also marks sensor faults (O/C, ERR, noCJC, range), a fan below
  the stall speed and the fan that tripped the stall stop (STALL, cleared by
  the next `f` preset), and glass current under 10 mA while the glass is at
  20% or more (CIRRUS1_GLASS_OC_MA, CIRRUS1_GLASS_OC_MIN_PCT): likely an open
  heater. Lower glass duties often read 0 mA and are not flagged.
- TEC2 heat-sink above 40 C (CIRRUS1_TEC2_FANS_ON_C) turns the TEC fans (L4)
  on automatically, Run open or not, including after a STOP. It never turns
  them off. TEC2 is read at least every 4 s for this.
- Flashing needs blinking enabled in the terminal (PuTTY: Window > Appearance
  or Terminal settings, "Enable blinking text"); otherwise it shows steady red.

## CB interlock (0.6.21)

The TEC lock follows the CB thermocouple (Process Chiller return pipe):

- CB below 4 C grants the TEC permit automatically (title bar TEC PERMIT), as
  if `tec permit` had been typed. This happens once each time CB goes cold, so
  after a STOP, `safe` or `tec revoke` while CB is still cold the TECs stay
  locked until `tec permit` is typed or CB warms above 4 and cools again.
- CB above 4 C, CB open circuit, or another CB fault on two reads in a row
  revokes the permit and stops all four TEC modules, however they were
  started (Run, `tec` commands or a sequence). Fans, pumps and the other
  outputs keep running. The reason shows as a TEC TRIP.
- A `tec permit` typed while CB is above 4 C is revoked at the next CB read.

### K: permit from inside Run (0.6.22)

When the TECs are locked, `t` with a non-zero value no longer just refuses.
It asks for confirmation: press K within 10 s (CIRRUS1_TEC_CONFIRM_MS) to
confirm the chiller is at temperature and circulating. That grants the permit,
as `tec permit chiller-at-temperature-and-circulating` would, and starts the
TEC at the value just entered. K is refused without a valid CB reading or
with CB above 4 C (exactly 4.0 C is accepted). K on its own,
with nothing pending, just grants the permit. Any
STOP (Space, 7 or `safe`) clears a pending TEC value.

CB is read every 2 s from boot, whether or not Run is open; outside Run, grants
and trips print as a `[run]` line on the console. Exactly 4.0 C changes
nothing. The limits are CIRRUS1_CB_UNLOCK_C, CIRRUS1_CB_LOCK_C and
CIRRUS1_CB_PERIOD_MS in cirrus1_config.h.

## Leaving and returning (0.6.14)

Leaving Run does not change outputs. If TECs, HV or a timed sequence (Prime,
Runup, Shutdown) are running, the first ESC asks "Leave Run ... still running?"
and a second ESC within 3 s leaves; any other key cancels. Losing the
connection leaves outputs as they are too.

While Run is off screen, Hub's main loop keeps Run's protection going: sensors
and fan tachs are still read, TEC trips and the fan stall stop still apply, and
Prime and the Shutdown ramp still advance. Anything that happens is printed on
the normal console as `[run] <message>`. Type `safe` at the prompt for a full
STOP; Run then shows STOP and cancels any sequence (0.6.15), so nothing is
turned back on. The background work holds the network lock while it runs, so
it never overlaps a typed command. Entering `run` again shows the same mode, outputs, min..max ranges and
the last notice, including a trip that happened while you were away.

## Sequences (0.6.13)

Keys 2, 3, 4 and 6 start a sequence after a second press of the same key
within 3 s (the first press only shows what will happen). Every value is a
PLACEHOLDER in cirrus1_config.h (CIRRUS1_SEQ_*). Space or 7 still stops
everything at any time. Sequences keep advancing after Run is left, and
re-entering Run shows the active mode.

- 2 Standby: TECs off (permission kept), HV off, wick and return off, filter on,
  fans low preset, both LEDs 20%, glass 0%.
- 3 Prime: filter and return on, wick prime (forward fast for 5 s). After 60 s
  Run applies Standby. Refused while any TEC is on.
- 4 Runup: only from Standby. Wick wetting (continuous), filter and return on,
  then TEC 30% through the same checks as `t` (permit, TEC fans, chiller
  ready). The Sequence panel shows plate B against the -20 C ready mark. If the
  TECs do not start, the reason is shown, wick and return stop and the mode
  stays Standby. The TEC DAC direction is still unconfirmed: watch B.
- 6 Shutdown: HV and glass off, wick drain (reverse fast for 5 s), filter and
  return on, then the TECs step down 10% every 30 s to off (permission kept).
  The ramp skips the start checks so a warm chiller cannot hold the TECs on;
  the trips still apply. Lights and fans are left as they are.
- 1 Tests and 5 Go remain unavailable: no ZapLink checks and no running
  targets yet.

## Protection and more prompts (0.6.12)

All limits are PLACEHOLDERS in cirrus1_config.h (CIRRUS1_TRIP_*,
CIRRUS1_CB_READY_C, CIRRUS1_FAN_STALL_*) until confirmed.

TEC start (`t` with a non-zero value) still needs `tec permit`. Run now also
turns the TEC fans (L4) on first, then requires a valid CB reading at or below
4 C (chiller ready; 0 C in 0.6.21-0.6.30, 25 C before) and a valid TEC2 reading below its trip. If a check fails
the TECs are not started and the reason is shown; the TEC fans stay on.

While any TEC is on from Run, a TEC trip turns all four TECs off and revokes
permission (fans, pumps, LEDs and HV keep running) when any of these holds:

- TEC fans (L4) are off.
- TEC2 heat-sink is above 60 C, or CB (chiller return) is above 35 C.
- Any plate RTD reads above 45 C (plate heating, for example a reversed DAC
  direction) or below -50 C.
- CB or TEC2 reads faulty (ERR, open, range) twice in a row.

The trip reason stays in the Status panel ("Last TEC trip") until the next TEC
start. Re-grant with `tec permit` outside Run. Protection keeps running after Run is
left (see Leaving and returning). Plate RTD faults are shown but do not trip.

Fan stall: once a preset has settled, an intake or exhaust fan reporting a
valid tach below 200 RPM on three 1 s reads in a row stops both fans so there
is no pressure differential. An invalid tach is treated as unknown, not stalled.

New prompts:

- `g` GLASS [n%]> : ITO glass heater, load L12, 0-100% (the 50% cap was
  removed in 0.6.20). The glass has no temperature sensor; watch its current
  in the Outputs panel.
- `c` TEC FANS 0-1 [n]> : load L4 at 100% or off. It refuses off while a TEC
  is on.
- `p` PUMPS 0-3 [n]> : 0 filter and return off, 1 filter only, 2 return only,
  3 both, at the fixed speeds in cirrus1_config.h (100 steps/s).
- `w` WICK 0-3 [n]> : 0 stop, 1 prime (forward 100 steps/s for 5 s), 2 wet
  (forward 30 steps/s, continuous), 3 drain (reverse 100 steps/s for 5 s).

The Outputs panel shows VSENSE for TEC boards A-D once a second (use it to
compare boards and confirm the DAC direction), HV as an estimated kV, and the
TEC fans state. The footer lists every key on two lines.

## Colour layout (0.6.8)

Run draws bordered, coloured panels: a title bar (yellow Zaptronics bolt
before CIRRUS1, drawn two columns wide; free heap RAM, version, TEC
permit, STOP state), Sequence, Temperatures, Status (notices and the open prompt), Outputs,
Fans, Pumps, and Modules and loads, with a key bar at the bottom. Zero values
are dim and active values bright. Temperatures are colour-banded (0.6.30; see
"Colours, alerts and TEC fan auto-on" below). Each refresh sends at most 12000 bytes;
remaining changed rows follow on the next ticks, keeping output below the
16 KiB network buffer.

## Fan presets and temperatures (0.6.6)

`f` FANS 0-3 [n]> : 0 off, 1 low 30%, 2 moderate 50%, 3 high 90% PWM.
Intake (fan 6) and exhaust (fan 3) must share one RPM to avoid a pressure
differential. A preset first drives both open loop at its duty. After five
seconds Run reads both tachs and sets both closed-loop targets to the slower
fan's RPM, so both fans can reach it. Before that, both fans' closed-loop
minimum drive is set to 20% (EMC2303 default 40%), so the 30% low preset can
lock instead of running past 2000 RPM. (0.6.16: low was 20% with a 10% floor,
which surged.) If a tach read or target write fails,
both stay at the same open-loop duty and the status row says so. If a preset
write fails, both fans are stopped. The dashboard shows both RPMs, the preset,
its phase (settling, matched, open loop) and the shared target. STOP resets
the preset; fan commands typed outside Run are not reflected in it.

## Temperature panel (0.6.7)

The plate RTDs are drawn as in notes/cirrus1-configuration.md, looking from
the edge with the AC inlet on the far side (A and D have no RTD fitted):

                              W2 21.3
      L1 21.3     (A)   B 21.3    |  C 21.3     (D)   L2 21.3
                              W1 21.3

Below it: CB (Process Chiller return pipe, TC0), TEC2 (PSU_TEC 2 heat-sink,
TC1) and the ZTM_ADC board cold-junction temperature; the plate spread
(hottest minus coldest plate RTD); and min..max for every sensor since Run was
opened, to help choose operating thresholds. Faults show as ERR, O/C (open
circuit), noCJC or range in flashing red; -- means not read yet. Run reads one sensor every 0.5 s. Since
0.6.21 CB is read every 2 s for the CB interlock (below), and the other
sensors share the remaining slots, refreshing about every 5 s.

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

Modes 1 and 5 only show unavailable messages; 2, 3, 4 and 6 need two presses
(see Sequences). ESC is byte 0x1B and exits after the existing short delay. Enter is
ignored on the dashboard and applies the value inside a prompt.

## Remaining implementation (as of 0.6.16)

Confirmed on hardware: Run up to 0.6.15 brings up on the assembled chamber, and
the 0.6.16 fan presets (low 30%, 20% floor) hold a stable shared RPM.

Still open:
- Every protection limit and sequence value in cirrus1_config.h
  (CIRRUS1_TRIP_*, CIRRUS1_CB_READY_C, CIRRUS1_FAN_STALL_*,
  CIRRUS1_SEQ_*) is a placeholder awaiting confirmed values.
- TEC DAC direction (does a higher % cool harder?) is not yet confirmed; Runup
  and the Shutdown ramp assume it does.
- 1 Tests needs a ZapLink check interface and pass criteria; 5 Go needs
  running targets (TEC, glass, HV, lights) and a Runup completion rule.
- TEC balancing to board B's VSENSE, temperature logging and a
  chamber temperature regulator are not implemented.
- The stall check only catches slow fans. A fan whose PWM wire is loose runs at
  full speed and is not flagged.
- RT0, RT1 and TC2 are unconnected.

Hub 0.6.0 separately requires explicit TEC operator permission; see
TEC_OPERATOR_INTERLOCK.md. Run adds its own start checks and trips on top.

Validation not run: no builds, tests, hardware regression or rollback application.
No Git operations. Reversible unified patches are in each project's patches folder.
