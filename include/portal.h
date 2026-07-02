#pragma once
#include <Arduino.h>

// WiFi config + OTA. Two modes:
//  - AP   : on-demand captive portal (button hold, off-network) for first setup.
//  - STA  : when staEnabled + creds are set, connect to home WiFi at boot and
//           serve the same settings page (and OTA) on the LAN at <host>.local.
// Both serve identical routes: "/" settings, "/save", "/sync", "/update" (OTA).

namespace portal {
  enum Phase { P_AP, P_CONNECTING, P_SYNCED, P_FAILED };

  // Live device state pushed from the main loop for the /diag page. The portal
  // owns network/system/logging facts itself; this carries what only main knows.
  struct Telemetry {
    uint16_t co2; float tempC; float hum;
    bool     scdStale; uint32_t scdAgeSec;
    bool     hasRtc; bool hasLux;
    bool     timeValid; uint32_t nowEpoch;
    float    lux; int brightness;     // brightness = perceptual level 0..255
    bool     frcValid; int frcCorrPpm;
    bool     hasBatt; float battPct; float battV; float battRate;  // rate %/hr, +charging
    const char* resetReason;
  };
  void setTelemetry(const Telemetry& t);

  void startAP();      // captive AP + DNS + server
  bool startSTA();     // connect home WiFi + mDNS + server + NTP; true if joined
  void handle();       // pump DNS/server; call every loop while active
  void stopAP();       // tear down AP (STA, if up, is left running)

  bool apActive();
  bool staActive();

  const char* apSsid();
  const char* apIp();
  const char* hostUrl();   // "<hostname>.local"
  const char* staIp();
  int         rssi();      // STA signal (dBm), 0 if not connected

  Phase       phase();
  const char* statusLine();

  bool consumeSynced(uint32_t& epochUtc);   // true once after a good NTP sync
}
