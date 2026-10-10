# FlySky FS-i6X Controller Layout & Operator Guide

> **Safety Notice:** Do not connect this wiring to the old ESP32-only firmware. Flash both the Teensy 4.1 body controller and the AstroPixels Plus ESP32 with the current firmware before combined testing; nothing counts as bench-verified until the commissioning record is filled in.

This guide details the function, channel mappings, and operational procedures for every switch, knob, and stick on your **FlySky FS-i6X 10-Channel 2.4GHz Transmitter** when operating R2-D2.

---

## 1. Visual Layout & Controls Overview

![FlySky FS-i6X Controller Layout](docs/images/flysky_fs_i6x_layout.png)

```
       [ SwA ] (2-pos)                         [ SwD ] (2-pos)
     Drive Safety Arm                        Auto Dome Enable
        UP: Disarmed                            UP: Manual only
       DOWN: Armed                             DOWN: Auto wandering
           \                                       /
      [ SwB ] (2-pos)                         [ SwC ] (3-pos)
     Macro Fire Trigger                      Speed Rate Cap
       UP: Idle                                UP: 35% (Slow)
     DOWN: Fire Selected Macro                MID: 70% (Cruise)
                                             DOWN: 95% (Full)
           \         ( VrA )         ( VrB )       /
                     Mood/Macro       Unused
                      Selector        (CH10)
                      (13 pos)           |
                         \              /
            ┌──────────────────────────────────┐
            │   [Left Stick]    [Right Stick]  │
            │   Dome Rotation     Foot Drive   │
            │   ◄- CCW  CW -►   ◄-Turn  Turn-► │
            │                    ▲ Fwd   Rev ▼ │
            │                                  │
            │          ┌────────────┐          │
            │          │  LCD Screen│          │
            │          └────────────┘          │
            │          [Power Switch]          │
            └──────────────────────────────────┘
```

---

## 2. Quick Reference Summary Table

| Control | Type | iBus Channel | Function | Default Safe Position |
| :--- | :--- | :--- | :--- | :--- |
| **Right Stick (Y)** | Vertical Axis | **CH2** | **Forward / Reverse Drive** | Centered (Stop) |
| **Right Stick (X)** | Horizontal Axis | **CH1** | **Left / Right Steering** | Centered (Straight) |
| **Left Stick (X)** | Horizontal Axis | **CH4** | **Dome Rotation** (Left=CCW, Right=CW) | Centered (Stop) |
| **Left Stick (Y)** | Vertical Axis | **CH3** | *Unused* (Retired manual tilt) | Centered |
| **Switch SwA** | 2-Position Toggle (Top Left) | **CH6** | **Drive Safety Arm** (UP=Disarmed, DOWN=Armed) | **UP** (Disarmed) |
| **Switch SwC** | 3-Position Toggle (Inner Right) | **CH5** | **Speed Rate Cap** (UP=35%, MID=70%, DOWN=95%) | **UP** (35% Slow) |
| **Knob VrA** | Rotary Dial (Left Knob) | **CH7** | **Mood & Macro Selector** (13 Positions) | **Pos 1** (Dome Home) |
| **Switch SwB** | 2-Position Toggle (Inner Left) | **CH8** | **Macro Fire Trigger** (Flip DOWN to activate) | **UP** (Idle) |
| **Switch SwD** | 2-Position Toggle (Top Right) | **CH9** | **Auto Dome Enable** (UP=Off, DOWN=On) | **UP** (Off) |
| **Knob VrB** | Rotary Dial (Right Knob) | **CH10** | *Unused* (Reserved for expansion) | Any |

---

## 3. Control Operations & Detailed Functions

### 🕹️ Right Stick — Foot Drive & Steering
The right stick controls R2-D2's feet using single-stick arcade drive:
* **Push UP:** Drive forward.
* **Pull DOWN:** Drive in reverse.
* **Push LEFT:** Steer left (spins left wheel backward, right wheel forward when stationary).
* **Push RIGHT:** Steer right (spins right wheel backward, left wheel forward when stationary).
* **Release to Center:** Both wheels brake at the commissioned brake current. Partially easing off the stick slows the wheels at the commissioned slew rate rather than stepping the duty down, so a tall droid is not pitched forward.
* **On-Board Mixing:** The Teensy 4.1 controller automatically mixes throttle and steering in real-time at 50Hz, scaling both wheels proportionally so turns remain smooth without spinning out or clipping.

