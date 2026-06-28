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
#include <RTClib.h>
#include <time.h>

#include "config.h"
#include "version.h"
#include "settings.h"
#include "portal.h"
#include "qrcode.h"
#include "datalog.h"

static Adafruit_GC9A01A tft(TFT_CS_PIN, TFT_DC_PIN, TFT_RST_PIN);
static SensirionI2cScd4x scd4x;
static Adafruit_VEML7700 veml;
static RTC_DS3231 rtc;

static bool     hasLux = false;
static bool     hasRtc = false;
static float    gLux   = -1;
static uint16_t gCo2   = 0;
static float    gTempC = 0, gHum = 0;

static bool     gTimeValid = false;     // RTC has a valid (set) time
static int      gHh = 0, gMm = 0;
static uint32_t gNowEpoch = 0;
static int      gTrend = 0;             // CO2 trend: -1 falling, 0 flat, +1 rising

static uint16_t gFrcCorr = 0;           // last FRC correction word
static bool     gFrcOk   = false;

enum CalState { CAL_UNKNOWN, CAL_FRESH, CAL_AGING, CAL_STALE, CAL_OVERDUE };
enum AppState { ST_MAIN, ST_CONFIRM, ST_EQUIL, ST_RESULT, ST_WIFI };
enum BtnEv    { BTN_NONE, BTN_TAP, BTN_DBL, BTN_HOLD };
enum View     { VIEW_CO2, VIEW_TIME, VIEW_DIAG, VIEW_COUNT };

static AppState gState      = ST_MAIN;
static uint32_t gStateStart = 0;
static View     gView       = VIEW_CO2;

// ---- small helpers ---------------------------------------------------------

static inline uint16_t cGrey()   { return tft.color565(130, 130, 130); }
static inline uint16_t cOrange() { return tft.color565(255, 140, 0); }
static inline uint16_t cSec()    { return tft.color565(0xB8, 0xBD, 0xC4); }  // secondary
static inline uint16_t cFaint()  { return tft.color565(0x8A, 0x8F, 0x96); }  // faint label

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
  uint32_t days = (nowEpoch - settings::cfg.lastFrcEpoch) / 86400UL;
  if (days >= settings::cfg.calOverdueDays) return CAL_OVERDUE;
  if (days >= settings::cfg.calStaleDays)   return CAL_STALE;
  if (days >= settings::cfg.calAgingDays)   return CAL_AGING;
  return CAL_FRESH;
}

// Clear a centered box and draw centered text. Boxes are sized per zone to
// their max content and kept inside the ring (r112) so updates never erase it.
static void drawZone(int cy, int boxW, int boxH, uint8_t size,
                     uint16_t color, const char* s) {
  tft.setFont(nullptr);   // built-in font for modal / WiFi screens
  tft.fillRect(120 - boxW / 2, cy - boxH / 2, boxW, boxH, GC9A01A_BLACK);
  if (!s || !*s) return;
  int w = (int)strlen(s) * 6 * size;
  tft.setTextSize(size);
  tft.setTextColor(color);
  tft.setCursor(120 - w / 2, cy - (8 * size) / 2);
  tft.print(s);
}

