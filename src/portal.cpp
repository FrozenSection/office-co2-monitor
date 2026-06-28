#include "portal.h"
#include "config.h"
#include "settings.h"

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <time.h>

namespace {

WebServer     server(80);
DNSServer     dns;
bool          gApActive  = false;
bool          gStaActive = false;
portal::Phase gPhase  = portal::P_AP;
char          gApSsid[32];
char          gApIp[20]   = "192.168.4.1";
char          gStaIp[20]  = "";
char          gHostUrl[40] = "";
char          gStatus[40]  = "join AP";
uint32_t      gSyncedEpoch  = 0;
bool          gSyncedPending = false;

const char* TZ_OPTIONS[][2] = {
  {"PST8PDT,M3.2.0,M11.1.0",  "Pacific"},
  {"MST7MDT,M3.2.0,M11.1.0",  "Mountain"},
  {"MST7",                    "Arizona"},
  {"CST6CDT,M3.2.0,M11.1.0",  "Central"},
  {"EST5EDT,M3.2.0,M11.1.0",  "Eastern"},
  {"AKST9AKDT,M3.2.0,M11.1.0","Alaska"},
  {"HST10",                   "Hawaii"},
  {"UTC0",                    "UTC"},
};
const char* ROT_LABELS[4] = {"0\xC2\xB0", "90\xC2\xB0", "180\xC2\xB0", "270\xC2\xB0"};

const char* OTA_FORM =
  "<!doctype html><meta charset=utf-8>"
  "<meta name=viewport content='width=device-width,initial-scale=1'>"
  "<title>firmware</title><style>body{font-family:system-ui;margin:24px;max-width:440px}"
  "input,button{width:100%;padding:11px;font-size:16px;box-sizing:border-box;margin-top:10px}"
  "button{background:#1d9e75;color:#fff;border:0;border-radius:6px}</style>"
  "<h1>Firmware update</h1>"
  "<form method=POST action=/update enctype='multipart/form-data'>"
  "<input type=file name=firmware accept='.bin'>"
  "<button>Upload &amp; flash</button></form>"
  "<p style='font-size:12px;color:#777'>Upload a PlatformIO .bin. The device reboots when done.</p>"
  "<a href='/'>&larr; settings</a>";

// --- html builders ---
String lbl(const char* t) { return "<label>" + String(t) + "</label>"; }

String num(const char* name, long v) {
  char b[96];
  snprintf(b, sizeof(b), "<input name=%s type=number value=%ld>", name, v);
  return String(b);
}

String opt(const char* val, const char* cur, const char* label) {
  String s = "<option value='";
  s += val;
  s += (strcmp(val, cur) == 0) ? "' selected>" : "'>";
  s += label;
  s += "</option>";
  return s;
}

String pageHtml() {
  Settings& c = settings::cfg;
  String unitCur = String(c.tempUnitF ? 1 : 0);
  String rotCur  = String(c.rotation);
  String profCur = String(c.profile);

  String h =
    "<!doctype html><meta charset=utf-8>"
    "<meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>stuffy settings</title>"
    "<style>body{font-family:system-ui;margin:20px;max-width:440px}h1{font-size:20px}"
    "h2{font-size:13px;letter-spacing:.4px;color:#159b90;margin:22px 0 4px;"
    "border-bottom:1px solid #eee;padding-bottom:4px}"
    "label{display:block;margin:10px 0 3px;font-size:13px;color:#444}"
    "input,select{width:100%;padding:9px;font-size:16px;box-sizing:border-box}"
    "input[type=checkbox]{width:auto;margin-right:8px}"
    ".btns{display:flex;gap:10px;margin:22px 0 6px}"
    "button{flex:1;padding:12px;font-size:15px;border:0;border-radius:6px;color:#fff}"
    ".save{background:#444}.sync{background:#1d9e75}.s{font-size:12px;color:#777}</style>"
    "<h1>stuffy settings</h1><form method=POST>";

  h += "<h2>DISPLAY</h2>";
  h += lbl("Brightness 0-255 (used when auto is off)") + num("bri", c.brightness);
  h += "<label><input type=checkbox name=autob";
  h += c.autoBrightness ? " checked>" : ">";
  h += "auto-brightness (needs lux sensor)</label>";
  h += lbl("Auto brightness min / max") + num("brmin", c.brightnessMin) + num("brmax", c.brightnessMax);
  h += lbl("Lux low / high (map to min / max)") + num("luxlo", c.luxLow) + num("luxhi", c.luxHigh);
  h += lbl("Temperature unit") + "<select name=unit>"
       + opt("1", unitCur.c_str(), "Fahrenheit") + opt("0", unitCur.c_str(), "Celsius") + "</select>";
  h += lbl("Rotation (screen orientation)") + "<select name=rot>";
  for (int r = 0; r < 4; r++) { char rv[2] = {char('0' + r), 0}; h += opt(rv, rotCur.c_str(), ROT_LABELS[r]); }
  h += "</select>";

  h += "<h2>AIR QUALITY (ppm ceilings)</h2>";
  h += lbl("GOOD up to") + num("aqg", c.aqGood);
  h += lbl("FAIR up to") + num("aqf", c.aqFair);
  h += lbl("POOR up to (above this = BAD)") + num("aqp", c.aqPoor);

  h += "<h2>CALIBRATION</h2>";
  h += lbl("Fresh-air reference (ppm)") + num("frc", c.frcReferencePpm);
  h += lbl("Location profile") + "<select name=profile>"
       + opt("0", profCur.c_str(), "Sealed office (ASC off)")
       + opt("1", profCur.c_str(), "Ventilated (ASC on)") + "</select>";
  h += lbl("Reminder days: aging / stale / overdue")
       + num("cala", c.calAgingDays) + num("cals", c.calStaleDays) + num("calo", c.calOverdueDays);

  h += "<h2>NETWORK</h2>";
  h += lbl("Device name (<name>.local + AP)")
       + "<input name=host autocapitalize=none autocorrect=off spellcheck=false value='"
       + String(c.hostname) + "'>";
  h += "<label><input type=checkbox name=sta";
  h += c.staEnabled ? " checked>" : ">";
  h += "stay on home WiFi (serve on the LAN)</label>";
  h += lbl("WiFi network")
       + "<input name=ssid autocapitalize=none autocorrect=off autocomplete=off "
         "spellcheck=false value='" + String(c.wifiSsid) + "'>";
  h += lbl("WiFi password")
       + "<input name=pass type=password autocapitalize=none autocorrect=off "
         "spellcheck=false placeholder='(leave blank to keep)'>";
  h += lbl("Time zone") + "<select name=tz>";
  for (auto& o : TZ_OPTIONS) h += opt(o[0], c.timezone, o[1]);
  h += "</select>";

  h += "<div class=btns>"
       "<button class=save formaction=/save>Save</button>"
       "<button class=sync formaction=/sync>Save &amp; sync time</button></div>"
       "<div class=s>Save stores all settings (profile + home-WiFi apply after restart). "
       "Save &amp; sync also sets the clock from NTP. "
       "<a href='/update'>Firmware update &rarr;</a></div></form>";
  return h;
}

void sendResult(const char* title, const char* detail) {
  String h = "<!doctype html><meta charset=utf-8>"
             "<meta name=viewport content='width=device-width,initial-scale=1'>"
             "<style>body{font-family:system-ui;margin:24px;max-width:440px}"
             "a{display:inline-block;margin-top:18px}</style><h1>";
  h += title; h += "</h1><p>"; h += detail; h += "</p><a href='/'>&larr; back</a>";
  server.send(200, "text/html", h);
}

bool doNtp() {
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  time_t now = 0;
  uint32_t t0 = millis();
  while ((now = time(nullptr)) < 1700000000 && millis() - t0 < 8000) delay(150);
  if (now < 1700000000) return false;
  setenv("TZ", settings::cfg.timezone, 1);
  tzset();
  gSyncedEpoch   = (uint32_t)now;
  gSyncedPending = true;
  struct tm lt;
  localtime_r(&now, &lt);
  snprintf(gStatus, sizeof(gStatus), "synced %02d:%02d", lt.tm_hour, lt.tm_min);
  return true;
}

void applyFormToSettings() {
  Settings& c = settings::cfg;
  strlcpy(c.wifiSsid, server.arg("ssid").c_str(), sizeof(c.wifiSsid));
  String pass = server.arg("pass");
  if (pass.length()) strlcpy(c.wifiPass, pass.c_str(), sizeof(c.wifiPass));
  strlcpy(c.timezone, server.arg("tz").c_str(), sizeof(c.timezone));
  if (server.arg("host").length())
    strlcpy(c.hostname, server.arg("host").c_str(), sizeof(c.hostname));
  c.staEnabled = server.hasArg("sta");

  c.brightness     = constrain(server.arg("bri").toInt(), 0, 255);
  c.autoBrightness = server.hasArg("autob");
  c.brightnessMin  = constrain(server.arg("brmin").toInt(), 0, 255);
  c.brightnessMax  = constrain(server.arg("brmax").toInt(), 0, 255);
  c.luxLow         = constrain(server.arg("luxlo").toInt(), 0, 65000);
  c.luxHigh        = constrain(server.arg("luxhi").toInt(), 1, 65000);
  c.tempUnitF      = server.arg("unit").toInt() != 0;
  c.rotation       = constrain(server.arg("rot").toInt(), 0, 3);
  c.aqGood         = constrain(server.arg("aqg").toInt(), 400, 5000);
  c.aqFair         = constrain(server.arg("aqf").toInt(), 400, 5000);
  c.aqPoor         = constrain(server.arg("aqp").toInt(), 400, 5000);
  c.frcReferencePpm= constrain(server.arg("frc").toInt(), 300, 2000);
  c.profile        = server.arg("profile").toInt() ? 1 : 0;
  c.calAgingDays   = constrain(server.arg("cala").toInt(), 1, 3650);
  c.calStaleDays   = constrain(server.arg("cals").toInt(), 1, 3650);
  c.calOverdueDays = constrain(server.arg("calo").toInt(), 1, 3650);
  settings::save();
}

void handleRoot() { server.send(200, "text/html", pageHtml()); }

void handleSaveSettings() {
  applyFormToSettings();
  sendResult("Saved", "Settings stored. Display and air-quality changes are live; "
                      "location profile and home-WiFi apply after a restart.");
}

void handleSync() {
  applyFormToSettings();

  // Already on home WiFi -> just NTP.
  if (gStaActive && WiFi.status() == WL_CONNECTED) {
    if (doNtp()) sendResult("Time synced", "Clock set from NTP.");
    else         sendResult("No time", "NTP didn't answer. Try again shortly.");
    return;
  }

  // AP path: connect (keep AP up) then NTP.
  gPhase = portal::P_CONNECTING;
  strlcpy(gStatus, "connecting...", sizeof(gStatus));
  WiFi.mode(WIFI_AP_STA);
  WiFi.begin(settings::cfg.wifiSsid, settings::cfg.wifiPass);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 12000) delay(200);
  if (WiFi.status() != WL_CONNECTED) {
    gPhase = portal::P_FAILED;
    strlcpy(gStatus, "wifi failed", sizeof(gStatus));
    sendResult("Couldn't connect", "Check the network name and password, then try again.");
    return;
  }
  if (doNtp()) {
    gPhase = portal::P_SYNCED;
    sendResult("Time synced", "Settings saved and the clock is set from NTP.");
  } else {
    gPhase = portal::P_FAILED;
    strlcpy(gStatus, "ntp failed", sizeof(gStatus));
    sendResult("Connected, but no time", "NTP didn't answer. Try again in a moment.");
  }
}

