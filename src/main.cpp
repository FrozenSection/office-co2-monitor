// office-co2-monitor
// Phase 7: button + guided recalibration. A single momentary button drives a
// small state machine: tap -> recalibration flow (confirm -> equilibration
// countdown -> forced recalibration -> result); 3-sec hold -> WiFi config
// (stub until the portal is built). The equilibration gate makes it hard to
// recalibrate wrong, which is worse than not recalibrating at all.

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_GC9A01A.h>
#include <Fonts/FreeSansBold24pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSans9pt7b.h>
#include <SensirionI2cScd4x.h>
#include <Adafruit_VEML7700.h>
#include <Adafruit_MAX1704X.h>
#include <RTClib.h>
#include <time.h>
#include <esp_system.h>

#include "config.h"
#include "version.h"
#include "settings.h"
#include "portal.h"
#include "qrcode.h"
#include "datalog.h"

static Adafruit_GC9A01A tft(TFT_CS_PIN, TFT_DC_PIN, TFT_RST_PIN);
static SensirionI2cScd4x scd4x;
static Adafruit_VEML7700 veml;
static Adafruit_MAX17048 maxlipo;
static RTC_DS3231 rtc;

static bool     hasLux = false;
static bool     hasRtc = false;
static bool     hasBatt = false;
static float    gBattPct = 0, gBattV = 0, gBattRate = 0;   // rate: %/hr, +charging
static float    gLux   = -1;
static uint16_t gCo2   = 0;
static float    gTempC = 0, gHum = 0;

static bool     gTimeValid = false;     // RTC has a valid (set) time
static int      gHh = 0, gMm = 0;
static uint32_t gNowEpoch = 0;
static int      gTrend = 0;             // CO2 trend: -1 falling, 0 flat, +1 rising
static int      gBrightness = 200;      // last applied backlight duty (for diag)

static uint16_t gFrcCorr = 0;           // last FRC correction word
static bool     gFrcOk   = false;

static uint32_t gLastReadMs = 0;        // millis() of last good SCD-41 read
static bool     gStale      = false;    // no fresh reading for SENSOR_STALE_SEC
static bool     gSensorFault = false;   // no first reading by SENSOR_FAULT_BOOT_SEC

// Why an FRC was refused (so the result screen can explain).
enum FrcFail { FRCF_NONE, FRCF_UNSTABLE, FRCF_IMPLAUSIBLE, FRCF_SENSOR };
static FrcFail  gFrcFail = FRCF_NONE;
static uint16_t gFrcLast = 0;           // reading the gate judged

// Ring of CO2 samples taken during equilibration; FRC commits only if these
// are tight (the air has settled) and plausible for outdoors.
static uint16_t gEqBuf[8];
static uint8_t  gEqN = 0, gEqHead = 0;
static void eqReset() { gEqN = 0; gEqHead = 0; }
static void eqPush(uint16_t co2) {
  gEqBuf[gEqHead] = co2;
  gEqHead = (gEqHead + 1) % 8;
  if (gEqN < 8) gEqN++;
}

enum CalState { CAL_UNKNOWN, CAL_FRESH, CAL_AGING, CAL_STALE, CAL_OVERDUE };
enum AppState { ST_MAIN, ST_CONFIRM, ST_EQUIL, ST_RESULT, ST_WIFI };
enum BtnEv    { BTN_NONE, BTN_TAP, BTN_DBL, BTN_HOLD, BTN_XHOLD };
enum View     { VIEW_CO2, VIEW_TIME, VIEW_DIAG, VIEW_COUNT };

static AppState gState      = ST_MAIN;
static uint32_t gStateStart = 0;
static View     gView       = VIEW_CO2;

// ---- small helpers ---------------------------------------------------------

static inline uint16_t cGrey()   { return tft.color565(130, 130, 130); }
static inline uint16_t cOrange() { return tft.color565(255, 140, 0); }
static inline uint16_t cSec()    { return tft.color565(0xB8, 0xBD, 0xC4); }  // secondary
static inline uint16_t cFaint()  { return tft.color565(0x8A, 0x8F, 0x96); }  // faint label
static inline uint16_t cBlue()   { return tft.color565(0x3E, 0x8B, 0xF0); }  // calibration blue

// Backlight: map a PERCEPTUAL level (0..255) to PWM duty through a gamma curve,
// so equal level steps look like equal brightness steps and the low end dims
// smoothly. 12-bit LEDC gives the gamma'd floor enough distinct levels.
static void setBacklight(int level) {
  if (level < 0) level = 0; else if (level > 255) level = 255;
  float g = settings::cfg.gammaX10 / 10.0f;
  uint32_t duty = (uint32_t)(powf(level / 255.0f, g) * BL_PWM_MAX + 0.5f);
  if (level > 0 && duty == 0) duty = 1;     // never fully dark when level > 0
  ledcWrite(TFT_LITE_PIN, duty);
}

static void enableI2CPower() {
  pinMode(NEOPIXEL_I2C_POWER, OUTPUT);
  digitalWrite(NEOPIXEL_I2C_POWER, HIGH);
  delay(50);
}

static void scanBus() {
  bool scd = false, rtcFound = false;
  Serial.println(F("I2C scan:"));
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("  found 0x%02X\n", addr);
      if (addr == I2C_ADDR_SCD41)  scd = true;
      if (addr == I2C_ADDR_DS3231) rtcFound = true;
    }
  }
  Serial.printf("  SCD-41 (0x62): %s\n", scd ? "OK" : "MISSING");
  Serial.printf("  DS3231 (0x68): %s\n\n", rtcFound ? "OK" : "MISSING");
}

static const char* resetReasonStr() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:  return "power-on";
    case ESP_RST_SW:       return "sw-restart";
    case ESP_RST_PANIC:    return "panic";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:      return "watchdog";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_DEEPSLEEP:return "deepsleep";
    default:               return "other";
  }
}

static void logScdError(const char* ctx, int16_t err) {
  if (err == 0) return;
  char msg[64];
  errorToString((uint16_t)err, msg, sizeof(msg));
  Serial.printf("SCD-41 %s error: %s\n", ctx, msg);
}

static uint16_t co2Color(uint16_t co2, const char** label) {
  if (co2 <= settings::cfg.aqGood) { *label = "GOOD"; return GC9A01A_GREEN; }
  if (co2 <= settings::cfg.aqFair) { *label = "FAIR"; return GC9A01A_YELLOW; }
  if (co2 <= settings::cfg.aqPoor) { *label = "POOR"; return cOrange(); }
  *label = "BAD";
  return GC9A01A_RED;
}

