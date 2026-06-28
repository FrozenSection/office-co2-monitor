#include "portal.h"
#include "config.h"
#include "settings.h"
#include "datalog.h"

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <ElegantOTA.h>
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

const char* HISTORY_PAGE =
  "<!doctype html><meta charset=utf-8>"
  "<meta name=viewport content='width=device-width,initial-scale=1'>"
  "<title>stuffy history</title>"
  "<style>body{font-family:system-ui;margin:16px;max-width:760px}h1{font-size:18px}"
  "a{font-size:13px;margin-right:14px}#c{max-width:100%}</style>"
  "<h1>stuffy \xE2\x80\x94 history</h1>"
  "<div><a href='/'>\xE2\x86\x90 settings</a><a href='/data.csv'>download CSV</a>"
  "<span id=n style='color:#777;font-size:12px'></span></div>"
  "<canvas id=c height=240></canvas>"
  "<script src='https://cdn.jsdelivr.net/npm/chart.js'></script>"
  "<script>fetch('/data.json').then(r=>r.json()).then(d=>{"
  "document.getElementById('n').textContent=' '+d.length+' points';"
  "const lab=d.map(x=>new Date(x[0]*1000).toLocaleString([],"
  "{month:'numeric',day:'numeric',hour:'2-digit',minute:'2-digit'}));"
  "new Chart(document.getElementById('c'),{type:'line',data:{labels:lab,datasets:["
  "{label:'CO2 ppm',data:d.map(x=>x[1]),borderColor:'#e2554b',pointRadius:0,borderWidth:2,yAxisID:'y'},"
  "{label:'temp',data:d.map(x=>x[2]),borderColor:'#3E8BF0',pointRadius:0,borderWidth:1,yAxisID:'y2'}]},"
  "options:{animation:false,interaction:{mode:'index',intersect:false},"
  "scales:{x:{ticks:{maxTicksLimit:8}},y:{position:'left'},"
  "y2:{position:'right',grid:{drawOnChartArea:false}}}}});});</script>";

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
  h += lbl("Dimming gamma 0.1, 22=2.2 (higher=dimmer low end)") + num("gam", c.gammaX10);
  h += lbl("Temperature unit") + "<select name=unit>"
       + opt("1", unitCur.c_str(), "Fahrenheit") + opt("0", unitCur.c_str(), "Celsius") + "</select>";
  h += lbl("Temp offset 0.1C, 40=4.0 (after restart)") + num("toff", c.tempOffsetC10);
  h += lbl("Rotation (applies after restart)") + "<select name=rot>";
  for (int r = 0; r < 4; r++) { char rv[2] = {char('0' + r), 0}; h += opt(rv, rotCur.c_str(), ROT_LABELS[r]); }
  h += "</select>";

  h += "<h2>AIR QUALITY (ppm ceilings)</h2>";
  h += lbl("GOOD up to") + num("aqg", c.aqGood);
  h += lbl("FAIR up to") + num("aqf", c.aqFair);
  h += lbl("POOR up to (above this = BAD)") + num("aqp", c.aqPoor);

  h += "<h2>CALIBRATION</h2>";
  h += lbl("Fresh-air reference (ppm)") + num("frc", c.frcReferencePpm);
  h += lbl("Altitude m, sea level (after restart)") + num("alt", c.altitudeM);
  h += lbl("Location profile") + "<select name=profile>"
       + opt("0", profCur.c_str(), "Sealed office (ASC off)")
       + opt("1", profCur.c_str(), "Ventilated (ASC on)") + "</select>";
  h += lbl("Reminder days: aging / stale / overdue")
       + num("cala", c.calAgingDays) + num("cals", c.calStaleDays) + num("calo", c.calOverdueDays);

  h += "<h2>LOGGING</h2>";
  h += lbl("Log interval (seconds)") + num("logiv", c.logIntervalSec);
  h += "<div class=s style='margin-top:6px'><a href='/history'>View history graph &rarr;</a>"
       "&nbsp;&nbsp;<a href='/events'>Event log &rarr;</a></div>";

  h += "<h2>NETWORK</h2>";
  h += lbl("Device name (<name>.local + AP)")
       + "<input name=host autocapitalize=none autocorrect=off spellcheck=false value='"
       + String(c.hostname) + "'>";
  h += "<label><input type=checkbox name=sta";
  h += c.staEnabled ? " checked>" : ">";
  h += "stay on home WiFi (serve on the LAN)</label>";
  h += lbl("Web / OTA password (blank keeps current)")
       + "<input name=webpw type=password autocapitalize=none autocorrect=off "
         "spellcheck=false placeholder='(set to require login)'>";
  h += lbl("Repeat password")
       + "<input name=webpw2 type=password autocapitalize=none autocorrect=off "
         "spellcheck=false placeholder='(repeat to confirm)'>";
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
       "<div class=s>Save stores all settings. Rotation, profile, and home-WiFi apply "
       "after a restart (button below). Save &amp; sync also sets the clock from NTP. "
       "<a href='/update'>Firmware update &rarr;</a></div></form>"
       "<form method=POST action=/restart style='margin-top:16px'>"
       "<button style='width:100%;padding:11px;border:0;border-radius:6px;"
       "background:#7a2e2e;color:#fff;font-size:15px'>Restart device</button></form>";
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
  c.logIntervalSec = constrain(server.arg("logiv").toInt(), 5, 86400);
  String wpw = server.arg("webpw");
  if (wpw.length()) strlcpy(c.webPassword, wpw.c_str(), sizeof(c.webPassword));

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
  c.altitudeM      = constrain(server.arg("alt").toInt(), 0, 9000);
  c.tempOffsetC10  = constrain(server.arg("toff").toInt(), 0, 200);
  c.gammaX10       = constrain(server.arg("gam").toInt(), 10, 30);

  // enforce ordering / sane relationships so labels can't contradict
  if (c.aqFair <= c.aqGood)               c.aqFair = c.aqGood + 1;
  if (c.aqPoor <= c.aqFair)               c.aqPoor = c.aqFair + 1;
  if (c.calStaleDays <= c.calAgingDays)   c.calStaleDays = c.calAgingDays + 1;
  if (c.calOverdueDays <= c.calStaleDays) c.calOverdueDays = c.calStaleDays + 1;
  if (c.brightnessMax < c.brightnessMin)  c.brightnessMax = c.brightnessMin;
  if (c.luxLow < 1)                       c.luxLow = 1;   // log map needs >= 1
  if (c.luxHigh <= c.luxLow)              c.luxHigh = c.luxLow + 1;

  settings::save();
}

