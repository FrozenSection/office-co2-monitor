# office-co2-monitor

A standalone CO2 / time / temp desk monitor for a sealed office with no
fresh-air supply. Because the room never sees outdoor air, the sensor's
automatic self-calibration is disabled — instead you periodically take the
device outside on battery, let it breathe real air, and trigger a forced
recalibration against a known ~420 ppm reference.

No WiFi dependency. Reads at a glance.

## Hardware

- Adafruit ESP32 Feather V2
- Adafruit SCD-41 (CO2 / temp / humidity, STEMMA QT)
- DS3231 RTC (battery-backed, STEMMA QT)
- 1.28" round TFT (GC9A01, 240x240) via EYESPI breakout + 18-pin FPC
- 500 mAh LiPo (USB-powered at the desk; battery covers the walk)
- Momentary button for the recalibration trigger

See [docs/wiring.md](docs/wiring.md) for the bus layout and pinout.

## Design decisions

- **Power:** USB-powered desk display; battery is only for the fresh-air
  walk, so no deep-sleep complexity.
- **Calibration:** automatic self-calibration (ASC) **off**; forced
  recalibration (FRC) on demand via a button + guided equilibration flow.
- **Calibration confidence:** the display tracks days since the last FRC
  and escalates a status cue (fresh → aging → stale → overdue). At
  "overdue" the CO2 reading is greyed and marked unverified rather than
  shown as if trustworthy.
- **Firmware:** PlatformIO + Arduino framework.

## Status

Phase 1 — I2C bring-up. `src/main.cpp` powers the STEMMA QT rail, scans the
bus, and confirms the SCD-41 (`0x62`) and DS3231 (`0x68`) respond.

### Roadmap

1. **Bring-up** — I2C power + scan ← *current*
2. Display hello (GC9A01 drawing)
3. SCD-41 read → serial
4. RTC set + read
5. Integrate round UI
6. Disable ASC, wire button, FRC + on-screen feedback
7. Temperature offset tuning (in enclosure)
8. Enclosure (H2D)

## Build

```
pio run                 # compile
pio run -t upload       # flash
pio device monitor      # serial @ 115200
```