---

### 🕹️ Left Stick — Dome Rotation
The left stick gives direct manual control over the dome's continuous rotation servo once the servo neutral has been commissioned and saved (until then the dome servo receives no signal at all):
* **Push LEFT:** Spin dome counter-clockwise (CCW).
* **Push RIGHT:** Spin dome clockwise (CW).
* **Release to Center:** The dome halts and holds its position.
* **Speed Modulation:** The further you push the stick, the faster the dome spins. Gentle deflection gives smooth, slow cinematic rotation.
* **Up / Down Axis (CH3):** Unused. Manual holo projector tilt was retired to prevent servo strain; holos are automated.

---

### 🛡️ Switch SwA — Drive Safety Arm (Motion Interlock)
Located on the far top-left. This is your primary safety switch:
* **UP (<= 1250µs): DISARMED (Safe State)**  
  The motor controllers (VESCs) are completely inhibited. The droid will not move even if you bump the right stick. Always start with SwA UP.
* **DOWN (>= 1750µs): ARMED (Drive Enabled)**  
  Enables foot drive.
* **500ms Neutral Arming Interlock:**  
  To prevent unexpected movement, the controller will **not** arm if the stick is pushed while flipping the switch! You must:
  1. Have SwA UP.
  2. Leave the right stick completely centered.
  3. Flip SwA DOWN.
  4. Keep the stick centered for at least **500 milliseconds**.  
  Only then will drive engage. If a motor fault or radio hiccup occurs, SwA automatically disarms and requires repeating this sequence.

---

### ⚡ Switch SwC — Speed Rate Limiter (Triple Rates)
Located on the inner top-right. On a stock FS-i6X, SwC is the only 3-position switch, so assign it to **CH5** in the transmitter's Aux Channels menu. It sets the maximum driving speed (a lower cap is approached at the commissioned slew rate, not stepped to):
* **UP (Position 1, <= 1250µs): 35% Speed (Low / Safe Mode)**  
  Capped at 35% duty. Perfect for indoor driving, navigating crowded events, narrow doorways, and workbench testing.
* **CENTER (Position 2, ~1500µs): 70% Speed (Cruising Mode)**  
  Capped at 70% duty. Smooth, comfortable outdoor cruising speed.
* **DOWN (Position 3, >= 1750µs): 95% Speed (High Performance)**  
  Capped at 95% maximum electrical duty. Full acceleration and speed for open pavement.

---

### 🎭 Knob VrA — Mood & Routine Selector (13 Positions)
Located on the top face, left knob. Rotating this knob selects which sound, mood, or autonomous routine is queued:

| Position | Dial Turn | Routine Name | Sound / Action Description |
| :---: | :--- | :--- | :--- |
| **1** | Full CCW | **Home Dome** | Automatically rotates dome to 0° Front center using Hall sensor (`:DMH` + Track 11) |
| **2** | ~8% | **Movie Normal** | Standard inquisitive movie chatter (Bank 1) |
| **3** | ~15% | **Happy Melodic** | Cheerful, upbeat melodic trills and chirps (Bank 3) |
| **4** | ~23% | **Scream Routine** | Loud panic scream (`:SE01` + Track 102) with rapid holo-projector twitching and red LED flicker |
| **5** | ~31% | **Cantina Band** | Plays full *Cantina Band* song (`:SE05` + Track 106) with choreographed holo-projector dance |
| **6** | ~38% | **Princess Leia** | With SwD ON, aligns dome forward first; then plays the 1977 Leia distress message (`:SE08` + Track 109) with blue holo flicker |
| **7** | ~46% | **Star Wars Disco** | Upbeat disco celebration (`:SE09` + Track 110) with rainbow logic display animations |
| **8** | ~54% | **Short Circuit / Faint** | R2 plays power-down sound (`:SE06` + Track 107), logic lights black out, dome halts |
| **9–13** | ~62% to Full CW | **Chatter Banks** | Plays a random astromech conversational whistle or chirp |

The 13 positions are equal ~77µs bands over 1000–2000µs, so position 13 is reachable with the knob fully clockwise.

---

### 🎯 Switch SwB — Routine Trigger
Located on the inner top-left; assign it to **CH8**. This switch executes whatever routine is selected on **Knob VrA**:
* **UP (Idle):** Resting position.
* **Flip DOWN (> 1750µs):** **FIRES** the selected routine!
* **Reset:** To trigger another routine, return SwB to the UP position, adjust knob VrA if desired, and flip DOWN again.

