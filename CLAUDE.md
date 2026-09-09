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

### From the command line

There is no `arduino-cli` on PATH, but the Arduino IDE bundles one. Note the install path is
`AppData/Local/Programs/arduino-ide`, *not* `C:/Program Files/Arduino IDE`:

```
C:/Users/puera/AppData/Local/Programs/arduino-ide/resources/app/lib/backend/resources/arduino-cli.exe
```

It already resolves the right directories on its own — cores from
`AppData/Local/Arduino15`, libraries from `OneDrive/Documentos/Arduino` — so no
`--config-file` is needed. If that ever stops resolving, add
`--config-file "$HOME/.arduinoIDE/arduino-cli.yaml"`.

Run these from the sketch directory (the trailing `.`). In Git Bash, set the path once:

```bash
CLI="$HOME/AppData/Local/Programs/arduino-ide/resources/app/lib/backend/resources/arduino-cli.exe"
```

Compile only:

```bash
"$CLI" compile --fqbn esp32-bluepad32:esp32:esp32 .
```

Compile and flash (the board showed up on **COM8**; confirm with `"$CLI" board list`, since
the port changes between machines and USB ports):

```bash
"$CLI" compile --fqbn esp32-bluepad32:esp32:esp32 --upload -p COM8 .
```

Serial monitor at **115200** — the port must be free, so close the IDE's monitor first:

```bash
"$CLI" monitor -p COM8 -c baudrate=115200
```

**`arduino-cli monitor` is interactive and useless from a non-interactive shell.** It reads
stdin to forward keystrokes to the port, so with stdin closed it hits EOF and exits
immediately with status 0 and no output — it looks like the board said nothing. To read the
serial log programmatically, drive the port through .NET instead:

```powershell
$p = New-Object System.IO.Ports.SerialPort 'COM8',115200,'None',8,'One'
$p.DtrEnable = $false
$p.RtsEnable = $false
$p.Open()
$sw = [Diagnostics.Stopwatch]::StartNew()
while ($sw.Elapsed.TotalSeconds -lt 8) { $p.ReadExisting(); Start-Sleep -Milliseconds 150 }
$p.Close(); $p.Dispose()
```

Two things learned the hard way:

- **Opening the port resets the board**, which is convenient — the sketch only prints during
  `setup()`, so this is how you catch the boot banner and the NVS limits readout. The reset
  reads back as `POWERON`, not `EXT`; on the ESP32 an EN-pin reset is indistinguishable from
  power-on.
- **Do not toggle `RtsEnable` after `Open()`** to force a reset. It re-enumerates the USB
  device, which throws "the port is closed" on every subsequent read and produces a stream
  of *repeated, partially truncated* boot banners. That looks exactly like a boot loop and is
  purely a measurement artifact. A passive listen with the lines left alone is the way to
  tell a real reboot loop from this.

A healthy build reports roughly `729821 bytes (55%)` of program storage and `87636 bytes
(26%)` of dynamic memory. A large jump in either is worth a look.

**Do not pass `--warnings all`.** `ESP32Servo` does not survive it — four pre-existing
`-Wunused-variable` findings in `ESP32Servo.cpp` / `ESP32PWM.cpp` become errors under
`-Werror` and the build dies inside the library, not in the sketch. It can appear to pass
once, because arduino-cli reuses cached library objects from an earlier default-warning
build, then fails as soon as that cache is invalidated. The default warning level is what
the IDE uses and what this sketch is expected to build under.

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
- Bucket `L` moves within `minL`(70)..`maxL`(180); arm `R` within `minR`(131)..`maxR`(175).
  Both start at `posInicial`(175) and are written there in `setup()`.
- **`posInicial`(175) is the arm's fully-lowered rest position**, and `maxR` equals it, so
  `cursoDesce` is 0 — R2 cannot go below rest, it only brings the arm back down after R1
  raised it. All of the travel is upward. This is deliberate: at rest the servo carries no
  load, and the `write()` in `setup()` commands the position gravity already left the arm in,
  so booting costs no swing and no current spike. It used to be 90, which sat 8 deg from the
  *top* — the servo held the arm up permanently, on a supply that is already marginal.
  175 rather than 180 keeps clear of the servo's internal end stop.
- When mounting: power the ESP first, let the servo settle at `posInicial`, and only then fit
  the arm at its lowest position without forcing it. That is what makes rest and bottom
  coincide.
- **The arm servo is mounted inverted**; the bucket is not. A *smaller* angle means a
  *higher* arm, so R1 (up) decrements `R` and stops at `minR`, while R2 (down) increments
  and stops at `maxR`. Getting this backwards drives the arm the wrong way, so check it
  before touching that block.
- Both arm limits are derived from `posInicial`, in **degrees** per side:
  `minR = posInicial - cursoSobe` and `maxR = posInicial + cursoDesce`. Retune
  `cursoSobe`/`cursoDesce`, never `minR`/`maxR` directly. They are deliberately *not*
  expressed in `step`s — `step` is the per-tick increment (1 deg), not a press-sized jump, so
  multiplying by it would collapse the travel to a few degrees.
