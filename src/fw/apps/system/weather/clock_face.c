/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "clock_face.h"
#include "detail_face.h"
#include "weather.h"
#include "applib/ui/app_window_stack.h"
#include "weather_math.h"
#include "weather_types.h"
#include "pebble_compat.h"
#include <time.h>

// ---- Layout ----
#define LOCATION_BAR_H   18
#define LOCATION_BAR_Y   PBL_IF_ROUND_ELSE(18, 0)
#define LOCATION_BAR_TEXT_INSET PBL_IF_ROUND_ELSE(36, 0)
#define ROUND_LOCATION_BAR_TEXT_INSET 50
#define ROUND_BAR_DEPTH 58
#define ROUND_BAR_RADIUS 207
#define ROUND_BAR_Y_ADJUST -13
#define ROUND_TIME_TEXT_Y 2
#define ROUND_LOCATION_TEXT_Y 21
#define MAX_DAYS          7
// One bitmap slot per drawable WeatherType enum value (0..8).
#define NUM_TYPE_SLOTS   9

// Geometry — tuned so 25×25 icons at the orbit don't clip the screen or the bar.
#if PBL_DISPLAY_HEIGHT >= 200
  // Emery: 200×228 rect — oval to fill the screen
  #define ICON_ORBIT_RX   PBL_IF_ROUND_ELSE(100, 87)
  #define ICON_ORBIT_RY   PBL_IF_ROUND_ELSE(100, 94)
#else
  // Chalk: 180×180 round
  #define ICON_ORBIT_RX   63
  #define ICON_ORBIT_RY   63
#endif

#define ICON_SIZE         25   // Emery/native tiny weather bitmap size
// Round clock icons draw 1:1 from the 25x25 TINY PNGs (CLOCK ids now map to TINY),
// so the slot must be 25 to avoid the previous 50->30 crop. Emery uses ICON_SIZE.
#define CLOCK_ICON_SIZE   PBL_IF_ROUND_ELSE(25, ICON_SIZE)
#define SWIPE_THRESHOLD   20
#define CLOCK_INTRO_DURATION_MS 520
#define CLOCK_GLOW_TIMER_MS 50
#define CLOCK_GLOW_OUTER_R PBL_IF_ROUND_ELSE(42, 32)
// Today's centre shows the temperature; its glow ring is a smidge smaller than
// the other days' (which keep the full CLOCK_GLOW_OUTER_R).
#define CLOCK_GLOW_OUTER_R_TODAY PBL_IF_ROUND_ELSE(38, 28)
#define CLOCK_GLOW_IDLE_TIMEOUT_MS 5000
#define CLOCK_GLOW_IDLE_TICKS (CLOCK_GLOW_IDLE_TIMEOUT_MS / CLOCK_GLOW_TIMER_MS)

// ---- State ----
typedef struct {
  Window  *window;
  Layer   *canvas;
  WeatherLocationForecast days[MAX_DAYS];
  size_t   num_days;
  int      day_index;        // which day this clock represents (0 = today)
  uint8_t  hourly_types[24]; // WeatherType per hour 0-23
  bool     hourly_valid;     // true once hourly data received
  GBitmap *type_bitmaps[NUM_TYPE_SLOTS]; // one loaded bitmap per unique WeatherType
  GFont      bar_font;
  GFont      temp_font;
  GBitmap   *temp_text_bmp;    // captured once at full size; NULL = not yet captured
  GSize      temp_text_full_sz; // pixel dimensions of the captured bitmap
  int16_t    temp_text_cx;      // x pixel of the text visual centre within the bitmap
  int16_t    temp_text_cy;      // y pixel of the text visual centre within the bitmap
  AnimationProgress anim_progress; // intro animation progress (0 → ANIMATION_NORMALIZED_MAX)
  Animation        *intro_anim;
  bool     bar_consumed;  // true once the HUD bar has fully slid off-screen (never shows again)
  bool     skip_intro_animation;
  AppTimer *glow_timer;
  uint32_t glow_phase;
  uint16_t glow_idle_ticks;
  // ---- Exit animation (tap → detail screen) ----
  AnimationProgress exit_p;       // 0 → MAX while exit is playing
  Animation        *exit_anim;
  bool              exit_active;  // true while exit animation is in progress
  AppTimer         *exit_push_timer; // fires at 0ms to push detail face after exit done
  WeatherLocationForecast exit_forecast_copy; // copy of days[0] for use in timer callback
  char exit_location_buf[64];
  char exit_phrase_buf[64];
  // Return animation (clock re-appears after popping detail)
  AnimationProgress return_p;
  Animation        *return_anim;
  bool              return_active;
  bool              should_play_return_anim; // set just before pushing detail; cleared in appear
  AppTimer         *back_pop_timer; // deferred pop back to main screen
  // ---- Temperature reveal (tap) ----
  int8_t   hourly_temps[24]; // temperature per hour 0-23
  bool     hourly_temps_valid;
  bool     temps_shown;      // stable state: true once temps are fully revealed
  bool     reveal_to;        // animation target state (true=reveal, false=hide)
  bool     reveal_active;    // true while reveal/hide animation is in progress
  AnimationProgress reveal_p;
  Animation        *reveal_anim;
#if WEATHER_PLATFORM_TOUCH_COLOR
  int16_t  touch_start_x;
  int16_t  touch_start_y;
  bool     touch_active;
  bool     touch_started_during_intro;
#endif
} ClockFaceData;

static ClockFaceData *s_cf;
static ClockFaceWrapCallback s_wrap_callback;
static void *s_wrap_context;

// ---- Helpers ----

#define prv_bg_color_for_type weather_type_bg_color
#if defined(PBL_PLATFORM_GABBRO)
  #define prv_icon_res weather_type_icon_clock_resource
#else
  #define prv_icon_res weather_type_icon_tiny_resource
#endif

static void prv_ascii_uppercase(char *buf) {
  for (char *c = buf; *c; c++) {
    if (*c >= 'a' && *c <= 'z') {
      *c = (char)(*c - ('a' - 'A'));
    }
  }
}

// Uppercase 3-letter weekday abbreviation for the day `day_index` days from now
// (e.g. "MON"). Used as the clock-face centre label for non-today days.
static void prv_weekday_abbrev(int day_index, char *buf, size_t sz) {
  time_t target = time(NULL) + (time_t)day_index * 86400;
  struct tm *lt = localtime(&target);
  strftime(buf, sz, "%a", lt);
  prv_ascii_uppercase(buf);
}

// Load the tiny bitmap set once; the clock reuses these during animations.
static void prv_load_bitmaps(void) {
  if (!s_cf) return;
  for (int i = 0; i < NUM_TYPE_SLOTS; i++) {
    if (s_cf->type_bitmaps[i]) {
      gbitmap_destroy(s_cf->type_bitmaps[i]);
      s_cf->type_bitmaps[i] = NULL;
    }
  }
  for (int i = WeatherType_PartlyCloudy; i <= WeatherType_RainAndSnow; i++) {
    s_cf->type_bitmaps[i] =
        gbitmap_create_with_resource(prv_icon_res((WeatherType)i));
  }
  // Ensure the generic/unknown slot is always loaded as fallback.
  // Also load bitmaps for all hourly types — these may differ from daily.
}

static void prv_clock_glow_timer_callback(void *context) {
  (void)context;
  if (!s_cf || !s_cf->window || window_stack_get_top_window() != s_cf->window) {
    if (s_cf) s_cf->glow_timer = NULL;
    return;
  }

  if (s_cf->glow_idle_ticks >= CLOCK_GLOW_IDLE_TICKS + WEATHER_GLOW_WRAP_TICKS) {
    s_cf->glow_timer = NULL;
    return;
  }

  bool transition_busy =
      s_cf->anim_progress < ANIMATION_NORMALIZED_MAX ||
      s_cf->exit_active ||
      s_cf->return_active;

  if (!transition_busy) {
    if (s_cf->glow_idle_ticks < CLOCK_GLOW_IDLE_TICKS) {
      s_cf->glow_phase =
          (s_cf->glow_phase + (uint32_t)(TRIG_MAX_ANGLE / 80)) %
          (uint32_t)TRIG_MAX_ANGLE;
    } else {
      s_cf->glow_phase =
          (s_cf->glow_phase + (uint32_t)(TRIG_MAX_ANGLE / 6)) %
          (uint32_t)TRIG_MAX_ANGLE;
    }
    s_cf->glow_idle_ticks++;
  }

  if (s_cf->canvas) layer_mark_dirty(s_cf->canvas);
  s_cf->glow_timer = app_timer_register(CLOCK_GLOW_TIMER_MS,
                                        prv_clock_glow_timer_callback, NULL);
}

static void prv_start_clock_glow(void) {
  if (!s_cf || s_cf->glow_timer) return;
  s_cf->glow_timer = app_timer_register(CLOCK_GLOW_TIMER_MS,
                                        prv_clock_glow_timer_callback, NULL);
}

