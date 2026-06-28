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

## SPI — display (EYESPI), hard-wired

Round GC9A01 240x240 → 18-pin FPC → EYESPI breakout → soldered to Feather V2.
Pins verified against the Adafruit Feather V2 and EYESPI breakout pinouts.

- **Vin → 3V.** The TFT is rated `Vlogic/Vin 3.3–5VDC` (per its silkscreen) and the
  EYESPI breakout has **no level shifting**, so driving Vin from the Feather's `3V`
  pad keeps power and logic both at a clean, in-range 3.3V.
- **MISO is not wired** — the GC9A01 is write-only.
- SPI bus pins (`SCK`/`MO`) are the Feather's hardware-SPI pads; don't reassign them.
- **Only 6 wire colors on hand:** red, black, green, white, blue, yellow. Red/black
  are reserved for power/ground; yellow and blue are each reused once. Reuses are
  split across the two header rows so no cluster holds two same-colored wires.

| EYESPI pad | Feather pad     | GPIO | Purpose          | Wire color |
| ---------- | --------------- | ---- | ---------------- | ---------- |
| Gnd        | GND             | —    | ground           | Black      |
| Vin        | 3V              | —    | 3.3V power       | Red        |
| SCK        | SCK (bottom)    | 5    | SPI clock        | Yellow     |
| MOSI       | MO (bottom)     | 19   | SPI data in      | Blue       |
| TCS        | 33 (top)        | 33   | TFT chip select  | Green      |
| DC         | 32 (top)        | 32   | data/command     | White      |
| RST        | 27 (top)        | 27   | display reset    | Yellow †   |
| Lite       | 14 (top)        | 14   | backlight (PWM)  | Blue †     |

(MISO pad: unconnected for the display — wired only if you add the optional
microSD; see below.)

> † Reused color. Yellow is also SCK and blue is also MOSI, but SCK/MO are on the
> **bottom** header row while RST/Lite are in the **top-row** control cluster
> (27·33·32·14). Within that cluster all four are unique: 27 Yellow · 33 Green ·
> 32 White · 14 Blue.

## Recalibration button

Panel-mounted momentary switch, prewired red/black:

| Button lead | Feather pad | GPIO | Wire color (prewired) |
| ----------- | ----------- | ---- | --------------------- |
| red         | A1          | 25   | Red                   |
| black       | GND         | —    | Black                 |

Firmware uses `INPUT_PULLUP` on GPIO25; pressing pulls it to GND. No external
resistor needed (GPIO25 has internal pull-ups; it is not a strapping/boot pin).
Polarity is irrelevant for a plain switch — red→A1, black→GND is just the
factory wiring.

## Optional: microSD logging (future)

The round TFT board has an onboard microSD slot, and the 18-pin ribbon already
carries the SD's SPI signals to the EYESPI breakout — so adding the card needs
**only two more wires** from the Feather to the breakout (no ribbon change). The
SD shares the display's SPI bus (`SCK`/`MO`) and just needs the read line plus its
own chip-select:

| EYESPI pad | Feather pad | GPIO | Purpose                     |
| ---------- | ----------- | ---- | --------------------------- |
| MISO       | MI (bottom) | 21   | SPI data out (SD → Feather) |
| SDCS       | A5          | 4    | SD chip select              |

- `MISO` is the line the display didn't need (it's write-only); the SD needs it to read.
- `SDCS` is separate from the display's `TCS`, so the display and SD share `SCK`/`MOSI`/`MISO`
  and are each picked by their own chip-select. `A5` (GPIO4) is free and not a strapping pin.
- Firmware would use `SD_CS_PIN 4` over the shared hardware SPI. Until/unless added, data
  logging lives on internal flash (LittleFS); the SD is the optional upgrade for
  unlimited / removable (CSV) history.

## Pins left free / system-reserved (do not solder to)

GPIO22/20 (I2C QT), GPIO5/19/21 (SPI), GPIO0 (NeoPixel), GPIO2 (I2C power rail),
GPIO13 (red LED), GPIO35 (battery monitor), GPIO38 (onboard SW38 button).
GPIO4 (A5) is earmarked for the optional microSD chip-select (above).
Avoid GPIO12 (boot strapping) and the input-only pins (34/36/37/39).