- `cursoSobe` is **44, measured** with the arm mounted; `cursoDesce` is **0 by design**,
  since rest already is the bottom. To measure the top it was temporarily opened to 170,
  because you cannot measure a limit that lies outside the limit in force. Closing it again
  is not cosmetic: while open, R1 could drive the servo 87 degrees past the mechanical stop
  and hold the MG996R in stall, one of the brownout sources.
- `minR`/`maxR` are **not `const`** — Options overwrites them at runtime, and NVS overrides
  them at boot. See Calibrating the arm limits.
- Arm and bucket are **MG996R** servos, attached with an explicit 500..2500 us pulse range
  (`MG996R_MIN_US`/`MG996R_MAX_US`) instead of the library default of 544..2400. `minL` and
  `maxL` still hold the old servos' values and need reconfirming on the chassis.

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
| R1 / R2      | 32 / 128 | arm up / down (`R`) — inverted, see Ranges |
| D-pad up/down| 1 / 2 | drive forward / backward  |
| D-pad left/right | 8 / 4 | steer to `ml` / `mr`  |
| Options      | misc `MISC_BUTTON_START` | capture the current arm position as that side's limit, save to NVS |
| Share        | misc `MISC_BUTTON_SELECT` | restore arm limits to the code defaults, clear NVS |
| Left stick X | —    | proportional steering, only when the D-pad is not steering |
| Right stick Y| —    | proportional throttle, only when neither the D-pad nor X/Triangle is driving |

Analog axes are `-511..512` and are mapped onto the servo ranges. Note the throttle map is
inverted (`180, 0`) so stick-forward means forward.

The arm and bucket move **continuously while the button is held**: each loop pass adds or
subtracts `step` (1 degree) and writes it. `tickLoop`(20 ms) is the `delay()` at the end of
`loop()` — the control tick rate, not idle padding — and it is deliberately one servo frame
at 50 Hz, so each tick delivers exactly one new setpoint per frame. Travel speed is
`step * 1000 / tickLoop`, currently 50 deg/s. For speed, change `step`; raising `tickLoop`
also makes steering and throttle less responsive, since the tick paces all of the control.

This replaced an earlier scheme of `step`(10) per tick with a `delay(150)` tick and a
`moverEmRampa()` helper that broke the 10 degrees into 2-degree increments. It moved for
~75 ms then sat still for 150 ms — a 33% duty cycle that was plainly visible as **pulsing**.
Do not reintroduce a large `step` with a long tick; the dead time between bursts is what
looks bad, and no amount of smoothing *inside* a burst fixes it.

A 1-degree-per-frame setpoint is also gentler on the supply than the old ramp was: the
position error never exceeds 1 degree, so the MG996R never starts at full duty. The
trade-off is that movement is now 100% duty instead of 33%, so *average* current while a
button is held is higher even though the *peak* is lower. Peak is what trips brownout.

Because the tick is short, two things are throttled on purpose:

- `logaAngulo()` prints only when the angle is a multiple of 10, matching the old log
  granularity. A limit that is not a multiple of 10 never shows up in the log.
- `setColorLED(0, 0, 0)` lives in `onConnectedController`, not `dumpCar`. It queues a
  Bluetooth output report, and at a 20 ms tick calling it per pass would be 50 reports/sec.

## Calibrating the arm limits

**There is no way to read a servo's real position here.** An MG996R is a 3-wire servo: the
internal potentiometer is not exposed on the connector, and `Servo::read()` in ESP32Servo
only echoes the last written value — `readMicroseconds()` returns `this->ticks`, which
`writeTicks()` set, and never queries the servo. So `meuBraco.read()` returns exactly the
`R` already held in a variable. Adding real feedback would mean tapping the servo's
potentiometer wiper to an ADC pin, or moving to a serial/smart servo.

What the sketch does instead: `R` is a good *proxy* for the physical position, because it
only ever moves 1 degree per tick from a known start (`posInicial`, written in `setup()`).
It stops being valid if the arm is moved by hand, jams, or slips under load — only a reboot
resynchronises it.

### The workflow, and why it is shaped this way

Calibration happens **away from the computer**: the board runs off a power bank, because PC
USB cannot supply the servos. So there is no serial monitor at the moment of capture. That
constraint drives two design choices — persist to flash, and confirm by touch.

1. On the power bank, drive the arm to a mechanical limit with R1 or R2.
2. Press **Options**. `calibraLimiteBraco()` captures the current `R` as that side's limit —
   `minR` if the arm is short of `posInicial` (the up side), `maxR` if past it (the down
   side) — and `salvaLimites()` writes both to NVS under the `trator` namespace.
3. The controller **rumbles** to confirm: a strong 300 ms pulse means saved, a weak 100 ms
   pulse means the arm was still exactly at `posInicial` so there was nothing to capture.
   This is the only feedback available in the field, so do not remove it.
