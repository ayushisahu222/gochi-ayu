// firmware.ino — Tamagotchi firmware entry point (ESP32-C3 SuperMini).
//
// The pet boots into Free Mode and lives on its own — it wanders through
// moods and shows matching expressions. Any serial command, BOOT-button
// tap, or IMU gesture hands control to Desktop Mode; ~60 s with no
// further input drifts back to Free Mode. IMU gestures take priority
// over everything: pickup → surprised, shake → angry (escalating to sad
// after SHAKE_CRY_COUNT shakes within SHAKE_CRY_WINDOW_MS — the pet has
// had enough). After IMU_FACE_HOLD_MS the pet "calms down" — if Free
// Mode is on, control hands back so it picks a fresh mood-appropriate
// face; otherwise the face reverts to neutral. The buzzer jingles on
// every face change in Desktop Mode. See firmware/README.

#include "src/assets/expressions.h"
#include "src/assets/jingles.h"
#include "src/buzzer/buzzer.h"
#include "src/clock/clock.h"
#include "src/command.h"
#include "src/config.h"
#include "src/display/display.h"
#include "src/imu/motion.h"
#include "src/imu/mpu6050.h"
#include "src/modes/desktop_mode.h"
#include "src/modes/free_mode.h"
#include "src/modes/mode.h"
#include "src/mood.h"
#include "src/renderer.h"
#include "src/transport.h"
#include "src/views/view_manager.h"
#include "src/weather/weather.h"

// Flip to 0 to disable Free Mode: the pet sits in Desktop Mode forever
// and only reacts to host commands / BOOT taps / IMU gestures (the
// motion-triggered face reverts to neutral after IMU_FACE_HOLD_MS
// instead of handing back to autonomous behaviour).
#define FREE_MODE_ENABLED 1

// The pet's mood — shared between the modes: SET mood writes it, Free Mode
// reads and slowly evolves it. RAM-only (a reboot resets it to content).
static Mood petMood = Mood::Content;

static Renderer renderer;
static Transport transport;
static ViewManager viewManager;
static DesktopMode desktopMode(transport, renderer, petMood);
#if FREE_MODE_ENABLED
static FreeMode freeMode(petMood);
#endif

// Default mode: Free if enabled (the pet lives on its own), otherwise
// Desktop (the pet only reacts to commands / buttons / gestures).
#if FREE_MODE_ENABLED
static Mode* currentMode = &freeMode;
#else
static Mode* currentMode = &desktopMode;
#endif
static uint32_t lastCmdMs = 0;  // time of the last command
static ExpressionId jingledExpr = ExpressionId::Count;

// No serial command for this long, while in Desktop Mode, drifts to Free.
// Unused when FREE_MODE_ENABLED is 0 — kept here so the constant survives
// the flip.
static const uint32_t IDLE_TO_FREE_MS = 60000;

// IMU-triggered faces (surprised / angry / sad) auto-revert to neutral
// after this long, so the pet "calms down" instead of staying upset
// forever. Only takes effect if the face hasn't been changed by anything
// else in the meantime — a serial command or BOOT-button tap during the
// hold cancels the auto-revert.
static const uint32_t IMU_FACE_HOLD_MS = 5000;
static uint32_t imuFaceExpiryMs = 0;                   // 0 = no pending revert
static ExpressionId imuSetFace = ExpressionId::Count;  // what motion last set

// Shake escalation: a single shake gets `angry`. If the pet has been
// shaken SHAKE_CRY_COUNT times within SHAKE_CRY_WINDOW_MS, the latest
// one tips into `sad` instead — the pet has had enough. Ring buffer of
// the last few shake timestamps; entries older than the window are
// implicitly stale (we just compare against `now` when counting).
static const uint32_t SHAKE_CRY_WINDOW_MS = 60000;
static const uint8_t SHAKE_CRY_COUNT = 3;
static uint32_t recentShakesMs[SHAKE_CRY_COUNT] = {0};
static uint8_t recentShakesHead = 0;

// Water reminder: at the top of each of these (local) hours, the pet
// interrupts whatever it's doing to nag you to drink water — the reminder
// view (text + falling droplets) holds for WATER_REMINDER_HOLD_MS, then
// hands control back to the mode that was running. Needs the real-time
// clock (WiFi/NTP); if the clock never syncs, no reminder ever fires.
static const uint8_t WATER_REMINDER_HOURS[] = {8, 10, 12, 14, 16, 18, 20, 22};
static const uint32_t WATER_REMINDER_HOLD_MS = 5000;

