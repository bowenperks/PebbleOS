/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "weather_report.h"
#include "weather_math.h"
#include "applib/app_timer.h"
#include "applib/graphics/gdraw_command_transforms.h"
#include "applib/ui/animation.h"
#include "applib/ui/animation_interpolate.h"
#include "applib/ui/app_window_stack.h"
#include "pebble_compat.h"

// "The Weather Report" — the SELECT screen.
//
// RECT (emery/obelix): a static newspaper front page for today. Nameplate, dateline,
// serif weekday headline, condition deck, framed halftone "press photo", temperature
// column with a high-over-low fraction, boxed UV tally — all printed in black ink on
// dithered cream newsprint. No day paging yet (the 7-day edition comes later).
//
// ROUND (gabbro/getafix): the classic condensed layout host, verbatim — hold UP/DOWN
// scrolls the week through weather_app_layout. Unchanged behavior, renamed API.

// SELECT expand-in latch: armed by weather.c before the push, consumed on the first
// appear frame. Shared by both platform arms.
static bool s_pending_select_in;

void weather_report_arm_select_in(void) {
  s_pending_select_in = true;
}

// Hard-cut latch: the unfold transition scene already delivered the entrance, so the
// next push renders the full page on frame one (only the rect arm ever arms this).
static bool s_pending_static_in;

void weather_report_arm_static_in(void) {
  s_pending_static_in = true;
}

#if PBL_ROUND
// ============================================================================
// ROUND — classic condensed layout host (weather_app_layout), unchanged.
// ============================================================================

#include "weather_app_layout.h"

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
} WeatherReportData;

static WeatherReportData *s_report;

// today = focused day's large icon; next = the day below (small icon). fin marker shows on the
// last day. Pointers are borrowed straight from days[] (the layout stores them without copying).
static void prv_seed_day(int i) {
  const WeatherLocationForecast *today = &s_report->days[i];
  const WeatherLocationForecast *next  =
      (i + 1 < (int)s_report->num_days) ? &s_report->days[i + 1] : NULL;
  weather_app_layout_set_fin_allowed(&s_report->layout,
                                     s_report->num_days > 1 && i + 1 >= (int)s_report->num_days);
  weather_app_layout_set_data(&s_report->layout, today, next);
  weather_app_layout_set_down_arrow_visible(&s_report->layout, i + 1 < (int)s_report->num_days);
  if (today->location_name) {
    weather_app_layout_set_location(&s_report->layout, today->location_name);
  }
}

// Step one day and play the layout's 220ms arc scroll. Bounded — no wrap. Rapid calls (from the
// hold-scroll timer) are fine: weather_app_layout_animate cancels the in-flight arc and restarts,
// so an accelerating hold fast-forwards through the days.
static void prv_navigate(bool is_down) {
  if (!s_report) return;
  int i = s_report->current_day_index;
  if (is_down) {
    if (i + 1 >= (int)s_report->num_days) return;   // already at the last day
    i++;
  } else {
    if (i <= 0) return;                              // already at today
    i--;
  }
  s_report->current_day_index = i;
  const WeatherLocationForecast *new_today = &s_report->days[i];
  const WeatherLocationForecast *new_next  =
      (i + 1 < (int)s_report->num_days) ? &s_report->days[i + 1] : NULL;
  weather_app_layout_set_fin_allowed(&s_report->layout,
                                     s_report->num_days > 1 && i + 1 >= (int)s_report->num_days);
  weather_app_layout_animate(&s_report->layout, new_today, new_next, is_down);
}

