# Bring-Up Checklist

First assembly, in order. Each stage passes before the next starts. Tick each box as you go.

## Stage 1: Power (no electronics connected)

Guide: [Power](POWER.md)

- [ ] Main fuse within 150mm of battery positive, master cutoff reachable from outside
- [ ] Terminal blocks continuity-checked and labelled; no positive-to-ground continuity
- [ ] All six fuse-block fuses and all four 5V fuses removed
- [ ] Main bus on: fuse block reads battery voltage
- [ ] F3 and F4 fitted: body and dome buck outputs each read 4.9–5.1V
- [ ] Body and dome 5V positives are not connected to each other
- [ ] Power off

## Stage 2: Receiver and transmitter

Guide: [Transmitter Setup](TRANSMITTER_SETUP.md)

- [ ] Receiver wired to B-LOGIC; B-LOGIC (2A) fitted
- [ ] Model created, type Airplane/glider, Mode 2
- [ ] Receiver bound
- [ ] Aux channels CH5–CH10 assigned
- [ ] Output mode PWM, serial i-BUS

## Stage 3: Drive (wheels off the ground)

Guide: [Drive Setup](DRIVE_SETUP.md)

- [ ] CAN switch OFF
- [ ] Half A reads receiver CH1 in VESC Tool
- [ ] Half B receiver lead soldered; continuity checks passed
- [ ] Half B reads receiver CH2 in VESC Tool
- [ ] 5V pins removed from both receiver leads
- [ ] FOC Hall detection done on both halves
- [ ] Wheel direction set in VESC Tool: positive duty rolls each wheel forward
- [ ] Current, voltage and PPM settings written on both halves; settings record filled in
- [ ] Elevon mix on; PPM mapping done on both halves
- [ ] Failsafe set: CH1, CH2, CH4 centred, CH5 (SwA) up
- [ ] Safety tests 1–7 passed, including centre-stick braking (test 2)
- [ ] Floor test 8 passed

## Stage 4: Dome electronics

Guide: [Dome Setup](DOME_SETUP.md)

- [ ] Firmware flashed; `#APZERO` done
- [ ] D-LOGIC (3A) fitted: logics, PSIs and holo LEDs work
- [ ] D-SERVO (5A) fitted: six holo servos added one at a time, none binding
- [ ] iBUS through slip ring CH3: SwC up centres the holos, SwC middle sets them moving

## Stage 5: Sound

Guide: [Sound Setup](SOUND_SETUP.md)

- [ ] SD card prepared: FAT32, `mp3` folder, numbered files
- [ ] DFPlayer serial through slip ring CH4/CH5
- [ ] F5 (5A) fitted
- [ ] Start-up sound plays
- [ ] SwB plays a mood sound; VrA changes the mood; VrB sets the volume
- [ ] SwC middle: R2 makes sounds on its own within 2 minutes (VrA on General); SwC up stops them

## Stage 6: Dome rotation

Guide: [Dome Setup](DOME_SETUP.md)

- [ ] Magnet ring glued from the printed template: 13 magnets, same face up
- [ ] Dome sensor mounted: its LED blinks 13 times per turn, twice close together at front
- [ ] Dome servo signal through slip ring CH6; B-SERVO (5A) fitted
- [ ] Stick right turns the dome clockwise seen from above
- [ ] CH4 subtrim set: no creep with the stick centred
- [ ] Alive: the dome finds front, then looks around and returns to front
- [ ] Stick overrides the dome; driving brings the dome to front
- [ ] SwB: sound and gesture in Alive, front-holo flick in Aware
- [ ] Transmitter off: wheels stop, R2 goes Still

## Stage 7: Ready

- [ ] All shell holes grommeted, harnesses strain-relieved and labelled
- [ ] [Operating Guide](OPERATING_GUIDE.md) read by everyone who drives the droid
