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

// POSIX TZ strings for the dropdown (value, label).
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

String pageHtml() {
  String tzSel;
  for (auto& o : TZ_OPTIONS) {
    tzSel += "<option value='";
    tzSel += o[0];
    tzSel += (strcmp(o[0], settings::cfg.timezone) == 0) ? "' selected>" : "'>";
    tzSel += o[1];
    tzSel += "</option>";
  }
  String h =
    "<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>office-co2 setup</title>"
    "<style>body{font-family:system-ui;margin:24px;max-width:420px}"
    "h1{font-size:20px}label{display:block;margin:14px 0 4px;font-size:14px}"
    "input,select,button{width:100%;padding:10px;font-size:16px;box-sizing:border-box}"
    "button{margin-top:18px;background:#1d9e75;color:#fff;border:0;border-radius:6px}"
    ".s{margin-top:14px;font-size:13px;color:#555}</style>"
    "<h1>office-co2 setup</h1><form method=POST action=/save>"
    "<label>WiFi network</label>"
    "<input name=ssid autocapitalize=none autocorrect=off autocomplete=off "
    "spellcheck=false value='";
  h += settings::cfg.wifiSsid;
  h += "'><label>WiFi password</label>"
       "<input name=pass type=password autocapitalize=none autocorrect=off "
       "spellcheck=false placeholder='(leave blank to keep)'>"
       "<label>Time zone</label><select name=tz>";
  h += tzSel;
  h += "</select><button>Save &amp; sync time</button></form>"
       "<div class=s>Saves WiFi + zone, connects, and sets the clock from NTP.</div>";
  return h;
}

void sendResult(const char* title, const char* detail) {
  String h = "<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'>"
             "<style>body{font-family:system-ui;margin:24px;max-width:420px}"
             "a{display:inline-block;margin-top:18px}</style><h1>";
  h += title; h += "</h1><p>"; h += detail; h += "</p><a href='/'>&larr; back</a>";
  server.send(200, "text/html", h);
}

void handleRoot() { server.send(200, "text/html", pageHtml()); }

void handleSave() {
  String ssid = server.arg("ssid");
  String pass = server.arg("pass");
  String tz   = server.arg("tz");

  strlcpy(settings::cfg.wifiSsid, ssid.c_str(), sizeof(settings::cfg.wifiSsid));
  if (pass.length())
    strlcpy(settings::cfg.wifiPass, pass.c_str(), sizeof(settings::cfg.wifiPass));
  strlcpy(settings::cfg.timezone, tz.c_str(), sizeof(settings::cfg.timezone));
  settings::save();

  gPhase = portal::P_CONNECTING;
  strlcpy(gStatus, "connecting...", sizeof(gStatus));

  WiFi.mode(WIFI_AP_STA);   // keep the portal AP up during the STA attempt
  WiFi.begin(settings::cfg.wifiSsid, settings::cfg.wifiPass);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 12000) delay(200);

  if (WiFi.status() != WL_CONNECTED) {
    gPhase = portal::P_FAILED;
    strlcpy(gStatus, "wifi failed", sizeof(gStatus));
    sendResult("Couldn't connect", "Check the network name and password, then try again.");
    return;
  }

  configTime(0, 0, "pool.ntp.org", "time.nist.gov");   // fetch UTC
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
  char msg[72];
  snprintf(msg, sizeof(msg), "Clock set to %02d:%02d local. You can close this.",
           lt.tm_hour, lt.tm_min);
  sendResult("Time synced", msg);
}

void handleNotFound() {   // captive-portal redirect -> config page
  server.sendHeader("Location", String("http://") + gApIp + "/", true);
  server.send(302, "text/plain", "");
}

}  // namespace

void portal::start() {
  snprintf(gApSsid, sizeof(gApSsid), "%s-setup", settings::cfg.hostname);
  WiFi.persistent(false);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(gApSsid);   // open AP, on-demand only
  IPAddress ip = WiFi.softAPIP();
  snprintf(gApIp, sizeof(gApIp), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);

  dns.start(53, "*", ip);
  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
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

bool        portal::active()     { return gActive; }
const char* portal::apSsid()     { return gApSsid; }
const char* portal::apIp()       { return gApIp; }
portal::Phase portal::phase()    { return gPhase; }
const char* portal::statusLine() { return gStatus; }

bool portal::consumeSynced(uint32_t& epochUtc) {
  if (!gSyncedPending) return false;
  gSyncedPending = false;
  epochUtc = gSyncedEpoch;
  return true;
}