static void prv_click_up_down(ClickRecognizerRef r, void *ctx) {
  if (!s_report) return;
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
  if (!s_report) return;
  if (event->type == TouchEvent_Touchdown) {
    s_report->touch_start_x = event->x;
    s_report->touch_start_y = event->y;
    s_report->touch_active  = true;
  } else if (event->type == TouchEvent_Liftoff && s_report->touch_active) {
    s_report->touch_active = false;
    int16_t dx = event->x - s_report->touch_start_x;
    int16_t dy = event->y - s_report->touch_start_y;
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
  if (!s_report) return;
  prv_navigate(s_hold_is_down);
  s_hold_repeat++;
  int interval = HOLD_INITIAL_MS - s_hold_repeat * 50;
  if (interval < HOLD_MIN_MS) interval = HOLD_MIN_MS;
  s_hold_timer = app_timer_register(interval, prv_hold_timer_cb, NULL);
}

static void prv_raw_up_down(ButtonId btn, bool pressed) {
  if (!s_report) return;
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

// The location bar's HH:MM needs a once-a-minute tick. Never overlaps clock_face's
// subscription — nothing stacks on top of this window.
static void prv_minute_tick(struct tm *tick_time, TimeUnits units_changed) {
  (void)tick_time; (void)units_changed;
  if (s_report) layer_mark_dirty(s_report->layout.root_layer);
}

static void prv_window_load(Window *window) {
  if (!s_report) return;
  GRect bounds = layer_get_bounds(window_get_root_layer(window));
  weather_app_layout_init(&s_report->layout, &bounds);
  layer_add_child(window_get_root_layer(window), s_report->layout.root_layer);
  prv_seed_day(s_report->current_day_index);
  tick_timer_service_subscribe(MINUTE_UNIT, prv_minute_tick);
}

// SELECT expand: the whole page slides in from the RIGHT (Timeline new-card entrance).
static Animation *s_slide_in_anim;

static void prv_slide_in_update(Animation *anim, AnimationProgress progress) {
  (void)anim;
  if (!s_report) return;
  Layer *root = s_report->layout.root_layer;
  const int w = layer_get_bounds(root).size.w;
  GRect f = layer_get_frame_by_value(root);
  f.origin.x = (int)interpolate_moook(progress, w, 0);   // slide in from the right (+w -> 0)
  layer_set_frame(root, f);
}

static void prv_slide_in_stopped(Animation *anim, bool finished, void *context) {
  (void)anim; (void)finished; (void)context;
  s_slide_in_anim = NULL;
  if (s_report) {
    Layer *root = s_report->layout.root_layer;
    GRect f = layer_get_frame_by_value(root);
    f.origin.x = 0;
    layer_set_frame(root, f);
    layer_mark_dirty(root);
  }
  animation_destroy(anim);
}

static const AnimationImplementation s_slide_in_impl = { .update = prv_slide_in_update };

static void prv_start_slide_in(void) {
  if (!s_report || s_slide_in_anim) return;
  Layer *root = s_report->layout.root_layer;
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
  if (s_report) touch_service_subscribe(prv_touch_handler, s_report);
#endif
  if (s_report && s_pending_select_in) {
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
  if (s_report) {
    weather_app_layout_deinit(&s_report->layout);   // cancels glow/icon/fin timers, frees layers+bitmaps
  }
  window_destroy(window);
  if (s_report) {
    free(s_report);
    s_report = NULL;
  }
}

void weather_report_push(const WeatherLocationForecast *days, size_t num_days, int start_day_index) {
  // Consume the entrance latch up front so NO early return (guard, alloc, window failure) can
  // leave it armed for an unrelated future push; re-armed below just before the push.
  const bool select_in = s_pending_select_in;
  s_pending_select_in = false;
  if (s_report || !days || num_days == 0) return;
  s_report = calloc(1, sizeof(WeatherReportData));
  if (!s_report) return;
  s_report->days = days;
  s_report->num_days = num_days;
  s_report->current_day_index =
      (start_day_index >= 0 && start_day_index < (int)num_days) ? start_day_index : 0;
  s_report->window = window_create();
  if (!s_report->window) {
    free(s_report);
    s_report = NULL;
    return;
  }
  window_set_background_color(s_report->window, GColorWhite);
  window_set_window_handlers(s_report->window, (WindowHandlers){
    .load   = prv_window_load,
    .appear = prv_window_appear,
    .unload = prv_window_unload,
  });
  window_set_click_config_provider(s_report->window, prv_click_provider);
  s_pending_select_in = select_in;   // re-arm for prv_window_appear, which consumes it
  window_stack_push(s_report->window, !select_in);  // armed expand IS the entrance -> no system slide
}

bool weather_report_is_showing(void) {
  return s_report && s_report->window;
}

// Re-point the borrowed days array after a weather-event refresh (the array is rewritten in
// place and can SHRINK, e.g. a location change to a v3-only record) — without this the view
// keeps navigating over the previous city's stale day slots. No-op when not showing.
void weather_report_update_data(const WeatherLocationForecast *days, size_t num_days) {
  if (!s_report || !days || num_days == 0) return;
  s_report->days = days;
  s_report->num_days = num_days;
  if (s_report->current_day_index >= (int)num_days) {
    s_report->current_day_index = (int)num_days - 1;
  }
  prv_seed_day(s_report->current_day_index);   // re-seed: the content may be a different city now
}

#else  // !PBL_ROUND
// ============================================================================
// RECT — "The Weather Report" newspaper front page.
// ============================================================================

// ---- Page grid (200x228, full bleed). The DAY is the masthead, per the sketch.
// Ink positions derive from measured font metrics: box_y + capTop = first ink row
// (DS28 capTop 8/capH 20; G18 7/11; G14 5/9; G09 3/6; LECO_38B digits top ~11,
// height ~27; LECO_20B ~7/~13).
#define WR_PAGE_W          200
#define WR_PAGE_H          228
#define WR_MARGIN_X        4      // rules inset
#define WR_TEXT_W          192

#define WR_DAY_Y           0      // GOTHIC_28_BOLD box -> caps ink y12..30 (the real
                                  // launcher/menu face; the G36 cut reads blocky, not Pebble)
#define WR_SCOTCH_THICK_Y  33     // Scotch rule under the title: 2px thick...
#define WR_SCOTCH_THIN_Y   37     // ...echoed by a 1px thin
#define WR_DECK_Y          34     // centered deck: line 1 (Timeline body voice, one line,
                                  // G28->G24B->G18B fit ladder) + alert line beneath

#define WR_PHOTO_X         8      // framed press photo (chunky 2px frame, white print)
#define WR_PHOTO_Y         81     // 62x62 -> y84..145 (pulled up under the deck — compact)
#define WR_PHOTO_W         62
#define WR_ICON_SIZE       52

// Right column (sketch layout): big temp top-right of the photo, the high/low box
// beside it, rain chance + wind as the column's second row.
#define WR_COL_X           80     // right column left edge
// The right-column group (temp+fraction ink y90.., row2 ink ..y139) is centered on the
// photo's vertical span (84..145): 6px pad above and below — measured, keep it so.
#define WR_TEMP_Y          76     // LECO_38B box -> digits ink y91..117
#define WR_HL_X            153    // high/low diagonal group (NO frame — user cut the box):
#define WR_HL_Y            78     // high top-left, slash, low bottom-right
#define WR_HL_S            44     // low right-railed to x192
#define WR_ROW2_TEXT_Y     121    // G14B rain/wind values -> ink y130..139

// UV sidebar, per the sketch: taller box, header (sun + "UV INDEX") top-left, tally
// squares centered on the box axis beneath, near-box-height numeral on the right.
#define WR_UV_X            8
#define WR_UV_Y            148    // 2px border box -> y152..191
#define WR_UV_W            184
#define WR_UV_H            40
#define WR_UV_SQ           10     // tally square size
#define WR_UV_SQ_PITCH     13
#define WR_UV_SLOTS        10     // a full 0-10 meter; filled = min(uv, 10), WHO color
#define WR_UV_SQ_X0        13     // left-aligned with the sun glyph's left edge

// Tomorrow footer — the old condensed view's bottom block, come home: its 2px divider,
// the label, tight °-temps, and the 25px satellite icon, one centered row.
#define WR_MORROW_RULE_Y   193
#define WR_MORROW_ICON_Y   199    // 25px tiny icon -> y202..226
#define WR_MORROW_TEXT_Y   199    // G18B boxes -> measured ink center y214 = icon center

// ---- 7-day paging ----------------------------------------------------------
// Day flip: the old condensed scroll's shape and speed (soft(3) moook @ 220ms).
#define WR_FLIP_MS       220
// Edge refusal: 4-frame moook-out decay, 6px amplitude.
#define WR_NUDGE_MS      132
// Fin ribbon (END_OF_TIMELINE, drawn UNSCALED): the 50x50 box's ink is a 47x22
// ribbon at x1..48, y12..33. Origin (75,191) puts ink at x76..123, y203..224
// -> optical center (99.5, 213.5) = the footer row's own axis (y214), h-centered.
#define WR_FIN_X          75
#define WR_FIN_Y         191
// Hold-to-scroll (round-arm contract, verbatim values):
#define HOLD_INITIAL_MS  300
#define HOLD_MIN_MS       90

typedef struct {
  Window *window;
  Layer  *page;
  const WeatherLocationForecast *days;   // borrowed; owned by weather.c (app-lifetime)
  size_t num_days;
  int    current_day_index;              // clamped [0,num_days); committed at navigate time
  Layer *ink[2];                         // transparent edition layers, children of page
  struct {
    int day;                             // day index this slot renders; -1 = empty/invalid
    GDrawCommandImage *photo;            // scaled photo-box PDC clone (owned)
    WeatherType photo_type;              // cache key — valid ONLY while photo != NULL
    GBitmap *morrow;                     // footer 25px tiny PNG (owned); NULL on last day
    WeatherType morrow_type;             // valid only while morrow != NULL
  } slot[2];
  int    front;                          // slot index showing the committed day
  GDrawCommandImage *fin_pdc;            // unscaled END_OF_TIMELINE clone (owned, load-once)
  Layer *fx;                             // topmost: entrance squash-in re-blit
  uint8_t *entrance_scratch;             // W*H snapshot for the squash-in; NULL when idle
  AnimationProgress entrance_p;
  GFont f_headline;   // GOTHIC_28_BOLD  weekday masthead (the launcher face — official Pebble)
  GFont f_body;       // GOTHIC_28  deck paragraph — the Timeline card's Body style on emery
  GFont f_body_medium; // GOTHIC_24_BOLD — the theme's medium-size Body, the fit fallback
  GFont f_lead;       // GOTHIC_18  "UV INDEX" label
  GFont f_stat;       // GOTHIC_14_BOLD  rain/wind values
  GFont f_temp;       // LECO_38_BOLD_NUMBERS  big temperature + big UV numeral
  GFont f_frac;       // GOTHIC_18_BOLD  bare high/low ("28/18") + deck line-1 size floor
#if WEATHER_PLATFORM_TOUCH_COLOR
  int16_t touch_start_x, touch_start_y;
  bool    touch_active;
#endif
} WeatherReportData;

static WeatherReportData *s_report;

// ---- Content helpers ----

static const char *const kWdayCaps[7] = {
  "SUNDAY", "MONDAY", "TUESDAY", "WEDNESDAY", "THURSDAY", "FRIDAY", "SATURDAY"
};

// Sentence case, the way phone-written Timeline bodies arrive ("Overcast, low of 14C...").
static void prv_sentence_copy(char *dst, size_t dst_size, const char *src) {
  if (!dst_size) return;
  size_t i = 0;
  for (; src && src[i] && i < dst_size - 1; i++) {
    char c = src[i];
    if (i == 0) {
      dst[i] = (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
    } else {
      dst[i] = (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
    }
  }
  dst[i] = '\0';
}

// The old condensed view's tight tomorrow temps, verbatim (weather_app_layout.c):
// "26°/15°", degrading per-side to "--°" when a bound is unknown.
static void prv_fill_high_low_tight_buffer(const int high, const int low, char *buffer,
                                           const size_t buffer_size) {
  if ((high == WEATHER_SERVICE_LOCATION_FORECAST_UNKNOWN_TEMP) &&
      (low == WEATHER_SERVICE_LOCATION_FORECAST_UNKNOWN_TEMP)) {
    snprintf(buffer, buffer_size, "--\xC2\xB0/--\xC2\xB0");
  } else if (low == WEATHER_SERVICE_LOCATION_FORECAST_UNKNOWN_TEMP) {
    snprintf(buffer, buffer_size, "%i\xC2\xB0/--\xC2\xB0", high);
  } else if (high == WEATHER_SERVICE_LOCATION_FORECAST_UNKNOWN_TEMP) {
    snprintf(buffer, buffer_size, "--\xC2\xB0/%i\xC2\xB0", low);
  } else {
    snprintf(buffer, buffer_size, "%i\xC2\xB0/%i\xC2\xB0", high, low);
  }
}

// What falls from the sky when it falls — for the "Chance of ..." sentence.
static const char *prv_precip_noun(WeatherType t) {
  switch (t) {
    case WeatherType_LightSnow:
    case WeatherType_HeavySnow:   return "snow";
    case WeatherType_RainAndSnow: return "sleet";
    default:                      return "rain";
  }
}

// "Chance of X"/"X likely" are only news when X is NOT already the headline — a
// light-rain day announcing "Chance of rain" reads like the paper didn't look outside.
static bool prv_precip_is_the_headline(WeatherType t) {
  return (t == WeatherType_LightRain) || (t == WeatherType_HeavyRain) ||
         (t == WeatherType_LightSnow) || (t == WeatherType_HeavySnow) ||
         (t == WeatherType_RainAndSnow);
}

// Deck line 1, in the Timeline card body's voice: "Light snow, feels like 21°."
// Future days have no feels-like — the line naturally degrades to the phrase alone.
static void prv_build_deck_line1(const WeatherLocationForecast *f, bool is_today,
                                 char *buf, size_t buf_size) {
  char phrase[36] = "";
  if (f->current_weather_phrase) {
    prv_sentence_copy(phrase, sizeof(phrase), f->current_weather_phrase);
  }
  const bool have_phrase = phrase[0] != '\0';
  const bool have_feels  = f->today_feels != WEATHER_SERVICE_LOCATION_FORECAST_UNKNOWN_TEMP;
  if (have_phrase && have_feels) {
    snprintf(buf, buf_size, "%s, feels like %d°", phrase, f->today_feels);
  } else if (have_phrase) {
    snprintf(buf, buf_size, "%s", phrase);
  } else if (have_feels) {
    snprintf(buf, buf_size, "Feels like %d°", f->today_feels);
  } else {
    snprintf(buf, buf_size, "%s", is_today ? "Today's weather" : "The week ahead");
  }
}

// Deck line 2: the day's weather WARNING, one short call, picked by severity —
// dangerous conditions first, then exposure, then the rain question, then a calm
// sign-off. The v4.2 raw readings (WMO code, humidity, min visibility, precip sum)
// unlock the storm/hail/fog/flood/humidity calls; on v4.0/4.1 records they are -1
// and those rungs simply never fire.
static void prv_build_alert(const WeatherLocationForecast *f, char *buf, size_t buf_size) {
  const WeatherType t = f->current_weather_type;
  const int wmo = f->today_wmo;   // WMO 4677 code, -1 unknown
  const bool feels_known = f->today_feels != WEATHER_SERVICE_LOCATION_FORECAST_UNKNOWN_TEMP;
  const bool temp_known  = f->current_temp != WEATHER_SERVICE_LOCATION_FORECAST_UNKNOWN_TEMP;
  const bool high_known  = f->today_high != WEATHER_SERVICE_LOCATION_FORECAST_UNKNOWN_TEMP;
  const bool low_known   = f->today_low != WEATHER_SERVICE_LOCATION_FORECAST_UNKNOWN_TEMP;
  const char *alert;
  char likely[20];
  if (wmo == 96 || wmo == 99) {                        // thunderstorm with hail
    alert = "Hail";
  } else if (wmo >= 95 && wmo <= 99) {                 // thunderstorm codes
    alert = "Chance of storms";
  } else if (t == WeatherType_HeavySnow || wmo == 75 || wmo == 86) {
    alert = "Heavy snow";
  } else if (t == WeatherType_HeavyRain) {
    alert = "Heavy rain";
  } else if (t == WeatherType_RainAndSnow) {
    alert = "Wintry mix";
  } else if (f->today_precip_sum_mm >= 30) {           // a real soaking on the day
    alert = "Flood risk";
  } else if (f->today_wind_mph >= 25) {
    alert = "Strong winds";
  } else if (high_known && f->today_high <= 0) {       // an ice day
    alert = "Below freezing";
  } else if (low_known && f->today_low <= -4) {
    alert = "Hard frost";
  } else if (feels_known && f->today_feels <= 0 &&
             temp_known && f->current_temp > 0) {      // wind chill crosses zero
    alert = "Feels below freezing";
  } else if (high_known && f->today_high >= 30) {
    alert = "Heatwave";
  } else if (f->today_uv >= 8) {
    alert = "Very high UV";
  } else if (f->today_uv >= 6) {
    alert = "High UV";
  } else if (wmo == 45 || wmo == 48 ||                 // fog / depositing rime fog
             (f->today_visibility_m >= 0 && f->today_visibility_m <= 1000)) {
    alert = "Poor visibility";
  } else if (f->today_humidity >= 85 && high_known && f->today_high >= 20) {
    alert = "High humidity";
  } else if (!prv_precip_is_the_headline(t) && f->today_precip_mm >= 60) {
    snprintf(likely, sizeof(likely), "%s likely", prv_precip_noun(t));
    likely[0] = (char)(likely[0] - 'a' + 'A');
    alert = likely;
  } else if (!prv_precip_is_the_headline(t) && f->today_precip_mm > 0) {
    snprintf(likely, sizeof(likely), "Chance of %s", prv_precip_noun(t));
    alert = likely;
  } else if (f->today_wind_mph >= 20) {
    alert = "Windy";
  } else {
    // No warning on file — a calm sign-off, deterministic per day's conditions.
    if (t == WeatherType_Sun) {
      alert = "Clear skies";
    } else if (f->today_wind_mph >= 10) {
      alert = "Light winds";
    } else if (f->today_wind_mph >= 0 && f->today_wind_mph <= 4) {
      alert = "Calm breeze";
    } else if (t == WeatherType_PartlyCloudy) {
      alert = "Fair conditions";
    } else if (temp_known && f->current_temp >= 12 && f->current_temp <= 24) {
      alert = "Mild conditions";
    } else {
      alert = "All clear";
    }
  }
  snprintf(buf, buf_size, "%s", alert);
}

// WHO UV-index severity color — the SAME ramp as the expanded card's UV dial
// (expanded_view.c prv_uv_severity_color), so every UV readout in the app tells
// the same story: 0-2 green, 3-5 yellow, 6-7 orange, 8-10 red, 11+ violet.
static GColor prv_uv_severity_color(int uv) {
  return (uv <= 2)  ? GColorIslamicGreen
       : (uv <= 5)  ? GColorChromeYellow
       : (uv <= 7)  ? GColorOrange
       : (uv <= 10) ? GColorRed
                    : GColorVividViolet;
}

// ---- Ink: glyphs drawn by hand (same vocabulary as the old metrics row) ----

static void prv_draw_raindrop_icon(GContext *ctx, GPoint origin) {
  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_fill_circle(ctx, GPoint(origin.x + 5, origin.y + 8), 4);
  graphics_draw_circle(ctx, GPoint(origin.x + 5, origin.y + 8), 4);
  graphics_fill_rect(ctx, GRect(origin.x + 3, origin.y + 5, 5, 4), 0, GCornerNone);
  graphics_draw_line(ctx, GPoint(origin.x + 5, origin.y), GPoint(origin.x + 9, origin.y + 6));
  graphics_draw_line(ctx, GPoint(origin.x + 5, origin.y), GPoint(origin.x + 1, origin.y + 6));
  graphics_draw_line(ctx, GPoint(origin.x + 1, origin.y + 6), GPoint(origin.x + 1, origin.y + 9));
  graphics_draw_line(ctx, GPoint(origin.x + 9, origin.y + 6), GPoint(origin.x + 9, origin.y + 9));
  // Fill the apex triangle: the two Bresenham edges alone leave a white notch at 1x.
  for (int dy = 1; dy <= 6; dy++) {
    int half = dy * 4 / 6;
    graphics_draw_line(ctx, GPoint(origin.x + 5 - half, origin.y + dy),
                       GPoint(origin.x + 5 + half, origin.y + dy));
  }
}

static void prv_draw_wind_icon(GContext *ctx, GPoint origin) {
  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_context_set_stroke_width(ctx, 2);
  // Decaying gust lengths (13/10/7) with a staggered leading edge.
  graphics_draw_line(ctx, GPoint(origin.x,     origin.y + 3),  GPoint(origin.x + 13, origin.y + 3));
  graphics_draw_line(ctx, GPoint(origin.x + 2, origin.y + 7),  GPoint(origin.x + 12, origin.y + 7));
  graphics_draw_line(ctx, GPoint(origin.x,     origin.y + 11), GPoint(origin.x + 7,  origin.y + 11));
  graphics_context_set_stroke_width(ctx, 1);
}

// Little radiant sun for the UV sidebar: core disc + 8 rays.
static void prv_draw_sun_glyph(GContext *ctx, GPoint c) {
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_fill_circle(ctx, c, 3);
  for (int i = 0; i < 8; i++) {
    int32_t a = TRIG_MAX_ANGLE * i / 8;
    int x0 = c.x + (int)(sin_lookup(a) * 5 / TRIG_MAX_RATIO);
    int y0 = c.y - (int)(cos_lookup(a) * 5 / TRIG_MAX_RATIO);
    int x1 = c.x + (int)(sin_lookup(a) * 7 / TRIG_MAX_RATIO);
    int y1 = c.y - (int)(cos_lookup(a) * 7 / TRIG_MAX_RATIO);
    graphics_draw_line(ctx, GPoint(x0, y0), GPoint(x1, y1));
  }
}

// ---- Newsprint: one framebuffer pass lays the cream paper (50% white/pastel-yellow
// checker — invisible weave at 228ppi, reads as newsprint). Pattern indices use LAYER
// coords so the paper rides with the page during the slide-in entrance.
static void prv_paper_pass(GContext *ctx, Layer *layer) {
  GBitmap *fb = graphics_capture_frame_buffer(ctx);
  if (!fb) return;
  const GRect fr = layer_get_frame_by_value(layer);
  const GRect pb = gbitmap_get_bounds(fb);
  for (int ly = 0; ly < fr.size.h; ly++) {
    const int sy = fr.origin.y + ly;
    if (sy < 0) continue;
    if (sy >= pb.size.h) break;
    const GBitmapDataRowInfo ri = gbitmap_get_data_row_info(fb, sy);
    for (int lx = 0; lx < fr.size.w; lx++) {
      const int sx = fr.origin.x + lx;
      if (sx < ri.min_x) continue;
      if (sx > ri.max_x) break;
      const bool odd = ((lx ^ ly) & 1) != 0;
      ri.data[sx] = odd ? GColorWhiteARGB8 : GColorPastelYellowARGB8;
    }
  }
  graphics_release_frame_buffer(ctx, fb);
}

// ---- The page ----
// The parent page layer draws ONLY the fixed cream paper; the two transparent ink
// children each render one day's edition. During a flip the ink slides over the
// stationary paper — the press stays put, the pages move.

static void prv_page_update_proc(Layer *layer, GContext *ctx) {
  if (!s_report) return;
  prv_paper_pass(ctx, layer);
}

// One day's full edition, drawn in layer-local coordinates. `si` = the slot whose
// cached icons belong to this edition.
static void prv_draw_edition(GContext *ctx, int si, int day) {
  const WeatherLocationForecast *f = &s_report->days[day];
  const int w = WR_PAGE_W;
  char buf[96];

  // Masthead: TODAY / TOMORROW for the first two pages, else the edition's weekday,
  // under a Scotch rule (thick echoed by thin). The weekday comes from RTC + day
  // offset — the same recipe as weather.c's labels, so masthead and footer agree.
  time_t now = rtc_get_time() + (time_t)day * SECONDS_PER_DAY;
  struct tm *lt = localtime(&now);
  const int wday = (lt && lt->tm_wday >= 0 && lt->tm_wday < 7) ? lt->tm_wday : 0;
  const char *masthead = (day == 0) ? "TODAY" : (day == 1) ? "TOMORROW" : kWdayCaps[wday];
  graphics_draw_text(ctx, masthead, s_report->f_headline,
                     GRect(0, WR_DAY_Y, w, 40),
                     GTextOverflowModeFill, GTextAlignmentCenter, NULL);
  graphics_fill_rect(ctx, GRect(WR_MARGIN_X, WR_SCOTCH_THICK_Y, WR_TEXT_W, 2), 0, GCornerNone);
  graphics_fill_rect(ctx, GRect(WR_MARGIN_X, WR_SCOTCH_THIN_Y, WR_TEXT_W, 1), 0, GCornerNone);

  // Deck, per the sketch: two CENTERED lines. Line 1 = "Condition, feels like N°" in
  // the Timeline body voice, sized down (G28 -> G24B -> G18B) until it fits one line.
  // Line 2 = the day's most pressing issue, smaller, directly beneath.
  char deck[64];
  prv_build_deck_line1(f, day == 0, deck, sizeof(deck));
  GFont deck_font = s_report->f_body;
  int line_h = 28;
  GSize sz = graphics_text_layout_get_content_size(
      deck, deck_font, GRect(0, 0, 400, 40), GTextOverflowModeFill, GTextAlignmentCenter);
  if (sz.w > WR_TEXT_W - 4) {
    deck_font = s_report->f_body_medium;
    line_h = 24;
    sz = graphics_text_layout_get_content_size(
        deck, deck_font, GRect(0, 0, 400, 40), GTextOverflowModeFill, GTextAlignmentCenter);
    if (sz.w > WR_TEXT_W - 4) {
      deck_font = s_report->f_frac;   // G18B floor
      line_h = 18;
    }
  }
  graphics_draw_text(ctx, deck, deck_font, GRect(WR_MARGIN_X, WR_DECK_Y, WR_TEXT_W, line_h + 8),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  prv_build_alert(f, deck, sizeof(deck));
  // On the G28 path the alert tucks 2px closer, so line 1's descenders and the photo
  // frame both keep clearance in the condensed grid.
  const int line2_y = WR_DECK_Y + (line_h == 28 ? 26 : line_h);
  graphics_draw_text(ctx, deck, s_report->f_lead,
                     GRect(WR_MARGIN_X, line2_y, WR_TEXT_W, 20),
                     GTextOverflowModeFill, GTextAlignmentCenter, NULL);
  // Pin dots (a dinkus) flanking the alert — print grammar for a short centered line.
  // Wide lines fill their measure and need no pinning.
  const GSize asz = graphics_text_layout_get_content_size(
      deck, s_report->f_lead, GRect(0, 0, 400, 20), GTextOverflowModeFill,
      GTextAlignmentCenter);
  if (asz.w > 0 && asz.w <= 150) {
    const int dot_y = line2_y + 12;   // the alert caps' optical center
    // Anchor on the renderer's own integer centering (box_x + (box_w - w)/2), not on
    // x=100 — with an odd remainder the ink sits a pixel off-center and dots pinned
    // to 100 read lopsided. Relative placement keeps both gaps equal by construction.
    const int tx0 = WR_MARGIN_X + (WR_TEXT_W - asz.w) / 2;
    graphics_fill_circle(ctx, GPoint(tx0 - 10, dot_y), 1);
    graphics_fill_circle(ctx, GPoint(tx0 + asz.w - 1 + 10, dot_y), 1);
  }

  // Press photo: chunky 2px frame; the print's backdrop is the condition's own disc
  // color from the old condensed screen (weather_type_bg_color — snow blue, sun
  // orange...), so the photo reads like the icon discs always did.
  graphics_fill_rect(ctx, GRect(WR_PHOTO_X, WR_PHOTO_Y, WR_PHOTO_W, WR_PHOTO_W), 0, GCornerNone);
  graphics_context_set_fill_color(ctx, weather_type_bg_color(f->current_weather_type));
  graphics_fill_rect(ctx, GRect(WR_PHOTO_X + 2, WR_PHOTO_Y + 2, WR_PHOTO_W - 4, WR_PHOTO_W - 4),
                     0, GCornerNone);
  if (s_report->slot[si].photo) {
    const int ix = WR_PHOTO_X + (WR_PHOTO_W - WR_ICON_SIZE) / 2;
    const int iy = WR_PHOTO_Y + (WR_PHOTO_W - WR_ICON_SIZE) / 2;
    gdraw_command_image_draw(ctx, s_report->slot[si].photo, GPoint(ix, iy));
  }
  // The PDC draw leaves the context's fill/stroke set to its last command's colors
  // (a white snow dot once turned the fraction bar invisible) — reset to ink.
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_context_set_stroke_color(ctx, GColorBlack);

  // The numbers desk: big current temperature, high-over-low fraction on the right rail.
  const bool temp_known = f->current_temp != WEATHER_SERVICE_LOCATION_FORECAST_UNKNOWN_TEMP;
  const int big_temp = temp_known ? f->current_temp : f->today_high;
  if (big_temp != WEATHER_SERVICE_LOCATION_FORECAST_UNKNOWN_TEMP) {
    snprintf(buf, sizeof(buf), "%d°", big_temp);
  } else {
    snprintf(buf, sizeof(buf), "--");
  }
  graphics_draw_text(ctx, buf, s_report->f_temp, GRect(WR_COL_X, WR_TEMP_Y, 76, 40),
                     GTextOverflowModeFill, GTextAlignmentLeft, NULL);

  // High/low, sketch style: high in the top-left, low in the bottom-right, a slash
  // between them — a bare diagonal fraction (the framed version was cut by request).
  char hi[8] = "--", lo[8] = "--";
  if (f->today_high != WEATHER_SERVICE_LOCATION_FORECAST_UNKNOWN_TEMP) {
    snprintf(hi, sizeof(hi), "%d", f->today_high);
  }
  if (f->today_low != WEATHER_SERVICE_LOCATION_FORECAST_UNKNOWN_TEMP) {
    snprintf(lo, sizeof(lo), "%d", f->today_low);
  }
  graphics_draw_text(ctx, hi, s_report->f_frac, GRect(WR_HL_X + 5, WR_HL_Y + 1, 26, 20),
                     GTextOverflowModeFill, GTextAlignmentLeft, NULL);
  graphics_draw_text(ctx, lo, s_report->f_frac,
                     GRect(WR_HL_X + WR_HL_S - 31, WR_HL_Y + WR_HL_S - 22, 26, 20),
                     GTextOverflowModeFill, GTextAlignmentRight, NULL);
  graphics_context_set_stroke_width(ctx, 2);
  graphics_draw_line(ctx, GPoint(WR_HL_X + 15, WR_HL_Y + WR_HL_S - 13),
                     GPoint(WR_HL_X + WR_HL_S - 15, WR_HL_Y + 13));
  graphics_context_set_stroke_width(ctx, 1);

  // Row 2 of the right column, per the sketch: rain chance then wind, under the temp,
  // to the right of the photo — the pair's bottom lines up with the photo frame.
  if (f->today_precip_mm >= 0) {
    prv_draw_raindrop_icon(ctx, GPoint(WR_COL_X, WR_ROW2_TEXT_Y + 4));
    snprintf(buf, sizeof(buf), "%d%%", f->today_precip_mm);
    graphics_draw_text(ctx, buf, s_report->f_stat, GRect(WR_COL_X + 15, WR_ROW2_TEXT_Y, 40, 16),
                       GTextOverflowModeFill, GTextAlignmentLeft, NULL);
  }
  if (f->today_wind_mph >= 0) {
    prv_draw_wind_icon(ctx, GPoint(134, WR_ROW2_TEXT_Y + 3));
    snprintf(buf, sizeof(buf), "%d MPH", f->today_wind_mph);
    graphics_draw_text(ctx, buf, s_report->f_stat, GRect(152, WR_ROW2_TEXT_Y, 44, 16),
                       GTextOverflowModeFill, GTextAlignmentLeft, NULL);
  }

  // UV sidebar, sketch layout: header row (sun + "UV INDEX") with the tally squares
  // centered beneath it, and a near-box-height numeral on the right. Squares fill in
  // the WHO severity color of today's value (same ramp as the card's UV dial).
  if (f->today_uv >= 0) {
    graphics_context_set_stroke_width(ctx, 1);
    graphics_draw_rect(ctx, GRect(WR_UV_X, WR_UV_Y, WR_UV_W, WR_UV_H));
    graphics_draw_rect(ctx, GRect(WR_UV_X + 1, WR_UV_Y + 1, WR_UV_W - 2, WR_UV_H - 2));
    // Header pinned to the box's top-left, banner-style: sun, then the label.
    prv_draw_sun_glyph(ctx, GPoint(20, WR_UV_Y + 14));
    // Gothic-14-Bold's V glyph fuses into a solid stem (reads as "UY") — Gothic 18's
    // V tapers properly.
    graphics_draw_text(ctx, "UV INDEX", s_report->f_lead,
                       GRect(32, WR_UV_Y + 2, 70, 20),
                       GTextOverflowModeFill, GTextAlignmentLeft, NULL);
    // The tally: ten left-aligned slots — a real 0-10 meter that fills up.
    const int filled = f->today_uv < WR_UV_SLOTS ? f->today_uv : WR_UV_SLOTS;
    for (int i = 0; i < WR_UV_SLOTS; i++) {
      const GRect sq = GRect(WR_UV_SQ_X0 + i * WR_UV_SQ_PITCH, WR_UV_Y + 25, WR_UV_SQ, WR_UV_SQ);
      if (i < filled) {
        graphics_context_set_fill_color(ctx, prv_uv_severity_color(f->today_uv));
        graphics_fill_rect(ctx, sq, 0, GCornerNone);
      }
      graphics_draw_rect(ctx, sq);
    }
    graphics_context_set_fill_color(ctx, GColorBlack);
    // The numeral rides the same LECO face as the big temp, nearly the box's height.
    snprintf(buf, sizeof(buf), "%d", f->today_uv);
    graphics_draw_text(ctx, buf, s_report->f_temp,
                       GRect(WR_UV_X + WR_UV_W - 46 - 6, WR_UV_Y - 5, 46, 44),
                       GTextOverflowModeFill, GTextAlignmentRight, NULL);
  }

  // Footer: the next day's row (label + tight °-temps + tiny icon) — or, on the last
  // page of the week, the Timeline fin ribbon. The 2px divider is page furniture on
  // every page, fin page included.
  if (s_report->num_days > 1) {
    graphics_fill_rect(ctx, GRect(WR_UV_X, WR_MORROW_RULE_Y, WR_UV_W, 2), 0, GCornerNone);
    if (day + 1 < (int)s_report->num_days) {
      const WeatherLocationForecast *morrow = &s_report->days[day + 1];
      char label[16];
      const char *lsrc = (morrow->label && morrow->label[0]) ? morrow->label : "TOMORROW";
      size_t li = 0;
      for (; lsrc[li] && li < sizeof(label) - 1; li++) {
        char c = lsrc[li];
        label[li] = (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
      }
      label[li] = '\0';
      char temps[16];
      prv_fill_high_low_tight_buffer(morrow->today_high, morrow->today_low,
                                     temps, sizeof(temps));
      // Fixed anchors so nothing jumps between pages: text starts on the left rail
      // (the divider/UV rail), the icon parks on the right rail. Temps ellipsize
      // before they can ever reach the icon (double-negative winters).
      const GSize lsz = graphics_text_layout_get_content_size(
          label, s_report->f_frac, GRect(0, 0, 200, 20), GTextOverflowModeFill,
          GTextAlignmentLeft);
      const int gap = 8;
      const int icon_x = WR_UV_X + WR_UV_W - 25;   // right edge on the x192 rail
      int x = WR_UV_X;
      graphics_draw_text(ctx, label, s_report->f_frac,
                         GRect(x, WR_MORROW_TEXT_Y, lsz.w + 2, 20),
                         GTextOverflowModeFill, GTextAlignmentLeft, NULL);
      x += lsz.w + gap;
      graphics_draw_text(ctx, temps, s_report->f_frac,
                         GRect(x, WR_MORROW_TEXT_Y, icon_x - 4 - x, 20),
                         GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
      if (s_report->slot[si].morrow) {
        graphics_context_set_compositing_mode(ctx, GCompOpSet);
        graphics_draw_bitmap_in_rect(ctx, s_report->slot[si].morrow,
                                     GRect(icon_x, WR_MORROW_ICON_Y, 25, 25));
        graphics_context_set_compositing_mode(ctx, GCompOpAssign);
      }
    } else if (s_report->fin_pdc) {
      // End of the week: the Timeline fin ribbon, native size, on the row's axis.
      gdraw_command_image_draw(ctx, s_report->fin_pdc, GPoint(WR_FIN_X, WR_FIN_Y));
      graphics_context_set_fill_color(ctx, GColorBlack);    // PDC leaves colors dirty
      graphics_context_set_stroke_color(ctx, GColorBlack);
    }
  }
}

// The transparent edition layers' shared update proc; each knows its slot index.
static void prv_ink_update_proc(Layer *layer, GContext *ctx) {
  if (!s_report || s_report->num_days == 0) return;
  const int si = *(int *)layer_get_data(layer);
  int d = s_report->slot[si].day;
  if (d < 0) return;
  if (d >= (int)s_report->num_days) d = (int)s_report->num_days - 1;  // draw-time clamp
  // GContext state can leak from the OTHER edition's last PDC command — reset to ink.
  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_context_set_stroke_color(ctx, GColorBlack);
  prv_draw_edition(ctx, si, d);
}

// ---- Edition slots: per-slot day icons (photo PDC + footer tiny PNG) --------
// <=2 resource loads per fill, success-keyed type cache. The old condensed did 3
// bitmap loads per 90ms hold step — proven safe. Loads happen BEFORE the flip is
// scheduled, never mid-animation.
static void prv_fill_slot(int si, int day) {
  if (day >= (int)s_report->num_days) day = (int)s_report->num_days - 1;
  if (day < 0) day = 0;
  s_report->slot[si].day = day;
  const WeatherType t = s_report->days[day].current_weather_type;
  if (!s_report->slot[si].photo || s_report->slot[si].photo_type != t) {
    if (s_report->slot[si].photo) {
      gdraw_command_image_destroy(s_report->slot[si].photo);
      s_report->slot[si].photo = NULL;
    }
    GDrawCommandImage *raw = gdraw_command_image_create_with_resource(
        weather_type_icon_large_resource(t));
    if (raw) {
      s_report->slot[si].photo = gdraw_command_image_clone(raw);  // writable copy to scale
      gdraw_command_image_destroy(raw);
      if (s_report->slot[si].photo) {
        gdraw_command_image_scale(s_report->slot[si].photo,
                                  GSize(WR_ICON_SIZE, WR_ICON_SIZE));
      }
    }
    s_report->slot[si].photo_type = t;
  }
  if (day + 1 < (int)s_report->num_days) {
    const WeatherType mt = s_report->days[day + 1].current_weather_type;
    if (!s_report->slot[si].morrow || s_report->slot[si].morrow_type != mt) {
      if (s_report->slot[si].morrow) gbitmap_destroy(s_report->slot[si].morrow);
      s_report->slot[si].morrow =
          gbitmap_create_with_resource(weather_type_icon_tiny_resource(mt));
      s_report->slot[si].morrow_type = mt;
    }
  } else if (s_report->slot[si].morrow) {
    gbitmap_destroy(s_report->slot[si].morrow);   // last day: fin, no footer icon
    s_report->slot[si].morrow = NULL;
  }
}

// ---- Day-flip animation engine -----------------------------------------------
// INVARIANT: flip/nudge code touches ONLY the ink[] layers' frame.y; only the
// prv_slide_in_* entrance touches the page layer's frame.x. Never cross.

static Animation *s_slide_in_anim;
static Animation *s_flip_anim;
static struct { bool is_down; bool hasted; } s_flip;
static Animation *s_nudge_anim;
static int        s_nudge_dir;   // -1 = tug up (DOWN refused), +1 = tug down

static int64_t prv_flip_interp(AnimationProgress p, int64_t from, int64_t to) {
  if (s_flip.hasted) p = (p + ANIMATION_NORMALIZED_MAX) / 2;   // Timeline second-half haste
  return interpolate_moook_soft(p, from, to, 3);
}

static void prv_flip_update(Animation *anim, AnimationProgress progress) {
  (void)anim;
  if (!s_report) return;
  const int end = s_flip.is_down ? -WR_PAGE_H : WR_PAGE_H;     // outgoing's destination
  const int dy  = (int)prv_flip_interp(progress, 0, end);
  Layer *lin  = s_report->ink[s_report->front];                // incoming (front swaps at start)
  Layer *lout = s_report->ink[s_report->front ^ 1];
  GRect fr = layer_get_frame_by_value(lout); fr.origin.y = dy;       layer_set_frame(lout, fr);
  fr       = layer_get_frame_by_value(lin);  fr.origin.y = dy - end; layer_set_frame(lin, fr);
}
static const AnimationImplementation s_flip_impl = { .update = prv_flip_update };

// Canonical rest: front edition at y0, back edition parked+hidden below. Also the
// cut-to-end used when a new flip (or a data refresh) interrupts one in flight.
static void prv_flip_settle(void) {
  if (!s_report || !s_report->ink[0]) return;                  // update_data-before-load guard
  Layer *lf = s_report->ink[s_report->front], *lb = s_report->ink[s_report->front ^ 1];
  GRect fr = layer_get_frame_by_value(lf); fr.origin.y = 0;          layer_set_frame(lf, fr);
  fr = layer_get_frame_by_value(lb);       fr.origin.y = WR_PAGE_H;  layer_set_frame(lb, fr);
  layer_set_hidden(lb, true);
  layer_set_hidden(lf, false);
}

static void prv_flip_stopped(Animation *anim, bool finished, void *context) {
  (void)finished; (void)context;
  if (s_flip_anim == anim) { s_flip_anim = NULL; prv_flip_settle(); }  // else: cancelled — no-op
  animation_destroy(anim);
}

// Makes the 90ms hold cadence safe against the 220ms animation: null-first, then
// unschedule (the stopped handler sees a nulled handle and only destroys).
static void prv_flip_prepare(void) {
  if (s_flip_anim)  { Animation *a = s_flip_anim;  s_flip_anim  = NULL; animation_unschedule(a); }
  if (s_nudge_anim) { Animation *a = s_nudge_anim; s_nudge_anim = NULL; animation_unschedule(a); }
  prv_flip_settle();   // committed day snaps to rest
}

static void prv_fill_slot(int si, int day);   // defined above; silence ordering doubts

static void prv_flip_start(bool is_down) {
  const bool was_in_flight = (s_flip_anim != NULL);
  prv_flip_prepare();
  const int in_slot = s_report->front ^ 1;
  prv_fill_slot(in_slot, s_report->current_day_index);   // synchronous, BEFORE schedule
  s_report->front = in_slot;
  s_flip.is_down = is_down;
  s_flip.hasted  = was_in_flight;   // re-trigger mid-flight rides the whip, not the windup
  GRect fr = layer_get_frame_by_value(s_report->ink[in_slot]);
  fr.origin.y = is_down ? WR_PAGE_H : -WR_PAGE_H;        // pre-position: no stale first frame
  layer_set_frame(s_report->ink[in_slot], fr);
  layer_set_hidden(s_report->ink[in_slot], false);
  layer_set_hidden(s_report->ink[in_slot ^ 1], false);   // outgoing visible during flight
  s_flip_anim = animation_create();
  animation_set_implementation(s_flip_anim, &s_flip_impl);
  animation_set_duration(s_flip_anim, WR_FLIP_MS);
  animation_set_curve(s_flip_anim, AnimationCurveLinear);  // the moook tables ARE the easing
  animation_set_handlers(s_flip_anim, (AnimationHandlers){ .stopped = prv_flip_stopped }, NULL);
  animation_schedule(s_flip_anim);
}

// Edge refusal: a 6px tug that decays in 4 frames — the page says "no more days".
static const int8_t kNudgeFrames[] = {6, 3, 1, 0};

static void prv_nudge_update(Animation *anim, AnimationProgress p) {
  (void)anim;
  if (!s_report || !s_report->ink[0]) return;
  const int n = (int)(sizeof(kNudgeFrames) / sizeof(kNudgeFrames[0]));
  int idx = (int)(((int64_t)p * n) / (ANIMATION_NORMALIZED_MAX + 1));
  if (idx < 0) idx = 0;
  if (idx >= n) idx = n - 1;
  GRect fr = layer_get_frame_by_value(s_report->ink[s_report->front]);
  fr.origin.y = s_nudge_dir * kNudgeFrames[idx];
  layer_set_frame(s_report->ink[s_report->front], fr);
}
static const AnimationImplementation s_nudge_impl = { .update = prv_nudge_update };

static void prv_nudge_stopped(Animation *anim, bool finished, void *context) {
  (void)finished; (void)context;
  if (s_nudge_anim == anim) {
    s_nudge_anim = NULL;
    if (s_report && s_report->ink[0]) {
      GRect fr = layer_get_frame_by_value(s_report->ink[s_report->front]);
      fr.origin.y = 0;
      layer_set_frame(s_report->ink[s_report->front], fr);
    }
  }
  animation_destroy(anim);
}

static void prv_start_nudge(bool is_down) {
  if (s_flip_anim || s_nudge_anim || s_slide_in_anim) return;
  s_nudge_dir = is_down ? -1 : 1;
  s_nudge_anim = animation_create();
  animation_set_implementation(s_nudge_anim, &s_nudge_impl);
  animation_set_duration(s_nudge_anim, WR_NUDGE_MS);
  animation_set_curve(s_nudge_anim, AnimationCurveLinear);
  animation_set_handlers(s_nudge_anim, (AnimationHandlers){ .stopped = prv_nudge_stopped }, NULL);
  animation_schedule(s_nudge_anim);
}

// ---- Input: tap UP/DOWN = one day, hold = accelerating scroll (the old condensed
// contract, verbatim timing), BACK (and swipe right) returns to the forecast list.

// Single entry point for clicks, hold repeats, and swipes. Bounded, no wrap.
static void prv_navigate(bool is_down, bool from_hold) {
  if (!s_report || s_slide_in_anim) return;               // entrance gate
  const int i = s_report->current_day_index;
  if (is_down ? (i + 1 >= (int)s_report->num_days) : (i <= 0)) {
    if (!from_hold) prv_start_nudge(is_down);             // taps/swipes only, never the 90ms floor
    return;
  }
  s_report->current_day_index = i + (is_down ? 1 : -1);   // model commits BEFORE animation
  prv_flip_start(is_down);
}

static void prv_click_back(ClickRecognizerRef r, void *ctx) {
  window_stack_pop(true);
}

static void prv_click_up_down(ClickRecognizerRef r, void *ctx) {
  if (!s_report) return;
  prv_navigate(click_recognizer_get_button_id(r) == BUTTON_ID_DOWN, false);
}

// Hold-to-scroll — the round arm's machinery verbatim: press arms a timer, each
// repeat steps a day and shrinks the interval by 50ms down to the 90ms floor.
static AppTimer *s_hold_timer;
static bool      s_hold_is_down;
static int       s_hold_repeat;

static void prv_hold_timer_cb(void *ctx) {
  s_hold_timer = NULL;
  if (!s_report) return;
  prv_navigate(s_hold_is_down, true);
  s_hold_repeat++;
  int interval = HOLD_INITIAL_MS - s_hold_repeat * 50;
  if (interval < HOLD_MIN_MS) interval = HOLD_MIN_MS;
  s_hold_timer = app_timer_register(interval, prv_hold_timer_cb, NULL);
}

static void prv_raw_up_down(ButtonId btn, bool pressed) {
  if (!s_report) return;
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
  window_single_click_subscribe(BUTTON_ID_UP,   prv_click_up_down);
  window_single_click_subscribe(BUTTON_ID_DOWN, prv_click_up_down);
  window_raw_click_subscribe(BUTTON_ID_UP,   prv_raw_up_pressed,   prv_raw_up_released,   NULL);
  window_raw_click_subscribe(BUTTON_ID_DOWN, prv_raw_down_pressed, prv_raw_down_released, NULL);
  window_single_click_subscribe(BUTTON_ID_BACK, prv_click_back);
}

#if WEATHER_PLATFORM_TOUCH_COLOR
#define SWIPE_THRESHOLD 20   // px; same tap/swipe split as the other screens

static void prv_touch_handler(const TouchEvent *event, void *context) {
  (void)context;
  if (!s_report) return;
  if (event->type == TouchEvent_Touchdown) {
    s_report->touch_start_x = event->x;
    s_report->touch_start_y = event->y;
    s_report->touch_active  = true;
  } else if (event->type == TouchEvent_Liftoff && s_report->touch_active) {
    s_report->touch_active = false;
    int16_t dx = event->x - s_report->touch_start_x;
    int16_t dy = event->y - s_report->touch_start_y;
    int16_t adx = dx < 0 ? -dx : dx;
    int16_t ady = dy < 0 ? -dy : dy;
    if (adx <= SWIPE_THRESHOLD && ady <= SWIPE_THRESHOLD) return;  // tap — no action
    if (ady >= adx) {
      prv_navigate(dy < 0, false);   // swipe up = next day; swipe down = previous
    } else if (dx > 0) {
      window_stack_pop(true);        // swipe right = BACK to the forecast list
    }
  }
}
#endif

// ---- Entrance squash-in (SELECT transition): the fully-composited page jelly-stretches
// in from the RIGHT, completing the scene the forecast played (its paper squashed out
// left). A topmost fx layer re-blits the frame through weather_render_squash — it MUST
// be the last child so its capture sees paper + both ink editions.

static Animation *s_entrance_anim;

static void prv_entrance_fx_update(Layer *layer, GContext *ctx) {
  (void)layer;
  if (!s_report || !s_report->entrance_scratch) return;
  weather_render_squash(ctx, s_report->entrance_scratch, s_report->entrance_p,
                        WEATHER_SQUASH_RIGHT_IN);
}

static void prv_entrance_update(Animation *anim, AnimationProgress progress) {
  (void)anim;
  if (!s_report) return;
  s_report->entrance_p = progress;
  if (s_report->page) layer_mark_dirty(s_report->page);
}

static void prv_entrance_stopped(Animation *anim, bool finished, void *context) {
  (void)finished; (void)context;
  if (s_entrance_anim == anim) {
    s_entrance_anim = NULL;
    if (s_report) {
      if (s_report->entrance_scratch) {
        free(s_report->entrance_scratch);
        s_report->entrance_scratch = NULL;
      }
      if (s_report->page) layer_mark_dirty(s_report->page);
    }
  }
  animation_destroy(anim);
}

static const AnimationImplementation s_entrance_impl = { .update = prv_entrance_update };

static void prv_start_squash_in(void) {
  if (!s_report || s_entrance_anim) return;
  s_report->entrance_scratch = malloc_try((size_t)WR_PAGE_W * (size_t)WR_PAGE_H);
  if (!s_report->entrance_scratch) return;   // OOM: the static entrance is the fallback
  s_report->entrance_p = 0;
  s_entrance_anim = animation_create();
  animation_set_implementation(s_entrance_anim, &s_entrance_impl);
  animation_set_duration(s_entrance_anim, 240);   // hasted soft-moook — snappy landing
  animation_set_curve(s_entrance_anim, AnimationCurveLinear);   // the jelly does the shaping
  animation_set_handlers(s_entrance_anim,
                         (AnimationHandlers){ .stopped = prv_entrance_stopped }, NULL);
  animation_schedule(s_entrance_anim);
}

// ---- Entrance: the page slides in from the RIGHT (Timeline new-card entrance),
// paper and ink riding together (the paper pass keys off the layer frame).

static void prv_slide_in_update(Animation *anim, AnimationProgress progress) {
  (void)anim;
  if (!s_report) return;
  Layer *root = s_report->page;
  const int w = layer_get_bounds(root).size.w;
  GRect fr = layer_get_frame_by_value(root);
  fr.origin.x = (int)interpolate_moook(progress, w, 0);
  layer_set_frame(root, fr);
}

static void prv_slide_in_stopped(Animation *anim, bool finished, void *context) {
  (void)anim; (void)finished; (void)context;
  s_slide_in_anim = NULL;
  if (s_report) {
    Layer *root = s_report->page;
    GRect fr = layer_get_frame_by_value(root);
    fr.origin.x = 0;
    layer_set_frame(root, fr);
    layer_mark_dirty(root);
  }
  animation_destroy(anim);
}

static const AnimationImplementation s_slide_in_impl = { .update = prv_slide_in_update };

static void prv_start_slide_in(void) {
  if (!s_report || s_slide_in_anim) return;
  Layer *root = s_report->page;
  const int w = layer_get_bounds(root).size.w;
  GRect fr = layer_get_frame_by_value(root);
  fr.origin.x = w;
  layer_set_frame(root, fr);
  s_slide_in_anim = animation_create();
  animation_set_implementation(s_slide_in_anim, &s_slide_in_impl);
  animation_set_duration(s_slide_in_anim, interpolate_moook_duration() / 2);   // 115ms (Timeline)
  animation_set_curve(s_slide_in_anim, AnimationCurveLinear);
  animation_set_handlers(s_slide_in_anim,
                         (AnimationHandlers){ .stopped = prv_slide_in_stopped }, NULL);
  animation_schedule(s_slide_in_anim);
}

// ---- Window lifecycle ----

static void prv_window_load(Window *window) {
  if (!s_report) return;
  s_report->f_headline = fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);
  s_report->f_body     = fonts_get_system_font(FONT_KEY_GOTHIC_28);
  s_report->f_body_medium = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
  s_report->f_lead     = fonts_get_system_font(FONT_KEY_GOTHIC_18);
  s_report->f_stat     = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
  s_report->f_temp     = fonts_get_system_font(FONT_KEY_LECO_38_BOLD_NUMBERS);
  s_report->f_frac     = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  GRect bounds = layer_get_bounds(window_get_root_layer(window));
  s_report->page = layer_create(bounds);
  layer_set_update_proc(s_report->page, prv_page_update_proc);
  layer_add_child(window_get_root_layer(window), s_report->page);
  // Two transparent edition layers over the paper; the back one parks below-screen.
  for (int i = 0; i < 2; i++) {
    s_report->ink[i] = layer_create_with_data(GRect(0, 0, WR_PAGE_W, WR_PAGE_H), sizeof(int));
    *(int *)layer_get_data(s_report->ink[i]) = i;
    layer_set_update_proc(s_report->ink[i], prv_ink_update_proc);
    layer_add_child(s_report->page, s_report->ink[i]);
  }
  s_report->front = 0;
  s_report->slot[0].day = s_report->slot[1].day = -1;
  prv_fill_slot(0, s_report->current_day_index);
  layer_set_frame(s_report->ink[1], GRect(0, WR_PAGE_H, WR_PAGE_W, WR_PAGE_H));
  layer_set_hidden(s_report->ink[1], true);
  // The fin ribbon (Timeline's own END_OF_TIMELINE PDC), loaded once, drawn native —
  // scaling would mush its ~7px "fin" letterforms.
  if (s_report->num_days > 1) {
    GDrawCommandImage *raw = gdraw_command_image_create_with_resource(RESOURCE_ID_END_OF_TIMELINE);
    s_report->fin_pdc = raw ? gdraw_command_image_clone(raw) : NULL;  // flash PDC is read-only
    if (raw) gdraw_command_image_destroy(raw);
  }
  // Entrance-fx layer LAST: its squash-in capture must see the whole composited page.
  s_report->fx = layer_create(bounds);
  layer_set_update_proc(s_report->fx, prv_entrance_fx_update);
  layer_add_child(s_report->page, s_report->fx);
}

static void prv_window_appear(Window *window) {
#if WEATHER_PLATFORM_TOUCH_COLOR
  if (s_report) touch_service_subscribe(prv_touch_handler, s_report);
#endif
  if (s_report && s_pending_select_in) {
    s_pending_select_in = false;
    prv_start_slide_in();
  } else if (s_report && s_pending_static_in) {
    s_pending_static_in = false;
    prv_start_squash_in();   // the transition scene's finale: jelly in from the right
  }
}

static void prv_window_unload(Window *window) {
#if WEATHER_PLATFORM_TOUCH_COLOR
  touch_service_unsubscribe();
#endif
  if (s_hold_timer) { app_timer_cancel(s_hold_timer); s_hold_timer = NULL; }
  // Module-level animations: null-first, then unschedule (the synchronous .stopped
  // handlers see nulled handles and only destroy) — so a re-push can't find stale
  // handles or animate the next instance's layers.
  if (s_flip_anim)  { Animation *a = s_flip_anim;  s_flip_anim  = NULL; animation_unschedule(a); }
  if (s_nudge_anim) { Animation *a = s_nudge_anim; s_nudge_anim = NULL; animation_unschedule(a); }
  if (s_slide_in_anim) {
    Animation *a = s_slide_in_anim;
    s_slide_in_anim = NULL;
    animation_unschedule(a);
  }
  if (s_entrance_anim) {   // capture-first: the synchronous .stopped sees the NULLed handle
    Animation *a = s_entrance_anim;
    s_entrance_anim = NULL;
    animation_unschedule(a);
    animation_destroy(a);
  }
  if (s_report) {
    if (s_report->entrance_scratch) {
      free(s_report->entrance_scratch);
      s_report->entrance_scratch = NULL;
    }
    for (int i = 0; i < 2; i++) {   // BOTH slots — forgetting the back slot leaks
      if (s_report->slot[i].photo)  gdraw_command_image_destroy(s_report->slot[i].photo);
      if (s_report->slot[i].morrow) gbitmap_destroy(s_report->slot[i].morrow);
    }
    if (s_report->fin_pdc) gdraw_command_image_destroy(s_report->fin_pdc);
    if (s_report->fx) layer_destroy(s_report->fx);
    if (s_report->ink[0]) layer_destroy(s_report->ink[0]);
    if (s_report->ink[1]) layer_destroy(s_report->ink[1]);
    if (s_report->page) layer_destroy(s_report->page);
  }
  window_destroy(window);
  if (s_report) {
    free(s_report);
    s_report = NULL;
  }
}

void weather_report_push(const WeatherLocationForecast *days, size_t num_days, int start_day_index) {
  // Consume the entrance latches up front so NO early return (guard, alloc, window failure)
  // can leave one armed for an unrelated future push; select_in re-arms below.
  const bool select_in = s_pending_select_in;
  const bool static_in = s_pending_static_in;
  s_pending_select_in = false;
  s_pending_static_in = false;
  if (s_report || !days || num_days == 0) return;
  s_report = calloc(1, sizeof(WeatherReportData));
  if (!s_report) return;
  s_report->days = days;
  s_report->num_days = num_days;
  s_report->current_day_index =
      (start_day_index >= 0 && start_day_index < (int)num_days) ? start_day_index : 0;
  s_report->window = window_create();
  if (!s_report->window) {
    free(s_report);
    s_report = NULL;
    return;
  }
  window_set_background_color(s_report->window, GColorWhite);
  window_set_window_handlers(s_report->window, (WindowHandlers){
    .load   = prv_window_load,
    .appear = prv_window_appear,
    .unload = prv_window_unload,
  });
  window_set_click_config_provider(s_report->window, prv_click_provider);
  s_pending_select_in = select_in;   // re-arm both for prv_window_appear, which consumes them
  s_pending_static_in = static_in;   // (static_in -> the squash-in-from-the-right entrance)
  window_stack_push(s_report->window, !(select_in || static_in));
}

bool weather_report_is_showing(void) {
  return s_report && s_report->window;
}

// Re-point the borrowed days array after a weather-event refresh (the array is rewritten in
// place and can SHRINK, e.g. a location change to a v3-only record). No-op when not showing.
void weather_report_update_data(const WeatherLocationForecast *days, size_t num_days) {
  if (!s_report || !days || num_days == 0) return;
  prv_flip_prepare();   // 1) cut any in-flight flip to rest FIRST — pure layer-frame
                        //    work, no days[] reads, so no stale-pointer window
  s_report->days = days;                       // 2) re-point the borrowed array
  s_report->num_days = num_days;
  if (s_report->current_day_index >= (int)num_days) {
    s_report->current_day_index = (int)num_days - 1;   // 3) the array can SHRINK (7 -> 2/1)
  }
  if (s_report->ink[0]) {                      // update_data-before-load guard
    prv_fill_slot(s_report->front, s_report->current_day_index);  // 4) re-seed the front slot
    s_report->slot[s_report->front ^ 1].day = -1;                 //    invalidate the back one
  }
  if (s_report->page) layer_mark_dirty(s_report->page);
}

#endif  // PBL_ROUND
