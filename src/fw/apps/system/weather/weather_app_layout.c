/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "weather_app_layout.h"
#include "weather_math.h"
#include "resource_ids.pin.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#define WEATHER_APP_LAYOUT_TOP_PADDING PBL_IF_RECT_ELSE(4, 0)
#define WEATHER_APP_LAYOUT_CONTENT_LAYER_HORIZONTAL_INSET PBL_IF_RECT_ELSE(6, 2)
#define WEATHER_APP_LAYOUT_LOCATION_BAR_Y PBL_IF_ROUND_ELSE(18, 0)
#define WEATHER_APP_LAYOUT_ROUND_BAR_DEPTH 58
#define WEATHER_APP_LAYOUT_ROUND_BAR_RADIUS 207
#define WEATHER_APP_LAYOUT_ROUND_BAR_Y_ADJUST -13
#define WEATHER_APP_LAYOUT_BAR_LAYER_Y PBL_IF_ROUND_ELSE(0, WEATHER_APP_LAYOUT_LOCATION_BAR_Y)
#define WEATHER_APP_LAYOUT_BAR_LAYER_HEIGHT \
  PBL_IF_ROUND_ELSE(WEATHER_APP_LAYOUT_ROUND_BAR_DEPTH, WEATHER_APP_LAYOUT_MAIN_BAR_HEIGHT)
#define WEATHER_APP_LAYOUT_MAIN_CONTENT_TOP \
  PBL_IF_ROUND_ELSE(WEATHER_APP_LAYOUT_ROUND_BAR_DEPTH, \
                    WEATHER_APP_LAYOUT_LOCATION_BAR_Y + \
                    WEATHER_APP_LAYOUT_MAIN_BAR_HEIGHT + \
                    WEATHER_APP_LAYOUT_TOP_PADDING)
#define WEATHER_APP_LAYOUT_CONTENT_Y_SHIFT PBL_IF_ROUND_ELSE(-5, 0)
#define WEATHER_APP_LAYOUT_ACTIVE_BAR_DEPTH \
  PBL_IF_ROUND_ELSE(WEATHER_APP_LAYOUT_ROUND_BAR_DEPTH, WEATHER_APP_LAYOUT_MAIN_BAR_HEIGHT)
#define WEATHER_APP_LAYOUT_LOCATION_BAR_TEXT_INSET PBL_IF_ROUND_ELSE(50, 0)
#define WEATHER_APP_LAYOUT_LOCATION_BAR_TEXT_Y PBL_IF_ROUND_ELSE(22, 1)
#define WEATHER_APP_LAYOUT_ROUND_TIME_TEXT_Y 2
#define WEATHER_APP_LAYOUT_ROUND_LOCATION_TEXT_Y 21
#define WEATHER_APP_LAYOUT_DAY_X_SHIFT PBL_IF_ROUND_ELSE(12, 0)
#define WEATHER_APP_LAYOUT_TEMP_X_SHIFT PBL_IF_ROUND_ELSE(-4, 0)
#define WEATHER_APP_LAYOUT_HIGHLOW_X_SHIFT PBL_IF_ROUND_ELSE(-2, 0)
#define WEATHER_APP_LAYOUT_METRICS_X_SHIFT PBL_IF_ROUND_ELSE(5, 0)
#define WEATHER_APP_LAYOUT_DAY_Y_SHIFT PBL_IF_ROUND_ELSE(-4, 0)
#define WEATHER_APP_LAYOUT_TEXT_STACK_STEP PBL_IF_ROUND_ELSE(34, 0)
#define WEATHER_APP_LAYOUT_TEXT_STACK_GAP PBL_IF_ROUND_ELSE(4, 0)
#define WEATHER_APP_LAYOUT_METRIC_TEXT_Y_INSET PBL_IF_RECT_ELSE(7, 18)
#define WEATHER_APP_LAYOUT_TEMP_Y_SHIFT \
  PBL_IF_ROUND_ELSE(WEATHER_APP_LAYOUT_TEXT_STACK_GAP, 0)
// Rect: +3 drops the high/low line so the LECO temp's glyphs sit visually
// CENTERED between the day label above and the high/low below (LECO carries
// ~6px of top bearing, so equal box gaps read as lopsided without this).
#define WEATHER_APP_LAYOUT_HIGHLOW_Y_SHIFT \
  PBL_IF_ROUND_ELSE(WEATHER_APP_LAYOUT_TEXT_STACK_GAP * 2, 5)
#define WEATHER_APP_LAYOUT_METRICS_Y_SHIFT \
  PBL_IF_ROUND_ELSE(WEATHER_APP_LAYOUT_TEXT_STACK_GAP * 3 - \
                    WEATHER_APP_LAYOUT_METRIC_TEXT_Y_INSET, 2)
#define WEATHER_APP_LAYOUT_TODAY_ICON_X_ADJUST PBL_IF_ROUND_ELSE(9, 0)
#define WEATHER_APP_LAYOUT_TOMORROW_ICON_X_ADJUST PBL_IF_ROUND_ELSE(23, 0)
#define WEATHER_APP_LAYOUT_TODAY_ICON_Y_ADJUST PBL_IF_ROUND_ELSE(-28, -11)
#define WEATHER_APP_LAYOUT_ICON_Y_ADJUST PBL_IF_ROUND_ELSE(-15, 0)
#define WEATHER_APP_LAYOUT_TOMORROW_ICON_Y_ADJUST PBL_IF_ROUND_ELSE(15, 0)
#define WEATHER_APP_LAYOUT_SEPARATOR_Y_ADJUST PBL_IF_ROUND_ELSE(-8, 5)
#define WEATHER_APP_LAYOUT_BOTTOM_TEXT_X_SHIFT PBL_IF_ROUND_ELSE(30, 0)
#define WEATHER_APP_LAYOUT_BOTTOM_LABEL_X_ADJUST PBL_IF_ROUND_ELSE(-18, 0)
#define WEATHER_APP_LAYOUT_BOTTOM_TEMP_X_ADJUST PBL_IF_ROUND_ELSE(4, 0)
// Arc animation: circle is off-screen to the RIGHT.
// REST_ANGLE = 3*TRIG_MAX_ANGLE/4 (9 o'clock from centre) puts the icon
// on-screen to the LEFT of the centre, which is right of screen centre.
// Both today and tomorrow icons ride the same circle with a fixed angular gap.
#define ICON_ARC_RADIUS       PBL_IF_RECT_ELSE(220, 190)
// 50°: at radius 220 the outgoing icon travels ~169px vertically + ~79px
// horizontally — enough to carry the 50px day icon fully past the screen
// corner. The old 40° sweep stopped with an ~18px sliver still on-screen at
// the bottom-right, which then popped away when the animation ended.
#define ICON_ARC_SWEEP_ANGLE  (TRIG_MAX_ANGLE * 50 / 360)
// 9 o'clock: icon is to the left of the circle centre (which is off-screen right)
#define ICON_ARC_TODAY_REST   (TRIG_MAX_ANGLE * 3 / 4)
// Timeline-style landing bounce for day text: anticipation, fast travel,
// then a tiny overshoot back into place.
#define TEXT_MOOOK_MID_FRAMES 3
#define TEXT_MOOOK_BOUNCE_BACK 4
#define ICON_ANIM_DURATION_MS 220

// Round bar layer height keeps the (removed) pull-to-refresh drawer's sizing so the
// round location bar renders unchanged: ROUND_BAR_DEPTH + 40 (old PULL_TRIGGER_PX) + 6.
#define WEATHER_APP_LAYOUT_ROUND_PULL_LAYER_HEIGHT \
  (WEATHER_APP_LAYOUT_ROUND_BAR_DEPTH + 40 + 6)

#define weather_type_get_bg_color weather_type_bg_color
#define weather_type_get_icon_res_tiny weather_type_icon_tiny_resource
#define weather_type_get_icon_res_today weather_type_icon_small_resource

#if PBL_DISPLAY_HEIGHT >= 200
static const GSize s_today_icon_size = {
  PBL_IF_ROUND_ELSE(75, 50),
  PBL_IF_ROUND_ELSE(75, 50)
};
#else
static const GSize s_today_icon_size = {25, 25};
#endif
static const GSize s_tomorrow_icon_size = {25, 25};

static bool prv_prepare_day_transition(WeatherAppLayout *layout,
                                       const WeatherLocationForecast *new_today,
                                       const WeatherLocationForecast *new_next,
                                       bool animate_down);

static int prv_draw_text(GPoint offset, int max_width, GContext *context,
                         const char *text, const GFont font,
                         GColor font_color, GTextAlignment alignment) {
  GSize size = graphics_text_layout_get_content_size(
      text, font, GRect(0, 0, max_width, 1000), GTextOverflowModeFill, alignment);
  const int height = size.h;
  const GRect box = (GRect){offset, GSize(max_width, height + 2)};
  graphics_context_set_text_color(context, font_color);
  graphics_draw_text(context, text, font, box, GTextOverflowModeFill, alignment, NULL);
  return height;
}

static void prv_draw_weather_background(const GRect *circle_bounding_box, GContext *context,
                                        GColor background_color) {
  GRect box = *circle_bounding_box;
  GPoint center = grect_center_point(&box);
  // White halo, 4px past the disc: invisible on the white field, but it
  // erases background chrome (the section rule) behind the icon art's
  // transparent edges while an icon crosses it — icons always read as in
  // front of the rule.
  graphics_context_set_fill_color(context, GColorWhite);
  graphics_fill_circle(context, center, (uint16_t)(box.size.w / 2 + 4));
  if (!gcolor_equal(background_color, GColorClear)) {
    graphics_context_set_fill_color(context, background_color);
    graphics_fill_circle(context, center, (uint16_t)(box.size.w / 2));
  }
}

static void prv_set_outgoing_weather_icon_frame(WeatherAppLayout *layout, GRect frame) {
  layer_set_frame(layout->outgoing_weather_icon_layer, frame);
}

#if WEATHER_APP_LAYOUT_USE_PDC_WEATHER_ICONS
static void prv_move_day_icons_to_root(WeatherAppLayout *layout) {
  (void)layout;
}

static void prv_move_day_icons_to_content(WeatherAppLayout *layout) {
  (void)layout;
}
#endif

static void prv_fill_high_low_temp_buffer(const int high, const int low, char *buffer,
                                          const size_t buffer_size) {
  if ((high == WEATHER_SERVICE_LOCATION_FORECAST_UNKNOWN_TEMP) &&
      (low == WEATHER_SERVICE_LOCATION_FORECAST_UNKNOWN_TEMP)) {
    snprintf(buffer, buffer_size, "--\xC2\xB0 / --\xC2\xB0");
  } else if (low == WEATHER_SERVICE_LOCATION_FORECAST_UNKNOWN_TEMP) {
    snprintf(buffer, buffer_size, "%i\xC2\xB0 / --\xC2\xB0", high);
  } else if (high == WEATHER_SERVICE_LOCATION_FORECAST_UNKNOWN_TEMP) {
    snprintf(buffer, buffer_size, "--\xC2\xB0 / %i\xC2\xB0", low);
  } else {
    snprintf(buffer, buffer_size, "%i\xC2\xB0 / %i\xC2\xB0", high, low);
  }
}

static void prv_fill_rain_value_buffer(const WeatherLocationForecast *forecast,
                                       char *buffer,
                                       const size_t buffer_size) {
  if (!buffer || buffer_size == 0) return;

  if (forecast && forecast->today_precip_mm >= 0) {
    snprintf(buffer, buffer_size, "%i%%", forecast->today_precip_mm);
  } else {
    snprintf(buffer, buffer_size, "--");
  }
}

static void prv_fill_wind_value_buffer(const WeatherLocationForecast *forecast,
                                       char *buffer,
                                       const size_t buffer_size) {
  if (!buffer || buffer_size == 0) return;

  if (forecast && forecast->today_wind_mph >= 0) {
    snprintf(buffer, buffer_size, "%imph", forecast->today_wind_mph);
  } else {
    snprintf(buffer, buffer_size, "--");
  }
}