// HTTP Basic auth gate. Open when no web password is set.
bool authed() {
  if (settings::cfg.webPassword[0] == '\0') return true;
  if (!server.authenticate("admin", settings::cfg.webPassword)) {
    server.requestAuthentication();
    return false;
  }
  return true;
}

// Reject a password change unless both entries match (typo / lockout guard).
bool passwordMismatch() {
  String wpw = server.arg("webpw");
  if (wpw.length() && wpw != server.arg("webpw2")) {
    sendResult("Passwords don't match",
               "Nothing was saved. Go back and enter the new password the same way in both fields.");
    return true;
  }
  return false;
}

void handleRoot() {
  if (!authed()) return;
  server.send(200, "text/html", pageHtml());
}

void handleSaveSettings() {
  if (!authed()) return;
  if (passwordMismatch()) return;
  applyFormToSettings();
  sendResult("Saved", "Settings stored. Display and air-quality changes are live; "
                      "profile, home-WiFi, altitude, and temp offset apply after a restart.");
}

void handleSync() {
  if (!authed()) return;
  if (passwordMismatch()) return;
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

void handleRestart() {
  if (!authed()) return;
  server.send(200, "text/html",
              "<!doctype html><meta charset=utf-8>"
              "<meta http-equiv=refresh content='7;url=/'>"
              "<body style='font-family:system-ui;margin:24px'>"
              "<h1>Restarting\xE2\x80\xA6</h1><p>This page returns in a few seconds.</p>");
  delay(400);
  ESP.restart();
}

void handleNotFound() {
  if (gApActive) {   // captive redirect to the portal page
    server.sendHeader("Location", String("http://") + gApIp + "/", true);
    server.send(302, "text/plain", "");
  } else {
    server.send(200, "text/html", pageHtml());
  }
}

void handleHistory() {
  if (!authed()) return;
  server.send(200, "text/html", HISTORY_PAGE);
}

void handleDataJson() {
  if (!authed()) return;
  uint32_t total  = datalog::count();
  uint32_t stride = (total > LOG_GRAPH_MAX_POINTS) ? (total / LOG_GRAPH_MAX_POINTS) : 1;
  if (stride == 0) stride = 1;
  bool unitF = settings::cfg.tempUnitF;

  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "");
  server.sendContent("[");
  String buf; buf.reserve(2048);
  uint32_t i = 0; bool first = true;
  datalog::readAll([&](const datalog::Rec& r) {
    if ((i++ % stride) != 0) return;
    float temp = unitF ? (r.tempC10 / 10.0f * 9.0f / 5.0f + 32.0f) : r.tempC10 / 10.0f;
    if (!first) buf += ',';
    first = false;
    buf += '['; buf += r.t; buf += ','; buf += r.co2; buf += ',';
    buf += String(temp, 1); buf += ','; buf += r.rh; buf += ']';
    if (buf.length() > 1800) { server.sendContent(buf); buf = ""; }
  });
  if (buf.length()) server.sendContent(buf);
  server.sendContent("]");
  server.sendContent("");
}

