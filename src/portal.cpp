#include "portal.h"
#include "config.h"
#include "settings.h"
#include "datalog.h"
#include "version.h"

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
portal::Telemetry gTelem = {};

String upStr() {
  uint32_t s = millis() / 1000;
  char b[16];
  if (s < 86400) snprintf(b, sizeof(b), "%lu:%02lu", (unsigned long)(s / 3600), (unsigned long)((s % 3600) / 60));
  else           snprintf(b, sizeof(b), "%lud %luh", (unsigned long)(s / 86400), (unsigned long)((s % 86400) / 3600));
  return String(b);
}

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

// Shared CSS for all served pages, so settings / history / diagnostics / result
// pages look like one app. Built as a macro so the static const pages can splice
// it at compile time.
#define UI_CSS \
  "<style>" \
  "body{font-family:system-ui,-apple-system,sans-serif;margin:0;background:#f4f4f3;color:#222}" \
  ".wrap{max-width:480px;margin:0 auto;padding:18px 16px 28px}" \
  "h1{font-size:18px;font-weight:500;margin:2px 0 12px}" \
  ".nav{display:flex;flex-wrap:wrap;gap:14px;font-size:13px;margin-bottom:14px}" \
  ".nav a{color:#159b90;text-decoration:none}" \
  ".card{background:#fff;border:1px solid #e4e4e0;border-radius:12px;padding:14px 16px;margin-bottom:12px}" \
  ".sec{font-size:13px;color:#159b90;font-weight:500;margin:0 0 12px}" \
  ".f{margin-bottom:14px}.f:last-child{margin-bottom:0}" \
  "label{display:block;font-size:13px;color:#3c3c3a;margin-bottom:5px}" \
  ".hint{font-size:12px;color:#9a9a95;margin-top:4px}" \
  "input,select{width:100%;box-sizing:border-box;padding:9px 11px;border:1px solid #d6d6d2;border-radius:8px;font-size:15px;color:#222;background:#fff}" \
  "input:focus,select:focus{outline:none;border-color:#159b90}" \
  ".row{display:flex;gap:10px}.row>div{flex:1}" \
  ".cap{display:block;font-size:12px;color:#888;margin-bottom:4px}" \
  ".chk{display:flex;align-items:center;gap:9px;font-size:13px;color:#3c3c3a;margin-bottom:0}" \
  ".chk input{width:17px;height:17px;margin:0;flex:none}" \
  "button{padding:12px;font-size:14px;border:0;border-radius:8px;color:#fff;cursor:pointer}" \
  ".btns{display:flex;gap:10px;margin-top:4px}.btns button{flex:1}" \
  ".save{background:#3a3a38}.sync{background:#1d9e75}" \
  ".s{font-size:12px;color:#888;line-height:1.55}.s a{color:#159b90}" \
  "a{color:#159b90}" \
  ".stat{display:flex;justify-content:space-between;align-items:center;padding:7px 0;border-bottom:1px solid #f0f0ec;font-size:13px}" \
  ".stat:last-child{border-bottom:0}.k{color:#777}.v{color:#222;font-variant-numeric:tabular-nums}" \
  ".d{width:8px;height:8px;border-radius:50%;display:inline-block;margin-right:5px;vertical-align:1px;background:#bbb}" \
  ".grid{display:grid;grid-template-columns:1fr 1fr;gap:12px}" \
  "@media(max-width:520px){.grid{grid-template-columns:1fr}}" \
  ".cardh{font-size:12px;color:#159b90;font-weight:500;margin:0 0 8px}" \
  ".copy{background:#fff;border:1px solid #d6d6d2;color:#444;width:100%}" \
  ".danger{background:#fff;border:1px solid #d98a8a;color:#b03030;width:100%}" \
  ".range{display:flex;gap:8px;align-items:center;margin-bottom:12px}" \
  ".range button{padding:6px 12px;font-size:13px;background:#fff;border:1px solid #d6d6d2;color:#555}" \
  ".range button.on{background:#159b90;color:#fff;border-color:#159b90}" \
  "textarea{width:100%;box-sizing:border-box;border:1px solid #d6d6d2;border-radius:8px}" \
  "</style>"