void handleNotFound() {
  if (gApActive) {   // captive redirect to the portal page
    server.sendHeader("Location", String("http://") + gApIp + "/", true);
    server.send(302, "text/plain", "");
  } else {
    server.send(200, "text/html", pageHtml());
  }
}

void otaDone() {
  bool ok = !Update.hasError();
  server.send(200, "text/html",
              ok ? "<h1>Updated</h1><p>Rebooting…</p>" : "<h1>Update failed</h1><a href='/update'>retry</a>");
  delay(800);
  if (ok) ESP.restart();
}

void otaUpload() {
  HTTPUpload& up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    Serial.printf("OTA: receiving %s\n", up.filename.c_str());
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) Update.printError(Serial);
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (Update.write(up.buf, up.currentSize) != up.currentSize) Update.printError(Serial);
  } else if (up.status == UPLOAD_FILE_END) {
    if (Update.end(true)) Serial.printf("OTA: ok, %u bytes\n", (unsigned)up.totalSize);
    else                  Update.printError(Serial);
  }
}

void setupRoutes() {
  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSaveSettings);
  server.on("/sync", HTTP_POST, handleSync);
  server.on("/update", HTTP_GET, []() { server.send(200, "text/html", OTA_FORM); });
  server.on("/update", HTTP_POST, otaDone, otaUpload);
  server.onNotFound(handleNotFound);
  server.begin();
}

}  // namespace

