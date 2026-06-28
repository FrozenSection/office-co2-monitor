#pragma once
#include <Arduino.h>
#include <functional>

// Lightweight time-series log on internal flash (LittleFS). Fixed-size binary
// records, append-and-rotate across two files so the total stays bounded
// (oldest history is dropped a file at a time). Timestamps come from the RTC.

namespace datalog {
  struct __attribute__((packed)) Rec {
    uint32_t t;        // unix UTC seconds
    uint16_t co2;      // ppm
    int16_t  tempC10;  // degC * 10
    uint8_t  rh;       // %
  };

  bool     begin();                                   // mount LittleFS
  void     append(uint32_t t, uint16_t co2, float tempC, float rh);
  uint32_t count();                                   // total records held
  void     readAll(std::function<void(const Rec&)> emit);  // oldest -> newest

  // Human-readable event log (boots, calibrations, sensor faults). Small, capped
  // text file — one "epoch,message" line per event. t=0 if the clock isn't set.
  void     event(uint32_t t, const char* msg);
  String   events();                                  // whole log (for the web UI)
}