static CalState calState(bool timeValid, uint32_t nowEpoch) {
  if (!timeValid || settings::cfg.lastFrcEpoch == 0) return CAL_UNKNOWN;
  // guard: FRC done on a fast clock, then NTP corrects backwards -> would underflow
  uint32_t days = (nowEpoch > settings::cfg.lastFrcEpoch)
                    ? (nowEpoch - settings::cfg.lastFrcEpoch) / 86400UL : 0;
  if (days >= settings::cfg.calOverdueDays) return CAL_OVERDUE;
  if (days >= settings::cfg.calStaleDays)   return CAL_STALE;
  if (days >= settings::cfg.calAgingDays)   return CAL_AGING;
  return CAL_FRESH;
}

static void drawRing(uint16_t color) {
  for (int r = 112; r <= 119; r++) tft.drawCircle(120, 120, r, color);
}

// Fill an annular sector (degrees; 0 = 12 o'clock, clockwise) with radial lines.
static void fillArc(int cx, int cy, int rIn, int rOut, float a0, float a1, uint16_t color) {
  for (float a = a0; a <= a1; a += 0.5f) {
    float rad = (a - 90.0f) * 0.01745329f;
    float c = cosf(rad), s = sinf(rad);
    tft.drawLine(cx + (int)(rIn * c), cy + (int)(rIn * s),
                 cx + (int)(rOut * c), cy + (int)(rOut * s), color);
  }
}

// Render a QR code centered on the display: white quiet-zone square with black
// modules. Sized to stay inside the round panel.
static void drawQR(const char* text) {
  uint8_t version = (strlen(text) <= 53) ? 3 : 4;
  uint8_t buf[qrcode_getBufferSize(4)];
  QRCode qr;
  qrcode_initText(&qr, buf, version, ECC_LOW, text);

  const int quiet = 2;
  int total = qr.size + 2 * quiet;
  int scale = 150 / total;
  if (scale < 3) scale = 3;
  int side = total * scale;
  int x0 = 120 - side / 2, y0 = 120 - side / 2;

  tft.fillRect(x0, y0, side, side, GC9A01A_WHITE);
  for (uint8_t y = 0; y < qr.size; y++)
    for (uint8_t x = 0; x < qr.size; x++)
      if (qrcode_getModule(&qr, x, y))
        tft.fillRect(x0 + (quiet + x) * scale, y0 + (quiet + y) * scale,
                     scale, scale, GC9A01A_BLACK);
}

// ---- main screen -----------------------------------------------------------

static uint16_t    mLastCo2;
static uint16_t    mLastRing;
static const char* mLastLabel;
static CalState    mLastCal;
static int         mLastTrend;
static char        mLastTime[8];
static char        mLastBot[24];
static bool        mLastStale;
static int         mLastBatt;
static bool        mFaultDrawn;

// Cool-family calibration colors (distinct from the AQ tier ramp).
static uint16_t calColor(CalState c) {
  switch (c) {
    case CAL_FRESH:   return tft.color565(0x2B, 0xD4, 0xC4);
    case CAL_AGING:   return tft.color565(0x3E, 0x8B, 0xF0);
    case CAL_STALE:   return tft.color565(0x7A, 0x6C, 0xF0);
    case CAL_OVERDUE: return tft.color565(0xB0, 0x5C, 0xF0);
    default:          return 0;
  }
}

// Draw text centered on (cx,cy) in a GFX font (uses the glyph bounding box).
static void drawTextC(const GFXfont* f, int cx, int cy, uint16_t color, const char* s) {
  tft.setFont(f);
  tft.setTextSize(1);   // custom fonts always 1x (modals leave size at 2-5)
  tft.setTextColor(color);
  int16_t x1, y1; uint16_t w, h;
  tft.getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
  tft.setCursor(cx - w / 2 - x1, cy - h / 2 - y1);
  tft.print(s);
}

static void zoneC(const GFXfont* f, int cx, int cy, int boxW, int boxH,
                  uint16_t color, const char* s) {
  tft.fillRect(cx - boxW / 2, cy - boxH / 2, boxW, boxH, GC9A01A_BLACK);
  if (s && *s) drawTextC(f, cx, cy, color, s);
}

// Draw "CO2"+suffix centered at (cx,cy) with the 2 subscripted (smaller, lower).
static void drawCO2(const GFXfont* mf, const GFXfont* sf, int cx, int cy,
                    uint16_t color, const char* suffix) {
  int16_t x1, y1; uint16_t wCO, h;
  tft.setFont(mf); tft.setTextSize(1);
  tft.getTextBounds("CO", 0, 0, &x1, &y1, &wCO, &h);
  int16_t s1, s2; uint16_t w2, h2;
  tft.setFont(sf);
  tft.getTextBounds("2", 0, 0, &s1, &s2, &w2, &h2);
  uint16_t wSuf = 0;
  if (suffix && *suffix) {
    int16_t a, b; uint16_t hh;
    tft.setFont(mf); tft.getTextBounds(suffix, 0, 0, &a, &b, &wSuf, &hh);
  }
  int baseY = cy - y1 - (int)h / 2;
  int x0 = cx - ((int)wCO + (int)w2 + (int)wSuf) / 2;
  tft.setTextColor(color);
  tft.setFont(mf); tft.setCursor(x0, baseY);                         tft.print("CO");
  tft.setFont(sf); tft.setCursor(tft.getCursorX(), baseY + (int)h / 4); tft.print("2");
  if (suffix && *suffix) {
    tft.setFont(mf); tft.setCursor(tft.getCursorX(), baseY);         tft.print(suffix);
  }
}

// Trend glyph: up triangle / down triangle / flat dash.
static void drawTrend(int cx, int cy, int dir, uint16_t color) {
  if (dir > 0)      tft.fillTriangle(cx, cy - 6, cx - 6, cy + 5, cx + 6, cy + 5, color);
  else if (dir < 0) tft.fillTriangle(cx, cy + 6, cx - 6, cy - 5, cx + 6, cy - 5, color);
  else              tft.fillRect(cx - 6, cy - 2, 12, 4, color);
}

