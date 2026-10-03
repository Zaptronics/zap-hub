# PWM fan fault report (Hub 0.3.9 / ZapEE 0.12.3)

Fan 3 visibly rotated at 10% despite tach count 0x1FFF, and reported 770 RPM
at 15%. After a 100% command, subsequent 40% commands failed. The log reported
POWER_SAFE read failure (-41), and pwm stop claimed success while the fan kept
running until machine power was removed.

## Confirmed source behavior

- The tach driver maps 0x1FFF to its STALL enum. This does not establish physical
  standstill; the CLI now says NO VALID TACH. The cause of the missing low-speed
  measurement is not established.
- The ZTPICO2 POWER_SAFE callback reads TLA2518 GPIO0 under the motherboard ADC
  lock. Invalid board state, lock acquisition failure, or a failed register read
  can fail that callback. -41 is a read failure, not a confirmed LOW input.
- The first fault clears ready, latches -41, and requests enable OFF. The enable
  callback result is discarded there, so the event does not confirm shutdown.
- Later polls return the latched error without retrying the POWER_SAFE read.
  Ordinary fan commands are blocked until explicit reinitialization.
- Previously stop_all treated a non-operational module as success without any
  output writes. ZapEE 0.12.3 returns false instead. Hub reports that zero duty
  was not confirmed, including the latched fault code.

## Scope and next investigation

This change corrects reporting; it does not fix the original read failure or
prove why the fan continued running. Timing after 100% does not by itself prove
100% PWM caused the failure. No automatic retry or fault bypass has been added.

The existing pwm power cycle command invokes explicit module reinitialization.
It may recover after the underlying condition clears, but recovery and fan
shutdown have not been verified. Reinitialization restores module defaults.

The next diagnostic change should distinguish ADC lock failure from TLA2518
transport failure and retain the enable-OFF callback result. Evidence from the
next occurrence can then guide a targeted fix rather than weakening POWER_SAFE.

Validation not run. The user performs all building and hardware testing.