void handleDataCsv() {
  if (!authed()) return;
  bool unitF = settings::cfg.tempUnitF;
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.sendHeader("Content-Disposition", "attachment; filename=stuffy-history.csv");
  server.send(200, "text/csv", "");
  server.sendContent(unitF ? "unix_time,co2_ppm,temp_F,rh\n" : "unix_time,co2_ppm,temp_C,rh\n");
  String buf; buf.reserve(2048);
  datalog::readAll([&](const datalog::Rec& r) {
    float temp = unitF ? (r.tempC10 / 10.0f * 9.0f / 5.0f + 32.0f) : r.tempC10 / 10.0f;
    buf += r.t; buf += ','; buf += r.co2; buf += ',';
    buf += String(temp, 1); buf += ','; buf += r.rh; buf += '\n';
    if (buf.length() > 1800) { server.sendContent(buf); buf = ""; }
  });
  if (buf.length()) server.sendContent(buf);
  server.sendContent("");
}

void handleEvents() {
  if (!authed()) return;
  String log = datalog::events();
  String out = "epoch,event\n";
  out += log.length() ? log : "(no events logged yet)\n";
  server.send(200, "text/plain", out);
}

void setupRoutes() {
  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSaveSettings);
  server.on("/sync", HTTP_POST, handleSync);
  server.on("/restart", HTTP_POST, handleRestart);
  server.on("/history", handleHistory);
  server.on("/data.json", handleDataJson);
  server.on("/data.csv", handleDataCsv);
  server.on("/events", handleEvents);
  server.onNotFound(handleNotFound);
  ElegantOTA.begin(&server);                 // serves /update with a progress UI
  if (settings::cfg.webPassword[0])
    ElegantOTA.setAuth("admin", settings::cfg.webPassword);
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
  if (gApActive || gStaActive) { server.handleClient(); ElegantOTA.loop(); }
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
int           portal::rssi()      { return gStaActive ? WiFi.RSSI() : 0; }
portal::Phase portal::phase()     { return gPhase; }
const char*   portal::statusLine(){ return gStatus; }

bool portal::consumeSynced(uint32_t& epochUtc) {
  if (!gSyncedPending) return false;
  gSyncedPending = false;
  epochUtc = gSyncedEpoch;
  return true;
}