void portal::startAP() {
  snprintf(gApSsid, sizeof(gApSsid), "%s-setup", settings::cfg.hostname);
  WiFi.persistent(false);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(gApSsid);
  IPAddress ip = WiFi.softAPIP();
  snprintf(gApIp, sizeof(gApIp), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
  dns.start(53, "*", ip);
  setupRoutes();
  gApActive = true;
  gPhase    = P_AP;
  strlcpy(gStatus, "join AP", sizeof(gStatus));
}

bool portal::startSTA() {
  if (!settings::cfg.staEnabled || settings::cfg.wifiSsid[0] == '\0') return false;
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(settings::cfg.hostname);
  WiFi.begin(settings::cfg.wifiSsid, settings::cfg.wifiPass);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 9000) delay(150);
  if (WiFi.status() != WL_CONNECTED) { WiFi.mode(WIFI_OFF); return false; }

  IPAddress ip = WiFi.localIP();
  snprintf(gStaIp, sizeof(gStaIp), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
  snprintf(gHostUrl, sizeof(gHostUrl), "%s.local", settings::cfg.hostname);
  if (MDNS.begin(settings::cfg.hostname)) MDNS.addService("http", "tcp", 80);
  setupRoutes();
  gStaActive = true;
  doNtp();   // best-effort; RTC already holds time if battery-backed
  return true;
}

void portal::handle() {
  if (gApActive) dns.processNextRequest();
  if (gApActive || gStaActive) server.handleClient();
}

void portal::stopAP() {
  if (!gApActive) return;
  server.stop();
  dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  gApActive = false;
}

bool          portal::apActive()  { return gApActive; }
bool          portal::staActive() { return gStaActive; }
const char*   portal::apSsid()    { return gApSsid; }
const char*   portal::apIp()      { return gApIp; }
const char*   portal::hostUrl()   { return gHostUrl; }
const char*   portal::staIp()     { return gStaIp; }
portal::Phase portal::phase()     { return gPhase; }
const char*   portal::statusLine(){ return gStatus; }

bool portal::consumeSynced(uint32_t& epochUtc) {
  if (!gSyncedPending) return false;
  gSyncedPending = false;
  epochUtc = gSyncedEpoch;
  return true;
}
