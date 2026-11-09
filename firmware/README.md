# Optical Spinner Firmware

Arduino Micro (ATmega32U4, 5 V, 16 MHz) firmware, separate from the ARM/Linux
MiSTer application. The Main Makefile does not compile these sketches.

Current hardware checkpoint: native Dial firmware and D8 fire were accepted in
Forgotten Worlds on 2026-11-08. See the dated working-prototype checkpoint below
for the installed mappings and remaining acceptance tests.

## Provenance

Migrated from `cdeisler/MisterFPGA`, branch
`agents/kraken-spinner-mister-integration`, controller checkpoint `792b54a`
("Add Arduino Micro spinner controller"). Original sketch names were
`kraken-spinner-controller` and `kraken-spinner-micro`. The initial controller
import compiled before adaptation. The Pico diagnostic and unrelated HTTP,
quarantine, ROM-management, and terminal auto-approval settings are not imported.

The controller retains active-low control debounce and interrupt-driven
quadrature capture. The diagnostic retains nominal 50 kHz simultaneous A/B
sampling, once-per-second serial reports, state/transition counts, and a bounded
sequence buffer. Serial reporting pauses sampling; it is not continuous 50 kHz
capture and cannot prove that every high-speed physical edge was recorded.

## Architecture

- `libraries/OpticalEncoder/src/OpticalEncoder.h`: hardware-independent transition
  decoder and fractional scaling/queued movement, shared by both sketches.
- `libraries/OpticalEncoder/src/kraken_micro_pins.h`: Kraken-specific Micro wiring.
- `optical-spinner/spinner_config.h`: USB mode, axis, profile, scaling, inversion,
  debounce, and optional acceleration. Acceleration is disabled by default.
- `optical-spinner/spinner_usb.h`: USB presentation, independent of wiring.
- `optical-diagnostic`: serial-only optical diagnostic; no mouse or Dial output.

For a different optical encoder, supply a compatible pin-profile header instead
of changing the decoder. Both channels must be on the same AVR input port for
simultaneous sampling; the controller also requires CHANGE-capable interrupt pins.
This implementation's hardware capture is AVR-specific, not a claim of Pico or
arbitrary-board support. Do not feed 5 V signals into a 3.3 V-only board.

## Kraken Micro Wiring

| Signal | Micro Pin |
| --- | --- |
| Encoder A | D2 |
| Encoder B | D3 |
| Encoder power | 5 V |
| Shared ground | GND |
| Joystick up/down/left/right | D4/D5/D6/D7 |
| Buttons 1/2/3 | D8/D9/D10 |

Encoder inputs use `INPUT`; the control inputs use `INPUT_PULLUP` and switch to
ground. This is the direct 5 V Micro connection, not the Pico/TXS0108E wiring.
Unconnected control inputs stay released.

## Toolset and Builds

Validated on this machine: Arduino CLI 1.5.1, Arduino AVR Boards 1.8.8,
official Arduino Mouse 1.0.1, WSL Ubuntu-24.04, GNU Make 4.3, and ARM GCC 10.2.1.
Dependencies on a fresh machine:

```powershell
arduino-cli core install arduino:avr@1.8.8
arduino-cli lib install Mouse@1.0.1
```

From the Main_MiSTer workspace root:

```powershell
& ./firmware/arduino.ps1 -Action Verify
& ./firmware/arduino.ps1 -Action Build -Mode All
& ./firmware/arduino.ps1 -Action Build -Mode Mouse
& ./firmware/arduino.ps1 -Action Build -Mode Dial
& ./firmware/arduino.ps1 -Action Build -Mode Diagnostic
```

The wrapper builds `arduino:avr:micro`, supplies the repository library path, and
isolates each mode's output/cache under ignored `firmware/.build`. It never uploads
as part of a build. If PowerShell blocks script execution, use the corresponding
VS Code task, which scopes execution-policy bypass to that process.

Direct equivalent (default mouse X mode):

