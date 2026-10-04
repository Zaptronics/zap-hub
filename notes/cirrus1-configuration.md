# Introduction

This document describes how each module is wired today inside the Cirrus1 cloud chamber. Hub is the master controller, and we use every ztm currently available. 

Note: The following information is specific to the Cirrus1 and not necessarily applicable to the ZTM modules themselves in other projects.

Cirrus1 weights 18.5kg!

## ZTM_ADC

This module is currently being used only to measure RTD and TC temperature of various system components.

The primary point of interest being the 8 RTDs embedded in the copper cooling

This table might hurt more than help, but there are eight TEC modules 
arranged in a 4x2 grid (looking from the edge down, with AC inlet on the far side) A1 through D2.

|    |    |    |    |    |    |    |
|----|----|----|----|----|----|----|
|    |    |    | W2 |    |    |    |
|    | A2 | B2 |    | C2 | D2 |    |
| L1 | A  | B  |    | C  | D  | L2 |
|    | A1 | B1 |    | C1 | D1 |    |
|    |    |    | W1 |    |    |    |

There are 
- four RTDs positioned between each column pair, labelled A to D
- two at the extremes left and right L1 and L2 respectively.
- two at the vertical extends W1, and W2

Given the ZTM_ADC only has a total of 8 RTD channels we are not connecting all of the copper plate ones up in order to leave spares for temperature sensing in other parts of the system.

There are two additional k-type thermocouples. TEC monitors PSU_TEC number 2 heat-sink temp, and CB on the return pipe of the Aluminum heat exchanger for the Process Chiller loop.

CB can be used in place of the command interlock for the TEC modules.

Here is the mapping.

|ZTM_ADC |Sensor |
|--------|-------|
|  RT0   |  -    |
|  RT1   |  -    |
|  RT2   |  C    |
|  RT3   |  B    |
|  RT4   |  W2   |
|  RT5   |  W1   |
|  RT6   |  L1   |
|  RT7   |  L2   |
|  TC0   |  CB   |
|  TC1   |  TEC  |
|  TC2   |  -    |

- = not connected.


## PSU_TEC

Each board drives two TEC modules. With A, B, C, D in the above diagram, relating to boards 1 - 4.

For the time being, they should all be in sync, so need to be actively set to keep the voltage on Vsense approximately the same between all four. Perhaps using B as a reference, and slowly nudging the others to match it's Vsense?

This will keep the temperature gradient for all of them equal.

Till we work out the values, if t is pressed show a prompt TEC [0%]> where I can enter the values manually. Non zero implies enable.

## PSU_HV

High Voltage ion sweeper 1K-5KV range, when user pressed h show the prompt HV [0%]> where I can enter values manually. Non zero implies enable.

## ZTM_PWM

### Loads

|ZTM_PWM |Sensor |
|--------|-------|
|  L1    | BLED2 |
|  L2    | BLED1 |
|  L3    |       |
|  L4    | PSU_TEC FANS
|  L5    |       |
|  L6    |       |
|  L7    |       |
|  L8    |       |
|  L9    | LED Left
|  L10   | LED Right
|  L11   |       |
|  l12   | ITO Glass Heater

PSU_TEC FANS on module heat-sinks, all run together, they are not PWM so it's 0% or 100% for these. 

LED Left & Right. l key LED LEFT [0%]>   r key LED right [0%]>

### 4-wire fans
- PWM FAN 3 = Exhaust
- PWM FAN 6 = Intake

Intake and Exhaust fans should be run together at same RPM, this is to prevent creating a pressure differential. Low speed is 30% PWM, moderate 50%, and high is 90%


## ZTM_6470 (AKA Stepper)

Only three of the four drivers are being used. 

### IPO Filter loop. 

Can only operate in one direction.
stepper run 1 fwd 100

### IPO Wick.

Run forward fast to prime the wick
stepper run 3 fwd 100

Forward slowly to continously wet.
stepper run 3 fwd 30

Reverse fast, perhaps as a shutdown step.
stepper run 3 rev 100


### IPO Return
stepper run 2 fwd 100