---

### 🤖 Switch SwD — Autonomous Dome Wandering
Located on the far top-right:
* **UP (<= 1250µs): OFF**  
  Autonomous dome wandering is disabled. The dome only rotates when you touch the left stick.
* **DOWN (>= 1750µs): ON**  
  Autonomous lifelike behavior enabled once auto dome is commissioned (references and timing saved). When R2-D2 is parked and untouched for more than **20 seconds**, the dome homes to the front, then glances up to ±45° and returns, like a real astromech.
* **Manual Override:** Touching the left stick or driving immediately cancels autonomous movement and hands full control back to you.

---

## 4. Safe Operating Procedure (Power-On Checklist)

Follow this routine every time you operate R2-D2:

1. **Pre-Flight Switch Check:**
   - SwA **UP** (Disarmed)
   - SwB **UP** (Trigger Released)
   - SwC **UP** (35% Low Speed)
   - SwD **UP** (Auto Dome Off)
   - Both joysticks resting at center.
2. **Turn On Transmitter:** Slide the center power switch UP. Confirm the LCD screen illuminates and battery voltage is above 9.0V.
3. **Turn On Droid:** Engage R2-D2's master cutoff switch. Verify logic displays light up; the startup chime (Track 255) plays once the dome and body link up.
4. **Arming to Drive:**
   - Ensure wheels are on the ground in an open area.
   - Keep right stick centered.
   - Flip **SwA DOWN**.
   - Wait **1 second** (Teensy verifies 500ms neutral stick handshake).
   - Gently push the right stick forward to drive!
5. **Emergency Stop (E-Stop):**
   - **Stop:** Release the right stick (wheels brake at the commissioned brake current).
   - **Kill Motion:** Flip **SwA UP** (disarms both feet; they brake and stay disarmed).
   - **Web STOP:** The dashboard STOP button latches a body STOP for feet and dome. It stays latched until **Release STOP** is pressed with SwA UP and all sticks centred for 500ms.
   - **Physical Cutoff:** Push the master cutoff switch on the droid's body.

---

## 5. Transmitter Failsafe Verification

The FS-iA6B receiver is programmed with a fail-safe configuration:
* If the transmitter runs out of batteries or goes out of range, the receiver automatically outputs:
  - CH1 (Steering) = `1500µs` (Centered)
  - CH2 (Throttle) = `1500µs` (Centered)
  - CH4 (Dome) = `1500µs` (Centered)
  - CH6 (SwA) = `1000µs` (UP / DISARMED)
  - CH8 (SwB) = `1000µs` (UP / Idle)
  - CH9 (SwD) = `1000µs` (UP / Off)
* FlySky receivers keep sending iBUS frames after signal loss, carrying these failsafe values, so the Teensy sees CH6 OFF and disarms both feet. Without the CH4 failsafe the receiver would hold the last dome stick position and keep the dome turning.
* If frames stop entirely (receiver unplugged or unpowered), the Teensy treats RC as stale after **250ms** and disarms. Re-arming always needs SwA UP, then DOWN, with the sticks centred for 500ms.

### Guided radio check (`/commissioning`)

The dome's commissioning page has a **Start radio check** button. It walks through every control and then the failsafe, so you can confirm the channel mapping above without a laptop. Keep the wheels raised (SwA goes DOWN during the check), and reload the page to see each new prompt. Complete each prompt within 15 s:

1. Right stick UP (CH2), then right stick RIGHT (CH1), then left stick RIGHT (CH4).
2. SwA DOWN (CH6), SwC through all three positions (CH5), SwB DOWN (CH8), SwD DOWN (CH9).
3. VrA knob fully one way and then the other (CH7).
4. Set SwA, SwB and SwD DOWN, then **turn the transmitter OFF**.

The check passes when the receiver's failsafe frames show the sticks centred and CH6/CH8/CH9 low. If the receiver stops sending frames instead, the result reads "receiver stops output". The Teensy still disarms on stale radio, but the receiver has no failsafe values set. Wrong values fail the check: set the failsafe as listed above and run it again. Turn the transmitter off for the last step. Flipping the switches back UP with the transmitter on can look like a pass. The **Audio check** button next to it plays track 255 and passes when the DFPlayer confirms playback.