const char* HISTORY_PAGE =
  "<!doctype html><meta charset=utf-8>"
  "<meta name=viewport content='width=device-width,initial-scale=1'>"
  "<title>stuffy history</title>"
  UI_CSS
  "<div class=wrap><h1>History</h1>"
  "<div class=nav><a href='/'>\xE2\x86\x90 Settings</a><a href='/diag'>Diagnostics</a>"
  "<a href='/events'>Event log</a><a href='/data.csv'>Download CSV</a></div>"
  "<div class=range><button onclick='load(24,this)'>24 h</button>"
  "<button onclick='load(168,this)'>7 d</button>"
  "<button onclick='load(0,this)'>All</button>"
  "<span id=n class=s></span></div>"
  "<div class=card><canvas id=c height=240></canvas></div></div>"
  "<script src='https://cdn.jsdelivr.net/npm/chart.js'></script>"
  "<script>let ch;"
  "function load(h,btn){"
  "document.querySelectorAll('.range button').forEach(b=>b.className='');"
  "if(btn)btn.className='on';"
  "fetch('/data.json?h='+h).then(r=>r.json()).then(d=>{"
  "document.getElementById('n').textContent=d.length+' points';"
  "const lab=d.map(x=>new Date(x[0]*1000).toLocaleString([],"
  "{month:'numeric',day:'numeric',hour:'2-digit',minute:'2-digit'}));"
  "if(ch)ch.destroy();"
  "ch=new Chart(document.getElementById('c'),{type:'line',data:{labels:lab,datasets:["
  "{label:'CO2 ppm',data:d.map(x=>x[1]),borderColor:'#e2554b',pointRadius:0,borderWidth:2,yAxisID:'y'},"
  "{label:'temp',data:d.map(x=>x[2]),borderColor:'#3E8BF0',pointRadius:0,borderWidth:1,yAxisID:'y2'}]},"
  "options:{animation:false,interaction:{mode:'index',intersect:false},"
  "plugins:{legend:{labels:{boxWidth:12,font:{size:11}}}},"
  "scales:{x:{ticks:{maxTicksLimit:8,font:{size:10}}},"
  "y:{position:'left',grace:'8%',title:{display:true,text:'ppm'}},"
  "y2:{position:'right',grace:'8%',grid:{drawOnChartArea:false}}}}});});}"
  "load(24,document.querySelectorAll('.range button')[0]);</script>";

// --- html builders ---
String card(const char* sec, const String& body) {
  return "<div class=card><div class=sec>" + String(sec) + "</div>" + body + "</div>";
}

String fieldH(const char* label, const String& input, const char* hint = "") {
  String s = "<div class=f><label>"; s += label; s += "</label>"; s += input;
  if (hint && *hint) { s += "<div class=hint>"; s += hint; s += "</div>"; }
  return s + "</div>";
}

String check(const char* name, bool on, const char* text, const char* hint = "") {
  String s = "<div class=f><label class=chk><input type=checkbox name=";
  s += name; s += on ? " checked> " : "> "; s += text; s += "</label>";
  if (hint && *hint) { s += "<div class=hint>"; s += hint; s += "</div>"; }
  return s + "</div>";
}

// Paired/triple inputs each get their own caption, so they can't be confused.
String pair2(const char* label, const char* hint,
             const char* ca, const String& ia, const char* cb, const String& ib) {
  String s = "<div class=f><label>"; s += label; s += "</label><div class=row>";
  s += "<div><span class=cap>"; s += ca; s += "</span>"; s += ia; s += "</div>";
  s += "<div><span class=cap>"; s += cb; s += "</span>"; s += ib; s += "</div></div>";
  if (hint && *hint) { s += "<div class=hint>"; s += hint; s += "</div>"; }
  return s + "</div>";
}

String pair3(const char* label, const char* hint,
             const char* ca, const String& ia, const char* cb, const String& ib,
             const char* cc, const String& ic) {
  String s = "<div class=f><label>"; s += label; s += "</label><div class=row>";
  s += "<div><span class=cap>"; s += ca; s += "</span>"; s += ia; s += "</div>";
  s += "<div><span class=cap>"; s += cb; s += "</span>"; s += ib; s += "</div>";
  s += "<div><span class=cap>"; s += cc; s += "</span>"; s += ic; s += "</div></div>";
  if (hint && *hint) { s += "<div class=hint>"; s += hint; s += "</div>"; }
  return s + "</div>";
}

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

