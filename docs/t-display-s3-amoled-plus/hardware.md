# Hardware — LilyGo T-Display-S3 AMOLED **Plus**

Everything here is read off the connected board or out of the vendor sources in
`~/dev/LilyGo-AMOLED-Series` (schematic in
[`vendor/LilyGo-AMOLED-Series/schematic/`](../../vendor/LilyGo-AMOLED-Series/schematic/T-Display-S3-AMOLED-Plus.pdf),
driver datasheets in [`datasheets/`](./datasheets/)).

## The board on this desk

Read back over USB with `esptool flash_id`:

```
Chip is ESP32-S3 (QFN56) (revision v0.2)
Features: WiFi, BLE, Embedded PSRAM 8MB (AP_3v3)
Crystal is 40MHz
USB mode: USB-Serial/JTAG
MAC: 30:ed:a0:bc:21:ec
Manufacturer: ef   Device: 4018      (Winbond W25Q128 — 16 MB)
Flash type set in eFuse: quad (4 data lines), 3.3 V
```

| | |
|---|---|
| Board | LilyGo **T-Display-S3 AMOLED Plus**, 1.91 inch |
| SoC | ESP32-S3R8, dual-core LX7 @ 240 MHz |
| Flash | 16 MB quad (QIO 80 MHz) |
| PSRAM | 8 MB **OPI** — `memory_type: qio_opi` |
| Display | RM67162 AMOLED, **240 (W) x 536 (H)** portrait, 350 nits |
| Touch | CST816T capacitive |
| PMU | TI **BQ25896** (some units ship SY6970 — the library probes for both) |
| RTC | NXP **PCF85063** |
| Radio | 2.4 GHz WiFi + BLE 5. **No 5 GHz** — pointing it at a 5 GHz SSID just never associates |
| Programming | Native USB JTAG/CDC, `303A:1001`, `/dev/cu.usbmodem*` |

## Device inventory — every board on this desk

