# CLAUDE.md

Guidance for Claude Code when working in this repository.

## What this is

A single-file Arduino sketch for an **ESP32** that drives an RC toy tractor (*trator*)
from a **PS4 DualShock** gamepad over Bluetooth, using **Bluepad32**.

Everything lives in [controle_trator_ps4.ino](controle_trator_ps4.ino). There is no
build config, no git repo, and no tests — it is built and flashed from the Arduino IDE.

`.skip.esp32s2` is an empty marker file inherited from the Bluepad32 examples: it tells
the build tooling to skip the ESP32-S2 target (that chip has no Bluetooth).

## Build / flash

Requires the **Bluepad32 fork of the ESP32 Arduino core** (`esp32-bluepad32`, currently
4.1.0 installed), *not* the stock `esp32` core — Bluepad32 replaces the Bluetooth stack,
so the sketch will not compile or connect against the standard core. `Bluepad32.h` ships
with that board package; `ESP32Servo` is a separate library that must be installed.

In the Arduino IDE, select **ESP32 Dev Module** from the *esp32-bluepad32* package.
FQBN for `arduino-cli`, if used:

```bash
arduino-cli compile --fqbn esp32-bluepad32:esp32:esp32 controle_trator_ps4.ino
```

Serial monitor runs at **115200**.

To make a controller pair, put the DualShock in pairing mode (Share + PS). If a gamepad
refuses to reconnect, uncomment `BP32.forgetBluetoothKeys()` in `setup()`, flash, run
once, then comment it out again.

## Code layout

Most of the file is boilerplate copied verbatim from the Bluepad32 `Controller` example:
`onConnectedController` / `onDisconnectedController`, `dumpMouse`, `dumpKeyboard`,
`dumpBalanceBoard`, `processMouse`, `processKeyboard`, `processBalanceBoard`,
`dumpGamepad`, `processControllers`, `setup`, `loop`. Those paths are dead weight for
this project — only gamepads are ever connected.

**All project logic is in `dumpCar()`.** `processGamepad()` exists only to call it.

## Hardware map

| Constant     | Pin | Actuator                              |
|--------------|-----|---------------------------------------|
| `pinoMotor`  | 18  | Drive motor / ESC (`meuMotor`)        |
| `pinoBraco`  | 19  | Arm servo (`meuBraco`)                |
| `pinoConch`  | 21  | Bucket servo (`meuConch`)             |
| `pinoServo`  | 26  | Steering servo (`meuServo`)           |

Ranges, all in servo degrees:

- Drive motor is a continuous-rotation/ESC signal: `0` = reverse, `90` = stop, `180` = forward.
- Steering is limited to `ml`(32) .. `mr`(93) — these are mechanical end stops for this
  chassis. Do not widen them without checking the linkage.
- Bucket `L` moves within `minL`(70)..`maxL`(180); arm `R` within `minR`(80)..`maxR`(180).

## Control mapping

Buttons are read as an **exact bitmask comparison** (`ctl->buttons() == 16`), so pressing
two buttons at once matches nothing and the input is ignored. This is how it currently
behaves; changing it to bitwise tests (`ctl->buttons() & 16`) is a behavioral change, not
a cleanup.

| Input        | Mask | Effect                     |
|--------------|------|----------------------------|
| X            | 1    | drive backward             |
| Triangle     | 8    | drive forward              |
| L1 / L2      | 16 / 64 | bucket up / down (`L`)  |
| R1 / R2      | 32 / 128 | arm up / down (`R`)    |
| D-pad up/down| 1 / 2 | drive forward / backward  |
| D-pad left/right | 8 / 4 | steer to `ml` / `mr`  |
| Left stick X | —    | proportional steering, only when the D-pad is not steering |
| Right stick Y| —    | proportional throttle, only when neither the D-pad nor X/Triangle is driving |

Analog axes are `-511..512` and are mapped onto the servo ranges. Note the throttle map is
inverted (`180, 0`) so stick-forward means forward.

The arm and bucket move **incrementally**: each loop pass with the button held adds or
subtracts `step` (10). Their travel speed is therefore set by the `delay(150)` at the end
of `loop()` — that delay is the control tick rate, not idle padding. Shortening it makes
the arm and bucket noticeably faster.

## Conventions

- Identifiers and comments are in **Portuguese** (`meuBraco`, `para frente`, `sirene`).
  Keep new code in the same language.
- Serial logging via `Serial.printf` / `Serial.println` is the only debugging channel.

## Dormant code

Sizeable commented-out blocks are intentionally kept: a siren/buzzer routine (in
`dumpCar()` and `stopBuzzer()`), a `switch`-based version of the button handling, and
gamepad LED/rumble demos. **The buzzer code references `buzzerPin`, which is not
declared anywhere** — uncommenting it requires adding that pin definition first. The
unused globals `sirene` and `center` exist only to support those blocks.
