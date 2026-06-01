// weather.h — current weather for a fixed city, over WiFi.
//
// Fetches a compact plain-text line from wttr.in (no JSON library needed)
// and reduces it to a temperature and a coarse condition category that the
// weather view draws as a little animated scene. The fetch manages its own
// WiFi: it brings the radio up, does one HTTPS GET, then drops it again, so
// the rest of the time the radio stays off (matching the clock module).
//
// City and refresh cadence are compile-time; credentials come from the
// shared wifi_creds.h. With no creds the module stays Idle forever and
// hasData() never becomes true.
#pragma once

#include <stdint.h>

namespace weather {

// Coarse sky category, derived from wttr.in's condition text. The view
// picks a scene per category.
enum class Sky : uint8_t {
  Unknown,
  Clear,    // sunny / clear
  Cloudy,   // cloud / overcast / fog / mist
  Rain,     // rain / drizzle / shower
  Storm,    // thunder
  Snow,     // snow / sleet
};

// Kick off a refresh if one isn't already running and the last successful
// fetch is older than the refresh interval (or there's never been one).
// Non-blocking: the actual HTTP work is driven by update(). Calling it
// while a fetch is in flight is a no-op.
void requestRefresh();

// Drive the fetch state machine. Call once per loop with millis().
void update(uint32_t now);

// True once at least one fetch has succeeded.
bool hasData();

// Last fetched values (meaningful only when hasData()).
int temperatureC();
Sky sky();

// True while a network fetch is in progress (the view can show a spinner).
bool isFetching();

}  // namespace weather
