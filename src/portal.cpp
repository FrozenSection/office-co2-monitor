#include "portal.h"
#include "config.h"
#include "settings.h"

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <time.h>

namespace {

WebServer     server(80);
DNSServer     dns;
bool          gActive = false;
portal::Phase gPhase  = portal::P_AP;
char          gApSsid[32];
char          gApIp[20]  = "192.168.4.1";
char          gStatus[40] = "join AP";
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

// --- tiny HTML builders ---
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
    "<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>office-co2 settings</title>"
    "<style>body{font-family:system-ui;margin:20px;max-width:440px}h1{font-size:20px}"
    "h2{font-size:13px;letter-spacing:.4px;color:#159b90;margin:22px 0 4px;"
    "border-bottom:1px solid #eee;padding-bottom:4px}"
    "label{display:block;margin:10px 0 3px;font-size:13px;color:#444}"
    "input,select{width:100%;padding:9px;font-size:16px;box-sizing:border-box}"
    "input[type=checkbox]{width:auto;margin-right:8px}"
    ".btns{display:flex;gap:10px;margin:22px 0 6px}"
    "button{flex:1;padding:12px;font-size:15px;border:0;border-radius:6px;color:#fff}"
    ".save{background:#444}.sync{background:#1d9e75}.s{font-size:12px;color:#777}</style>"
    "<h1>office-co2 settings</h1><form method=POST>";

  h += "<h2>DISPLAY</h2>";
  h += lbl("Brightness 0-255 (used when auto is off)") + num("bri", c.brightness);
  h += "<label><input type=checkbox name=autob";
  h += c.autoBrightness ? " checked>" : ">";
  h += "auto-brightness (needs lux sensor)</label>";
  h += lbl("Auto brightness min / max") + num("brmin", c.brightnessMin) + num("brmax", c.brightnessMax);
  h += lbl("Lux low / high (map to min / max)") + num("luxlo", c.luxLow) + num("luxhi", c.luxHigh);
  h += lbl("Temperature unit") + "<select name=unit>"
       + opt("1", unitCur.c_str(), "Fahrenheit") + opt("0", unitCur.c_str(), "Celsius") + "</select>";
  h += lbl("Rotation") + "<select name=rot>";
  for (int r = 0; r < 4; r++) { char rv[2] = {char('0' + r), 0}; h += opt(rv, rotCur.c_str(), rv); }
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

  h += "<h2>WI-FI &amp; TIME</h2>";
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
       "<div class=s>Save stores all settings (profile applies after restart). "
       "Save &amp; sync also joins WiFi and sets the clock from NTP.</div></form>";
  return h;
}

void sendResult(const char* title, const char* detail) {
  String h = "<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'>"
             "<style>body{font-family:system-ui;margin:24px;max-width:440px}"
             "a{display:inline-block;margin-top:18px}</style><h1>";
  h += title; h += "</h1><p>"; h += detail; h += "</p><a href='/'>&larr; back</a>";
  server.send(200, "text/html", h);
}

void handleRoot() { server.send(200, "text/html", pageHtml()); }

// Persist every form field into settings (with clamping).
void applyFormToSettings() {
  Settings& c = settings::cfg;
  strlcpy(c.wifiSsid, server.arg("ssid").c_str(), sizeof(c.wifiSsid));
  String pass = server.arg("pass");
  if (pass.length()) strlcpy(c.wifiPass, pass.c_str(), sizeof(c.wifiPass));
  strlcpy(c.timezone, server.arg("tz").c_str(), sizeof(c.timezone));

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

void handleSaveSettings() {
  applyFormToSettings();
  sendResult("Saved", "Settings stored. Display and air-quality changes are live; "
                      "the location profile applies after a restart.");
}

void handleSync() {
  applyFormToSettings();

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

  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  time_t now = 0;
  t0 = millis();
  while ((now = time(nullptr)) < 1700000000 && millis() - t0 < 10000) delay(200);

  if (now < 1700000000) {
    gPhase = portal::P_FAILED;
    strlcpy(gStatus, "ntp failed", sizeof(gStatus));
    sendResult("Connected, but no time", "NTP didn't answer. Try again in a moment.");
    return;
  }

  setenv("TZ", settings::cfg.timezone, 1);
  tzset();
  gSyncedEpoch   = (uint32_t)now;
  gSyncedPending = true;
  gPhase         = portal::P_SYNCED;

  struct tm lt;
  localtime_r(&now, &lt);
  snprintf(gStatus, sizeof(gStatus), "synced %02d:%02d", lt.tm_hour, lt.tm_min);
  char msg[80];
  snprintf(msg, sizeof(msg), "Settings saved and clock set to %02d:%02d local.",
           lt.tm_hour, lt.tm_min);
  sendResult("Time synced", msg);
}

void handleNotFound() {
  server.sendHeader("Location", String("http://") + gApIp + "/", true);
  server.send(302, "text/plain", "");
}

}  // namespace

void portal::start() {
  snprintf(gApSsid, sizeof(gApSsid), "%s-setup", settings::cfg.hostname);
  WiFi.persistent(false);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(gApSsid);
  IPAddress ip = WiFi.softAPIP();
  snprintf(gApIp, sizeof(gApIp), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);

  dns.start(53, "*", ip);
  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSaveSettings);
  server.on("/sync", HTTP_POST, handleSync);
  server.onNotFound(handleNotFound);
  server.begin();

  gActive = true;
  gPhase  = P_AP;
  strlcpy(gStatus, "join AP", sizeof(gStatus));
}

void portal::handle() {
  if (!gActive) return;
  dns.processNextRequest();
  server.handleClient();
}

void portal::stop() {
  if (!gActive) return;
  server.stop();
  dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  gActive = false;
}

bool          portal::active()     { return gActive; }
const char*   portal::apSsid()     { return gApSsid; }
const char*   portal::apIp()       { return gApIp; }
portal::Phase portal::phase()      { return gPhase; }
const char*   portal::statusLine() { return gStatus; }

bool portal::consumeSynced(uint32_t& epochUtc) {
  if (!gSyncedPending) return false;
  gSyncedPending = false;
  epochUtc = gSyncedEpoch;
  return true;
}
