// weather.cpp — current weather over WiFi from open-meteo (see weather.h).
//
// Uses open-meteo.com's free, no-key forecast API over PLAIN HTTP. Earlier
// this used wttr.in over HTTPS, but the ESP32-C3's TLS handshake to it
// failed (ssl_starttls) — plain HTTP sidesteps the whole TLS stack, which
// also shrinks the binary. The response is small JSON; we hand-parse the
// two fields we need (temperature_2m, weather_code) rather than pull in a
// JSON library.

#include "weather.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "../clock/wifi_creds.h"

namespace weather {
namespace {

// Bengaluru. open-meteo takes coordinates, not a city name. `current=`
// asks for just the fields we use, keeping the JSON tiny.
const char* URL =
    "http://api.open-meteo.com/v1/forecast"
    "?latitude=12.97&longitude=77.59"
    "&current=temperature_2m,weather_code";

// Don't refetch more often than this — the weather doesn't change minute
// to minute. The scheduled shows (8am / 6pm) request a refresh; this just
// bounds the rate.
const uint32_t MIN_REFETCH_MS = 30UL * 60UL * 1000UL;  // 30 min

// WiFi association timeout for a fetch. If we can't join in this long, give
// up this attempt and drop the radio; the next requestRefresh() retries.
const uint32_t WIFI_JOIN_TIMEOUT_MS = 12000;

// HTTP request timeout.
const uint32_t HTTP_TIMEOUT_MS = 8000;

enum class State : uint8_t { Idle, Connecting, Fetching, Done };

State state_ = State::Idle;
uint32_t connectStartMs_ = 0;
uint32_t lastSuccessMs_ = 0;
bool everSucceeded_ = false;
bool pending_ = false;  // a refresh was requested while not Idle

int tempC_ = 0;
Sky sky_ = Sky::Unknown;

void radioOff() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}

// Map a WMO weather_code (open-meteo) to a coarse Sky category.
// Code groups: 0 clear; 1-3 mainly clear..overcast; 45/48 fog;
// 51-57 drizzle; 61-67 rain; 71-77 snow; 80-82 rain showers;
// 85-86 snow showers; 95-99 thunderstorm.
Sky classifyCode(int code) {
  if (code == 0) return Sky::Clear;
  if (code >= 1 && code <= 3) return Sky::Cloudy;
  if (code == 45 || code == 48) return Sky::Cloudy;
  if (code >= 95 && code <= 99) return Sky::Storm;
  if ((code >= 71 && code <= 77) || code == 85 || code == 86) return Sky::Snow;
  if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) return Sky::Rain;
  return Sky::Unknown;
}

// Pull the numeric value following `"<key>":` out of a JSON blob. Returns
// false if the key isn't present. Minimal hand-parse — fine for the small,
// fixed-shape open-meteo "current" object; no JSON library needed.
bool jsonNumber(const String& body, const char* key, double& out) {
  String needle = String("\"") + key + "\":";
  int i = body.indexOf(needle);
  if (i < 0) return false;
  i += needle.length();
  out = atof(body.c_str() + i);
  return true;
}

// Parse open-meteo JSON: temperature_2m (°C) and weather_code.
//
// The response carries each key twice — once in "current_units" (where the
// value is a unit *string*, e.g. "temperature_2m":"°C") and once in
// "current" (the real number). We must read from "current", so anchor the
// search past the "current": object; otherwise we'd parse the unit string
// and get 0.
bool parseResponse(const String& body) {
  int cur = body.indexOf("\"current\":");
  if (cur < 0) return false;
  String current = body.substring(cur);  // everything from "current": on

  double temp, code;
  if (!jsonNumber(current, "temperature_2m", temp)) return false;
  if (!jsonNumber(current, "weather_code", code)) return false;
  tempC_ = (int)lround(temp);
  sky_ = classifyCode((int)code);
  return true;
}

}  // namespace

void requestRefresh() {
  if (!wifiHasCreds()) return;
  // Rate-limit successful fetches; always allow the very first one.
  if (everSucceeded_ && (millis() - lastSuccessMs_) < MIN_REFETCH_MS) return;

  if (state_ != State::Idle) {
    pending_ = true;  // a fetch is running; remember to honor this after
    return;
  }
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID_STR, WIFI_PASS_STR);
  connectStartMs_ = millis();
  state_ = State::Connecting;
}

void update(uint32_t now) {
  switch (state_) {
    case State::Idle:
    case State::Done:
      return;

    case State::Connecting:
      if (WiFi.status() == WL_CONNECTED) {
        state_ = State::Fetching;
        return;
      }
      if (now - connectStartMs_ >= WIFI_JOIN_TIMEOUT_MS) {
        radioOff();
        state_ = State::Idle;  // give up; next request retries
      }
      return;

    case State::Fetching: {
      // Plain HTTP (no TLS) — open-meteo serves over http, which avoids the
      // ESP32-C3 TLS handshake that failed against the old HTTPS endpoint.
      WiFiClient client;
      client.setTimeout(HTTP_TIMEOUT_MS / 1000);

      HTTPClient http;
      bool ok = false;
      if (http.begin(client, URL)) {
        http.setTimeout(HTTP_TIMEOUT_MS);
        int code = http.GET();
        if (code == HTTP_CODE_OK) {
          String body = http.getString();
          ok = parseResponse(body);
        }
        http.end();
      }

      if (ok) {
        everSucceeded_ = true;
        lastSuccessMs_ = now;
      }
      radioOff();
      // If another refresh was asked for mid-flight, fall back to Idle so
      // the next requestRefresh() (or the pending flag) can start fresh.
      state_ = State::Idle;
      if (pending_) {
        pending_ = false;
        requestRefresh();
      }
      return;
    }
  }
}

bool hasData() { return everSucceeded_; }
int temperatureC() { return tempC_; }
Sky sky() { return sky_; }
bool isFetching() { return state_ == State::Connecting || state_ == State::Fetching; }

}  // namespace weather