// Status word + trend glyph, centered as a group.
static void drawStatusTrend(int cy, uint16_t color, const char* word, int dir) {
  tft.fillRect(40, cy - 16, 160, 32, GC9A01A_BLACK);   // narrow: stay inside the ring
  tft.setFont(&FreeSansBold12pt7b);
  tft.setTextSize(1);
  int16_t x1, y1; uint16_t w, h;
  tft.getTextBounds(word, 0, 0, &x1, &y1, &w, &h);
  const int gap = 11, tri = 13;
  int x0 = 120 - ((int)w + gap + tri) / 2;
  tft.setTextColor(color);
  tft.setCursor(x0 - x1, cy - h / 2 - y1);
  tft.print(word);
  // trend is NEUTRAL (grey) — direction only; the tier color carries quality
  drawTrend(x0 + (int)w + gap + tri / 2, cy, dir, cSec());
}

static uint16_t battColor(int pct) {
  if (pct <= 15) return GC9A01A_RED;
  if (pct <= 35) return cOrange();
  return cSec();                       // healthy: subtle grey, not attention-grabbing
}

// Small horizontal battery glyph: outline + terminal nub + proportional fill.
static void drawBattery(int cx, int cy, int pct, uint16_t color) {
  const int w = 24, h = 12, nub = 2;
  int x = cx - (w + nub) / 2, y = cy - h / 2;
  tft.drawRect(x, y, w, h, color);
  tft.fillRect(x + w, y + h / 2 - 3, nub, 6, color);
  int fw = (w - 4) * pct / 100;
  if (fw < 0) fw = 0; else if (fw > w - 4) fw = w - 4;
  if (fw > 0) tft.fillRect(x + 2, y + 2, fw, h - 4, color);
}

// Battery glyph — top center, mirrors the cal dot; only when a gauge is present.
// Shared by the normal main view and the sensor-fault screen.
static void updateBattGlyph() {
  if (!hasBatt) return;
  int p = (int)(gBattPct + 0.5f);
  if (p < 0) p = 0; else if (p > 100) p = 100;
  if (p != mLastBatt) {
    tft.fillRect(102, 16, 36, 16, GC9A01A_BLACK);
    drawBattery(120, 24, p, battColor(p));
    mLastBatt = p;
  }
}

// Design 1 "Aperture": tier ring, white number, faint label, colored status +
// trend, secondary temp/RH, cool calibration dot (shown only when aging+).
static void mainScreenEnter() {
  mLastCo2 = 0xFFFF; mLastRing = 0; mLastLabel = nullptr;
  mLastCal = (CalState)255; mLastTrend = 99; mLastStale = false; mLastBatt = -999;
  mFaultDrawn = false;
  mLastTime[0] = '\0'; mLastBot[0] = '\0';
  tft.fillScreen(GC9A01A_BLACK);
  drawCO2(&FreeSans9pt7b, &FreeSans9pt7b, 120, 136, cFaint(), " ppm");
  zoneC(&FreeSans12pt7b, 120, 102, 200, 26, GC9A01A_WHITE,  "warming up");
  zoneC(&FreeSans12pt7b, 120,  56,  90, 24, cFaint(),       "--:--");
}

static void renderMain(uint16_t co2, float tempC, float hum,
                       bool timeValid, int hh, int mm, CalState cal) {
  const char* label;
  uint16_t tier     = co2Color(co2, &label);
  bool     stale    = gStale;
  // grey = don't trust this number: stale reading, or calibration overdue
  uint16_t numColor = (stale || cal == CAL_OVERDUE) ? cGrey() : GC9A01A_WHITE;

  if (tier != mLastRing) { drawRing(tier); mLastRing = tier; }

  // big number (ring + status carry the tier color)
  if (co2 != mLastCo2 || cal != mLastCal || stale != mLastStale) {
    char num[8];
    snprintf(num, sizeof(num), "%u", co2);
    zoneC(&FreeSansBold24pt7b, 120, 102, 180, 44, numColor, num);
    mLastCo2 = co2;
  }

  // status word + trend ("STALE" in amber overrides the tier word)
  if (label != mLastLabel || cal != mLastCal || gTrend != mLastTrend || stale != mLastStale) {
    if (stale) drawStatusTrend(164, cOrange(), "STALE", gTrend);
    else       drawStatusTrend(164, tier, label, gTrend);
    mLastLabel = label;
    mLastTrend = gTrend;
  }
  mLastStale = stale;

  // time
  char ts[8];
  if (timeValid) snprintf(ts, sizeof(ts), "%02d:%02d", hh, mm);
  else           strcpy(ts, "--:--");
  if (strcmp(ts, mLastTime) != 0) {
    zoneC(&FreeSans12pt7b, 120, 56, 90, 24, timeValid ? cSec() : cFaint(), ts);
    strcpy(mLastTime, ts);
  }

  // temp + humidity
  float tShow = settings::cfg.tempUnitF ? (tempC * 9.0f / 5.0f + 32.0f) : tempC;
  char  tUnit = settings::cfg.tempUnitF ? 'F' : 'C';
  char  bot[24];
  snprintf(bot, sizeof(bot), "%d%c   %d%%",
           (int)(tShow + 0.5f), tUnit, (int)(hum + 0.5f));
  if (strcmp(bot, mLastBot) != 0) {
    zoneC(&FreeSans12pt7b, 120, 191, 140, 22, cSec(), bot);
    strcpy(mLastBot, bot);
  }

  // calibration dot — cool color, hidden unless aging+
  if (cal != mLastCal) {
    tft.fillRect(112, 208, 16, 16, GC9A01A_BLACK);
    if (cal == CAL_AGING || cal == CAL_STALE || cal == CAL_OVERDUE)
      tft.fillCircle(120, 216, 5, calColor(cal));
    mLastCal = cal;
  }

  updateBattGlyph();
}

// ---- time view (clock-prominent) -------------------------------------------
static uint16_t tLastCo2;
static bool     tLastStale;
static char     tLastClock[8];
static char     tLastBot[24];

static void timeViewEnter() {
  tLastCo2 = 0xFFFF; tLastStale = false; tLastClock[0] = '\0'; tLastBot[0] = '\0';
  tft.fillScreen(GC9A01A_BLACK);
  for (int r = 115; r <= 118; r++)
    tft.drawCircle(120, 120, r, tft.color565(0x5A, 0x4E, 0x1E));   // dimmed ring
  drawCO2(&FreeSans9pt7b, &FreeSans9pt7b, 120, 88, cFaint(), " ppm");
}

