# Four-channel TEC cooling and console update

## Hardware ownership

`cooling.c` now owns the four independent TEC output channels as one subsystem.
Each channel contains an MCP4901 DAC with its own external chip-select and enable
line. All four DACs use SPI0; they are not a serial daisy chain. The managed SPI
layer changes between the L6470 mode-3 configuration and MCP4901 mode-0
configuration whenever required.

The onboard TLA2518 remains on SPI1. The current PCB mapping used by this patch is:

| TEC channel | TLA2518 input |
|---|---|
| TEC 1 | AIN5 |
| TEC 2 | AIN4 |
| TEC 3 | AIN3 |
| TEC 4 | AIN2 |

The `130 kOhm / 20 kOhm` divider is converted with a `7.5:1` scale factor. With a
3.3 V ADC reference, the nominal measurable TEC-output range is approximately
0 to 24.75 V.

## Console commands

```text
tec set <1..4> <0..255>
tec set-all <0..255>
tec read <1..4|all>
tec stop [1..4]
```

The previous command remains as a compatibility alias:

```text
tec start <value>             # TEC 1
tec start <1..4> <value>
```

`tec set` disables the selected power stage, writes the raw DAC code, enables
the stage, waits 100 ms for the supply and VSENSE follower to settle, then reads
and prints the corresponding VSENSE value.
If VSENSE cannot be verified, the channel is disabled and its DAC is placed in
shutdown.

`tec set-all` first disables all four stages, programs all four DACs, enables all
four, waits 100 ms, and reads all four VSENSE channels. If any write, enable, or
readback fails, all four channels are stopped.

`tec stop <channel>` lowers that module's external enable line first, then
sends the MCP4901 shutdown frame as a secondary safe state. The external enable
line is the authoritative power-off control. `tec stop` with no channel applies
the same sequence to all four channels. The root `safe` command also explicitly
forces every fitted TEC enable low before sending DAC shutdown frames.

## Suggested incremental bench test

```text
module list
tec stop
tec read all

tec set 1 10
tec read 1
tec stop 1

tec set 2 10
tec set 3 10
tec set 4 10
tec read all
tec stop

tec set-all 10
tec read all
tec stop
```

After confirming channel order and scaling at a low code, repeat with larger DAC
codes while watching supply current and the physical output with a multimeter.