4. Back on USB, `carregaLimites()` runs at boot, applies the stored values and prints them:

   ```
   Limites do braco (NVS): minR=53 cursoSobe=37 | maxR=130 cursoDesce=40
   ```

   It says `padrao do codigo` instead of `NVS` when nothing has been stored yet.
5. Copy the printed `cursoSobe`/`cursoDesce` into the constants to make it permanent in
   source. Until then NVS is what actually governs, and it wins over the constants on every
   boot.

`salvaLimites()` also stores `posRef`, the `posInicial` the measurement was taken from, and
`carregaLimites()` throws the stored limits away when it no longer matches the compiled
value — printing `Calibragem da NVS descartada`. Without that check, editing `posInicial`
had no effect at all: NVS silently won and only a Share press rescued it. That trap cost two
bogus measurements before the check existed, so keep it.

**A captured limit that is too tight self-locks.** With `minR` stored at 89, the arm cannot
travel past 89, so it can never be driven to the real limit to capture a wider value —
re-capturing would only store 89 again. Press **Share** to escape: `reiniciaLimites()`
restores `minR`/`maxR` from `cursoSobe`/`cursoDesce` and clears the NVS namespace. It
confirms with **two short pulses**, deliberately different from the capture's single long
one; the second pulse is scheduled through `playDualRumble`'s `delayedStartMs` so nothing
blocks the loop. Do not remove this escape hatch — without it a bad capture can only be
undone by reflashing.

Rewriting an identical value costs no flash wear: the underlying `nvs_set_i32` compares
first and skips a write that would not change anything.

Rumble vocabulary, since it is the only field feedback:

| Pattern | Meaning |
|---------|---------|
| one long strong pulse (300 ms) | limit captured and saved to NVS |
| one short weak pulse (100 ms)  | arm was still at `posInicial`, nothing captured |
| two short pulses (120 ms each) | limits restored to code defaults, NVS cleared |

The bucket has no equivalent; `minL`/`maxL` are plain absolute angles.

## Conventions

- Identifiers and comments are in **Portuguese** (`meuBraco`, `para frente`, `sirene`).
  Keep new code in the same language.
- Serial logging via `Serial.printf` / `Serial.println` is the main debugging channel, but
  it is unavailable during field calibration on the power bank — that is what the NVS
  readout at boot and the controller rumble are for.
- `mostrarMotivoReset()` prints `esp_reset_reason()` on every boot. `BROWNOUT`/`POWERON`
  mean the supply sagged (servo current); `PANIC`/`TASK_WDT`/`INT_WDT` mean a software
  fault. Keep it — it is the only way to tell those apart on this hardware.

## Reset history in NVS

`registraReset()` keeps a boot counter and the reasons for the last `MAX_HIST_RESET`(20)
boots, so a reset that happens in the field leaves a trace that can be read later:

```
Boot #7 | resets antigo->recente: PPBPPP
  P=poweron B=brownout X=panic W=watchdog S=software E=pino D=deepsleep ?=outro
```

Two design points that are easy to get wrong if this is ever refactored:

- **It is a history, not a "last reason" slot.** Opening the serial port resets the board, so
  a single slot would be overwritten by that very reset, at the exact moment you try to read
  it. That is how the evidence behind a bogus `cursoSobe=1` capture was lost.
- **The boot counter matters as much as the reasons.** A severe sag wipes the RTC domain and
  reports as `POWERON`, indistinguishable from plugging the board in. What exposes the
  problem is the *count*: power on once, come back to five new boots, and four resets were
  not asked for. Note that each USB read adds one `P` of its own.

It lives in a **separate NVS namespace** (`diag`, not `trator`) because `reiniciaLimites()`
calls `prefs.clear()` on `trator` — merging them would let a Share press erase the
diagnostic history along with the calibration.

**What it has already shown.** On 2026-09-09, boot #16 read `PPPBPPPBBPBPPBBP` — six
brownouts in sixteen boots. The supply is *not* adequately fixed: powering the board from a
stronger source removed the USB current cap but left the servo current flowing through the
board's 5V trace and the regulator's input node. Read this history before assuming a
software cause for any arm or bucket misbehaviour, and expect it to keep happening until a
dedicated BEC is in place.

`registraReset()` runs early in `setup()`, deliberately **before** the servos are
positioned, so that if the servo write browns the board out, the boot that is dying has
already been recorded. Reflashing the sketch does not clear NVS, so counters and captured
limits survive an upload; only `esptool erase_flash` wipes them.

## Dormant code

Sizeable commented-out blocks are intentionally kept: a siren/buzzer routine (in
`dumpCar()` and `stopBuzzer()`), a `switch`-based version of the button handling, and
gamepad LED/rumble demos. **The buzzer code references `buzzerPin`, which is not
declared anywhere** — uncommenting it requires adding that pin definition first. The
unused globals `sirene` and `center` exist only to support those blocks.
