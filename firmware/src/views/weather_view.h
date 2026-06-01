// weather_view.h — the animated weather scene.
//
// Draws a little scene for the current weather (sun, clouds, rain, ...)
// with the temperature shown alongside. Reads from the weather module;
// firmware.ino schedules it (morning / evening) like the water reminder.
// If weather data isn't ready yet it shows a "fetching" placeholder.
#pragma once

#include <stdint.h>

#include "view.h"

class WeatherView : public View {
 public:
  void onEnter() override;
  void update(uint32_t now) override;
  void render(Renderer& r) override;

 private:
  static const int RAIN_COUNT = 10;

  struct Drop {
    int x;
    int y16;
    int speed16;
  };

  void seedDrop_(Drop& d, bool spread);
  void drawSun_(Renderer& r, uint32_t now);
  void drawCloud_(Renderer& r, int cx, int cy);
  void drawTemp_(Renderer& r);

  Drop rain_[RAIN_COUNT];
  bool seeded_ = false;
  uint32_t lastStepMs_ = 0;
  uint32_t enterMs_ = 0;  // for time-based effects (sun-ray rotation)
};
