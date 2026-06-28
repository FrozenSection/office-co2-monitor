# office-co2-monitor ("stuffy")

A standalone CO₂ / time / temperature desk monitor for a sealed office with no
fresh-air supply. Because the room never sees outdoor air, the sensor's automatic
self-calibration is disabled — instead you periodically carry the device outside,
let it breathe real air, and trigger a forced recalibration against a known
~420 ppm reference.

Runs fully standalone (no WiFi required); an on-demand web UI handles configuration,
history, and updates.

## Features

- Live **CO₂ + temperature + humidity** (Sensirion SCD-41) on a 1.28" round display,
  color-coded by air-quality tier.
- Button-driven **forced recalibration** with a guided equilibration countdown — the
  "fresh-air walk" — since ASC is disabled for a sealed room.
- **Calibration-confidence** cue that escalates with time since the last recal, and
  greys the reading when it's overdue rather than showing it as trustworthy.
- **Battery-backed real-time clock** (DS3231), NTP-synced when online.
- **Ambient-light auto-brightness** (optional VEML7700).
- **On-demand web config** — a captive AP for first setup, plus an optional always-on
  page at `http://<name>.local` when connected to home WiFi.
- **Data logging** to internal flash with a trend graph + CSV export.
- **OTA firmware updates** (ElegantOTA), protected by HTTP Basic auth.

## Hardware

- Adafruit ESP32 Feather V2
- Adafruit SCD-41 (CO₂ / temp / humidity, STEMMA QT)
- DS3231 RTC (coin-cell-backed, STEMMA QT)
- 1.28" round TFT (GC9A01, 240×240) via EYESPI breakout + 18-pin FPC
- Momentary button (recalibration / menu)
- LiPo battery (USB-powered at the desk; battery covers the recalibration walk)
- Optional: VEML7700 lux sensor (auto-brightness), MAX17048 fuel gauge (battery %),
  microSD for extended logging

See [docs/wiring.md](docs/wiring.md) for pinouts and wire colors, and
[docs/design/](docs/design/) for the display design.

## Build & flash

```
pio run                 # compile
pio run -t upload       # flash over USB
pio device monitor      # serial @ 115200
```

After the first USB flash, updates can go over the air at `http://<name>.local/update`.

## License

MIT — see [LICENSE](LICENSE).

## Development

This firmware was developed with [Claude](https://claude.com/claude-code) (Anthropic)
as a coding assistant, working from the author's direction and input: the author chose
the hardware, did all wiring and on-device testing, and made the design and engineering
decisions; Claude wrote and iterated the code accordingly. The display mockups were also
generated with Claude. Commits carry a `Co-Authored-By: Claude` trailer.
