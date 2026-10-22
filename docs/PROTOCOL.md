# ROCCAT Kone AIMO Remastered protocol

USB `1e7d:2e2c` (Swarm product type 47, `KONE_AIMO_16K`). Everything is done with
HID **feature reports** on the interface whose report descriptor declares report
ID `0x06`. Multi-byte values are little endian.

Everything below was verified on a Kone AIMO Remastered (firmware 1.05)
unless marked otherwise; rossat-swarm 1.0.1 implements all of it.

Source of truth: `ROCCAT_Swarm_Monitor.exe` (class `kone_aimo_base_device_c`,
which does all device I/O) and the downloadable Swarm module `KONE_AIMO.dll`
v1.0009 (class `kone_aimo_kernel_c` and the UI pages, which define the data
layouts). Addresses below refer to those binaries.

The original Kone AIMO (`1e7d:2e27`, type 26) uses the same protocol with a
shorter 0x69-byte settings report (single-byte DPI values); the Remastered
inserts 15 bytes for 16-bit X/Y DPI values. Only the Remastered is implemented.

## Transfer rules

| Step | Detail |
|---|---|
| Write | `SetFeature(report)`, then poll `GetFeature(0x04, 3 bytes)` |
| Status | byte 1 of report `0x04`: `1` ready, `2` invalid data, `3` busy |
| Timing | Swarm waits 150 ms before the first poll and gives up after 100 busy answers (`FUN_0042d6c0`) |
| Checksum | report `0x06` and the macro blob end in a u16 = byte sum of everything before it (`FUN_0042e190`) |
| Read | write a select report `0x04 [a, b]` (with status wait), then `GetFeature` the data report |

## Report overview

| ID | Size | Dir | Purpose |
|---|---|---|---|
| `0x04` | 3 | set/get | select `[04 a b]` / status `[04 s ?]` |
| `0x05` | 4 | set/get | active profile `[05 04 profile count]` |
| `0x06` | 126 | set/get | profile settings (select `[04 profile 80]`) |
| `0x07` | 75 | set/get | button assignment (select `[04 profile 90]`) |
| `0x08` | 1026 | set/get | macro chunk `[08 n data…]`, n = 1, 2 |
| `0x0d` | 46 | set | direct LED colours `[0d 2e (R G B 00)×11]` |
| `0x09` | 9 | get | device info `[09 09 fw ? …]`, fw 105 = firmware 1.05 |
| `0x0e` | 6 | set | LED control `[0e 06 x host override brightness]`, see Direct lighting |
| `0x0f` | 6 | set/get | TDCU sensor calibration `[0f 06 mode state value_u16]` |
| `0x10` | 16 | set/get | software state `[10 10 …]` used by Swarm for Easy-Shift / AIMO |

## Settings – report `0x06`, 126 bytes

