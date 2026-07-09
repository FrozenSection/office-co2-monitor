#pragma once

// =====================================================================
// I2C — STEMMA QT chain (SCD-41 -> DS3231)
// =====================================================================
#define I2C_ADDR_SCD41      0x62
#define I2C_ADDR_DS3231     0x68
#define I2C_ADDR_RTC_EEPROM 0x57   // AT24C32 on the DS3231 module (harmless)
#define I2C_ADDR_VEML7700   0x10   // optional ambient-light sensor (auto-detected)
#define I2C_ADDR_MAX17048   0x36   // optional LiPo fuel gauge (auto-detected, later)

// The Feather V2 powers its STEMMA QT / I2C port through this pin.
// It MUST be driven HIGH before Wire.begin() or NO I2C device responds
// (classic "all my sensors are dead" red herring).
#ifndef NEOPIXEL_I2C_POWER
#define NEOPIXEL_I2C_POWER 2
#endif

// =====================================================================
// Forced recalibration (FRC) — the "fresh air walk"
// =====================================================================
#define FRC_REFERENCE_PPM   420   // outdoor fresh-air CO2 reference
#define FRC_EQUILIBRATE_SEC 180   // settle time outside before FRC fires
#define RECAL_BUTTON_PIN    25    // A1 — momentary btn to GND, INPUT_PULLUP

#define BTN_DEBOUNCE_MS     25
#define BTN_DBL_MS          350   // double-tap window
#define BTN_HOLD_MS         3000  // hold this long -> enter WiFi config
#define BTN_XHOLD_MS        10000 // keep holding -> reboot (no power switch in enclosure)
#define RECAL_CONFIRM_SEC   10    // confirm screen auto-cancels after this
#define RECAL_RESULT_SEC    6     // result screen dwell before returning

// =====================================================================
// Calibration-confidence tiers (days since last FRC).
// Logic only — palette is deliberately kept separate (designer TODO).
// =====================================================================
#define CAL_AGING_DAYS    7    // gentle hint
#define CAL_STALE_DAYS    14   // persistent prompt
#define CAL_OVERDUE_DAYS  21   // grey the reading, mark unverified

// =====================================================================
// Display — EYESPI / SPI (GC9A01 240x240 round), hard-wired to Feather V2.
// Pins verified against Adafruit Feather V2 + EYESPI breakout pinouts.
// Shared hardware SPI bus: SCK=5, MOSI=19, MISO=21 (MISO unused — display
// is write-only, not wired). EYESPI Vin -> Feather 3V (no level shifting).
// =====================================================================
#define TFT_CS_PIN   33   // EYESPI TCS  (clean GPIO)
#define TFT_DC_PIN   32   // EYESPI DC   (clean GPIO)
#define TFT_RST_PIN  27   // EYESPI RST  (clean GPIO)
#define TFT_LITE_PIN 14   // EYESPI Lite — backlight, LEDC PWM for dimming

// Optional microSD (future) shares the display SPI bus; needs MISO (GPIO21,
// Feather MI) wired + its own chip-select. See docs/wiring.md.
#define SD_CS_PIN    4    // A5 — microSD chip select (only if SD is added)

// =====================================================================
// Runtime-settings defaults. These seed the NVS-backed Settings store on
// first boot (see settings.h); thereafter the saved values win and are
// editable from the config web UI. Pins/addresses above stay compile-time.
// =====================================================================
#define DEFAULT_ROTATION    0     // 0..3 — finalize once enclosure mount is set
#define DEFAULT_BRIGHTNESS  200   // 0..255 backlight PWM duty
#define DEFAULT_TEMP_UNIT_F true  // show degF (vs degC)

#define AQ_GOOD_PPM   800    // <= GOOD ; <= FAIR ; <= POOR ; else BAD
#define AQ_FAIR_PPM   1200
#define AQ_POOR_PPM   1500

#define DEFAULT_TZ          "UTC0"   // POSIX TZ string; set real zone in UI
#define DEFAULT_HOSTNAME    "stuffy" // mDNS (<name>.local) / AP name base
#define DEFAULT_STA_ENABLED false    // stay on home WiFi + serve LAN page

// Data logging (internal LittleFS)
#define DEFAULT_LOG_INTERVAL_SEC 300   // sample period (tunable in settings)
#define LOG_BOOT_QUIET_SEC       300   // no logging this long after boot: a reboot
                                       // (esp. OTA) leaves a real heat pulse in the
                                       // enclosure that reads as hot/dry garbage rows
#define LOG_MAX_RECS_PER_FILE    8000  // ~72KB/file; 2 files -> ~16k records
#define LOG_GRAPH_MAX_POINTS     600   // downsample the chart to this many

// SCD-41 compensation
#define DEFAULT_ALTITUDE_M       0     // meters above sea level
#define DEFAULT_TEMP_OFFSET_C10  40    // temperature offset, degC*10 (SCD default 4.0)

// Forced-recalibration quality gate (the "fresh air walk")
#define FRC_STABLE_BAND_PPM      30    // max spread over the last samples to commit
#define FRC_PLAUSIBLE_LO_PPM     250   // outdoor reading must be within this range
#define FRC_PLAUSIBLE_HI_PPM     600
#define SENSOR_STALE_SEC         30    // no good read for this long -> mark stale
#define SENSOR_FAULT_BOOT_SEC    30    // no first reading by this uptime -> show fault

// Auto-brightness (active only when a VEML7700 is detected). Lux maps to a
// 0..1 position LOGARITHMICALLY (lux spans decades), then to a perceptual
// brightness between BRIGHT_MIN/MAX, then to PWM duty through a GAMMA curve so
// the low end dims smoothly. Tune the lux endpoints to your enclosure window.
#define DEFAULT_AUTO_BRIGHTNESS true
#define DEFAULT_BRIGHT_MIN  15     // night floor (perceptual 0..255; never fully dark)
#define DEFAULT_BRIGHT_MAX  255    // bright-room ceiling (perceptual)
#define DEFAULT_LUX_LOW     3      // lux at/below this -> min brightness (>=1 for log)
#define DEFAULT_LUX_HIGH    400    // lux at/above this -> max brightness
#define DEFAULT_GAMMA_X10   22     // perceptual dimming gamma * 10 (2.2)

// Backlight PWM (LEDC). 12-bit so the gamma'd low end resolves smoothly.
#define BL_PWM_FREQ  10000   // Hz (no visible/audible artifacts)
#define BL_PWM_BITS  12
#define BL_PWM_MAX   4095    // (1 << BL_PWM_BITS) - 1
