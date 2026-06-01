// view_manager.h — owns the views and runs the active one.
#pragma once

#include <stdint.h>

#include "face_view.h"
#include "image_view.h"
#include "text_view.h"
#include "water_reminder_view.h"
#include "weather_view.h"

class Renderer;

// ViewManager owns the FaceView, TextView and ImageView and tracks which
// is active. setView() drives the onExit/onEnter handoff; tick() advances
// and draws the active view once per loop.
class ViewManager {
 public:
  void setView(View* v);
  void tick(uint32_t now, Renderer& r);

  FaceView& face() { return face_; }
  TextView& text() { return text_; }
  ImageView& image() { return image_; }
  WaterReminderView& water() { return water_; }
  WeatherView& weather() { return weather_; }

  bool isFaceActive() const { return active_ == &face_; }
  bool isTextActive() const { return active_ == &text_; }
  bool isImageActive() const { return active_ == &image_; }
  bool isWaterActive() const { return active_ == &water_; }
  bool isWeatherActive() const { return active_ == &weather_; }

  // "face", "text", "image", or "none" — used by the GET state response.
  const char* activeViewName() const;

 private:
  FaceView face_;
  TextView text_;
  ImageView image_;
  WaterReminderView water_;
  WeatherView weather_;
  View* active_ = nullptr;
};
