// water_reminder_view.h — the "drink water" nag.
//
// Shows a fixed reminder message centered on screen with animated water
// droplets falling over it. Driven by the clock: firmware.ino switches to
// this view at the top of each reminder hour and holds it for a few
// seconds (see WaterReminder in firmware.ino).
#pragma once

#include <stdint.h>

#include "view.h"

class WaterReminderView : public View {
 public:
  void onEnter() override;
  void update(uint32_t now) override;
  void render(Renderer& r) override;

 private:
  static const int DROP_COUNT = 12;

  // Each droplet falls down its own column, wrapping back to the top at a
  // randomized horizontal position when it leaves the bottom.
  struct Drop {
    int x;       // column (px)
    int y16;     // vertical position, 1/16-px fixed point (smooth fall)
    int speed16; // fall speed, 1/16-px per step
  };

  void seedDrop_(Drop& d, bool startOffscreen);

  Drop drops_[DROP_COUNT];
  bool seeded_ = false;
  uint32_t lastStepMs_ = 0;
};
