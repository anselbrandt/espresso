# Flashing a LilyGo ESP32-S3

Applies to the T-Display-S3 AMOLED Plus in this repo, and to LilyGo's other
ESP32-S3 boards — the USB and download-mode behaviour is a property of the chip,
not the board. The hard-won parts are in [Gotchas](#gotchas); most of them were
found on a T-Display-S3-Pro and cost hours each.

## What has to be installed

**One prerequisite. Everything else arrives with it.**

PlatformIO Core, on your `PATH`:

```sh
uv tool install platformio      # or: pipx install platformio
pio --version                   # PlatformIO Core, version 6.1.19
```

Installed that way the `pio` entry point lands in `~/.local/bin` (a symlink into
the uv or pipx tool directory), so that has to be on `PATH` — the usual cause of
`pio: command not found` on a fresh machine.

On the first `pio run`, PlatformIO downloads the compiler, the flasher and the
board support into `~/.platformio`. **None of it goes on your `PATH`**, which is
why the full paths below matter:

| Tool | Package | Path |
|---|---|---|
| `esptool.py` 4.9.0 | `tool-esptoolpy` | `~/.platformio/packages/tool-esptoolpy/esptool.py` |
| `pyserial` 3.5 | PlatformIO's own venv | `~/.platformio/penv/bin/python` |
| `xtensa-esp32s3-elf-addr2line` | `toolchain-xtensa-esp32s3` | `~/.platformio/packages/toolchain-xtensa-esp32s3/bin/` |

**No USB driver is needed.** The S3 enumerates natively — there is no CP210x or
CH340 bridge in the path, so the driver install that most ESP32 board guides open
with does not apply here.

**No system Python packages are needed either**, and macOS's stock
`/usr/bin/python3` (3.9.6) is missing the one this document uses: `import serial`
fails there with `ModuleNotFoundError`. PlatformIO's venv has it. See
[Getting a clean boot log](#getting-a-clean-boot-log).

### Running `esptool.py` directly

Typing `esptool.py` bare fails with `command not found` — it is not on `PATH`,
and PlatformIO does not put it there. It also cannot be run by an arbitrary
Python: the `esptool.py` in that package is a 1.4 KB shim that imports the real
`esptool` package sitting beside it and injects vendored dependencies from
`_contrib/`. Point the wrong interpreter at it and the import fails.

Two invocations that work:

```sh
# Portable — PlatformIO resolves both the package and the interpreter
pio pkg exec -p tool-esptoolpy -- esptool.py version

# Explicit — PlatformIO's interpreter, PlatformIO's copy of the tool
~/.platformio/penv/bin/python ~/.platformio/packages/tool-esptoolpy/esptool.py version
```

Both print `esptool.py v4.9.0`.

You *can* put a copy on `PATH` with `uv tool install esptool` or
`pip install esptool`, but that is a second, independent copy — its version will
drift from the one `pio run -t upload` actually flashes with, and a version skew
between the two is a confusing thing to debug. Prefer the bundled one.

## Finding the board

The S3 has a **native USB peripheral**, so there is no CP210x or CH340 bridge in
the path. It enumerates itself:

```sh
ls /dev/cu.*
```

```
cu.bluetooth-debug
cu.Bluetooth-Incoming-Port
cu.debug-console
cu.usbmodem1201        <- the board
cu.wlan-debug
```

Confirm by hardware ID rather than by guessing which `usbmodem` it is:

```sh
pio device list
```

```
/dev/cu.usbmodem1201
--------------------
Hardware ID: USB VID:PID=303A:1001 SER=30:ED:A0:BC:21:EC LOCATION=0-1.2
Description: USB JTAG/serial debug unit
```

`303A:1001` is Espressif's USB-Serial/JTAG. `SER=` is the MAC, which is how you
tell two identical boards apart. PlatformIO matches on this VID:PID via the board
JSON's `hwids`, so `upload_port` never needs to be set.

## Building and flashing

```sh
pio run                # build
pio run -t upload      # build + flash
pio device monitor     # serial log at 115200
```

Serial output comes back over that *same* USB port, because the vendor build
sets `-DARDUINO_USB_CDC_ON_BOOT=1`. There is no second cable.

### Flashing a prebuilt .bin

The vendor's factory image is a bare app flashed at offset `0x0`:

```sh
pio pkg exec -p tool-esptoolpy -- \
  esptool.py --chip esp32s3 --baud 921600 --before default_reset --after hard_reset \
  write_flash -z --flash_mode dio --flash_freq 80m 0x0 firmware.bin
```

The `pio pkg exec` prefix is not decoration — bare `esptool.py` is not on `PATH`.
See [Running `esptool.py` directly](#running-esptoolpy-directly).

For a full-flash restore from a dump, see `backup/README.md` in
[esp32-amoled](https://github.com/anselbrandt/esp32-amoled) -- the 16 MB factory
image is kept there, not copied here.

Other routes to the same place, if a machine has no toolchain:

- [ESP Web Flasher](https://espressif.github.io/esptool-js/) — browser, WebSerial.
  Remember to close the tab afterwards, it holds the port.
- [Flash Download Tool](https://www.espressif.com.cn/sites/default/files/tools/flash_download_tool_3.9.5_0.zip) — Espressif's GUI, Windows.

Both need an RST press when they finish, same as the command line.

## Forcing download mode

When the port won't appear, or flashing fails outright:

1. Connect the board over USB.
2. Press and **hold BOOT** (GPIO0).
3. Tap **RST** and release it, still holding BOOT.
4. Release **BOOT**.
5. The port should now be stable and stop re-enumerating. Flash.
6. Tap **RST** to leave download mode and run the app.

If the board has no BOOT button, short **IO0 to GND** across steps 2–4 instead.

## Getting a clean boot log

Two separate things conspire to hide the start of a boot, and they have to be
solved separately.

### Reset over USB without entering download mode

On the USB-Serial/JTAG peripheral, **RTS drives EN (reset) and DTR drives GPIO0
(boot mode)**. esptool's download-mode sequence toggles both, which is what
latches the ROM into download mode and forces the replug described above.

Pulsing RTS while leaving DTR alone resets the chip with GPIO0 still high, so it
boots the app normally — and the port stays open, so nothing of the log is lost
to a reconnect:

```python
import serial, time, sys
p = serial.Serial('/dev/cu.usbmodem1201', 115200, timeout=0.3)
p.dtr = False          # GPIO0 stays high -> normal boot, not download mode
p.rts = True           # EN low  -> reset asserted
time.sleep(0.2)
p.reset_input_buffer()
p.rts = False          # release reset
end = time.time() + 10
while time.time() < end:
    sys.stdout.write(p.read(512).decode('utf-8', 'replace'))
```

Save it as `bootlog.py` and run it with PlatformIO's interpreter, which already
carries `pyserial` 3.5 — nothing to install:

```sh
~/.platformio/penv/bin/python bootlog.py
```

Do **not** reach for the system Python. On macOS `/usr/bin/python3` has no
`pyserial`, and the script dies on line 1 with
`ModuleNotFoundError: No module named 'serial'`. If you would rather use your own
interpreter, `pip install pyserial` into it first.

The port number is not stable — it changes with the USB port and across replugs
(`usbmodem1101` and `usbmodem1201` are both real sightings on this desk), so
confirm it with `pio device list` rather than pasting the literal above.

### The first prints are lost anyway

Even with the port held open, **the CDC link takes roughly a second to
re-enumerate after a reset**, and everything printed before then is gone. It is
not dropped output — the host is not attached yet. On a sibling board the log
reliably began at the *third* `Serial.println` in `setup()`, and the first
library log line to survive carried a timestamp of `[1087]` ms.

So anything printed in the first instructions of `setup()` is written to nobody.
Three ways to cope, in rough order of preference:

| Approach | Cost |
|---|---|
| Log the state again later in `setup()`, once the link is up | none — but say what you *measured*, not what you intended |
| `while (!Serial);` | blocks forever on battery — see below |
| Bounded wait: `while (!Serial && millis() < 2000);` | up to 2 s on every boot, but never hangs |

The vendor examples all use bare `while (!Serial);`, which is why *they* never
lose a line. That is the same flag the battery note below warns about: it is not
a free choice in either direction, it is a trade between losing the start of the
log and hanging when no host is attached. The bounded wait is the compromise.

When re-logging later, prefer reading hardware back over restating intent. A
line built from the `#define` that was *supposed* to configure a pin will happily
claim success next to a pin that never got configured.

## Gotchas

### Download mode latches, and USB resets can't clear it

Once the ROM enters download mode it **stays there across every reset that
arrives over USB** — esptool's `Hard resetting via RTS pin`, a web flasher's
reset button, any DTR/RTS toggle. The board will just keep re-entering download
mode and look bricked.

Only a reset that does *not* come over USB clears it: the **physical RST button**,
or **unplug and replug the cable**. Expect to replug after a flash that put the
board into download mode manually.

### Serial silence proves nothing

With `ARDUINO_USB_MODE=1`, `Serial` is the hardware USB-Serial/JTAG peripheral,
not a bridge chip and not a software CDC stack. It **stays enumerated across
application resets and crashes**, and its USB descriptor reads `USB JTAG/serial
debug unit` whether the ROM or your app is driving it.

So a stable `/dev/cu.usbmodem*` does not mean the firmware is alive, and no
output does not mean the board is dead. The only way to answer "is it running"
is to make it say so — a heartbeat print on a timer:

```c
static uint32_t last = 0;
if (millis() - last > 2000) { last = millis(); Serial.printf("alive: %lus\n", millis()/1000); }
```

Pair it with something visible on the panel at boot (a full-screen colour flash)
and "blank screen" splits cleanly into "not running" vs. "running but not
drawing".

### `Resource busy` / multiple access on port

Exactly one process may hold the serial port. When a flash fails with
`Resource busy`:

```sh
lsof /dev/cu.usbmodem*
```

Usually a leftover `pio device monitor`, or a browser tab with a web flasher
still attached.

### `--after no_reset` leaves the board in the bootloader

Handy when chaining esptool commands — the next one can use `--before no_reset`
and skip the reconnect. But the board sits in the stub bootloader doing nothing
until you finish with `--after hard_reset` (or press RST). Not a fault; it just
looks like one.

### 2.4 GHz only

The ESP32-S3 has no 5 GHz radio. A 5 GHz SSID doesn't fail with an error, it
simply never associates.

### Powering from battery instead of USB

`ARDUINO_USB_CDC_ON_BOOT=1` makes the firmware **wait for a USB host at startup**.
On battery that wait never ends. For battery use, flip it:

```ini
build_flags =
    -UARDUINO_USB_CDC_ON_BOOT   ; instead of -DARDUINO_USB_CDC_ON_BOOT=1
```

Serial then goes out GPIO43/44 instead of USB, so you lose the USB log.

### Decoding a panic

`monitor_filters = esp32_exception_decoder` in `platformio.ini` decodes
backtraces live. For a log captured elsewhere:

```sh
~/.platformio/packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-addr2line \
  -pfiaC -e .pio/build/<env>/firmware.elf <backtrace addresses>
```

### TFT_eSPI on the S3 needs `-DUSE_HSPI_PORT=1`

Only relevant if you drive a panel through TFT_eSPI directly — the AMOLED
library drives the panel itself (via ESP-IDF's SPI master driver, not TFT_eSPI
and not `esp_lcd`), and `WeatherDisplay`, like LilyGo's `TFT_eSPI_Sprite*` examples, uses TFT_eSPI purely as an
off-screen sprite renderer, so this doesn't bite there. It is recorded because it
is invisible until it isn't:

Without the flag the firmware **builds cleanly and then bootloops**, panicking
`Guru Meditation Error: StoreProhibited` at `EXCVADDR 0x10` on the first
`writecommand()` inside `tft.init()`. On screen that reads as a dead panel, which
sends you hunting the backlight instead of the crash.

`TFT_eSPI_ESP32_S3.h` defaults to `SPI_PORT = FSPI`, and `FSPI == 0` on this
chip. The SDK macro is strict about it:

```c
#define REG_SPI_BASE(i)  (((i)>=2) ? (DR_REG_SPI2_BASE + (i-2)*0x1000) : (0))
#define SPI_USER_REG(i)  (REG_SPI_BASE(i) + 0x10)
```

Port 0 yields base `0`, so the raw register pointer becomes the absolute address
`0x10` and the first write faults. `USE_HSPI_PORT` selects `SPI_PORT 3`, a real
SPI3 base, and keeps the Arduino `SPIClass` and the raw register writes pointing
at the same peripheral.

LilyGo's own examples omit the flag because they pin `espressif32@6.3.0`, whose
older `REG_SPI_BASE` did not return 0 for `i < 2` — port 0 happened to land on
SPI2. On `@6.12.0` the same code is a hard crash. A compile-time guard is cheap
insurance:

```c
#if defined(SPI_PORT) && SPI_PORT < 2
#error "TFT_eSPI picked SPI port 0/1 — add -DUSE_HSPI_PORT=1"
#endif
```

### TFT_eSPI's built-in fonts stop at ASCII 127

No `°`, no accents, no arrows. Draw a degree sign with two `drawCircle` calls and
write routes as `->`, or pull in a free font with `-DLOAD_GFXFF=1`. Font 8 is
digits-only; font 4 (26 px) is the largest built-in that renders letters.

### Pin library versions, and know why

Two that have already broken builds:

- `lewisxhe/SensorLib` — `0.4.0` restructured the headers and deprecated
  `TouchDrvCSTXXX.hpp` in favour of `TouchDrvCST.hpp`. Anything calling the old
  API needs `^0.3.4`; the LilyGo AMOLED library pins `0.2.4`.
- `platform = espressif32` — LilyGo pins `6.3.0`; moving to `6.12.0` is what
  exposes the `USE_HSPI_PORT` crash above. Newer isn't wrong, but it is not a
  free upgrade.