// DEMO ONLY — set to 1 to fire the reminder ~6 s after the clock syncs,
// once, regardless of the hour, so the screen can be seen without waiting
// for a real reminder slot. Leave at 0 for normal (clock-hour) behaviour.
#define WATER_REMINDER_DEMO 0
#if WATER_REMINDER_DEMO
static bool waterDemoFired = false;
#endif
static int8_t lastWaterReminderHour = -1;  // hour we last fired for (de-dupe)
static uint32_t waterReminderExpiryMs = 0;  // 0 = reminder not showing
static Mode* waterReminderPrevMode = nullptr;  // mode to restore afterwards

// At the hours that are both weather and water slots (8am / 6pm) the two
// play back-to-back: weather first, then the water reminder. Set when the
// weather view starts at such an hour; consumed when its timer expires to
// chain straight into the water reminder instead of returning to the face.
static bool waterAfterWeather = false;

static bool isWaterReminderHour(int hour) {
  for (uint8_t i = 0; i < sizeof(WATER_REMINDER_HOURS); ++i) {
    if (WATER_REMINDER_HOURS[i] == hour) return true;
  }
  return false;
}

// Weather scene: at the top of each of these hours the pet shows the
// animated weather scene (Bengaluru) for WEATHER_HOLD_MS, then hands back.
// Shares the same one-timed-view-at-a-time machinery as the water reminder
// (timedViewExpiryMs / timedViewPrevMode below).
static const uint8_t WEATHER_HOURS[] = {8, 18};
static const uint32_t WEATHER_HOLD_MS = 10000;
static int8_t lastWeatherHour = -1;

// DEMO ONLY — set to 1 to show the weather scene ~once shortly after the
// clock syncs, ignoring the hour, so it can be seen without waiting for a
// scheduled slot. Leave at 0 for normal behaviour.
#define WEATHER_DEMO 0
#if WEATHER_DEMO
static bool weatherDemoFired = false;
#endif

static bool isWeatherHour(int hour) {
  for (uint8_t i = 0; i < sizeof(WEATHER_HOURS); ++i) {
    if (WEATHER_HOURS[i] == hour) return true;
  }
  return false;
}

// Hand off between modes via the Mode onExit/onEnter hooks.
static void setMode(Mode& mode) {
  if (currentMode == &mode) return;
  currentMode->onExit();
  currentMode = &mode;
  currentMode->onEnter(viewManager);
}

// Debounced BOOT-button (GPIO9) press detector. Returns true once on each
// release->press edge. Active-low: LOW means pressed.
static bool bootButtonPressed(uint32_t now) {
  static bool raw = false;
  static bool stable = false;
  static uint32_t changeMs = 0;
  bool reading = digitalRead(PIN_BTN_BOOT) == LOW;
  if (reading != raw) {
    raw = reading;
    changeMs = now;
  }
  if (now - changeMs >= 25 && reading != stable) {
    stable = reading;
    if (stable) return true;  // settled into "pressed"
  }
  return false;
}

void setup() {
  transport.begin(115200);
  renderer.init();

  // Boot splash: show "MUCHI" for ~2 s before the pet takes over. Uses the
  // display directly (the view system isn't driving yet at this point).
  display::message("MUCHI");
  delay(2000);

  buzzer::begin();

  // Start the WiFi/NTP clock (for the timed water reminder). Non-blocking:
  // syncs in the background over the next few loops; the pet boots and runs
  // regardless of whether it ever connects.
  clock_::begin();

  // Bring up the IMU on its bit-banged I2C bus. A false return just
  // means the MPU isn't connected — the rest of the pet boots fine and
  // imu::isReady() will keep the motion path skipped.
  if (imu::begin()) {
    motion::begin();
  }

  // The on-board BOOT button cycles expressions.
  pinMode(PIN_BTN_BOOT, INPUT_PULLUP);

  currentMode->onEnter(viewManager);
#if FREE_MODE_ENABLED
  transport.println("Tamagotchi ready (Free Mode). Send any command for Desktop Mode.");
#else
  transport.println("Tamagotchi ready (Desktop Mode — Free Mode disabled).");
#endif
}

