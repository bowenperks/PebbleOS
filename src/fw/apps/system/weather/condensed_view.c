/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "condensed_view.h"
#include "weather_app_layout.h"
#include "applib/app_timer.h"
#include "applib/ui/animation.h"
#include "applib/ui/animation_interpolate.h"
#include "applib/ui/app_window_stack.h"
#include "pebble_compat.h"

// Singleton host for the "condensed view" (mirrors clock_face.c / forecast_list.c). The
// WeatherAppLayout is embedded here exactly as the original app embedded it in WeatherAppData; we
// just give it its own window.
typedef struct {
  Window *window;
  WeatherAppLayout layout;
  const WeatherLocationForecast *days;   // borrowed; owned by weather.c (app-lifetime)
  size_t num_days;
  int    current_day_index;
#if WEATHER_PLATFORM_TOUCH_COLOR
  int16_t touch_start_x, touch_start_y;  // Touchdown origin for swipe detection
  bool    touch_active;
#endif
} CondensedViewData;

static CondensedViewData *s_cond;

// SELECT expand-in latch: armed by weather.c before the push, consumed on the first appear frame.
static bool s_pending_select_in;

void condensed_view_arm_select_in(void) {
  s_pending_select_in = true;
}

// today = focused day's large icon; next = the day below (small icon). fin marker shows on the
// last day. Pointers are borrowed straight from days[] (the layout stores them without copying).
static void prv_seed_day(int i) {
  const WeatherLocationForecast *today = &s_cond->days[i];
  const WeatherLocationForecast *next  =
      (i + 1 < (int)s_cond->num_days) ? &s_cond->days[i + 1] : NULL;
  weather_app_layout_set_fin_allowed(&s_cond->layout,
                                     s_cond->num_days > 1 && i + 1 >= (int)s_cond->num_days);
  weather_app_layout_set_data(&s_cond->layout, today, next);
  weather_app_layout_set_down_arrow_visible(&s_cond->layout, i + 1 < (int)s_cond->num_days);
  if (today->location_name) {
    weather_app_layout_set_location(&s_cond->layout, today->location_name);
  }
}

// Step one day and play the layout's 220ms arc scroll. Bounded — no wrap. Rapid calls (from the
// hold-scroll timer) are fine: weather_app_layout_animate cancels the in-flight arc and restarts,
// so an accelerating hold fast-forwards through the days.
static void prv_navigate(bool is_down) {
  if (!s_cond) return;
  int i = s_cond->current_day_index;
  if (is_down) {
    if (i + 1 >= (int)s_cond->num_days) return;   // already at the last day
    i++;
  } else {
    if (i <= 0) return;                            // already at today
    i--;
  }
  s_cond->current_day_index = i;
  const WeatherLocationForecast *new_today = &s_cond->days[i];
  const WeatherLocationForecast *new_next  =
      (i + 1 < (int)s_cond->num_days) ? &s_cond->days[i + 1] : NULL;
  weather_app_layout_set_fin_allowed(&s_cond->layout,
                                     s_cond->num_days > 1 && i + 1 >= (int)s_cond->num_days);
  weather_app_layout_animate(&s_cond->layout, new_today, new_next, is_down);
}

static void prv_click_up_down(ClickRecognizerRef r, void *ctx) {
  if (!s_cond) return;
  prv_navigate(click_recognizer_get_button_id(r) == BUTTON_ID_DOWN);
}

static void prv_click_back(ClickRecognizerRef r, void *ctx) {
  window_stack_pop(true);   // back to the forecast list
}

// ---- Touch input (touch colour platforms) ----
#if WEATHER_PLATFORM_TOUCH_COLOR
#define SWIPE_THRESHOLD 20   // px; same tap/swipe split as the other screens

static void prv_touch_handler(const TouchEvent *event, void *context) {
  (void)context;
  if (!s_cond) return;
  if (event->type == TouchEvent_Touchdown) {
    s_cond->touch_start_x = event->x;
    s_cond->touch_start_y = event->y;
    s_cond->touch_active  = true;
  } else if (event->type == TouchEvent_Liftoff && s_cond->touch_active) {
    s_cond->touch_active = false;
    int16_t dx = event->x - s_cond->touch_start_x;
    int16_t dy = event->y - s_cond->touch_start_y;
    int16_t adx = dx < 0 ? -dx : dx;
    int16_t ady = dy < 0 ? -dy : dy;
    if (adx <= SWIPE_THRESHOLD && ady <= SWIPE_THRESHOLD) return;  // tap — no action
    if (ady >= adx) {
      prv_navigate(dy < 0);      // swipe up = next day (like DOWN), swipe down = previous
    } else if (dx > 0) {
      window_stack_pop(true);    // swipe right = BACK to the forecast list
    }
  }
}
#endif

