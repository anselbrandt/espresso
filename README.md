# Espresso

An ESP32-S3 monitor for an espresso machine's **group head** and **boiler**
surface temperatures: two PT1000 RTDs, each read by its own Adafruit MAX31865
breakout, shown on a LilyGo T-Display-S3 AMOLED Plus.

The sensors are not wired yet. The firmware on the board today shows Montreal
and Vancouver weather, plus an espresso screen whose two rows are placeholders.

## Layout

| Path | What |
|---|---|
| [`firmware/`](firmware/README.md) | PlatformIO project: the firmware on the board |
| [`docs/t-display-s3-amoled-plus/`](docs/t-display-s3-amoled-plus/) | Pin map, flashing notes, panel and touch datasheets, product image |
| [`docs/max31865/`](docs/max31865/) | MAX31865 datasheet, Adafruit's guide, breakout schematic and fab print, Fritzing part |
| [`docs/pt1000/`](docs/pt1000/) | Yageo Nexensos PT1000 datasheet |
| [`vendor/`](vendor/) | Upstream repos, copied in unmodified (below) |

### `vendor/`

| Path | Upstream |
|---|---|
| `LilyGo-AMOLED-Series/` | [Xinyuan-LilyGO/LilyGo-AMOLED-Series](https://github.com/Xinyuan-LilyGO/LilyGo-AMOLED-Series): board library, examples, schematics |
| `Adafruit_MAX31865/` | [adafruit/Adafruit_MAX31865](https://github.com/adafruit/Adafruit_MAX31865): Arduino driver |
| `Adafruit-MAX31865-PCB/` | [adafruit/Adafruit-MAX31865-PCB](https://github.com/adafruit/Adafruit-MAX31865-PCB): Eagle board and schematic |
| `pt1000rtd/` | [ChrSchultz/pt1000rtd](https://github.com/ChrSchultz/pt1000rtd): PT1000 resistance-to-temperature lookup |

The firmware builds against the registry releases in `platformio.ini`, not these
copies.

## Quick start

```sh
cd firmware
cp include/secrets.h.example include/secrets.h   # 2.4 GHz WiFi only
pio run -t upload
pio device monitor
```

See [`firmware/README.md`](firmware/README.md) for details.