static void renderTimeView() {
  // small CO2 (tier dot + number) up top — greyed when stale/absent, so a
  // frozen number can't read as live on this view either
  if (gCo2 != tLastCo2 || gStale != tLastStale) {
    const char* label; uint16_t tier = co2Color(gCo2, &label);
    if (gCo2 == 0 || gStale) tier = cGrey();     // no reading / stale: neutral dot
    tft.fillRect(40, 56, 160, 22, GC9A01A_BLACK);
    char num[8];
    if (gCo2 == 0) strcpy(num, "--");
    else           snprintf(num, sizeof(num), "%u", gCo2);
    tft.setFont(&FreeSans12pt7b);
    tft.setTextSize(1);
    int16_t x1, y1; uint16_t w, h;
    tft.getTextBounds(num, 0, 0, &x1, &y1, &w, &h);
    const int dot = 10, gap = 8;
    int x0 = 120 - (dot + gap + (int)w) / 2;
    tft.fillCircle(x0 + dot / 2, 67, dot / 2, tier);
    tft.setTextColor((gCo2 == 0 || gStale) ? cFaint() : cSec());
    tft.setCursor(x0 + dot + gap - x1, 67 - h / 2 - y1);
    tft.print(num);
    tLastCo2 = gCo2;
    tLastStale = gStale;
  }
  // big clock
  char clk[8];
  if (gTimeValid) snprintf(clk, sizeof(clk), "%02d:%02d", gHh, gMm);
  else            strcpy(clk, "--:--");
  if (strcmp(clk, tLastClock) != 0) {
    zoneC(&FreeSansBold24pt7b, 120, 124, 200, 48,
          gTimeValid ? GC9A01A_WHITE : cFaint(), clk);
    strcpy(tLastClock, clk);
  }
  // temp / RH
  float tShow = settings::cfg.tempUnitF ? (gTempC * 9.0f / 5.0f + 32.0f) : gTempC;
  char  u = settings::cfg.tempUnitF ? 'F' : 'C';
  char  bot[24];
  snprintf(bot, sizeof(bot), "%d%c   %d%%", (int)(tShow + 0.5f), u, (int)(gHum + 0.5f));
  if (strcmp(bot, tLastBot) != 0) {
    zoneC(&FreeSans12pt7b, 120, 178, 140, 22, cSec(), bot);
    strcpy(tLastBot, bot);
  }
}

// ---- diagnostics view ------------------------------------------------------
static char dLastRow[7][40];

static void diagViewEnter() {
  for (int i = 0; i < 7; i++) dLastRow[i][0] = '\0';
  tft.fillScreen(GC9A01A_BLACK);
  tft.drawCircle(120, 120, 118, tft.color565(0x46, 0x46, 0x46));   // brighter neutral ring
  tft.drawCircle(120, 120, 117, tft.color565(0x46, 0x46, 0x46));
}

// Per-row change detection: only repaint a row whose value changed (no blink).
static void diagRow(int idx, int cy, uint16_t color, const char* s) {
  if (strcmp(s, dLastRow[idx]) == 0) return;
  strncpy(dLastRow[idx], s, sizeof(dLastRow[idx]) - 1);
  dLastRow[idx][sizeof(dLastRow[idx]) - 1] = '\0';
  tft.fillRect(36, cy - 12, 168, 24, GC9A01A_BLACK);
  drawTextC(&FreeSans9pt7b, 120, cy, color, s);
}

static void renderDiagView() {
  char line[40];
  uint32_t up = millis() / 1000;

  // network
  if (portal::staActive())     diagRow(0, 60, GC9A01A_CYAN, portal::staIp());
  else if (portal::apActive()) diagRow(0, 60, cOrange(), "AP setup mode");
  else                         diagRow(0, 60, cFaint(), "wifi off");

  if (portal::staActive()) {
    snprintf(line, sizeof(line), "rssi %d dBm", portal::rssi());
    diagRow(1, 82, cSec(), line);
  } else {
    snprintf(line, sizeof(line), "%s.local", settings::cfg.hostname);
    diagRow(1, 82, cFaint(), line);
  }

  // battery (or sensor-presence fallback when no fuel gauge is fitted)
  if (hasBatt) {
    int p = (int)(gBattPct + 0.5f);
    snprintf(line, sizeof(line), "batt %d%%  %.2fV", p, gBattV);
    diagRow(2, 104, battColor(p), line);
  } else {
    snprintf(line, sizeof(line), "scd41%s%s", hasRtc ? " rtc" : "", hasLux ? " lux" : "");
    diagRow(2, 104, cSec(), line);
  }

  // identity (middle)
  if (up < 86400) snprintf(line, sizeof(line), "v%s  up %lu:%02lu", FIRMWARE_VERSION,
                           (unsigned long)(up / 3600), (unsigned long)((up % 3600) / 60));
  else            snprintf(line, sizeof(line), "v%s  up %lud %luh", FIRMWARE_VERSION,
                           (unsigned long)(up / 86400), (unsigned long)((up % 86400) / 3600));
  diagRow(3, 126, GC9A01A_WHITE, line);

  // lux + brightness
  int br = (hasLux && settings::cfg.autoBrightness) ? gBrightness : settings::cfg.brightness;
  if (hasLux) snprintf(line, sizeof(line), "lux %ld   br %d%%", (long)gLux, br * 100 / 255);
  else        snprintf(line, sizeof(line), "br %d%%", br * 100 / 255);
  diagRow(4, 148, cSec(), line);

  // calibration
  CalState cal = calState(gTimeValid, gNowEpoch);
  if (cal == CAL_UNKNOWN) strcpy(line, "cal: not set");
  else {
    uint32_t days = (gNowEpoch > settings::cfg.lastFrcEpoch)
                      ? (gNowEpoch - settings::cfg.lastFrcEpoch) / 86400UL : 0;
    snprintf(line, sizeof(line), "cal: %ud ago", (unsigned)days);
  }
  diagRow(5, 170, cSec(), line);

  // free heap
  snprintf(line, sizeof(line), "heap %luk", (unsigned long)(ESP.getFreeHeap() / 1024));
  diagRow(6, 190, cFaint(), line);
}

// ---- view dispatch ---------------------------------------------------------
static void enterView() {
  switch (gView) {
    case VIEW_TIME: timeViewEnter(); break;
    case VIEW_DIAG: diagViewEnter(); break;
    default:        mainScreenEnter(); break;
  }
}

