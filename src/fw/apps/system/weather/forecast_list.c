/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "forecast_list.h"
#include "weather.h"
#include "applib/ui/app_window_stack.h"
#include "weather_math.h"
#include "resource_ids.pin.h"
#include "applib/graphics/gdraw_command_transforms.h"
#include "applib/graphics/gdraw_command.h"
#include "applib/graphics/gdraw_command_list.h"
#include "util/trig.h"

#include <time.h>

#define MAX_ROWS         7
#define ROWS_VISIBLE     4
#define ICON_SIZE        25
#define ICON_X           8
#define DAY_LABEL_X      50
#define DAY_LABEL_W      PBL_IF_ROUND_ELSE(78, 34)
#define DAY_CONDITION_DIVIDER_GAP 4
#define CONDITION_TEXT_LEFT_GAP 11
#define CONDITION_RIGHT_MARGIN 4
#define ANIM_MS          200
#define SWIPE_THRESHOLD  20
#define LOCATION_BAR_H   18
#define LOCATION_BAR_Y   PBL_IF_ROUND_ELSE(20, 0)
#define LIST_TOP_Y       (LOCATION_BAR_Y + LOCATION_BAR_H)
#define ROUND_SIDE_INSET PBL_IF_ROUND_ELSE(28, 0)
#define ROUND_BAR_INSET  PBL_IF_ROUND_ELSE(44, 0)

#if defined(PBL_PLATFORM_GABBRO)
#define GABBRO_LIST_ROW_PITCH      45
#define GABBRO_LIST_FOCUSED_HEIGHT 55
#define GABBRO_LIST_ICON_SIZE      30
#define GABBRO_LIST_ICON_RADIUS    17
#define GABBRO_LIST_FOCUS_RADIUS   19
#define GABBRO_LIST_CENTER_Y_SHIFT 0
#define GABBRO_LIST_ICON_BASE_X    10
#define GABBRO_LIST_TEXT_GAP       11
#define GABBRO_LIST_EDGE_PAD       8
#define GABBRO_LIST_TOP_FOCUS_GAP  21
#define GABBRO_LIST_BOTTOM_FOCUS_MARGIN 48
#define GABBRO_LIST_CURVE_BOOST_DIVISOR 800
#define GABBRO_LIST_TOP_REST_VISIBLE_ROWS 4
#define GABBRO_CAP_DEPTH           58
#define GABBRO_CAP_RADIUS          207
#define GABBRO_CAP_Y_ADJUST        -13
#define GABBRO_CAP_TEXT_INSET      50
#define GABBRO_CAP_TIME_Y          2
#define GABBRO_CAP_LOCATION_Y      21
#endif

#define prv_weather_bg_color weather_type_bg_color
#if defined(PBL_PLATFORM_GABBRO)
#define prv_icon_res weather_type_icon_clock_resource
#else
#define prv_icon_res weather_type_icon_tiny_resource
#endif

// ---- State ----

typedef struct {
  Window  *window;
  Layer   *canvas;
  WeatherLocationForecast days[MAX_ROWS];
  size_t   num_days;
  int      scroll_offset_px;
  int      scroll_from;
  int      scroll_to;
  Animation *anim;
  GBitmap *icons[MAX_ROWS];
  GFont    day_font;
  GFont    condition_font;
  GFont    loc_font;
  char     condition_str[MAX_ROWS][24];  // pre-formatted condition strings
  int      row_height;  // computed at load: (screen_h - LOCATION_BAR_H) / ROWS_VISIBLE
#if defined(PBL_PLATFORM_GABBRO)
  GDrawCommandSequence *icon_sequence;
#endif
#if PBL_ROUND
  GDrawCommandImage *pdc_icons[MAX_ROWS];  // official PebbleOS weather PDC icons (round 5-day)
  GDrawCommandImage *today_pdc;            // larger today icon for the summary header
  AppTimer *sun_timer;                     // drives the today sun-ray swirl (only when Sun)
  int32_t   sun_phase;                     // ray rotation angle (0..TRIG_MAX_ANGLE)
#endif
  void   (*on_pop_cb)(void *ctx);
  void    *on_pop_ctx;
  void   (*on_city_select_cb)(void *ctx);
  void    *on_city_select_ctx;
  int      start_day_index;  // focused day from main view; list opens scrolled here
#if WEATHER_PLATFORM_TOUCH_COLOR
  int16_t  touch_start_x;
  int16_t  touch_start_y;
  int      scroll_at_drag_start;  // scroll_offset_px captured on Touchdown
  int16_t  vel_buf[4];            // ring buffer of recent y positions for velocity
  uint8_t  vel_idx;               // next write slot (mod 4)
  uint8_t  vel_count;             // valid entries in vel_buf (0..4)
  bool     touch_active;
  bool     drag_axis_set;         // true once horizontal/vertical determined
  bool     drag_is_vertical;
#endif
} ForecastListData;

static ForecastListData *s_list;

// ---- Helpers ----

static int prv_max_scroll(void) {
#if PBL_ROUND
  return 0;  // round 5-day view is a single static screen — no scrolling
#elif defined(PBL_PLATFORM_GABBRO)
  int extra = (int)s_list->num_days - 1;
  return extra > 0 ? extra * s_list->row_height : 0;
#else
  int extra = (int)s_list->num_days - ROWS_VISIBLE;
  return extra > 0 ? extra * s_list->row_height : 0;
#endif
}

static void prv_fill_weekday_label(int day_index, const char *fallback,
                                   char *buffer, size_t buffer_size) {
  if (!buffer || buffer_size == 0) return;

#if defined(PBL_PLATFORM_GABBRO)
  if (fallback && fallback[0]) {
    snprintf(buffer, buffer_size, "%s", fallback);
    for (char *c = buffer; *c; c++) {
      if (*c >= 'a' && *c <= 'z') *c = (char)(*c - 'a' + 'A');
    }
  } else {
    snprintf(buffer, buffer_size, "---");
  }
  return;
#else
  time_t target = time(NULL) + (time_t)day_index * 86400;
  struct tm *lt = localtime(&target);
  if (lt && strftime(buffer, buffer_size, "%a", lt) > 0) {
    for (char *c = buffer; *c; c++) {
      if (*c >= 'a' && *c <= 'z') *c = (char)(*c - 'a' + 'A');
    }
    return;
  }

  if (fallback && fallback[0]) {
    snprintf(buffer, buffer_size, "%.3s", fallback);
    for (char *c = buffer; *c; c++) {
      if (*c >= 'a' && *c <= 'z') *c = (char)(*c - 'a' + 'A');
    }
  } else {
    snprintf(buffer, buffer_size, "---");
  }
#endif
}

#if PBL_ROUND
#define R5_TODAY_ICON 40   // scaled size of the today-summary header icon

// Official PebbleOS weather PDC icon (Pebble_25x25_*) for each app WeatherType.
static uint32_t prv_official_weather_pdc_res(WeatherType t) {
  switch (t) {
    case WeatherType_Sun:          return RESOURCE_ID_SUNNY_DAY_TINY;
    case WeatherType_PartlyCloudy: return RESOURCE_ID_PARTLY_CLOUDY_TINY;
    case WeatherType_CloudyDay:    return RESOURCE_ID_CLOUDY_DAY_TINY;
    case WeatherType_LightRain:    return RESOURCE_ID_LIGHT_RAIN_TINY;
    case WeatherType_HeavyRain:    return RESOURCE_ID_HEAVY_RAIN_TINY;
    case WeatherType_LightSnow:    return RESOURCE_ID_LIGHT_SNOW_TINY;
    case WeatherType_HeavySnow:    return RESOURCE_ID_HEAVY_SNOW_TINY;
    case WeatherType_RainAndSnow:  return RESOURCE_ID_RAINING_AND_SNOWING_TINY;
    default:                       return RESOURCE_ID_GENERIC_WEATHER_TINY;
  }
}

// Larger (Pebble_50x50_*) official PDC, scaled down for the today-summary header.
static uint32_t prv_official_weather_pdc_res_small(WeatherType t) {
  switch (t) {
    case WeatherType_Sun:          return RESOURCE_ID_SUNNY_DAY_SMALL;
    case WeatherType_PartlyCloudy: return RESOURCE_ID_PARTLY_CLOUDY_SMALL;
    case WeatherType_CloudyDay:    return RESOURCE_ID_CLOUDY_DAY_SMALL;
    case WeatherType_LightRain:    return RESOURCE_ID_LIGHT_RAIN_SMALL;
    case WeatherType_HeavyRain:    return RESOURCE_ID_HEAVY_RAIN_SMALL;
    case WeatherType_LightSnow:    return RESOURCE_ID_LIGHT_SNOW_SMALL;
    case WeatherType_HeavySnow:    return RESOURCE_ID_HEAVY_SNOW_SMALL;
    case WeatherType_RainAndSnow:  return RESOURCE_ID_RAINING_AND_SNOWING_SMALL;
    default:                       return RESOURCE_ID_GENERIC_WEATHER_SMALL;
  }
}
#endif