static void prv_draw_raindrop_icon(GContext *context, GPoint origin) {
  graphics_context_set_stroke_color(context, GColorBlack);
  graphics_context_set_fill_color(context, GColorBlack);
  graphics_context_set_stroke_width(context, 1);

  // Keep this allocation-free; the metrics row is redrawn repeatedly while
  // finger-scrubbing between days.
  graphics_fill_circle(context, GPoint(origin.x + 5, origin.y + 8), 4);
  graphics_draw_circle(context, GPoint(origin.x + 5, origin.y + 8), 4);
  graphics_fill_rect(context, GRect(origin.x + 3, origin.y + 5, 5, 4), 0, GCornerNone);
  graphics_draw_line(context, GPoint(origin.x + 5, origin.y),
                     GPoint(origin.x + 9, origin.y + 6));
  graphics_draw_line(context, GPoint(origin.x + 5, origin.y),
                     GPoint(origin.x + 1, origin.y + 6));
  graphics_draw_line(context, GPoint(origin.x + 1, origin.y + 6),
                     GPoint(origin.x + 1, origin.y + 9));
  graphics_draw_line(context, GPoint(origin.x + 9, origin.y + 6),
                     GPoint(origin.x + 9, origin.y + 9));
  // Fill the apex triangle: the two Bresenham edges alone leave a white
  // notch inside the tip at 1x (system glyphs are always solid fills).
  for (int dy = 1; dy <= 6; dy++) {
    int half = dy * 4 / 6;
    graphics_draw_line(context, GPoint(origin.x + 5 - half, origin.y + dy),
                       GPoint(origin.x + 5 + half, origin.y + dy));
  }
}

static void prv_draw_wind_icon(GContext *context, GPoint origin) {
  graphics_context_set_stroke_color(context, GColorBlack);
  graphics_context_set_stroke_width(context, 2);
  // Decaying gust lengths (13/10/7) with a staggered leading edge — the
  // firmware motion-line vocabulary — instead of three equal hamburger bars.
  graphics_draw_line(context, GPoint(origin.x, origin.y + 3),
                     GPoint(origin.x + 13, origin.y + 3));
  graphics_draw_line(context, GPoint(origin.x + 2, origin.y + 7),
                     GPoint(origin.x + 12, origin.y + 7));
  graphics_draw_line(context, GPoint(origin.x, origin.y + 11),
                     GPoint(origin.x + 7, origin.y + 11));
  graphics_context_set_stroke_width(context, 1);
}

static void prv_fill_uv_value_buffer(const WeatherLocationForecast *forecast,
                                     char *buffer, const size_t buffer_size) {
  if (!buffer || buffer_size == 0) return;
  if (forecast && forecast->today_uv >= 0) {
    snprintf(buffer, buffer_size, "%i", forecast->today_uv);
  } else {
    snprintf(buffer, buffer_size, "--");
  }
}

// (Feels-like removed 2026-07-04 by request: the wind-derived "feels 21°" whisper
// cell and the round "Feels like" metric row are gone. today_feels_like_temp stays
// in the v4 schema should a real phone-supplied feels-like ever be displayed.)


#if !PBL_ROUND
// Left-aligned "UV" tally: one solid black square per UV unit (1..11), uniformly spaced. Solid black
// matches the ink weight of the raindrop/wind glyphs above it and reads instantly as a count. The
// square band is lifted to sit vertically centred on the "UV" cap height. Unknown ("--") draws
// nothing; a valid 0 shows a single hollow marker so the row never looks broken. RECT only.
static void prv_draw_uv_squares(GContext *context, GPoint origin, const char *uv,
                                const GFont font, AnimationProgress reveal) {
  if (!uv || uv[0] == '-' || uv[0] == '\0') {
    return;   // unknown / no data -> blank row
  }
  int idx = 0;
  for (const char *c = uv; *c >= '0' && *c <= '9'; c++) {
    idx = idx * 10 + (*c - '0');
  }
  if (idx > 11) idx = 11;   // clamp to the UV-index max

  // Landing delight: the squares tick in left-to-right across the back half
  // of the day-scroll animation (draw-time gating only — no allocations).
  int shown = idx;
  if (reveal < ANIMATION_NORMALIZED_MAX) {
    int32_t t = ((int32_t)reveal - (ANIMATION_NORMALIZED_MAX / 2)) * 2;
    if (t < 0) t = 0;
    shown = (int)(((int64_t)idx * t + ANIMATION_NORMALIZED_MAX - 1) /
                  ANIMATION_NORMALIZED_MAX);
  }

  const int sq = 6;                 // square size (px)
  const int gap = 3;                // uniform gap between every square (even, no grouping)
  const int sq_y = origin.y + 2;    // sit the square band on the vertical centre of the "UV" caps

  graphics_context_set_text_color(context, GColorBlack);
  graphics_draw_text(context, "UV", font, GRect(origin.x, origin.y - 5, 18, 16),
                     GTextOverflowModeFill, GTextAlignmentLeft, NULL);

  const int sq_x0 = origin.x + 20;   // squares start just right of the "UV" label
  if (idx == 0) {
    graphics_context_set_stroke_color(context, GColorBlack);
    graphics_context_set_stroke_width(context, 1);
    graphics_draw_rect(context, GRect(sq_x0, sq_y, sq, sq));   // "measured zero" marker
    return;
  }
  int x = sq_x0;
  for (int i = 0; i < shown; i++) {
    // WHO UV-index color ramp (Health-app pattern: palette color on color
    // displays, solid black on B&W): 1-2 green, 3-5 yellow, 6-7 orange,
    // 8-10 red, 11 violet.
    GColor band = (i < 2)   ? GColorIslamicGreen
                : (i < 5)   ? GColorChromeYellow
                : (i < 7)   ? GColorOrange
                : (i < 10)  ? GColorRed
                            : GColorVividViolet;
    graphics_context_set_fill_color(context, PBL_IF_COLOR_ELSE(band, GColorBlack));
    graphics_fill_rect(context, GRect(x, sq_y, sq, sq), 0, GCornerNone);
    x += sq + gap;
  }
}
#endif

static int prv_draw_metric_row(GPoint offset, int max_width, GContext *context,
                               const GFont font, const GFont value_font,
                               AnimationProgress uv_reveal,
                               const char *rain, const char *wind,
                               const char *uv) {
  const int row_h = PBL_IF_RECT_ELSE(16, 14);
  const int y = offset.y + WEATHER_APP_LAYOUT_METRIC_TEXT_Y_INSET;
  // Rows on one uniform pitch: precip + wind, then the UV tally. One rhythm
  // and one shared left rail keep the whole block on the same grid as the
  // day/temp/high-low stack above it.
  const int row_pitch = PBL_IF_RECT_ELSE(19, 18);
  const int y_top = y - 16;
  const int y_bot = y_top + row_pitch;   // precip / wind

  // Value rail: precip value + UV squares share x+20 (one label column at
  // the left rail, one value column, wind justified to the right rail).
  const int rain_text_x = offset.x + PBL_IF_RECT_ELSE(20, 14);

  graphics_context_set_text_color(context, GColorBlack);

#if !PBL_ROUND
  // Precipitation on the left rail, wind JUSTIFIED to the right text
  // rail (the separator's endpoint and both icon discs' tangent — the only
  // true rail on that side; a pinned left edge always floats).
  prv_draw_raindrop_icon(context, GPoint(offset.x, y_bot + 5));
  graphics_draw_text(context, rain, value_font,
                     GRect(rain_text_x, y_bot, 60, row_h + 4),
                     GTextOverflowModeFill, GTextAlignmentLeft, NULL);
  GSize wsz = graphics_text_layout_get_content_size(
      wind, value_font, GRect(0, 0, max_width, 32),
      GTextOverflowModeFill, GTextAlignmentLeft);
  const int wind_text_x = offset.x + max_width - wsz.w - 2;
  prv_draw_wind_icon(context, GPoint(wind_text_x - 19, y_bot + 5));
  graphics_draw_text(context, wind, value_font,
                     GRect(wind_text_x, y_bot, wsz.w + 2, row_h + 4),
                     GTextOverflowModeFill, GTextAlignmentLeft, NULL);

  // Row 3 (UV tally): "UV" + one colored square per UV unit, centered
  // between the precip/wind row above and the section rule below.
  prv_draw_uv_squares(context, GPoint(offset.x, y_bot + 24), uv, font, uv_reveal);
#else
  (void)value_font;
  (void)uv_reveal;
  prv_draw_raindrop_icon(context, GPoint(offset.x, y_bot + 2));
  const int wind_x = offset.x + 50;
  graphics_draw_text(context, rain, font, GRect(rain_text_x, y_bot, wind_x - rain_text_x - 2, row_h + 2),
                     GTextOverflowModeFill, GTextAlignmentLeft, NULL);
  prv_draw_wind_icon(context, GPoint(wind_x, y_bot + 2));
  graphics_draw_text(context, wind, font,
                     GRect(wind_x + 17, y_bot, max_width - (wind_x + 17 - offset.x), row_h + 2),
                     GTextOverflowModeFill, GTextAlignmentLeft, NULL);
  (void)uv;   // round: UV omitted from this screen
#endif
  return row_h;
}

static int prv_interpolate_text_moook(AnimationProgress progress, int from, int to) {
  static const int32_t frames_in[] = {0, 1, 20};
  static const int32_t frames_out[] = {TEXT_MOOOK_BOUNCE_BACK, 2, 1, 0};
  const int32_t num_in = sizeof(frames_in) / sizeof(frames_in[0]);
  const int32_t num_mid = TEXT_MOOOK_MID_FRAMES;
  const int32_t num_out = sizeof(frames_out) / sizeof(frames_out[0]);
  const int32_t num_total = num_in + num_mid + num_out;
  const int32_t dir = (from == to) ? 0 : ((from < to) ? 1 : -1);
  if (dir == 0 || progress >= ANIMATION_NORMALIZED_MAX) return to;

  int32_t frame_idx = (progress * num_total +
                       (ANIMATION_NORMALIZED_MAX / (2 * num_total))) /
                      ANIMATION_NORMALIZED_MAX;
  if (frame_idx < 0) frame_idx = 0;
  if (frame_idx >= num_total) frame_idx = num_total - 1;

  if (frame_idx < num_in) {
    return from + (int)(dir * frames_in[frame_idx]);
  }

  if (frame_idx < num_in + num_mid) {
    int32_t shifted = progress -
        (num_in * ANIMATION_NORMALIZED_MAX / num_total);
    int32_t mid_normalized = num_total * shifted / num_mid;
    if (mid_normalized < 0) mid_normalized = 0;
    if (mid_normalized > ANIMATION_NORMALIZED_MAX) {
      mid_normalized = ANIMATION_NORMALIZED_MAX;
    }

    const int32_t start = from + dir * frames_in[num_in - 1];
    const int32_t end = to + dir * frames_out[0];
    return (int)(start + ((end - start) * mid_normalized / ANIMATION_NORMALIZED_MAX));
  }

  return to + (int)(dir * frames_out[frame_idx - (num_in + num_mid)]);
}

static int32_t prv_interpolate_icon_landing_progress(AnimationProgress progress) {
  const int32_t max = ANIMATION_NORMALIZED_MAX;
  if (progress >= max) return max;

  const int32_t overshoot = max / 36;
  const int32_t rebound = max / 120;
  const int32_t hit = max * 86 / 100;
  const int32_t settle = max * 95 / 100;

  if ((int32_t)progress < hit) {
    return weather_scale_i32(progress, max + overshoot, hit);
  }

  if ((int32_t)progress < settle) {
    return max + overshoot -
        weather_scale_i32(progress - hit, overshoot + rebound, settle - hit);
  }

  return max - rebound +
      weather_scale_i32(progress - settle, rebound, max - settle);
}

static int32_t prv_interpolate_icon_landing_progress_down(AnimationProgress progress) {
  const int32_t max = ANIMATION_NORMALIZED_MAX;
  if (progress >= max) return max;

  const int32_t rebound = max / 140;
  const int32_t hit = max * 88 / 100;
  const int32_t settle = max * 95 / 100;

  if ((int32_t)progress < hit) {
    return weather_scale_i32(progress, max, hit);
  }

  if ((int32_t)progress < settle) {
    return max - weather_scale_i32(progress - hit, rebound, settle - hit);
  }

  return max - rebound +
      weather_scale_i32(progress - settle, rebound, max - settle);
}

static void prv_draw_fin_layer(Layer *layer, GContext *context) {
  WeatherAppLayout *layout = *(WeatherAppLayout **)layer_get_data(layer);
  if (!layout || !layout->fin_pdc) return;

  graphics_context_set_compositing_mode(context, GCompOpSet);
  gdraw_command_image_draw(context, layout->fin_pdc, GPointZero);
}

static void prv_fin_anim_update(Animation *anim, AnimationProgress progress) {
  WeatherAppLayout *layout = (WeatherAppLayout *)animation_get_context(anim);
  GRect frame = layout->fin_anim.to;
  frame.origin.x = prv_interpolate_text_moook(
      progress, layout->fin_anim.from.origin.x, layout->fin_anim.to.origin.x);
  frame.origin.y = prv_interpolate_text_moook(
      progress, layout->fin_anim.from.origin.y, layout->fin_anim.to.origin.y);
  layer_set_frame(layout->fin_layer, frame);
}

static void prv_fin_anim_stopped(Animation *anim, bool finished, void *context) {
  WeatherAppLayout *layout = (WeatherAppLayout *)context;
  if (layout) {
    layout->fin_animation = NULL;
    if (finished && layout->fin_layer) {
      layer_set_frame(layout->fin_layer, layout->fin_anim.to);
    }
  }
  animation_destroy(anim);
}