void loop() {
  uint32_t now = millis();

  Command cmd;
  if (transport.poll(cmd)) {
    lastCmdMs = now;
    setMode(desktopMode);  // any command means a host is driving the pet
    currentMode->onCommand(cmd, viewManager);
  }

  // BOOT button: a tap cycles to the next expression (and, like a
  // command, hands control to Desktop Mode).
  if (bootButtonPressed(now)) {
    lastCmdMs = now;
    setMode(desktopMode);
    uint8_t next = (static_cast<uint8_t>(viewManager.face().expression()) + 1) % expressionCount();
    viewManager.face().setExpression(static_cast<ExpressionId>(next));
  }

  // IMU: pickup → anxious-looking `surprised`; shake → `angry` (the pet
  // is annoyed), escalating to `sad` (it actually cries) once shaken
  // SHAKE_CRY_COUNT times within SHAKE_CRY_WINDOW_MS. Acts like a BOOT-
  // button tap: hands control to Desktop Mode, resets the idle timer,
  // snaps to the face view so the reaction is always visible even if a
  // text/image view was up.
  if (imu::isReady()) {
    imu::Sample s;
    if (imu::read(s)) {
      motion::Event ev = motion::update(s, now);
      if (ev != motion::Event::None) {
        lastCmdMs = now;
        setMode(desktopMode);
        viewManager.setView(&viewManager.face());

        ExpressionId target;
        if (ev == motion::Event::Pickup) {
          target = ExpressionId::Surprised;
        } else {
          // Record this shake into the ring buffer of recent shakes,
          // then count how many of the last SHAKE_CRY_COUNT entries
          // fall inside the window. >= SHAKE_CRY_COUNT means the pet
          // has been mistreated enough to cry; otherwise it's just
          // annoyed.
          recentShakesMs[recentShakesHead] = now;
          recentShakesHead = (recentShakesHead + 1) % SHAKE_CRY_COUNT;
          uint8_t recent = 0;
          for (uint8_t i = 0; i < SHAKE_CRY_COUNT; ++i) {
            if (recentShakesMs[i] != 0 && (now - recentShakesMs[i]) <= SHAKE_CRY_WINDOW_MS) {
              ++recent;
            }
          }
          target = (recent >= SHAKE_CRY_COUNT) ? ExpressionId::Sad : ExpressionId::Angry;
        }

        viewManager.face().setExpression(target);
        // Schedule the calm-down: in IMU_FACE_HOLD_MS we'll revert to
        // neutral, unless something else has changed the face by then.
        imuSetFace = target;
        imuFaceExpiryMs = now + IMU_FACE_HOLD_MS;
      }
    }
  }

  // Calm-down timer: if motion set the face and the hold has elapsed,
  // hand control back. The `current face still matches` guard means a
  // serial command or BOOT-button tap during the hold cancels the
  // revert — we don't overwrite the user's choice.
  if (imuFaceExpiryMs != 0 && (int32_t)(now - imuFaceExpiryMs) >= 0) {
    imuFaceExpiryMs = 0;
    if (viewManager.face().expression() == imuSetFace) {
#if FREE_MODE_ENABLED
      // Hand back to Free Mode so the pet resumes living on its own
      // instead of freezing on neutral until the 60 s idle timer fires.
      setMode(freeMode);
#else
      viewManager.face().setExpression(ExpressionId::Neutral);
#endif
    }
    imuSetFace = ExpressionId::Count;
  }

#if FREE_MODE_ENABLED
  if (currentMode == &desktopMode && now - lastCmdMs >= IDLE_TO_FREE_MS) {
    setMode(freeMode);  // gone idle — let the pet live on its own
  }
#endif

  currentMode->update(now, viewManager);

  // Water reminder. Drive the clock, and at the top of each reminder hour
  // take over the screen with the droplet nag for WATER_REMINDER_HOLD_MS,
  // then restore the view the mode was showing. We let the mode run its
  // update() first (above) so it has a current view to hand back to.
  clock_::update(now);
  weather::update(now);  // drive any in-flight weather fetch

  // Prefetch weather once, as soon as the clock is ready, so the data is
  // already cached by the time a scheduled (or demo) weather view appears —
  // otherwise the view would open before a cold-start WiFi+HTTPS fetch
  // finishes and just show "Weather...". The module rate-limits refetches,
  // so later requestRefresh() calls from the view are cheap no-ops.
  static bool weatherPrefetched = false;
  if (!weatherPrefetched && clock_::isReady()) {
    weatherPrefetched = true;
    weather::requestRefresh();
  }

  // waterReminderExpiryMs / waterReminderPrevMode double as the single
  // "timed view" slot — only one scheduled takeover (water or weather)
  // shows at a time. While it's non-zero a takeover is on screen; we just
  // hold it until the timer elapses, then restore the face and let the
  // mode re-assert its own view.
  if (waterReminderExpiryMs != 0) {
    if ((int32_t)(now - waterReminderExpiryMs) >= 0) {
      if (waterAfterWeather) {
        // The weather scene just finished at a combined slot — chain
        // straight into the water reminder instead of returning to the face.
        waterAfterWeather = false;
        waterReminderExpiryMs = now + WATER_REMINDER_HOLD_MS;
        viewManager.setView(&viewManager.water());
      } else {
        waterReminderExpiryMs = 0;
        viewManager.setView(&viewManager.face());
        waterReminderPrevMode = nullptr;
      }
    }
#if WEATHER_DEMO
  } else if (weather::hasData() && !weatherDemoFired) {
    // DEMO: show the weather scene once the first fetch has landed (so the
    // scene is populated, not stuck on "Weather..."), then chain into the
    // water reminder back-to-back, ignoring the hour.
    weatherDemoFired = true;
    waterAfterWeather = true;
    waterReminderPrevMode = currentMode;
    waterReminderExpiryMs = now + WEATHER_HOLD_MS;
    viewManager.setView(&viewManager.weather());
#endif
#if WATER_REMINDER_DEMO
  } else if (clock_::isReady() && !waterDemoFired) {
    // DEMO: fire the water reminder once shortly after the clock comes up,
    // ignoring the hour.
    waterDemoFired = true;
    waterReminderPrevMode = currentMode;
    waterReminderExpiryMs = now + WATER_REMINDER_HOLD_MS;
    viewManager.setView(&viewManager.water());
#endif
#if !WEATHER_DEMO && !WATER_REMINDER_DEMO
  } else if (clock_::isReady()) {
    int hour = clock_::localHour();
    // At a weather hour (8am / 6pm) show the forecast first; if that hour is
    // also a water slot, the water reminder plays right after (chained when
    // the weather timer expires). Other hours just show water. Both de-dupe
    // per hour and re-arm once the hour moves on.
    if (isWeatherHour(hour) && hour != lastWeatherHour) {
      lastWeatherHour = hour;
      // Claim the water slot too so it doesn't also fire on its own — but
      // remember to chain it after the weather scene if this hour is one.
      if (isWaterReminderHour(hour)) {
        lastWaterReminderHour = hour;
        waterAfterWeather = true;
      }
      waterReminderPrevMode = currentMode;
      waterReminderExpiryMs = now + WEATHER_HOLD_MS;
      weather::requestRefresh();
      viewManager.setView(&viewManager.weather());
    } else if (isWaterReminderHour(hour) && hour != lastWaterReminderHour) {
      lastWaterReminderHour = hour;
      waterReminderPrevMode = currentMode;
      waterReminderExpiryMs = now + WATER_REMINDER_HOLD_MS;
      viewManager.setView(&viewManager.water());
    } else {
      // Re-arm each schedule once its hour has passed.
      if (!isWeatherHour(hour)) lastWeatherHour = -1;
      if (!isWaterReminderHour(hour)) lastWaterReminderHour = -1;
    }
#endif  // !WEATHER_DEMO && !WATER_REMINDER_DEMO
  }

  // Buzzer synced to the face. In Desktop Mode every expression change
  // jingles; Free Mode stays quieter and plays its own jingle only on
  // mood shifts, so it is not gated in here.
  ExpressionId expr = viewManager.face().expression();
  if (expr != jingledExpr) {
    jingledExpr = expr;
    if (currentMode == &desktopMode) {
      Jingle jingle = jingleFor(expr);
      buzzer::play(jingle.tones, jingle.count);
    }
  }
  buzzer::update(now);

  renderer.beginFrame();
  viewManager.tick(now, renderer);
  renderer.endFrame();
}