static void prv_load_icons(void) {
#if defined(PBL_PLATFORM_GABBRO)
  return;
#elif PBL_ROUND
  // Round 5-day uses the official PebbleOS weather PDC icons, cloned into RAM
  // (a system-app resource is mmap'd READ-ONLY, so draw of the raw would fault).
  for (int i = 0; i < (int)s_list->num_days; i++) {
    if (s_list->pdc_icons[i]) {
      gdraw_command_image_destroy(s_list->pdc_icons[i]);
      s_list->pdc_icons[i] = NULL;
    }
    GDrawCommandImage *raw = gdraw_command_image_create_with_resource(
        prv_official_weather_pdc_res(s_list->days[i].current_weather_type));
    s_list->pdc_icons[i] = raw ? gdraw_command_image_clone(raw) : NULL;
    if (raw) gdraw_command_image_destroy(raw);
  }
  // Larger today icon for the summary header: official SMALL (50px) PDC scaled down.
  if (s_list->today_pdc) {
    gdraw_command_image_destroy(s_list->today_pdc);
    s_list->today_pdc = NULL;
  }
  if (s_list->num_days > 0) {
    GDrawCommandImage *traw = gdraw_command_image_create_with_resource(
        prv_official_weather_pdc_res_small(s_list->days[0].current_weather_type));
    if (traw) {
      s_list->today_pdc = gdraw_command_image_clone(traw);
      gdraw_command_image_destroy(traw);
      if (s_list->today_pdc) {
        gdraw_command_image_scale(s_list->today_pdc, GSize(R5_TODAY_ICON, R5_TODAY_ICON));
      }
    }
  }
#else
  for (int i = 0; i < (int)s_list->num_days; i++) {
    if (s_list->icons[i]) {
      gbitmap_destroy(s_list->icons[i]);
      s_list->icons[i] = NULL;
    }
    s_list->icons[i] = gbitmap_create_with_resource(
        prv_icon_res(s_list->days[i].current_weather_type));
  }
#endif
}

#if defined(PBL_PLATFORM_GABBRO)
static void prv_draw_gabbro_pdc_icon(GContext *ctx, WeatherType weather_type,
                                     GPoint origin) {
  if (!s_list) return;
  if (!s_list->icon_sequence) {
    s_list->icon_sequence =
        gdraw_command_sequence_create_with_resource(RESOURCE_ID_WEATHER_CLOCK_ICONS_PDC);
    if (!s_list->icon_sequence) return;
  }

  int frame_index = (int)weather_type;
  if (frame_index > (int)WeatherType_RainAndSnow) {
    frame_index = WeatherType_Generic;
  }

  GDrawCommandFrame *frame =
      gdraw_command_sequence_get_frame_by_index(s_list->icon_sequence, frame_index);
  if (frame) {
    gdraw_command_frame_draw(ctx, s_list->icon_sequence, frame, origin);
  }
}
#endif

// Pre-format condition strings so the draw proc does zero formatting work.
static void prv_format_conditions(void) {
  for (int i = 0; i < (int)s_list->num_days; i++) {
    const WeatherLocationForecast *f = &s_list->days[i];
    const char *condition = (f->current_weather_phrase && f->current_weather_phrase[0])
                                ? f->current_weather_phrase
                                : "--";
    snprintf(s_list->condition_str[i], sizeof(s_list->condition_str[i]), "%s", condition);
  }
}

// ---- Scroll animation ----

static void prv_anim_update(Animation *anim, AnimationProgress progress) {
  if (!s_list) return;
  int delta = s_list->scroll_to - s_list->scroll_from;
  int offset = s_list->scroll_from +
      weather_scale_i32(delta, progress, ANIMATION_NORMALIZED_MAX);

  s_list->scroll_offset_px = offset;
  layer_mark_dirty(s_list->canvas);
}

static const AnimationImplementation s_anim_impl = { .update = prv_anim_update };

static void prv_scroll_to_ms(int target, uint32_t duration_ms, bool button_scroll) {
  if (!s_list) return;
  if (target < 0) target = 0;
  if (target > prv_max_scroll()) target = prv_max_scroll();
  if (s_list->anim) {
    animation_unschedule(s_list->anim);
    animation_destroy(s_list->anim);
    s_list->anim = NULL;
  }
  s_list->scroll_from = s_list->scroll_offset_px;
  s_list->scroll_to   = target;
  if (s_list->scroll_from == s_list->scroll_to) return;
  if (duration_ms == 0) {
    // Instant: jump directly with no animation
    s_list->scroll_offset_px = target;
    layer_mark_dirty(s_list->canvas);
    return;
  }
  Animation *a = animation_create();
  animation_set_duration(a, duration_ms);
  animation_set_curve(a, AnimationCurveEaseOut);  // decelerate for fling feel
  animation_set_implementation(a, &s_anim_impl);
  animation_schedule(a);
  s_list->anim = a;
}

static void prv_scroll_to(int target) {
  prv_scroll_to_ms(target, ANIM_MS, false);
}

#if defined(PBL_PLATFORM_GABBRO)
static void prv_scroll_to_menu_repeat(int target) {
  if (!s_list) return;
  if (target < 0) target = 0;
  if (target > prv_max_scroll()) target = prv_max_scroll();

  if (s_list->anim) {
    animation_unschedule(s_list->anim);
    animation_destroy(s_list->anim);
    s_list->anim = NULL;
    s_list->scroll_offset_px = s_list->scroll_to;
  }

  prv_scroll_to_ms(target, 96, false);
}
#endif

// ---- Drawing ----

#if defined(PBL_PLATFORM_GABBRO)
static int prv_round_content_inset(int screen_y, int screen_h) {
  int radius = screen_h / 2;
  int y_offset = screen_y - radius;
  int32_t radius_sq = (int32_t)radius * radius;
  int32_t y_sq = (int32_t)y_offset * y_offset;
  int32_t sqrt_arg = radius_sq - y_sq;
  if (sqrt_arg < 0) sqrt_arg = 0;
  return GABBRO_LIST_ICON_BASE_X + radius - (int)weather_isqrt(sqrt_arg) +
      (int)(y_sq / GABBRO_LIST_CURVE_BOOST_DIVISOR);
}

static int prv_gabbro_focus_y_for_scroll(int scroll_offset, int screen_h) {
  int max_scroll = prv_max_scroll();
  int top_y = GABBRO_CAP_DEPTH + GABBRO_LIST_TOP_FOCUS_GAP;
  int bottom_y = screen_h - GABBRO_LIST_BOTTOM_FOCUS_MARGIN;
  if (bottom_y < top_y) bottom_y = top_y;
  if (max_scroll <= 0) return top_y;
  if (scroll_offset < 0) scroll_offset = 0;
  if (scroll_offset > max_scroll) scroll_offset = max_scroll;
  return top_y + weather_scale_i32(bottom_y - top_y, scroll_offset, max_scroll);
}