static const AnimationImplementation s_fin_anim_impl = {
  .update = prv_fin_anim_update,
};

// ---- Text animation snapshot helpers ----
// Featured temperature: current temp, or the daily high on future days.
static void prv_fill_featured_temp_buffer(const WeatherLocationForecast *f,
                                          char *buffer, size_t buffer_size) {
  if (f->current_temp == WEATHER_SERVICE_LOCATION_FORECAST_UNKNOWN_TEMP) {
    if (f->today_high != WEATHER_SERVICE_LOCATION_FORECAST_UNKNOWN_TEMP)
      snprintf(buffer, buffer_size, "%i\xC2\xB0", f->today_high);
    else
      snprintf(buffer, buffer_size, "--\xC2\xB0");
  } else {
    snprintf(buffer, buffer_size, "%i\xC2\xB0", f->current_temp);
  }
}

// Capture current forecast strings into text_anim before the forecast pointer is updated.
static void prv_snapshot_text(WeatherAppLayout *layout) {
  const WeatherLocationForecast *f = layout->forecast;
  if (f) {
    const char *lbl = (f->label && f->label[0]) ? f->label : "TODAY";
    strncpy(layout->text_anim.top_label, lbl, sizeof(layout->text_anim.top_label) - 1);
    layout->text_anim.top_label[sizeof(layout->text_anim.top_label) - 1] = '\0';

    prv_fill_featured_temp_buffer(f, layout->text_anim.top_temp,
                                  sizeof(layout->text_anim.top_temp));
    prv_fill_high_low_temp_buffer(f->today_high, f->today_low,
                                  layout->text_anim.top_highlow,
                                  sizeof(layout->text_anim.top_highlow));
    prv_fill_rain_value_buffer(f,
                               layout->text_anim.top_rain,
                               sizeof(layout->text_anim.top_rain));
    prv_fill_wind_value_buffer(f,
                               layout->text_anim.top_wind,
                               sizeof(layout->text_anim.top_wind));
    prv_fill_uv_value_buffer(f,
                             layout->text_anim.top_uv,
                             sizeof(layout->text_anim.top_uv));
  } else {
    layout->text_anim.top_label[0] = '\0';
    layout->text_anim.top_temp[0]  = '\0';
    layout->text_anim.top_highlow[0] = '\0';
    layout->text_anim.top_rain[0] = '\0';
    layout->text_anim.top_wind[0] = '\0';
    layout->text_anim.top_uv[0] = '\0';
  }

  const WeatherLocationForecast *n = layout->next_forecast;
  if (n) {
    const char *lbl = (n->label && n->label[0]) ? n->label : "TOMORROW";
    strncpy(layout->text_anim.bot_label, lbl, sizeof(layout->text_anim.bot_label) - 1);
    layout->text_anim.bot_label[sizeof(layout->text_anim.bot_label) - 1] = '\0';
    prv_fill_high_low_temp_buffer(n->today_high, n->today_low,
                                  layout->text_anim.bot_highlow,
                                  sizeof(layout->text_anim.bot_highlow));
    layout->text_anim.bot_valid = true;
  } else {
    layout->text_anim.bot_valid = false;
  }
}

// One string-driven renderer for the top-half rows, shared by the outgoing
// snapshot pass and the live pass — the row geometry lives here exactly once.
typedef struct {
  const char *label, *temp, *highlow, *rain, *wind, *uv;
} TopText;

static void prv_draw_top_rows(const WeatherAppLayout *layout, GPoint *off, int cw,
                              GContext *ctx, const TopText *t, int label_temp_gap,
                              AnimationProgress uv_reveal) {
#if PBL_ROUND
  (void)label_temp_gap;
  const int day_y = off->y + WEATHER_APP_LAYOUT_DAY_Y_SHIFT;
  GPoint line_off = GPoint(off->x + WEATHER_APP_LAYOUT_DAY_X_SHIFT, day_y);
  prv_draw_text(line_off, cw - WEATHER_APP_LAYOUT_DAY_X_SHIFT, ctx, t->label,
                layout->location_font, GColorBlack, GTextAlignmentLeft);
  line_off = GPoint(off->x + WEATHER_APP_LAYOUT_TEMP_X_SHIFT,
                    day_y + WEATHER_APP_LAYOUT_TEXT_STACK_STEP);
  prv_draw_text(line_off, cw - WEATHER_APP_LAYOUT_TEMP_X_SHIFT, ctx, t->temp,
                layout->temperature_font, GColorBlack, GTextAlignmentLeft);
  line_off = GPoint(off->x + WEATHER_APP_LAYOUT_HIGHLOW_X_SHIFT,
                    day_y + WEATHER_APP_LAYOUT_TEXT_STACK_STEP * 2);
  prv_draw_text(line_off, cw - WEATHER_APP_LAYOUT_HIGHLOW_X_SHIFT, ctx, t->highlow,
                layout->high_low_phrase_font, GColorBlack, GTextAlignmentLeft);
  line_off = GPoint(off->x + WEATHER_APP_LAYOUT_METRICS_X_SHIFT,
                    day_y + WEATHER_APP_LAYOUT_TEXT_STACK_STEP * 3 -
                        WEATHER_APP_LAYOUT_METRIC_TEXT_Y_INSET);
  prv_draw_metric_row(line_off, cw - WEATHER_APP_LAYOUT_METRICS_X_SHIFT, ctx,
                      layout->metrics_font, layout->metrics_value_font, uv_reveal,
                      t->rain, t->wind, t->uv);
#else
  GPoint line_off = GPoint(off->x + WEATHER_APP_LAYOUT_DAY_X_SHIFT, off->y);
  off->y += prv_draw_text(line_off, cw - WEATHER_APP_LAYOUT_DAY_X_SHIFT, ctx, t->label,
                          layout->location_font, GColorBlack, GTextAlignmentLeft);
  off->y += label_temp_gap;
  line_off = GPoint(off->x + WEATHER_APP_LAYOUT_TEMP_X_SHIFT,
                    off->y + WEATHER_APP_LAYOUT_TEMP_Y_SHIFT);
  off->y += prv_draw_text(line_off, cw - WEATHER_APP_LAYOUT_TEMP_X_SHIFT, ctx, t->temp,
                          layout->temperature_font, GColorBlack, GTextAlignmentLeft);
  line_off = GPoint(off->x + WEATHER_APP_LAYOUT_HIGHLOW_X_SHIFT,
                    off->y + WEATHER_APP_LAYOUT_HIGHLOW_Y_SHIFT);
  off->y += prv_draw_text(line_off, cw - WEATHER_APP_LAYOUT_HIGHLOW_X_SHIFT, ctx, t->highlow,
                          layout->high_low_phrase_font, GColorBlack, GTextAlignmentLeft);
  line_off = GPoint(off->x + WEATHER_APP_LAYOUT_METRICS_X_SHIFT,
                    off->y + WEATHER_APP_LAYOUT_METRICS_Y_SHIFT);
  prv_draw_metric_row(line_off, cw - WEATHER_APP_LAYOUT_METRICS_X_SHIFT, ctx,
                      layout->metrics_font, layout->metrics_value_font, uv_reveal,
                      t->rain, t->wind, t->uv);
#endif
}

// Draw top-half text from the snapshot (outgoing frame).
static void prv_draw_snapshot_top(const WeatherAppLayout *layout, GPoint *off,
                                   int cw, GContext *ctx) {
  const TopText t = {
    .label = layout->text_anim.top_label,
    .temp = layout->text_anim.top_temp,
    .highlow = layout->text_anim.top_highlow,
    .rain = layout->text_anim.top_rain,
    .wind = layout->text_anim.top_wind,
    .uv = layout->text_anim.top_uv,
  };
  prv_draw_top_rows(layout, off, cw, ctx, &t, PBL_IF_RECT_ELSE(2, 0),
                    ANIMATION_NORMALIZED_MAX);
}

static void prv_draw_top_half_text(const WeatherAppLayout *layout, GPoint *current_offset,
                                   int content_width, GContext *context) {
  const WeatherLocationForecast *forecast = layout->forecast;

  char temp_buffer[15] = {0};
  char highlow_buffer[15] = {0};
  char rain_buffer[12] = {0};
  char wind_buffer[16] = {0};
  char uv_buffer[12] = {0};
  prv_fill_featured_temp_buffer(forecast, temp_buffer, sizeof(temp_buffer));
  prv_fill_high_low_temp_buffer(forecast->today_high, forecast->today_low,
                                highlow_buffer, sizeof(highlow_buffer));
  prv_fill_rain_value_buffer(forecast, rain_buffer, sizeof(rain_buffer));
  prv_fill_wind_value_buffer(forecast, wind_buffer, sizeof(wind_buffer));
  prv_fill_uv_value_buffer(forecast, uv_buffer, sizeof(uv_buffer));
  const TopText t = {
    .label = (forecast->label && forecast->label[0]) ? forecast->label : "TODAY",
    .temp = temp_buffer,
    .highlow = highlow_buffer,
    .rain = rain_buffer,
    .wind = wind_buffer,
    .uv = uv_buffer,
  };
  prv_draw_top_rows(layout, current_offset, content_width, context, &t,
                    PBL_IF_RECT_ELSE(6, 0),
                    layout->text_anim.active ? layout->text_anim.progress
                                             : ANIMATION_NORMALIZED_MAX);
}

// Shared bottom-half rows (label + high/low), string-driven.
static void prv_draw_bottom_rows(const WeatherAppLayout *layout, GPoint *off, int cw,
                                 GContext *ctx, const char *label, const char *highlow,
                                 int gap) {
  off->x += WEATHER_APP_LAYOUT_BOTTOM_TEXT_X_SHIFT;
  cw -= WEATHER_APP_LAYOUT_BOTTOM_TEXT_X_SHIFT;
  off->y += gap;
  GPoint label_off = GPoint(off->x + WEATHER_APP_LAYOUT_BOTTOM_LABEL_X_ADJUST, off->y);
  off->y += prv_draw_text(label_off, cw, ctx, label,
                          layout->tomorrow_font, GColorBlack, GTextAlignmentLeft);
  GPoint temp_off = GPoint(off->x + WEATHER_APP_LAYOUT_BOTTOM_TEMP_X_ADJUST, off->y);
  prv_draw_text(temp_off, cw, ctx, highlow,
                layout->high_low_phrase_font, GColorBlack, GTextAlignmentLeft);
}

// Draw bottom-half text from the snapshot (outgoing frame).
static void prv_draw_snapshot_bot(const WeatherAppLayout *layout, GPoint *off,
                                   int cw, GContext *ctx) {
  if (!layout->text_anim.bot_valid) return;
  prv_draw_bottom_rows(layout, off, cw, ctx, layout->text_anim.bot_label,
                       layout->text_anim.bot_highlow, 6);
}

static void prv_draw_bottom_half_text(const WeatherAppLayout *layout, GPoint *current_offset,
                                      int content_width, GContext *context) {
  const WeatherLocationForecast *next = layout->next_forecast;
  if (!next) return;
  char text_buffer[15] = {0};
  prv_fill_high_low_temp_buffer(next->today_high, next->today_low,
                                text_buffer, sizeof(text_buffer));
  prv_draw_bottom_rows(layout, current_offset, content_width, context,
                       (next->label && next->label[0]) ? next->label : "TOMORROW",
                       text_buffer, PBL_IF_RECT_ELSE(2, 10));
}

static void prv_draw_circle_at_layer(Layer *icon_layer, GContext *context,
                                     WeatherType weather_type) {
  GRect frame = layer_get_frame(icon_layer);
  const GSize sz = frame.size;
  const int diam = (int)(sz.w * 14 / 10);
  GRect bg_circle = (GRect){
    .origin = GPoint(frame.origin.x + sz.w / 2 - diam / 2,
                     frame.origin.y + sz.h / 2 - diam / 2),
    .size = GSize(diam, diam),
  };
  prv_draw_weather_background(&bg_circle, context, weather_type_get_bg_color(weather_type));
}

#if WEATHER_APP_LAYOUT_USE_PDC_WEATHER_ICONS
static void prv_draw_weather_pdc_frame(const WeatherAppLayout *layout,
                                       GContext *ctx,
                                       WeatherType weather_type,
                                       GRect frame_rect) {
  int frame_index = ((int)weather_type <= WeatherType_RainAndSnow)
      ? (int)weather_type : WeatherType_Generic;
  // No set_bounds_size: the PDC's native viewbox is already the displayed size
  // (75x75) and gdraw_command_frame_draw never reads sequence->size — it draws at
  // the native coords offset by frame_rect.origin. (The sequence is also a RAM
  // clone now, so a write would be legal, but it's simply unnecessary here.)
  GDrawCommandFrame *frame =
      gdraw_command_sequence_get_frame_by_index(layout->weather_icon_pdc_sequence,
                                                frame_index);
  gdraw_command_frame_draw(ctx, layout->weather_icon_pdc_sequence, frame,
                           frame_rect.origin);
}