static void prv_note_clock_interaction(void) {
  if (!s_cf) return;
  s_cf->glow_idle_ticks = 0;
  if (s_cf->canvas) layer_mark_dirty(s_cf->canvas);
  prv_start_clock_glow();
}

// Return the absolute 24h hour for a clock-face position.
// Today (day 0): maps to the NEXT 12 hours from now:
//   position P → the upcoming hour h where h%12 == P%12.
//   At 10:00: pos 11→11, pos 12→12, pos 1→13, pos 2→14 ... pos 10→22.
// Other days: a fixed daytime window so the morning reads as AM:
//   pos 7→07, 8→08 ... 12→12, 1→13 ... 6→18 (07:00 through 18:00).
static int prv_abs_hour_for_pos(int pos_1_to_12) {
  if (s_cf && s_cf->day_index != 0) {
    return (pos_1_to_12 >= 7) ? pos_1_to_12 : pos_1_to_12 + 12;
  }
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  int cur_h = t->tm_hour;
  int target_mod = pos_1_to_12 % 12;  // pos 12→0, pos 1→1, ..., pos 11→11
  for (int i = 1; i <= 12; i++) {
    int h = (cur_h + i) % 24;
    if (h % 12 == target_mod) return h;
  }
  return pos_1_to_12 % 12; // unreachable
}

// Map a clock-face hour position (1-12) to the WeatherType for that hour.
// Uses real hourly data if available, otherwise falls back to daily forecast.
static WeatherType prv_type_for_pos(int pos_1_to_12) {
  if (!s_cf) return WeatherType_Unknown;
  int abs_hour = prv_abs_hour_for_pos(pos_1_to_12);
  if (s_cf->hourly_valid && abs_hour >= 0 && abs_hour < 24) {
    return (WeatherType)s_cf->hourly_types[abs_hour];
  }
  // Fallback: daily
  if (s_cf->num_days == 0) return WeatherType_Unknown;
  return s_cf->days[0].current_weather_type;
}

// Map a clock-face hour position (1-12) to the temperature for that hour.
// Returns true and fills *out_temp if hourly temperature data is available.
static bool prv_temp_for_pos(int pos_1_to_12, int *out_temp) {
  if (!s_cf || !s_cf->hourly_temps_valid) return false;
  int abs_hour = prv_abs_hour_for_pos(pos_1_to_12);
  if (abs_hour < 0 || abs_hour >= 24) return false;
  *out_temp = (int)s_cf->hourly_temps[abs_hour];
  return true;
}

// Per-position reveal progress (0 → ANIMATION_NORMALIZED_MAX) for the tap
// temperature reveal. Icons swap clockwise starting from the 7 o'clock position.
static int prv_reveal_rp(AnimationProgress reveal_t, int pos) {
  int rank = (pos - 7 + 12) % 12;  // clockwise rank: 7→0, 8→1, ... 6→11
  int32_t item_dur = ANIMATION_NORMALIZED_MAX * 45 / 100;
  int32_t stagger = (ANIMATION_NORMALIZED_MAX - item_dur) / 11;
  int32_t local = (int32_t)reveal_t - rank * stagger;
  if (local <= 0) return 0;
  int32_t rp = weather_scale_i32(local, ANIMATION_NORMALIZED_MAX, item_dur);
  if (rp > ANIMATION_NORMALIZED_MAX) rp = ANIMATION_NORMALIZED_MAX;
  return (int)rp;
}

// ---- Intro animation (expand from dot + clockwise spin) ----

static void prv_intro_update(Animation *anim, AnimationProgress progress) {
  if (s_cf) {
    if (progress > ANIMATION_NORMALIZED_MAX * 99 / 100) {
      progress = ANIMATION_NORMALIZED_MAX;
    }
    s_cf->anim_progress = progress;
    if (s_cf->canvas) layer_mark_dirty(s_cf->canvas);
  }
}
static const AnimationImplementation s_intro_impl = { .update = prv_intro_update };

// ---- Exit animation (clock → detail) ----
static void prv_exit_anim_push_callback(void *ctx);

static void prv_exit_update(Animation *anim, AnimationProgress progress) {
  if (!s_cf) return;
  s_cf->exit_p = progress;
  if (s_cf->canvas) layer_mark_dirty(s_cf->canvas);
  // When animation reaches the end, schedule a 0ms timer to push the detail face.
  // (Avoid pushing directly from the animation callback.)
  if (progress >= ANIMATION_NORMALIZED_MAX && !s_cf->exit_push_timer) {
    s_cf->exit_push_timer = app_timer_register(0, prv_exit_anim_push_callback, NULL);
  }
}
static const AnimationImplementation s_exit_impl = { .update = prv_exit_update };

// ---- Return animation (clock reappears after detail pop) ----
static void prv_return_update(Animation *anim, AnimationProgress progress) {
  if (!s_cf) return;
  s_cf->return_p = progress;
  if (progress >= ANIMATION_NORMALIZED_MAX) s_cf->return_active = false;
  if (s_cf->canvas) layer_mark_dirty(s_cf->canvas);
}
static const AnimationImplementation s_return_impl = { .update = prv_return_update };

static void prv_start_return_animation(void) {
  if (!s_cf) return;
  s_cf->return_active = true;
  s_cf->return_p = 0;
  if (s_cf->return_anim) {
    animation_unschedule(s_cf->return_anim);
    animation_destroy(s_cf->return_anim);
  }
  s_cf->return_anim = animation_create();
  animation_set_implementation(s_cf->return_anim, &s_return_impl);
  animation_set_duration(s_cf->return_anim, 150);
  animation_set_curve(s_cf->return_anim, AnimationCurveEaseOut);
  animation_schedule(s_cf->return_anim);
}

static void prv_exit_anim_push_callback(void *ctx) {
  if (!s_cf) return;
  s_cf->exit_push_timer = NULL;
  s_cf->should_play_return_anim = true; // clock will play return anim when detail is popped
  detail_face_push_animated(&s_cf->exit_forecast_copy);
}

static void prv_start_exit_animation(void) {
  if (!s_cf || s_cf->exit_active) return;
  // Snapshot the focused day's forecast so it survives a potential redraw cycle.
  int di = (s_cf->day_index >= 0 && (size_t)s_cf->day_index < s_cf->num_days)
           ? s_cf->day_index : 0;
  if (s_cf->num_days > 0) {
    s_cf->exit_forecast_copy = s_cf->days[di];
    
    // For non-today days, set up weekday animation
    if (di != 0) {
      // Get the full weekday name (e.g., "WEDNESDAY") for the centre animation.
      time_t target = time(NULL) + (time_t)di * 86400;
      struct tm *lt = localtime(&target);
      char full_name[32] = {0};

      strftime(full_name, sizeof(full_name), "%A", lt);
      prv_ascii_uppercase(full_name);

      // Keep the real location name for the detail screen's top label.
      if (s_cf->days[di].location_name) {
        strncpy(s_cf->exit_location_buf, s_cf->days[di].location_name,
                sizeof(s_cf->exit_location_buf) - 1);
        s_cf->exit_location_buf[sizeof(s_cf->exit_location_buf) - 1] = '\0';
        s_cf->exit_forecast_copy.location_name = s_cf->exit_location_buf;
      }

      // Schedule the weekday animation to activate before push
      detail_face_set_weekday_animation(full_name);
    } else {
      // Today: use normal location
      if (s_cf->days[di].location_name) {
        strncpy(s_cf->exit_location_buf, s_cf->days[di].location_name,
                sizeof(s_cf->exit_location_buf) - 1);
        s_cf->exit_forecast_copy.location_name = s_cf->exit_location_buf;
      }
    }
    
    if (s_cf->days[di].current_weather_phrase) {
      strncpy(s_cf->exit_phrase_buf, s_cf->days[di].current_weather_phrase,
              sizeof(s_cf->exit_phrase_buf) - 1);
      s_cf->exit_forecast_copy.current_weather_phrase = s_cf->exit_phrase_buf;
    }
  }
  s_cf->exit_active = true;
  s_cf->exit_p = 0;
  if (s_cf->exit_anim) {
    animation_unschedule(s_cf->exit_anim);
    animation_destroy(s_cf->exit_anim);
  }
  s_cf->exit_anim = animation_create();
  animation_set_implementation(s_cf->exit_anim, &s_exit_impl);
  animation_set_duration(s_cf->exit_anim, 250);
  animation_set_curve(s_cf->exit_anim, AnimationCurveEaseIn);
  animation_schedule(s_cf->exit_anim);
}

// ---- Temperature reveal animation (tap) ----
static void prv_reveal_update(Animation *anim, AnimationProgress progress) {
  if (!s_cf) return;
  s_cf->reveal_p = progress;
  if (progress >= ANIMATION_NORMALIZED_MAX) {
    s_cf->reveal_active = false;
    s_cf->temps_shown = s_cf->reveal_to;
  }
  if (s_cf->canvas) layer_mark_dirty(s_cf->canvas);
}
static const AnimationImplementation s_reveal_impl = { .update = prv_reveal_update };