| Offset | Size | Field | Notes |
|---|---|---|---|
| `0x00` | 1 | report id | `0x06` |
| `0x01` | 1 | size | `0x7e` |
| `0x02` | 1 | profile | 0–4 |
| `0x03` | 1 | ? | factory 6; no effect on hardware (Swarm's "sensitivity" is the Windows pointer speed, set by the host) |
| `0x04` | 1 | DPI enable mask | bit n = level n, factory `0x1f` |
| `0x05` | 1 | active DPI level | 0–4 |
| `0x06` | 2×5 | DPI X | value × 50 = DPI (100–16000) |
| `0x10` | 2×5 | DPI Y | same unit; Swarm writes X = Y |
| `0x1a` | 1 | polling rate | 0 = 125, 1 = 250, 2 = 500, 3 = 1000 Hz (factory 3; 250 and 1000 measured on hardware) |
| `0x1b` | 1 | angle snapping | 0 off (factory) |
| `0x1c` | 1 | ? | |
| `0x1d` | 1 | debounce time | ms, factory 8 |
| `0x1e` | 2 | LED enable mask | 11 bits, factory `0x07ff` |
| `0x20` | 1 | ? | factory 0 |
| `0x21` | 1 | lighting effect | factory 9 = AIMO |
| `0x22` | 1 | effect speed | factory 6 |
| `0x23` | 1 | brightness | factory 255; 0 = LEDs off (verified) |
| `0x24` | 25 | ? | |
| `0x3d` | 5×11 | LED colours | `[alpha R G B A]` per LED; the 5th byte (amber channel) is overwritten by the firmware's white mix |
| `0x74` | 1 | colour theme | bit 7 flag, low bits theme index (factory `0x80`…`0x88`) |
| `0x75` | 1 | ? | factory 1 |
| `0x76` | 6 | ? | |
| `0x7c` | 2 | checksum | |

Evidence: DPI `FUN_6a4133f0` (`value / 50` into `+6+2n` and `+0x10+2n`) and
`FUN_6a413640` (factory DPI table 400/800/1200/1600/3200, mask `0x1f`, active 1);
polling/angle snapping/debounce `FUN_6a41f580`, `FUN_6a41f240`, `FUN_6a41f3e0`,
reset values in `FUN_6a420090`; lighting defaults `FUN_6a457150`; LED write
`FUN_6a456fd0`.

### Lighting effect values

Values from Swarm's `CMouse_Effect_Setting` table in `utility.dll`
(`FUN_65e67040`), behaviour checked on a Kone AIMO Remastered, firmware 1.05:

| Value | Swarm name | Firmware behaviour |
|---|---|---|
| 0 | (none) | LEDs off |
| 1 | Fully lit | static per-LED colours |
| 2 | Blinking | blinking |
| 3 | Breathing | breathing |
| 4 | Heartbeat | **not rendered** (keeps the previous frame) – software effect |
| 5 | – | colour cycle (firmware only, not offered by Swarm) |
| 6 | Snake | snake |
| 7 | Breathing (2nd) | a single dot running over a white background – Swarm draws its own |
| 8 | Heartbeat (2nd) | similar moving light – Swarm draws its own |
| 9 | AIMO | **not rendered** – software effect, uses the colour theme |
| 10 | Colorwave | moving colours |
| 11 | Custom | – |

Swarm's Monitor sends `[0e 06 01 00 00 ff]` (firmware lighting) only for
effect 1 and otherwise `[0e 06 01 01 00 ff]` plus a software engine
(`aimo.dll`, one thread class per effect). rossat's engine renders 4, 7, 8 and 9
and leaves the others to the firmware.

### AIMO colour themes

Byte `0x74` low bits select one of the 10 themes in
`data/Swarm/aimo_color_theme.dat` (a `QDataStream` of 10 × 3 `QColor`s). The
factory profiles use themes 0, 6, 8, 5, 1 (`0x80 0x86 0x88 0x85 0x81`). Theme
colour 1 belongs to LEDs 0/1/5, colour 2 to 2/3/6/7, colour 3 to 4/8/9/10.

### LED indices

Verified on hardware (front to back):

| LED | Position |
|---|---|
| 0 | scroll wheel |
| 1, 2, 3, 4, 9 | left side |
| 5, 6, 7, 8, 10 | right side |

## Buttons – report `0x07`, 75 bytes

```
07 4b <profile> { <type> <p1> <p2> } × 24
```

The mouse's HID descriptor declares 75 bytes and the report has no checksum.
Swarm appends a checksum and sends 77 bytes, which the device ignores
(verified on hardware).

Entries 0–11 are the physical buttons, 12–23 the same buttons while
Easy-Shift[+] is held. Physical order (from the factory layout in
`FUN_6a44ea50`): left, right, middle, tilt left, tilt right, wheel up,
wheel down, thumb forward, thumb backward, thumb Easy-Shift, DPI up, DPI down.

Factory layout: `01 02 03 09 0a 0d 0e 07 08 41 15 16`, shift layer
`00 00 23 21 22 26 27 00 00 00 11 12`.

Function codes follow ROCCAT's common table (`0x01` click … `0x30` macro,
`0x33`–`0x37` EasyAim, `0x41` Easy-Shift, `0x51`–`0x54` Easy-Shift variants);
see `rossat_button_type` in `include/rossat/rossat.h`. The firmware's
dispatcher (`FUN_00003016`) shows `0x17`–`0x19` (sensitivity) do nothing on
this mouse. Types `0x8c`–`0xbb` are host actions: the mouse only sends input
report `[03 00 f2 N 00]` on press and `[03 00 f2 N 01]` on release
(N = type − 0x8b); rossat maps N = 1/2 to brightness step up/down
(levels `0 1 2 3 6 11 21 39 72 134 255`). Writing a profile hands the LEDs
back to the firmware (`0x0e` reads `host = 0` afterwards). For `0x05` (shortcut)
p1 is the modifier mask (Shift `0x02`, Ctrl `0x04`, Alt `0x08`, Win `0x10`)
and p2 the HID key usage; the mouse sends it as a boot-keyboard report on
interface 1 (verified on hardware).