static GRect prv_icon_background_frame_for_icon(GRect icon_frame) {
  const GSize sz = icon_frame.size;
  const int diam = (int)(sz.w * 14 / 10);
  return (GRect){
    .origin = GPoint(icon_frame.origin.x + sz.w / 2 - diam / 2,
                     icon_frame.origin.y + sz.h / 2 - diam / 2),
    .size = GSize(diam, diam),
  };
}

static void prv_set_current_escape_icon_frame(WeatherAppLayout *layout,
                                              GRect content_icon_frame) {
  if (!layout->current_weather_escape_layer) return;
  GRect root_icon = content_icon_frame;
  root_icon.origin.x += layout->content_layer_origin.x;
  root_icon.origin.y += layout->content_layer_origin.y;
  layer_set_frame(layout->current_weather_escape_layer,
                  prv_icon_background_frame_for_icon(root_icon));
}

static void prv_hide_current_escape_icon(WeatherAppLayout *layout) {
  layout->anim_params.current_root_overlay = false;
  if (layout->current_weather_escape_layer) {
    layer_set_hidden(layout->current_weather_escape_layer, true);
  }
}

static void prv_draw_current_escape_icon(Layer *layer, GContext *ctx) {
  WeatherAppLayout *layout = *(WeatherAppLayout **)layer_get_data(layer);
  if (!layout || !layout->anim_params.current_root_overlay
      || !layout->weather_icon_pdc_sequence) {
    return;
  }
  GRect bounds = layer_get_bounds(layer);
  prv_draw_weather_background(&bounds, ctx,
      weather_type_get_bg_color(layout->anim_params.incoming_weather_type));

  GSize icon_size = layout->today_icon_rest_frame.size;
  GRect icon_rect = {
    GPoint((bounds.size.w - icon_size.w) / 2,
           (bounds.size.h - icon_size.h) / 2),
    icon_size
  };
  prv_draw_weather_pdc_frame(layout, ctx,
                             layout->anim_params.incoming_weather_type,
                             icon_rect);
}

static bool prv_weather_pdc_paused(const WeatherAppLayout *layout) {
  return layout->text_anim.active;
}

static void prv_draw_current_weather_pdc(const WeatherAppLayout *layout, GContext *ctx) {
  if (prv_weather_pdc_paused(layout)) return;
  prv_draw_weather_pdc_frame(layout, ctx, layout->forecast->current_weather_type,
                             layout->today_icon_rest_frame);
}

static void prv_set_current_weather_pdc(WeatherAppLayout *layout,
                                        const WeatherLocationForecast *today) {
  layer_set_hidden(bitmap_layer_get_layer(layout->current_weather_icon_layer),
                   today && layout->weather_icon_pdc_sequence);
}
#endif