```powershell
arduino-cli compile --fqbn arduino:avr:micro --libraries firmware/libraries firmware/optical-spinner
```

Run the focused decoder/scaling/queue tests with WSL's host g++:

```powershell
wsl -d Ubuntu-24.04 --exec bash /mnt/d/Documents/GitHub/Main_MiSTer/firmware/tests/run.sh
```

The root VS Code tasks migrate the original Micro build/upload workflows, adding
mode selection, board discovery, serial monitor, tests, and local-only MiSTer
builds. They use workspace-relative paths and prompt for ports, not a stale COM7.

## USB Modes

| Wrapper Mode | Device Presentation | Spinner Motion | Other Controls |
| --- | --- | --- | --- |
| `Mouse` (default) | Standard USB HID mouse | Relative X | Three mouse buttons; joystick directions unused |
| `MouseY` | Standard USB HID mouse | Relative Y | Three mouse buttons; joystick directions unused |
| `Dial` | Original USB HID joystick | Relative Generic Desktop Dial | Absolute joystick X/Y and three buttons |

The official Mouse library supplies the standard mouse descriptor. The adapter
sends its four-byte report through `HID().SendReport` because `Mouse.move()` does
not expose send failures. Both modes retain queued motion until a successful USB
write and split large deltas into signed reports limited to -127..127. USB write
success is not evidence that a game consumed the input. The default scale is one
unit per valid quadrature transition, not one unit per complete four-edge cycle.

For direct builds, `compiler.cpp.extra_flags=-DSPINNER_USB_MODE=0` selects Dial;
mode `1` selects mouse, and `-DSPINNER_MOUSE_AXIS=1` selects relative Y. The wrapper
passes these explicitly so cached mode changes cannot silently select old output.
Inversion and scaling are configured in `spinner_config.h`; do not enable mouse
acceleration on the host during signal calibration.

## Manual Live Test

1. Run `& ./firmware/arduino.ps1 -Action List`. A discovery check on 2026-11-07
   identified a Micro at COM5 with VID/PID `2341:8037`; ports can change.
2. Close any serial monitor. To deliberately flash the diagnostic, run
   `& ./firmware/arduino.ps1 -Action Upload -Mode Diagnostic -Port <current-port>`.
   Uploading replaces existing firmware; Micro bootloader and runtime ports may
   differ. Board identity does not identify the sketch currently installed.
3. Rediscover the runtime port, then run
   `& ./firmware/arduino.ps1 -Action Monitor -Port <runtime-port>`. This opens at
   115200 baud. Rotate slowly in both directions and inspect count, state coverage,
   invalid transitions, and skipped sampling periods. Stop the monitor with Ctrl+C.
4. Deliberately upload `Mouse`, `MouseY`, or `Dial` using the same upload workflow.
   On Windows, mouse modes move the pointer and can click through the three wired
   buttons. Keep the device idle during upload and initial enumeration.
5. On MiSTer, use `evtest` to verify the actual device: mouse modes should produce
   `EV_REL/REL_X` or `REL_Y`; Dial should produce `EV_REL/REL_DIAL` (code 7),
   `ABS_X`, `ABS_Y`, and joystick buttons. Then verify assignment and game behavior.

For mouse-to-spinner routing, existing Main configuration uses `spinner_vid`,
`spinner_pid`, `spinner_axis` (0=X, 1=Y, 2=wheel), and `spinner_throttle`. Use the
VID/PID measured on the uploaded firmware at MiSTer; do not assume all Micro
clones share `2341:8037`. Native Dial uses Main's existing code-7 handler after
player assignment (press a controller button) and is not scaled by
`spinner_throttle`. Leave firmware scaling linear while calibrating one layer.
Do not overwrite the active MiSTer INI just to test a device.

Local builds and unit tests are not physical validation. No firmware upload,
MiSTer deployment, live serial capture, or game input test is implied by this
migration. The accepted kiosk success marker is separate from spinner acceptance.

## Evaluation Checkpoint: 2026-11-07