// Toggle the per-hour temperature reveal. Each tap flips between showing the
// weather bitmaps and showing the temperature bubbles.
static void prv_toggle_temp_reveal(void) {
  if (!s_cf || !s_cf->hourly_temps_valid) return;
  if (s_cf->exit_active || s_cf->return_active) return;
  if (s_cf->reveal_active) return; // ignore taps until the current reveal/hide cycle finishes
  if (s_cf->anim_progress < ANIMATION_NORMALIZED_MAX) return; // wait for intro to finish
  s_cf->reveal_to = !s_cf->temps_shown;
  s_cf->reveal_active = true;
  s_cf->reveal_p = 0;
  if (s_cf->reveal_anim) {
    animation_unschedule(s_cf->reveal_anim);
    animation_destroy(s_cf->reveal_anim);
  }
  s_cf->reveal_anim = animation_create();
  animation_set_implementation(s_cf->reveal_anim, &s_reveal_impl);
  animation_set_duration(s_cf->reveal_anim, 700);
  animation_set_curve(s_cf->reveal_anim, AnimationCurveLinear);
  animation_schedule(s_cf->reveal_anim);
}

static void prv_play_animation(void) {
  if (!s_cf) return;
  if (s_cf->intro_anim) {
    animation_unschedule(s_cf->intro_anim);
    animation_destroy(s_cf->intro_anim);
    s_cf->intro_anim = NULL;
  }
  s_cf->anim_progress = 0;
  // Invalidate the cached text bitmap so it is re-captured on the first draw.
  if (s_cf->temp_text_bmp) {
    gbitmap_destroy(s_cf->temp_text_bmp);
    s_cf->temp_text_bmp = NULL;
  }
  s_cf->intro_anim = animation_create();
  animation_set_implementation(s_cf->intro_anim, &s_intro_impl);
  animation_set_duration(s_cf->intro_anim, CLOCK_INTRO_DURATION_MS);
  animation_set_curve(s_cf->intro_anim, AnimationCurveLinear);
  animation_schedule(s_cf->intro_anim);
}

// ---- Draw proc ----
static int32_t prv_clock_intro_motion_progress(AnimationProgress progress) {
  const int32_t max = ANIMATION_NORMALIZED_MAX;
  if ((int32_t)progress >= max) return max;

  const int32_t overshoot = max / 34;
  const int32_t rebound = max / 135;
  const int32_t hit = max * 78 / 100;
  const int32_t settle = max * 93 / 100;

  if ((int32_t)progress < hit) {
    return weather_scale_i32(progress, max + overshoot, hit);
  }

  if ((int32_t)progress < settle) {
    return max + overshoot -
        weather_scale_i32((int32_t)progress - hit, overshoot + rebound,
                          settle - hit);
  }

  return max - rebound +
      weather_scale_i32((int32_t)progress - settle, rebound, max - settle);
}

static int32_t prv_clock_center_scale_progress(AnimationProgress progress) {
  const int32_t max = ANIMATION_NORMALIZED_MAX;
  if ((int32_t)progress >= max) return max;

  const int32_t start = max * 44 / 100;
  const int32_t min_scale = max / 4;
  const int32_t overshoot = max / 14;
  const int32_t rebound = max / 100;
  const int32_t hit = max * 80 / 100;
  const int32_t settle = max * 94 / 100;

  if ((int32_t)progress <= start) return min_scale;

  if ((int32_t)progress < hit) {
    int32_t t = weather_scale_i32((int32_t)progress - start, max,
                                  hit - start);
    int32_t ease_out = max - weather_norm_square(max - t);
    return min_scale +
        weather_scale_i32(ease_out, max + overshoot - min_scale, max);
  }

  if ((int32_t)progress < settle) {
    return max + overshoot -
        weather_scale_i32((int32_t)progress - hit, overshoot + rebound,
                          settle - hit);
  }

  return max - rebound +
      weather_scale_i32((int32_t)progress - settle, rebound, max - settle);
}