// Escape a value for a single-quoted HTML attribute (hostname/SSID can be anything).
String htmlAttrEsc(const String& v) {
  String o; o.reserve(v.length() + 8);
  for (size_t i = 0; i < v.length(); i++) {
    char c = v[i];
    if      (c == '&')  o += "&amp;";
    else if (c == '<')  o += "&lt;";
    else if (c == '>')  o += "&gt;";
    else if (c == '\'') o += "&#39;";
    else if (c == '"')  o += "&quot;";
    else                o += c;
  }
  return o;
}

// Escape a value for a JSON string (SSID/hostname could contain " or \).
String jsonEsc(const String& v) {
  String o; o.reserve(v.length() + 8);
  for (size_t i = 0; i < v.length(); i++) {
    char c = v[i];
    if      (c == '"' || c == '\\')   { o += '\\'; o += c; }
    else if ((unsigned char)c < 0x20) { char b[8]; snprintf(b, sizeof(b), "\\u%04x", c); o += b; }
    else                                o += c;
  }
  return o;
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
    UI_CSS
    "<div class=wrap><h1>stuffy settings</h1>"
    "<div class=nav><a href='/diag'>Diagnostics</a><a href='/history'>History</a>"
    "<a href='/events'>Event log</a><a href='/update'>Firmware</a></div>"
    "<form method=POST>";

  // DISPLAY
  String disp;
  disp += fieldH("Fixed brightness", num("bri", c.brightness),
                 "0\xE2\x80\x93""255, used when auto-brightness is off");
  disp += check("autob", c.autoBrightness, "Auto-brightness (needs lux sensor)");
  disp += pair2("Auto brightness range", "",
                "min (night)", num("brmin", c.brightnessMin),
                "max (day)",   num("brmax", c.brightnessMax));
  disp += pair2("Ambient light range", "dimmer below low, brightest above high",
                "lux low",  num("luxlo", c.luxLow),
                "lux high", num("luxhi", c.luxHigh));
  disp += fieldH("Dimming gamma", num("gam", c.gammaX10),
                 "\xC3\x97""0.1 (22 = 2.2). Higher = gentler low end");
  disp += fieldH("Temperature unit",
                 "<select name=unit>" + opt("1", unitCur.c_str(), "Fahrenheit")
                 + opt("0", unitCur.c_str(), "Celsius") + "</select>");
  disp += fieldH("Temp offset", num("toff", c.tempOffsetC10),
                 "\xC3\x97""0.1\xC2\xB0""C (40 = 4.0). Applies after restart");
  String rotSel = "<select name=rot>";
  for (int r = 0; r < 4; r++) { char rv[2] = {char('0' + r), 0}; rotSel += opt(rv, rotCur.c_str(), ROT_LABELS[r]); }
  rotSel += "</select>";
  disp += fieldH("Rotation", rotSel, "Applies after restart");
  h += card("Display", disp);

  // AIR QUALITY
  h += card("Air quality",
       pair3("Tier thresholds (ppm)", "above poor = bad",
             "good \xE2\x89\xA4", num("aqg", c.aqGood),
             "fair \xE2\x89\xA4", num("aqf", c.aqFair),
             "poor \xE2\x89\xA4", num("aqp", c.aqPoor)));

  // CALIBRATION
  String cal;
  cal += fieldH("Fresh-air reference (ppm)", num("frc", c.frcReferencePpm));
  cal += fieldH("Altitude (m)", num("alt", c.altitudeM),
                "Above sea level. Applies after restart");
  cal += fieldH("Location profile",
                "<select name=profile>" + opt("0", profCur.c_str(), "Sealed office (ASC off)")
                + opt("1", profCur.c_str(), "Ventilated (ASC on)") + "</select>");
  cal += pair3("Reminder days", "",
               "aging",   num("cala", c.calAgingDays),
               "stale",   num("cals", c.calStaleDays),
               "overdue", num("calo", c.calOverdueDays));
  h += card("Calibration", cal);

  // LOGGING
  h += card("Logging", fieldH("Log interval (seconds)", num("logiv", c.logIntervalSec)));

  // NETWORK
  String net;
  net += fieldH("Device name",
                "<input name=host autocapitalize=none autocorrect=off spellcheck=false value='"
                + htmlAttrEsc(c.hostname) + "'>", "Lowercase letters, digits, hyphens (used for &lt;name&gt;.local)");
  net += check("sta", c.staEnabled, "WiFi \xE2\x80\x94 connect to home network",
               "Off = radio disabled; reconfigure via the setup AP (hold the button). Saving restarts to apply.");
  net += fieldH("Web / OTA password",
                "<input name=webpw type=password autocapitalize=none autocorrect=off "
                "spellcheck=false placeholder='(blank keeps current)'>");
  net += fieldH("Repeat password",
                "<input name=webpw2 type=password autocapitalize=none autocorrect=off "
                "spellcheck=false placeholder='(repeat to confirm)'>");
  net += fieldH("WiFi network",
                "<input name=ssid autocapitalize=none autocorrect=off autocomplete=off "
                "spellcheck=false value='" + htmlAttrEsc(c.wifiSsid) + "'>");
  net += fieldH("WiFi password",
                "<input name=pass type=password autocapitalize=none autocorrect=off "
                "spellcheck=false placeholder='(leave blank to keep)'>");
  String tzSel = "<select name=tz>";
  for (auto& o : TZ_OPTIONS) tzSel += opt(o[0], c.timezone, o[1]);
  tzSel += "</select>";
  net += fieldH("Time zone", tzSel);
  h += card("Network", net);

  h += "<div class=btns>"
       "<button class=save formaction=/save>Save</button>"
       "<button class=sync formaction=/sync>Save &amp; sync time</button></div>"
       "<div class=s style='margin-top:10px'>Display and air-quality changes are live; "
       "rotation, profile, home-WiFi, altitude, and temp offset apply after a restart. "
       "Save &amp; sync also sets the clock from NTP.</div></form>"
       "<form method=POST action=/restart style='margin-top:14px'>"
       "<button style='width:100%;background:#7a2e2e'>Restart device</button></form>"
       "</div>";
  return h;
}