static void renderView() {
  switch (gView) {
    case VIEW_TIME: renderTimeView(); break;
    case VIEW_DIAG: renderDiagView(); break;
    default:
      if (gCo2 != 0)
        renderMain(gCo2, gTempC, gHum, gTimeValid, gHh, gMm,
                   calState(gTimeValid, gNowEpoch));
      else {                                     // no reading yet: warming up or fault
        if (gSensorFault && !mFaultDrawn) {
          zoneC(&FreeSans12pt7b, 120, 102, 200, 26, GC9A01A_RED, "no sensor");
          mFaultDrawn = true;
        }
        // the RTC and gauge still work — keep the clock and battery live
        char ts[8];
        if (gTimeValid) snprintf(ts, sizeof(ts), "%02d:%02d", gHh, gMm);
        else            strcpy(ts, "--:--");
        if (strcmp(ts, mLastTime) != 0) {
          zoneC(&FreeSans12pt7b, 120, 56, 90, 24, gTimeValid ? cSec() : cFaint(), ts);
          strcpy(mLastTime, ts);
        }
        updateBattGlyph();
      }
      break;
  }
}

// ---- recalibration-flow screens (modal; full clear is fine) ----------------

static void drawConfirm() {
  tft.fillScreen(GC9A01A_BLACK);
  drawTextC(&FreeSansBold12pt7b, 120, 84, GC9A01A_WHITE, "Recalibrate?");
  drawTextC(&FreeSans12pt7b, 120, 126, GC9A01A_GREEN, "tap = yes");
  drawTextC(&FreeSans12pt7b, 120, 158, cOrange(), "hold = cancel");
}

static void drawEquilTick(int remain) {
  // cool-blue progress ring fills as equilibration proceeds
  float prog = 1.0f - (float)remain / (float)FRC_EQUILIBRATE_SEC;
  if (prog < 0) prog = 0; else if (prog > 1) prog = 1;
  fillArc(120, 120, 110, 118, 0, prog * 360.0f, cBlue());
  // ppm up top, countdown down low (RECALIBRATING splits them, drawn on enter)
  char num[8]; snprintf(num, sizeof(num), "%u", gCo2);
  zoneC(&FreeSansBold24pt7b, 120, 76, 150, 40, GC9A01A_WHITE, num);
  char t[8]; snprintf(t, sizeof(t), "%d:%02d", remain / 60, remain % 60);
  zoneC(&FreeSans12pt7b, 120, 160, 100, 22, cSec(), t);
}

static void drawEquilEnter() {
  tft.fillScreen(GC9A01A_BLACK);
  fillArc(120, 120, 110, 118, 0, 360, tft.color565(0x1C, 0x1C, 0x1C));   // track
  drawTextC(&FreeSans9pt7b, 120, 106, cFaint(), "ppm");
  drawTextC(&FreeSansBold12pt7b, 120, 132, cBlue(), "RECALIBRATING");
  drawTextC(&FreeSans9pt7b, 120, 190, cFaint(), "hold to cancel");
  drawEquilTick(FRC_EQUILIBRATE_SEC);
}

static void drawResult() {
  tft.fillScreen(GC9A01A_BLACK);
  if (gFrcOk) {
    drawTextC(&FreeSansBold12pt7b, 120, 88, GC9A01A_GREEN, "Calibrated");
    char p[20]; snprintf(p, sizeof(p), "%u ppm", settings::cfg.frcReferencePpm);
    drawTextC(&FreeSans12pt7b, 120, 122, GC9A01A_WHITE, p);
    char c[20]; snprintf(c, sizeof(c), "corr %+d", (int)gFrcCorr - 0x8000);
    drawTextC(&FreeSans9pt7b, 120, 150, cFaint(), c);
    if (!gTimeValid)
      drawTextC(&FreeSans9pt7b, 120, 176, cOrange(), "clock unset: age n/a");
  } else {
    const char* l1 = "FRC failed";
    uint16_t    col = GC9A01A_RED;
    char        l2[24] = "sensor not ready";
    switch (gFrcFail) {
      case FRCF_IMPLAUSIBLE:
        l1 = "Not fresh air"; col = cOrange();
        snprintf(l2, sizeof(l2), "%u ppm (indoors?)", gFrcLast); break;
      case FRCF_UNSTABLE:
        l1 = "Air not settled"; col = cOrange();
        snprintf(l2, sizeof(l2), "wait, then retry"); break;
      case FRCF_SENSOR:
        l1 = "Sensor error"; snprintf(l2, sizeof(l2), "try again"); break;
      default: break;
    }
    drawTextC(&FreeSansBold12pt7b, 120, 104, col, l1);
    drawTextC(&FreeSans9pt7b, 120, 140, cFaint(), l2);
  }
}

static void drawWifiStatus() {
  uint16_t c = (portal::phase() == portal::P_SYNCED) ? GC9A01A_GREEN
             : (portal::phase() == portal::P_FAILED) ? GC9A01A_RED
                                                     : cOrange();
  tft.fillRect(40, 22, 160, 22, GC9A01A_BLACK);
  drawTextC(&FreeSans9pt7b, 120, 32, c, portal::statusLine());
}

static void drawWifiAP() {
  tft.fillScreen(GC9A01A_BLACK);
  drawWifiStatus();                          // top: status / instruction
  char wifi[80];
  snprintf(wifi, sizeof(wifi), "WIFI:T:nopass;S:%s;;", portal::apSsid());
  drawQR(wifi);                              // scan with phone camera to join
  drawTextC(&FreeSans9pt7b, 120, 214, cFaint(), "tap to exit");
}

static void drawWifiSTA() {
  tft.fillScreen(GC9A01A_BLACK);
  char ssid[24];
  if (strlen(settings::cfg.wifiSsid) > 16)
    snprintf(ssid, sizeof(ssid), "%.13s...", settings::cfg.wifiSsid);  // truncate long
  else
    snprintf(ssid, sizeof(ssid), "%s", settings::cfg.wifiSsid);
  drawTextC(&FreeSans12pt7b, 120, 60, GC9A01A_GREEN, ssid[0] ? ssid : "home wifi");
  drawTextC(&FreeSans9pt7b, 120, 92, cFaint(), "browse to:");
  drawTextC(&FreeSans12pt7b, 120, 116, GC9A01A_CYAN, portal::hostUrl());
  drawTextC(&FreeSans12pt7b, 120, 142, GC9A01A_CYAN, portal::staIp());
  drawTextC(&FreeSans9pt7b, 120, 198, cFaint(), "tap to exit");
}

static void drawSplash() {
  tft.fillScreen(GC9A01A_BLACK);
  tft.drawCircle(120, 92, 22, GC9A01A_GREEN);
  tft.drawCircle(120, 92, 21, GC9A01A_GREEN);
  tft.fillCircle(120, 92, 8, GC9A01A_GREEN);
  drawCO2(&FreeSansBold12pt7b, &FreeSans9pt7b, 120, 142, GC9A01A_WHITE, " Monitor");
  drawTextC(&FreeSans9pt7b, 120, 168, cFaint(), "v" FIRMWARE_VERSION);
}

