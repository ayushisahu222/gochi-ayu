// clock.h — wall-clock time over WiFi + NTP.
//
// The rest of the firmware only knows millis() (time since boot). This
// module connects to WiFi at startup and syncs the real time of day from
// an NTP server, so features that fire at clock times (e.g. the water
// reminder at 8am/10am/...) know what hour it actually is.
//
// Credentials and the UTC offset come from compile-time -D flags fed by
// the Makefile from .env (WIFI_SSID / WIFI_PASS). With no creds, or if
// WiFi/NTP never succeeds, isReady() stays false and the rest of the pet
// boots and runs normally — time-of-day features just stay dormant.
#pragma once

#include <stdint.h>

namespace clock_ {

// Kick off the WiFi connection and NTP sync. Non-blocking: returns right
// away; update() does the polling. Safe to call even with no creds.
void begin();

// Advance the connect/sync state machine. Call once per loop with millis().
void update(uint32_t now);

// True once NTP has delivered a valid time at least once.
bool isReady();

// Local hour (0–23) and minute (0–59). Only meaningful when isReady();
// returns 0 otherwise.
int localHour();
int localMinute();

}  // namespace clock_