void sendResult(const char* title, const char* detail) {
  String h = "<!doctype html><meta charset=utf-8>"
             "<meta name=viewport content='width=device-width,initial-scale=1'>"
             "<title>stuffy</title>" UI_CSS
             "<div class=wrap><div class=card><h1 style='margin-top:0'>";
  h += title;
  h += "</h1><div class=s style='font-size:14px'>"; h += detail;
  h += "</div><div style='margin-top:16px'><a href='/'>\xE2\x86\x90 Back to settings</a></div>"
       "</div></div>";
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
  String hn = server.arg("host");           // sanitize to mDNS-legal [a-z0-9-]
  if (hn.length()) {
    String clean;
    for (size_t i = 0; i < hn.length(); i++) {
      char ch = hn[i];
      if (ch >= 'A' && ch <= 'Z') ch += 32;
      if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-') clean += ch;
    }
    if (clean.length()) strlcpy(c.hostname, clean.c_str(), sizeof(c.hostname));
  }
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
  // keep OTA auth in step with a password set after boot (routes register once)
  if (c.webPassword[0]) ElegantOTA.setAuth("admin", c.webPassword);
  // apply a timezone change immediately (clock display, event log, /diag) —
  // previously only setup()/NTP paths called tzset, so plain Save left the old zone
  setenv("TZ", c.timezone, 1);
  tzset();
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

// Send a brief page, then restart so a WiFi change takes effect cleanly.
void restartWith(const char* title, const char* note) {
  String h = "<!doctype html><meta charset=utf-8>"
    "<meta name=viewport content='width=device-width,initial-scale=1'>"
    "<meta http-equiv=refresh content='9;url=/'>" UI_CSS
    "<div class=wrap><div class=card><h1 style='margin-top:0'>";
  h += title;
  h += "</h1><div class=s>";
  h += note;
  h += "</div></div></div>";
  server.send(200, "text/html", h);
  delay(500);
  ESP.restart();
}

void handleSaveSettings() {
  if (!authed()) return;
  if (passwordMismatch()) return;
  bool wasSta = settings::cfg.staEnabled;
  applyFormToSettings();
  if (settings::cfg.staEnabled != wasSta) {       // WiFi toggled -> restart to (dis)connect
    restartWith("Applying WiFi change\xE2\x80\xA6",
                "Restarting. If you turned WiFi off, reconnect via the device's setup AP "
                "(hold the button).");
    return;
  }
  sendResult("Saved", "Settings stored. Display and air-quality changes are live; "
                      "profile, altitude, and temp offset apply after a restart.");
}

void handleSync() {
  if (!authed()) return;
  if (passwordMismatch()) return;
  bool wasSta = settings::cfg.staEnabled;
  applyFormToSettings();
  bool toggled = settings::cfg.staEnabled != wasSta;

  // Already on home WiFi -> NTP, then honor a WiFi-toggle change like Save does.
  if (gStaActive && WiFi.status() == WL_CONNECTED) {
    bool ok = doNtp();
    if (toggled) {
      restartWith(ok ? "Time synced \xE2\x80\x94 applying WiFi change\xE2\x80\xA6"
                     : "Applying WiFi change\xE2\x80\xA6",
                  "Restarting. If you turned WiFi off, reconnect via the device's setup AP "
                  "(hold the button).");
      return;
    }
    if (ok) sendResult("Time synced", "Clock set from NTP.");
    else    sendResult("No time", "NTP didn't answer. Try again shortly.");
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
    // stay in the AP so the user can fix credentials and retry (no restart even
    // if the toggle changed — a reboot here would strand the device offline)
    gPhase = portal::P_FAILED;
    strlcpy(gStatus, "wifi failed", sizeof(gStatus));
    sendResult("Couldn't connect", "Check the network name and password, then try again.");
    return;
  }
  bool ntpOk = doNtp();
  gPhase = ntpOk ? portal::P_SYNCED : portal::P_FAILED;
  if (!ntpOk) strlcpy(gStatus, "ntp failed", sizeof(gStatus));

  if (settings::cfg.staEnabled) {
    // This ad-hoc STA link never set gStaActive, so exiting the AP would tear it
    // down (the "verified WiFi, then went dark" trap). Restart: the device boots
    // straight onto home WiFi properly (mDNS, LAN page, NTP retry if needed).
    restartWith(ntpOk ? "Time synced \xE2\x80\x94 joining home WiFi\xE2\x80\xA6"
                      : "Joining home WiFi\xE2\x80\xA6",
                "WiFi verified. Restarting to connect properly \xE2\x80\x94 find the device "
                "at its usual address in a few seconds.");
    return;
  }
  // One-shot sync (WiFi stays off): the link is dropped when the AP closes.
  if (ntpOk) sendResult("Time synced", "Settings saved and the clock is set from NTP.");
  else       sendResult("Connected, but no time", "NTP didn't answer. Try again in a moment.");
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
  } else {           // STA: bounce to "/" so auth is enforced there (never serve
    server.sendHeader("Location", "/", true);   // the settings page ungated)
    server.send(302, "text/plain", "");
  }
}