// ---- actions ---------------------------------------------------------------

// Gate the calibration on the equilibration samples, then (if they pass) run the
// SCD-41 FRC. A bad FRC is worse than none, so refuse implausible or unsettled air.
static void commitFRC() {
  gFrcFail = FRCF_NONE;
  gFrcOk   = false;
  gFrcLast = gCo2;

  if (gCo2 == 0 || gStale) {             // no reading at all, or it went quiet
    gFrcFail = FRCF_SENSOR;
    Serial.println(gCo2 == 0 ? F("FRC refused: no reading") : F("FRC refused: reading is stale"));
    datalog::event(gTimeValid ? gNowEpoch : 0,
                   gCo2 == 0 ? "frc refused: no reading" : "frc refused: stale sensor");
    return;
  }

  uint16_t mn = 0xFFFF, mx = 0;
  for (uint8_t i = 0; i < gEqN; i++) {
    if (gEqBuf[i] < mn) mn = gEqBuf[i];
    if (gEqBuf[i] > mx) mx = gEqBuf[i];
  }
  uint16_t spread = (gEqN > 0) ? (uint16_t)(mx - mn) : 9999;

  // 1) plausibility — did we actually take it to outdoor air?
  if (gCo2 < FRC_PLAUSIBLE_LO_PPM || gCo2 > FRC_PLAUSIBLE_HI_PPM) {
    gFrcFail = FRCF_IMPLAUSIBLE;
    Serial.printf("FRC refused: %u ppm not plausible outdoor air\n", gCo2);
    datalog::event(gTimeValid ? gNowEpoch : 0, "frc refused: implausible reading");
    return;
  }
  // 2) stability — has the reading settled?
  if (gEqN < 4 || spread > FRC_STABLE_BAND_PPM) {
    gFrcFail = FRCF_UNSTABLE;
    Serial.printf("FRC refused: unstable (spread %u ppm / %u samples)\n", spread, gEqN);
    datalog::event(gTimeValid ? gNowEpoch : 0, "frc refused: air not settled");
    return;
  }

  Serial.println(F("FRC: gate passed, stopping measurement"));
  logScdError("stop", scd4x.stopPeriodicMeasurement());
  delay(500);

  uint16_t corr = 0;
  int16_t err = scd4x.performForcedRecalibration(settings::cfg.frcReferencePpm, corr);
  gFrcCorr = corr;
  gFrcOk   = (err == 0 && corr != 0xFFFF);
  int corrPpm = (corr == 0xFFFF) ? 0 : (int)corr - 0x8000;
  if (err) logScdError("FRC", err);
  Serial.printf("FRC: target=%u corr=%+d ppm -> %s\n",
                settings::cfg.frcReferencePpm, corrPpm, gFrcOk ? "OK" : "FAILED");
  if (gFrcOk && abs(corrPpm) > 200)
    Serial.printf("FRC: WARNING unusually large correction (%+d ppm)\n", corrPpm);

  logScdError("start", scd4x.startPeriodicMeasurement());

  if (gFrcOk) {
    char ev[48];
    snprintf(ev, sizeof(ev), "frc ok: ref %u, corr %+d", settings::cfg.frcReferencePpm, corrPpm);
    datalog::event(gTimeValid ? gNowEpoch : 0, ev);
    if (gTimeValid) { settings::markRecalibrated(gNowEpoch); Serial.println(F("FRC: age saved")); }
    else            Serial.println(F("FRC: ok, but clock unset -> age not tracked yet"));
  } else {
    gFrcFail = FRCF_SENSOR;
    datalog::event(gTimeValid ? gNowEpoch : 0, "frc failed: sensor error");
  }
}

static void refreshTime() {
  gTimeValid = false;
  gNowEpoch  = 0;
  if (hasRtc && !rtc.lostPower()) {
    gNowEpoch = rtc.now().unixtime();        // RTC holds UTC
    time_t t = (time_t)gNowEpoch;
    struct tm lt;
    localtime_r(&t, &lt);                     // -> local via the TZ env
    gHh = lt.tm_hour; gMm = lt.tm_min;
    gTimeValid = true;
  }
}

static void enterMain(uint32_t now) {
  gState = ST_MAIN;
  gStateStart = now;
  tft.setRotation(settings::cfg.rotation);            // apply any settings change
  // restore the CURRENT auto level, not fixed duty — else every modal exit
  // flashes full-bright in a dark room before the slew catches up
  setBacklight((hasLux && settings::cfg.autoBrightness) ? gBrightness
                                                        : settings::cfg.brightness);
  enterView();
  renderView();
}

// Debounced button -> tap / double-tap / hold. A single tap is held back
// until the double-tap window passes (so it can become a double).
static BtnEv pollButton() {
  static bool     pStable = false, pRaw = false;
  static uint32_t lastChange = 0, pressStart = 0, lastRelease = 0;
  static bool     longFired = false, xFired = false;
  static uint8_t  taps = 0;
  uint32_t now = millis();

  bool pressed = (digitalRead(RECAL_BUTTON_PIN) == LOW);
  if (pressed != pRaw) { pRaw = pressed; lastChange = now; }

  if (now - lastChange >= BTN_DEBOUNCE_MS && pressed != pStable) {
    pStable = pressed;
    if (pStable) { pressStart = now; longFired = false; xFired = false; }
    else if (!longFired) {                 // short release = a tap
      taps++;
      lastRelease = now;
      if (taps >= 2) { taps = 0; return BTN_DBL; }
    }
  }
  if (pStable && !longFired && (now - pressStart) >= BTN_HOLD_MS) {
    longFired = true; taps = 0;
    return BTN_HOLD;
  }
  if (pStable && longFired && !xFired && (now - pressStart) >= BTN_XHOLD_MS) {
    xFired = true;                         // ultra-hold: ride through the 3s hold
    return BTN_XHOLD;
  }
  if (taps == 1 && !pStable && (now - lastRelease) >= BTN_DBL_MS) {
    taps = 0;
    return BTN_TAP;
  }
  return BTN_NONE;
}