Four devices, four repos. Read off the hardware on **2026-08-04** with `esptool`,
`ioreg` and the ADSBee AT console — see [how to re-derive
this](#how-to-re-derive-this). **This repo targets the first row.**

The canonical copy is **`~/dev/docs/device-inventory.md`**, outside every repo —
when it and this section disagree, that one is right.

| Board | Repo | Chip | MAC | Reached over |
|---|---|---|---|---|
| **T-Display-S3 AMOLED Plus** | `esp32-amoled` | ESP32-S3 QFN56 rev v0.2 | `30:ed:a0:bc:21:ec` | Native USB `303A:1001` |
| **T-Display-P4** | `esp32-p4` | ESP32-P4 rev v1.0 | `30:ed:a0:e1:c3:1d` | CH343 bridge `1A86:55D3` |
| **T-Display-S3-Pro** | `esp32-s3` | ESP32-S3 QFN56 rev v0.2 | `64:e8:33:7a:08:e0` | Native USB `303A:1001` |
| **ADSBee 1090** | `adsbee` | RP2040 **+** ESP32-S3 | `F0:9E:9E:75:6A:94` (ESP32) | **Network only** — see below |

Only the P4 has a USB-serial bridge chip. The two LilyGo S3s enumerate as the
ESP32 itself (`Espressif USB JTAG/serial debug unit`), which is why they vanish
from the bus entirely — not just from `/dev` — when firmware doesn't bring up
USB CDC. `firmware/platformio.ini` flags exactly that case: swapping
`-DARDUINO_USB_CDC_ON_BOOT=1` for `-UARDUINO_USB_CDC_ON_BOOT` to run on battery
makes the board invisible until it is put back into download mode.

### The two S3s are separable only by MAC

`esp32-amoled` and `esp32-s3` sit on identical silicon and report identical
`esptool` output — ESP32-S3 QFN56 rev v0.2, 8 MB embedded PSRAM, 16 MB Winbond
`ef`/`4018` flash. Chip features and flash ID **cannot** tell them apart, and the
port names move (below). The MAC is the only stable discriminator:

- `30:ed:a0:bc:21:ec` → AMOLED Plus (this repo)
- `64:e8:33:7a:08:e0` → S3-Pro (ST7796 TFT)

### The ADSBee's ESP32-S3 is not on USB, and never will be

The ADSBee is two chips and **only the RP2040 is wired to the USB port**. It
enumerates as `2E8A:000A` "Pico" — its USB serial number is the RP2040 flash
unique ID (`E4654C61973F1239`), which is why it looks like a bare Raspberry Pi
Pico in `ioreg`. The ESP32-S3 is a networking co-processor hanging off the
RP2040's SPI bus with no USB path of its own. Hunting for it in `/dev/cu.*` is a
dead end, not a fault.

Reach it two ways instead:

- **Network** — `http://192.168.8.187/` serves the ADSBee web UI (Console and
  Radar tabs) plus an AT console over `ws://<host>/console`.
- **RP2040 AT console** — open the Pico's CDC port at 115200 and send `AT+HELP`,
  `AT+DEVICE_INFO?`, `AT+SETTINGS?`. Commands need the `AT+` prefix; the parser
  is `CppAT`.

> `adsbee.anselbrandt.net` returned **NXDOMAIN** from this Mac on 2026-08-04.
> The bare IP works. If that name is meant to resolve on the LAN, the DNS record
> is missing or not served to this client.

As reported by `AT+DEVICE_INFO?`: RP2040 and ESP32 firmware both `0.9.1-rc3`,
1090 MHz RF frontend version 3, part code `010250002G-20260529-020112`. ESP32
MACs are base/STA `F0:9E:9E:75:6A:94`, AP `…:95`, BT `…:96`, Ethernet `…:97`.

> **`AT+DEVICE_INFO?` also prints two OTA signing keys.** Redact that output
> before pasting it into docs, issues or chat.

### Port names are not stable — match on MAC or serial

`/dev/cu.usbmodem*` names are derived from USB topology, so they change whenever
the hub is rearranged. Rearranging the hub during this session moved every native-USB
board's path while the MACs stayed put:

| Device | Before | After |
|---|---|---|
| AMOLED Plus | *(not enumerated)* | `/dev/cu.usbmodem122101` |
| S3-Pro | *(not enumerated)* | `/dev/cu.usbmodem122201` |
| ADSBee (RP2040) | `/dev/cu.usbmodem121401` | `/dev/cu.usbmodem122301` |
| T-Display-P4 | `/dev/cu.usbmodem5B910814031` | `/dev/cu.usbmodem5B910814031` |

Only the P4 kept its path, because the CH343 exposes a USB serial number
(`5B91081403`) and macOS names the port from that instead of the bus location.
**Treat the paths above as a snapshot, not as configuration** — resolve the port
from the MAC at use time.

### How to re-derive this

```sh
# What is on the bus, with VID/PID/serial
ioreg -p IOUSB -w0 -l | grep -E '"(USB Product Name|USB Serial Number|idVendor|idProduct)"|\+-o'

# Identify a chip (resets the board via RTS)
esptool --port /dev/cu.usbmodemXXXX chip_id

# Identify without disturbing a board already in download mode
esptool --port /dev/cu.usbmodemXXXX --before no_reset --after no_reset chip_id
```

`esptool` is not on `PATH` — see [flashing.md](flashing.md) for where the
toolchain venvs live. The `no_reset` form doubles as a download-mode test: it
only completes against a chip already sitting in the ROM bootloader, so it
distinguishes "running firmware" from "waiting to be flashed" without changing
which of the two is true.

## Plus vs. plain 1.91": the display bus is different

This matters more than the name suggests. LilyGo ships two 1.91-inch AMOLED
boards with the same panel and the same resolution, and the library models them
as **two separate board profiles**:

| | T-Display-S3 AMOLED | T-Display-S3 AMOLED **Plus** (this board) |
|---|---|---|
| Library ID | `LILYGO_AMOLED_191` | `LILYGO_AMOLED_191_SPI` |
| `begin` entry point | `beginAMOLED_191()` | `beginAMOLED_191_SPI()` |
| Display bus | **QSPI**, 4 data lines, 75 MHz | **1-lane SPI**, MOSI + DC, 40 MHz |
| Init sequence | `rm67162_cmd` | `rm67162_spi_cmd` |
| PMU | none | BQ25896 / SY6970 |
| RTC | none | PCF85063 |
| SD slot | none | yes, SPI |

So **an example written for the QSPI board will not drive this panel.** The one
that bites is `examples/Arduino_GFX_HelloWorld` / `Arduino_GFX_PDQgraphicstest`,
which hardcode `new Arduino_ESP32QSPI(6, 47, 18, 7, 48, 5)`. That is the QSPI
pinout; on the Plus, GPIO48 and GPIO5 are not display data lines at all. Those
two examples were deliberately never copied into `firmware/` for that reason.

### How `amoled.begin()` tells them apart

`LilyGo_AMOLED::begin()` walks candidate I2C buses looking for a marker chip
(`src/LilyGo_AMOLED.cpp:484`):

1. `Wire.begin(1, 2)` → AXP2101 present? → 1.47-inch Lite.
2. `Wire.begin(3, 2)` → CST816 at **0x15** present? Then, on that same bus:
   - PCF85063 RTC answers at **0x51** → **1.91 SPI (Plus)** ← this board
   - nothing at 0x51 → 1.91 QSPI
3. `Wire.begin(6, 7)` → SY6970 at 0x6A → 2.41-inch T4-S3.
4. Nothing matched → falls through to `beginAMOLED_191(false)`, i.e. **the QSPI
   profile with touch disabled**.

That last line is the trap: a wiring or touch-rail fault doesn't report as an
error, it silently selects the *wrong board profile* and you get a dead panel.
Calling `amoled.beginAMOLED_191_SPI()` explicitly, as
LilyGo's `examples/PPM_Example_for_191Plus` and `firmware/WeatherDisplay` do, skips the guessing entirely — worth
doing in anything that only ever has to run on this board.

## Pin map (`BOARD_AMOLED_191_SPI`)

From `src/LilyGo_AMOLED.h:245` and the `RM67162_AMOLED_SPI` display config.

### Display — RM67162 over 1-lane SPI @ 40 MHz

| Signal | GPIO |
|---|---|
| MOSI (`DATA0`) | 18 |
| DC (`DATA1`) | 7 |
| SCK | 47 |
| CS | 6 |
| RST | 17 |
| TE | 9 |

**`DATA1` is a DC line here, not a data line.** The board config carries `8`
command bits and `24` address bits, but those are read only by the library's
*QSPI* branch, which encodes the command in the SPI peripheral's command/address
phases. This board takes the other branch: ordinary full-duplex SPI, CS driven by
hand, and GPIO7 low for a command byte / high for data
(`LilyGo_AMOLED::writeCommand()`). Worth knowing before porting anything — it is
what makes a driver for this board simpler than one for the QSPI sibling.
A Rust port of it (`firmware-rs/rm67162`) was written and has since been
removed; git history has it.

No framebuffer is allocated by the driver (`framebuffer: false`) — you push
pixels with `amoled.pushColors()`, typically out of an LVGL buffer or a TFT_eSPI
`TFT_eSprite`.

A full-screen repaint costs **~51.5 ms of bus time** and cannot be made faster:
536 x 240 x 2 bytes is 257 KB, and this is one data line at 40 MHz. Measured
63 ms end to end. Anything that redraws the whole panel per frame is
capped near 16 fps, which is why LVGL's dirty-region updates matter here.

**The panel is 240x536, but `begin()` hands you 536x240.** `RM67162_WIDTH` and
`RM67162_HEIGHT` are 240 and 536 (`src/initSequence.h:29`), and every spec sheet
quotes it that way — but `beginAMOLED_191_SPI()` finishes with `setRotation(0)`
(`src/LilyGo_AMOLED.cpp:646`), and that reports **landscape**:

```
panel: 536x240        // Serial.printf("%dx%d", amoled.width(), amoled.height())
```

So there are only **240 px of vertical space**, not 536. Laying out against the
datasheet numbers silently overflows the bottom of the screen, and LVGL will not
complain — it just clips. Print `amoled.width()` and `amoled.height()` once at
boot and lay out against those; `firmware/WeatherDisplay` does. The discrepancy
was found by `PMU_Readout`, since removed (git history has it).

### I2C bus — SDA 3, SCL 2 (shared)

| Device | Address | IRQ |
|---|---|---|
| CST816T touch | `0x15` | GPIO21 (RST not wired, `-1`) |
| BQ25896 PMU | `0x6B` | GPIO1 |
| SY6970 PMU (alternate) | `0x6A` | GPIO1 |
| PCF85063 RTC | `0x51` | — |

### SD card — separate SPI bus

| Signal | GPIO |
|---|---|
| MISO | 13 |
| MOSI | 12 |
| SCK | 14 |
| CS | 11 |

### Other

| Function | GPIO |
|---|---|
| BOOT button | 0 |
| Battery ADC | 4 |
| PMIC / display power enable | 38 |

GPIO38 is the display power rail on the touch versions of the 1.91-inch boards —
it has to be driven before the panel will light. `begin()` handles it; hand-rolled
init that skips it gets a black screen with a perfectly healthy SPI bus.

## Brightness

The AMOLED has no backlight. Brightness is a **DCS command to the panel**,
`0x51` with one byte, 0–255 (`LCD_CMD_BRIGHTNESS` in `src/LilyGo_AMOLED.cpp:44`).
Use `amoled.setBrightness(level)`; see LilyGo's `examples/AdjustBrightness`.

Note the collision: `0x51` is *both* the panel's brightness command and the RTC's
I2C address. Different buses, unrelated — but grepping for `0x51` returns both.

## Power path — BQ25896 / SY6970

`begin()` probes `0x6A` (SY6970) then `0x6B` (BQ25896), and **returns `false` if
neither answers** (`src/LilyGo_AMOLED.cpp:600`) — on a board profile with a PMU,
a dead I2C bus fails `begin()` outright rather than degrading. Having found one,
it does exactly three things (`:604`):

```c
BQ.enableMeasure();
BQ.disableOTG();
BQ.disableCharge();    //Default disable charge function
```

### Charging is off until you turn it on

That last line is the surprise: **attach a battery and it will not charge** until
something calls `amoled.enableCharge()`. Nothing reports this — the board just
runs off USB and the battery sits there.

It is a deliberate default, and the reason is in LilyGo's own comment in
`examples/PPM_Example_for_191Plus` in the LilyGo repo:

> If USB is used as the only power input, it is best to turn off the charging
> function, otherwise the VSYS power supply will have a sawtooth wave, affecting
> the discharge output capability.

and:

> If the battery does not exist, the data may be confused when the charging
> function is turned on. **The chip cannot determine whether the battery exists.**

So the chip cannot tell an absent battery from a flat one, and charging into a
battery that is not there makes VSYS oscillate. Leaving charging off is the safe
default for a USB-powered board; turning it on is a decision that belongs to
firmware that knows a battery is fitted.

This was observed first-hand on the sibling T-Display-S3-Pro, whose firmware does
*not* initialise the PMU and so leaves charging enabled at the chip's power-on
default: VSYS was measured swinging 4284 → 4244 → 3804 mV, with the charge status
flipping between `Fast charging` and `Not charging` between one-second samples.
That is the sawtooth. The AMOLED library avoids it for you — but only if you go
through `begin()`.

### USB-C PD: this board cannot ask, and the S3 cannot either

There is no way to enumerate a PD supply's voltage/current profiles from this
board. Two independent reasons, either of which alone is fatal.

**The CC lines dead-end into pulldowns.** On the schematic
(`vendor/LilyGo-AMOLED-Series/schematic/T-Display-S3-AMOLED-Plus.pdf`, sheet 2/3), `USB1` pin `A5`
(`CC1`) goes through `R10` 5.1K to GND and pin `B5` (`CC2`) through `R11` 5.1K to
GND. That is all they do — no trace to a GPIO, no PD controller anywhere on the
board. Those two resistors are the standard Rd sink advertisement: a *passive*
"I am a sink, give me default 5 V", asserted by the resistors themselves. Nothing
on this board is listening on CC, so a `Source_Capabilities` message cannot be
received even in principle.

**The ESP32-S3 has no CC front end.** Its two USB peripherals — USB-OTG
full-speed and USB-Serial/JTAG — are both D+/D- only. PD is 300 kbps
biphase-mark-coded signalling on CC with a `GoodCRC` turnaround inside about a
millisecond, on top of Rp/Rd analog thresholds. That is a PD PHY's job; it is not
something a GPIO can be talked into, and the S3 does not have one.

Getting real PDO enumeration means adding an I2C PD sink controller in-line ahead
of the board — `AP33772S` (exposes the source PDO list directly) or `FUSB302B`
(full TCPC; works with the `usb-pd-arduino` library on ESP32) — fed from its own
USB-C connector, since this board's port gives no CC access.

### The supply class is a strap, not a measurement

It is tempting to fall back on "well, at least the charger does BC1.2 D+/D-
detection". **On this board it does not, and `VBUS_STAT` carries no information
about what is plugged in.** An earlier version of this file claimed otherwise.

`U6`'s 24 pins on the schematic are `BUS, PSEL, PG, STAT, SCL, SDA, INT, OTG, CE,
ILIM, NTC/TS, QON` and `BAT×2, SYS×2, PGND×2, SW×2, BTST, REGN, PMID, NC`. There
is no D+/D- among them, and the connector's data lines (`USB_P`/`USB_N`, `USB1`
pins A6/B6/A7/B7) run to the ESP32 and nowhere else. What decides the class
instead is **`PSEL` (pin 2), strapped straight to GND**, which LilyGo's own driver
comment spells out:

```c
// PSEL = Hi (USB500) = 500mA
// PSEL = Lo         = 3.25A
```

The measurement agrees, and rules out the alternative. Plugged into a **laptop
host port** — a textbook BC1.2 SDP, D+/D- carrying live USB traffic with host
pulldowns, not shorted — the chip reports `Adapter (2)` and `IINLIM` 3250 mA. A
D+/D- detector would have to call that `USB Host SDP` and 500 mA. It reports what
the strap says instead.

Two consequences:

- **`VBUS_STAT` and `IINLIM` are constants here.** A PD brick, a 5 W phone
  charger and a laptop port all read `Adapter` / 3250 mA, because nothing about
  the supply reaches the chip. Do not read a change in supply behaviour out of
  those fields; they cannot report one.
- **A supply-dependent fault is not diagnosable from the PMU registers.** VBUS
  voltage and the DPM status bits are the only fields that respond to the outside
  world at all.

The register map *does* expose DPDM detection (`AUTO_DPDM_EN`, `FORCE_DPDM`, via
`enableInputDetection()`), so the silicon can do this in principle. It just has
nothing wired to look at on this board.

`PMU_Readout` (removed; git history has it) showed what is knowable on its
second screen, and labelled the class cell as the strap it is rather than
dressing it up as detection.

### The input current limit: measure it, don't reason about it

What any of this *implies* for a given board is much less obvious than it looks,
and guessing has already produced two wrong answers. Two boards, both with
`EN_ILIM` set, measured on the same desk:

| | T-Display-S3-Pro (SY6970) | AMOLED Plus (BQ25896) |
|---|---|---|
| `VBUS_STAT` | `Unknown adapter` (5) | `Adapter` (2) |
| `IINLIM` (`REG00[5:0]`) | 500 mA | **3250 mA** |
| `IDPM_LIM` (`REG13[5:0]`) | 500 mA | **3250 mA** |
| `EN_ILIM` | 1 | 1 |

A 6.5× spread, from the same family of chip, on the same supply. So there is no
useful rule of thumb here — not "PD gets throttled", and not "a resistor pins it
low" either.

The T-Display-S3-Pro reported an identical 500 mA on a 20 W USB-C PD supply, a
5 W USB-A brick and a computer port alike. That looked like a hardware clamp
overriding detection, and it was written up that way. The likelier explanation is
that **all three inputs produced the same class** (`Unknown adapter`), whose
standard limit is 500 mA — the 680R on that board's ILIM pin sits at ~530 mA,
close enough that registers cannot separate the two.

Note what that does *not* establish. Whether the S3-Pro's SY6970 arrives at its
class by D+/D- detection or by a strap has not been checked on that board; the
AMOLED's answer (see above — it is a strap) does not transfer, and neither board
should be used to predict the other. The only honest reading of the table is that
two boards differ by 6.5x and the reason is board-specific.

Two things follow, and they are the parts worth keeping:

- **The ILIM pin clamps current in hardware, and no register reports it.** So
  `IDPM_LIM` is a ceiling from the register/ICO side, not proof of what the
  board can actually draw.
- **`IDPM_STAT` is the question actually worth asking.** It says whether the
  limit is binding *now*, which is what you wanted to know in the first place.

#### What the ILIM pin is set to here — 260R, ~1.37 A

The resistor is on the schematic even though the register isn't: `R14` = **260R**
from `U6` pin 10 (`ILIM`) to GND, sheet 2/3. The BQ25896 sets
`IINMAX = K_ILIM / R_ILIM` with `K_ILIM` ≈ 355 A·Ω, so:

```
355 / 260  =  ~1.37 A
```

against the **3250 mA** the register reports. If that is right, the hardware
clamp is the real ceiling and it is roughly 2.4× lower than `IDPM_LIM` suggests.
The same arithmetic on the T-Display-S3-Pro's 680R gives ~522 mA, which is where
this file's earlier "~530 mA" came from.

**This is computed, not measured** — it is exactly the kind of reasoned-from-the-
datasheet claim that got the previous version of this section wrong, and it is
recorded here as a hypothesis with a test attached, not as a fact. The test:
draw more than ~1.4 A and watch `IDPM_STAT`. If it goes high well below 3250 mA,
the ILIM pin is confirmed as the binding constraint.

| Register | Field | Meaning |
|---|---|---|
| `REG13[6]` | `IDPM_STAT` | set while the **input current** limit is binding — start here |
| `REG13[7]` | `VDPM_STAT` | set while the **input voltage** loop is regulating instead |
| `REG13[5:0]` | `IDPM_LIM` | limit in effect on the register side, `100 + n*50` mA |
| `REG00[5:0]` | `IINLIM` | what detection asked for |
| `REG00[6]` | `EN_ILIM` | set means the ILIM pin is also in play, invisibly |

If `IDPM_STAT` is set you are current-starved and no firmware change will fix it.
If it is clear — as it is on this board — the limit is not binding *at the load
you are drawing right now*, which is the only claim the bit supports. It is not a
statement that 3250 mA is available; with a ~1.37 A pin clamp in the way, the
board has almost certainly never been asked for enough current to find out.

`PMU_Readout` (removed; git history has it) put `IDPM_LIM` on the panel and
coloured it when `IDPM_STAT` went high; its second screen added `IINLIM`,
`VBUS_STAT` and the computed ILIM figure side by side.

## Build settings

The vendor board definition (`boards/T-Display-AMOLED.json`, one profile for the
whole AMOLED family) resolves to:

```
mcu           esp32s3
memory_type   qio_opi          ; 16 MB QIO flash + 8 MB OPI PSRAM
f_cpu         240 MHz
f_flash       80 MHz
flash_mode    qio
partitions    default_16MB.csv
extra_flags   -DLILYGO_TDISPLAY_AMOLED_SERIES -DARDUINO_USB_MODE=1
              -DARDUINO_RUNNING_CORE=1 -DARDUINO_EVENT_RUNNING_CORE=1
upload_speed  921600
hwids         303A:1001
```

Arduino IDE equivalents, if you go that route rather than PlatformIO — the bold
ones in LilyGo's table are the ones that actually matter: **ESP32S3 Dev Module**,
USB CDC On Boot **enabled**, Flash Size **16MB**, PSRAM **OPI PSRAM**, Partition
Scheme **16M Flash (3M APP / 9.9MB FATFS)**, USB Mode **CDC and JTAG**.
