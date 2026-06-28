#pragma once
#include <Arduino.h>

// WiFi config + OTA. Two modes:
//  - AP   : on-demand captive portal (button hold, off-network) for first setup.
//  - STA  : when staEnabled + creds are set, connect to home WiFi at boot and
//           serve the same settings page (and OTA) on the LAN at <host>.local.
// Both serve identical routes: "/" settings, "/save", "/sync", "/update" (OTA).

namespace portal {
  enum Phase { P_AP, P_CONNECTING, P_SYNCED, P_FAILED };

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

  Phase       phase();
  const char* statusLine();

  bool consumeSynced(uint32_t& epochUtc);   // true once after a good NTP sync
}
