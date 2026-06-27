#pragma once
#include <Arduino.h>

// On-demand WiFi config portal: a captive AP serving a small page to enter
// home WiFi credentials + timezone, then connect (STA) and fetch time via NTP.
// WiFi is only up while the portal is active (entered by the button hold).

namespace portal {
  enum Phase { P_AP, P_CONNECTING, P_SYNCED, P_FAILED };

  void start();      // bring up AP + captive DNS + web server
  void handle();     // pump DNS + server; call frequently while active
  void stop();       // tear down everything, radio off
  bool active();

  const char* apSsid();
  const char* apIp();
  Phase       phase();
  const char* statusLine();                // short status for the display

  // True exactly once after a successful NTP sync; yields the UTC epoch so
  // the caller can set the RTC.
  bool consumeSynced(uint32_t& epochUtc);
}