// ---- Hold-to-scroll with acceleration (raw click subscriber + AppTimer) ----
// Ported verbatim from the original main screen (bowens ideal weather.c): a single tap steps one
// day; holding UP/DOWN kicks off a timer that steps repeatedly, shrinking the interval by 50ms each
// step (from 300ms down to a 90ms floor) so the scroll speeds up the longer it is held.
#define HOLD_INITIAL_MS  300
#define HOLD_MIN_MS       90

static AppTimer *s_hold_timer   = NULL;
static bool      s_hold_is_down = false;
static int       s_hold_repeat  = 0;

static void prv_hold_timer_cb(void *ctx) {
  s_hold_timer = NULL;
  if (!s_cond) return;
  prv_navigate(s_hold_is_down);
  s_hold_repeat++;
  int interval = HOLD_INITIAL_MS - s_hold_repeat * 50;
  if (interval < HOLD_MIN_MS) interval = HOLD_MIN_MS;
  s_hold_timer = app_timer_register(interval, prv_hold_timer_cb, NULL);
}

static void prv_raw_up_down(ButtonId btn, bool pressed) {
  if (!s_cond) return;
  if (pressed) {
    s_hold_is_down = (btn == BUTTON_ID_DOWN);
    s_hold_repeat  = 0;
    if (s_hold_timer) { app_timer_cancel(s_hold_timer); s_hold_timer = NULL; }
    s_hold_timer = app_timer_register(HOLD_INITIAL_MS, prv_hold_timer_cb, NULL);
  } else {
    if (s_hold_timer) { app_timer_cancel(s_hold_timer); s_hold_timer = NULL; }
  }
}

static void prv_raw_up_pressed(ClickRecognizerRef r, void *ctx)   { prv_raw_up_down(BUTTON_ID_UP,   true);  }
static void prv_raw_up_released(ClickRecognizerRef r, void *ctx)  { prv_raw_up_down(BUTTON_ID_UP,   false); }
static void prv_raw_down_pressed(ClickRecognizerRef r, void *ctx) { prv_raw_up_down(BUTTON_ID_DOWN, true);  }
static void prv_raw_down_released(ClickRecognizerRef r, void *ctx){ prv_raw_up_down(BUTTON_ID_DOWN, false); }

static void prv_click_provider(void *ctx) {
  // Single tap = one day; hold UP/DOWN = accelerating scroll (raw handlers drive the hold timer).
  window_single_click_subscribe(BUTTON_ID_UP,   prv_click_up_down);
  window_single_click_subscribe(BUTTON_ID_DOWN, prv_click_up_down);
  window_raw_click_subscribe(BUTTON_ID_UP,   prv_raw_up_pressed,   prv_raw_up_released,   NULL);
  window_raw_click_subscribe(BUTTON_ID_DOWN, prv_raw_down_pressed, prv_raw_down_released, NULL);
  window_single_click_subscribe(BUTTON_ID_BACK, prv_click_back);
}

// The location bar's HH:MM used to ride the (removed) no-op glow timer's 20Hz redraws; a
// once-a-minute tick is all it actually needs. Never overlaps clock_face's subscription —
// nothing stacks on top of the condensed window.
static void prv_minute_tick(struct tm *tick_time, TimeUnits units_changed) {
  (void)tick_time; (void)units_changed;
  if (s_cond) layer_mark_dirty(s_cond->layout.root_layer);
}

static void prv_window_load(Window *window) {
  if (!s_cond) return;
  GRect bounds = layer_get_bounds(window_get_root_layer(window));
  weather_app_layout_init(&s_cond->layout, &bounds);
  layer_add_child(window_get_root_layer(window), s_cond->layout.root_layer);
  prv_seed_day(s_cond->current_day_index);
  tick_timer_service_subscribe(MINUTE_UNIT, prv_minute_tick);
}

// SELECT expand: the whole condensed page slides in from the RIGHT (Timeline new-card entrance).
static Animation *s_slide_in_anim;

static void prv_slide_in_update(Animation *anim, AnimationProgress progress) {
  (void)anim;
  if (!s_cond) return;
  Layer *root = s_cond->layout.root_layer;
  const int w = layer_get_bounds(root).size.w;
  GRect f = layer_get_frame_by_value(root);
  f.origin.x = (int)interpolate_moook(progress, w, 0);   // slide in from the right (+w -> 0)
  layer_set_frame(root, f);
}

