// clock.cpp — WiFi + NTP wall-clock time (see clock.h).

#include "clock.h"

#include <Arduino.h>
#include <WiFi.h>
#include <time.h>

#include "wifi_creds.h"

// IST is UTC+5:30 with no daylight saving, so a fixed offset is exact.
// 5*3600 + 30*60 = 19800 seconds.
#ifndef CLOCK_GMT_OFFSET_SEC
#define CLOCK_GMT_OFFSET_SEC 19800
#endif
#ifndef CLOCK_DST_OFFSET_SEC
#define CLOCK_DST_OFFSET_SEC 0
#endif

namespace clock_ {
namespace {

const char* NTP_SERVER = "pool.ntp.org";

// Re-attempt the WiFi connection if it hasn't come up after this long,
// and re-check NTP on the same cadence until the first valid time lands.
const uint32_t RETRY_MS = 15000;

// A real time-of-day always lands in this century; anything earlier means
// the SNTP client hasn't synced yet (it starts the clock near epoch 0).
const time_t MIN_VALID_EPOCH = 1700000000;  // 2023-11-14

bool started_ = false;   // configTime() has been issued
bool ready_ = false;     // a valid time has been read at least once
uint32_t lastTryMs_ = 0;

bool hasCreds() { return wifiHasCreds(); }

}  // namespace

void begin() {
  if (!hasCreds()) return;  // nothing to do without an SSID
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID_STR, WIFI_PASS_STR);
  lastTryMs_ = millis();
}

void update(uint32_t now) {
  if (ready_ || !hasCreds()) return;

  if (WiFi.status() != WL_CONNECTED) {
    // Still associating. Retry the join periodically in case the first
    // attempt missed the AP.
    if (now - lastTryMs_ >= RETRY_MS) {
      lastTryMs_ = now;
      WiFi.begin(WIFI_SSID_STR, WIFI_PASS_STR);
    }
    return;
  }

  if (!started_) {
    // Connected — start the SNTP client. It syncs in the background.
    configTime(CLOCK_GMT_OFFSET_SEC, CLOCK_DST_OFFSET_SEC, NTP_SERVER);
    started_ = true;
    lastTryMs_ = now;
  }

  // Poll for the first valid time.
  time_t t = time(nullptr);
  if (t >= MIN_VALID_EPOCH) {
    ready_ = true;
    // Time is set; we no longer need the radio. Drop it to save power and
    // free RAM — localHour()/localMinute() read the system clock, which
    // keeps ticking without WiFi.
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
  }
}

bool isReady() { return ready_; }

int localHour() {
  if (!ready_) return 0;
  time_t t = time(nullptr);
  struct tm lt;
  localtime_r(&t, &lt);
  return lt.tm_hour;
}

int localMinute() {
  if (!ready_) return 0;
  time_t t = time(nullptr);
  struct tm lt;
  localtime_r(&t, &lt);
  return lt.tm_min;
}

}  // namespace clock_