static void prv_draw_gabbro_cap(GContext *ctx, int w) {
  int hide_px = 0;
  int hide_distance = s_list->row_height * 2;
  if (hide_distance > 0) {
    hide_px = (int)((int32_t)s_list->scroll_offset_px * GABBRO_CAP_DEPTH /
                    hide_distance);
    if (hide_px > GABBRO_CAP_DEPTH) hide_px = GABBRO_CAP_DEPTH;
  }

  int center_y = GABBRO_CAP_DEPTH - GABBRO_CAP_RADIUS +
                 GABBRO_CAP_Y_ADJUST - hide_px;
  if (center_y + GABBRO_CAP_RADIUS <= 0) return;

  graphics_context_set_fill_color(ctx, GColorVividCerulean);
  graphics_fill_circle(ctx, GPoint(w / 2, center_y), GABBRO_CAP_RADIUS);

  graphics_context_set_text_color(ctx, GColorWhite);
  GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
  int safe = GABBRO_CAP_TEXT_INSET;

  char time_str[8];
  time_t now = time(NULL);
  struct tm *lt = localtime(&now);
  if (clock_is_24h_style()) {
    strftime(time_str, sizeof(time_str), "%H:%M", lt);
  } else {
    strftime(time_str, sizeof(time_str), "%I:%M", lt);
    if (time_str[0] == '0') memmove(time_str, time_str + 1, sizeof(time_str) - 1);
  }

  graphics_draw_text(ctx, time_str, font,
      GRect(safe, GABBRO_CAP_TIME_Y - hide_px, w - safe * 2, 18),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

  const char *loc = (s_list->num_days > 0 && s_list->days[0].location_name[0])
                      ? s_list->days[0].location_name : "";
  char city_name[48];
  size_t city_len = 0;
  while (loc[city_len] && loc[city_len] != ',' &&
         city_len < sizeof(city_name) - 1) {
    city_name[city_len] = loc[city_len];
    city_len++;
  }
  while (city_len > 0 && city_name[city_len - 1] == ' ') city_len--;
  city_name[city_len] = '\0';
  graphics_draw_text(ctx, city_name, font,
      GRect(safe, GABBRO_CAP_LOCATION_Y - hide_px, w - safe * 2, 19),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}

static void prv_canvas_draw_gabbro(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  int W = bounds.size.w;
  int H = bounds.size.h;
  int off = s_list->scroll_offset_px;
  int focus_y = prv_gabbro_focus_y_for_scroll(off, H) + GABBRO_LIST_CENTER_Y_SHIFT;
  int rh = s_list->row_height;

  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);
  graphics_context_set_compositing_mode(ctx, GCompOpSet);

  for (int i = 0; i < (int)s_list->num_days; i++) {
    if (off <= 0 && i >= GABBRO_LIST_TOP_REST_VISIBLE_ROWS) {
      continue;
    }

    int row_center_y = focus_y + i * rh - off;
    if (row_center_y < -GABBRO_LIST_FOCUSED_HEIGHT ||
        row_center_y > H + GABBRO_LIST_FOCUSED_HEIGHT) {
      continue;
    }

    int delta = row_center_y - focus_y;
    int abs_delta = delta < 0 ? -delta : delta;
    bool focused = abs_delta <= rh / 2;
    const WeatherLocationForecast *f = &s_list->days[i];

    int inset = prv_round_content_inset(row_center_y, H);
    int icon_x = inset;
    int draw_icon_size = GABBRO_LIST_ICON_SIZE;
    int icon_y = row_center_y - draw_icon_size / 2;
    int icon_cx = icon_x + draw_icon_size / 2;
    int icon_r = focused ? GABBRO_LIST_FOCUS_RADIUS : GABBRO_LIST_ICON_RADIUS;

    GColor bg = prv_weather_bg_color(f->current_weather_type);
    if (!gcolor_equal(bg, GColorClear)) {
      graphics_context_set_fill_color(ctx, bg);
      graphics_fill_circle(ctx, GPoint(icon_cx, row_center_y), (uint16_t)icon_r);
    }

    prv_draw_gabbro_pdc_icon(ctx, f->current_weather_type, GPoint(icon_x, icon_y));

    char weekday_label[16];
    prv_fill_weekday_label(i, f->label, weekday_label, sizeof(weekday_label));

    int text_x = icon_x + draw_icon_size + GABBRO_LIST_TEXT_GAP;
    int text_w = W - text_x - inset - GABBRO_LIST_EDGE_PAD;
    if (text_w < 28) text_w = 28;
    graphics_context_set_text_color(ctx, focused ? GColorBlack : GColorDarkGray);

    if (focused) {
      graphics_draw_text(ctx, weekday_label, s_list->day_font,
          GRect(text_x, row_center_y - 29, text_w, 28),
          GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
      graphics_draw_text(ctx, s_list->condition_str[i], s_list->condition_font,
          GRect(text_x, row_center_y - 2, text_w, 22),
          GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    } else {
      graphics_draw_text(ctx, weekday_label, s_list->loc_font,
          GRect(text_x, row_center_y - 11, text_w, 22),
          GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    }
  }

  prv_draw_gabbro_cap(ctx, W);
}
#endif

#if PBL_ROUND
// ---- Round single-view 5-day forecast ----------------------------------------
// All five days fit on one screen (no scrolling): a centred row of weather icons
// on reflective coloured discs, weekday name above + precip % below each, and a
// two-line hi/lo dot graph beneath (high temp above each warm dot, low temp below
// each cool dot). Gabbro/round only; emery keeps the scrolling list.
#define R5_MAX_COLS     5
// Three column tiers fan inward as they descend, inscribing the content into the
// round screen: icons widest (the circle's middle), high temps narrower, low temps
// narrowest (as wide as that depth allows).
#define R5_COL_STEP_ICON 50   // icon / day name / precip row — widest
#define R5_COL_STEP_HIGH 45   // high temps — narrower, following the curve down
#define R5_COL_STEP_LOW  42   // low temps — wide; labels live in the gap so it can stay low
#define R5_HEADER_Y     14    // header pinned at top; everything below is centred
#define R5_DAYNAME_Y    94
#define R5_ICON_CY      130   // icon row on the round screen's true vertical centre
#define R5_ICON_SIZE    25
#define R5_DISC_R       (R5_ICON_SIZE * 7 / 10)  // 17 — same disc as old list + mainscreen
#define R5_PRECIP_Y     150
#define R5_GRAPH_TOP    172   // y of the warmest dot — lifted into the freed precip band
#define R5_GRAPH_BOT    202   // y of the coolest dot — raised to leave room for low labels below
#define R5_DOT_R        5     // outer coloured dot radius
#define R5_DOT_INNER    2     // inner white radius (hollow dot)
#define R5_TODAY_X      68    // today icon draw offset → icon centred at x=88
#define R5_TODAY_Y      26
#define R5_DAYDATE_Y    68    // day + date, below the today icon

// ---- Animated sun: 10 rays orbit the sun clockwise, each ray's length recomputed
// from its CURRENT angle to the sun's natural profile (short at top, long at bottom)
// so the rays grow as they descend + shrink as they rise — a living glow. ----
#define R5_SUN_RAYS       10
#define R5_SUN_BODY_R     10   // sun body radius (approximates the PDC octagon)
#define R5_SUN_RAY_INNER  15   // rays start out here → clear gap between body + rays
#define R5_SUN_RAY_MIN    3    // shortest ray length (at the top)
#define R5_SUN_RAY_MAX    10   // longest ray length (at the bottom)
// Partly cloudy reuses the same sun, smaller, poking out behind the cloud.
#define R5_PC_SUN_BODY_R    6
#define R5_PC_SUN_RAY_INNER 10   // clear gap between the body + the rays
#define R5_PC_SUN_RAY_MIN   2
#define R5_PC_SUN_RAY_MAX   6

// Animation cadence (sun + rain share one timer): a 40 ms tick = 25 fps, smooth.
// The sun advances R5_SUN_ANGLE_STEP per tick; 168 ticks ≈ 6.7 s per orbit. The rain
// divides the accumulated angle back into a tick count so its fall speed is its own.
#define R5_SUN_PERIOD_MS   40
#define R5_SUN_ANGLE_STEP  (TRIG_MAX_ANGLE / 168)

static void prv_draw_animated_sun(GContext *ctx, GPoint c, int32_t phase,
                                  int body_r, int ray_inner, int ray_min, int ray_max) {
  // Warm yellow-orange = the partly-cloudy disc colour on this screen (not pure red
  // orange). Pulled from the same source so it matches exactly. This icon only.
  const GColor sun_color = prv_weather_bg_color(WeatherType_PartlyCloudy);
  graphics_context_set_antialiased(ctx, true);
  graphics_context_set_fill_color(ctx, sun_color);
  graphics_fill_circle(ctx, c, body_r + 1);
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_circle(ctx, c, body_r - 1);
  // Rays: each orbits clockwise; length depends only on its current angle.
  graphics_context_set_stroke_color(ctx, sun_color);
  graphics_context_set_stroke_width(ctx, 2);
  for (int i = 0; i < R5_SUN_RAYS; i++) {
    const int32_t ang = phase + (int32_t)i * (TRIG_MAX_ANGLE / R5_SUN_RAYS);
    const int32_t cos_a = cos_lookup(ang);  // +max at top (ang 0), -max at bottom (ang 180)
    const int32_t sin_a = sin_lookup(ang);
    // (1 - cos)/2 : 0 at top → 1 at bottom.
    const int len = ray_min + (int)((int32_t)(ray_max - ray_min) *
                    (TRIG_MAX_RATIO - cos_a) / (2 * TRIG_MAX_RATIO));
    // Radial direction from the sun centre, clockwise from top = (sin, -cos).
    const int ix = c.x + (int)((int32_t)ray_inner * sin_a / TRIG_MAX_RATIO);
    const int iy = c.y - (int)((int32_t)ray_inner * cos_a / TRIG_MAX_RATIO);
    const int ox = c.x + (int)((int32_t)(ray_inner + len) * sin_a / TRIG_MAX_RATIO);
    const int oy = c.y - (int)((int32_t)(ray_inner + len) * cos_a / TRIG_MAX_RATIO);
    graphics_draw_line(ctx, GPoint(ix, iy), GPoint(ox, oy));
  }
}

// ---- Animated precipitation (light rain + storm): the official cloud stays (black,
// static) but gently floats, while blue drops fall beneath it — a clear gap below the
// cloud, each drop fading in (grow), falling, fading out (shrink + pale) above the
// date. Drops have independent x, start offset + speed so they scatter, not sweep.
// Storm = darker blue, diagonal (wind-swept, down-left) drops + a more violent 2-axis
// cloud float. The PDC's own static rain lines are pushed off-screen by the processor.
#define R5_RAIN_TOP    54     // drops emerge here — a clear gap below the cloud bottom
#define R5_RAIN_BOT    68     // drops are destroyed here — just above the date
#define R5_RAIN_LEN    5      // full streak length
#define R5_RAIN_FADE   4      // px over which a drop grows in / shrinks out
#define R5_SNOW_TOP    51     // snow starts a touch higher than rain → more room to grow
#define R5_SNOW_MAX    5      // snowflake half-size (px) at its peak, just before it vanishes

typedef struct { int8_t dx; uint8_t spd; uint8_t off; } RainDrop;
typedef struct {
  GColor  main_col;          // streak colour
  GColor  pale_col;          // colour at the fade-in/out ends (toward the background)
  int8_t  slant;             // horizontal drift per px of fall, in 1/16 px (0 = vertical)
  uint8_t bob_y;             // gentle vertical cloud bob amplitude (px)
  uint8_t bob_ticks;         // cloud bob period (ticks)
  const RainDrop *drops;
  uint8_t n_drops;
} PrecipStyle;

// Light rain: 4 vertical drops on the columns the PDC implies, gentle vertical float.
static const RainDrop kRainDrops[4] = {
  { -11, 14,  0 }, {  -4, 18,  7 }, {   4, 12,  3 }, {  11, 16, 10 },
};
static const PrecipStyle kLightRainStyle = {
  GColorVividCerulean, GColorCeleste, 0, 2, 88, kRainDrops, 4,
};
// Storm (heavy rain): 4 diagonal (wind-swept, down-left) drops in darker blue, with the
// same gentle vertical bob as the rain cloud.
static const RainDrop kStormDrops[4] = {
  { -15, 15,  0 }, {  -9, 19,  8 }, {  -3, 13,  4 }, {   3, 17, 11 },
};
static const PrecipStyle kStormStyle = {
  GColorDukeBlue, GColorVividCerulean, 4, 2, 88, kStormDrops, 4,
};
// Light snow: very light blue flakes, slower fall (low spd) than rain, gentle sway. Reuses
// the rain cloud-bob; `dx` = column, `spd` = fall speed, `off` = phase. slant unused (snow
// sways instead). Drawn by prv_draw_animated_snow, not the streak path.
static const RainDrop kSnowFlakes[3] = {
  { -10, 4, 0 }, { 2, 3, 10 }, { 11, 4, 6 },
};
static const PrecipStyle kLightSnowStyle = {
  GColorPictonBlue, GColorPictonBlue, 0, 2, 88, kSnowFlakes, 3,
};
// Heavy snow: same flakes, denser (6) and a touch faster — a thicker fall.
static const RainDrop kHeavySnowFlakes[6] = {
  { -13, 5, 0 }, { -8, 6, 9 }, { -2, 5, 4 }, { 4, 6, 13 }, { 9, 5, 7 }, { 13, 6, 2 },
};
static const PrecipStyle kHeavySnowStyle = {
  GColorPictonBlue, GColorPictonBlue, 0, 2, 88, kHeavySnowFlakes, 6,
};
// Rain & snow (sleet): a moderate flake count falling a little quicker than plain snow.
static const RainDrop kRainSnowFlakes[4] = {
  { -11, 6, 0 }, { -3, 7, 9 }, { 5, 6, 4 }, { 12, 7, 12 },
};
static const PrecipStyle kRainSnowStyle = {
  GColorPictonBlue, GColorPictonBlue, 0, 2, 88, kRainSnowFlakes, 4,
};

static void prv_hide_rain_proc(GDrawCommandProcessor *processor, GDrawCommand *pc,
                               size_t max_size, const GDrawCommandList *list,
                               const GDrawCommand *command) {
  if (gdraw_command_get_num_points(pc) == 2) {  // 2-pt commands = the PDC rain lines
    const GPoint off = { -100, -100 };          // shove them off-screen (cloud stays)
    gdraw_command_set_point(pc, 0, off);
    gdraw_command_set_point(pc, 1, off);
  }
}

// Hide the PartlyCloudy PDC's sun (8-pt body octagon + 2-pt rays), leaving just the
// 13-pt cloud + its 3-pt highlight — so we can draw our own animated sun behind it.
static void prv_hide_sun_proc(GDrawCommandProcessor *processor, GDrawCommand *pc,
                              size_t max_size, const GDrawCommandList *list,
                              const GDrawCommand *command) {
  // Keep only the cloud (13/14-pt polygon) + its highlight (3-pt polyline); hide the
  // sun body (8 or 9 pts, depending on the explicit polygon close) + its rays (2 pts).
  const uint16_t n = gdraw_command_get_num_points(pc);
  if (n != 3 && n < 13) {
    const GPoint off = { -100, -100 };
    for (uint16_t i = 0; i < n; i++) {
      gdraw_command_set_point(pc, i, off);
    }
  }
}

// Partly cloudy: a small sun with orbiting rays pokes out (top-right, behind the cloud),
// and the official cloud floats on top with the same gentle bob as the rain cloud.
static void prv_draw_animated_partly_cloudy(GContext *ctx, GPoint pdc_offset, int32_t phase) {
  const int tick = phase / R5_SUN_ANGLE_STEP;
  const int32_t ay = (int32_t)(tick % 88) * (TRIG_MAX_ANGLE / 88);
  const int bob_y = 2 * sin_lookup(ay) / TRIG_MAX_RATIO;
  // Sun first (behind) at the PDC sun centre (33,16) scaled into the today box.
  prv_draw_animated_sun(ctx,
      GPoint(pdc_offset.x + 33 * R5_TODAY_ICON / 50, pdc_offset.y + 16 * R5_TODAY_ICON / 50),
      phase, R5_PC_SUN_BODY_R, R5_PC_SUN_RAY_INNER, R5_PC_SUN_RAY_MIN, R5_PC_SUN_RAY_MAX);
  // Floating cloud on top, its sun hidden (we drew our own animated one).
  if (s_list->today_pdc) {
    GDrawCommandProcessor proc = { .command = prv_hide_sun_proc };
    gdraw_command_image_draw_processed(ctx, s_list->today_pdc,
        GPoint(pdc_offset.x, pdc_offset.y + bob_y), &proc);
  }
}

// Cloudy: float the two clouds the same clean way we float the rain / partly-cloudy cloud —
// by translating the whole PDC's draw offset, not by nudging individual points. To move the
// clouds independently we draw the PDC twice; each pass hides the OTHER cloud off-screen so
// the kept cloud renders crisply at its own offset. CloudyDay PDC = lower cloud (cmds 0+1)
// then upper cloud (cmds 2+3).
typedef struct {
  GDrawCommandProcessor base;   // must be first — callback casts back to this
  int counter;
  int keep;                     // 0 = render lower cloud only, 1 = render upper cloud only
} CloudHideProc;

static void prv_cloud_hide_proc(GDrawCommandProcessor *processor, GDrawCommand *pc,
                                size_t max_size, const GDrawCommandList *list,
                                const GDrawCommand *command) {
  CloudHideProc *p = (CloudHideProc *)processor;
  const int which = (p->counter < 2) ? 0 : 1;  // cmds 0,1 = lower cloud; 2,3 = upper cloud
  p->counter++;
  if (which == p->keep) {
    return;  // keep this cloud exactly as authored
  }
  const uint16_t n = gdraw_command_get_num_points(pc);
  for (uint16_t i = 0; i < n; i++) {
    gdraw_command_set_point(pc, i, GPoint(-100, -100));  // shove the other cloud off-screen
  }
}

static void prv_draw_animated_cloudy(GContext *ctx, GPoint pdc_offset, int32_t phase) {
  if (!s_list->today_pdc) return;
  const int tick = phase / R5_SUN_ANGLE_STEP;
  // Each cloud floats a gentle ellipse — wider than tall, like a breeze — at different
  // rates + phases so they drift around and slide relative to one another.
  const int32_t aL = (int32_t)tick * (TRIG_MAX_ANGLE / 140);          // lower cloud, slower
  const int32_t aU = (int32_t)tick * (TRIG_MAX_ANGLE / 100) + 0x5000; // upper cloud, faster + offset
  const int lx = 5 * cos_lookup(aL) / TRIG_MAX_RATIO, ly = 2 * sin_lookup(aL) / TRIG_MAX_RATIO;
  const int ux = 5 * cos_lookup(aU) / TRIG_MAX_RATIO, uy = 2 * sin_lookup(aU) / TRIG_MAX_RATIO;

  CloudHideProc keep_lower = { .base = { .command = prv_cloud_hide_proc }, .counter = 0, .keep = 0 };
  gdraw_command_image_draw_processed(ctx, s_list->today_pdc,
      GPoint(pdc_offset.x + lx, pdc_offset.y + ly), &keep_lower.base);
  CloudHideProc keep_upper = { .base = { .command = prv_cloud_hide_proc }, .counter = 0, .keep = 1 };
  gdraw_command_image_draw_processed(ctx, s_list->today_pdc,
      GPoint(pdc_offset.x + ux, pdc_offset.y + uy), &keep_upper.base);
}

static void prv_draw_animated_precip(GContext *ctx, GPoint pdc_offset, int32_t phase,
                                     const PrecipStyle *st) {
  // phase is the sun-angle accumulator → back to a tick count (sun-speed independent).
  const int tick = phase / R5_SUN_ANGLE_STEP;
  // Gentle vertical cloud bob (same for light rain + storm).
  const int32_t ay = (int32_t)(tick % st->bob_ticks) * (TRIG_MAX_ANGLE / st->bob_ticks);
  const int bob_y = st->bob_y * sin_lookup(ay) / TRIG_MAX_RATIO;
  // Static black cloud = the official PDC with its built-in rain lines hidden.
  if (s_list->today_pdc) {
    GDrawCommandProcessor proc = { .command = prv_hide_rain_proc };
    gdraw_command_image_draw_processed(ctx, s_list->today_pdc,
        GPoint(pdc_offset.x, pdc_offset.y + bob_y), &proc);
  }
  const int range = R5_RAIN_BOT - R5_RAIN_TOP;
  const int cx    = pdc_offset.x + R5_TODAY_ICON / 2;
  graphics_context_set_antialiased(ctx, true);
  graphics_context_set_stroke_width(ctx, 2);
  for (int i = 0; i < st->n_drops; i++) {
    const RainDrop *d = &st->drops[i];
    const int prog  = ((tick * d->spd) / 10 + d->off) % range;  // 0..range-1
    const int y_bot = R5_RAIN_TOP + prog;
    // Fade in (grow) below the cloud, full mid-fall, fade out (shrink) at the base.
    int len;
    if (prog < R5_RAIN_FADE) {
      len = 1 + (R5_RAIN_LEN - 1) * prog / R5_RAIN_FADE;
    } else if (prog >= range - R5_RAIN_FADE) {
      len = 1 + (R5_RAIN_LEN - 1) * (range - 1 - prog) / R5_RAIN_FADE;
    } else {
      len = R5_RAIN_LEN;
    }
    const GColor col = (prog < 2 || prog >= range - 2) ? st->pale_col : st->main_col;
    graphics_context_set_stroke_color(ctx, col);
    // Diagonal drops drift sideways as they fall; the streak leans by the same slant.
    const int head_x = cx + d->dx - prog * st->slant / 16;
    const int tail_x = head_x + len * st->slant / 16;
    graphics_draw_line(ctx, GPoint(head_x, y_bot), GPoint(tail_x, y_bot - len));
  }
}

// A small snowflake: a "+" with shorter diagonals (8-pointed asterisk) at half-size r.
static void prv_draw_flake(GContext *ctx, GPoint c, int r) {
  if (r < 1) return;
  graphics_draw_line(ctx, GPoint(c.x, c.y - r), GPoint(c.x, c.y + r));
  graphics_draw_line(ctx, GPoint(c.x - r, c.y), GPoint(c.x + r, c.y));
  const int d = (r * 3) / 4;  // diagonals a touch shorter so the star stays round
  graphics_draw_line(ctx, GPoint(c.x - d, c.y - d), GPoint(c.x + d, c.y + d));
  graphics_draw_line(ctx, GPoint(c.x - d, c.y + d), GPoint(c.x + d, c.y - d));
}

// Light snow: same feel as the rain — flakes fall beneath a gently bobbing cloud, fade in
// under it and shrink away just above the date — but drawn as light-blue flakes that fall
// slower and sway side to side. Reuses prv_hide_rain_proc (the PDC's static flakes are 2-pt
// lines, same as rain lines) so only the cloud is kept.
static void prv_draw_animated_snow(GContext *ctx, GPoint pdc_offset, int32_t phase,
                                   const PrecipStyle *st) {
  const int tick = phase / R5_SUN_ANGLE_STEP;
  const int32_t ay = (int32_t)(tick % st->bob_ticks) * (TRIG_MAX_ANGLE / st->bob_ticks);
  const int bob_y = st->bob_y * sin_lookup(ay) / TRIG_MAX_RATIO;
  if (s_list->today_pdc) {
    GDrawCommandProcessor proc = { .command = prv_hide_rain_proc };
    gdraw_command_image_draw_processed(ctx, s_list->today_pdc,
        GPoint(pdc_offset.x, pdc_offset.y + bob_y), &proc);
  }
  const int range = R5_RAIN_BOT - R5_SNOW_TOP;
  const int peak  = (range * 3) / 5;   // grow to max ~60% down, then shrink away to nothing
  const int cx    = pdc_offset.x + R5_TODAY_ICON / 2;
  graphics_context_set_antialiased(ctx, true);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_context_set_stroke_color(ctx, st->main_col);
  for (int i = 0; i < st->n_drops; i++) {
    const RainDrop *f = &st->drops[i];
    const int prog = ((tick * f->spd) / 10 + f->off) % range;  // slow descent
    const int y    = R5_SNOW_TOP + prog;
    // Grows from a speck to full size as it falls, then shrinks away (disappears) before
    // it reaches the date — it never just pops out at full size.
    const int r = (prog <= peak)
        ? 1 + (R5_SNOW_MAX - 1) * prog / peak
        : R5_SNOW_MAX * (range - 1 - prog) / (range - 1 - peak);
    // Gentle side-to-side drift, each flake out of phase.
    const int32_t sa = (int32_t)tick * (TRIG_MAX_ANGLE / 45) + (int32_t)i * 0x2800;
    const int sway = 3 * sin_lookup(sa) / TRIG_MAX_RATIO;
    prv_draw_flake(ctx, GPoint(cx + f->dx + sway, y), r);
  }
}

static void prv_canvas_draw_round_5day(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  const int W = bounds.size.w;

  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  int total = (int)s_list->num_days;
  if (total <= 0) return;
  int n = total - 1;                  // fan columns — today headlines the block above
  if (n > R5_MAX_COLS) n = R5_MAX_COLS;
  if (n < 0) n = 0;
  const WeatherLocationForecast *fan = &s_list->days[1];  // fan = tomorrow onward

  // Three column tiers, each horizontally centred, fanning inward as they descend.
  int col_icon[R5_MAX_COLS], col_high[R5_MAX_COLS], col_low[R5_MAX_COLS];
  for (int i = 0; i < n; i++) {
    col_icon[i] = W / 2 - (n - 1) * R5_COL_STEP_ICON / 2 + i * R5_COL_STEP_ICON;
    col_high[i] = W / 2 - (n - 1) * R5_COL_STEP_HIGH / 2 + i * R5_COL_STEP_HIGH;
    col_low[i]  = W / 2 - (n - 1) * R5_COL_STEP_LOW  / 2 + i * R5_COL_STEP_LOW;
  }
  const int box_w = 52;  // text box; centred short text never overlaps neighbours
  // Fan weekday row demoted to match the today header weight (so TODAY outranks the fan).
  GFont day_font   = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
  GFont small_font = fonts_get_system_font(FONT_KEY_GOTHIC_14);

  // ---- Today summary header: large icon top-left, current temp top-right,
  //      condition below the temp, day + date below the icon ----
  const WeatherLocationForecast *today = &s_list->days[0];
  if (today->current_weather_type == WeatherType_Sun) {
    prv_draw_animated_sun(ctx,
        GPoint(R5_TODAY_X + R5_TODAY_ICON / 2, R5_TODAY_Y + R5_TODAY_ICON * 2 / 5),
        s_list->sun_phase, R5_SUN_BODY_R, R5_SUN_RAY_INNER, R5_SUN_RAY_MIN, R5_SUN_RAY_MAX);
  } else if (today->current_weather_type == WeatherType_PartlyCloudy) {
    prv_draw_animated_partly_cloudy(ctx, GPoint(R5_TODAY_X, R5_TODAY_Y), s_list->sun_phase);
  } else if (today->current_weather_type == WeatherType_CloudyDay) {
    prv_draw_animated_cloudy(ctx, GPoint(R5_TODAY_X, R5_TODAY_Y), s_list->sun_phase);
  } else if (today->current_weather_type == WeatherType_LightRain) {
    prv_draw_animated_precip(ctx, GPoint(R5_TODAY_X, R5_TODAY_Y),
                             s_list->sun_phase, &kLightRainStyle);
  } else if (today->current_weather_type == WeatherType_HeavyRain) {
    prv_draw_animated_precip(ctx, GPoint(R5_TODAY_X, R5_TODAY_Y),
                             s_list->sun_phase, &kStormStyle);
  } else if (today->current_weather_type == WeatherType_LightSnow) {
    prv_draw_animated_snow(ctx, GPoint(R5_TODAY_X, R5_TODAY_Y),
                           s_list->sun_phase, &kLightSnowStyle);
  } else if (today->current_weather_type == WeatherType_HeavySnow) {
    prv_draw_animated_snow(ctx, GPoint(R5_TODAY_X, R5_TODAY_Y),
                           s_list->sun_phase, &kHeavySnowStyle);
  } else if (today->current_weather_type == WeatherType_RainAndSnow) {
    prv_draw_animated_snow(ctx, GPoint(R5_TODAY_X, R5_TODAY_Y),
                           s_list->sun_phase, &kRainSnowStyle);
  } else if (s_list->today_pdc) {
    gdraw_command_image_draw(ctx, s_list->today_pdc, GPoint(R5_TODAY_X, R5_TODAY_Y));
  }
  char tnow[16];
  snprintf(tnow, sizeof(tnow), "%d\xC2\xB0", today->current_temp);
  graphics_context_set_text_color(ctx, GColorBlack);
  // Large temp, sized + vertically aligned to balance the 40px today icon.
  graphics_draw_text(ctx, tnow, fonts_get_system_font(FONT_KEY_LECO_36_BOLD_NUMBERS),
      GRect(112, 22, 120, 44),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  char cond[24];
  snprintf(cond, sizeof(cond), "%s",
           (today->current_weather_phrase && today->current_weather_phrase[0])
               ? today->current_weather_phrase : "--");
  // Title-case (e.g. "Partly Cloudy") — Pebble reserves ALL CAPS for terse labels/units.
  // Condition matches the date: same font/size/weight, same baseline (date is on
  // the left under the icon, condition on the right under the temp).
  graphics_context_set_text_color(ctx, GColorBlack);
  // Condition: right-anchored at x=230 (mirrors the left date, aligned to the fan's
  // 30/230 columns), growing LEFT as it lengthens down to x=110 (just clear of the
  // date). Length-dependent: measure the phrase + step the font down one size if even
  // that band can't hold it, so long phrases like "PARTLY CLOUDY" are never clipped.
  const int cond_l = 110, cond_r = 230;
  GFont cond_font = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
  const GSize cw = graphics_text_layout_get_content_size(
      cond, cond_font, GRect(0, 0, 300, 20), GTextOverflowModeWordWrap, GTextAlignmentLeft);
  if (cw.w > cond_r - cond_l) {
    cond_font = fonts_get_system_font(FONT_KEY_GOTHIC_14);  // non-bold is a touch narrower
  }
  graphics_draw_text(ctx, cond, cond_font,
      GRect(cond_l, R5_DAYDATE_Y, cond_r - cond_l, 16),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
  char daydate[24];
  time_t now = time(NULL);
  struct tm *lt = localtime(&now);
  if (lt) {
    strftime(daydate, sizeof(daydate), "%a, %b %d", lt);  // title-case "Thu, Jun 25"
  } else {
    daydate[0] = '\0';
  }
  graphics_context_set_text_color(ctx, GColorBlack);
  // Date: left box, left-aligned (mirror of the condition box).
  graphics_draw_text(ctx, daydate, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
      GRect(30, R5_DAYDATE_Y, 95, 16),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

  // Hairline rule separating the today header from the 5-day fan (timeline-card idiom).
  graphics_context_set_stroke_color(ctx, GColorLightGray);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_line(ctx, GPoint(40, 88), GPoint(W - 40, 88));

  // ---- Per-day fan column: weekday, disc + icon, precip % ----
  for (int i = 0; i < n; i++) {
    const WeatherLocationForecast *f = &fan[i];
    const int cx = col_icon[i];

    char weekday[16];
    prv_fill_weekday_label(i + 1, f->label, weekday, sizeof(weekday));
    graphics_context_set_text_color(ctx, GColorBlack);
    graphics_draw_text(ctx, weekday, day_font,
        GRect(cx - box_w / 2, R5_DAYNAME_Y, box_w, 22),
        GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

    GColor bg = prv_weather_bg_color(f->current_weather_type);
    if (!gcolor_equal(bg, GColorClear)) {
      graphics_context_set_fill_color(ctx, bg);
      graphics_fill_circle(ctx, GPoint(cx, R5_ICON_CY), R5_DISC_R);
    }
    if (s_list->pdc_icons[i + 1]) {
      GSize isz = gdraw_command_image_get_bounds_size(s_list->pdc_icons[i + 1]);
      gdraw_command_image_draw(ctx, s_list->pdc_icons[i + 1],
          GPoint(cx - isz.w / 2, R5_ICON_CY - isz.h / 2));
    }
  }

  // ---- Two-line hi/lo dot graph ----
  if (n < 1) return;  // no future days → just the today header
  // Scale each line to its OWN day-to-day range, in its own band (high band on top, low
  // band below), so the trend stays visible even when temps cluster — never a flat line.
  // A 4° floor stops a tiny spread from over-dramatising into a full-band swing.
  int hmin = fan[0].today_high, hmax = fan[0].today_high;
  int lmin = fan[0].today_low,  lmax = fan[0].today_low;
  for (int i = 0; i < n; i++) {
    if (fan[i].today_high > hmax) hmax = fan[i].today_high;
    if (fan[i].today_high < hmin) hmin = fan[i].today_high;
    if (fan[i].today_low  > lmax) lmax = fan[i].today_low;
    if (fan[i].today_low  < lmin) lmin = fan[i].today_low;
  }
  int hrange = hmax - hmin; if (hrange < 2) hrange = 2;
  int lrange = lmax - lmin; if (lrange < 2) lrange = 2;
  const int band   = (R5_GRAPH_BOT - R5_GRAPH_TOP) / 3;  // each line's vertical band
  const int hi_bot = R5_GRAPH_TOP + band;                // high band = [TOP, hi_bot]

  int y_hi[R5_MAX_COLS], y_lo[R5_MAX_COLS];
  for (int i = 0; i < n; i++) {
    y_hi[i] = hi_bot - (fan[i].today_high - hmin) * band / hrange;
    y_lo[i] = R5_GRAPH_BOT - (fan[i].today_low - lmin) * band / lrange;
  }

  graphics_context_set_antialiased(ctx, true);
  graphics_context_set_stroke_width(ctx, 2);
  // Muted connector lines — quiet links so the saturated dots + labels read as the data.
  for (int i = 0; i < n - 1; i++) {
    graphics_context_set_stroke_color(ctx, GColorWindsorTan);
    graphics_draw_line(ctx, GPoint(col_high[i], y_hi[i]), GPoint(col_high[i + 1], y_hi[i + 1]));
    graphics_context_set_stroke_color(ctx, GColorCadetBlue);
    graphics_draw_line(ctx, GPoint(col_low[i], y_lo[i]), GPoint(col_low[i + 1], y_lo[i + 1]));
  }
  // Hollow dots: a coloured ring with a white centre (warm high, cool low).
  for (int i = 0; i < n; i++) {
    graphics_context_set_fill_color(ctx, GColorOrange);
    graphics_fill_circle(ctx, GPoint(col_high[i], y_hi[i]), R5_DOT_R);
    graphics_context_set_fill_color(ctx, GColorWhite);
    graphics_fill_circle(ctx, GPoint(col_high[i], y_hi[i]), R5_DOT_INNER);
    graphics_context_set_fill_color(ctx, GColorVividCerulean);
    graphics_fill_circle(ctx, GPoint(col_low[i], y_lo[i]), R5_DOT_R);
    graphics_context_set_fill_color(ctx, GColorWhite);
    graphics_fill_circle(ctx, GPoint(col_low[i], y_lo[i]), R5_DOT_INNER);
  }
  // Temperature labels: high ABOVE its warm dot, low BELOW its cool dot.
  for (int i = 0; i < n; i++) {
    char hs[16], ls[16];
    snprintf(hs, sizeof(hs), "%d\xC2\xB0", fan[i].today_high);
    snprintf(ls, sizeof(ls), "%d\xC2\xB0", fan[i].today_low);
    graphics_context_set_text_color(ctx, GColorBlack);  // black numerals; dots/lines stay coloured
    graphics_draw_text(ctx, hs, small_font,
        GRect(col_high[i] - box_w / 2, y_hi[i] - R5_DOT_R - 17, box_w, 16),
        GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    graphics_context_set_text_color(ctx, GColorBlack);
    graphics_draw_text(ctx, ls, small_font,
        GRect(col_low[i] - box_w / 2, y_lo[i] + R5_DOT_R + 1, box_w, 16),
        GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  }
}
#endif

static void prv_canvas_draw(Layer *layer, GContext *ctx) {
  if (!s_list) return;
#if defined(PBL_PLATFORM_GABBRO)
  prv_canvas_draw_gabbro(layer, ctx);
  return;
#endif
#if PBL_ROUND
  prv_canvas_draw_round_5day(layer, ctx);
  return;
#endif
  GRect bounds = layer_get_bounds(layer);
  int W   = bounds.size.w;
  int H   = bounds.size.h;
  int off = s_list->scroll_offset_px;

  // Background
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  // Hoist context state changes outside the row loop.
  graphics_context_set_stroke_color(ctx, PBL_IF_COLOR_ELSE(GColorLightGray, GColorBlack));
  graphics_context_set_compositing_mode(ctx, GCompOpSet);

  int rh = s_list->row_height;
  const int side_inset = ROUND_SIDE_INSET;
  const int icon_x = ICON_X + side_inset;
  const int day_label_x = DAY_LABEL_X + side_inset;
  const int right_margin = CONDITION_RIGHT_MARGIN + side_inset;
  for (int i = 0; i < (int)s_list->num_days; i++) {
    int ry = LIST_TOP_Y + i * rh - off;
    if (ry + rh <= LIST_TOP_Y || ry >= H) continue;

    const WeatherLocationForecast *f = &s_list->days[i];

    // Row separator (skip very first row)
    if (ry > LIST_TOP_Y) {
      graphics_draw_line(ctx, GPoint(day_label_x, ry),
                         GPoint(W - right_margin, ry));
    }

    // Coloured circle — use fill_circle (much faster than fill_radial for full circles)
    int ic_cx = icon_x + ICON_SIZE / 2;
    int ic_cy = ry + (rh - ICON_SIZE) / 2 + ICON_SIZE / 2;
    int radius = ICON_SIZE * 7 / 10;  // same as diam/2 = ICON_SIZE*14/10/2
    GColor bg = prv_weather_bg_color(f->current_weather_type);
    if (!gcolor_equal(bg, GColorClear)) {
      graphics_context_set_fill_color(ctx, bg);
      graphics_fill_circle(ctx, GPoint(ic_cx, ic_cy), (uint16_t)radius);
    }

    // Icon
    if (s_list->icons[i]) {
      int iy = ry + (rh - ICON_SIZE) / 2;
      graphics_draw_bitmap_in_rect(ctx, s_list->icons[i],
          GRect(icon_x, iy, ICON_SIZE, ICON_SIZE));
    }

    // Vertical text baseline — subtract 3px to correct for GOTHIC_18 internal top padding
    // so glyphs are visually centred in the row rather than sitting low.
    int ty = ry + (rh - 18) / 2 - 3;

    int divider_x = day_label_x + DAY_LABEL_W + DAY_CONDITION_DIVIDER_GAP;
    int condition_x = divider_x + CONDITION_TEXT_LEFT_GAP;
    int condition_w = W - right_margin - condition_x;
    if (condition_w < 20) condition_w = 20;

    // Day label
    char weekday_label[16];
    prv_fill_weekday_label(i, f->label, weekday_label, sizeof(weekday_label));
    graphics_context_set_text_color(ctx, GColorBlack);
    graphics_draw_text(ctx, weekday_label, s_list->day_font,
        GRect(day_label_x, ty, DAY_LABEL_W, 20),
        GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

    graphics_context_set_stroke_color(ctx, PBL_IF_COLOR_ELSE(GColorLightGray, GColorBlack));
    graphics_draw_line(ctx, GPoint(divider_x, ry + 5), GPoint(divider_x, ry + rh - 5));

    // Hi / Lo temperature — use pre-formatted string, zero allocation per frame
    graphics_draw_text(ctx, s_list->condition_str[i], s_list->condition_font,
        GRect(condition_x, ty, condition_w, 20),
        GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  }

  // Scroll indicator
  int max_s = prv_max_scroll();
  if (max_s > 0) {
    int scroll_h = H - LIST_TOP_Y;
    int total_h  = (int)s_list->num_days * rh;
    int bar_h    = scroll_h * scroll_h / total_h;
    int bar_y    = LIST_TOP_Y + off * scroll_h / total_h;
    graphics_context_set_fill_color(ctx, GColorLightGray);
    graphics_fill_rect(ctx, GRect(W - side_inset - 3, bar_y, 3, bar_h),
                       0, GCornerNone);
  }

  // Blue location bar drawn last (covers any row that scrolled under it)
  graphics_context_set_fill_color(ctx, GColorVividCerulean);
  graphics_fill_rect(ctx, GRect(0, LOCATION_BAR_Y, W, LOCATION_BAR_H),
                     0, GCornerNone);
  graphics_context_set_text_color(ctx, GColorWhite);
  GFont bar_font = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
  const int bar_inset = ROUND_BAR_INSET;
  // Location name: left-aligned
  const char *loc = (s_list->num_days > 0 && s_list->days[0].location_name[0])
                      ? s_list->days[0].location_name : "";
  graphics_draw_text(ctx, loc, bar_font,
      GRect(bar_inset + 4, LOCATION_BAR_Y + 1,
            W - (bar_inset * 2) - 52, LOCATION_BAR_H),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  // Digital time: right-aligned (matches main screen)
  char time_str[8];
  time_t now = time(NULL);
  struct tm *lt = localtime(&now);
  if (clock_is_24h_style()) {
    strftime(time_str, sizeof(time_str), "%H:%M", lt);
  } else {
    strftime(time_str, sizeof(time_str), "%I:%M", lt);
    if (time_str[0] == '0') memmove(time_str, time_str + 1, sizeof(time_str) - 1);
  }
  graphics_draw_text(ctx, time_str, bar_font,
      GRect(W - bar_inset - 50, LOCATION_BAR_Y + 1, 48, LOCATION_BAR_H),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
}

// ---- Touch input (Emery only) ----

#if WEATHER_PLATFORM_TOUCH_COLOR
// Clamp scroll to valid range.
static int prv_clamp_scroll(int v) {
  if (v < 0) v = 0;
  int max = prv_max_scroll();
  if (v > max) v = max;
  return v;
}

// Fling: project a coast distance from velocity, snap to nearest row, scale duration.
// velocity_raw: total y-delta over vel_buf samples (positive = finger moved down).
// count: number of samples used.
static void prv_fling(int velocity_raw, int count) {
  int rh = s_list->row_height;
  if (rh <= 0) return;

  // Average pixels-per-event.  Positive velocity → finger moved DOWN → content
  // scrolls toward EARLIER rows (scroll_offset decreases).
  int vel = (count > 1) ? (velocity_raw / (count - 1)) : velocity_raw;

  // Project coast distance: vel * coast_factor (tuned so ~15px/event ≈ 2-3 rows).
  // Sign: finger-down (vel>0) → scroll_offset should decrease → coast is negative.
  const int COAST_FACTOR = 9;
  int coast_px = -vel * COAST_FACTOR;

  // Raw target in scroll space, then clamp.
  int target_raw     = s_list->scroll_offset_px + coast_px;
  int target_clamped = prv_clamp_scroll(target_raw);

  // Round to nearest row boundary.
  int target_snapped = ((target_clamped + rh / 2) / rh) * rh;
  target_snapped = prv_clamp_scroll(target_snapped);

  // Scale duration: 180ms per row, clamped 200..500ms, EaseOut for deceleration.
  int rows = target_snapped - s_list->scroll_offset_px;
  if (rows < 0) rows = -rows;
  rows = (rows + rh / 2) / rh;
  if (rows < 1) rows = 1;
  uint32_t duration = (uint32_t)(180 * rows);
  if (duration < 200) duration = 200;
  if (duration > 500) duration = 500;

  prv_scroll_to_ms(target_snapped, duration, false);
}

static void prv_touch_handler(const TouchEvent *event, void *context) {
  if (!s_list) return;

  if (event->type == TouchEvent_Touchdown) {
    // Cancel any running snap so dragging immediately takes over.
    if (s_list->anim) {
      animation_unschedule(s_list->anim);
      animation_destroy(s_list->anim);
      s_list->anim = NULL;
    }
    s_list->touch_start_x        = event->x;
    s_list->touch_start_y        = event->y;
    s_list->scroll_at_drag_start = s_list->scroll_offset_px;
    s_list->vel_buf[0]           = event->y;
    s_list->vel_idx              = 1;
    s_list->vel_count            = 1;
    s_list->drag_axis_set        = false;
    s_list->drag_is_vertical     = false;
    s_list->touch_active         = true;

  } else if (event->type == TouchEvent_PositionUpdate && s_list->touch_active) {
    int16_t dx  = event->x - s_list->touch_start_x;
    int16_t dy  = event->y - s_list->touch_start_y;
    int16_t adx = dx < 0 ? -dx : dx;
    int16_t ady = dy < 0 ? -dy : dy;

    // Determine axis once we have > 5 px of movement.
    if (!s_list->drag_axis_set && (adx > 5 || ady > 5)) {
      s_list->drag_is_vertical = (ady >= adx);
      s_list->drag_axis_set    = true;
    }

    if (s_list->drag_is_vertical) {
      // Live-update scroll: finger moved up (dy < 0) → scroll forward (later rows).
      int new_offset = prv_clamp_scroll(s_list->scroll_at_drag_start - (int)dy);
      if (new_offset != s_list->scroll_offset_px) {
        s_list->scroll_offset_px = new_offset;
        layer_mark_dirty(s_list->canvas);
      }
    }

    // Push y into the ring buffer for velocity calculation on liftoff.
    s_list->vel_buf[s_list->vel_idx & 3] = event->y;
    s_list->vel_idx++;
    if (s_list->vel_count < 4) s_list->vel_count++;

  } else if (event->type == TouchEvent_Liftoff && s_list->touch_active) {
    s_list->touch_active = false;

    int16_t dx  = event->x - s_list->touch_start_x;
    int16_t dy  = event->y - s_list->touch_start_y;
    int16_t adx = dx < 0 ? -dx : dx;
    int16_t ady = dy < 0 ? -dy : dy;

    // Horizontal swipe right = previous ring page (back to the main screen).
    if (!s_list->drag_is_vertical && dx > SWIPE_THRESHOLD && adx > ady) {
      weather_carousel_navigate(-1);
      return;
    }

    // Horizontal swipe left = next ring page (the clock face).
    if (!s_list->drag_is_vertical && dx < -SWIPE_THRESHOLD && adx > ady) {
      weather_carousel_navigate(+1);
      return;
    }

    // Vertical drag ended: fling using ring-buffer velocity.
    if (s_list->drag_is_vertical || ady >= adx) {
      int count = (int)s_list->vel_count;
      if (count >= 2) {
        // oldest entry in the ring buffer
        int oldest_idx = (int)(s_list->vel_idx - (uint8_t)count) & 3;
        int newest_idx = (int)(s_list->vel_idx - 1) & 3;
        int vel_total  = (int)s_list->vel_buf[newest_idx] - (int)s_list->vel_buf[oldest_idx];
        prv_fling(vel_total, count);
      } else {
        prv_fling(0, 1);  // no velocity, snap to nearest row
      }
    }
  }
}
#endif

// ---- Button input ----

static void prv_click_up_down(ClickRecognizerRef recognizer, void *context) {
  bool down = click_recognizer_get_button_id(recognizer) == BUTTON_ID_DOWN;
  if (down && s_list->scroll_to >= prv_max_scroll()) {
#if defined(PBL_PLATFORM_GABBRO)
    return;
#else
    if (s_list->on_city_select_cb) {
      s_list->on_city_select_cb(s_list->on_city_select_ctx);
    }
    return;
#endif
  }
#if defined(PBL_PLATFORM_GABBRO)
  if (click_recognizer_is_repeating(recognizer)) {
    prv_scroll_to_menu_repeat(s_list->scroll_to +
                              (down ? s_list->row_height : -s_list->row_height));
    return;
  }
#endif
  prv_scroll_to(s_list->scroll_to + (down ? s_list->row_height : -s_list->row_height));
}

static void prv_click_back(ClickRecognizerRef recognizer, void *context) {
  app_window_stack_pop_all(true);  // BACK exits the app from any ring page
}

static void prv_click_select(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer;
  (void)context;  // no per-screen SELECT action on the list in the carousel
}

// ---- Hold-to-scroll ----
#if !defined(PBL_PLATFORM_GABBRO)
// Rectangular builds keep the previous physics-style continuous scrolling.
// A 16ms ticker nudges scroll_offset_px by a velocity that ramps up each tick.
// Velocity stored in 1/16 px units to allow smooth sub-pixel accumulation.
#define SCROLL_TICK_MS    16   // ~60fps
#define SCROLL_VEL_START  16   // initial:  1 px/frame  (units = 1/16 px)
#define SCROLL_VEL_MAX    96   // maximum:  6 px/frame
#define SCROLL_VEL_ACCEL   2   // added to velocity each tick

static AppTimer *s_list_hold_timer = NULL;
static bool      s_list_hold_down  = false;
static int       s_list_velocity   = 0;
static int       s_list_subpx      = 0;

static void prv_list_tick_cb(void *ctx) {
  s_list_hold_timer = NULL;
  if (!s_list) return;

  s_list_velocity += SCROLL_VEL_ACCEL;
  if (s_list_velocity > SCROLL_VEL_MAX) s_list_velocity = SCROLL_VEL_MAX;

  s_list_subpx += s_list_velocity;
  int delta = s_list_subpx / 16;
  s_list_subpx %= 16;

  if (delta > 0) {
    int next = s_list->scroll_offset_px + (s_list_hold_down ? delta : -delta);
    int max  = prv_max_scroll();
    if (next < 0) next = 0;
    if (next > max) next = max;

    // Cancel any in-flight eased animation and take direct control
    if (s_list->anim) {
      animation_unschedule(s_list->anim);
      animation_destroy(s_list->anim);
      s_list->anim = NULL;
    }
    s_list->scroll_offset_px = next;
    s_list->scroll_to        = next;
    s_list->scroll_from      = next;
    layer_mark_dirty(s_list->canvas);
  }

  s_list_hold_timer = app_timer_register(SCROLL_TICK_MS, prv_list_tick_cb, NULL);
}

static void prv_list_raw(ButtonId btn, bool pressed) {
  if (pressed) {
    s_list_hold_down = (btn == BUTTON_ID_DOWN);
    s_list_velocity  = SCROLL_VEL_START;
    s_list_subpx     = 0;
    if (s_list_hold_timer) { app_timer_cancel(s_list_hold_timer); s_list_hold_timer = NULL; }
    // 300ms hold threshold before continuous scroll begins
    s_list_hold_timer = app_timer_register(300, prv_list_tick_cb, NULL);
  } else {
    if (s_list_hold_timer) { app_timer_cancel(s_list_hold_timer); s_list_hold_timer = NULL; }
  }
}

static void prv_list_raw_up_press(ClickRecognizerRef r, void *ctx)   { prv_list_raw(BUTTON_ID_UP,   true);  }
static void prv_list_raw_up_rel(ClickRecognizerRef r, void *ctx)     { prv_list_raw(BUTTON_ID_UP,   false); }
static void prv_list_raw_down_press(ClickRecognizerRef r, void *ctx) { prv_list_raw(BUTTON_ID_DOWN, true);  }
static void prv_list_raw_down_rel(ClickRecognizerRef r, void *ctx)   { prv_list_raw(BUTTON_ID_DOWN, false); }
#endif

static void prv_click_provider(void *context) {
#if defined(PBL_PLATFORM_GABBRO)
  window_single_repeating_click_subscribe(BUTTON_ID_UP, 100, prv_click_up_down);
  window_single_repeating_click_subscribe(BUTTON_ID_DOWN, 100, prv_click_up_down);
#else
  window_single_click_subscribe(BUTTON_ID_UP,     prv_click_up_down);
  window_single_click_subscribe(BUTTON_ID_DOWN,   prv_click_up_down);
#endif
  window_single_click_subscribe(BUTTON_ID_BACK,   prv_click_back);
  window_single_click_subscribe(BUTTON_ID_SELECT, prv_click_select);
#if !defined(PBL_PLATFORM_GABBRO)
  window_raw_click_subscribe(BUTTON_ID_UP,   prv_list_raw_up_press,   prv_list_raw_up_rel,   NULL);
  window_raw_click_subscribe(BUTTON_ID_DOWN, prv_list_raw_down_press, prv_list_raw_down_rel, NULL);
#endif
}

// ---- Window lifecycle ----

#if PBL_ROUND
static void prv_sun_tick(void *ctx) {
  if (!s_list) return;
  s_list->sun_timer = NULL;
  s_list->sun_phase += R5_SUN_ANGLE_STEP;  // 168 steps × 40ms ≈ 6.7s per full orbit
  if (s_list->canvas) {
    layer_mark_dirty(s_list->canvas);
  }
  s_list->sun_timer = app_timer_register(R5_SUN_PERIOD_MS, prv_sun_tick, NULL);
}
#endif

static void prv_window_load(Window *window) {
  GRect bounds = layer_get_bounds(window_get_root_layer(window));
#if defined(PBL_PLATFORM_GABBRO)
  s_list->row_height = GABBRO_LIST_ROW_PITCH;
#else
  // Divide available height evenly so exactly ROWS_VISIBLE rows fill the screen.
  s_list->row_height = (bounds.size.h - LIST_TOP_Y) / ROWS_VISIBLE;
#endif
  // Apply initial scroll so the focused day is at the top.
  {
    int initial = s_list->start_day_index * s_list->row_height;
    int max_s   = prv_max_scroll();
    if (initial > max_s) initial = max_s;
    s_list->scroll_offset_px = initial;
    s_list->scroll_to        = initial;
    s_list->scroll_from      = initial;
  }
  s_list->canvas = layer_create(bounds);
  layer_set_update_proc(s_list->canvas, prv_canvas_draw);
  layer_add_child(window_get_root_layer(window), s_list->canvas);

#if defined(PBL_PLATFORM_GABBRO)
  s_list->day_font       = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
  s_list->condition_font = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  s_list->loc_font       = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
#else
  s_list->day_font       = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  s_list->condition_font = s_list->day_font;
  s_list->loc_font       = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
#endif

  prv_load_icons();
  prv_format_conditions();

#if PBL_ROUND
  if (s_list->num_days > 0 &&
      (s_list->days[0].current_weather_type == WeatherType_Sun ||
       s_list->days[0].current_weather_type == WeatherType_PartlyCloudy ||
       s_list->days[0].current_weather_type == WeatherType_CloudyDay ||
       s_list->days[0].current_weather_type == WeatherType_LightRain ||
       s_list->days[0].current_weather_type == WeatherType_HeavyRain ||
       s_list->days[0].current_weather_type == WeatherType_LightSnow ||
       s_list->days[0].current_weather_type == WeatherType_HeavySnow ||
       s_list->days[0].current_weather_type == WeatherType_RainAndSnow)) {
    s_list->sun_phase = 0;
    s_list->sun_timer = app_timer_register(R5_SUN_PERIOD_MS, prv_sun_tick, NULL);
  }
#endif

#if WEATHER_PLATFORM_TOUCH_COLOR
  touch_service_subscribe(prv_touch_handler, s_list);
#endif
}

static void prv_window_appear(Window *window) {
  (void)window;
#if WEATHER_PLATFORM_TOUCH_COLOR
  if (s_list) {
    touch_service_subscribe(prv_touch_handler, s_list);
  }
#endif
}

static void prv_window_unload(Window *window) {
  // Fire pop callback before tearing down so the main screen can start its return animation.
  if (s_list && s_list->on_pop_cb) {
    void (*cb)(void *) = s_list->on_pop_cb;
    void *ctx = s_list->on_pop_ctx;
    s_list->on_pop_cb  = NULL;
    s_list->on_pop_ctx = NULL;
    cb(ctx);
  }

#if WEATHER_PLATFORM_TOUCH_COLOR
  touch_service_unsubscribe();
  // NOTE: the main window's appear handler will re-subscribe its own touch handler.
#endif

  if (s_list->anim) {
    animation_unschedule(s_list->anim);
    animation_destroy(s_list->anim);
    s_list->anim = NULL;
  }

#if defined(PBL_PLATFORM_GABBRO)
  if (s_list->icon_sequence) {
    gdraw_command_sequence_destroy(s_list->icon_sequence);
    s_list->icon_sequence = NULL;
  }
#endif
#if PBL_ROUND
  if (s_list->sun_timer) {
    app_timer_cancel(s_list->sun_timer);
    s_list->sun_timer = NULL;
  }
  for (int i = 0; i < MAX_ROWS; i++) {
    if (s_list->pdc_icons[i]) {
      gdraw_command_image_destroy(s_list->pdc_icons[i]);
      s_list->pdc_icons[i] = NULL;
    }
  }
  if (s_list->today_pdc) {
    gdraw_command_image_destroy(s_list->today_pdc);
    s_list->today_pdc = NULL;
  }
#endif

  for (int i = 0; i < MAX_ROWS; i++) {
    if (s_list->icons[i]) {
      gbitmap_destroy(s_list->icons[i]);
      s_list->icons[i] = NULL;
    }
  }
  layer_destroy(s_list->canvas);
  s_list->canvas = NULL;

  window_destroy(window);
  free(s_list);
  s_list = NULL;
}

// ---- Public API ----

static void prv_forecast_list_push(const WeatherLocationForecast *days, size_t num_days,
                                   int start_day_index, bool animated,
                                   void (*on_pop)(void *ctx), void *on_pop_ctx,
                                   void (*on_city_select)(void *ctx),
                                   void *on_city_select_ctx) {
  if (s_list) return;  // already showing

  s_list = calloc(1, sizeof(ForecastListData));
  if (!s_list) return;

  size_t n = num_days < MAX_ROWS ? num_days : MAX_ROWS;
  s_list->num_days = n;
  for (size_t i = 0; i < n; i++) {
    s_list->days[i] = days[i];
  }
  s_list->on_pop_cb          = on_pop;
  s_list->on_pop_ctx         = on_pop_ctx;
  s_list->on_city_select_cb  = on_city_select;
  s_list->on_city_select_ctx = on_city_select_ctx;
  s_list->start_day_index = start_day_index < (int)n ? start_day_index : (int)n - 1;

  s_list->window = window_create();
  window_set_background_color(s_list->window, GColorWhite);
  window_set_window_handlers(s_list->window, (WindowHandlers){
    .load   = prv_window_load,
    .appear = prv_window_appear,
    .unload = prv_window_unload,
  });
  window_set_click_config_provider(s_list->window, prv_click_provider);
  window_stack_push(s_list->window, animated);
}

void forecast_list_push(const WeatherLocationForecast *days, size_t num_days,
                        int start_day_index, bool animated,
                        void (*on_pop)(void *ctx), void *on_pop_ctx,
                        void (*on_city_select)(void *ctx), void *on_city_select_ctx) {
  prv_forecast_list_push(days, num_days, start_day_index, animated,
                         on_pop, on_pop_ctx, on_city_select, on_city_select_ctx);
}

void forecast_list_dismiss(bool animated) {
  if (!s_list || !s_list->window) return;
  window_stack_remove(s_list->window, animated);
}

bool forecast_list_is_showing(void) {
  return s_list && s_list->window;
}

void forecast_list_update_data(const WeatherLocationForecast *days, size_t num_days) {
  if (!s_list) return;

  size_t n = num_days < MAX_ROWS ? num_days : MAX_ROWS;
  s_list->num_days = n;
  for (size_t i = 0; i < n; i++) {
    s_list->days[i] = days[i];
  }

  if (s_list->canvas) {
    prv_load_icons();
    prv_format_conditions();
    layer_mark_dirty(s_list->canvas);
  }
}