Firmware and local tooling are committed in `492aa3a` ("Import optical spinner
firmware and local Arduino tooling"). Mouse, MouseY, Dial, and Diagnostic builds,
the host optical tests, and both local MiSTer build profiles passed. These results
do not establish USB input or Forgotten Worlds compatibility on the target.

After that import, Diagnostic firmware was uploaded to the Micro. A physical
bidirectional test observed 2,181 valid transitions, zero invalid transitions,
all four encoder states, and opposite quadrature sequences. The cumulative count
went from 0 to 1,135 and back to 91. Diagnostic sampling pauses during serial
output, so this is not evidence of lossless capture at maximum spinner speed.
The last observed runtime port was COM6; rediscover it before further work.

The Micro still runs the serial-only Diagnostic sketch, not Mouse or Dial.
Combined Dial mode is implemented and builds: one USB HID joystick report carries
X/Y directions, three independent buttons, and relative Dial movement. It is
ready for an initial integration test, not accepted as a working cabinet controller.
Mouse mode remains a separate option for spinner-only use with another controller.

The user is off site with only the Micro and spinner. Joystick, fire, start, and
coin switches and the actual MiSTer are unavailable. The existing Pico controllers
were flashed from https://github.com/cdeisler/GP2040-CE; they are not replaced or
modified by this checkpoint. Consolidation onto one Micro per player remains an
option to evaluate, not a completed migration.

Pending evaluations:

- [ ] Intentionally upload the selected USB mode after closing the serial monitor
   and rediscovering the port; verify local USB enumeration and spinner reports.
- [ ] With the switches available, verify all four directions, diagonals, and
   independent fire/start/coin inputs. Check debounce and simultaneous switch and
   spinner activity. Dial uses D4-D7 for directions and D8-D10 for its three buttons.
- [ ] Stress-test maximum expected spinner speed with USB reporting active; check
   for missed motion or input stalls. The diagnostic rotation test is insufficient
   for this acceptance gate.
- [ ] On the actual MiSTer, inspect the uploaded device's VID/PID, stable physical
   USB path, and Linux events. Expect REL_X/REL_Y for mouse modes, or REL_DIAL,
   ABS_X/ABS_Y, and independent joystick buttons for Dial mode.
- [ ] Verify player assignment and button mappings in Forgotten Worlds. Mouse
   spinner VID/PID/axis/throttle settings apply to the mouse conversion path;
   native Dial uses the existing relative-axis handler. Do not assume the two modes
   use identical configuration or reuse the Pico's saved mappings automatically.
- [ ] Verify independent P1/P2 aim, movement, fire, start, and coin inputs, then
   cold-boot persistence and the accepted kiosk experience. Use stable USB-port
   assignments for identical devices and preserve existing controller/boot settings.

No Mouse/Dial upload, MiSTer deployment, INI change, or game acceptance has occurred
at this checkpoint. Keep the GP2040 Pico controllers intact until replacement
hardware and both-player integration have passed the pending evaluations.

## Working Spinner Prototype: 2026-11-08

This checkpoint supersedes the pending hardware status above. The existing Dial
firmware from `492aa3a` was uploaded and accepted for basic spinner/fire operation
on the actual MiSTer. No MiSTer application source change or executable deployment
was needed. This is a working spinner prototype, not full cabinet acceptance.

### Firmware and Hardware

- Arduino Micro, ATmega32U4, 5 V/16 MHz; board target `arduino:avr:micro`.
- Encoder A/B on D2/D3, encoder power on 5 V, and shared GND.
- Fire microswitch COM to GND, normally-open contact to D8; normally-closed
   contact unused. The switch uses the firmware's internal pull-up.
- Successful upload: `& ./firmware/arduino.ps1 -Action Upload -Mode Dial -Port COM8`.
   Build size was 6,752 bytes flash and 316 bytes RAM. Runtime subsequently appeared
   on COM5; rediscover ports before any future upload.
- Windows detected a healthy HID game controller. MiSTer detected VID/PID
   `2341:8037` with USB unique string `HIDOD`. Dial uses report ID 4, signed X/Y,
   relative Dial, and three independent button bits; it is not a composite mouse.

### Live MiSTer Acceptance

The target was reached at `192.168.68.174` and ran `/media/fat/MiSTer-kiosk-black`
with `/media/fat/_Arcade/Forgotten Worlds (World, newer).mra` and `jtcps1.rbf`.
The core mapping name was verified as `forgottn`.

A bounded, read-only raw HID capture observed 1,178 valid reports, three D8 presses
and three releases, 693 positive and 713 negative Dial counts, a final released
button state, and zero unexpected reports. Linux input event numbers changed
after reload; rediscover by device identity rather than assuming an event number.

After the mappings below were installed with user authorization, the user
confirmed smooth, responsive in-game rotation in both directions and the expected
rapid firing while D8 was held. Keyboard credit/start also worked. Held-button
firing is game behavior, not newly implemented firmware autofire. These results
do not measure latency or prove lossless capture at maximum spinner speed.

### Installed Mapping Checkpoint

Two new files were installed under `/media/fat/config/inputs/`, without replacing
existing controller maps:

| File | Values in the 32-entry array |
| --- | --- |
| `input_2341_8037_HIDOD_v3.map` | Entry 4 = `0x120`; entries 24-31 = `0xffffffff`; all other entries = 0 |
| `forgottn_input_2341_8037_HIDOD_v3.map` | Entry 4 = `0x120`; all other entries = 0 |

Entry numbers are zero-based. Each file is exactly 128 bytes: 32 little-endian
unsigned 32-bit integers, equivalent to Python `struct.pack('<32I', *values)`.
Linux button code `0x120` is BTN_TRIGGER; entry 4 supplies system A and core Shoot.
The `HIDOD` suffix is required for this observed Arduino identity. These values
fully specify the installed maps without tracking generated binary map files.

Both files were read back and compared against the expected arrays. SHA256 checks
confirmed that `/media/fat/MiSTer.ini` and the existing
`input_0079_0006_v3.map` remained unchanged. No boot configuration was changed.
Pressing the mapped controller button assigns this generic Dial device before
native relative Dial input can reach the game. The kiosk's Reboot action returned
to Forgotten Worlds via bootcore; it did not provide usable Menu-core setup.

### Controller Plan and Remaining Gates

The agreed design is one Micro per player, each using the same Dial firmware and
D2-D10 wiring. The preferred second board is the official Arduino Micro with
headers, model A000053, matching the tested board type. A documented ATmega32U4
5 V/16 MHz Pro Micro is a possible alternative, but its pin layout, board target,
bootloader, and USB identity must be verified separately. The ATmega328P Pro Mini
schematic examined during selection is not compatible with this native-USB plan.

Switch grounds can share a common junction. D4/D5/D6/D7 are planned for joystick
up/down/left/right, D8 for fire, D9 for start, and D10 for coin. Only D8 has been
mapped and accepted in-game so far. The existing GP2040 Pico controllers remain
unaltered. The second board has not yet been flashed or tested.

- [x] Upload Dial and verify local USB enumeration.
- [x] Verify live MiSTer bidirectional Dial and D8 press/release reports.
- [x] Install and read back system/core fire mappings; accept spinner/fire in-game.
- [ ] Wire, map, and test joystick directions/diagonals plus D9 start and D10 coin.
- [ ] Stress-test simultaneous controls and maximum expected spinner speed.
- [ ] Flash the second Micro and verify independent two-player operation.
- [ ] Configure stable physical USB-port P1/P2 assignments. Identical VID/PID and
   `HIDOD` values cannot distinguish the players; transient event/input numbers
   must not be used as permanent identities.
- [ ] Explicitly verify cold-boot persistence and the accepted kiosk experience.

Firmware sources and local build/upload tooling are tracked; generated builds
and private SSH credentials remain excluded. This checkpoint records the live
configuration rather than changing the MiSTer or claiming the pending gates passed.