static void updateLuxAndBrightness() {
  static uint32_t last = 0;
  static float    ema  = -1;
  static float    level = -1;           // current perceptual level (0..255), slewed
  if (!hasLux) return;
  if (gState == ST_WIFI) return;        // keep the QR screen at full brightness
  if (millis() - last < 400) return;
  last = millis();

  float lux = veml.readLux();
  if (lux >= 0) {
    ema = (ema < 0) ? lux : (ema * 0.7f + lux * 0.3f);
    gLux = ema;
  }
  if (!settings::cfg.autoBrightness) return;

  // Lux -> 0..1 position on a LOG axis (lux spans decades, so equal ratios feel
  // equal). Below luxLow sits at the floor; above luxHigh at the ceiling.
  float lo = max((float)settings::cfg.luxLow, 1.0f);
  float hi = max((float)settings::cfg.luxHigh, lo + 1.0f);
  float l  = (gLux < 0.01f) ? 0.01f : gLux;
  float p  = (logf(l) - logf(lo)) / (logf(hi) - logf(lo));
  if (p < 0) p = 0; else if (p > 1) p = 1;

  // Perceptual target between the min/max endpoints; setBacklight() applies gamma.
  float target = settings::cfg.brightnessMin +
                 p * (settings::cfg.brightnessMax - settings::cfg.brightnessMin);

  // Slew in PERCEPTUAL space so fades feel even from dim to bright.
  if (level < 0) level = target;
  float step = 6.0f;
  if      (level < target) level += fminf(step, target - level);
  else if (level > target) level -= fminf(step, level - target);

  setBacklight((int)(level + 0.5f));
  gBrightness = (int)(level + 0.5f);    // perceptual level, for the diag readout
}

// ---- lifecycle ------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.printf("\noffice-co2-monitor  v%s\n", FIRMWARE_VERSION);
  Serial.println(F("Phase 21.1: second-audit fixes (auth 404, sync restart, NaN, stale views)\n"));

  settings::begin();
  setenv("TZ", settings::cfg.timezone, 1);   // local-time conversion for display
  tzset();
  pinMode(RECAL_BUTTON_PIN, INPUT_PULLUP);

  ledcAttach(TFT_LITE_PIN, BL_PWM_FREQ, BL_PWM_BITS);
  setBacklight(settings::cfg.brightness);
  tft.begin();
  tft.setRotation(settings::cfg.rotation);
  drawSplash();
  delay(1300);
  mainScreenEnter();

  enableI2CPower();
  Wire.begin();
  scanBus();

  Wire.beginTransmission(I2C_ADDR_VEML7700);
  if (Wire.endTransmission() == 0 && veml.begin(&Wire)) {
    hasLux = true;
    veml.setGain(VEML7700_GAIN_1_8);
    veml.setIntegrationTime(VEML7700_IT_100MS);
    Serial.println(F("VEML7700: present -> auto-brightness available"));
  } else {
    Serial.println(F("VEML7700: not found -> fixed brightness"));
  }

  Wire.beginTransmission(I2C_ADDR_MAX17048);
  if (Wire.endTransmission() == 0 && maxlipo.begin(&Wire)) {
    hasBatt = true;
    Serial.printf("MAX17048: present -> %.2fV  %.0f%%\n", maxlipo.cellVoltage(), maxlipo.cellPercent());
  } else {
    Serial.println(F("MAX17048: not found -> no battery gauge"));
  }

  if (rtc.begin(&Wire)) {
    hasRtc = true;
    Serial.println(rtc.lostPower() ? F("DS3231: present, time NOT set")
                                   : F("DS3231: present, time set"));
  } else {
    Serial.println(F("DS3231: not found"));
  }

  scd4x.begin(Wire, I2C_ADDR_SCD41);
  logScdError("stop", scd4x.stopPeriodicMeasurement());
  delay(500);

  uint64_t serial = 0;
  if (scd4x.getSerialNumber(serial) == 0)
    Serial.printf("SCD-41 serial: 0x%llX\n", (unsigned long long)serial);
  else
    Serial.println(F("SCD-41 not responding — check the QT chain"));

  logScdError("altitude", scd4x.setSensorAltitude(settings::cfg.altitudeM));
  logScdError("temp offset", scd4x.setTemperatureOffset(settings::cfg.tempOffsetC10 / 10.0f));

  bool asc = settings::ascEnabled();
  logScdError("set ASC", scd4x.setAutomaticSelfCalibrationEnabled(asc ? 1 : 0));
  logScdError("start", scd4x.startPeriodicMeasurement());
  Serial.printf("ASC %s (profile=%u); measuring\n",
                asc ? "ENABLED" : "disabled", settings::cfg.profile);
  Serial.println(F("button: tap=view, double-tap=recalibrate, hold=wifi\n"));

  datalog::begin();
  {
    uint32_t bootT = (hasRtc && !rtc.lostPower()) ? rtc.now().unixtime() : 0;
    char boot[48];
    snprintf(boot, sizeof(boot), "boot v%s (%s)", FIRMWARE_VERSION, resetReasonStr());
    datalog::event(bootT, boot);
  }

  // Home-WiFi (STA) background server, if enabled + creds present.
  if (settings::cfg.staEnabled && settings::cfg.wifiSsid[0]) {
    Serial.println(F("WiFi: trying home network..."));
    if (portal::startSTA()) {
      Serial.printf("WiFi: connected -> http://%s/  (%s)\n", portal::hostUrl(), portal::staIp());
      if (settings::cfg.webPassword[0] == '\0')
        Serial.println(F("WARNING: web auth disabled — set a Web/OTA password in settings"));
    } else {
      Serial.println(F("WiFi: home network not reachable"));
    }
  }
}