static void drawRing(uint16_t color) {
  for (int r = 112; r <= 119; r++) tft.drawCircle(120, 120, r, color);
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

// Design 1 "Aperture": tier ring, white number, faint label, colored status +
// trend, secondary temp/RH, cool calibration dot (shown only when aging+).
static void mainScreenEnter() {
  mLastCo2 = 0xFFFF; mLastRing = 0; mLastLabel = nullptr;
  mLastCal = (CalState)255; mLastTrend = 99;
  mLastTime[0] = '\0'; mLastBot[0] = '\0';
  tft.fillScreen(GC9A01A_BLACK);
  zoneC(&FreeSans9pt7b,  120, 136, 120, 18, cFaint(),       "CO2 ppm");
  zoneC(&FreeSans12pt7b, 120, 102, 200, 26, GC9A01A_WHITE,  "warming up");
  zoneC(&FreeSans12pt7b, 120,  48,  90, 24, cFaint(),       "--:--");
}

static void renderMain(uint16_t co2, float tempC, float hum,
                       bool timeValid, int hh, int mm, CalState cal) {
  const char* label;
  uint16_t tier     = co2Color(co2, &label);
  uint16_t numColor = (cal == CAL_OVERDUE) ? cGrey() : GC9A01A_WHITE;  // grey = untrusted

  if (tier != mLastRing) { drawRing(tier); mLastRing = tier; }

  // big white number (ring + status carry the tier color)
  if (co2 != mLastCo2 || cal != mLastCal) {
    char num[8];
    snprintf(num, sizeof(num), "%u", co2);
    zoneC(&FreeSansBold24pt7b, 120, 102, 180, 44, numColor, num);
    mLastCo2 = co2;
  }

  // status word + trend
  if (label != mLastLabel || cal != mLastCal || gTrend != mLastTrend) {
    drawStatusTrend(164, tier, label, gTrend);
    mLastLabel = label;
    mLastTrend = gTrend;
  }

  // time
  char ts[8];
  if (timeValid) snprintf(ts, sizeof(ts), "%02d:%02d", hh, mm);
  else           strcpy(ts, "--:--");
  if (strcmp(ts, mLastTime) != 0) {
    zoneC(&FreeSans12pt7b, 120, 48, 90, 24, timeValid ? cSec() : cFaint(), ts);
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
}

// ---- time view (clock-prominent) -------------------------------------------
static uint16_t tLastCo2;
static char     tLastClock[8];
static char     tLastBot[24];

static void timeViewEnter() {
  tLastCo2 = 0xFFFF; tLastClock[0] = '\0'; tLastBot[0] = '\0';
  tft.fillScreen(GC9A01A_BLACK);
  for (int r = 115; r <= 118; r++)
    tft.drawCircle(120, 120, r, tft.color565(0x5A, 0x4E, 0x1E));   // dimmed ring
  zoneC(&FreeSans9pt7b, 120, 88, 120, 16, cFaint(), "CO2 ppm");
}

static void renderTimeView() {
  // small CO2 (tier dot + number) up top
  if (gCo2 != tLastCo2) {
    const char* label; uint16_t tier = co2Color(gCo2, &label);
    tft.fillRect(40, 56, 160, 22, GC9A01A_BLACK);
    char num[8]; snprintf(num, sizeof(num), "%u", gCo2);
    tft.setFont(&FreeSans12pt7b);
    tft.setTextSize(1);
    int16_t x1, y1; uint16_t w, h;
    tft.getTextBounds(num, 0, 0, &x1, &y1, &w, &h);
    const int dot = 10, gap = 8;
    int x0 = 120 - (dot + gap + (int)w) / 2;
    tft.fillCircle(x0 + dot / 2, 67, dot / 2, tier);
    tft.setTextColor(cSec());
    tft.setCursor(x0 + dot + gap - x1, 67 - h / 2 - y1);
    tft.print(num);
    tLastCo2 = gCo2;
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
static uint32_t dLastDraw;

static void diagViewEnter() {
  dLastDraw = 0;
  tft.fillScreen(GC9A01A_BLACK);
  tft.drawCircle(120, 120, 118, tft.color565(0x24, 0x24, 0x24));
  tft.drawCircle(120, 120, 117, tft.color565(0x24, 0x24, 0x24));
}

static void renderDiagView() {
  uint32_t now = millis();
  if (dLastDraw != 0 && now - dLastDraw < 2000) return;   // refresh ~2s
  dLastDraw = now;
  tft.fillRect(28, 62, 184, 124, GC9A01A_BLACK);

  char line[48];
  const char* label; uint16_t tier = co2Color(gCo2, &label);

  snprintf(line, sizeof(line), "CO2 %u  %s", gCo2, label);
  drawTextC(&FreeSans9pt7b, 120, 72, tier, line);

  float tShow = settings::cfg.tempUnitF ? (gTempC * 9.0f / 5.0f + 32.0f) : gTempC;
  char  u = settings::cfg.tempUnitF ? 'F' : 'C';
  snprintf(line, sizeof(line), "%d%c   %d%% RH", (int)(tShow + 0.5f), u, (int)(gHum + 0.5f));
  drawTextC(&FreeSans9pt7b, 120, 94, cSec(), line);

  if (hasLux) snprintf(line, sizeof(line), "lux %ld", (long)gLux);
  else        strcpy(line, "no lux sensor");
  drawTextC(&FreeSans9pt7b, 120, 116, hasLux ? cSec() : cFaint(), line);

  const char* wf = portal::staActive() ? portal::hostUrl()
                 : (portal::apActive() ? "AP setup" : "wifi off");
  drawTextC(&FreeSans9pt7b, 120, 138, cSec(), wf);

  CalState cal = calState(gTimeValid, gNowEpoch);
  if (cal == CAL_UNKNOWN) strcpy(line, "cal: not set");
  else {
    uint32_t days = (gNowEpoch - settings::cfg.lastFrcEpoch) / 86400UL;
    snprintf(line, sizeof(line), "cal: %ud ago", (unsigned)days);
  }
  drawTextC(&FreeSans9pt7b, 120, 160, cSec(), line);

  uint32_t up = millis() / 1000;
  snprintf(line, sizeof(line), "v%s  up %lu:%02lu", FIRMWARE_VERSION,
           (unsigned long)(up / 3600), (unsigned long)((up % 3600) / 60));
  drawTextC(&FreeSans9pt7b, 120, 182, cFaint(), line);
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
      break;
  }
}

// ---- recalibration-flow screens (modal; full clear is fine) ----------------

static void drawConfirm() {
  tft.fillScreen(GC9A01A_BLACK);
  drawZone(60, 200, 24, 2, GC9A01A_WHITE, "Recalibrate");
  drawZone(110, 200, 24, 2, GC9A01A_GREEN, "tap = yes");
  drawZone(150, 200, 24, 2, cOrange(), "hold = cancel");
}

static void drawEquilTick(int remain) {
  char t[8];
  snprintf(t, sizeof(t), "%d:%02d", remain / 60, remain % 60);
  drawZone(102, 150, 40, 4, GC9A01A_WHITE, t);
  char p[16];
  snprintf(p, sizeof(p), "%u ppm", gCo2);
  drawZone(146, 180, 20, 2, cGrey(), p);
}

static void drawEquilEnter() {
  tft.fillScreen(GC9A01A_BLACK);
  drawZone(42, 180, 20, 2, GC9A01A_WHITE, "fresh air");
  drawZone(192, 220, 16, 1, cOrange(), "hold = cancel");
  drawEquilTick(FRC_EQUILIBRATE_SEC);
}

static void drawResult() {
  tft.fillScreen(GC9A01A_BLACK);
  if (gFrcOk) {
    drawZone(85, 220, 30, 3, GC9A01A_GREEN, "Calibrated");
    char p[20];
    snprintf(p, sizeof(p), "%u ppm", settings::cfg.frcReferencePpm);
    drawZone(130, 200, 24, 2, GC9A01A_WHITE, p);
    char c[20];
    snprintf(c, sizeof(c), "corr %+d", (int)gFrcCorr - 0x8000);
    drawZone(165, 200, 16, 1, cGrey(), c);
  } else {
    drawZone(95, 220, 30, 2, GC9A01A_RED, "FRC failed");
    drawZone(135, 220, 16, 1, cGrey(), "sensor not ready");
  }
}

static void drawWifiStatus() {
  uint16_t c = (portal::phase() == portal::P_SYNCED) ? GC9A01A_GREEN
             : (portal::phase() == portal::P_FAILED) ? GC9A01A_RED
                                                     : cOrange();
  drawZone(26, 150, 16, 1, c, portal::statusLine());
}

static void drawWifiAP() {
  tft.fillScreen(GC9A01A_BLACK);
  drawWifiStatus();                          // top: status / instruction
  char wifi[80];
  snprintf(wifi, sizeof(wifi), "WIFI:T:nopass;S:%s;;", portal::apSsid());
  drawQR(wifi);                              // scan with phone camera to join
  drawZone(214, 150, 16, 1, cGrey(), "tap to exit");
}

static void drawWifiSTA() {
  tft.fillScreen(GC9A01A_BLACK);
  drawZone(58, 220, 20, 2, GC9A01A_GREEN, "home wifi");
  drawZone(92, 230, 14, 1, cGrey(), "browse to:");
  drawZone(114, 230, 16, 1, GC9A01A_CYAN, portal::hostUrl());
  drawZone(138, 230, 16, 1, GC9A01A_CYAN, portal::staIp());
  drawZone(198, 220, 14, 1, cGrey(), "tap to exit");
}

// ---- actions ---------------------------------------------------------------

static void performFRC() {
  Serial.println(F("FRC: stopping measurement"));
  scd4x.stopPeriodicMeasurement();
  delay(500);

  uint16_t corr = 0;
  int16_t err = scd4x.performForcedRecalibration(settings::cfg.frcReferencePpm, corr);
  gFrcCorr = corr;
  gFrcOk = (err == 0 && corr != 0xFFFF);
  if (err) logScdError("FRC", err);
  Serial.printf("FRC: target=%u corr=0x%04X (%+d ppm) -> %s\n",
                settings::cfg.frcReferencePpm, corr,
                (corr == 0xFFFF) ? 0 : (int)corr - 0x8000,
                gFrcOk ? "OK" : "FAILED");

  scd4x.startPeriodicMeasurement();

  if (gFrcOk && gTimeValid) {
    settings::markRecalibrated(gNowEpoch);
    Serial.println(F("FRC: lastFrcEpoch saved"));
  } else if (gFrcOk) {
    Serial.println(F("FRC: ok, but clock unset -> age not tracked yet"));
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
  analogWrite(TFT_LITE_PIN, settings::cfg.brightness); // auto-brightness re-takes over if on
  enterView();
  renderView();
}

// Debounced button -> tap / double-tap / hold. A single tap is held back
// until the double-tap window passes (so it can become a double).
static BtnEv pollButton() {
  static bool     pStable = false, pRaw = false;
  static uint32_t lastChange = 0, pressStart = 0, lastRelease = 0;
  static bool     longFired = false;
  static uint8_t  taps = 0;
  uint32_t now = millis();

  bool pressed = (digitalRead(RECAL_BUTTON_PIN) == LOW);
  if (pressed != pRaw) { pRaw = pressed; lastChange = now; }

  if (now - lastChange >= BTN_DEBOUNCE_MS && pressed != pStable) {
    pStable = pressed;
    if (pStable) { pressStart = now; longFired = false; }
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
  if (taps == 1 && !pStable && (now - lastRelease) >= BTN_DBL_MS) {
    taps = 0;
    return BTN_TAP;
  }
  return BTN_NONE;
}

static void updateLuxAndBrightness() {
  static uint32_t last = 0;
  static float    ema  = -1;
  static int      applied = -1;
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

  float lo = settings::cfg.luxLow, hi = settings::cfg.luxHigh;
  float t = (hi > lo) ? (gLux - lo) / (hi - lo) : 1.0f;
  if (t < 0) t = 0; else if (t > 1) t = 1;
  int target = settings::cfg.brightnessMin +
               (int)(t * (settings::cfg.brightnessMax - settings::cfg.brightnessMin));

  if (applied < 0) applied = target;
  if      (applied < target) applied += min(8, target - applied);
  else if (applied > target) applied -= min(8, applied - target);
  analogWrite(TFT_LITE_PIN, applied);
}

// ---- lifecycle ------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.printf("\noffice-co2-monitor  v%s\n", FIRMWARE_VERSION);
  Serial.println(F("Phase 13: Design 1 (views + gestures)\n"));

  settings::begin();
  setenv("TZ", settings::cfg.timezone, 1);   // local-time conversion for display
  tzset();
  pinMode(RECAL_BUTTON_PIN, INPUT_PULLUP);

  pinMode(TFT_LITE_PIN, OUTPUT);
  analogWrite(TFT_LITE_PIN, settings::cfg.brightness);
  tft.begin();
  tft.setRotation(settings::cfg.rotation);
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

  bool asc = settings::ascEnabled();
  logScdError("set ASC", scd4x.setAutomaticSelfCalibrationEnabled(asc ? 1 : 0));
  logScdError("start", scd4x.startPeriodicMeasurement());
  Serial.printf("ASC %s (profile=%u); measuring\n",
                asc ? "ENABLED" : "disabled", settings::cfg.profile);
  Serial.println(F("button: tap=view, double-tap=recalibrate, hold=wifi\n"));

  datalog::begin();

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
        if (hasLux)
          Serial.printf("CO2=%u ppm  T=%.1fC  RH=%.0f%%  lux=%.0f\n", co2, t, h, gLux);
        else
          Serial.printf("CO2=%u ppm  T=%.1fC  RH=%.0f%%\n", co2, t, h);
      }
    }
    refreshTime();

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
          analogWrite(TFT_LITE_PIN, 255);     // full bright so the QR scans well
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
        performFRC();
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
