// weather_view.cpp — the animated weather scene (see header).

#include "weather_view.h"

#include <Arduino.h>
#include <math.h>
#include <stdio.h>

#include "../config.h"
#include "../renderer.h"
#include "../weather/weather.h"

namespace {
const uint32_t STEP_MS = 33;  // ~30 fps

// Rain fall speed range, in 1/16-px per step.
const int RAIN_MIN16 = 24;
const int RAIN_MAX16 = 56;

// The scene lives on the left; the temperature reads on the right.
const int SCENE_CX = 38;  // scene horizontal center
const int SCENE_CY = 26;  // scene vertical center
}  // namespace

void WeatherView::seedDrop_(Drop& d, bool spread) {
  // Rain falls across the whole width, behind/over the scene + text.
  d.x = random(0, OLED_W);
  d.y16 = spread ? random(0, OLED_H) * 16 : -random(0, 8) * 16;
  d.speed16 = random(RAIN_MIN16, RAIN_MAX16 + 1);
}

void WeatherView::onEnter() {
  for (int i = 0; i < RAIN_COUNT; ++i) seedDrop_(rain_[i], /*spread=*/true);
  seeded_ = true;
  lastStepMs_ = millis();
  enterMs_ = lastStepMs_;
  // Ask for fresh data when the view comes up. The module rate-limits, so
  // this is cheap if we fetched recently; otherwise it refetches in the
  // background and the view shows whatever it has (or "fetching").
  weather::requestRefresh();
}

void WeatherView::update(uint32_t now) {
  if (!seeded_) onEnter();
  if (now - lastStepMs_ < STEP_MS) return;
  lastStepMs_ = now;

  // Advance rain only when it's relevant — but stepping it always is
  // harmless and keeps the state warm for a sky change mid-view.
  for (int i = 0; i < RAIN_COUNT; ++i) {
    rain_[i].y16 += rain_[i].speed16;
    if ((rain_[i].y16 >> 4) >= OLED_H) seedDrop_(rain_[i], /*spread=*/false);
  }
}

void WeatherView::drawSun_(Renderer& r, uint32_t now) {
  int cx = SCENE_CX, cy = SCENE_CY;
  r.fillCircle(cx, cy, 9);
  // Eight rays that slowly rotate, for a touch of life.
  float phase = (now - enterMs_) * 0.001f;  // radians-ish, slow
  for (int k = 0; k < 8; ++k) {
    float a = phase + k * (float)(M_PI / 4.0);
    int x0 = cx + (int)(12 * cosf(a));
    int y0 = cy + (int)(12 * sinf(a));
    int x1 = cx + (int)(16 * cosf(a));
    int y1 = cy + (int)(16 * sinf(a));
    r.drawLine(x0, y0, x1, y1);
  }
}

void WeatherView::drawCloud_(Renderer& r, int cx, int cy) {
  // Three overlapping circles + a flat base read as a puffy cloud.
  r.fillCircle(cx - 8, cy + 2, 6);
  r.fillCircle(cx + 8, cy + 2, 6);
  r.fillCircle(cx, cy - 2, 8);
  r.fillRoundRect(cx - 14, cy + 2, 28, 6, 3);
}

void WeatherView::drawTemp_(Renderer& r) {
  char buf[12];
  snprintf(buf, sizeof(buf), "%dC", weather::temperatureC());
  int w = r.textWidth(buf);
  // Right side, vertically centered with the scene.
  int x = OLED_W - w - 6;
  if (x < SCENE_CX + 18) x = SCENE_CX + 18;  // don't collide with the scene
  r.drawText(x, 20, buf);
}

void WeatherView::render(Renderer& r) {
  if (!weather::hasData()) {
    // No reading yet — show a small placeholder so the screen isn't blank.
    const char* msg = weather::isFetching() ? "Weather..." : "No weather";
    int w = r.textWidth(msg);
    r.drawText((OLED_W - w) / 2, 24, msg);
    return;
  }

  using weather::Sky;
  Sky s = weather::sky();

  switch (s) {
    case Sky::Clear:
      drawSun_(r, lastStepMs_);
      break;
    case Sky::Cloudy:
      drawCloud_(r, SCENE_CX, SCENE_CY);
      break;
    case Sky::Rain:
    case Sky::Storm:
      drawCloud_(r, SCENE_CX, SCENE_CY - 4);
      for (int i = 0; i < RAIN_COUNT; ++i) {
        int x = rain_[i].x, y = rain_[i].y16 >> 4;
        r.drawLine(x, y, x, y + 3);  // a slanting streak reads as rain
      }
      if (s == Sky::Storm) {
        // A jagged bolt under the cloud.
        r.drawLine(SCENE_CX, SCENE_CY + 6, SCENE_CX - 4, SCENE_CY + 14);
        r.drawLine(SCENE_CX - 4, SCENE_CY + 14, SCENE_CX + 2, SCENE_CY + 14);
        r.drawLine(SCENE_CX + 2, SCENE_CY + 14, SCENE_CX - 2, SCENE_CY + 22);
      }
      break;
    case Sky::Snow:
      drawCloud_(r, SCENE_CX, SCENE_CY - 4);
      for (int i = 0; i < RAIN_COUNT; ++i) {
        r.fillCircle(rain_[i].x, rain_[i].y16 >> 4, 1);  // flakes
      }
      break;
    case Sky::Unknown:
    default:
      // Sun behind a cloud — a neutral "weather" glyph.
      drawSun_(r, lastStepMs_);
      drawCloud_(r, SCENE_CX + 4, SCENE_CY + 4);
      break;
  }

  drawTemp_(r);
}