static void prv_slide_in_stopped(Animation *anim, bool finished, void *context) {
  (void)anim; (void)finished; (void)context;
  s_slide_in_anim = NULL;
  if (s_cond) {
    Layer *root = s_cond->layout.root_layer;
    GRect f = layer_get_frame_by_value(root);
    f.origin.x = 0;
    layer_set_frame(root, f);
    layer_mark_dirty(root);
  }
  animation_destroy(anim);
}

static const AnimationImplementation s_slide_in_impl = { .update = prv_slide_in_update };

static void prv_start_slide_in(void) {
  if (!s_cond || s_slide_in_anim) return;
  Layer *root = s_cond->layout.root_layer;
  const int w = layer_get_bounds(root).size.w;
  GRect f = layer_get_frame_by_value(root);
  f.origin.x = w;                 // start the whole page off the right edge
  layer_set_frame(root, f);
  s_slide_in_anim = animation_create();
  animation_set_implementation(s_slide_in_anim, &s_slide_in_impl);
  animation_set_duration(s_slide_in_anim, interpolate_moook_duration() / 2);   // 115ms (Timeline)
  animation_set_curve(s_slide_in_anim, AnimationCurveLinear);
  animation_set_handlers(s_slide_in_anim,
                         (AnimationHandlers){ .stopped = prv_slide_in_stopped }, NULL);
  animation_schedule(s_slide_in_anim);
}

static void prv_window_appear(Window *window) {
#if WEATHER_PLATFORM_TOUCH_COLOR
  if (s_cond) touch_service_subscribe(prv_touch_handler, s_cond);
#endif
  if (s_cond && s_pending_select_in) {
    s_pending_select_in = false;
    prv_start_slide_in();   // whole page slides in from the right
  }
}

static void prv_window_unload(Window *window) {
#if WEATHER_PLATFORM_TOUCH_COLOR
  touch_service_unsubscribe();
#endif
  tick_timer_service_unsubscribe();
  if (s_hold_timer) { app_timer_cancel(s_hold_timer); s_hold_timer = NULL; }
  // The slide-in is module-level: cancel it so a re-push within its 115ms can't find a stale
  // handle (which would skip the entrance) or animate the next instance's root layer.
  if (s_slide_in_anim) {
    Animation *a = s_slide_in_anim;
    s_slide_in_anim = NULL;
    animation_unschedule(a);   // fires .stopped, which destroys the animation
  }
  if (s_cond) {
    weather_app_layout_deinit(&s_cond->layout);   // cancels glow/icon/fin timers, frees layers+bitmaps
  }
  window_destroy(window);
  if (s_cond) {
    free(s_cond);
    s_cond = NULL;
  }
}

void condensed_view_push(const WeatherLocationForecast *days, size_t num_days, int start_day_index) {
  // Consume the entrance latch up front so NO early return (guard, alloc, window failure) can
  // leave it armed for an unrelated future push; re-armed below just before the push.
  const bool select_in = s_pending_select_in;
  s_pending_select_in = false;
  if (s_cond || !days || num_days == 0) return;
  s_cond = calloc(1, sizeof(CondensedViewData));
  if (!s_cond) return;
  s_cond->days = days;
  s_cond->num_days = num_days;
  s_cond->current_day_index =
      (start_day_index >= 0 && start_day_index < (int)num_days) ? start_day_index : 0;
  s_cond->window = window_create();
  if (!s_cond->window) {
    free(s_cond);
    s_cond = NULL;
    return;
  }
  window_set_background_color(s_cond->window, GColorWhite);
  window_set_window_handlers(s_cond->window, (WindowHandlers){
    .load   = prv_window_load,
    .appear = prv_window_appear,
    .unload = prv_window_unload,
  });
  window_set_click_config_provider(s_cond->window, prv_click_provider);
  s_pending_select_in = select_in;   // re-arm for prv_window_appear, which consumes it
  window_stack_push(s_cond->window, !select_in);  // armed expand IS the entrance -> no system slide
}

bool condensed_view_is_showing(void) {
  return s_cond && s_cond->window;
}

// Re-point the borrowed days array after a weather-event refresh (the array is rewritten in
// place and can SHRINK, e.g. a location change to a v3-only record) — without this the view
// keeps navigating over the previous city's stale day slots. No-op when not showing.
void condensed_view_update_data(const WeatherLocationForecast *days, size_t num_days) {
  if (!s_cond || !days || num_days == 0) return;
  s_cond->days = days;
  s_cond->num_days = num_days;
  if (s_cond->current_day_index >= (int)num_days) {
    s_cond->current_day_index = (int)num_days - 1;
  }
  prv_seed_day(s_cond->current_day_index);   // re-seed: the content may be a different city now
}