static void prv_draw_weather_icon_backgrounds(const WeatherAppLayout *layout,
                                              GContext *context) {
  bool animating = !layer_get_hidden(layout->outgoing_weather_icon_layer);

  if (animating) {
    prv_draw_circle_at_layer(layout->outgoing_weather_icon_layer,
                             context, layout->anim_params.outgoing_weather_type);
#if WEATHER_APP_LAYOUT_USE_PDC_WEATHER_ICONS
    if (layout->text_anim.active && !layout->outgoing_weather_icon) {
      prv_draw_weather_pdc_frame(layout, context,
                                 layout->anim_params.outgoing_weather_type,
                                 layer_get_frame(layout->outgoing_weather_icon_layer));
    }
#endif
#if WEATHER_APP_LAYOUT_USE_PDC_WEATHER_ICONS
    if (!layout->anim_params.current_root_overlay) {
#else
    {
#endif
      prv_draw_circle_at_layer(bitmap_layer_get_layer(layout->current_weather_icon_layer),
                               context, layout->anim_params.incoming_weather_type);
#if WEATHER_APP_LAYOUT_USE_PDC_WEATHER_ICONS
      if (layout->text_anim.active && layout->text_anim.progress > 0) {
        prv_draw_weather_pdc_frame(layout, context,
                                   layout->anim_params.incoming_weather_type,
                                   layer_get_frame(bitmap_layer_get_layer(
                                       layout->current_weather_icon_layer)));
      }
#endif
    }
    // Draw circle for the reparented/incoming tomorrow icon during UP/DOWN day animation.
    // The layer lives on root_layer so its frame is in root coordinates; convert to
    // content-layer coordinates so the circle is drawn correctly in this context.
    // This must be done here (after white text-animation fills) so it is not erased.
    if (layout->anim_params.tomorrow_reparented || layout->anim_params.tomorrow_incoming) {
      Layer *tmr = bitmap_layer_get_layer(layout->tomorrow_weather_icon_layer);
      GRect f_root = layer_get_frame(tmr);
      GSize sz = f_root.size;
      int diam = (int)(sz.w * 14 / 10);
      GRect bg_circle = {
        GPoint(f_root.origin.x - layout->content_layer_origin.x + sz.w / 2 - diam / 2,
               f_root.origin.y - layout->content_layer_origin.y + sz.h / 2 - diam / 2),
        GSize(diam, diam)
      };
      prv_draw_weather_background(&bg_circle, context,
          weather_type_get_bg_color(layout->anim_params.tomorrow_exit_weather_type));
    }

  } else {
    if (layout->forecast) {
      prv_draw_circle_at_layer(bitmap_layer_get_layer(layout->current_weather_icon_layer),
                               context, layout->forecast->current_weather_type);
      // Glow ring removed on this screen — just the plain background disc behind the icon.
#if WEATHER_APP_LAYOUT_USE_PDC_WEATHER_ICONS
      prv_draw_current_weather_pdc(layout, context);
#endif
    }
    if (layout->next_forecast) {
      prv_draw_circle_at_layer(bitmap_layer_get_layer(layout->tomorrow_weather_icon_layer),
                               context, layout->next_forecast->current_weather_type);
    }
  }
}

static void prv_render_layout(Layer *layer, GContext *context) {
  const GRect bounds = layer_get_bounds(layer);
  const GRect content_bounds =
      grect_inset(bounds, GEdgeInsets(0, WEATHER_APP_LAYOUT_CONTENT_LAYER_HORIZONTAL_INSET, 0));
  const int content_x_offset = content_bounds.origin.x;
  const int content_width = content_bounds.size.w;

  const WeatherAppLayout *layout = *(WeatherAppLayout **)layer_get_data(layer);
  const WeatherLocationForecast *forecast = layout->forecast;

  if (!forecast) {
    return;
  }

  // Separator fixed at 2/3 of the content layer height for a consistent today:tomorrow split
  const int separator_y = bounds.size.h * 2 / 3 + WEATHER_APP_LAYOUT_SEPARATOR_Y_ADJUST;
  const int full_h = bounds.size.h;

  if (layout->text_anim.active) {
    AnimationProgress p = layout->text_anim.progress;
    bool dir_down = layout->text_anim.dir_down;

    int out_dy = prv_interpolate_text_moook(p, 0, dir_down ? -full_h : full_h);
    int in_dy  = prv_interpolate_text_moook(p, dir_down ? full_h : -full_h, 0);

    // ---- TOP HALF (region 0 → separator_y) ----
    // Fill white first so it acts as a clip: any text drawn outside is invisible.
    graphics_context_set_fill_color(context, GColorWhite);
    graphics_fill_rect(context, GRect(0, 0, bounds.size.w, separator_y), 0, GCornerNone);
    GPoint top_out = GPoint(content_x_offset, 1 + out_dy);
    prv_draw_snapshot_top(layout, &top_out, content_width, context);
    GPoint top_in = GPoint(content_x_offset, 1 + in_dy);
    prv_draw_top_half_text(layout, &top_in, content_width, context);

    // ---- BOTTOM HALF (region separator_y → full_h) ----
    graphics_context_set_fill_color(context, GColorWhite);
    graphics_fill_rect(context, GRect(0, separator_y, bounds.size.w, full_h - separator_y),
                       0, GCornerNone);
    {
      // Clip the bottom-half passes to below the separator: during the moook bounce the
      // incoming/outgoing text overshoots above it, and the un-clipped bleed used to be
      // erased by re-drawing the entire top half a second time — twice the text-layout
      // work per frame on the hold-scroll hot path.
      GDrawState saved = context->draw_state;
      const int16_t sep_abs_y =
          (int16_t)(context->draw_state.drawing_box.origin.y + separator_y);
      GRect below = context->draw_state.clip_box;
      if (below.origin.y < sep_abs_y) {
        below.size.h = (int16_t)(below.size.h - (sep_abs_y - below.origin.y));
        if (below.size.h < 0) below.size.h = 0;
        below.origin.y = sep_abs_y;
      }
      context->draw_state.clip_box = below;
      if (layout->text_anim.bot_valid) {
        GPoint bot_out = GPoint(content_x_offset, separator_y + out_dy);
        prv_draw_snapshot_bot(layout, &bot_out, content_width, context);
      }
      if (layout->next_forecast) {
        GPoint bot_in = GPoint(content_x_offset, separator_y + in_dy);
        prv_draw_bottom_half_text(layout, &bot_in, content_width, context);
      }
      context->draw_state = saved;
    }

  } else {
    // Static (no text animation active)
    GPoint current_offset = GPoint(content_x_offset, PBL_IF_RECT_ELSE(-5, 1));
    prv_draw_top_half_text(layout, &current_offset, content_width, context);

    if (layout->next_forecast) {
      current_offset = GPoint(content_x_offset, separator_y);
      prv_draw_bottom_half_text(layout, &current_offset, content_width, context);
    } else if (layout->forecast && layout->fin_pdc && !layout->icon_animation) {
      // Handled by the Timeline Fin bitmap layer.
    }
  }

  // Timeline-style 2px section rule: after the sliding text (whose erase
  // pass would wipe an early draw) but BEFORE the discs — the white halo
  // under each disc (prv_draw_weather_background) then erases it locally,
  // so crossing icons always read as IN FRONT of the rule.
  graphics_context_set_fill_color(context, GColorBlack);
  graphics_fill_rect(context,
                     GRect(content_x_offset + 1, separator_y - 1,
                           bounds.size.w - (2 * content_x_offset) - 2, 2),
                     0, GCornerNone);

  prv_draw_weather_icon_backgrounds(layout, context);
}

// Draw the standard location + time content at x/y offsets (for sliding).
static void prv_draw_location_bar_content(GContext *ctx, const WeatherAppLayout *layout,
                                          GRect bounds, GFont font, int x_off, int y_off) {
  const int safe = WEATHER_APP_LAYOUT_LOCATION_BAR_TEXT_INSET;
  char time_str[8];
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  if (clock_is_24h_style()) {
    strftime(time_str, sizeof(time_str), "%H:%M", t);
  } else {
    strftime(time_str, sizeof(time_str), "%I:%M", t);
    if (time_str[0] == '0') memmove(time_str, time_str + 1, sizeof(time_str) - 1);
  }

#if PBL_ROUND
  char city_name[sizeof(layout->location_name)];
  size_t city_len = 0;
  while (layout->location_name[city_len]
      && layout->location_name[city_len] != ','
      && city_len < sizeof(city_name) - 1) {
    city_name[city_len] = layout->location_name[city_len];
    city_len++;
  }
  while (city_len > 0 && city_name[city_len - 1] == ' ') {
    city_len--;
  }
  city_name[city_len] = '\0';

  graphics_draw_text(ctx, time_str, font,
                     GRect(safe + x_off, WEATHER_APP_LAYOUT_ROUND_TIME_TEXT_Y + y_off,
                           bounds.size.w - (safe * 2), 19),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  if (city_name[0]) {
    graphics_draw_text(ctx, city_name, font,
                       GRect(safe + x_off, WEATHER_APP_LAYOUT_ROUND_LOCATION_TEXT_Y + y_off,
                             bounds.size.w - (safe * 2), 19),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  }
#else
  const int text_y = WEATHER_APP_LAYOUT_LOCATION_BAR_TEXT_Y;
  if (layout->location_name[0]) {
    graphics_draw_text(ctx, layout->location_name, font,
                       GRect(safe + 4 + x_off, text_y + y_off,
                             bounds.size.w - (safe * 2) - 52,
                             bounds.size.h - text_y),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  }
  graphics_draw_text(ctx, time_str, font,
                      GRect(bounds.size.w - safe - 50 + x_off,
                            text_y + y_off, 48, bounds.size.h - text_y),
                      GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
#endif
}

static void prv_draw_location_bar_background(GContext *ctx, GRect bounds) {
  graphics_context_set_fill_color(ctx, PBL_IF_COLOR_ELSE(GColorDarkGray, GColorBlack));
#if PBL_ROUND
  const int radius = WEATHER_APP_LAYOUT_ROUND_BAR_RADIUS;
  const int center_y = WEATHER_APP_LAYOUT_ROUND_BAR_DEPTH - radius +
                       WEATHER_APP_LAYOUT_ROUND_BAR_Y_ADJUST;
  graphics_fill_circle(ctx, GPoint(bounds.size.w / 2, center_y), radius);
#else
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);
#endif
}

static void prv_draw_location_bar(Layer *layer, GContext *ctx) {
  const WeatherAppLayout *layout = *(WeatherAppLayout **)layer_get_data(layer);
  GRect bounds = layer_get_bounds(layer);
  prv_draw_location_bar_background(ctx, bounds);
  graphics_context_set_text_color(ctx, GColorWhite);
  GFont font = fonts_get_system_font(PBL_IF_ROUND_ELSE(FONT_KEY_GOTHIC_14_BOLD,
                                                       FONT_KEY_GOTHIC_18_BOLD));
  prv_draw_location_bar_content(ctx, layout, bounds, font, 0, 0);
}

// ---- Arc animation ----

static GRect prv_icon_frame_at_angle(GPoint circle_center, int32_t radius,
                                      int32_t angle, GSize icon_size) {
  // Pebble trig: 0=up (12 o'clock), increases clockwise.
  // x = cx + sin(angle)*r,  y = cy - cos(angle)*r
  int16_t cx = circle_center.x +
      (int16_t)((int32_t)sin_lookup(angle) * radius / TRIG_MAX_RATIO);
  int16_t cy = circle_center.y -
      (int16_t)((int32_t)cos_lookup(angle) * radius / TRIG_MAX_RATIO);
  return (GRect){
    .origin = GPoint(cx - icon_size.w / 2, cy - icon_size.h / 2),
    .size = icon_size,
  };
}


// Lerp between two rects: centres and sizes interpolate together (p in 0..MAX).
static GRect prv_lerp_rect_center(GRect a, GRect b, int32_t p) {
  const int16_t a_cx = (int16_t)(a.origin.x + a.size.w / 2);
  const int16_t a_cy = (int16_t)(a.origin.y + a.size.h / 2);
  const int16_t b_cx = (int16_t)(b.origin.x + b.size.w / 2);
  const int16_t b_cy = (int16_t)(b.origin.y + b.size.h / 2);
  const int16_t cx = a_cx + (int16_t)((int32_t)(b_cx - a_cx) * p / ANIMATION_NORMALIZED_MAX);
  const int16_t cy = a_cy + (int16_t)((int32_t)(b_cy - a_cy) * p / ANIMATION_NORMALIZED_MAX);
  const int16_t sw = (int16_t)((int32_t)a.size.w +
      (int32_t)(b.size.w - a.size.w) * p / ANIMATION_NORMALIZED_MAX);
  const int16_t sh = (int16_t)((int32_t)a.size.h +
      (int32_t)(b.size.h - a.size.h) * p / ANIMATION_NORMALIZED_MAX);
  return (GRect){ .origin = GPoint(cx - sw / 2, cy - sh / 2), .size = GSize(sw, sh) };
}

// Lerp only the origin between two equal-size rects.
static GRect prv_lerp_rect_origin(GRect from, GRect to, int32_t p, GSize sz) {
  int16_t x = from.origin.x +
      (int16_t)((int32_t)(to.origin.x - from.origin.x) * p / ANIMATION_NORMALIZED_MAX);
  int16_t y = from.origin.y +
      (int16_t)((int32_t)(to.origin.y - from.origin.y) * p / ANIMATION_NORMALIZED_MAX);
  return (GRect){ GPoint(x, y), sz };
}

// The tomorrow rest frame translated into root-layer coordinates.
static GRect prv_tomorrow_rest_root(const WeatherAppLayout *layout) {
  return (GRect){
    GPoint(layout->content_layer_origin.x + layout->tomorrow_icon_rest_frame.origin.x,
           layout->content_layer_origin.y + layout->tomorrow_icon_rest_frame.origin.y),
    layout->tomorrow_icon_rest_frame.size
  };
}

static void prv_apply_day_transition_progress(WeatherAppLayout *layout,
                                              AnimationProgress progress) {
  layout->text_anim.progress = progress;
  const int32_t motion_p = layout->anim_params.animate_down
      ? prv_interpolate_icon_landing_progress_down(progress)
      : prv_interpolate_icon_landing_progress(progress);

  GPoint cc      = layout->anim_params.circle_center;
  int32_t r      = layout->anim_params.radius;
  GSize today_sz = layout->today_icon_rest_frame.size;

  GRect in_frame;
  GRect out_frame;

  if (layout->anim_params.animate_down) {
    // DOWN — roles swapped vs UP:
    //   outgoing_weather_icon_layer (scaler)      = INCOMING: scales up from tomorrow_rest
    //   current_weather_icon_layer  (BitmapLayer) = OUTGOING: sweeps along arc and exits

    // Incoming (scaler): straight-line from tomorrow_rest → today_rest while scaling up.
    // This is the exact reverse of UP’s outgoing shrink path — same motion, opposite direction.
    in_frame = prv_lerp_rect_center(layout->tomorrow_icon_rest_frame,
                                    layout->today_icon_rest_frame, motion_p);

    // Outgoing (BitmapLayer, old today): sweeps CW along arc at full size and exits.
    int32_t out_angle = layout->anim_params.outgoing_start_angle +
        weather_scale_i32(layout->anim_params.outgoing_end_angle -
                              layout->anim_params.outgoing_start_angle,
                          motion_p, ANIMATION_NORMALIZED_MAX);
    out_frame = prv_icon_frame_at_angle(cc, r, out_angle, today_sz);

    // Roles swapped: scaler gets in_frame, BitmapLayer gets out_frame.
    prv_set_outgoing_weather_icon_frame(layout, in_frame);
    layer_set_frame(bitmap_layer_get_layer(layout->current_weather_icon_layer), out_frame);
#if WEATHER_APP_LAYOUT_USE_PDC_WEATHER_ICONS
    if (layout->anim_params.current_root_overlay) {
      prv_set_current_escape_icon_frame(layout, out_frame);
    }
#endif

    // Animate new tomorrow icon in from off-screen arc position → tomorrow rest slot.
    if (layout->anim_params.tomorrow_incoming) {
      GSize tmr_sz2 = layout->tomorrow_icon_rest_frame.size;
      GPoint cc_root = GPoint(
          layout->anim_params.circle_center.x + layout->content_layer_origin.x,
          layout->anim_params.circle_center.y + layout->content_layer_origin.y);
      GRect tmr_arc_start = prv_icon_frame_at_angle(cc_root, layout->anim_params.radius,
                                                     layout->anim_params.incoming_start_angle,
                                                     tmr_sz2);
      layer_set_frame(bitmap_layer_get_layer(layout->tomorrow_weather_icon_layer),
                      prv_lerp_rect_origin(tmr_arc_start, prv_tomorrow_rest_root(layout),
                                           motion_p, tmr_sz2));
    }
  } else {
    // UP: incoming (previous day) arrives full-size along arc, no scaling
    int32_t in_angle = layout->anim_params.incoming_start_angle +
        weather_scale_i32(ICON_ARC_TODAY_REST -
                              layout->anim_params.incoming_start_angle,
                          motion_p, ANIMATION_NORMALIZED_MAX);
    in_frame = prv_icon_frame_at_angle(cc, r, in_angle, today_sz);

    // Outgoing (old today) shrinks and slides straight to tomorrow slot
    out_frame = prv_lerp_rect_center(layout->today_icon_rest_frame,
                                     layout->tomorrow_icon_rest_frame, motion_p);

    prv_set_outgoing_weather_icon_frame(layout, out_frame);
    layer_set_frame(bitmap_layer_get_layer(layout->current_weather_icon_layer), in_frame);
#if WEATHER_APP_LAYOUT_USE_PDC_WEATHER_ICONS
    if (layout->anim_params.current_root_overlay) {
      prv_set_current_escape_icon_frame(layout, in_frame);
    }
#endif

    // Also sweep the reparented tomorrow icon off-screen along the same arc as the
    // outgoing today icon — faster exit so it clears the screen early.
    if (layout->anim_params.tomorrow_reparented) {
      GSize tmr_sz2 = layout->tomorrow_icon_rest_frame.size;
      GPoint cc_root = GPoint(
          layout->anim_params.circle_center.x + layout->content_layer_origin.x,
          layout->anim_params.circle_center.y + layout->content_layer_origin.y);
      GRect tmr_end = prv_icon_frame_at_angle(cc_root, layout->anim_params.radius,
                                               layout->anim_params.outgoing_end_angle, tmr_sz2);

      // Faster exit: accelerate so it's off-screen well before animation ends.
      int32_t fast_p = motion_p * 5 / 3;
      if (fast_p > ANIMATION_NORMALIZED_MAX) fast_p = ANIMATION_NORMALIZED_MAX;

      layer_set_frame(bitmap_layer_get_layer(layout->tomorrow_weather_icon_layer),
                      prv_lerp_rect_origin(prv_tomorrow_rest_root(layout), tmr_end,
                                           fast_p, tmr_sz2));
    }
  }

  layer_mark_dirty(layout->root_layer);
}


// Destroy *slot, load the day/tiny icon for f (NULL f -> no bitmap), point bl at it.
// Unconditional: on round the CURRENT icon goes through the PDC path, but the
// tomorrow icon is still a bitmap and reloads through here.
static void prv_reload_icon(GBitmap **slot, BitmapLayer *bl,
                            const WeatherLocationForecast *f, bool tiny) {
  if (*slot) {
    gbitmap_destroy(*slot);
    *slot = NULL;
  }
  if (f) {
    *slot = gbitmap_create_with_resource(
        tiny ? weather_type_get_icon_res_tiny(f->current_weather_type)
             : weather_type_get_icon_res_today(f->current_weather_type));
  }
  bitmap_layer_set_bitmap(bl, *slot);
}

static void prv_icon_anim_update(Animation *anim, AnimationProgress progress) {
  WeatherAppLayout *layout = (WeatherAppLayout *)animation_get_context(anim);
  prv_apply_day_transition_progress(layout, progress);
}

static void prv_icon_anim_stopped(Animation *anim, bool finished, void *context) {
  WeatherAppLayout *layout = (WeatherAppLayout *)context;
  if (layout && layout->icon_animation) {
    layout->icon_animation = NULL;
    layout->text_anim.active = false;
#if WEATHER_APP_LAYOUT_USE_PDC_WEATHER_ICONS
    prv_move_day_icons_to_content(layout);
    prv_hide_current_escape_icon(layout);
#endif

    if (layout->anim_params.animate_down) {
      // Layer roles were swapped during DOWN animation:
      //   outgoing_weather_icon_layer held the incoming (tiny scaler) bitmap.
      //   current_weather_icon_layer  held the outgoing (old today) bitmap.
      // Now reload current_weather_icon_layer with the new today icon so the
      // resting state is correct once we snap the frame.
#if !WEATHER_APP_LAYOUT_USE_PDC_WEATHER_ICONS
      prv_reload_icon(&layout->current_weather_icon, layout->current_weather_icon_layer,
                      layout->forecast, false);
#endif
      // The tiny scaler bitmap is no longer needed.
      if (layout->outgoing_weather_icon) {
        gbitmap_destroy(layout->outgoing_weather_icon);
        layout->outgoing_weather_icon = NULL;
      }
    }
    // Snap incoming icon to exact today rest position
    layer_set_frame(bitmap_layer_get_layer(layout->current_weather_icon_layer),
                    layout->today_icon_rest_frame);
    // Hide the outgoing (old today)
    layer_set_hidden(layout->outgoing_weather_icon_layer, true);
    // Reparent tomorrow icon back to content_layer if it was animated
    if (layout->anim_params.tomorrow_reparented || layout->anim_params.tomorrow_incoming) {
      bool was_reparented = layout->anim_params.tomorrow_reparented;
      Layer *tmr = bitmap_layer_get_layer(layout->tomorrow_weather_icon_layer);
      layer_remove_from_parent(tmr);
      layer_add_child(layout->content_layer, tmr);
      layer_set_frame(tmr, layout->tomorrow_icon_rest_frame);
      layout->anim_params.tomorrow_reparented = false;
      layout->anim_params.tomorrow_incoming = false;
      // For exit (UP navigation), the old bitmap was kept alive during animation.
      // Now load the correct new tomorrow bitmap.
      if (was_reparented) {
        prv_reload_icon(&layout->tomorrow_weather_icon, layout->tomorrow_weather_icon_layer,
                        layout->next_forecast, true);
      }
    }
    // Restore the static tomorrow icon
    layer_set_hidden(bitmap_layer_get_layer(layout->tomorrow_weather_icon_layer), false);
#if WEATHER_APP_LAYOUT_USE_PDC_WEATHER_ICONS
    prv_set_current_weather_pdc(layout, layout->forecast);
#endif
    layer_mark_dirty(layout->root_layer);

    // If there is no next forecast, the FIN was already started in
    // weather_app_layout_animate — nothing more to do here.
  }
  animation_destroy(anim);
}

static const AnimationImplementation s_icon_anim_impl = {
  .update = prv_icon_anim_update,
};

// ---- Right-swipe list transition animation ----
// Icons fly diagonally to their list-screen positions with squash-stretch.
// All text is hidden (transitioning_to_list flag gates prv_render_layout).
// Rows 0 and 1 positions match forecast_list.c's draw formula exactly so
// the instant window-push is invisible.

#define LIST_ROWS_VISIBLE     4
#define LIST_ICON_X           8
#if defined(PBL_PLATFORM_GABBRO)
#define LIST_GABBRO_ROW_PITCH   45
#define LIST_GABBRO_CENTER_Y_SHIFT 0
#define LIST_GABBRO_ICON_BASE_X 10
#define LIST_GABBRO_TOP_FOCUS_GAP 21
#define LIST_GABBRO_BOTTOM_FOCUS_MARGIN 48
#define LIST_GABBRO_CURVE_BOOST_DIVISOR 800
#endif

#if defined(PBL_PLATFORM_GABBRO)
#endif


// Shared boilerplate: create + duration + linear curve + impl + stopped(ctx) + schedule.
static Animation *prv_start_anim(uint32_t dur_ms, const AnimationImplementation *impl,
                                 AnimationStoppedHandler stopped, void *ctx) {
  Animation *a = animation_create();
  animation_set_duration(a, dur_ms);
  animation_set_curve(a, AnimationCurveLinear);
  animation_set_implementation(a, impl);
  animation_set_handlers(a, (AnimationHandlers){ .stopped = stopped }, ctx);
  animation_schedule(a);
  return a;
}

// Move the Timeline Fin marker from the next scroll slot into its resting spot,
// matching Timeline's single frame animation rather than a delayed two-phase slide.
static void prv_animate_fin_in(WeatherAppLayout *layout, uint32_t total_ms) {
  if (!layout->fin_layer || !layout->fin_pdc) return;
  if (layout->fin_animation) {
    animation_unschedule(layout->fin_animation);
    layout->fin_animation = NULL;
  }

  Layer *fin = layout->fin_layer;
  GRect cl = layer_get_frame(layout->content_layer);
  GSize fin_size = gdraw_command_image_get_bounds_size(layout->fin_pdc);
  GRect to = (GRect){
    GPoint(cl.origin.x + (cl.size.w - fin_size.w) / 2,
           cl.origin.y + cl.size.h - fin_size.h - 12),
    fin_size
  };
  GRect from = to;
  from.origin.y += cl.size.h / 3;

  layout->fin_anim.from = from;
  layout->fin_anim.to = to;
  layer_set_frame(fin, from);
  layer_set_hidden(fin, false);

  layout->fin_animation = prv_start_anim(total_ms, &s_fin_anim_impl,
                                         prv_fin_anim_stopped, layout);
}

// Per-pixel scaler for the outgoing icon, identical technique to the map app's zoom path.
// Walks the parent-layer chain to find screen-absolute coords, then captures the framebuffer
// and writes scaled pixels directly — the only way to truly scale a bitmap on Pebble.
static void prv_draw_bitmap_scaled_to_root(GContext *ctx, GBitmap *src, GRect root_frame) {
  if (!src) return;

  // Use layer_get_frame for BOTH position and animated size.
  // layer_get_bounds does not auto-update when layer_set_frame shrinks the layer,
  // so layer_get_frame is the only reliable source of the current animated size.
  int dst_w = root_frame.size.w;
  int dst_h = root_frame.size.h;
  if (dst_w <= 0 || dst_h <= 0) return;

  GRect src_bounds = gbitmap_get_bounds(src);
  int src_w = src_bounds.size.w;
  int src_h = src_bounds.size.h;
  if (src_w <= 0 || src_h <= 0) return;

  uint8_t *sdata = gbitmap_get_data(src);
  uint16_t sbpr  = gbitmap_get_bytes_per_row(src);

  // Pebble tools compile small-colour PNGs as 4-bit or 2-bit palette bitmaps,
  // not GBitmapFormat8Bit. We must look up each pixel's palette entry to get
  // the actual GColor8 ARGB byte — otherwise we read raw palette indices and
  // get garbage colours (including the grey silhouette this bug caused).
  GBitmapFormat fmt = gbitmap_get_format(src);
  GColor *palette   = gbitmap_get_palette(src);  // NULL for non-palette formats

  GBitmap *fb = graphics_capture_frame_buffer(ctx);
  if (!fb) return;
  GRect fbb = gbitmap_get_bounds(fb);
  int fb_w = fbb.size.w;
  int fb_h = fbb.size.h;

  int32_t sx_step = ((int32_t)src_w << 16) / dst_w;
  int32_t sy_step = ((int32_t)src_h << 16) / dst_h;

  int32_t sy_fp = sy_step >> 1;
  for (int dy = 0; dy < dst_h; dy++, sy_fp += sy_step) {
    int sy = sy_fp >> 16;
    if (sy >= src_h) sy = src_h - 1;
    int ay = root_frame.origin.y + dy;
    if (ay < 0 || ay >= fb_h) continue;

    uint8_t *srow = sdata + (uint32_t)sy * sbpr;
    GBitmapDataRowInfo row = gbitmap_get_data_row_info(fb, ay);

    int32_t sx_fp2 = sx_step >> 1;
    for (int dx = 0; dx < dst_w; dx++, sx_fp2 += sx_step) {
      int sx = sx_fp2 >> 16;
      if (sx >= src_w) sx = src_w - 1;
      int ax = root_frame.origin.x + dx;
      if (ax < 0 || ax >= fb_w) continue;
      if (ax < row.min_x || ax > row.max_x) continue;

      uint8_t pixel;
      if (fmt == GBitmapFormat8Bit || fmt == GBitmapFormat8BitCircular) {
        // Direct 8-bit ARGB — one byte per pixel
        pixel = srow[sx];
      } else if (fmt == GBitmapFormat4BitPalette) {
        // Two pixels packed per byte; high nibble = left (even) pixel
        uint8_t raw = srow[sx >> 1];
        uint8_t idx = (sx & 1) ? (raw & 0xF) : (raw >> 4);
        pixel = palette ? palette[idx].argb : 0;
      } else if (fmt == GBitmapFormat2BitPalette) {
        // Four pixels packed per byte; bits 7:6 = leftmost pixel
        uint8_t raw = srow[sx >> 2];
        uint8_t idx = (raw >> (6 - ((sx & 3) << 1))) & 0x3;
        pixel = palette ? palette[idx].argb : 0;
      } else if (fmt == GBitmapFormat1BitPalette) {
        // Eight pixels per byte; bit 7 = leftmost pixel
        uint8_t raw = srow[sx >> 3];
        uint8_t idx = (raw >> (7 - (sx & 7))) & 0x1;
        pixel = palette ? palette[idx].argb : 0;
      } else {
        // GBitmapFormat1Bit: set bit = opaque white
        uint8_t raw = srow[sx >> 3];
        pixel = ((raw >> (7 - (sx & 7))) & 0x1) ? 0xFF : 0x00;
      }

      // Top 2 bits are the alpha channel; 0b00 = transparent, skip it
      if (pixel >> 6) row.data[ax] = pixel;
    }
  }

  graphics_release_frame_buffer(ctx, fb);
}

static void prv_draw_outgoing_icon_scaled(Layer *layer, GContext *ctx) {
  WeatherAppLayout *layout = *(WeatherAppLayout **)layer_get_data(layer);
  GRect root_frame = layer_get_frame(layer);
  root_frame.origin.x += layout->content_layer_origin.x;
  root_frame.origin.y += layout->content_layer_origin.y;
  prv_draw_bitmap_scaled_to_root(ctx, layout->outgoing_weather_icon, root_frame);
}

static GRect prv_fin_rest_frame(WeatherAppLayout *layout) {
  GRect cl = layer_get_frame(layout->content_layer);
  GSize fin_size = layout->fin_pdc ? gdraw_command_image_get_bounds_size(layout->fin_pdc)
                                      : GSize(0, 0);
  return (GRect){
    GPoint(cl.origin.x + (cl.size.w - fin_size.w) / 2,
           cl.origin.y + cl.size.h - fin_size.h - 12),
    fin_size
  };
}

static void prv_restore_fin_rest(WeatherAppLayout *layout) {
  if (!layout->fin_layer || !layout->fin_pdc) return;
  if (layout->fin_animation) {
    animation_unschedule(layout->fin_animation);
    layout->fin_animation = NULL;
  }
  if (layout->next_forecast || !layout->fin_allowed) {
    layer_set_hidden(layout->fin_layer, true);
    return;
  }
  layer_set_frame(layout->fin_layer, prv_fin_rest_frame(layout));
  layer_set_hidden(layout->fin_layer, false);
}

void weather_app_layout_init(WeatherAppLayout *layout, const GRect *frame) {
#if PBL_DISPLAY_HEIGHT >= 200
#if PBL_ROUND
  layout->location_font        = fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);
#else
  // Emery resolves Timeline's Header role to GOTHIC_24_BOLD (system_theme
  // PreferredContentSizeLarge) — match it so the day labels sit on the same
  // tier as every pin card the user sees.
  layout->location_font        = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
#endif
  layout->temperature_font     = fonts_get_system_font(FONT_KEY_LECO_36_BOLD_NUMBERS);
  layout->high_low_phrase_font = fonts_get_system_font(
      PBL_IF_RECT_ELSE(FONT_KEY_GOTHIC_24, FONT_KEY_GOTHIC_18));
  layout->metrics_font         = fonts_get_system_font(FONT_KEY_GOTHIC_14);
  layout->metrics_value_font   = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  layout->tomorrow_font        = fonts_get_system_font(
      PBL_IF_RECT_ELSE(FONT_KEY_GOTHIC_24_BOLD, FONT_KEY_GOTHIC_18_BOLD));
#else
  layout->location_font        = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  layout->metrics_value_font   = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
  layout->temperature_font     = fonts_get_system_font(FONT_KEY_LECO_26_BOLD_NUMBERS_AM_PM);
  layout->high_low_phrase_font = fonts_get_system_font(FONT_KEY_GOTHIC_18);
  layout->metrics_font         = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
  layout->tomorrow_font        = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
#endif

  layout->fin_pdc = NULL;
  layout->fin_layer = NULL;
  layout->fin_animation = NULL;
  // Real timeline 'fin' flag (END_OF_TIMELINE PDC, from Pebble_50x50_Fin.svg).
  // A system-app PDC is mmap'd READ-ONLY in flash, so clone it into a writable
  // heap copy before any draw (same trap as the weather-icons sequence).
  GDrawCommandImage *fin_raw =
      gdraw_command_image_create_with_resource(RESOURCE_ID_END_OF_TIMELINE);
  layout->fin_pdc = fin_raw ? gdraw_command_image_clone(fin_raw) : NULL;
  if (fin_raw) {
    gdraw_command_image_destroy(fin_raw);  // munmaps the read-only flash mapping
  }
#if WEATHER_APP_LAYOUT_USE_PDC_WEATHER_ICONS
  layout->weather_icon_pdc_sequence = NULL;
  layout->current_weather_escape_layer = NULL;
  layout->anim_params.current_root_overlay = false;
#endif

  layout->root_layer = layer_create_with_data(*frame, sizeof(WeatherAppLayout *));
  *(WeatherAppLayout **)layer_get_data(layout->root_layer) = layout;

  // Location bar: full-width strip at the very top of the screen.
  // Taller than the forecast-list bar (MAIN_BAR_HEIGHT vs LOCATION_BAR_HEIGHT)
  // for better readability on the main screen.
  const GRect location_bar_frame = GRect(
      frame->origin.x,
      frame->origin.y + WEATHER_APP_LAYOUT_BAR_LAYER_Y,
      frame->size.w,
      PBL_IF_ROUND_ELSE(WEATHER_APP_LAYOUT_ROUND_PULL_LAYER_HEIGHT,
                        WEATHER_APP_LAYOUT_BAR_LAYER_HEIGHT));
  layout->location_bar_layer = layer_create_with_data(location_bar_frame, sizeof(WeatherAppLayout *));
  *(WeatherAppLayout **)layer_get_data(layout->location_bar_layer) = layout;
  layer_set_update_proc(layout->location_bar_layer, prv_draw_location_bar);
  // Added to root_layer last (after fin_layer) so it renders on top of everything.

  // Down-arrow removed — location bar provides sufficient bottom UI.
  layout->down_arrow_layer = NULL;

  // Content layer fills the root below the location bar.
  const int content_layer_side_padding = PBL_IF_RECT_ELSE(5, 12);
  GRect content_layer_frame = grect_inset(
      *frame, GEdgeInsets(WEATHER_APP_LAYOUT_MAIN_CONTENT_TOP,
                          content_layer_side_padding,
                          -WEATHER_APP_LAYOUT_CONTENT_Y_SHIFT));
  content_layer_frame.origin.y += WEATHER_APP_LAYOUT_CONTENT_Y_SHIFT;

  layout->content_layer = layer_create_with_data(content_layer_frame, sizeof(WeatherAppLayout *));
  WeatherAppLayout **stored = layer_get_data(layout->content_layer);
  *stored = layout;
  layer_set_update_proc(layout->content_layer, prv_render_layout);
  // Don't clip children/drawing to the content frame: the day-scroll arc and
  // the vertical text slide must run to the REAL screen edges. Everything
  // that overshoots the top passes UNDER the opaque location bar (added
  // after us, so it draws on top); the framebuffer bounds clip the rest.
  // With clipping on, the icons visibly vanished at the content boundary
  // 30px below the top edge instead of sliding off-screen.
  layer_set_clips(layout->content_layer, false);
  layer_add_child(layout->root_layer, layout->content_layer);

  // Store the screen-absolute origin of the content layer for the per-pixel scaler.
  // root_layer is at *frame.origin (0,0 for fullscreen), content_layer is inset from that.
  layout->content_layer_origin = GPoint(
      frame->origin.x + content_layer_frame.origin.x,
      frame->origin.y + content_layer_frame.origin.y);

  // Weather icon layers — right-aligned, nudged inward by an extra 8px
  const int icon_layer_margin_top = PBL_IF_RECT_ELSE(16, 8);
  const int icon_x_inset = WEATHER_APP_LAYOUT_CONTENT_LAYER_HORIZONTAL_INSET + 8;
  const int icon_y_adjust = WEATHER_APP_LAYOUT_ICON_Y_ADJUST;
  const int today_icon_x_adjust = WEATHER_APP_LAYOUT_TODAY_ICON_X_ADJUST;
  const int today_icon_y_adjust = WEATHER_APP_LAYOUT_TODAY_ICON_Y_ADJUST;
  const int tomorrow_icon_x_adjust = WEATHER_APP_LAYOUT_TOMORROW_ICON_X_ADJUST;
  const int tomorrow_icon_y_adjust = WEATHER_APP_LAYOUT_TOMORROW_ICON_Y_ADJUST;
  const int content_separator_y = content_layer_frame.size.h * 2 / 3 +
                                  WEATHER_APP_LAYOUT_SEPARATOR_Y_ADJUST;

  GRect today_icon_frame = (GRect){
    .origin = GPoint(content_layer_frame.size.w - s_today_icon_size.w - icon_x_inset -
                     today_icon_x_adjust,
                     content_layer_frame.origin.y + icon_layer_margin_top + icon_y_adjust +
                     today_icon_y_adjust),
    .size = s_today_icon_size,
  };
  layout->today_icon_rest_frame = today_icon_frame;
  layout->current_weather_icon_layer = bitmap_layer_create(today_icon_frame);
  bitmap_layer_set_compositing_mode(layout->current_weather_icon_layer, GCompOpSet);
  // current_weather_icon_layer carries the incoming (new today) — centred so
  // it looks like a grow/zoom as the frame expands during the animation
  bitmap_layer_set_alignment(layout->current_weather_icon_layer, GAlignCenter);
  layer_add_child(layout->content_layer,
                  bitmap_layer_get_layer(layout->current_weather_icon_layer));

  // Outgoing icon layer: plain Layer with per-pixel framebuffer scaler.
  // This is the same technique the map app uses for zoom — it's the only way
  // to truly scale (not crop) a bitmap on Pebble hardware.
  layout->outgoing_weather_icon_layer =
      layer_create_with_data(today_icon_frame, sizeof(WeatherAppLayout *));
  *(WeatherAppLayout **)layer_get_data(layout->outgoing_weather_icon_layer) = layout;
  layer_set_update_proc(layout->outgoing_weather_icon_layer, prv_draw_outgoing_icon_scaled);
  layer_set_hidden(layout->outgoing_weather_icon_layer, true);
  layer_add_child(layout->content_layer, layout->outgoing_weather_icon_layer);

  GRect tomorrow_icon_frame = (GRect){
    .origin = GPoint(content_layer_frame.size.w - s_tomorrow_icon_size.w - icon_x_inset -
                     tomorrow_icon_x_adjust,
                     content_separator_y + 16 + icon_y_adjust + tomorrow_icon_y_adjust),
    .size = s_tomorrow_icon_size,
  };
  layout->tomorrow_icon_rest_frame = tomorrow_icon_frame;
  layout->tomorrow_weather_icon_layer = bitmap_layer_create(tomorrow_icon_frame);
  bitmap_layer_set_compositing_mode(layout->tomorrow_weather_icon_layer, GCompOpSet);
  layer_add_child(layout->content_layer,
                  bitmap_layer_get_layer(layout->tomorrow_weather_icon_layer));

#if WEATHER_APP_LAYOUT_USE_PDC_WEATHER_ICONS
  // A system app's PDC resource is mmap'd directly into READ-ONLY flash (no RAM
  // copy), so clone it into a writable heap copy — otherwise any write to the
  // sequence (e.g. gdraw_command_sequence_set_bounds_size -> sequence->size = ...)
  // stores into read-only flash and faults the app on launch. (The globe uses the
  // same clone-into-RAM pattern for its writable sequence.)
  GDrawCommandSequence *pdc_raw =
      gdraw_command_sequence_create_with_resource(RESOURCE_ID_WEATHER_ICONS_PDC);
  layout->weather_icon_pdc_sequence =
      pdc_raw ? gdraw_command_sequence_clone(pdc_raw) : NULL;
  if (pdc_raw) {
    gdraw_command_sequence_destroy(pdc_raw);  // munmaps the read-only flash mapping
  }
  layout->current_weather_escape_layer =
      layer_create_with_data(GRect(0, 0, 1, 1), sizeof(WeatherAppLayout *));
  if (layout->current_weather_escape_layer) {
    *(WeatherAppLayout **)layer_get_data(layout->current_weather_escape_layer) = layout;
    layer_set_update_proc(layout->current_weather_escape_layer,
                          prv_draw_current_escape_icon);
    layer_set_hidden(layout->current_weather_escape_layer, true);
    layer_add_child(layout->root_layer, layout->current_weather_escape_layer);
  }
#endif

  // Timeline Fin marker: child of root_layer so it can animate like the system timeline.
  if (layout->fin_pdc) {
    GRect fin_offscreen = (GRect){GPoint(0, frame->size.h),
                                  gdraw_command_image_get_bounds_size(layout->fin_pdc)};
    layout->fin_layer = layer_create_with_data(fin_offscreen, sizeof(WeatherAppLayout *));
    *(WeatherAppLayout **)layer_get_data(layout->fin_layer) = layout;
    layer_set_update_proc(layout->fin_layer, prv_draw_fin_layer);
    layer_set_hidden(layout->fin_layer, true);
    layer_add_child(layout->root_layer, layout->fin_layer);
  }

  // Location bar added last — always renders above animations and fin ribbon.
  layer_add_child(layout->root_layer, layout->location_bar_layer);
}

void weather_app_layout_set_data(WeatherAppLayout *layout,
                                 const WeatherLocationForecast *today_forecast,
                                 const WeatherLocationForecast *next_forecast) {
  layout->forecast = today_forecast;
  layout->next_forecast = next_forecast;

  if (layout->current_weather_icon) {
    gbitmap_destroy(layout->current_weather_icon);
    layout->current_weather_icon = NULL;
  }
  if (layout->tomorrow_weather_icon) {
    gbitmap_destroy(layout->tomorrow_weather_icon);
    layout->tomorrow_weather_icon = NULL;
  }

#if WEATHER_APP_LAYOUT_USE_PDC_WEATHER_ICONS
  bitmap_layer_set_bitmap(layout->current_weather_icon_layer, NULL);
  layer_set_frame(bitmap_layer_get_layer(layout->current_weather_icon_layer),
                  layout->today_icon_rest_frame);
#else
  if (today_forecast) {
    layout->current_weather_icon = gbitmap_create_with_resource(
        weather_type_get_icon_res_today(today_forecast->current_weather_type));
    bitmap_layer_set_bitmap(layout->current_weather_icon_layer, layout->current_weather_icon);
    layer_set_frame(bitmap_layer_get_layer(layout->current_weather_icon_layer),
                    layout->today_icon_rest_frame);
  } else {
    bitmap_layer_set_bitmap(layout->current_weather_icon_layer, NULL);
  }
#endif

  if (next_forecast) {
    layout->tomorrow_weather_icon = gbitmap_create_with_resource(
        weather_type_get_icon_res_tiny(next_forecast->current_weather_type));
    bitmap_layer_set_bitmap(layout->tomorrow_weather_icon_layer, layout->tomorrow_weather_icon);
  } else {
    bitmap_layer_set_bitmap(layout->tomorrow_weather_icon_layer, NULL);
  }

#if WEATHER_APP_LAYOUT_USE_PDC_WEATHER_ICONS
  prv_set_current_weather_pdc(layout, today_forecast);
#endif

  prv_restore_fin_rest(layout);
  layer_mark_dirty(layout->root_layer);
}

void weather_app_layout_set_fin_allowed(WeatherAppLayout *layout,
                                        bool fin_allowed) {
  if (!layout) return;
  layout->fin_allowed = fin_allowed;
  prv_restore_fin_rest(layout);
}

void weather_app_layout_deinit(WeatherAppLayout *layout) {
  if (layout->icon_animation) {
    Animation *anim = layout->icon_animation;
    layout->icon_animation = NULL;
    animation_unschedule(anim);
  }
  if (layout->fin_animation) {
    animation_unschedule(layout->fin_animation);
    layout->fin_animation = NULL;
  }
  if (layout->current_weather_icon) {
    gbitmap_destroy(layout->current_weather_icon);
  }
  if (layout->outgoing_weather_icon) {
    gbitmap_destroy(layout->outgoing_weather_icon);
  }
  if (layout->tomorrow_weather_icon) {
    gbitmap_destroy(layout->tomorrow_weather_icon);
  }
#if WEATHER_APP_LAYOUT_USE_PDC_WEATHER_ICONS
  if (layout->weather_icon_pdc_sequence) {
    gdraw_command_sequence_destroy(layout->weather_icon_pdc_sequence);
  }
  if (layout->current_weather_escape_layer) {
    layer_destroy(layout->current_weather_escape_layer);
  }
#endif
  if (layout->fin_pdc) gdraw_command_image_destroy(layout->fin_pdc);
  if (layout->fin_layer) layer_destroy(layout->fin_layer);
  bitmap_layer_destroy(layout->current_weather_icon_layer);
  layer_destroy(layout->outgoing_weather_icon_layer);
  bitmap_layer_destroy(layout->tomorrow_weather_icon_layer);
  layer_destroy(layout->location_bar_layer);
  layer_destroy(layout->content_layer);
  layer_destroy(layout->root_layer);
}

void weather_app_layout_set_location(WeatherAppLayout *layout, const char *name) {
  if (!name) return;
  strncpy(layout->location_name, name, sizeof(layout->location_name) - 1);
  layout->location_name[sizeof(layout->location_name) - 1] = '\0';
  if (layout->location_bar_layer) layer_mark_dirty(layout->location_bar_layer);
}

static bool prv_prepare_day_transition(WeatherAppLayout *layout,
                                       const WeatherLocationForecast *new_today,
                                       const WeatherLocationForecast *new_next,
                                       bool animate_down) {
  // Cancel any running animation
  if (layout->icon_animation) {
    Animation *old = layout->icon_animation;
    layout->icon_animation = NULL;
    bool was_down = layout->anim_params.animate_down;
    if (layout->anim_params.tomorrow_reparented || layout->anim_params.tomorrow_incoming) {
      Layer *tmr = bitmap_layer_get_layer(layout->tomorrow_weather_icon_layer);
      layer_remove_from_parent(tmr);
      layer_add_child(layout->content_layer, tmr);
      layer_set_frame(tmr, layout->tomorrow_icon_rest_frame);
      layout->anim_params.tomorrow_reparented = false;
      layout->anim_params.tomorrow_incoming = false;
    }
    animation_unschedule(old);
    layer_set_hidden(layout->outgoing_weather_icon_layer, true);
    layer_set_hidden(bitmap_layer_get_layer(layout->tomorrow_weather_icon_layer), false);
    layer_set_frame(bitmap_layer_get_layer(layout->current_weather_icon_layer),
                    layout->today_icon_rest_frame);
    // For a cancelled DOWN animation the layer roles were swapped: current_weather_icon_layer
    // still holds the OLD today bitmap while layout->forecast was already advanced.
    // Reload it now so the next animation's outgoing icon shows the correct (current) day.
    if (was_down) {
#if !WEATHER_APP_LAYOUT_USE_PDC_WEATHER_ICONS
      prv_reload_icon(&layout->current_weather_icon, layout->current_weather_icon_layer,
                      layout->forecast, false);
#endif
    }
  }
#if WEATHER_APP_LAYOUT_USE_PDC_WEATHER_ICONS
  prv_hide_current_escape_icon(layout);
#endif

  // Save weather types for background circle drawing during animation.
  // For animate_down the layer roles are swapped (scaler = incoming, BitmapLayer = outgoing),
  // so swap the type assignments too so the circles get the right colour.
  if (animate_down) {
    layout->anim_params.outgoing_weather_type =
        new_today ? new_today->current_weather_type : WeatherType_Unknown;
    layout->anim_params.incoming_weather_type =
        layout->forecast ? layout->forecast->current_weather_type : WeatherType_Unknown;
  } else {
    layout->anim_params.outgoing_weather_type =
        layout->forecast ? layout->forecast->current_weather_type : WeatherType_Unknown;
    layout->anim_params.incoming_weather_type =
        new_today ? new_today->current_weather_type : WeatherType_Unknown;
  }
  // Capture tomorrow weather type before layout->next_forecast pointer is updated.
  // For DOWN (entering next day), use new_next (the day that will become tomorrow).
  // For UP (returning), use the current tomorrow that's about to exit.
  layout->anim_params.tomorrow_exit_weather_type =
      (animate_down && new_next) ? new_next->current_weather_type :
      (layout->next_forecast ? layout->next_forecast->current_weather_type : WeatherType_Unknown);

  // ---- Layer bitmap setup ----
#if WEATHER_APP_LAYOUT_USE_PDC_WEATHER_ICONS
  layer_set_hidden(bitmap_layer_get_layer(layout->current_weather_icon_layer), true);
#endif
  if (animate_down) {
    // DOWN: scaler layer (outgoing_weather_icon_layer) = INCOMING tiny icon that scales up.
    //       BitmapLayer (current_weather_icon_layer)   = OUTGOING old today at full size.
    // Load the tiny resource of new_today into the scaler; start it at the tomorrow slot.
    if (layout->outgoing_weather_icon) gbitmap_destroy(layout->outgoing_weather_icon);
#if WEATHER_APP_LAYOUT_USE_PDC_WEATHER_ICONS
    bitmap_layer_set_bitmap(layout->current_weather_icon_layer, NULL);
#endif
    layout->outgoing_weather_icon = new_today ?
        gbitmap_create_with_resource(
            weather_type_get_icon_res_today(new_today->current_weather_type)) : NULL;
    // Frame set to arc start position below, after circle_center is computed.
    layer_set_hidden(layout->outgoing_weather_icon_layer, false);
    // current_weather_icon_layer already holds the old today bitmap — keep it as-is.
  } else {
    // UP: scaler layer = OUTGOING old today (shrinks + slides to tomorrow slot).
    //     BitmapLayer  = INCOMING new today (previous day, full size, sweeps along arc).
    if (layout->outgoing_weather_icon) gbitmap_destroy(layout->outgoing_weather_icon);
#if WEATHER_APP_LAYOUT_USE_PDC_WEATHER_ICONS
    layout->outgoing_weather_icon = layout->forecast ?
        gbitmap_create_with_resource(
            weather_type_get_icon_res_today(layout->forecast->current_weather_type)) : NULL;
#else
    layout->outgoing_weather_icon = layout->current_weather_icon;
    layout->current_weather_icon = NULL;
#endif
    layer_set_frame(layout->outgoing_weather_icon_layer, layout->today_icon_rest_frame);
    layer_set_hidden(layout->outgoing_weather_icon_layer, false);

    if (layout->current_weather_icon) {
      gbitmap_destroy(layout->current_weather_icon);
      layout->current_weather_icon = NULL;
    }
#if WEATHER_APP_LAYOUT_USE_PDC_WEATHER_ICONS
    bitmap_layer_set_bitmap(layout->current_weather_icon_layer, NULL);
#else
    prv_reload_icon(&layout->current_weather_icon, layout->current_weather_icon_layer,
                    new_today, false);
#endif
  }

  // Reparent tomorrow icon to root_layer for any arc animation (exit or entrance).
  // UP (going back):   reparent and animate it OUT along the arc.
  // DOWN (next day):   reparent and animate it IN from the arc if new_next exists.
  // Otherwise:         hide it immediately.
  if (!animate_down && layout->next_forecast) {
    Layer *tmr = bitmap_layer_get_layer(layout->tomorrow_weather_icon_layer);
    GRect tmr_root = (GRect){
      GPoint(layout->content_layer_origin.x + layout->tomorrow_icon_rest_frame.origin.x,
             layout->content_layer_origin.y + layout->tomorrow_icon_rest_frame.origin.y),
      layout->tomorrow_icon_rest_frame.size
    };
    layer_set_frame(tmr, tmr_root);
    layer_remove_from_parent(tmr);
    layer_insert_below_sibling(tmr, layout->location_bar_layer);
    layer_set_hidden(tmr, false);
    layout->anim_params.tomorrow_reparented = true;
    layout->anim_params.tomorrow_incoming = false;
  } else if (animate_down && new_next) {
    // Entrance: reparent but start frame will be set after circle_center is computed below.
    Layer *tmr = bitmap_layer_get_layer(layout->tomorrow_weather_icon_layer);
    layer_remove_from_parent(tmr);
    layer_insert_below_sibling(tmr, layout->location_bar_layer);
    layer_set_hidden(tmr, false);
    layout->anim_params.tomorrow_reparented = false;
    layout->anim_params.tomorrow_incoming = true;
  } else {
    layer_set_hidden(bitmap_layer_get_layer(layout->tomorrow_weather_icon_layer), true);
    layout->anim_params.tomorrow_reparented = false;
    layout->anim_params.tomorrow_incoming = false;
  }
  // Hide and reset the FIN layer whenever a new animation starts.
  if (layout->fin_layer) {
    layer_set_hidden(layout->fin_layer, true);
  }

  // Pre-load the new tomorrow bitmap for after the animation.
  // Exception: when the tomorrow layer is animated OUT (reparented for UP navigation),
  // keep the existing bitmap alive so the exit animation shows the correct icon.
  // The bitmap is reloaded in prv_icon_anim_stopped after the animation ends.
  if (!layout->anim_params.tomorrow_reparented) {
#if !WEATHER_APP_LAYOUT_USE_PDC_WEATHER_ICONS
    prv_reload_icon(&layout->tomorrow_weather_icon, layout->tomorrow_weather_icon_layer,
                    new_next, true);
#else
    if (layout->tomorrow_weather_icon) {
      gbitmap_destroy(layout->tomorrow_weather_icon);
      layout->tomorrow_weather_icon = NULL;
    }
    if (new_next) {
      layout->tomorrow_weather_icon = gbitmap_create_with_resource(
          weather_type_get_icon_res_tiny(new_next->current_weather_type));
      bitmap_layer_set_bitmap(layout->tomorrow_weather_icon_layer, layout->tomorrow_weather_icon);
    } else {
      bitmap_layer_set_bitmap(layout->tomorrow_weather_icon_layer, NULL);
    }
#endif
  }

  // Snapshot outgoing text strings before forecast pointer is updated.
  prv_snapshot_text(layout);
  layout->text_anim.dir_down = animate_down;
  layout->text_anim.progress = 0;
  layout->text_anim.active   = true;
#if WEATHER_APP_LAYOUT_USE_PDC_WEATHER_ICONS
  prv_move_day_icons_to_root(layout);
#endif

  // Update forecast pointers — text redraws immediately with new data
  layout->forecast = new_today;
  layout->next_forecast = new_next;

  // ---- Arc geometry ----
  // Circle centre is off-screen to the RIGHT.
  // At 9 o'clock (TODAY_REST = 3*MAX/4) icon sits left of centre = on-screen.
  // DOWN scroll: both icons sweep CW (increasing angle).
  //   Outgoing:  TODAY_REST  →  TODAY_REST + SWEEP  (exits upward-right)
  //   Incoming:  TODAY_REST - SWEEP  →  TODAY_REST  (arrives from below-right)
  // UP scroll: both sweep CCW (decreasing angle).
  //   Outgoing:  TODAY_REST  →  TODAY_REST - SWEEP  (exits downward-right)
  //   Incoming:  TODAY_REST + SWEEP  →  TODAY_REST  (arrives from above-right)
  GRect today_rest = layout->today_icon_rest_frame;
  GPoint today_center = GPoint(today_rest.origin.x + today_rest.size.w / 2,
                                today_rest.origin.y + today_rest.size.h / 2);
  int32_t radius = ICON_ARC_RADIUS;
  layout->anim_params.circle_center = GPoint(today_center.x + radius, today_center.y);
  layout->anim_params.radius = radius;
  layout->anim_params.animate_down = animate_down;
  layout->anim_params.outgoing_start_angle = ICON_ARC_TODAY_REST;
  if (animate_down) {
    layout->anim_params.outgoing_end_angle   = ICON_ARC_TODAY_REST + ICON_ARC_SWEEP_ANGLE;
    layout->anim_params.incoming_start_angle = ICON_ARC_TODAY_REST - ICON_ARC_SWEEP_ANGLE;
  } else {
    layout->anim_params.outgoing_end_angle   = ICON_ARC_TODAY_REST - ICON_ARC_SWEEP_ANGLE;
    layout->anim_params.incoming_start_angle = ICON_ARC_TODAY_REST + ICON_ARC_SWEEP_ANGLE;
  }
  // Place each icon at its starting position.
  if (animate_down) {
    // Scaler (incoming): starts at the actual on-screen tomorrow_rest position.
    prv_set_outgoing_weather_icon_frame(layout, layout->tomorrow_icon_rest_frame);
    // BitmapLayer (outgoing) is already at today_icon_rest_frame — no change needed.
    // New tomorrow icon entrance: start it at the arc incoming position in root coords.
    if (layout->anim_params.tomorrow_incoming) {
      GPoint cc_root = GPoint(
          layout->content_layer_origin.x + layout->anim_params.circle_center.x,
          layout->content_layer_origin.y + layout->anim_params.circle_center.y);
      layer_set_frame(bitmap_layer_get_layer(layout->tomorrow_weather_icon_layer),
                      prv_icon_frame_at_angle(cc_root, radius,
                                              layout->anim_params.incoming_start_angle,
                                              layout->tomorrow_icon_rest_frame.size));
    }
  } else {
    // BitmapLayer (incoming): starts at arc entry point above-right (TODAY_REST + SWEEP).
    layer_set_frame(bitmap_layer_get_layer(layout->current_weather_icon_layer),
                    prv_icon_frame_at_angle(layout->anim_params.circle_center, radius,
                                            layout->anim_params.incoming_start_angle,
                                            layout->today_icon_rest_frame.size));
  }
#if WEATHER_APP_LAYOUT_USE_PDC_WEATHER_ICONS
  if (layout->current_weather_escape_layer) {
    GRect start_frame = animate_down
        ? layout->today_icon_rest_frame
        : prv_icon_frame_at_angle(layout->anim_params.circle_center, radius,
                                  layout->anim_params.incoming_start_angle,
                                  layout->today_icon_rest_frame.size);
    layout->anim_params.current_root_overlay = true;
    prv_set_current_escape_icon_frame(layout, start_frame);
    layer_set_hidden(layout->current_weather_escape_layer, false);
  }
#endif

  return true;
}

void weather_app_layout_animate(WeatherAppLayout *layout,
                                const WeatherLocationForecast *new_today,
                                const WeatherLocationForecast *new_next,
                                bool animate_down) {
  if (!prv_prepare_day_transition(layout, new_today, new_next, animate_down)) return;

  // Linear time: the moook frame tables ARE the easing. Stacking EaseOut on
  // top crushed the 3-frame anticipation (~25ms, invisible) and smeared the
  // bounce-back across ~130ms of mush — every firmware moook runs linear.
  layout->icon_animation = prv_start_anim(ICON_ANIM_DURATION_MS, &s_icon_anim_impl,
                                          prv_icon_anim_stopped, layout);

  // If this scroll lands on the last day (no next forecast), kick off the FIN
  // ribbon slide at the same time so it arrives exactly as the icon settles.
  if (!new_next && layout->fin_allowed) {
    prv_animate_fin_in(layout, ICON_ANIM_DURATION_MS);
  }

  layer_mark_dirty(layout->root_layer);
}

// ---- Return transition: list screen → main screen ----
// Mirror of the forward transition: icons collapse to flat bars at their list
// positions, fly right back to their main-screen rest frames, then spring open.
