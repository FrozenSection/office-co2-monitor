#include "datalog.h"
#include "config.h"
#include <LittleFS.h>
#include <math.h>

namespace {
  const char*  CUR = "/log.bin";
  const char*  OLD = "/log.old";
  const char*  EVT = "/events.log";
  const size_t EVT_MAX = 6000;          // cap the event log; drop oldest wholesale
  const size_t REC = sizeof(datalog::Rec);
  bool         gReady = false;

  uint32_t fileRecs(const char* path) {
    File f = LittleFS.open(path, "r");
    if (!f) return 0;
    uint32_t n = f.size() / REC;
    f.close();
    return n;
  }
}

bool datalog::begin() {
  if (!LittleFS.begin(true)) {          // format on first use
    Serial.println(F("datalog: LittleFS mount failed"));
    return false;
  }
  gReady = true;
  Serial.printf("datalog: ready, %u records\n", (unsigned)count());
  return true;
}

uint32_t datalog::count() {
  if (!gReady) return 0;
  return fileRecs(OLD) + fileRecs(CUR);
}

void datalog::append(uint32_t t, uint16_t co2, float tempC, float rh) {
  if (!gReady) return;

  // Rotate when the current file is full: drop the old, current -> old.
  if (fileRecs(CUR) >= LOG_MAX_RECS_PER_FILE) {
    LittleFS.remove(OLD);
    LittleFS.rename(CUR, OLD);
  }

  Rec r;
  r.t       = t;
  r.co2     = co2;
  r.tempC10 = (int16_t)lroundf(tempC * 10.0f);
  r.rh      = (uint8_t)lroundf(rh);

  File f = LittleFS.open(CUR, "a");
  if (f) {
    f.write((const uint8_t*)&r, REC);
    f.close();
  }
}

void datalog::event(uint32_t t, const char* msg) {
  if (!gReady) return;
  // Keep it bounded: once it grows past the cap, start fresh (events are rare).
  File chk = LittleFS.open(EVT, "r");
  if (chk) { size_t sz = chk.size(); chk.close(); if (sz > EVT_MAX) LittleFS.remove(EVT); }

  File f = LittleFS.open(EVT, "a");
  if (f) {
    f.printf("%lu,%s\n", (unsigned long)t, msg);
    f.close();
  }
  Serial.printf("event: %s\n", msg);
}

String datalog::events() {
  String out;
  if (!gReady) return out;
  File f = LittleFS.open(EVT, "r");
  if (!f) return out;
  out.reserve(f.size() + 1);
  while (f.available()) out += (char)f.read();
  f.close();
  return out;
}

void datalog::readAll(std::function<void(const Rec&)> emit) {
  if (!gReady) return;
  const char* files[2] = { OLD, CUR };
  for (int i = 0; i < 2; i++) {
    File f = LittleFS.open(files[i], "r");
    if (!f) continue;
    Rec r;
    while (f.read((uint8_t*)&r, REC) == (int)REC) emit(r);
    f.close();
  }
}
