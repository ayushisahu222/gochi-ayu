// water_reminder_view.cpp — the "drink water" nag (see header).

#include "water_reminder_view.h"

#include <Arduino.h>

#include "../config.h"
#include "../renderer.h"

namespace {
// Two centered lines, stacked. With the ~14px-tall ncenB14 font, tops at
// 18 and 36 put the pair vertically centered on the 64px panel with a
// comfortable gap.
const char* LINE1 = "Drink";
const char* LINE2 = "Water Bitch";
const int LINE1_TOP_Y = 18;
const int LINE2_TOP_Y = 36;

const uint32_t STEP_MS = 33;  // ~30 fps droplet animation

// Droplet fall speed range, in 1/16-px per step.
const int SPEED_MIN16 = 16;  // 1 px/step
const int SPEED_MAX16 = 56;  // 3.5 px/step
}  // namespace

void WaterReminderView::seedDrop_(Drop& d, bool startOffscreen) {
  d.x = random(0, OLED_W);
  // Either drifting in from above the top edge (initial spread) or just
  // re-entering at the very top after wrapping.
  d.y16 = startOffscreen ? -random(0, OLED_H) * 16 : 0;
  d.speed16 = random(SPEED_MIN16, SPEED_MAX16 + 1);
}

void WaterReminderView::onEnter() {
  // Spread the droplets across the screen so the first frame already looks
  // like rain rather than a single row dropping in.
  for (int i = 0; i < DROP_COUNT; ++i) {
    seedDrop_(drops_[i], /*startOffscreen=*/true);
    drops_[i].y16 = random(0, OLED_H) * 16;
  }
  seeded_ = true;
  lastStepMs_ = millis();
}

void WaterReminderView::update(uint32_t now) {
  if (!seeded_) onEnter();
  if (now - lastStepMs_ < STEP_MS) return;
  lastStepMs_ = now;

  for (int i = 0; i < DROP_COUNT; ++i) {
    drops_[i].y16 += drops_[i].speed16;
    if ((drops_[i].y16 >> 4) >= OLED_H) {
      seedDrop_(drops_[i], /*startOffscreen=*/false);
    }
  }
}

void WaterReminderView::render(Renderer& r) {
  // The message sits underneath; droplets fall over it on top.
  int w1 = r.textWidth(LINE1);
  int w2 = r.textWidth(LINE2);
  r.drawText((OLED_W - w1) / 2, LINE1_TOP_Y, LINE1);
  r.drawText((OLED_W - w2) / 2, LINE2_TOP_Y, LINE2);

  for (int i = 0; i < DROP_COUNT; ++i) {
    int x = drops_[i].x;
    int y = drops_[i].y16 >> 4;
    // A teardrop: a 1px tail above a small round body.
    r.drawPixel(x, y - 3);
    r.drawPixel(x, y - 2);
    r.fillCircle(x, y, 1);
  }
}