## Macros – report `0x08`

A macro is a 2002-byte blob transferred as two 1026-byte reports:
`[08 01 blob[0..1023]]` and `[08 02 blob[1024..2001] 00…]`
(`FUN_004b1880`). Reading selects each half first with
`[04 profile|0x10 button]` / `[04 profile|0x20 button]` (`FUN_004b1560`).

| Offset | Size | Field |
|---|---|---|
| `0x000` | 1 | `0x0e` |
| `0x001` | 2 | `0x07d2` |
| `0x003` | 1 | profile |
| `0x004` | 1 | button |
| `0x005` | 1 | loop count |
| `0x006` | 40 | macro set name |
| `0x02e` | 32 | macro name |
| `0x04e` | 2 | `mode << 12 | keystroke count` (mode 0 on press, 1 while pressed, 2 infinite) |
| `0x050` | 4×480 | keystrokes `[HID usage, action (1 press / 2 release), delay ms u16]` |
| `0x7d0` | 2 | checksum |

## Profile switching – report `0x05`

`GetFeature(0x05, 4)` returns `[05 04 active count]`; writing the same layout
with a new `active` switches profile (`FUN_004b9840`).

## Firmware notes

The update file `KONE_AIMO_16K_V1.05.bin` shipped in the Swarm module is a
plain ARM Cortex-M (Thumb) image (vectors at 0, RAM at `0x10000000`); it can
be loaded into Ghidra with `-processor ARM:LE:32:Cortex` and
`tools/re/FwFix.java`. Findings:

- **LEDs are R, G, B + amber.** Every firmware effect runs through
  `FUN_00005a04`: amber = min(R, G), R and G reduced by 20 % of it. That is why
  the mouse's own white looks mint.
- **Easy-Shift[+] indicator is hard-coded.** `FUN_00006462` fills all 11 LEDs
  with `R 05 G 9c B fa` (#059cfa) while Easy-Shift is held. `FUN_000067d6`
  skips it only when the live effect (`RAM 0x10000d87`) is 9 (AIMO). No report
  sets this colour, so it can only be changed from the host: keep the profile
  on effect 9 and draw the lighting from the PC (rossat's "render with engine").
- Button events: input report `03 00 ee hi lo` carries a bitmask of pressed
  buttons when the host enabled event reporting; Easy-Shift alone gives `02 00`.

## Sensor calibration (TDCU) – report `0x0f`

`[0f 06 mode state value]`: mode 0 = factory, 2 = custom calibration; `value`
is the sensor result (u16). Sequence from `dcu_calibration_fm`
(`FUN_6a467480`, `FUN_6a467b80`), verified on hardware:

1. `SetFeature [0f 06 02 ff 00 00]` without waiting – calibration starts; an
   input report `03 00 30 01 00` is emitted.
2. Move the mouse; report `0x04` status stays `3` (busy) while sampling and
   returns to `1` when done.
3. `[0f 06 02 01 00 00]` keeps the result, `[0f 06 02 00 00 00]` discards it.

## Direct lighting

Report `0x0e` (`FUN_0000143a` case 0xe): `[0e 06 x host override brightness]`.
`host = 1` hands the LEDs to the PC, `0` returns them to the profile's effect.
If `override != 0`, `brightness` replaces the profile brightness for host
colours; otherwise host colours are scaled by settings byte `0x23` (so they are
invisible on a profile with brightness 0). Swarm sends `[0e 06 01 01 00 ff]`;
rossat sends `[0e 06 01 01 ff ff]` and applies brightness itself.

Colours: `[0d 2e]` + 11 × `[R G B A]`, A = amber LED. rossat draws the white
part of a colour, min(R, G, B), with the hand-calibrated neutral mix
R 200 / G 190 / B 170 / A 255 (verified by eye on hardware).
