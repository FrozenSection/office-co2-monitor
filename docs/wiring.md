# Wiring

Two buses, cleanly separated — no address or pin conflicts.

## I2C — STEMMA QT chain

Daisy-chained off the Feather V2's STEMMA QT port:

```
Feather V2 QT ── SCD-41 ── DS3231
```

| Device  | Address | Notes                                   |
| ------- | ------- | --------------------------------------- |
| SCD-41  | `0x62`  | CO2 + temp + humidity                   |
| DS3231  | `0x68`  | RTC (battery-backed)                    |
| AT24C32 | `0x57`  | EEPROM on the DS3231 module (harmless)  |

> **Gotcha:** the Feather V2 powers the QT port through `NEOPIXEL_I2C_POWER`
> (GPIO2). It must be driven HIGH before `Wire.begin()` or the bus scan
> finds nothing.

## SPI — display (EYESPI)

Round GC9A01 240x240 → 18-pin FPC → EYESPI breakout → Feather SPI.
MISO is unused (display is write-only).

| EYESPI | Feather V2 | Purpose                          |
| ------ | ---------- | -------------------------------- |
| SCK    | SCK        | SPI clock                        |
| MOSI   | MO         | SPI data                         |
| TCS    | GPIO 33\*  | TFT chip select                  |
| DC     | GPIO 15\*  | data/command                     |
| RST    | GPIO 32\*  | reset                            |
| LITE   | GPIO 14\*  | backlight (PWM for dimming)      |

\* Provisional — finalize when wiring the display phase.

## Recalibration button

Momentary switch on `GPIO 27`\* to GND, `INPUT_PULLUP`. Triggers the
fresh-air forced-recalibration (FRC) flow.
