# WeatherDisplay

The one sketch in this tree, and what runs on the board. Three screens side by
side:

```
 ESPRESSO  <-  MONTREAL  ->  VANCOUVER
             (boot screen)
```

- **Espresso** -- boiler and group head temperature. A placeholder for now:
  both rows read `---.-` until the two MAX31865 / PT1000 sensors are wired.
- **Montreal** and **Vancouver** -- current temperature, humidity and rain/snow,
  read straight from Environment Canada over WiFi.

Montreal is on the glass at boot. Swipe right for the espresso screen, left for
Vancouver.

Copied from [esp32-amoled](https://github.com/anselbrandt/esp32-amoled)
`firmware/` at `655e200`, which was verified byte-for-byte against the board's
app partition before the espresso screen was added.

`platformio.ini` and `boards/T-Display-AMOLED.json` are the build scaffolding,
derived from LilyGo's. LilyGo's examples are in
[`../vendor/LilyGo-AMOLED-Series/examples`](../vendor/LilyGo-AMOLED-Series/examples).

## Setup

```sh
cp include/secrets.h.example include/secrets.h
$EDITOR include/secrets.h        # 2.4 GHz network -- the ESP32-S3 has no 5 GHz radio
```

`include/secrets.h` is gitignored.

## Build, flash, monitor

```sh
cd firmware
pio run              # build
pio run -t upload    # flash
pio device monitor   # 115200
```

Flashing details and failure modes: [`../docs/t-display-s3-amoled-plus/flashing.md`](../docs/t-display-s3-amoled-plus/flashing.md).

## What it does

```
┌────────────────────────────────────────────────┐
│ MONTREAL                     updated 2m ago    │
│                                                │
│      ▟▙      11.8 °C                           │
│      ▀▘                                        │
│                                                │
│ HUMIDITY   91%                           RAIN  │
└────────────────────────────────────────────────┘
```

- **Data.** `https://weather.gc.ca/api/app/v3/en/Location/<lat>,<lon>?type=city`,
  the app API behind weather.gc.ca. Montreal is `45.529,-73.562`, observed at
  Trudeau; Vancouver is `49.163,-123.138`, which the API names Richmond and
  observes at Vancouver Int'l. Both are fetched every **5 minutes** --
  observations only change hourly upstream -- so the screen off the glass is
  current when it slides in.
- **Parsed on the device.** Each response is ~28 KB of forecasts, alerts and
  zone polygons, for three fields. An ArduinoJson filter keeps
  `observation.condition`, `observation.humidity` and
  `observation.temperature.{metric,metricUnrounded}` and drops the rest.
  Every value is a string, and an unavailable one is `""` rather than absent,
  so an empty unrounded temperature falls back to the rounded one.
- **Rain and snow** are case-insensitive substring tests on `condition`
  (`Light Rain Showers`, `Rain And Snow`), since Environment Canada's condition
  vocabulary is long and unpublished. Either shows twice: as an icon left of the
  temperature digits, and as the word bottom right. Snow wins when both are set.
- **Status line**, top right: `updated Nm ago`, or `update failed` / `no wifi`
  in red when the last fetch failed. A failed fetch keeps the last reading.

### HTTPS

`WiFiClientSecure` with `setInsecure()`: the certificate is not checked,
because checking one needs the wall clock and nothing here sets it. Pinning the
issuer and adding `configTime()` is the upgrade; this is an unauthenticated read
of a public observation.

`setHandshakeTimeout(10)` matters -- the library default is **120 s**, which
would freeze the panel against a host that accepts and then stalls.

The response is `Transfer-Encoding: chunked`. `HTTPClient::getStream()` returns
the raw socket with the chunk headers still interleaved in the JSON, so the body
is read with `getString()` instead, which decodes them. That buffers the whole
~28 KB, which lands in PSRAM.

### The espresso screen

Two rows, `BOILER` over `GROUP HEAD`, each a name on the left and a font 8
temperature on the right, from the `PROBES[]` table in the sketch. Nothing
fills it yet: `haveReading` stays false, both show `---.-` in grey and the
status line says `no sensors`. Wiring the sensors means setting `temp` and
`haveReading` on each probe; the screen already renders a real reading in white.

The unit here is `C` in font 4, not font 6: font 6 only has digits, `: - . a p m`,
so a font 6 `C` renders as a blank -- which is what the weather screens draw.

### Three screens and the swipe

Each screen renders into its own full-frame `TFT_eSprite` (257 KB each, in
PSRAM). The RM67162 keeps no framebuffer and `pushColors()` takes one contiguous
W x H buffer, so a page cannot be pushed at an offset; the slide splices each of
its 8 frames row by row from the two pages into a spare sprite and pushes that.
Four sprites is 1 MB of the 8 MB.

A swipe is a press that travels at least 60 px sideways, and more sideways than
vertically, measured on release. A full-frame push is ~63 ms, too slow to follow
a finger, so the slide runs after the finger lifts. Every gesture is logged:

```
touch: (397,162) -> (19,166) dx=-378 dy=4
```

The fetch blocks for a second or two per city every five minutes, and a swipe in
that window is not seen.

### Icons

The raindrop and snowflake are drawn from TFT_eSPI's anti-aliased primitives
(`fillSmoothCircle`, `drawWideLine`), ported from `~/dev/esp32-s3`. Each is
placed off the rendered width of the temperature, so it keeps its distance from
`9.4` and `-19.1` alike. Set `ICON_CYCLE 1` to alternate the two every 2 s
whatever the weather, for looking at them without waiting for rain.

## Its boot log, and the errors that don't matter

```
[   929][E][esp32-hal-spi.c:215] spiAttachMISO(): HSPI Does not have default pins on ESP32S3!
Devices Scan start.
I2C device found at address 0x15 !      <- CST816T touch
I2C device found at address 0x51 !      <- PCF85063 RTC
I2C device found at address 0x6B !      <- BQ25896 PMU
Done
[  1961][E][sd_diskio.cpp:806] sdcard_mount(): f_mount failed: (3) The physical drive cannot work
[  2475][E][LilyGo_AMOLED.cpp:741] installSD(): Failed to detect SD Card!!
panel: 536x240
wifi: connected ip=192.168.8.199 rssi=-60
weather: MONTREAL 11.8C 91% "Mostly Cloudy" rain=0 snow=0 heap=238768 psram=7569655
weather: VANCOUVER 13.8C 84% "Cloudy" rain=0 snow=0 heap=238516 psram=7569895
```

The `[E]` lines are `begin()` doing its normal work, not faults:

- The **HSPI** complaint comes from `installSD()` bringing up the SD bus.
- The **SD mount failure** is the empty card slot. It costs about 1.5 s of boot
  and cannot be skipped from the sketch -- `installSD()` is unconditional inside
  `beginAMOLED_191_SPI()`.

The I2C scan is the useful part: those three addresses are exactly what this
board should answer with, so it doubles as a hardware check.

## Auto-detect vs. explicit begin

`amoled.begin()` probes I2C to work out which board it is on. If detection fails
it does **not** error -- it falls through to `beginAMOLED_191(false)`, the
*QSPI* profile with touch off, and you get a dead panel with no diagnostic. The
sketch calls `amoled.beginAMOLED_191_SPI()` explicitly for that reason.