void handleHistory() {
  if (!authed()) return;
  server.send(200, "text/html", HISTORY_PAGE);
}

void handleDataJson() {
  if (!authed()) return;
  long hours = server.arg("h").toInt();        // 0 / absent = all history
  if (hours < 0) hours = 0;
  if (hours > 24L * 3650) hours = 24L * 3650;  // clamp: h*3600 must not wrap uint32
  uint32_t cutoff = 0;
  if (hours > 0 && gTelem.nowEpoch > 0) {
    uint32_t span = (uint32_t)hours * 3600;
    cutoff = (gTelem.nowEpoch > span) ? gTelem.nowEpoch - span : 0;
  }
  uint32_t total = 0;
  datalog::readAll([&](const datalog::Rec& r) { if (r.t >= cutoff) total++; });
  uint32_t stride = (total > LOG_GRAPH_MAX_POINTS) ? (total / LOG_GRAPH_MAX_POINTS) : 1;
  if (stride == 0) stride = 1;
  bool unitF = settings::cfg.tempUnitF;

  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "");
  server.sendContent("[");
  String buf; buf.reserve(2048);
  uint32_t i = 0; bool first = true;
  datalog::readAll([&](const datalog::Rec& r) {
    if (r.t < cutoff) return;
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
  String raw = datalog::events();

  // Stream in chunks (like /data.csv) — the full log rendered as HTML rows is
  // far bigger than the raw text, and one giant String risks failing on a
  // fragmented heap with WiFi active.
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", "");
  server.sendContent(
    "<!doctype html><meta charset=utf-8>"
    "<meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>stuffy events</title>" UI_CSS
    "<div class=wrap><h1>Event log</h1>"
    "<div class=nav><a href='/'>\xE2\x86\x90 Settings</a><a href='/diag'>Diagnostics</a>"
    "<a href='/history'>History</a></div><div class=card>");

  // Newest-first with local timestamps; tint failures/refusals.
  String buf; buf.reserve(2048);
  bool any = false;
  int end = raw.length();
  while (end > 0) {
    int nl = raw.lastIndexOf('\n', end - 1);
    String line = raw.substring(nl + 1, end);
    end = nl;
    line.trim();
    if (line.length()) {
      int comma = line.indexOf(',');
      if (comma > 0) {
        any = true;
        uint32_t ep = (uint32_t)line.substring(0, comma).toInt();
        String msg = line.substring(comma + 1);
        char ts[24];
        if (ep > 0) { time_t tt = ep; struct tm lt; localtime_r(&tt, &lt); strftime(ts, sizeof(ts), "%b %d  %H:%M", &lt); }
        else strcpy(ts, "(no clock)");
        const char* col = (msg.indexOf("fail") >= 0 || msg.indexOf("refused") >= 0) ? "#b06a1a" : "#333";
        buf += "<div class=stat><span class=k style='flex:none;min-width:100px'>";
        buf += ts;
        buf += "</span><span class=v style='flex:1;text-align:left;color:";
        buf += col; buf += "'>"; buf += htmlAttrEsc(msg); buf += "</span></div>";
        if (buf.length() > 1800) { server.sendContent(buf); buf = ""; }
      }
    }
  }
  if (!any) buf += "<div class=s>No events logged yet.</div>";
  buf += "</div></div>";
  server.sendContent(buf);
  server.sendContent("");
}

#define ROW(label,id)  "<div class=stat><span class=k>" label "</span><span class=v id=" id "></span></div>"
#define ROWD(label,id) "<div class=stat><span class=k>" label "</span><span class=v><i class=d id=" id "D></i><span id=" id "></span></span></div>"

void handleDiag() {
  if (!authed()) return;
  String h = "<!doctype html><meta charset=utf-8>"
    "<meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>stuffy diagnostics</title>" UI_CSS
    "<div class=wrap>"
    "<div style='display:flex;justify-content:space-between;align-items:center'>"
    "<h1 style='margin:0'>Diagnostics</h1>"
    "<span class=s style='color:#159b90'>live \xC2\xB7 5s</span></div>"
    "<div class=nav style='margin-top:10px'><a href='/'>\xE2\x86\x90 Settings</a>"
    "<a href='/history'>History</a><a href='/events'>Event log</a></div>"
    "<div class=grid>"
    "<div class=card><div class=cardh>Sensors</div>"
      ROW("CO2", "co2") ROW("Temp / RH", "th") ROWD("SCD-41", "scd")
      ROWD("Clock", "clock") ROW("Light", "light") "</div>"
    "<div class=card><div class=cardh>Calibration</div>"
      ROW("Profile", "profile") ROW("Last recal", "recal")
      ROW("Correction", "corr") ROWD("Confidence", "conf") "</div>"
    "<div class=card><div class=cardh>Network</div>"
      ROW("Network", "mode") ROW("IP", "ip") ROW("Signal", "sig")
      ROW("Host", "host") ROWD("Auth", "auth") "</div>"
    "<div class=card><div class=cardh>System</div>"
      ROW("Firmware", "fw") ROW("Uptime", "up") ROW("Free heap", "heap")
      ROW("Last reset", "reset") ROW("Battery", "batt") ROW("Est. runtime", "runtime") "</div>"
    "</div>"
    "<div class=card><div class=cardh>Logging</div>"
      ROW("Records", "recs") ROW("Span", "span")
      "<button class=danger onclick='wipe()' style='margin-top:10px'>Erase logged data</button>"
      "</div>"
    "<button class=copy id=cpb onclick='copyDiag()'>Copy diagnostics</button>"
    "<textarea id=cp readonly style='display:none;height:170px;margin-top:10px;"
      "font-family:monospace;font-size:12px;padding:8px'></textarea>"
    "</div>"
    "<script>"
    "function setDot(id,s){var e=document.getElementById(id);if(!e)return;"
    "e.style.background=s=='ok'?'#3abd6e':s=='warn'?'#e0a52f':s=='bad'?'#d8473d':'#bbb';}"
    "function paint(d){for(var k in d){var e=document.getElementById(k);if(e)e.textContent=d[k];}"
    "setDot('scdD',d.scdS);setDot('clockD',d.clockS);setDot('confD',d.confS);setDot('authD',d.authS);}"
    "function load(){fetch('/diag.json').then(r=>r.json()).then(d=>{window._d=d;paint(d);});}"
    "load();setInterval(load,5000);"
    "function copyDiag(){var d=window._d||{};"
    "var t='stuffy diagnostics\\n'"
    "+'CO2: '+d.co2+'\\nTemp/RH: '+d.th+'\\nSCD-41: '+d.scd+'\\nClock: '+d.clock+'\\nLight: '+d.light"
    "+'\\nProfile: '+d.profile+'\\nLast recal: '+d.recal+'\\nCorrection: '+d.corr+'\\nConfidence: '+d.conf"
    "+'\\nNetwork: '+d.mode+'\\nIP: '+d.ip+'\\nSignal: '+d.sig+'\\nHost: '+d.host+'\\nAuth: '+d.auth"
    "+'\\nFirmware: '+d.fw+'\\nUptime: '+d.up+'\\nHeap: '+d.heap+'\\nReset: '+d.reset+'\\nBattery: '+d.batt+'\\nRuntime: '+d.runtime"
    "+'\\nRecords: '+d.recs+'\\nSpan: '+d.span+'\\n';"
    "var a=document.getElementById('cp');a.value=t;a.style.display='block';a.select();"
    "try{document.execCommand('copy');document.getElementById('cpb').textContent='Copied to clipboard';}catch(e){}}"
    "function wipe(){if(!confirm('Erase all logged history? This cannot be undone.'))return;"
    "fetch('/wipe',{method:'POST'}).then(()=>load());}"
    "</script>";
  server.send(200, "text/html", h);
}

void handleWipe() {
  if (!authed()) return;
  datalog::clear();
  server.send(200, "text/plain", "ok");
}

void handleDiagJson() {
  if (!authed()) return;
  Settings& c = settings::cfg;
  portal::Telemetry& t = gTelem;
  char b[64];
  String j = "{";
  auto kv = [&](const char* k, const String& v) { j += '"'; j += k; j += "\":\""; j += jsonEsc(v); j += "\","; };

  if (t.co2) { snprintf(b, sizeof(b), "%u ppm", t.co2); kv("co2", b); } else kv("co2", "warming up");
  { float ts = c.tempUnitF ? t.tempC * 9.0f / 5 + 32 : t.tempC;
    snprintf(b, sizeof(b), "%.1f %c / %.0f%%", ts, c.tempUnitF ? 'F' : 'C', t.hum); kv("th", b); }
  if (!t.co2)            { kv("scd", "warming up"); kv("scdS", "warn"); }
  else if (t.scdStale)   { snprintf(b, sizeof(b), "stale / %lus ago", (unsigned long)t.scdAgeSec); kv("scd", b); kv("scdS", "bad"); }
  else                   { snprintf(b, sizeof(b), "ok / %lus ago", (unsigned long)t.scdAgeSec); kv("scd", b); kv("scdS", "ok"); }
  if (!t.hasRtc)         { kv("clock", "RTC absent"); kv("clockS", "bad"); }
  else if (!t.timeValid) { kv("clock", "time not set"); kv("clockS", "warn"); }
  else { time_t tt = t.nowEpoch; struct tm lt; localtime_r(&tt, &lt);
         snprintf(b, sizeof(b), "set / %02d:%02d", lt.tm_hour, lt.tm_min); kv("clock", b); kv("clockS", "ok"); }
  if (!t.hasLux) snprintf(b, sizeof(b), "no sensor / %d%%", t.brightness * 100 / 255);
  else           snprintf(b, sizeof(b), "%.0f lx / %d%%", t.lux, t.brightness * 100 / 255);
  kv("light", b);

  snprintf(b, sizeof(b), "%s / ASC %s", c.profile == PROFILE_VENTILATED ? "ventilated" : "sealed",
           c.profile == PROFILE_VENTILATED ? "on" : "off"); kv("profile", b);
  if (t.frcValid) { snprintf(b, sizeof(b), "%+d ppm", t.frcCorrPpm); kv("corr", b); } else kv("corr", "-");
  if (!t.timeValid || c.lastFrcEpoch == 0) { kv("recal", "never"); kv("conf", "not set"); kv("confS", "warn"); }
  else {
    uint32_t days = (t.nowEpoch > c.lastFrcEpoch)          // clock corrected backwards
                      ? (t.nowEpoch - c.lastFrcEpoch) / 86400UL : 0;
    snprintf(b, sizeof(b), "%lu days ago", (unsigned long)days); kv("recal", b);
    const char *cf, *cs;
    if (days >= c.calOverdueDays)    { cf = "overdue"; cs = "bad"; }
    else if (days >= c.calStaleDays) { cf = "stale"; cs = "warn"; }
    else if (days >= c.calAgingDays) { cf = "aging"; cs = "warn"; }
    else                             { cf = "fresh"; cs = "ok"; }
    kv("conf", cf); kv("confS", cs);
  }

  if (gStaActive)     kv("mode", WiFi.SSID());
  else if (gApActive) kv("mode", gApSsid);
  else                kv("mode", "off");
  kv("ip",   gStaActive ? gStaIp : gApActive ? gApIp : "-");
  if (gStaActive) { snprintf(b, sizeof(b), "%d dBm", WiFi.RSSI()); kv("sig", b); } else kv("sig", "-");
  snprintf(b, sizeof(b), "%s.local", c.hostname); kv("host", b);
  if (c.webPassword[0]) { kv("auth", "on"); kv("authS", "ok"); } else { kv("auth", "off"); kv("authS", "warn"); }

  kv("fw", "v" FIRMWARE_VERSION);
  kv("up", upStr());
  snprintf(b, sizeof(b), "%lu KB", (unsigned long)(ESP.getFreeHeap() / 1024)); kv("heap", b);
  kv("reset", t.resetReason ? t.resetReason : "?");
  if (t.hasBatt) { snprintf(b, sizeof(b), "%.0f%% / %.2f V", t.battPct, t.battV); kv("batt", b); }
  else           kv("batt", "no gauge");
  if (!t.hasBatt)               kv("runtime", "-");
  else if (t.battRate > 1.0f)   kv("runtime", "charging");
  else if (t.battRate < -1.0f)  { snprintf(b, sizeof(b), "~%.1f h left", t.battPct / -t.battRate); kv("runtime", b); }
  else                          kv("runtime", "steady");

  snprintf(b, sizeof(b), "%lu / every %us", (unsigned long)datalog::count(), c.logIntervalSec); kv("recs", b);
  uint32_t o, nw;
  if (datalog::span(o, nw)) {
    char d1[16], d2[16]; time_t a = o, z = nw; struct tm la, lz;
    localtime_r(&a, &la); localtime_r(&z, &lz);
    strftime(d1, sizeof(d1), "%b %d", &la); strftime(d2, sizeof(d2), "%b %d", &lz);
    snprintf(b, sizeof(b), "%s -> %s", d1, d2); kv("span", b);
  } else kv("span", "-");

  if (j.endsWith(",")) j.remove(j.length() - 1);
  j += "}";
  server.send(200, "application/json", j);
}

void setupRoutes() {
  // Register handlers exactly once: server.stop() doesn't free the handler
  // chain, so re-registering on every AP open/close cycle leaks heap.
  static bool routed = false;
  if (!routed) {
    routed = true;
    server.on("/", handleRoot);
    server.on("/save", HTTP_POST, handleSaveSettings);
    server.on("/sync", HTTP_POST, handleSync);
    server.on("/restart", HTTP_POST, handleRestart);
    server.on("/history", handleHistory);
    server.on("/data.json", handleDataJson);
    server.on("/data.csv", handleDataCsv);
    server.on("/events", handleEvents);
    server.on("/diag", handleDiag);
    server.on("/diag.json", handleDiagJson);
    server.on("/wipe", HTTP_POST, handleWipe);
    server.onNotFound(handleNotFound);
    ElegantOTA.begin(&server);               // serves /update with a progress UI
  }
  if (settings::cfg.webPassword[0])
    ElegantOTA.setAuth("admin", settings::cfg.webPassword);
  server.begin();
}

}  // namespace

void portal::setTelemetry(const portal::Telemetry& t) { gTelem = t; }

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
  gApActive = false;
  dns.stop();
  WiFi.softAPdisconnect(true);             // drop the AP
  if (gStaActive) {
    WiFi.mode(WIFI_STA);                    // keep the home-WiFi link + LAN server up
  } else {
    server.stop();
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
  }
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
