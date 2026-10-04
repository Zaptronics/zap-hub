# TEC operator permission — Hub 0.6.0

PSU_TEC operation without Process Chiller coolant can destroy Cirrus1.
Every boot starts locked. An operator must physically verify that the Process
Chiller is at temperature and coolant is circulating, then enter exactly:

    tec permit chiller-at-temperature-and-circulating

`tec permit` alone shows status and instructions. Invalid or incomplete phrases
do not grant permission. Granting permission does not start any output. All four
modules must be ready, guarded and readable; a new grant requires enables low.
The existing terminal/HTTP command service is not an authenticated operator
identity system; the explicit phrase records an assertion, not measured flow.
Do not put this confirmation command into an automatic startup script.

Locked calls to tec set, set-all and start (including existing HTTP aliases)
are rejected even for DAC code zero, which would otherwise assert enable.
The cooling APIs also check permission. Hub installs a guard on each module's
physical enable callback so direct ztm_psu_tec_start/set_enable calls cannot
assert EN without permission. Permission checks and physical enable writes
are serialized with a mutex. Disabling remains available while locked.
ZapEE's reusable driver policy is unchanged; this is Cirrus1 application policy.
Initialization only disables outputs and installs the guard before exposing
the module. Init/reinit follows the adapter's existing quiescent-user contract.

`tec revoke`, `tec stop` without a channel, `safe`, dashboard 7 and emergency
keys revoke permission and independently attempt to disable all four enables,
read them back, and shut down available DACs. Errors remain errors; the lock
stays closed regardless. Failed starts/readbacks revoke and attempt all-off.
Reinitialization/deinitialization revokes and attempts all enable lines low.
An individual `tec stop N` stops that rail but retains permission for the others.
Permission is RAM-only; it never survives restart. ESC and TCP disconnect retain
permission and running outputs, consistent with leaving the console unchanged.
The next all-stop requires a fresh confirmation before any restart.

No run mode grants permission implicitly. Go is still separately locked pending
implementation of Runup and the process recipes. Confirmation alone cannot
unlock Go. The dashboard shows actual permission independently of recipe state.

This software interlock does not detect subsequent loss of coolant, provide a
hardware thermal cutout, or guarantee an OFF write succeeds during a bus/power
fault. Its status must not be interpreted as a live flow measurement.

Validation not run per project instructions: no builds, tests, hardware regression
or rollback patch application. No Git operations. Reverse patch available in
patches/Hub_0.6.0_tec-operator-interlock.patch.


## Run console checks (Hub 0.6.12 onward)

Run's `t` prompt and Runup keep this permission and add their own checks: TEC
fans (L4) are turned on first, and a start needs valid CB and TEC2 readings
with CB at or below CIRRUS1_CB_READY_C. While a TEC runs, Run's trips (see
docs/RUN_CONSOLE.md) turn all TECs off and revoke this permission. From 0.6.14
those checks keep running after Run is left. All limits are placeholders.