void loop() {
  updateLuxAndBrightness();
  BtnEv ev = pollButton();
  uint32_t now = millis();

  // 10s ultra-hold: reboot from any screen — the enclosure has no power switch,
  // so this is the no-tools way to restart (rides through the 3s WiFi hold).
  if (ev == BTN_XHOLD) {
    Serial.println(F("button: 10s hold -> restart"));
    tft.fillScreen(GC9A01A_BLACK);
    drawTextC(&FreeSansBold12pt7b, 120, 120, GC9A01A_WHITE, "restarting");
    delay(600);
    ESP.restart();
  }

  // Keep the web server (AP or home-WiFi STA) responsive, and adopt NTP time.
  if (portal::apActive() || portal::staActive()) {
    portal::handle();
    uint32_t epoch;
    if (portal::consumeSynced(epoch)) {
      rtc.adjust(DateTime(epoch));
      setenv("TZ", settings::cfg.timezone, 1); tzset();
      Serial.printf("NTP: RTC set, UTC epoch %u\n", (unsigned)epoch);
    }
  }

  static uint32_t lastTick = 0;
  bool tick = (now - lastTick >= 1000);
  if (tick) {
    lastTick = now;
    bool ready = false;
    if (scd4x.getDataReadyStatus(ready) == 0 && ready) {
      uint16_t co2 = 0; float t = 0, h = 0;
      if (scd4x.readMeasurement(co2, t, h) == 0 && co2 != 0) {
        gCo2 = co2; gTempC = t; gHum = h;
        gLastReadMs = now;
        if (gState == ST_EQUIL) eqPush(co2);   // feed the FRC stability gate
        if (hasLux)
          Serial.printf("CO2=%u ppm  T=%.1fC  RH=%.0f%%  lux=%.0f\n", co2, t, h, gLux);
        else
          Serial.printf("CO2=%u ppm  T=%.1fC  RH=%.0f%%\n", co2, t, h);
      }
    }
    refreshTime();

    // Mark the reading stale if the SCD-41 has gone quiet (keep the last value
    // on screen but greyed, so a frozen number can't read as live).
    gStale = (gCo2 != 0) && (now - gLastReadMs > (uint32_t)SENSOR_STALE_SEC * 1000UL);
    gSensorFault = (gCo2 == 0) && (now > (uint32_t)SENSOR_FAULT_BOOT_SEC * 1000UL);

    if (hasBatt) {
      // Library returns NAN if the gauge stops ACKing (loose QT / brownout):
      // keep the last good values rather than propagating NaN to the UI.
      float v = maxlipo.cellVoltage(), p = maxlipo.cellPercent(), r = maxlipo.chargeRate();
      if (!isnan(v) && !isnan(p)) {
        gBattV    = v;
        gBattPct  = (p > 100.0f) ? 100.0f : (p < 0.0f ? 0.0f : p);  // full cell reads 101-104%
        gBattRate = isnan(r) ? 0.0f : r;
      }
    }

    // Push live state to the portal for the /diag page.
    portal::Telemetry tel;
    tel.co2 = gCo2; tel.tempC = gTempC; tel.hum = gHum;
    tel.scdStale = gStale; tel.scdAgeSec = gLastReadMs ? (now - gLastReadMs) / 1000UL : 0;
    tel.hasRtc = hasRtc; tel.hasLux = hasLux;
    tel.timeValid = gTimeValid; tel.nowEpoch = gNowEpoch;
    tel.lux = gLux; tel.brightness = gBrightness;
    tel.frcValid = gFrcOk; tel.frcCorrPpm = gFrcOk ? (int)gFrcCorr - 0x8000 : 0;
    tel.hasBatt = hasBatt; tel.battPct = gBattPct; tel.battV = gBattV; tel.battRate = gBattRate;
    tel.resetReason = resetReasonStr();
    portal::setTelemetry(tel);

    // CO2 trend over a ~2 min window, for the arrow glyph.
    static uint32_t trendT = 0;
    static uint16_t trendRef = 0;
    if (gCo2 != 0) {
      if (trendRef == 0) { trendRef = gCo2; trendT = now; }
      else if (now - trendT >= 120000) {
        gTrend = (gCo2 > trendRef + 20) ? 1 : (gCo2 < trendRef - 20 ? -1 : 0);
        trendRef = gCo2;
        trendT = now;
      }
    }

    // Periodic data log — needs a fresh reading and a real (RTC) clock.
    static uint32_t lastLog = 0;
    static bool     loggedFirst = false;
    if (gCo2 != 0 && gTimeValid) {
      uint32_t ivMs = (uint32_t)settings::cfg.logIntervalSec * 1000;
      if (!loggedFirst || now - lastLog >= ivMs) {
        loggedFirst = true;
        lastLog = now;
        datalog::append(gNowEpoch, gCo2, gTempC, gHum);
      }
    }
  }

  switch (gState) {
    case ST_MAIN:
      if (ev == BTN_TAP) {
        gView = (View)((gView + 1) % VIEW_COUNT);
        Serial.printf("view -> %d\n", (int)gView);
        enterView();
        renderView();
      } else if (ev == BTN_DBL) {
        Serial.println(F("button: double-tap -> confirm recalibrate"));
        gState = ST_CONFIRM; gStateStart = now; drawConfirm();
      } else if (ev == BTN_HOLD) {
        gState = ST_WIFI; gStateStart = now;
        if (portal::staActive()) {            // already on home WiFi -> show LAN URL
          Serial.println(F("button: hold -> wifi info (home)"));
          drawWifiSTA();
        } else {                              // off-network -> captive AP + QR
          Serial.println(F("button: hold -> wifi config AP"));
          setBacklight(255);                  // full bright so the QR scans well
          portal::startAP();
          Serial.printf("AP up: %s  http://%s/\n", portal::apSsid(), portal::apIp());
          drawWifiAP();
        }
      } else if (tick) {
        renderView();
      }
      break;

    case ST_CONFIRM:
      if (ev == BTN_TAP) {
        Serial.printf("recal: equilibrating %ds\n", FRC_EQUILIBRATE_SEC);
        eqReset();
        gState = ST_EQUIL; gStateStart = now; drawEquilEnter();
      } else if (ev == BTN_HOLD || now - gStateStart >= (uint32_t)RECAL_CONFIRM_SEC * 1000) {
        Serial.println(F("recal: confirm canceled"));
        enterMain(now);
      }
      break;

    case ST_EQUIL: {
      if (ev == BTN_HOLD) {
        Serial.println(F("recal: equilibration canceled"));
        enterMain(now);
        break;
      }
      int elapsed = (int)((now - gStateStart) / 1000);
      int remain  = FRC_EQUILIBRATE_SEC - elapsed;
      if (remain <= 0) {
        commitFRC();
        gState = ST_RESULT; gStateStart = now; drawResult();
      } else if (tick) {
        drawEquilTick(remain);
      }
      break;
    }

    case ST_RESULT:
      if (ev == BTN_TAP || now - gStateStart >= (uint32_t)RECAL_RESULT_SEC * 1000)
        enterMain(now);
      break;

    case ST_WIFI: {
      if (portal::apActive()) {                      // AP: refresh the status line
        static char lastStatus[40] = "";
        if (strcmp(lastStatus, portal::statusLine()) != 0) {
          strlcpy(lastStatus, portal::statusLine(), sizeof(lastStatus));
          drawWifiStatus();
        }
      }
      if (ev == BTN_TAP) {
        if (portal::apActive()) portal::stopAP();     // STA, if up, keeps serving
        enterMain(now);
      }
      break;
    }
  }

  delay(5);
}