static void prv_canvas_draw(Layer *layer, GContext *ctx) {
  if (!s_cf) return;
  GRect bounds = layer_get_bounds(layer);
  int W = bounds.size.w;
  int H = bounds.size.h;

  // White background — matches other screens
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  // Clock face centre — full screen, no bar offset
  int cx = W / 2;
  int cy = H / 2;

  // Return animation state
  bool return_active = s_cf->return_active;
  AnimationProgress return_p = s_cf->return_p;
  // Exit animation state
  bool exit_active = s_cf->exit_active;
  AnimationProgress exit_p = s_cf->exit_p;
  AnimationProgress p = s_cf->anim_progress;
  int32_t intro_p = prv_clock_intro_motion_progress(p);
  int32_t centre_scale_p = prv_clock_center_scale_progress(p);
  bool anim_done = (p >= ANIMATION_NORMALIZED_MAX);
  // Temperature reveal (tap) state — engaged once the intro has finished and the
  // temperatures are showing. Stays engaged through the exit animation so the
  // temperature bubbles fall/shrink away rather than flicking back to weather icons.
  bool reveal_engaged = s_cf->hourly_temps_valid && anim_done && !return_active &&
      (s_cf->temps_shown || s_cf->reveal_active);
  AnimationProgress reveal_t = 0;
  if (reveal_engaged) {
    reveal_t = s_cf->reveal_active
        ? (s_cf->reveal_to ? s_cf->reveal_p
                           : (ANIMATION_NORMALIZED_MAX - s_cf->reveal_p))
        : ANIMATION_NORMALIZED_MAX;
  }
  // Clockwise spin: icons start 90° counter-clockwise from target, sweep clockwise to land
  int32_t angle_offset = -(int32_t)(TRIG_MAX_ANGLE / 4)
      * (int32_t)(ANIMATION_NORMALIZED_MAX - intro_p) / (int32_t)ANIMATION_NORMALIZED_MAX;

  // Current wall time — used by the HUD exit bar and for highlighting the next upcoming hour.
  time_t now_t = time(NULL);
  struct tm *now_tm = localtime(&now_t);
  int cur_h24 = now_tm->tm_hour;
  int next_h24 = (cur_h24 + 1) % 24;  // the first upcoming hour to highlight

  // ---- Blue location bar — matches weather screen HUD exactly ----
  // Slides up off the top edge over the first 45% of the very first intro animation.
  // bar_consumed is set to true the moment the bar exits, and is never cleared,
  // so the bar can never appear again (no callbacks, no race conditions).
  if (!anim_done && !s_cf->bar_consumed) {
    const int32_t EXIT_END = ANIMATION_NORMALIZED_MAX * 45 / 100;
    if ((int32_t)p >= EXIT_END) {
      s_cf->bar_consumed = true;  // bar has fully exited; never draw it again
    } else {
      // EaseIn: t² — bar accelerates as it exits
      int32_t t = weather_scale_i32((int32_t)p, ANIMATION_NORMALIZED_MAX,
                                    EXIT_END);
      int32_t t2 = weather_norm_square(t);
      const int bar_exit_distance =
          PBL_IF_ROUND_ELSE(ROUND_BAR_DEPTH + ROUND_BAR_Y_ADJUST,
                            LOCATION_BAR_Y + LOCATION_BAR_H);
      int y_off = -(int)(bar_exit_distance * t2 / ANIMATION_NORMALIZED_MAX);
#if !PBL_ROUND
      int bar_top = LOCATION_BAR_Y + y_off;
#endif
      // Text exits independently: linear ramp over first 15% of animation
      // (faster and simpler than the bar's eased curve — no bounce possible).
      const int32_t TEXT_EXIT_END = ANIMATION_NORMALIZED_MAX * 15 / 100;
      int32_t text_t = (int32_t)p < TEXT_EXIT_END
          ? (int32_t)p * ANIMATION_NORMALIZED_MAX / TEXT_EXIT_END
          : ANIMATION_NORMALIZED_MAX;
      int text_top = PBL_IF_ROUND_ELSE(0, LOCATION_BAR_Y) -
          (int)(bar_exit_distance * text_t / ANIMATION_NORMALIZED_MAX);
      // Blue fill (clip to screen — only visible rows matter)
      graphics_context_set_fill_color(ctx, GColorVividCerulean);
#if PBL_ROUND
      const int cap_center_y = ROUND_BAR_DEPTH - ROUND_BAR_RADIUS +
                               ROUND_BAR_Y_ADJUST + y_off;
      graphics_fill_circle(ctx, GPoint(W / 2, cap_center_y), ROUND_BAR_RADIUS);

      GFont hud_font = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
      const int safe = ROUND_LOCATION_BAR_TEXT_INSET;
      graphics_context_set_text_color(ctx, GColorWhite);

      char city_name[48];
      city_name[0] = '\0';
      if (s_cf->num_days > 0 && s_cf->days[0].location_name) {
        const char *src = s_cf->days[0].location_name;
        size_t city_len = 0;
        while (src[city_len] && src[city_len] != ',' && city_len < sizeof(city_name) - 1) {
          city_name[city_len] = src[city_len];
          city_len++;
        }
        while (city_len > 0 && city_name[city_len - 1] == ' ') city_len--;
        city_name[city_len] = '\0';
      }

      char time_str[8];
      struct tm *t_tm = localtime(&now_t);
      if (clock_is_24h_style()) {
        strftime(time_str, sizeof(time_str), "%H:%M", t_tm);
      } else {
        strftime(time_str, sizeof(time_str), "%I:%M", t_tm);
        if (time_str[0] == '0') memmove(time_str, time_str + 1, sizeof(time_str) - 1);
      }
      graphics_draw_text(ctx, time_str, hud_font,
          GRect(safe, ROUND_TIME_TEXT_Y + text_top, W - (safe * 2), 19),
          GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
      if (city_name[0]) {
        graphics_draw_text(ctx, city_name, hud_font,
            GRect(safe, ROUND_LOCATION_TEXT_Y + text_top, W - (safe * 2), 19),
            GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
      }
#else
      graphics_fill_rect(ctx, GRect(0, bar_top, W, LOCATION_BAR_H), 0, GCornerNone);
      // Location name left, time right — identical to weather screen
      GFont hud_font = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
      const int safe = LOCATION_BAR_TEXT_INSET;
      graphics_context_set_text_color(ctx, GColorWhite);
      if (s_cf->num_days > 0 && s_cf->days[0].location_name) {
        graphics_draw_text(ctx, s_cf->days[0].location_name, hud_font,
            GRect(safe + 4, text_top + 1, W - (safe * 2) - 52,
                  LOCATION_BAR_H),
            GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
      }
      char time_str[8];
      struct tm *t_tm = localtime(&now_t);
      if (clock_is_24h_style()) {
        strftime(time_str, sizeof(time_str), "%H:%M", t_tm);
      } else {
        strftime(time_str, sizeof(time_str), "%I:%M", t_tm);
        if (time_str[0] == '0') memmove(time_str, time_str + 1, sizeof(time_str) - 1);
      }
      graphics_draw_text(ctx, time_str, hud_font,
          GRect(W - safe - 50, text_top + 1, 48, LOCATION_BAR_H),
          GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
#endif
    }
  }

  // ---- 12 weather icons at hour positions (oval orbit on Emery) ----
  // During exit animation the icons fall downward and shrink away.
  graphics_context_set_compositing_mode(ctx, GCompOpSet);
  int icon_xs[12];
  int icon_ys[12];
  GBitmap *icon_bmps[12];
  
  for (int pos = 1; pos <= 12; pos++) {
    int32_t angle = (int32_t)TRIG_MAX_ANGLE * pos / 12 + angle_offset;
    int nudge = PBL_IF_ROUND_ELSE(0, (pos == 3 ? 7 : pos == 9 ? 7 : 0));
    int orbit_rx = ((int)(ICON_ORBIT_RX - nudge) * (int)intro_p / (int)ANIMATION_NORMALIZED_MAX);
    int orbit_ry = ((int)ICON_ORBIT_RY * (int)intro_p / (int)ANIMATION_NORMALIZED_MAX);
    int ix = cx + (int)((int32_t)sin_lookup(angle) * orbit_rx / TRIG_MAX_RATIO);
    int iy = cy - (int)((int32_t)cos_lookup(angle) * orbit_ry / TRIG_MAX_RATIO);

    // Exit: fall down and shrink. Return: rise from below.
    if (exit_active) {
      int fall = weather_scale_i32(40, exit_p, ANIMATION_NORMALIZED_MAX);
      iy += fall;
    } else if (return_active) {
      // Icons start 20px below, rise to final position
      int rise = weather_scale_i32(20,
                                   ANIMATION_NORMALIZED_MAX -
                                       (int32_t)return_p,
                                   ANIMATION_NORMALIZED_MAX);
      iy += rise;
    }

    WeatherType wt = prv_type_for_pos(pos);
    int idx = (wt <= WeatherType_RainAndSnow) ? (int)wt : (int)WeatherType_Generic;

    int full_r = CLOCK_ICON_SIZE * 7 / 10;
    int circle_r;
    if (exit_active) {
      // Shrink from full to 0
      circle_r = weather_scale_i32(full_r,
                                   ANIMATION_NORMALIZED_MAX -
                                       (int32_t)exit_p,
                                   ANIMATION_NORMALIZED_MAX);
    } else {
      circle_r = anim_done ? full_r
          : 2 + (int)((full_r - 2) * (int)intro_p / (int)ANIMATION_NORMALIZED_MAX);
    }
    
    icon_xs[pos-1] = ix;
    icon_ys[pos-1] = iy;
    icon_bmps[pos-1] = s_cf->type_bitmaps[idx]
                   ? s_cf->type_bitmaps[idx]
                   : s_cf->type_bitmaps[WeatherType_Generic];

    if (reveal_engaged) {
      // Per-hour temperature reveal: each circle flips clockwise from a coloured
      // weather bubble into a white circle with a dithered grey outline + the temp.
      int rp = prv_reveal_rp(reveal_t, pos);
      // Pinch the circle as it flips: full size at the ends, half size at the
      // midpoint where the content swaps.
      int32_t mag = (rp <= ANIMATION_NORMALIZED_MAX / 2)
          ? (ANIMATION_NORMALIZED_MAX - 2 * rp)   // 1 → 0
          : (2 * rp - ANIMATION_NORMALIZED_MAX);  // 0 → 1
      int rr = full_r - weather_scale_i32(ANIMATION_NORMALIZED_MAX - mag,
                                          full_r / 2,
                                          ANIMATION_NORMALIZED_MAX);
      bool show_temp_side = (rp >= ANIMATION_NORMALIZED_MAX / 2);
      bool show_temp_text = (rp >= ANIMATION_NORMALIZED_MAX * 76 / 100);
      bool show_icon      = (rp <= ANIMATION_NORMALIZED_MAX * 24 / 100);

      // During the exit transition the temperature bubbles shrink to nothing
      // (matching the weather-icon exit) instead of reverting to weather bitmaps.
      if (exit_active) {
        rr = weather_scale_i32(full_r,
                               ANIMATION_NORMALIZED_MAX - (int32_t)exit_p,
                               ANIMATION_NORMALIZED_MAX);
        show_temp_side = true;
        show_icon = false;
        // Hide the number once the circle has shrunk too small to contain it.
        show_temp_text = (exit_p < ANIMATION_NORMALIZED_MAX * 35 / 100);
      }

      if (rr < 1) continue; // fully shrunk — nothing to draw

      graphics_context_set_compositing_mode(ctx, GCompOpAssign);
      if (show_temp_side) {
        graphics_context_set_fill_color(ctx, GColorWhite);
        graphics_fill_circle(ctx, GPoint(ix, iy), rr);
        // Soft dithered grey outline (single pixel) instead of a hard black ring.
        graphics_context_set_stroke_color(ctx, GColorLightGray);
        graphics_context_set_stroke_width(ctx, 1);
        graphics_draw_circle(ctx, GPoint(ix, iy), rr);
      } else {
        graphics_context_set_fill_color(ctx, prv_bg_color_for_type(wt));
        graphics_fill_circle(ctx, GPoint(ix, iy), rr);
      }

      // Content: weather bitmap before the flip, temperature after — hidden during
      // the pinch so it never pokes outside the shrinking circle.
      if (show_icon) {
        GBitmap *bmp = icon_bmps[pos-1];
        if (bmp) {
          graphics_context_set_compositing_mode(ctx, GCompOpSet);
          graphics_draw_bitmap_in_rect(ctx, bmp,
              GRect(ix - CLOCK_ICON_SIZE / 2, iy - CLOCK_ICON_SIZE / 2,
                    CLOCK_ICON_SIZE, CLOCK_ICON_SIZE));
        }
      } else if (show_temp_text) {
        int temp;
        if (prv_temp_for_pos(pos, &temp)) {
          // Centre the number in the circle; the degree sign hangs off to the
          // right without shifting the number's centring.
          char nbuf[6];
          snprintf(nbuf, sizeof(nbuf), "%d", temp);
          GFont tfont = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
          GSize nsz = graphics_text_layout_get_content_size(
              nbuf, tfont, GRect(0, 0, 40, 18),
              GTextOverflowModeFill, GTextAlignmentCenter);
          graphics_context_set_text_color(ctx, GColorBlack);
          graphics_draw_text(ctx, nbuf, tfont,
              GRect(ix - nsz.w / 2, iy - 11, nsz.w, 18),
              GTextOverflowModeFill, GTextAlignmentCenter, NULL);
          graphics_draw_text(ctx, "\xC2\xB0", tfont,
              GRect(ix + nsz.w / 2, iy - 13, 10, 18),
              GTextOverflowModeFill, GTextAlignmentLeft, NULL);
        }
      }
      continue; // reveal path draws both circle and content; skip the bitmap pass
    }

    if (circle_r < 1) continue; // fully shrunk — skip background circle

    graphics_context_set_compositing_mode(ctx, GCompOpAssign);
    graphics_context_set_fill_color(ctx, prv_bg_color_for_type(wt));
    graphics_fill_circle(ctx, GPoint(ix, iy), circle_r);
  }

  // Draw icon bitmaps — doing this in a separate pass minimizes framebuffer captures
  if (!reveal_engaged && (anim_done || p > ANIMATION_NORMALIZED_MAX / 2)) {
    if (return_active) {
      int32_t t = return_p;
      int32_t mt = ANIMATION_NORMALIZED_MAX - t;
      int squash = (int)(4 * t / ANIMATION_NORMALIZED_MAX * mt /
                         ANIMATION_NORMALIZED_MAX * 4); // max ~4px
      int dst_w = CLOCK_ICON_SIZE + squash;
      int dst_h = CLOCK_ICON_SIZE - squash;
      if (dst_w < 1) dst_w = 1;
      if (dst_h < 1) dst_h = 1;
      
      AnimationProgress dp = ANIMATION_NORMALIZED_MAX - (int32_t)return_p;
      int phase = 0;
      if      (dp > ANIMATION_NORMALIZED_MAX * 3 / 5) phase = 2;
      else if (dp > ANIMATION_NORMALIZED_MAX * 1 / 5) phase = 1;
      
      GBitmap *fb = graphics_capture_frame_buffer(ctx);
      if (fb) {
        GRect fbb = gbitmap_get_bounds(fb);
        int fb_h = fbb.size.h;
        int32_t sx_step = ((int32_t)CLOCK_ICON_SIZE << 16) / dst_w;
        int32_t sy_step = ((int32_t)CLOCK_ICON_SIZE << 16) / dst_h;
        
        for (int i = 0; i < 12; i++) {
          GBitmap *bmp = icon_bmps[i];
          if (!bmp) continue;
          int ix = icon_xs[i];
          int iy = icon_ys[i];
          int bx = ix - dst_w / 2;
          int by = iy - dst_h / 2;
          
          GBitmapFormat sfmt = gbitmap_get_format(bmp);
          GColor *spalette   = gbitmap_get_palette(bmp);
          uint8_t *sdata     = gbitmap_get_data(bmp);
          uint16_t sbpr      = gbitmap_get_bytes_per_row(bmp);
          
          int32_t sy_fp = sy_step >> 1;
          for (int dy = 0; dy < dst_h; dy++, sy_fp += sy_step) {
            int sy = sy_fp >> 16;
            if (sy >= CLOCK_ICON_SIZE) sy = CLOCK_ICON_SIZE - 1;
            int ay = by + dy;
            if (ay < 0 || ay >= fb_h) continue;
            GBitmapDataRowInfo ri = gbitmap_get_data_row_info(fb, (uint16_t)ay);
            uint8_t *srow = sdata + (uint32_t)sy * sbpr;
            
            int32_t sx_fp2 = sx_step >> 1;
            for (int dx = 0; dx < dst_w; dx++, sx_fp2 += sx_step) {
              if (phase > 0) {
                bool erase = (phase == 1) ? (((dx ^ dy) & 1) != 0) : ((dx & 1) || (dy & 1));
                if (erase) continue;
              }
              int ax = bx + dx;
              if (ax < (int)ri.min_x || ax > (int)ri.max_x) continue;
              int sx = sx_fp2 >> 16;
              if (sx >= CLOCK_ICON_SIZE) sx = CLOCK_ICON_SIZE - 1;
              
              uint8_t pixel;
              if (sfmt == GBitmapFormat8Bit || sfmt == GBitmapFormat8BitCircular) pixel = srow[sx];
              else if (sfmt == GBitmapFormat4BitPalette) pixel = spalette ? spalette[(sx & 1) ? (srow[sx >> 1] & 0xF) : (srow[sx >> 1] >> 4)].argb : 0;
              else if (sfmt == GBitmapFormat2BitPalette) pixel = spalette ? spalette[(srow[sx >> 2] >> (6 - ((sx & 3) << 1))) & 0x3].argb : 0;
              else if (sfmt == GBitmapFormat1BitPalette) pixel = spalette ? spalette[(srow[sx >> 3] >> (7 - (sx & 7))) & 0x1].argb : 0;
              else pixel = ((srow[sx >> 3] >> (7 - (sx & 7))) & 0x1) ? 0xFF : 0x00;

              if ((pixel >> 6) != 0) {
                ri.data[ax] = pixel;
              }
            }
          }
        }
        graphics_release_frame_buffer(ctx, fb);
      }
    } else {
      graphics_context_set_compositing_mode(ctx, GCompOpSet);
      for (int i = 0; i < 12; i++) {
        if (!icon_bmps[i]) continue;
        int bx = icon_xs[i] - CLOCK_ICON_SIZE / 2;
        int by = icon_ys[i] - CLOCK_ICON_SIZE / 2;
        graphics_draw_bitmap_in_rect(ctx, icon_bmps[i],
            GRect(bx, by, CLOCK_ICON_SIZE, CLOCK_ICON_SIZE));
      }
      if (exit_active) {
        AnimationProgress dp = exit_p;
        int phase = 0;
        if      (dp > ANIMATION_NORMALIZED_MAX * 3 / 5) phase = 2;
        else if (dp > ANIMATION_NORMALIZED_MAX * 1 / 5) phase = 1;
        if (phase > 0) {
          GBitmap *fb = graphics_capture_frame_buffer(ctx);
          if (fb) {
            GRect fbb = gbitmap_get_bounds(fb);
            int fb_h = fbb.size.h;
            for (int i = 0; i < 12; i++) {
              int bx = icon_xs[i] - CLOCK_ICON_SIZE / 2;
              int by = icon_ys[i] - CLOCK_ICON_SIZE / 2;
              for (int dy = 0; dy < CLOCK_ICON_SIZE; dy++) {
                int ay = by + dy;
                if (ay < 0 || ay >= fb_h) continue;
                GBitmapDataRowInfo ri = gbitmap_get_data_row_info(fb, (uint16_t)ay);
                for (int dx = 0; dx < CLOCK_ICON_SIZE; dx++) {
                  bool erase = (phase == 1) ? (((dx ^ dy) & 1) != 0) : ((dx & 1) || (dy & 1));
                  if (!erase) continue;
                  int ax = bx + dx;
                  if (ax < (int)ri.min_x || ax > (int)ri.max_x) continue;
                  ri.data[ax] = 0xFF;
                }
              }
            }
            graphics_release_frame_buffer(ctx, fb);
          }
        }
      }
    }
  }

  // ---- Centre: temperature (today) or weekday label (other days) ----
  // For today (day 0) the captured bitmap is the current temperature + degree.
  // For any other day it is the uppercase weekday abbreviation ("MON", "TUE"...).
  bool centre_is_temp = (s_cf->day_index == 0);
  bool centre_valid = centre_is_temp
      ? (s_cf->num_days > 0 &&
         s_cf->days[0].current_temp != WEATHER_SERVICE_LOCATION_FORECAST_UNKNOWN_TEMP)
      : true;
  if (centre_valid) {
    // ---- Lazy-capture the centre text into a bitmap once, then scale it each frame. ----
    // On the very first draw call after a new animation starts, render the text at full size
    // into an off-screen corner of the framebuffer, copy those pixels into a GBitmap, and
    // erase the staging area.  Every subsequent frame just scales that bitmap.
    if (!s_cf->temp_text_bmp && s_cf->bar_consumed) {
      // Render the centre text at full size into a staging rect.
      // The text is centered at (stg_cx, stg_cy) within the staging area so we
      // know exactly where the visual centre is and can use it as the scale pivot.
      char stg_buf[8];
      GFont centre_font;
      bool draw_degree;
      if (centre_is_temp) {
        snprintf(stg_buf, sizeof(stg_buf), "%d", s_cf->days[0].current_temp);
        centre_font = s_cf->temp_font;
        draw_degree = false;
      } else {
        prv_weekday_abbrev(s_cf->day_index, stg_buf, sizeof(stg_buf));
        centre_font = fonts_get_system_font(FONT_KEY_BITHAM_30_BLACK);
        draw_degree = false;
      }
      GSize nsz = graphics_text_layout_get_content_size(
          stg_buf, centre_font, GRect(0, 0, 100, 50),
          GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft);
      const int left_pad = 4;
      const int degree_w = 12;
      const int degree_gap = 1;
      const int right_pad = 4;
      const int stg_w  = left_pad + nsz.w +
          (draw_degree ? degree_gap + degree_w + right_pad : right_pad);
      const int stg_h  = nsz.h + 8;   // vertical padding
      const int stg_x  = PBL_IF_ROUND_ELSE((W - stg_w) / 2, 0);
      const int stg_y  = PBL_IF_ROUND_ELSE((H - stg_h) / 2, 0);
      const int stg_cx = left_pad + nsz.w / 2;
      const int stg_cy = stg_h / 2;
      const int font_top_pad = -4;
      int ty_s = stg_cy - nsz.h / 2 + font_top_pad;
      // Clear staging area so no icon or HUD pixels bleed into the capture.
      graphics_context_set_fill_color(ctx, GColorWhite);
      graphics_fill_rect(ctx, GRect(stg_x, stg_y, stg_w, stg_h), 0, GCornerNone);
      graphics_context_set_text_color(ctx, GColorBlack);
      // Number: centred horizontally at stg_cx
      graphics_draw_text(ctx, stg_buf, centre_font,
          GRect(stg_x + left_pad, stg_y + ty_s, nsz.w + 2, nsz.h),
          GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
      // Degree symbol: immediately right of number (today only)
      if (draw_degree) {
        GFont dg = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
        graphics_draw_text(ctx, "\xC2\xB0", dg,
            GRect(stg_x + left_pad + nsz.w + degree_gap,
                  stg_y + ty_s + 2, degree_w, 18),
            GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
      }
      // Capture those pixels into a GBitmap
      GBitmap *fb = graphics_capture_frame_buffer(ctx);
      if (fb) {
        GBitmap *bmp = gbitmap_create_blank(GSize(stg_w, stg_h), GBitmapFormat8Bit);
        if (bmp) {
          weather_capture_framebuffer_rect(fb, bmp, GRect(stg_x, stg_y, stg_w, stg_h), 0);
          s_cf->temp_text_bmp     = bmp;
          s_cf->temp_text_full_sz = GSize(stg_w, stg_h);
          s_cf->temp_text_cx      = (int16_t)stg_cx;
          s_cf->temp_text_cy      = (int16_t)(stg_cy - font_top_pad / 2);
        }
        graphics_release_frame_buffer(ctx, fb);
      }
    }

    if (s_cf->temp_text_bmp) {
      int src_w = s_cf->temp_text_full_sz.w;
      int src_h = s_cf->temp_text_full_sz.h;
      int dst_w, dst_h;
      if (exit_active) {
        // Grow from 100% → 200% over the exit animation (EaseIn)
        int32_t scale_num = ANIMATION_NORMALIZED_MAX + (int32_t)exit_p;
        dst_w = src_w * scale_num / ANIMATION_NORMALIZED_MAX;
        dst_h = src_h * scale_num / ANIMATION_NORMALIZED_MAX;
      } else if (anim_done) {
        dst_w = src_w; dst_h = src_h;
      } else {
        dst_w = src_w * centre_scale_p / ANIMATION_NORMALIZED_MAX;
        dst_h = src_h * centre_scale_p / ANIMATION_NORMALIZED_MAX;
      }
      if (dst_w < 1) dst_w = 1;
      if (dst_h < 1) dst_h = 1;
      if (centre_is_temp) {
        dst_w = dst_w * 11 / 10;
        dst_h = dst_h * 11 / 10;
        if (dst_w < 1) dst_w = 1;
        if (dst_h < 1) dst_h = 1;
      }
      // Weekday label (non-today) renders a touch smaller than today's temperature.
      // Today's centre is left exactly as-is.
      if (!centre_is_temp) {
        dst_w = dst_w * 8 / 10;
        dst_h = dst_h * 8 / 10;
        if (dst_w < 1) dst_w = 1;
        if (dst_h < 1) dst_h = 1;
      }

      // Use the recorded visual centre of the text as the scale pivot,
      // so the text remains centred on (cx, cy) at all sizes.
      int cx_scaled = s_cf->temp_text_cx * dst_w / src_w;
      int cy_scaled = s_cf->temp_text_cy * dst_h / src_h;
      // Swoop: arc the temperature down then back up as it grows.
      // Parabola peaks at midpoint: offset = 20 * 4 * t * (1-t)
      int swoop_y = 0;
      if (exit_active) {
        int32_t t  = (int32_t)exit_p;
        swoop_y = weather_scale_i32(20, weather_norm_bell(t),
                                    ANIMATION_NORMALIZED_MAX);
      }
      WeatherType centre_weather_type = WeatherType_Unknown;
      if (s_cf->num_days > 0) {
        int di = s_cf->day_index >= 0 && (size_t)s_cf->day_index < s_cf->num_days
                     ? s_cf->day_index
                     : 0;
        centre_weather_type = s_cf->days[di].current_weather_type;
      }
      bool steady_glow = anim_done && !exit_active && !return_active;
      if (steady_glow) {
        GColor glow_color = prv_bg_color_for_type(centre_weather_type);
        uint8_t idle_progress = s_cf->glow_idle_ticks >= CLOCK_GLOW_IDLE_TICKS
            ? (uint8_t)(s_cf->glow_idle_ticks - CLOCK_GLOW_IDLE_TICKS) : 0;
        int glow_outer_r = centre_is_temp ? CLOCK_GLOW_OUTER_R_TODAY
                                          : CLOCK_GLOW_OUTER_R;
        weather_draw_lava_ring(ctx, GPoint(cx, cy + swoop_y), glow_outer_r,
                               glow_color, s_cf->glow_phase, idle_progress);
        prv_start_clock_glow();
      }

      int abs_x = cx - cx_scaled;
      int abs_y = cy - cy_scaled + swoop_y;

      GBitmap *fb = graphics_capture_frame_buffer(ctx);
      if (fb) {
        GRect fbb = gbitmap_get_bounds(fb);
        int fb_h = fbb.size.h;
        uint8_t *sdata = gbitmap_get_data(s_cf->temp_text_bmp);
        uint16_t sbpr  = gbitmap_get_bytes_per_row(s_cf->temp_text_bmp);
        int32_t sx_step = ((int32_t)src_w << 16) / dst_w;
        int32_t sy_step = ((int32_t)src_h << 16) / dst_h;
        int32_t sy_fp = sy_step >> 1;
        for (int dy = 0; dy < dst_h; dy++, sy_fp += sy_step) {
          int sy = sy_fp >> 16;
          if (sy >= src_h) sy = src_h - 1;
          int ay = abs_y + dy;
          if (ay < 0 || ay >= fb_h) continue;
          GBitmapDataRowInfo ri = gbitmap_get_data_row_info(fb, (uint16_t)ay);
          uint8_t *srow = sdata + (uint32_t)sy * sbpr;
          int32_t sx_fp2 = sx_step >> 1;
          for (int dx = 0; dx < dst_w; dx++, sx_fp2 += sx_step) {
            int sx = sx_fp2 >> 16;
            if (sx >= src_w) sx = src_w - 1;
            int ax = abs_x + dx;
            if (ax < (int)ri.min_x || ax > (int)ri.max_x) continue;
            uint8_t pixel = srow[sx];
            // Skip white pixels (background) — GColor8 white = 0xFF
            if (pixel == 0xFF) continue;
            ri.data[ax] = pixel;
          }
        }
        graphics_release_frame_buffer(ctx, fb);
      }
    }
  }

  // ---- Hour number labels — fade in during final third of animation ----
  // Skip labels entirely during exit animation.
  if (exit_active) return;
  // Pebble has no alpha; simulate fade: invisible (white) → light gray → dark gray
  // Start appearing at 60% progress, reach full color at 100%.
  AnimationProgress fade_start = (AnimationProgress)(ANIMATION_NORMALIZED_MAX * 6 / 10);
  if (p < fade_start) return;
  GColor label_color;
  if (anim_done || p > (AnimationProgress)(ANIMATION_NORMALIZED_MAX * 85 / 100)) {
    label_color = GColorDarkGray;
  } else if (p > (AnimationProgress)(ANIMATION_NORMALIZED_MAX * 72 / 100)) {
    label_color = GColorDarkGray;   // second step — already dark
  } else {
    label_color = GColorLightGray;  // first step — light gray on white
  }
  GFont label_font = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
#if PBL_DISPLAY_HEIGHT >= 200
  #define LABEL_ORBIT_RX (ICON_ORBIT_RX - 42)
  #define LABEL_ORBIT_RY (ICON_ORBIT_RY - 42)
  #define DOT_ORBIT_RX  (ICON_ORBIT_RX - 27)
  #define DOT_ORBIT_RY  (ICON_ORBIT_RY - 27)
#else
  #define LABEL_ORBIT_RX (ICON_ORBIT_RX - 30)
  #define LABEL_ORBIT_RY (ICON_ORBIT_RY - 30)
  #define DOT_ORBIT_RX  (ICON_ORBIT_RX - 18)
  #define DOT_ORBIT_RY  (ICON_ORBIT_RY - 18)
#endif
  for (int pos = 1; pos <= 12; pos++) {
    int32_t angle = (int32_t)TRIG_MAX_ANGLE * pos / 12;
    // Label nudge: 3 and 9 o'clock pulled 5px inward (their text sits widest horizontally)
    int label_nudge = PBL_IF_ROUND_ELSE(0, (pos == 3 || pos == 9) ? 5 : 0);
    int lx = cx + (int)((int32_t)sin_lookup(angle) * (LABEL_ORBIT_RX - label_nudge) / TRIG_MAX_RATIO);
    int ly = cy - (int)((int32_t)cos_lookup(angle) * LABEL_ORBIT_RY / TRIG_MAX_RATIO);
    // Dot between number and icon — 3 and 9 o'clock nudged 6px inward
    int dot_nudge = PBL_IF_ROUND_ELSE(0, (pos == 3 || pos == 9) ? 6 : 0);
    int ddx = cx + (int)((int32_t)sin_lookup(angle) * (DOT_ORBIT_RX - dot_nudge) / TRIG_MAX_RATIO);
    int ddy = cy - (int)((int32_t)cos_lookup(angle) * (DOT_ORBIT_RY - dot_nudge) / TRIG_MAX_RATIO);
    // 24h hour number — next upcoming hour gets a blue bubble (and no dot).
    // Only today's clock highlights "the next hour" — for other days "now" isn't
    // on that day, so no hour is highlighted.
    int abs_h = prv_abs_hour_for_pos(pos);
    bool is_next = (s_cf->day_index == 0) && (abs_h == next_h24);
    if (!is_next) {
      graphics_context_set_fill_color(ctx, label_color);
      graphics_fill_circle(ctx, GPoint(ddx, ddy), 2);
    }
    char lbuf[4];
    snprintf(lbuf, sizeof(lbuf), "%d", abs_h);
    int lw   = (abs_h >= 10) ? 28 : 20;
    int loff = (abs_h >= 10) ? -14 : -10;
    GRect label_rect = GRect(lx + loff, ly - 12, lw, 18);
    GTextAlignment label_align = GTextAlignmentCenter;
    int pill_len = lw + 12;
    int pill_center_x = lx;
    int pill_center_y = ly + 1;

#if defined(PBL_PLATFORM_GABBRO)
    if (pos == 3 || pos == 9) {
      const int side_slot_w = 30;
      const int side_slot_inner_half = 14;
      if (pos == 3) {
        label_rect = GRect(lx - side_slot_inner_half, ly - 12,
                           side_slot_w, 18);
        label_align = GTextAlignmentLeft;
      } else {
        label_rect = GRect(lx + side_slot_inner_half - side_slot_w, ly - 12,
                           side_slot_w, 18);
        label_align = GTextAlignmentRight;
      }
      pill_len = side_slot_w + 12;
      pill_center_x = label_rect.origin.x + label_rect.size.w / 2;
    }
#endif
    
    // The blue indicator fades in (scales up from a small dot) a few frames before
    // it settles, then stretches into the oval once the bitmaps have landed.
    int32_t pill_appear  = (ANIMATION_NORMALIZED_MAX * 70) / 100; // dot starts growing
    int32_t pill_settled = (ANIMATION_NORMALIZED_MAX * 78) / 100; // full circle, begins stretch
    bool show_pill = is_next && (anim_done || p > pill_appear);

    if (show_pill) {
      int r = 9;   // final radius => diameter 19
      int D = (pill_len - (r * 2)) / 2;
      
      int D_out = D - 5; // Pull the pill inwards 5px from the outside (the number-facing end)
      if (D_out < 0) D_out = 0;
      int D_in = D - 8; // Pull the pill inwards from the center of the watch face (less padding on the inside)
      if (D_in < 0) D_in = 0;
      
      if (!anim_done) {
        if (p < pill_settled) {
          // Grow the dot from a small radius up to full size while it fades in.
          int32_t grow_p = (p - pill_appear) * ANIMATION_NORMALIZED_MAX / (pill_settled - pill_appear);
          if (grow_p < 0) grow_p = 0;
          r = 2 + (int)((int32_t)(r - 2) * grow_p / ANIMATION_NORMALIZED_MAX);
          D_out = 0;
          D_in = 0;
        } else {
          // Stretch the full circle out into the oval.
          int32_t stretched_p = (p - pill_settled) * ANIMATION_NORMALIZED_MAX / (ANIMATION_NORMALIZED_MAX - pill_settled);
          D_out = (int)((int32_t)D_out * stretched_p / ANIMATION_NORMALIZED_MAX);
          D_in = (int)((int32_t)D_in * stretched_p / ANIMATION_NORMALIZED_MAX);
        }
      }
      
      int bcx = pill_center_x;
      int bcy = pill_center_y;
      
      int c1x = bcx + (int)((int32_t)sin_lookup(angle) * D_out / TRIG_MAX_RATIO);
      int c1y = bcy - (int)((int32_t)cos_lookup(angle) * D_out / TRIG_MAX_RATIO);
      int c2x = bcx - (int)((int32_t)sin_lookup(angle) * D_in / TRIG_MAX_RATIO);
      int c2y = bcy + (int)((int32_t)cos_lookup(angle) * D_in / TRIG_MAX_RATIO);
      
      graphics_context_set_fill_color(ctx, GColorVividCerulean);
      graphics_context_set_stroke_color(ctx, GColorVividCerulean);
      graphics_context_set_stroke_width(ctx, r * 2 + 1);
      
      graphics_fill_circle(ctx, GPoint(c1x, c1y), r);
      graphics_fill_circle(ctx, GPoint(c2x, c2y), r);
      graphics_draw_line(ctx, GPoint(c1x, c1y), GPoint(c2x, c2y));
      
      graphics_context_set_stroke_width(ctx, 1);
      graphics_context_set_text_color(ctx, GColorWhite);
    } else {
      graphics_context_set_text_color(ctx, label_color);
    }
    graphics_draw_text(ctx, lbuf, label_font, label_rect,
        GTextOverflowModeTrailingEllipsis, label_align, NULL);
  }
}

// ---- Tick handler — redraws every minute to keep hands + time current ----
static void prv_tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  if (s_cf && s_cf->canvas) layer_mark_dirty(s_cf->canvas);
}

// The deferred back-pop machinery was removed with the carousel rewrite: BACK now exits the
// app and ring hops go through weather_carousel_navigate(). The back_pop_timer field stays
// (harmlessly NULL) so the unused unload cleanup keeps compiling.

// ---- Touch input (touch colour platforms) ----
#if WEATHER_PLATFORM_TOUCH_COLOR
static bool prv_handle_clock_swipe(int16_t dx, int16_t dy) {
  int16_t adx = dx < 0 ? -dx : dx;
  int16_t ady = dy < 0 ? -dy : dy;
  if (dy < -SWIPE_THRESHOLD && ady > adx) {
    if (s_cf->num_days > 0 && !s_cf->exit_active) {
      prv_start_exit_animation();
    }
    return true;
  }
  if (dx < -SWIPE_THRESHOLD && adx > ady) {
    weather_carousel_navigate(+1);   // swipe left = next ring page (globe)
    return true;
  }
  if (dx > SWIPE_THRESHOLD && adx > ady) {
    weather_carousel_navigate(-1);   // swipe right = previous ring page (7-day list)
    return true;
  }
  return false;
}

static void prv_touch_handler(const TouchEvent *event, void *context) {
  (void)context;
  if (!s_cf) return;

  if (event->type == TouchEvent_Touchdown) {
    s_cf->touch_start_x = event->x;
    s_cf->touch_start_y = event->y;
    s_cf->touch_active  = true;
    s_cf->touch_started_during_intro =
        s_cf->anim_progress < ANIMATION_NORMALIZED_MAX;
  } else if (event->type == TouchEvent_Liftoff && s_cf->touch_active) {
    s_cf->touch_active = false;
    if (s_cf->touch_started_during_intro) {
      s_cf->touch_started_during_intro = false;
      prv_note_clock_interaction();
      return;
    }

    int16_t dx = event->x - s_cf->touch_start_x;
    int16_t dy = event->y - s_cf->touch_start_y;
    if (prv_handle_clock_swipe(dx, dy)) return;

    int16_t adx = dx < 0 ? -dx : dx;
    int16_t ady = dy < 0 ? -dy : dy;
    if (adx <= SWIPE_THRESHOLD && ady <= SWIPE_THRESHOLD) {
      prv_toggle_temp_reveal();
    }
    prv_note_clock_interaction();
  }
}
#endif

// ---- Button input ----
static void prv_click_back(ClickRecognizerRef r, void *ctx) {
  prv_note_clock_interaction();
  app_window_stack_pop_all(true);  // BACK exits the app from any ring page
}
static void prv_click_select(ClickRecognizerRef r, void *ctx) {
  // Single click — reveal the per-hour temperatures (mirrors the touch tap).
  prv_note_clock_interaction();
  prv_toggle_temp_reveal();
}
static void prv_click_down(ClickRecognizerRef r, void *ctx) {
  // Down — go to the UV/detail screen (mirrors the touch swipe-up).
  prv_note_clock_interaction();
  if (s_cf && s_cf->num_days > 0 && !s_cf->exit_active)
    prv_start_exit_animation();
}
static void prv_click_up(ClickRecognizerRef r, void *ctx) {
  prv_note_clock_interaction();  // no ring hop on a button — swipes drive the carousel
}
static void prv_click_provider(void *ctx) {
  window_single_click_subscribe(BUTTON_ID_BACK,   prv_click_back);
  window_single_click_subscribe(BUTTON_ID_UP,     prv_click_up);
  window_single_click_subscribe(BUTTON_ID_SELECT, prv_click_select);
  window_single_click_subscribe(BUTTON_ID_DOWN,   prv_click_down);
}

// ---- Window lifecycle ----
static void prv_window_load(Window *window) {
  window_set_background_color(window, GColorWhite);
  GRect bounds = layer_get_bounds(window_get_root_layer(window));
  s_cf->canvas = layer_create(bounds);
  layer_set_update_proc(s_cf->canvas, prv_canvas_draw);
  layer_add_child(window_get_root_layer(window), s_cf->canvas);

  s_cf->bar_font = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
#if PBL_DISPLAY_HEIGHT >= 200
  s_cf->temp_font = fonts_get_system_font(FONT_KEY_LECO_26_BOLD_NUMBERS_AM_PM);
#else
  s_cf->temp_font = fonts_get_system_font(FONT_KEY_LECO_26_BOLD_NUMBERS_AM_PM);
#endif

  prv_load_bitmaps();
  tick_timer_service_subscribe(MINUTE_UNIT, prv_tick_handler);

  if (s_cf->skip_intro_animation) {
    s_cf->anim_progress = ANIMATION_NORMALIZED_MAX;
    s_cf->bar_consumed = true;
    if (s_cf->canvas) layer_mark_dirty(s_cf->canvas);
  } else {
    // Schedule intro animation: bloom + clockwise spin into position
    prv_play_animation();
  }
}

static void prv_window_appear(Window *window) {
  if (!s_cf) return;
  // Reset exit state so the clock renders normally when returning from detail_face.
  s_cf->exit_active = false;
  s_cf->exit_p = 0;
  if (s_cf->exit_anim) {
    animation_unschedule(s_cf->exit_anim);
    animation_destroy(s_cf->exit_anim);
    s_cf->exit_anim = NULL;
  }
  if (s_cf->exit_push_timer) {
    app_timer_cancel(s_cf->exit_push_timer);
    s_cf->exit_push_timer = NULL;
  }
  // Reset the temperature reveal so the clock shows weather icons on return.
  s_cf->reveal_active = false;
  s_cf->temps_shown = false;
  s_cf->reveal_p = 0;
  if (s_cf->reveal_anim) {
    animation_unschedule(s_cf->reveal_anim);
    animation_destroy(s_cf->reveal_anim);
    s_cf->reveal_anim = NULL;
  }
  if (s_cf->canvas) layer_mark_dirty(s_cf->canvas);
#if WEATHER_PLATFORM_TOUCH_COLOR
  touch_service_subscribe(prv_touch_handler, s_cf);
#endif
  prv_note_clock_interaction();
  // Only play the return (rise-from-below) animation when genuinely returning from detail_face.
  // On first push the intro animation handles the entrance; don't clobber it.
  if (s_cf->should_play_return_anim) {
    s_cf->should_play_return_anim = false;
    prv_start_return_animation();
  }
}

static void prv_window_unload(Window *window) {
  if (s_cf->glow_timer) {
    app_timer_cancel(s_cf->glow_timer);
    s_cf->glow_timer = NULL;
  }
  if (s_cf->intro_anim) {
    animation_unschedule(s_cf->intro_anim);
    animation_destroy(s_cf->intro_anim);
    s_cf->intro_anim = NULL;
  }
  if (s_cf->exit_anim) {
    animation_unschedule(s_cf->exit_anim);
    animation_destroy(s_cf->exit_anim);
    s_cf->exit_anim = NULL;
  }
  if (s_cf->return_anim) {
    animation_unschedule(s_cf->return_anim);
    animation_destroy(s_cf->return_anim);
    s_cf->return_anim = NULL;
  }
  if (s_cf->reveal_anim) {
    animation_unschedule(s_cf->reveal_anim);
    animation_destroy(s_cf->reveal_anim);
    s_cf->reveal_anim = NULL;
  }
  if (s_cf->back_pop_timer) {
    app_timer_cancel(s_cf->back_pop_timer);
    s_cf->back_pop_timer = NULL;
  }
  if (s_cf->exit_push_timer) {
    app_timer_cancel(s_cf->exit_push_timer);
    s_cf->exit_push_timer = NULL;
  }
  if (s_cf->temp_text_bmp) {
    gbitmap_destroy(s_cf->temp_text_bmp);
    s_cf->temp_text_bmp = NULL;
  }
  tick_timer_service_unsubscribe();

#if WEATHER_PLATFORM_TOUCH_COLOR
  touch_service_unsubscribe();
#endif

  for (int i = 0; i < NUM_TYPE_SLOTS; i++) {
    if (s_cf->type_bitmaps[i]) {
      gbitmap_destroy(s_cf->type_bitmaps[i]);
      s_cf->type_bitmaps[i] = NULL;
    }
  }
  layer_destroy(s_cf->canvas);
  s_cf->canvas = NULL;
  window_destroy(window);
  free(s_cf);
  s_cf = NULL;
}

// ---- Public API ----

void clock_face_set_wrap_callback(ClockFaceWrapCallback callback, void *context) {
  s_wrap_callback = callback;
  s_wrap_context = context;
}

bool clock_face_is_showing(void) {
  return s_cf != NULL;
}

static void prv_clock_face_push(const WeatherLocationForecast *days, size_t num_days,
                                int day_index, const uint8_t *hourly_types,
                                size_t hourly_count, bool animated, bool play_intro) {
  if (s_cf) return;

  s_cf = calloc(1, sizeof(ClockFaceData));
  if (!s_cf) return;

  size_t n = num_days < MAX_DAYS ? num_days : MAX_DAYS;
  s_cf->num_days = n;
  for (size_t i = 0; i < n; i++) s_cf->days[i] = days[i];
  s_cf->day_index = (day_index >= 0 && (size_t)day_index < n) ? day_index : 0;
  s_cf->skip_intro_animation = !play_intro;

  // Seed hourly data so it's available immediately on first draw.
  if (hourly_count > 0) {
    size_t hn = hourly_count < 24 ? hourly_count : 24;
    for (size_t i = 0; i < hn; i++) s_cf->hourly_types[i] = hourly_types[i];
    s_cf->hourly_valid = (hn >= 24);
  }

  s_cf->window = window_create();
  window_set_background_color(s_cf->window, GColorBlack);
  window_set_window_handlers(s_cf->window, (WindowHandlers){
    .load   = prv_window_load,
    .unload = prv_window_unload,
    .appear = prv_window_appear,
  });
  window_set_click_config_provider(s_cf->window, prv_click_provider);
  window_stack_push(s_cf->window, animated);
}

void clock_face_push(const WeatherLocationForecast *days, size_t num_days,
                     int day_index, const uint8_t *hourly_types, size_t hourly_count,
                     bool animated) {
  prv_clock_face_push(days, num_days, day_index, hourly_types, hourly_count,
                      animated, true);
}

void clock_face_push_static(const WeatherLocationForecast *days, size_t num_days,
                            int day_index, const uint8_t *hourly_types,
                            size_t hourly_count, bool animated) {
  prv_clock_face_push(days, num_days, day_index, hourly_types, hourly_count,
                      animated, false);
}

void clock_face_dismiss(bool animated) {
  if (!s_cf || !s_cf->window) return;
  window_stack_remove(s_cf->window, animated);
}

void clock_face_update_data(const WeatherLocationForecast *days, size_t num_days) {
  if (!s_cf) return;
  size_t n = num_days < MAX_DAYS ? num_days : MAX_DAYS;
  s_cf->num_days = n;
  for (size_t i = 0; i < n; i++) s_cf->days[i] = days[i];
  if (s_cf->canvas) {
    prv_load_bitmaps();
    layer_mark_dirty(s_cf->canvas);
  }
}

void clock_face_update_hourly_for_day(int day_index, const uint8_t *types, size_t count) {
  if (!s_cf || day_index != s_cf->day_index) return;
  size_t n = count < 24 ? count : 24;
  for (size_t i = 0; i < n; i++) s_cf->hourly_types[i] = types[i];
  s_cf->hourly_valid = (n >= 24);
  if (s_cf->canvas) {
    prv_load_bitmaps();
    layer_mark_dirty(s_cf->canvas);
  }
}

void clock_face_update_hourly_temps_for_day(int day_index, const int8_t *temps, size_t count) {
  if (!s_cf || day_index != s_cf->day_index) return;
  size_t n = count < 24 ? count : 24;
  for (size_t i = 0; i < n; i++) s_cf->hourly_temps[i] = temps[i];
  s_cf->hourly_temps_valid = (n >= 24);
  if (s_cf->canvas) layer_mark_dirty(s_cf->canvas);
}
