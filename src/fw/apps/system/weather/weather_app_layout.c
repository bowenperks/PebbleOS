/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "weather_app_layout.h"
#include "weather_math.h"
#include "resource_ids.pin.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#define WEATHER_APP_LAYOUT_TOP_PADDING PBL_IF_RECT_ELSE(0, 0)
#define WEATHER_APP_LAYOUT_CONTENT_LAYER_HORIZONTAL_INSET PBL_IF_RECT_ELSE(3, 2)
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
#define WEATHER_APP_LAYOUT_ROUND_REFRESH_TEXT_Y 13
#define WEATHER_APP_LAYOUT_DAY_X_SHIFT PBL_IF_ROUND_ELSE(12, 0)
#define WEATHER_APP_LAYOUT_TEMP_X_SHIFT PBL_IF_ROUND_ELSE(-4, 0)
#define WEATHER_APP_LAYOUT_HIGHLOW_X_SHIFT PBL_IF_ROUND_ELSE(-2, 0)
#define WEATHER_APP_LAYOUT_METRICS_X_SHIFT PBL_IF_ROUND_ELSE(5, 0)
#define WEATHER_APP_LAYOUT_DAY_Y_SHIFT PBL_IF_ROUND_ELSE(-4, 0)
#define WEATHER_APP_LAYOUT_TEXT_STACK_STEP PBL_IF_ROUND_ELSE(34, 0)
#define WEATHER_APP_LAYOUT_TEXT_STACK_GAP PBL_IF_ROUND_ELSE(4, 0)
#define WEATHER_APP_LAYOUT_METRIC_TEXT_Y_INSET PBL_IF_RECT_ELSE(17, 18)
#define WEATHER_APP_LAYOUT_TEMP_Y_SHIFT \
  PBL_IF_ROUND_ELSE(WEATHER_APP_LAYOUT_TEXT_STACK_GAP, 0)
#define WEATHER_APP_LAYOUT_HIGHLOW_Y_SHIFT \
  PBL_IF_ROUND_ELSE(WEATHER_APP_LAYOUT_TEXT_STACK_GAP * 2, 0)
#define WEATHER_APP_LAYOUT_METRICS_Y_SHIFT \
  PBL_IF_ROUND_ELSE(WEATHER_APP_LAYOUT_TEXT_STACK_GAP * 3 - \
                    WEATHER_APP_LAYOUT_METRIC_TEXT_Y_INSET, 0)
#define WEATHER_APP_LAYOUT_TODAY_ICON_X_ADJUST PBL_IF_ROUND_ELSE(9, 0)
#define WEATHER_APP_LAYOUT_TOMORROW_ICON_X_ADJUST PBL_IF_ROUND_ELSE(23, 0)
#define WEATHER_APP_LAYOUT_TODAY_ICON_Y_ADJUST PBL_IF_ROUND_ELSE(-28, 0)
#define WEATHER_APP_LAYOUT_ICON_Y_ADJUST PBL_IF_ROUND_ELSE(-15, 0)
#define WEATHER_APP_LAYOUT_TOMORROW_ICON_Y_ADJUST PBL_IF_ROUND_ELSE(15, 0)
#define WEATHER_APP_LAYOUT_SEPARATOR_Y_ADJUST PBL_IF_ROUND_ELSE(-8, 0)
#define WEATHER_APP_LAYOUT_BOTTOM_TEXT_X_SHIFT PBL_IF_ROUND_ELSE(30, 0)
#define WEATHER_APP_LAYOUT_BOTTOM_LABEL_X_ADJUST PBL_IF_ROUND_ELSE(-18, 0)
#define WEATHER_APP_LAYOUT_BOTTOM_TEMP_X_ADJUST PBL_IF_ROUND_ELSE(4, 0)
// Arc animation: circle is off-screen to the RIGHT.
// REST_ANGLE = 3*TRIG_MAX_ANGLE/4 (9 o'clock from centre) puts the icon
// on-screen to the LEFT of the centre, which is right of screen centre.
// Both today and tomorrow icons ride the same circle with a fixed angular gap.
#define ICON_ARC_RADIUS       PBL_IF_RECT_ELSE(220, 190)
#define ICON_ARC_SWEEP_ANGLE  (TRIG_MAX_ANGLE * 40 / 360)
// 9 o'clock: icon is to the left of the circle centre (which is off-screen right)
#define ICON_ARC_TODAY_REST   (TRIG_MAX_ANGLE * 3 / 4)
// Timeline-style landing bounce for day text: anticipation, fast travel,
// then a tiny overshoot back into place.
#define TEXT_MOOOK_MID_FRAMES 3
#define TEXT_MOOOK_BOUNCE_BACK 4
#define ICON_ANIM_DURATION_MS 220

// Glow ring drawn around the focused (today) artwork when at rest.
#define GLOW_OUTER_EXTRA    7     // px the ring extends beyond the filled circle edge
#define GLOW_TIMER_MS       50    // ~20fps tick
#define GLOW_IDLE_TIMEOUT_MS 5000
#define GLOW_IDLE_TICKS (GLOW_IDLE_TIMEOUT_MS / GLOW_TIMER_MS)

// Pull-to-refresh
#define PULL_TRIGGER_PX   40    // pixels of downward drag needed to arm refresh
#define PULL_HINT_PX      14    // pixels of drag before the spinner hint appears
#define PULL_SNAP_STEPS   14    // ease-out steps for snap-back (~224 ms at 16 ms/step)
#define PULL_SNAP_MS      16    // ms between snap-back timer ticks
#define PULL_SPIN_MS      40    // ms between spinner frames (~25 fps)
#define WEATHER_APP_LAYOUT_ROUND_PULL_LAYER_HEIGHT \
  (WEATHER_APP_LAYOUT_ROUND_BAR_DEPTH + PULL_TRIGGER_PX + 6)
#define CLOCK_TRANSIT_MS  240   // quick shrink-to-point then shake

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
static bool prv_prepare_list_transition(WeatherAppLayout *layout,
                                        int current_day_index,
                                        int num_days,
                                        void (*done_cb)(void *ctx),
                                        void *done_ctx);
static bool prv_prepare_return_transition(WeatherAppLayout *layout);
static void prv_apply_return_transition_progress(WeatherAppLayout *layout,
                                                 AnimationProgress progress);
static void prv_complete_return_transition(WeatherAppLayout *layout);

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
  if (!gcolor_equal(background_color, GColorClear)) {
    graphics_context_set_fill_color(context, background_color);
    graphics_fill_radial(context, *circle_bounding_box, GOvalScaleModeFitCircle,
                         circle_bounding_box->size.w / 2, 0, TRIG_MAX_ANGLE);
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
  graphics_context_set_fill_color(context, PBL_IF_COLOR_ELSE(GColorVividCerulean, GColorBlack));
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
}

static void prv_draw_wind_icon(GContext *context, GPoint origin) {
  graphics_context_set_stroke_color(context, GColorBlack);
  graphics_context_set_stroke_width(context, 2);
  graphics_draw_line(context, GPoint(origin.x, origin.y + 3),
                     GPoint(origin.x + 12, origin.y + 3));
  graphics_draw_line(context, GPoint(origin.x + 3, origin.y + 7),
                     GPoint(origin.x + 15, origin.y + 7));
  graphics_draw_line(context, GPoint(origin.x, origin.y + 11),
                     GPoint(origin.x + 9, origin.y + 11));
  graphics_context_set_stroke_width(context, 1);
}

static int prv_draw_metric_row(GPoint offset, int max_width, GContext *context,
                               const GFont font, const char *rain, const char *wind) {
  const int row_h = PBL_IF_RECT_ELSE(16, 14);
  const int y = offset.y + WEATHER_APP_LAYOUT_METRIC_TEXT_Y_INSET;
  const int rain_text_x = offset.x + 14;
  const int wind_x = offset.x + PBL_IF_RECT_ELSE(58, 50);
  const int wind_text_x = wind_x + 17;

  prv_draw_raindrop_icon(context, GPoint(offset.x + 1, y + 2));
  graphics_context_set_text_color(context, GColorBlack);
  graphics_draw_text(context, rain, font,
                     GRect(rain_text_x, y, wind_x - rain_text_x - 2, row_h + 2),
                     GTextOverflowModeTrailingEllipsis,
                     GTextAlignmentLeft,
                     NULL);

  prv_draw_wind_icon(context, GPoint(wind_x, y + 2));
  graphics_draw_text(context, wind, font,
                     GRect(wind_text_x, y, max_width - (wind_text_x - offset.x), row_h + 2),
                     GTextOverflowModeTrailingEllipsis,
                     GTextAlignmentLeft,
                     NULL);
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
// Capture current forecast strings into text_anim before the forecast pointer is updated.
static void prv_snapshot_text(WeatherAppLayout *layout) {
  const WeatherLocationForecast *f = layout->forecast;
  if (f) {
    const char *lbl = (f->label && f->label[0]) ? f->label : "TODAY";
    strncpy(layout->text_anim.top_label, lbl, sizeof(layout->text_anim.top_label) - 1);
    layout->text_anim.top_label[sizeof(layout->text_anim.top_label) - 1] = '\0';

    char tbuf[15] = {0};
    if (f->current_temp == WEATHER_SERVICE_LOCATION_FORECAST_UNKNOWN_TEMP) {
      if (f->today_high != WEATHER_SERVICE_LOCATION_FORECAST_UNKNOWN_TEMP)
        snprintf(tbuf, sizeof(tbuf), "%i\xC2\xB0", f->today_high);
      else
        snprintf(tbuf, sizeof(tbuf), "--\xC2\xB0");
    } else {
      snprintf(tbuf, sizeof(tbuf), "%i\xC2\xB0", f->current_temp);
    }
    strncpy(layout->text_anim.top_temp, tbuf, sizeof(layout->text_anim.top_temp) - 1);

    prv_fill_high_low_temp_buffer(f->today_high, f->today_low,
                                  layout->text_anim.top_highlow,
                                  sizeof(layout->text_anim.top_highlow));
    prv_fill_rain_value_buffer(f,
                               layout->text_anim.top_rain,
                               sizeof(layout->text_anim.top_rain));
    prv_fill_wind_value_buffer(f,
                               layout->text_anim.top_wind,
                               sizeof(layout->text_anim.top_wind));
  } else {
    layout->text_anim.top_label[0] = '\0';
    layout->text_anim.top_temp[0]  = '\0';
    layout->text_anim.top_highlow[0] = '\0';
    layout->text_anim.top_rain[0] = '\0';
    layout->text_anim.top_wind[0] = '\0';
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

// Draw top-half text from the snapshot (outgoing frame).
static void prv_draw_snapshot_top(const WeatherAppLayout *layout, GPoint *off,
                                   int cw, GContext *ctx) {
#if PBL_ROUND
  const int day_y = off->y + WEATHER_APP_LAYOUT_DAY_Y_SHIFT;
  GPoint line_off = GPoint(off->x + WEATHER_APP_LAYOUT_DAY_X_SHIFT, day_y);
  prv_draw_text(line_off,
                cw - WEATHER_APP_LAYOUT_DAY_X_SHIFT,
                ctx, layout->text_anim.top_label,
                layout->location_font, GColorBlack, GTextAlignmentLeft);

  line_off = GPoint(off->x + WEATHER_APP_LAYOUT_TEMP_X_SHIFT,
                    day_y + WEATHER_APP_LAYOUT_TEXT_STACK_STEP);
  prv_draw_text(line_off,
                cw - WEATHER_APP_LAYOUT_TEMP_X_SHIFT,
                ctx, layout->text_anim.top_temp,
                layout->temperature_font, GColorBlack, GTextAlignmentLeft);

  line_off = GPoint(off->x + WEATHER_APP_LAYOUT_HIGHLOW_X_SHIFT,
                    day_y + WEATHER_APP_LAYOUT_TEXT_STACK_STEP * 2);
  prv_draw_text(line_off,
                cw - WEATHER_APP_LAYOUT_HIGHLOW_X_SHIFT,
                ctx, layout->text_anim.top_highlow,
                layout->high_low_phrase_font, GColorBlack, GTextAlignmentLeft);

  line_off = GPoint(off->x + WEATHER_APP_LAYOUT_METRICS_X_SHIFT,
                    day_y + WEATHER_APP_LAYOUT_TEXT_STACK_STEP * 3 -
                        WEATHER_APP_LAYOUT_METRIC_TEXT_Y_INSET);
  prv_draw_metric_row(line_off,
                      cw - WEATHER_APP_LAYOUT_METRICS_X_SHIFT,
                      ctx, layout->metrics_font,
                      layout->text_anim.top_rain,
                      layout->text_anim.top_wind);
#else
  GPoint line_off = GPoint(off->x + WEATHER_APP_LAYOUT_DAY_X_SHIFT, off->y);
  off->y += prv_draw_text(line_off,
                           cw - WEATHER_APP_LAYOUT_DAY_X_SHIFT,
                           ctx, layout->text_anim.top_label,
                           layout->location_font, GColorBlack, GTextAlignmentLeft);
  off->y += PBL_IF_RECT_ELSE(2, 0);
  line_off = GPoint(off->x + WEATHER_APP_LAYOUT_TEMP_X_SHIFT,
                    off->y + WEATHER_APP_LAYOUT_TEMP_Y_SHIFT);
  off->y += prv_draw_text(line_off,
                           cw - WEATHER_APP_LAYOUT_TEMP_X_SHIFT,
                           ctx, layout->text_anim.top_temp,
                           layout->temperature_font, GColorBlack, GTextAlignmentLeft);
  line_off = GPoint(off->x + WEATHER_APP_LAYOUT_HIGHLOW_X_SHIFT,
                    off->y + WEATHER_APP_LAYOUT_HIGHLOW_Y_SHIFT);
  off->y += prv_draw_text(line_off,
                           cw - WEATHER_APP_LAYOUT_HIGHLOW_X_SHIFT,
                           ctx, layout->text_anim.top_highlow,
                           layout->high_low_phrase_font, GColorBlack, GTextAlignmentLeft);
  line_off = GPoint(off->x + WEATHER_APP_LAYOUT_METRICS_X_SHIFT,
                    off->y + WEATHER_APP_LAYOUT_METRICS_Y_SHIFT);
  prv_draw_metric_row(line_off,
                      cw - WEATHER_APP_LAYOUT_METRICS_X_SHIFT,
                      ctx, layout->metrics_font,
                      layout->text_anim.top_rain,
                      layout->text_anim.top_wind);
#endif
}

// Draw bottom-half text from the snapshot (outgoing frame).
static void prv_draw_snapshot_bot(const WeatherAppLayout *layout, GPoint *off,
                                   int cw, GContext *ctx) {
  if (!layout->text_anim.bot_valid) return;
  off->x += WEATHER_APP_LAYOUT_BOTTOM_TEXT_X_SHIFT;
  cw -= WEATHER_APP_LAYOUT_BOTTOM_TEXT_X_SHIFT;
  off->y += 6;
  GPoint label_off = GPoint(off->x + WEATHER_APP_LAYOUT_BOTTOM_LABEL_X_ADJUST, off->y);
  off->y += prv_draw_text(label_off, cw, ctx, layout->text_anim.bot_label,
                           layout->tomorrow_font, GColorBlack, GTextAlignmentLeft);
  GPoint temp_off = GPoint(off->x + WEATHER_APP_LAYOUT_BOTTOM_TEMP_X_ADJUST, off->y);
  prv_draw_text(temp_off, cw, ctx, layout->text_anim.bot_highlow,
                 layout->high_low_phrase_font, GColorBlack, GTextAlignmentLeft);
}

static void prv_draw_top_half_text(const WeatherAppLayout *layout, GPoint *current_offset,
                                   int content_width, GContext *context) {
  const WeatherLocationForecast *forecast = layout->forecast;

  const char *label = (forecast->label && forecast->label[0]) ? forecast->label : "TODAY";
#if PBL_ROUND
  const int day_y = current_offset->y + WEATHER_APP_LAYOUT_DAY_Y_SHIFT;
  GPoint line_offset =
      GPoint(current_offset->x + WEATHER_APP_LAYOUT_DAY_X_SHIFT, day_y);
  prv_draw_text(line_offset,
                content_width - WEATHER_APP_LAYOUT_DAY_X_SHIFT,
                context, label,
                layout->location_font, GColorBlack,
                GTextAlignmentLeft);

  char text_buffer[15] = {0};
  const size_t max_text_buff_size = sizeof(text_buffer);

  if (forecast->current_temp == WEATHER_SERVICE_LOCATION_FORECAST_UNKNOWN_TEMP) {
    if (forecast->today_high != WEATHER_SERVICE_LOCATION_FORECAST_UNKNOWN_TEMP) {
      snprintf(text_buffer, max_text_buff_size, "%i\xC2\xB0", forecast->today_high);
    } else {
      snprintf(text_buffer, max_text_buff_size, "--\xC2\xB0");
    }
  } else {
    snprintf(text_buffer, max_text_buff_size, "%i\xC2\xB0", forecast->current_temp);
  }
  line_offset = GPoint(current_offset->x + WEATHER_APP_LAYOUT_TEMP_X_SHIFT,
                       day_y + WEATHER_APP_LAYOUT_TEXT_STACK_STEP);
  prv_draw_text(line_offset,
                content_width - WEATHER_APP_LAYOUT_TEMP_X_SHIFT,
                context, text_buffer,
                layout->temperature_font, GColorBlack, GTextAlignmentLeft);

  prv_fill_high_low_temp_buffer(forecast->today_high, forecast->today_low,
                                text_buffer, max_text_buff_size);
  line_offset = GPoint(current_offset->x + WEATHER_APP_LAYOUT_HIGHLOW_X_SHIFT,
                       day_y + WEATHER_APP_LAYOUT_TEXT_STACK_STEP * 2);
  prv_draw_text(line_offset,
                content_width - WEATHER_APP_LAYOUT_HIGHLOW_X_SHIFT,
                context, text_buffer,
                layout->high_low_phrase_font, GColorBlack,
                GTextAlignmentLeft);

  char rain_buffer[12] = {0};
  char wind_buffer[16] = {0};
  prv_fill_rain_value_buffer(forecast, rain_buffer, sizeof(rain_buffer));
  prv_fill_wind_value_buffer(forecast, wind_buffer, sizeof(wind_buffer));
  line_offset = GPoint(current_offset->x + WEATHER_APP_LAYOUT_METRICS_X_SHIFT,
                       day_y + WEATHER_APP_LAYOUT_TEXT_STACK_STEP * 3 -
                           WEATHER_APP_LAYOUT_METRIC_TEXT_Y_INSET);
  prv_draw_metric_row(line_offset,
                      content_width - WEATHER_APP_LAYOUT_METRICS_X_SHIFT,
                      context, layout->metrics_font,
                      rain_buffer, wind_buffer);
#else
  GPoint line_offset =
      GPoint(current_offset->x + WEATHER_APP_LAYOUT_DAY_X_SHIFT,
             current_offset->y);
  current_offset->y += prv_draw_text(line_offset,
                                     content_width - WEATHER_APP_LAYOUT_DAY_X_SHIFT,
                                     context, label,
                                     layout->location_font, GColorBlack,
                                     GTextAlignmentLeft);

  const int location_and_today_temperature_vertical_spacing = PBL_IF_RECT_ELSE(2, 0);
  current_offset->y += location_and_today_temperature_vertical_spacing;

  char text_buffer[15] = {0};
  const size_t max_text_buff_size = sizeof(text_buffer);

  if (forecast->current_temp == WEATHER_SERVICE_LOCATION_FORECAST_UNKNOWN_TEMP) {
    // Future day: show daily high as the featured temperature
    if (forecast->today_high != WEATHER_SERVICE_LOCATION_FORECAST_UNKNOWN_TEMP) {
      snprintf(text_buffer, max_text_buff_size, "%i\xC2\xB0", forecast->today_high);
    } else {
      snprintf(text_buffer, max_text_buff_size, "--\xC2\xB0");
    }
  } else {
    snprintf(text_buffer, max_text_buff_size, "%i\xC2\xB0", forecast->current_temp);
  }
  line_offset = GPoint(current_offset->x + WEATHER_APP_LAYOUT_TEMP_X_SHIFT,
                       current_offset->y + WEATHER_APP_LAYOUT_TEMP_Y_SHIFT);
  current_offset->y += prv_draw_text(line_offset,
                                     content_width - WEATHER_APP_LAYOUT_TEMP_X_SHIFT,
                                     context, text_buffer,
                                     layout->temperature_font, GColorBlack, GTextAlignmentLeft);

  prv_fill_high_low_temp_buffer(forecast->today_high, forecast->today_low,
                                text_buffer, max_text_buff_size);
  line_offset = GPoint(current_offset->x + WEATHER_APP_LAYOUT_HIGHLOW_X_SHIFT,
                       current_offset->y + WEATHER_APP_LAYOUT_HIGHLOW_Y_SHIFT);
  current_offset->y += prv_draw_text(line_offset,
                                     content_width - WEATHER_APP_LAYOUT_HIGHLOW_X_SHIFT,
                                     context, text_buffer,
                                     layout->high_low_phrase_font, GColorBlack,
                                     GTextAlignmentLeft);

  char rain_buffer[12] = {0};
  char wind_buffer[16] = {0};
  prv_fill_rain_value_buffer(forecast, rain_buffer, sizeof(rain_buffer));
  prv_fill_wind_value_buffer(forecast, wind_buffer, sizeof(wind_buffer));
  line_offset = GPoint(current_offset->x + WEATHER_APP_LAYOUT_METRICS_X_SHIFT,
                       current_offset->y + WEATHER_APP_LAYOUT_METRICS_Y_SHIFT);
  prv_draw_metric_row(line_offset,
                      content_width - WEATHER_APP_LAYOUT_METRICS_X_SHIFT,
                      context, layout->metrics_font,
                      rain_buffer, wind_buffer);
#endif
}

static void prv_draw_bottom_half_text(const WeatherAppLayout *layout, GPoint *current_offset,
                                      int content_width, GContext *context) {
  const WeatherLocationForecast *next = layout->next_forecast;
  if (!next) return;
  current_offset->x += WEATHER_APP_LAYOUT_BOTTOM_TEXT_X_SHIFT;
  content_width -= WEATHER_APP_LAYOUT_BOTTOM_TEXT_X_SHIFT;

  const int separator_tomorrow_title_vertical_spacing = 6;
  current_offset->y += separator_tomorrow_title_vertical_spacing;

  GPoint label_offset = GPoint(current_offset->x + WEATHER_APP_LAYOUT_BOTTOM_LABEL_X_ADJUST,
                               current_offset->y);
  current_offset->y += prv_draw_text(label_offset, content_width, context,
                                     (next->label && next->label[0]) ? next->label : "TOMORROW",
                                     layout->tomorrow_font,
                                     GColorBlack, GTextAlignmentLeft);

  char text_buffer[15] = {0};
  prv_fill_high_low_temp_buffer(next->today_high, next->today_low,
                                text_buffer, sizeof(text_buffer));
  GPoint temp_offset = GPoint(current_offset->x + WEATHER_APP_LAYOUT_BOTTOM_TEMP_X_ADJUST,
                              current_offset->y);
  prv_draw_text(temp_offset, content_width, context, text_buffer,
                layout->high_low_phrase_font, GColorBlack, GTextAlignmentLeft);
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

// Root-layer draw proc — draws the pull-to-refresh gap indicator when active;
// child layers render on top automatically.
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
  return layout->text_anim.active
      || layout->animation_state.transitioning_to_list
      || layout->animation_state.returning_from_list
      || layout->clock_transit.transitioning_to_clock;
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

static GColor prv_pull_bg_color(const WeatherAppLayout *layout) {
  (void)layout;
  // Always the location/"updated" banner blue (see prv_draw_location_bar_background),
  // independent of the current weather type.
  return PBL_IF_COLOR_ELSE(GColorVividCerulean, GColorBlack);
}

static void prv_draw_pull_spinner(const WeatherAppLayout *layout, GContext *context,
                                  int cx, int cy, int gap_h) {
  if (gap_h < PULL_HINT_PX) return;

  // Spinner ring: radius fits ~80 % of the gap half-height, capped at 11 px.
  int r = (gap_h / 2) * 4 / 5;
  if (r < 4) r = 4;
  if (r > 11) r = 11;
  GRect ring = GRect(cx - r, cy - r, r * 2, r * 2);
  graphics_context_set_fill_color(context, GColorWhite);

  if (layout->pull_triggered) {
    // Full 3/4 arc spinning.
    int32_t arc_span = (int32_t)TRIG_MAX_ANGLE * 3 / 4;
    graphics_fill_radial(context, ring, GOvalScaleModeFitCircle, 3,
                         layout->pull_spin_angle,
                         layout->pull_spin_angle + arc_span);
  } else {
    // Arc grows from zero (at PULL_HINT_PX) to 3/4 circle (at PULL_TRIGGER_PX).
    int32_t frac = (int32_t)((gap_h - PULL_HINT_PX) * ANIMATION_NORMALIZED_MAX)
                   / (PULL_TRIGGER_PX - PULL_HINT_PX);
    if (frac > ANIMATION_NORMALIZED_MAX) frac = ANIMATION_NORMALIZED_MAX;
    int32_t arc_span = (int32_t)TRIG_MAX_ANGLE * 3 * frac / (4 * ANIMATION_NORMALIZED_MAX);
    if (arc_span > 0) {
      // Centre at 6 o'clock so the arc grows symmetrically downward.
      int32_t start = (int32_t)(TRIG_MAX_ANGLE / 2) - arc_span / 2;
      graphics_fill_radial(context, ring, GOvalScaleModeFitCircle, 3,
                           start, start + arc_span);
    }
  }
}

static void prv_render_root_layer(Layer *layer, GContext *context) {
  const WeatherAppLayout *layout = *(const WeatherAppLayout **)layer_get_data(layer);
  if (!layout || layout->pull_offset <= 0) return;

#if PBL_ROUND
  (void)context;
  return;
#else
  GRect bounds = layer_get_bounds(layer);
  int bar_h = WEATHER_APP_LAYOUT_ACTIVE_BAR_DEPTH;
  int gap_h = layout->pull_offset;
  int cx    = bounds.size.w / 2;
  int cy    = bar_h + gap_h / 2;

  // Fill the revealed gap with the current weather background colour.
  graphics_context_set_fill_color(context, prv_pull_bg_color(layout));
  graphics_fill_rect(context, GRect(0, bar_h, bounds.size.w, gap_h), 0, GCornerNone);
  prv_draw_pull_spinner(layout, context, cx, cy, gap_h);
#endif
}

static void prv_glow_timer_callback(void *context);

static void prv_start_glow_timer(WeatherAppLayout *layout) {
  if (!layout || layout->glow_timer) return;
  layout->glow_timer = app_timer_register(GLOW_TIMER_MS,
                                          prv_glow_timer_callback,
                                          layout);
}

void weather_app_layout_note_interaction(WeatherAppLayout *layout) {
  if (!layout) return;
  layout->glow_idle_ticks = 0;
  prv_start_glow_timer(layout);
}

static void prv_glow_timer_callback(void *context) {
  WeatherAppLayout *layout = (WeatherAppLayout *)context;
  if (!layout) return;
  layout->glow_timer = NULL;

  if (layout->glow_idle_ticks >= GLOW_IDLE_TICKS + WEATHER_GLOW_WRAP_TICKS) {
    return;
  }

  bool transition_busy = layout->text_anim.active
      || layout->icon_animation
      || layout->interactive.kind != WeatherInteractiveTransition_None
      || layout->animation_state.transitioning_to_list
      || layout->animation_state.returning_from_list
      || layout->clock_transit.transitioning_to_clock;
  if (!layout->glow_paused && !transition_busy) {
    if (layout->glow_idle_ticks < GLOW_IDLE_TICKS) {
      // Advance phase ~4 seconds per full rotation at 50ms tick
      layout->glow_phase = (layout->glow_phase + (uint32_t)(TRIG_MAX_ANGLE / 80)) & ((uint32_t)TRIG_MAX_ANGLE - 1);
    } else {
      layout->glow_phase = (layout->glow_phase + (uint32_t)(TRIG_MAX_ANGLE / 6)) & ((uint32_t)TRIG_MAX_ANGLE - 1);
    }
    layout->glow_idle_ticks++;
    layer_mark_dirty(layout->content_layer);
  }
  prv_start_glow_timer(layout);
}

static void prv_draw_weather_icon_backgrounds(const WeatherAppLayout *layout,
                                              GContext *context) {
  bool animating = !layer_get_hidden(layout->outgoing_weather_icon_layer)
                   || layout->clock_transit.transitioning_to_clock;

  if (animating) {
    // During clock-transit shake phase: draw a solid black circle at the merged dot position.
    if (layout->clock_transit.transitioning_to_clock && layout->clock_transit.shake_phase) {
      Layer *out = layout->outgoing_weather_icon_layer;
      GRect fr = layer_get_frame(out);
      const int diam = fr.size.w;  // layer size == dot diameter
      GRect dot_rect = {
        GPoint(fr.origin.x + fr.size.w / 2 - diam / 2,
               fr.origin.y + fr.size.h / 2 - diam / 2),
        GSize(diam, diam)
      };
      prv_draw_weather_background(&dot_rect, context, GColorBlack);
      return;  // skip all other circle drawing
    }
    prv_draw_circle_at_layer(layout->outgoing_weather_icon_layer,
                             context, layout->anim_params.outgoing_weather_type);
#if WEATHER_APP_LAYOUT_USE_PDC_WEATHER_ICONS
    if (layout->text_anim.active && !layout->outgoing_weather_icon) {
      prv_draw_weather_pdc_frame(layout, context,
                                 layout->anim_params.outgoing_weather_type,
                                 layer_get_frame(layout->outgoing_weather_icon_layer));
    }
#endif
    if (!layout->animation_state.transitioning_to_list
        && !layout->animation_state.returning_from_list
        && !layout->clock_transit.transitioning_to_clock
#if WEATHER_APP_LAYOUT_USE_PDC_WEATHER_ICONS
        && !layout->anim_params.current_root_overlay
#endif
        ) {
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
    if ((layout->animation_state.transitioning_to_list || layout->animation_state.returning_from_list
         || layout->clock_transit.transitioning_to_clock)
        && layout->next_forecast) {
      // Draw circle behind the tomorrow icon as it slides. During the clock
      // transition the small icon is rendered by its squashing scaler layer, so
      // anchor the circle to that scaler (not the resting BitmapLayer) so it
      // follows the squash; the list transition still uses the BitmapLayer.
      Layer *tmr_circle_layer = layout->clock_transit.transitioning_to_clock
          ? layout->tomorrow_outgoing_icon_layer
          : bitmap_layer_get_layer(layout->tomorrow_weather_icon_layer);
      prv_draw_circle_at_layer(tmr_circle_layer,
                               context, layout->next_forecast->current_weather_type);
    }

    // Draw circle for the reparented/incoming tomorrow icon during UP/DOWN day animation.
    // The layer lives on root_layer so its frame is in root coordinates; convert to
    // content-layer coordinates so the circle is drawn correctly in this context.
    // This must be done here (after white text-animation fills) so it is not erased.
    if ((layout->anim_params.tomorrow_reparented || layout->anim_params.tomorrow_incoming)
        && !layout->animation_state.transitioning_to_list
        && !layout->animation_state.returning_from_list) {
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

    // ---- Speed lines during list transition/return flight phase ----
    // Forward (to list): icons move LEFT  → lines trail RIGHT (+x)
    // Return  (to main): icons move RIGHT → lines trail LEFT  (-x)
    if (layout->animation_state.transitioning_to_list || layout->animation_state.returning_from_list) {
      bool is_return = layout->animation_state.returning_from_list;
      int32_t p = layout->animation_state.list_transit_progress;
      const int32_t FLIGHT_START = ANIMATION_NORMALIZED_MAX * 28 / 100;
      const int32_t FLIGHT_END   = ANIMATION_NORMALIZED_MAX * 80 / 100;

      if (p > FLIGHT_START && p < FLIGHT_END) {
        int32_t ft = (p - FLIGHT_START) * ANIMATION_NORMALIZED_MAX / (FLIGHT_END - FLIGHT_START);
        int32_t intensity = weather_norm_bell(ft);

        GColor sc = weather_type_get_bg_color(
            layout->forecast ? layout->forecast->current_weather_type : WeatherType_Unknown);
        if (gcolor_equal(sc, GColorClear)) sc = GColorLightGray;
        graphics_context_set_stroke_color(context, sc);
        graphics_context_set_stroke_width(context, 1);

        GPoint tp = layout->animation_state.list_today_icon_pos;
        int max_len = 22;
        const int dy_offsets[5] = { 0, -3, 3, -7, 7 };
        const int len_scale[5]  = { 100, 85, 85, 60, 60 };
        for (int i = 0; i < 5; i++) {
          int len = (int)((int32_t)max_len * (int32_t)len_scale[i] * intensity
                          / (100 * (int32_t)ANIMATION_NORMALIZED_MAX));
          if (len < 2) continue;
          int y = tp.y + dy_offsets[i];
          int x0 = is_return ? (tp.x - 2) : (tp.x + 2);
          int x1 = is_return ? (x0 - len)  : (x0 + len);
          graphics_draw_line(context, GPoint(x0, y), GPoint(x1, y));
        }

        if (layout->next_forecast) {
          GColor sc2 = weather_type_get_bg_color(layout->next_forecast->current_weather_type);
          if (gcolor_equal(sc2, GColorClear)) sc2 = GColorLightGray;
          graphics_context_set_stroke_color(context, sc2);
          GPoint mp = layout->animation_state.list_tmr_icon_pos;
          int32_t tp2 = (p > FLIGHT_START + ANIMATION_NORMALIZED_MAX * 12 / 100)
              ? (p - FLIGHT_START - ANIMATION_NORMALIZED_MAX * 12 / 100) : 0;
          int32_t ft2 = tp2 * ANIMATION_NORMALIZED_MAX /
              (FLIGHT_END - FLIGHT_START - ANIMATION_NORMALIZED_MAX * 12 / 100 + 1);
          if (ft2 > ANIMATION_NORMALIZED_MAX) ft2 = ANIMATION_NORMALIZED_MAX;
          int32_t intensity2 = weather_norm_bell(ft2);
          for (int i = 0; i < 3; i++) {
            int len = (int)((int32_t)15 * (int32_t)len_scale[i] * intensity2
                            / (100 * (int32_t)ANIMATION_NORMALIZED_MAX));
            if (len < 2) continue;
            int y = mp.y + dy_offsets[i];
            int x0 = is_return ? (mp.x - 2) : (mp.x + 2);
            int x1 = is_return ? (x0 - len)  : (x0 + len);
            graphics_draw_line(context, GPoint(x0, y), GPoint(x1, y));
          }
        }
      }
    }
  } else {
    if (layout->forecast) {
      prv_draw_circle_at_layer(bitmap_layer_get_layer(layout->current_weather_icon_layer),
                               context, layout->forecast->current_weather_type);

      // Lava lamp glow: two counter-rotating half-arcs + orbiting bubble sparkles.
      if (!layout->glow_paused) {
      GColor glow_color = weather_type_get_bg_color(layout->forecast->current_weather_type);
      if (gcolor_equal(glow_color, GColorClear)) glow_color = GColorVividCerulean;
      {
        GRect tdr = layout->today_icon_rest_frame;
        uint8_t idle_progress = layout->glow_idle_ticks >= GLOW_IDLE_TICKS
            ? layout->glow_idle_ticks - GLOW_IDLE_TICKS : 0;
        weather_draw_lava_ring(context,
                               GPoint(tdr.origin.x + tdr.size.w / 2,
                                      tdr.origin.y + tdr.size.h / 2),
                               tdr.size.w * 7 / 10 + GLOW_OUTER_EXTRA,
                               glow_color, layout->glow_phase,
                               idle_progress);
      }
      } // end if (!glow_paused)
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

  // During right-swipe list transition, return transition, OR clock-face transition:
  // hide all text and just show icons + circles.
  if (layout->animation_state.transitioning_to_list
      || layout->animation_state.returning_from_list
      || layout->clock_transit.transitioning_to_clock) {
    prv_draw_weather_icon_backgrounds(layout, context);
    return;
  }

  // Separator fixed at 2/3 of the content layer height for a consistent today:tomorrow split
  const int separator_y = bounds.size.h * 2 / 3 + WEATHER_APP_LAYOUT_SEPARATOR_Y_ADJUST;
  const int full_h = bounds.size.h;

  if (layout->text_anim.active) {
    AnimationProgress p = layout->text_anim.progress;
    bool dir_down = layout->text_anim.dir_down;

    int out_dy;
    int in_dy;
    if (layout->interactive.kind == WeatherInteractiveTransition_Day
        && layout->interactive.use_floaty_day) {
      int slide = (int)((int32_t)full_h * (int32_t)p / (int32_t)ANIMATION_NORMALIZED_MAX);
      out_dy = dir_down ? -slide : slide;
      in_dy  = dir_down ? (full_h - slide) : (-full_h + slide);
    } else {
      out_dy = prv_interpolate_text_moook(p, 0, dir_down ? -full_h : full_h);
      in_dy  = prv_interpolate_text_moook(p, dir_down ? full_h : -full_h, 0);
    }

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
    if (!layout->animation_state.hide_bottom_half_text) {
      if (layout->text_anim.bot_valid) {
        GPoint bot_out = GPoint(content_x_offset, separator_y + out_dy);
        prv_draw_snapshot_bot(layout, &bot_out, content_width, context);
      }
      if (layout->next_forecast) {
        GPoint bot_in = GPoint(content_x_offset, separator_y + in_dy);
        prv_draw_bottom_half_text(layout, &bot_in, content_width, context);
      }
    }

    // Re-clip top region to erase any bottom-half text that bled upward.
    graphics_context_set_fill_color(context, GColorWhite);
    graphics_fill_rect(context, GRect(0, 0, bounds.size.w, separator_y), 0, GCornerNone);
    top_out = GPoint(content_x_offset, 1 + out_dy);
    prv_draw_snapshot_top(layout, &top_out, content_width, context);
    top_in = GPoint(content_x_offset, 1 + in_dy);
    prv_draw_top_half_text(layout, &top_in, content_width, context);

  } else {
    // Static (no text animation active)
    GPoint current_offset = GPoint(content_x_offset, 1);
    prv_draw_top_half_text(layout, &current_offset, content_width, context);

    if (!layout->animation_state.hide_bottom_half_text) {
      if (layout->next_forecast) {
        current_offset = GPoint(content_x_offset, separator_y);
        prv_draw_bottom_half_text(layout, &current_offset, content_width, context);
      } else if (layout->forecast && layout->fin_pdc && !layout->icon_animation) {
        // Handled by the Timeline Fin bitmap layer.
      }
    }
  }

  // Dotted separator — drawn last so it always sits on top of sliding text.
  graphics_context_set_stroke_width(context, 1);
  graphics_context_set_stroke_color(context, PBL_IF_COLOR_ELSE(GColorLightGray, GColorBlack));
  {
    const int dot_w = 2;
    const int gap_w = 3;
    int x = 0;
    while (x < bounds.size.w) {
      int x_end = x + dot_w - 1;
      if (x_end >= bounds.size.w) x_end = bounds.size.w - 1;
      graphics_draw_line(context, GPoint(x, separator_y),
                         GPoint(x_end, separator_y));
      x += dot_w + gap_w;
    }
  }

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
  graphics_context_set_fill_color(ctx, GColorVividCerulean);
#if PBL_ROUND
  const int radius = WEATHER_APP_LAYOUT_ROUND_BAR_RADIUS;
  const int center_y = WEATHER_APP_LAYOUT_ROUND_BAR_DEPTH - radius +
                       WEATHER_APP_LAYOUT_ROUND_BAR_Y_ADJUST;
  graphics_fill_circle(ctx, GPoint(bounds.size.w / 2, center_y), radius);
#else
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);
#endif
}

#if PBL_ROUND
static void prv_draw_round_pull_drawer(GContext *ctx, const WeatherAppLayout *layout,
                                       GRect bounds) {
  if (!layout || layout->pull_offset <= 0) return;

  const int radius = WEATHER_APP_LAYOUT_ROUND_BAR_RADIUS;
  const int cap_center_y = WEATHER_APP_LAYOUT_ROUND_BAR_DEPTH - radius +
                           WEATHER_APP_LAYOUT_ROUND_BAR_Y_ADJUST;
  const int cap_bottom_y = cap_center_y + radius;
  const int pull_bottom_y = layout->pull_content_rest_frame.origin.y + layout->pull_offset;
  const int shifted_center_y = cap_center_y + pull_bottom_y - cap_bottom_y;

  graphics_context_set_fill_color(ctx, prv_pull_bg_color(layout));
  graphics_fill_circle(ctx, GPoint(bounds.size.w / 2, shifted_center_y), radius);
}

static void prv_draw_round_pull_spinner(GContext *ctx, const WeatherAppLayout *layout,
                                        GRect bounds) {
  if (!layout || layout->pull_offset < PULL_HINT_PX) return;

  const int radius = WEATHER_APP_LAYOUT_ROUND_BAR_RADIUS;
  const int cap_center_y = WEATHER_APP_LAYOUT_ROUND_BAR_DEPTH - radius +
                           WEATHER_APP_LAYOUT_ROUND_BAR_Y_ADJUST;
  const int cap_bottom_y = cap_center_y + radius;
  const int pull_bottom_y = layout->pull_content_rest_frame.origin.y + layout->pull_offset;
  const int spinner_y = (cap_bottom_y + pull_bottom_y) / 2;

  prv_draw_pull_spinner(layout, ctx, bounds.size.w / 2, spinner_y, layout->pull_offset);
}
#endif

static void prv_draw_location_bar(Layer *layer, GContext *ctx) {
  const WeatherAppLayout *layout = *(WeatherAppLayout **)layer_get_data(layer);
  GRect bounds = layer_get_bounds(layer);
  int bar_w = bounds.size.w;
  const int safe = WEATHER_APP_LAYOUT_LOCATION_BAR_TEXT_INSET;
#if PBL_ROUND
  prv_draw_round_pull_drawer(ctx, layout, bounds);
#endif
  prv_draw_location_bar_background(ctx, bounds);
#if PBL_ROUND
  prv_draw_round_pull_spinner(ctx, layout, bounds);
#endif
  graphics_context_set_text_color(ctx, GColorWhite);
  GFont font = fonts_get_system_font(PBL_IF_ROUND_ELSE(FONT_KEY_GOTHIC_14_BOLD,
                                                       FONT_KEY_GOTHIC_18_BOLD));

  if (layout->refresh_banner_active) {
    // Phase breakpoints in normalized progress (0 → ANIMATION_NORMALIZED_MAX over 2400ms)
    const AnimationProgress P_IN   = (AnimationProgress)((int32_t)ANIMATION_NORMALIZED_MAX * 200  / 2400);
    const AnimationProgress P_HOLD = (AnimationProgress)((int32_t)ANIMATION_NORMALIZED_MAX * 2200 / 2400);
    AnimationProgress p = layout->refresh_banner_progress;

#if PBL_ROUND
    const char *banner = layout->refresh_banner_text;
#else
    const char *banner = "last refresh";
#endif

    int banner_x = 0;
    int normal_x = 0;
    bool draw_normal = false;
    if (p <= P_IN) {
      int32_t t = weather_scale_i32(p, ANIMATION_NORMALIZED_MAX, P_IN);
      banner_x = -bar_w + (int)((int32_t)bar_w * t / ANIMATION_NORMALIZED_MAX);
    } else if (p > P_HOLD) {
      int32_t t = weather_scale_i32(p - P_HOLD, ANIMATION_NORMALIZED_MAX,
                                    ANIMATION_NORMALIZED_MAX - P_HOLD);
      banner_x = (int)((int32_t)bar_w * t / ANIMATION_NORMALIZED_MAX);
      normal_x = -bar_w + banner_x;
      draw_normal = true;
    }
#if PBL_ROUND
    graphics_draw_text(ctx, banner, font,
        GRect(safe + banner_x, WEATHER_APP_LAYOUT_ROUND_REFRESH_TEXT_Y,
              bar_w - (safe * 2), 19),
        GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
#else
    graphics_draw_text(ctx, banner, font,
        GRect(safe + 4 + banner_x, WEATHER_APP_LAYOUT_LOCATION_BAR_TEXT_Y,
              bar_w - (safe * 2) - 4,
              bounds.size.h - WEATHER_APP_LAYOUT_LOCATION_BAR_TEXT_Y),
        GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
#endif
    if (draw_normal) {
      prv_draw_location_bar_content(ctx, layout, bounds, font, normal_x, 0);
    }
    return;
  }

#if PBL_ROUND
  prv_draw_location_bar_content(ctx, layout, bounds, font, 0, 0);
  return;
#endif

  // Animate text during bar resize: slide in from above (growing) or slide up out (shrinking).
  GFont bar_font;
  int y_off = 0;
  if (layout->bar_anim_progress > 0) {
    int32_t p = (int32_t)layout->bar_anim_progress;
    if (layout->bar_anim_shrinking) {
      // Shrinking: GOTHIC_14 slides up into view (arrives from below, y_off: +N→0)
      bar_font = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
      y_off = (int)((int32_t)20 * (ANIMATION_NORMALIZED_MAX - p) / ANIMATION_NORMALIZED_MAX);
    } else {
      // Growing: GOTHIC_18 drops down into view (arrives from above, y_off: -N→0)
      bar_font = fonts_get_system_font(PBL_IF_ROUND_ELSE(FONT_KEY_GOTHIC_14_BOLD,
                                                         FONT_KEY_GOTHIC_18_BOLD));
      y_off = -(int)((int32_t)20 * (ANIMATION_NORMALIZED_MAX - p) / ANIMATION_NORMALIZED_MAX);
    }
  } else {
    bar_font = fonts_get_system_font(PBL_IF_ROUND_ELSE(FONT_KEY_GOTHIC_14_BOLD,
                                                       FONT_KEY_GOTHIC_18_BOLD));
  }
  prv_draw_location_bar_content(ctx, layout, bounds, bar_font, 0, y_off);
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

static void prv_apply_day_transition_progress(WeatherAppLayout *layout,
                                              AnimationProgress progress) {
  layout->text_anim.progress = progress;
  const int32_t motion_p = layout->anim_params.animate_down
      ? prv_interpolate_icon_landing_progress_down(progress)
      : prv_interpolate_icon_landing_progress(progress);

  GPoint cc      = layout->anim_params.circle_center;
  int32_t r      = layout->anim_params.radius;
  GSize today_sz = layout->today_icon_rest_frame.size;
  GSize tmr_sz   = layout->tomorrow_icon_rest_frame.size;

  GRect in_frame;
  GRect out_frame;

  if (layout->anim_params.animate_down) {
    // DOWN — roles swapped vs UP:
    //   outgoing_weather_icon_layer (scaler)      = INCOMING: scales up from tomorrow_rest
    //   current_weather_icon_layer  (BitmapLayer) = OUTGOING: sweeps along arc and exits

    // Incoming (scaler): straight-line from tomorrow_rest → today_rest while scaling up.
    // This is the exact reverse of UP’s outgoing shrink path — same motion, opposite direction.
    GRect tmr_rest = layout->tomorrow_icon_rest_frame;
    GRect tdy_rest = layout->today_icon_rest_frame;
    int16_t t_cx = (int16_t)(tmr_rest.origin.x + tmr_rest.size.w / 2);
    int16_t t_cy = (int16_t)(tmr_rest.origin.y + tmr_rest.size.h / 2);
    int16_t d_cx = (int16_t)(tdy_rest.origin.x + tdy_rest.size.w / 2);
    int16_t d_cy = (int16_t)(tdy_rest.origin.y + tdy_rest.size.h / 2);
    int16_t cx = t_cx + (int16_t)((int32_t)(d_cx - t_cx) * motion_p / ANIMATION_NORMALIZED_MAX);
    int16_t cy = t_cy + (int16_t)((int32_t)(d_cy - t_cy) * motion_p / ANIMATION_NORMALIZED_MAX);
    int16_t sw = (int16_t)((int32_t)tmr_sz.w +
        (int32_t)(today_sz.w - tmr_sz.w) * motion_p / ANIMATION_NORMALIZED_MAX);
    int16_t sh = (int16_t)((int32_t)tmr_sz.h +
        (int32_t)(today_sz.h - tmr_sz.h) * motion_p / ANIMATION_NORMALIZED_MAX);
    in_frame = (GRect){ .origin = GPoint(cx - sw / 2, cy - sh / 2), .size = GSize(sw, sh) };

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
      GRect tmr_rest_root = (GRect){
        GPoint(layout->content_layer_origin.x + layout->tomorrow_icon_rest_frame.origin.x,
               layout->content_layer_origin.y + layout->tomorrow_icon_rest_frame.origin.y),
        tmr_sz2
      };
      int16_t tx = tmr_arc_start.origin.x +
          (int16_t)((int32_t)(tmr_rest_root.origin.x - tmr_arc_start.origin.x)
                    * motion_p / ANIMATION_NORMALIZED_MAX);
      int16_t ty = tmr_arc_start.origin.y +
          (int16_t)((int32_t)(tmr_rest_root.origin.y - tmr_arc_start.origin.y)
                    * motion_p / ANIMATION_NORMALIZED_MAX);
      layer_set_frame(bitmap_layer_get_layer(layout->tomorrow_weather_icon_layer),
                      (GRect){ GPoint(tx, ty), tmr_sz2 });
    }
  } else {
    // UP: incoming (previous day) arrives full-size along arc, no scaling
    int32_t in_angle = layout->anim_params.incoming_start_angle +
        weather_scale_i32(ICON_ARC_TODAY_REST -
                              layout->anim_params.incoming_start_angle,
                          motion_p, ANIMATION_NORMALIZED_MAX);
    in_frame = prv_icon_frame_at_angle(cc, r, in_angle, today_sz);

    // Outgoing (old today) shrinks and slides straight to tomorrow slot
    GRect t_rest = layout->today_icon_rest_frame;
    GRect m_rest = layout->tomorrow_icon_rest_frame;
    int16_t t_cx = (int16_t)(t_rest.origin.x + t_rest.size.w / 2);
    int16_t t_cy = (int16_t)(t_rest.origin.y + t_rest.size.h / 2);
    int16_t m_cx = (int16_t)(m_rest.origin.x + m_rest.size.w / 2);
    int16_t m_cy = (int16_t)(m_rest.origin.y + m_rest.size.h / 2);
    int16_t cx = t_cx + (int16_t)((int32_t)(m_cx - t_cx) * motion_p / ANIMATION_NORMALIZED_MAX);
    int16_t cy = t_cy + (int16_t)((int32_t)(m_cy - t_cy) * motion_p / ANIMATION_NORMALIZED_MAX);
    int16_t sw = (int16_t)((int32_t)today_sz.w +
        (int32_t)(tmr_sz.w - today_sz.w) * motion_p / ANIMATION_NORMALIZED_MAX);
    int16_t sh = (int16_t)((int32_t)today_sz.h +
        (int32_t)(tmr_sz.h - today_sz.h) * motion_p / ANIMATION_NORMALIZED_MAX);
    out_frame = (GRect){ .origin = GPoint(cx - sw / 2, cy - sh / 2), .size = GSize(sw, sh) };

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
      GRect tmr_start = (GRect){
        GPoint(layout->content_layer_origin.x + layout->tomorrow_icon_rest_frame.origin.x,
               layout->content_layer_origin.y + layout->tomorrow_icon_rest_frame.origin.y),
        tmr_sz2
      };
      GRect tmr_end = prv_icon_frame_at_angle(cc_root, layout->anim_params.radius,
                                               layout->anim_params.outgoing_end_angle, tmr_sz2);

      // Faster exit: accelerate so it's off-screen well before animation ends.
      int32_t fast_p = motion_p * 5 / 3;
      if (fast_p > ANIMATION_NORMALIZED_MAX) fast_p = ANIMATION_NORMALIZED_MAX;

      int16_t tx = tmr_start.origin.x + (int16_t)((int32_t)(tmr_end.origin.x - tmr_start.origin.x)
                   * fast_p / ANIMATION_NORMALIZED_MAX);
      int16_t ty = tmr_start.origin.y + (int16_t)((int32_t)(tmr_end.origin.y - tmr_start.origin.y)
                   * fast_p / ANIMATION_NORMALIZED_MAX);
      layer_set_frame(bitmap_layer_get_layer(layout->tomorrow_weather_icon_layer),
                      (GRect){ GPoint(tx, ty), tmr_sz2 });
    }
  }

  layer_mark_dirty(layout->root_layer);
}

static void prv_apply_day_transition_progress_floaty(WeatherAppLayout *layout,
                                                     AnimationProgress progress) {
  layout->text_anim.progress = progress;

  GPoint cc      = layout->anim_params.circle_center;
  int32_t r      = layout->anim_params.radius;
  GSize today_sz = layout->today_icon_rest_frame.size;
  GSize tmr_sz   = layout->tomorrow_icon_rest_frame.size;

  GRect in_frame;
  GRect out_frame;

  if (layout->anim_params.animate_down) {
    GRect tmr_rest = layout->tomorrow_icon_rest_frame;
    GRect tdy_rest = layout->today_icon_rest_frame;
    int16_t t_cx = (int16_t)(tmr_rest.origin.x + tmr_rest.size.w / 2);
    int16_t t_cy = (int16_t)(tmr_rest.origin.y + tmr_rest.size.h / 2);
    int16_t d_cx = (int16_t)(tdy_rest.origin.x + tdy_rest.size.w / 2);
    int16_t d_cy = (int16_t)(tdy_rest.origin.y + tdy_rest.size.h / 2);
    int16_t cx = t_cx + (int16_t)((int32_t)(d_cx - t_cx) * progress / ANIMATION_NORMALIZED_MAX);
    int16_t cy = t_cy + (int16_t)((int32_t)(d_cy - t_cy) * progress / ANIMATION_NORMALIZED_MAX);
    int16_t sw = (int16_t)((int32_t)tmr_sz.w +
        (int32_t)(today_sz.w - tmr_sz.w) * progress / ANIMATION_NORMALIZED_MAX);
    int16_t sh = (int16_t)((int32_t)tmr_sz.h +
        (int32_t)(today_sz.h - tmr_sz.h) * progress / ANIMATION_NORMALIZED_MAX);
    in_frame = (GRect){ .origin = GPoint(cx - sw / 2, cy - sh / 2), .size = GSize(sw, sh) };

    int32_t out_angle = layout->anim_params.outgoing_start_angle +
        weather_scale_i32(layout->anim_params.outgoing_end_angle -
                              layout->anim_params.outgoing_start_angle,
                          progress, ANIMATION_NORMALIZED_MAX);
    out_frame = prv_icon_frame_at_angle(cc, r, out_angle, today_sz);

    prv_set_outgoing_weather_icon_frame(layout, in_frame);
    layer_set_frame(bitmap_layer_get_layer(layout->current_weather_icon_layer), out_frame);
#if WEATHER_APP_LAYOUT_USE_PDC_WEATHER_ICONS
    if (layout->anim_params.current_root_overlay) {
      prv_set_current_escape_icon_frame(layout, out_frame);
    }
#endif

    if (layout->anim_params.tomorrow_incoming) {
      GSize tmr_sz2 = layout->tomorrow_icon_rest_frame.size;
      GPoint cc_root = GPoint(
          layout->anim_params.circle_center.x + layout->content_layer_origin.x,
          layout->anim_params.circle_center.y + layout->content_layer_origin.y);
      GRect tmr_arc_start = prv_icon_frame_at_angle(cc_root, layout->anim_params.radius,
                                                     layout->anim_params.incoming_start_angle,
                                                     tmr_sz2);
      GRect tmr_rest_root = (GRect){
        GPoint(layout->content_layer_origin.x + layout->tomorrow_icon_rest_frame.origin.x,
               layout->content_layer_origin.y + layout->tomorrow_icon_rest_frame.origin.y),
        tmr_sz2
      };
      int16_t tx = tmr_arc_start.origin.x +
          (int16_t)((int32_t)(tmr_rest_root.origin.x - tmr_arc_start.origin.x)
                    * progress / ANIMATION_NORMALIZED_MAX);
      int16_t ty = tmr_arc_start.origin.y +
          (int16_t)((int32_t)(tmr_rest_root.origin.y - tmr_arc_start.origin.y)
                    * progress / ANIMATION_NORMALIZED_MAX);
      layer_set_frame(bitmap_layer_get_layer(layout->tomorrow_weather_icon_layer),
                      (GRect){ GPoint(tx, ty), tmr_sz2 });
    }
  } else {
    int32_t in_angle = layout->anim_params.incoming_start_angle +
        weather_scale_i32(ICON_ARC_TODAY_REST -
                              layout->anim_params.incoming_start_angle,
                          progress, ANIMATION_NORMALIZED_MAX);
    in_frame = prv_icon_frame_at_angle(cc, r, in_angle, today_sz);

    GRect t_rest = layout->today_icon_rest_frame;
    GRect m_rest = layout->tomorrow_icon_rest_frame;
    int16_t t_cx = (int16_t)(t_rest.origin.x + t_rest.size.w / 2);
    int16_t t_cy = (int16_t)(t_rest.origin.y + t_rest.size.h / 2);
    int16_t m_cx = (int16_t)(m_rest.origin.x + m_rest.size.w / 2);
    int16_t m_cy = (int16_t)(m_rest.origin.y + m_rest.size.h / 2);
    int16_t cx = t_cx + (int16_t)((int32_t)(m_cx - t_cx) * progress / ANIMATION_NORMALIZED_MAX);
    int16_t cy = t_cy + (int16_t)((int32_t)(m_cy - t_cy) * progress / ANIMATION_NORMALIZED_MAX);
    int16_t sw = (int16_t)((int32_t)today_sz.w +
        (int32_t)(tmr_sz.w - today_sz.w) * progress / ANIMATION_NORMALIZED_MAX);
    int16_t sh = (int16_t)((int32_t)today_sz.h +
        (int32_t)(tmr_sz.h - today_sz.h) * progress / ANIMATION_NORMALIZED_MAX);
    out_frame = (GRect){ .origin = GPoint(cx - sw / 2, cy - sh / 2), .size = GSize(sw, sh) };

    prv_set_outgoing_weather_icon_frame(layout, out_frame);
    layer_set_frame(bitmap_layer_get_layer(layout->current_weather_icon_layer), in_frame);
#if WEATHER_APP_LAYOUT_USE_PDC_WEATHER_ICONS
    if (layout->anim_params.current_root_overlay) {
      prv_set_current_escape_icon_frame(layout, in_frame);
    }
#endif

    if (layout->anim_params.tomorrow_reparented) {
      GSize tmr_sz2 = layout->tomorrow_icon_rest_frame.size;
      GPoint cc_root = GPoint(
          layout->anim_params.circle_center.x + layout->content_layer_origin.x,
          layout->anim_params.circle_center.y + layout->content_layer_origin.y);
      GRect tmr_start = (GRect){
        GPoint(layout->content_layer_origin.x + layout->tomorrow_icon_rest_frame.origin.x,
               layout->content_layer_origin.y + layout->tomorrow_icon_rest_frame.origin.y),
        tmr_sz2
      };
      GRect tmr_end = prv_icon_frame_at_angle(cc_root, layout->anim_params.radius,
                                               layout->anim_params.outgoing_end_angle, tmr_sz2);
      int32_t fast_p = progress * 5 / 3;
      if (fast_p > ANIMATION_NORMALIZED_MAX) fast_p = ANIMATION_NORMALIZED_MAX;
      int16_t tx = tmr_start.origin.x + (int16_t)((int32_t)(tmr_end.origin.x - tmr_start.origin.x)
                   * fast_p / ANIMATION_NORMALIZED_MAX);
      int16_t ty = tmr_start.origin.y + (int16_t)((int32_t)(tmr_end.origin.y - tmr_start.origin.y)
                   * fast_p / ANIMATION_NORMALIZED_MAX);
      layer_set_frame(bitmap_layer_get_layer(layout->tomorrow_weather_icon_layer),
                      (GRect){ GPoint(tx, ty), tmr_sz2 });
    }
  }

  layer_mark_dirty(layout->root_layer);
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
      if (layout->current_weather_icon) gbitmap_destroy(layout->current_weather_icon);
      layout->current_weather_icon = layout->forecast ?
          gbitmap_create_with_resource(
              weather_type_get_icon_res_today(layout->forecast->current_weather_type)) : NULL;
      bitmap_layer_set_bitmap(layout->current_weather_icon_layer, layout->current_weather_icon);
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
        if (layout->tomorrow_weather_icon) {
          gbitmap_destroy(layout->tomorrow_weather_icon);
          layout->tomorrow_weather_icon = NULL;
        }
        if (layout->next_forecast) {
          layout->tomorrow_weather_icon = gbitmap_create_with_resource(
              weather_type_get_icon_res_tiny(layout->next_forecast->current_weather_type));
          bitmap_layer_set_bitmap(layout->tomorrow_weather_icon_layer, layout->tomorrow_weather_icon);
        } else {
          bitmap_layer_set_bitmap(layout->tomorrow_weather_icon_layer, NULL);
        }
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
static int prv_list_gabbro_content_inset(int screen_y, int screen_h) {
  int radius = screen_h / 2;
  int y_offset = screen_y - radius;
  int32_t radius_sq = (int32_t)radius * radius;
  int32_t y_sq = (int32_t)y_offset * y_offset;
  int32_t sqrt_arg = radius_sq - y_sq;
  if (sqrt_arg < 0) sqrt_arg = 0;
  return LIST_GABBRO_ICON_BASE_X + radius - (int)weather_isqrt(sqrt_arg) +
      (int)(y_sq / LIST_GABBRO_CURVE_BOOST_DIVISOR);
}

static void prv_list_gabbro_icon_center(WeatherAppLayout *layout,
                                        int row_offset,
                                        int icon_sz,
                                        int *x_c,
                                        int *y_c) {
  int screen_h = layer_get_bounds(layout->root_layer).size.h;
  int max_scroll = layout->animation_state.list_num_days - 1;
  if (max_scroll < 0) max_scroll = 0;
  max_scroll *= LIST_GABBRO_ROW_PITCH;
  int scroll_offset = layout->animation_state.list_start_day * LIST_GABBRO_ROW_PITCH;
  if (scroll_offset > max_scroll) scroll_offset = max_scroll;

  int top_y = WEATHER_APP_LAYOUT_ROUND_BAR_DEPTH + LIST_GABBRO_TOP_FOCUS_GAP;
  int bottom_y = screen_h - LIST_GABBRO_BOTTOM_FOCUS_MARGIN;
  if (bottom_y < top_y) bottom_y = top_y;
  int focus_y = top_y;
  if (max_scroll > 0) {
    focus_y += weather_scale_i32(bottom_y - top_y, scroll_offset, max_scroll);
  }

  int y_screen = focus_y + LIST_GABBRO_CENTER_Y_SHIFT +
                 row_offset * LIST_GABBRO_ROW_PITCH;
  int x_screen = prv_list_gabbro_content_inset(y_screen, screen_h) + icon_sz / 2;
  *x_c = x_screen - layout->content_layer_origin.x;
  *y_c = y_screen - layout->content_layer_origin.y;
}
#endif

// Resize the location bar to the given height (top of screen, full width).
static void prv_set_bar_height(WeatherAppLayout *layout, int h) {
  GRect root_bounds = layer_get_bounds(layout->root_layer);
#if PBL_ROUND
  h = WEATHER_APP_LAYOUT_ROUND_PULL_LAYER_HEIGHT;
#endif
  layer_set_frame(layout->location_bar_layer,
                  GRect(0, WEATHER_APP_LAYOUT_BAR_LAYER_Y, root_bounds.size.w, h));
}

static void prv_apply_list_transition_progress(WeatherAppLayout *layout,
                                               AnimationProgress progress) {
  // Store progress for speed-line drawing in background proc.
  layout->animation_state.list_transit_progress = progress;

  // Shrink bar from MAIN_BAR_HEIGHT → LOCATION_BAR_HEIGHT as we fly to list.
  int bar_h = WEATHER_APP_LAYOUT_MAIN_BAR_HEIGHT
      - (int)((int32_t)(WEATHER_APP_LAYOUT_MAIN_BAR_HEIGHT - WEATHER_APP_LAYOUT_LOCATION_BAR_HEIGHT)
              * (int32_t)progress / (int32_t)ANIMATION_NORMALIZED_MAX);
  prv_set_bar_height(layout, bar_h);
  layout->bar_anim_progress = progress;
  layout->bar_anim_shrinking = true;

  int icon_sz  = (int)layout->tomorrow_icon_rest_frame.size.w;  // 25 px

#if defined(PBL_PLATFORM_GABBRO)
  int to_x0_c, to_x1_c, to_y0_c, to_y1_c;
  prv_list_gabbro_icon_center(layout, 0, icon_sz, &to_x0_c, &to_y0_c);
  prv_list_gabbro_icon_center(layout, 1, icon_sz, &to_x1_c, &to_y1_c);
#else
  int screen_h = layer_get_bounds(layout->root_layer).size.h;
  int rh       = (screen_h - WEATHER_APP_LAYOUT_LOCATION_BAR_HEIGHT) / LIST_ROWS_VISIBLE;

  // Compute which visual row the focused day lands on after scroll clamping.
  int _max_s_rows = layout->animation_state.list_num_days - LIST_ROWS_VISIBLE;
  if (_max_s_rows < 0) _max_s_rows = 0;
  int _eff = layout->animation_state.list_start_day < _max_s_rows
             ? layout->animation_state.list_start_day : _max_s_rows;
  int vis_row = layout->animation_state.list_start_day - _eff;

  // Target centres in content-layer coords
  int to_x0_c = LIST_ICON_X + icon_sz / 2 - layout->content_layer_origin.x;
  int to_x1_c = to_x0_c;
  int row0_y_screen = WEATHER_APP_LAYOUT_LOCATION_BAR_HEIGHT + vis_row * rh + (rh - icon_sz) / 2;
  int row1_y_screen = row0_y_screen + rh;
  int to_y0_c = row0_y_screen + icon_sz / 2 - layout->content_layer_origin.y;
  int to_y1_c = row1_y_screen + icon_sz / 2 - layout->content_layer_origin.y;
#endif

  // ---- Pebble "collapse to square then fly" pattern ----
  // Mirrors cards-example: attract_draw_command_image_to_square (EaseIn) then
  // reverse from square at destination (EaseOut), but for bitmaps:
  //
  // Phase 1 [0–28%]  : COLLAPSE at origin — icon squashes to a flat horizontal bar
  // Phase 2 [28–80%] : FLIGHT   — flat bar slides left to destination (speed-line zone)
  // Phase 3 [80–100%]: EXPAND at destination — bar springs back to icon_sz square

  const int32_t P1 = ANIMATION_NORMALIZED_MAX * 28 / 100;
  const int32_t P2 = ANIMATION_NORMALIZED_MAX * 80 / 100;

  GRect from_t = layout->today_icon_rest_frame;
  int16_t fc_x = (int16_t)(from_t.origin.x + from_t.size.w / 2);
  int16_t fc_y = (int16_t)(from_t.origin.y + from_t.size.h / 2);
  int src_sz   = (int)from_t.size.w;  // 50 on emery

  // Collapsed bar dimensions: wide+very flat (matches the cards-example "to-square" squash)
  const int16_t BAR_W = (int16_t)(src_sz * 130 / 100);  // 130% width
  const int16_t BAR_H = 4;                               // 4px tall — the "flat line"

  int16_t cx, cy, sw, sh;

  if (progress <= P1) {
    // Collapse: ease-in shrink to flat bar, stay at origin
    int32_t t = weather_scale_i32(progress, ANIMATION_NORMALIZED_MAX, P1);
    // EaseIn feel: t² curve
    int32_t t2 = weather_norm_square(t);
    cx = fc_x;
    cy = fc_y;
    sw = (int16_t)(src_sz  + (int32_t)(BAR_W - src_sz) * t2 / ANIMATION_NORMALIZED_MAX);
    sh = (int16_t)(src_sz  + (int32_t)(BAR_H - src_sz) * t2 / ANIMATION_NORMALIZED_MAX);
  } else if (progress <= P2) {
    // Flight: translate flat bar from origin to destination
    int32_t t = weather_scale_i32(progress - P1, ANIMATION_NORMALIZED_MAX,
                                  P2 - P1);
    cx = (int16_t)((int32_t)fc_x + (int32_t)(to_x0_c - fc_x) * t / ANIMATION_NORMALIZED_MAX);
    cy = (int16_t)((int32_t)fc_y + (int32_t)(to_y0_c - fc_y) * t / ANIMATION_NORMALIZED_MAX);
    sw = BAR_W;
    sh = BAR_H;
  } else {
    // Expand: ease-out spring from flat bar to icon_sz at destination
    int32_t t = weather_scale_i32(progress - P2, ANIMATION_NORMALIZED_MAX,
                                  ANIMATION_NORMALIZED_MAX - P2);
    // EaseOut: 1-(1-t)² curve
    int32_t inv = ANIMATION_NORMALIZED_MAX - t;
    int32_t te  = ANIMATION_NORMALIZED_MAX - weather_norm_square(inv);
    cx = (int16_t)to_x0_c;
    cy = (int16_t)to_y0_c;
    sw = (int16_t)(BAR_W  + (int32_t)(icon_sz - BAR_W)  * te / ANIMATION_NORMALIZED_MAX);
    sh = (int16_t)(BAR_H  + (int32_t)(icon_sz - BAR_H)  * te / ANIMATION_NORMALIZED_MAX);
    // Landing squash overshoot: brief extra width peak then settle
    int32_t bell = weather_norm_bell(t);
    sw = (int16_t)((int32_t)sw + (int32_t)(icon_sz * 28 / 100) * bell / ANIMATION_NORMALIZED_MAX);
    sh = (int16_t)((int32_t)sh - (int32_t)(icon_sz * 20 / 100) * bell / ANIMATION_NORMALIZED_MAX);
  }

  if (sw < 2) sw = 2;
  if (sh < 2) sh = 2;
  layer_set_frame(layout->outgoing_weather_icon_layer,
                  GRect(cx - sw / 2, cy - sh / 2, sw, sh));
  // Store centre for speed-line drawing.
  ((WeatherAppLayout *)layout)->animation_state.list_today_icon_pos = GPoint(cx, cy);

  // ---- Tomorrow icon: same collapse→fly→expand, delayed 15% ----
  const int32_t TMR_DELAY = ANIMATION_NORMALIZED_MAX * 15 / 100;
  int32_t tp = (progress > TMR_DELAY)
      ? weather_scale_i32(progress - TMR_DELAY, ANIMATION_NORMALIZED_MAX,
                          ANIMATION_NORMALIZED_MAX - TMR_DELAY)
      : 0;

  GRect from_m   = layout->tomorrow_icon_rest_frame;
  int16_t fm_x   = (int16_t)(from_m.origin.x + from_m.size.w / 2);
  int16_t fm_y   = (int16_t)(from_m.origin.y + from_m.size.h / 2);

  const int16_t TBAR_W = (int16_t)(icon_sz * 130 / 100);
  const int16_t TBAR_H = 3;

  int16_t mx, my, msw, msh;

  if (tp <= P1) {
    int32_t t = weather_scale_i32(tp, ANIMATION_NORMALIZED_MAX, P1);
    int32_t t2 = weather_norm_square(t);
    mx = fm_x;  my = fm_y;
    msw = (int16_t)(icon_sz + (int32_t)(TBAR_W - icon_sz) * t2 / ANIMATION_NORMALIZED_MAX);
    msh = (int16_t)(icon_sz + (int32_t)(TBAR_H - icon_sz) * t2 / ANIMATION_NORMALIZED_MAX);
  } else if (tp <= P2) {
    int32_t t = weather_scale_i32(tp - P1, ANIMATION_NORMALIZED_MAX,
                                  P2 - P1);
    mx  = (int16_t)((int32_t)fm_x + (int32_t)(to_x1_c - fm_x) * t / ANIMATION_NORMALIZED_MAX);
    my  = (int16_t)((int32_t)fm_y + (int32_t)(to_y1_c - fm_y) * t / ANIMATION_NORMALIZED_MAX);
    msw = TBAR_W;  msh = TBAR_H;
  } else {
    int32_t t   = weather_scale_i32(tp - P2, ANIMATION_NORMALIZED_MAX,
                                    ANIMATION_NORMALIZED_MAX - P2);
    int32_t inv = ANIMATION_NORMALIZED_MAX - t;
    int32_t te  = ANIMATION_NORMALIZED_MAX - weather_norm_square(inv);
    mx = (int16_t)to_x1_c;  my = (int16_t)to_y1_c;
    msw = (int16_t)(TBAR_W + (int32_t)(icon_sz - TBAR_W) * te / ANIMATION_NORMALIZED_MAX);
    msh = (int16_t)(TBAR_H + (int32_t)(icon_sz - TBAR_H) * te / ANIMATION_NORMALIZED_MAX);
    int32_t bell = weather_norm_bell(t);
    msw = (int16_t)((int32_t)msw + (int32_t)(icon_sz * 22 / 100) * bell / ANIMATION_NORMALIZED_MAX);
    msh = (int16_t)((int32_t)msh - (int32_t)(icon_sz * 16 / 100) * bell / ANIMATION_NORMALIZED_MAX);
  }

  if (msw < 2) msw = 2;
  if (msh < 2) msh = 2;
  layer_set_frame(bitmap_layer_get_layer(layout->tomorrow_weather_icon_layer),
                  GRect(mx - msw / 2, my - msh / 2, msw, msh));
  ((WeatherAppLayout *)layout)->animation_state.list_tmr_icon_pos = GPoint(mx, my);

  layer_mark_dirty(layout->root_layer);
}

static void prv_list_transit_update(Animation *anim, AnimationProgress progress) {
  WeatherAppLayout *layout = (WeatherAppLayout *)animation_get_context(anim);
  prv_apply_list_transition_progress(layout, progress);
}

static void prv_list_transit_stopped(Animation *anim, bool finished, void *context) {
  WeatherAppLayout *layout = (WeatherAppLayout *)context;
  layout->icon_animation = NULL;

  // Restore layer states for when we return from the list screen.
  layer_set_hidden(layout->outgoing_weather_icon_layer, true);
  layer_set_hidden(bitmap_layer_get_layer(layout->current_weather_icon_layer), false);
  layer_set_frame(bitmap_layer_get_layer(layout->current_weather_icon_layer),
                  layout->today_icon_rest_frame);
  layer_set_frame(bitmap_layer_get_layer(layout->tomorrow_weather_icon_layer),
                  layout->tomorrow_icon_rest_frame);
  layout->animation_state.transitioning_to_list = false;

  // Snap bar to list size.
  prv_set_bar_height(layout, WEATHER_APP_LAYOUT_LOCATION_BAR_HEIGHT);
  layout->bar_anim_progress = 0;

  animation_destroy(anim);

  if (finished) {
    void (*cb)(void *) = layout->animation_state.on_list_done;
    void *ctx = layout->animation_state.on_list_done_ctx;
    layout->animation_state.on_list_done = NULL;
    if (cb) cb(ctx);
  }
}

static const AnimationImplementation s_list_transit_impl = {
  .update = prv_list_transit_update,
};

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

  Animation *anim = animation_create();
  animation_set_duration(anim, total_ms);
  animation_set_curve(anim, AnimationCurveLinear);
  animation_set_implementation(anim, &s_fin_anim_impl);
  animation_set_handlers(anim, (AnimationHandlers){ .stopped = prv_fin_anim_stopped }, layout);
  layout->fin_animation = anim;
  animation_schedule(anim);
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

// Small (tomorrow) icon scaler — same per-pixel squash/stretch as the today
// scaler, used during the clock transition so the small icon squashes instead of
// just clipping. Scales the existing tomorrow bitmap to the animated frame.
static void prv_draw_tomorrow_outgoing_icon_scaled(Layer *layer, GContext *ctx) {
  WeatherAppLayout *layout = *(WeatherAppLayout **)layer_get_data(layer);
  GRect root_frame = layer_get_frame(layer);
  root_frame.origin.x += layout->content_layer_origin.x;
  root_frame.origin.y += layout->content_layer_origin.y;
  prv_draw_bitmap_scaled_to_root(ctx, layout->tomorrow_weather_icon, root_frame);
}

// ---- Refresh banner animation ----

static void prv_refresh_banner_update(Animation *anim, AnimationProgress progress) {
  WeatherAppLayout *layout = (WeatherAppLayout *)animation_get_context(anim);
  layout->refresh_banner_progress = progress;
  layer_mark_dirty(layout->location_bar_layer);
}

static void prv_refresh_banner_stopped(Animation *anim, bool finished, void *context) {
  WeatherAppLayout *layout = (WeatherAppLayout *)context;
  layout->refresh_banner_active = false;
  layout->refresh_banner_anim   = NULL;
  layer_mark_dirty(layout->location_bar_layer);
  animation_destroy(anim);
}

static const AnimationImplementation s_refresh_banner_impl = {
  .update = prv_refresh_banner_update,
};

// ---- Pull-to-refresh implementation ----

// Forward declarations for timer callbacks.
static void prv_pull_snap_timer_cb(void *ctx);
static void prv_pull_spin_timer_cb(void *ctx);

static void prv_pull_mark_dirty(WeatherAppLayout *layout) {
  layer_mark_dirty(layout->root_layer);
#if PBL_ROUND
  layer_mark_dirty(layout->location_bar_layer);
#endif
}

// Apply a clamped y-offset to the content layer and mark dirty.
static void prv_pull_apply_offset(WeatherAppLayout *layout, int offset) {
  if (offset < 0) offset = 0;
  if (offset > PULL_TRIGGER_PX) offset = PULL_TRIGGER_PX;
  layout->pull_offset = offset;
  GRect fr = layout->pull_content_rest_frame;
  fr.origin.y += offset;
  layer_set_frame(layout->content_layer, fr);
  prv_pull_mark_dirty(layout);
}

// Hard-cancel all pull state and restore the content layer to its rest frame.
static void prv_pull_cancel_all(WeatherAppLayout *layout) {
  if (layout->pull_spin_timer) {
    app_timer_cancel(layout->pull_spin_timer);
    layout->pull_spin_timer = NULL;
  }
  if (layout->pull_snap_timer) {
    app_timer_cancel(layout->pull_snap_timer);
    layout->pull_snap_timer = NULL;
  }
  layout->pull_active    = false;
  layout->pull_triggered = false;
  layout->pull_snapping  = false;
  layout->pull_offset    = 0;
  layer_set_frame(layout->content_layer, layout->pull_content_rest_frame);
  prv_pull_mark_dirty(layout);
}

static void prv_pull_spin_timer_cb(void *ctx) {
  WeatherAppLayout *layout = (WeatherAppLayout *)ctx;
  layout->pull_spin_timer = NULL;
  if (!layout->pull_triggered && !layout->pull_snapping) return;
  layout->pull_spin_angle = ((int32_t)layout->pull_spin_angle
      + (int32_t)(TRIG_MAX_ANGLE / 10)) % (int32_t)TRIG_MAX_ANGLE;
  prv_pull_mark_dirty(layout);
  // Keep spinning as long as the gap is still visible.
  if (layout->pull_offset >= PULL_HINT_PX) {
    layout->pull_spin_timer = app_timer_register(PULL_SPIN_MS, prv_pull_spin_timer_cb, layout);
  }
}

static void prv_pull_snap_timer_cb(void *ctx) {
  WeatherAppLayout *layout = (WeatherAppLayout *)ctx;
  layout->pull_snap_timer = NULL;
  layout->pull_snap_step++;

  if (layout->pull_snap_step >= PULL_SNAP_STEPS) {
    // Snap complete — clean everything up.
    if (layout->pull_spin_timer) {
      app_timer_cancel(layout->pull_spin_timer);
      layout->pull_spin_timer = NULL;
    }
    layout->pull_snapping  = false;
    layout->pull_triggered = false;
    layout->pull_active    = false;
    layout->pull_offset    = 0;
    layer_set_frame(layout->content_layer, layout->pull_content_rest_frame);
    prv_pull_mark_dirty(layout);
    return;
  }

  // Ease-out quadratic: offset = snap_from * ((STEPS - step) / STEPS)^2
  int32_t inv      = (int32_t)(PULL_SNAP_STEPS - layout->pull_snap_step);
  int new_offset   = layout->pull_snap_from * inv * inv /
                     (PULL_SNAP_STEPS * PULL_SNAP_STEPS);
  prv_pull_apply_offset(layout, new_offset);
  layout->pull_snap_timer = app_timer_register(PULL_SNAP_MS, prv_pull_snap_timer_cb, layout);
}

void weather_app_layout_pull_update(WeatherAppLayout *layout, int total_dy) {
  // Don't interfere with other running animations.
  if (layout->pull_snapping
      || layout->icon_animation
      || layout->animation_state.transitioning_to_list
      || layout->animation_state.returning_from_list) return;

  layout->pull_active = true;

  int offset = total_dy < 0 ? 0 : (total_dy > PULL_TRIGGER_PX ? PULL_TRIGGER_PX : total_dy);
  prv_pull_apply_offset(layout, offset);

  bool newly_triggered = (offset >= PULL_TRIGGER_PX && !layout->pull_triggered);
  layout->pull_triggered = (offset >= PULL_TRIGGER_PX);

  // Start the spinner timer the first time we pass the hint threshold.
  if (offset >= PULL_HINT_PX && !layout->pull_spin_timer) {
    layout->pull_spin_timer = app_timer_register(PULL_SPIN_MS, prv_pull_spin_timer_cb, layout);
  }

  // Haptic click at the moment the full-pull threshold is first crossed.
  if (newly_triggered) {
    vibes_short_pulse();
  }
}

bool weather_app_layout_pull_release(WeatherAppLayout *layout) {
  if (!layout->pull_active) return false;
  bool triggered = layout->pull_triggered;

  // Spin timer continues during snap-back for visual continuity;
  // prv_pull_snap_timer_cb will stop it when the gap closes.
  layout->pull_snap_from = layout->pull_offset;
  layout->pull_snap_step = 0;
  layout->pull_snapping  = true;
  layout->pull_active    = false;

  if (layout->pull_snap_timer) {
    app_timer_cancel(layout->pull_snap_timer);
  }
  layout->pull_snap_timer = app_timer_register(PULL_SNAP_MS, prv_pull_snap_timer_cb, layout);
  return triggered;
}

void weather_app_layout_pull_abort(WeatherAppLayout *layout) {
  if (!layout->pull_active) return;
  layout->pull_triggered = false;
  if (layout->pull_spin_timer) {
    app_timer_cancel(layout->pull_spin_timer);
    layout->pull_spin_timer = NULL;
  }
  // Start snap-back from wherever the drag reached.
  layout->pull_snap_from = layout->pull_offset;
  layout->pull_snap_step = 0;
  layout->pull_snapping  = true;
  layout->pull_active    = false;
  if (layout->pull_snap_timer) {
    app_timer_cancel(layout->pull_snap_timer);
  }
  layout->pull_snap_timer = app_timer_register(PULL_SNAP_MS, prv_pull_snap_timer_cb, layout);
}

// ---- Clock-face entry transition ----
// Both weather icons uniformly shrink toward the screen centre, merging into
// a single point that shakes before the clock face spiral-out plays.
//
// 2 phases (CLOCK_TRANSIT_MS = 240 ms):
//   SHRINK  0-78%  Simultaneous position->centre + size->0 with a tiny landing rebound
//   SHAKE   78-100% Orbital shake: 4 rotations, bell-curved amplitude 3->7->0 px

static void prv_apply_clock_transition_progress(WeatherAppLayout *layout,
                                                AnimationProgress progress) {
  GRect root_bounds = layer_get_bounds(layout->root_layer);
  int screen_cx = root_bounds.size.w / 2;
  int screen_cy = root_bounds.size.h / 2;

  int to_cx = screen_cx - layout->content_layer_origin.x;
  int to_cy = screen_cy - layout->content_layer_origin.y;

  const int32_t CS = ANIMATION_NORMALIZED_MAX * 78 / 100;  // end of shrink phase

  // ---- Today icon (outgoing_weather_icon_layer = per-pixel scaler) ----
  GRect from_t = layout->today_icon_rest_frame;
  int16_t fc_x = (int16_t)(from_t.origin.x + from_t.size.w / 2);
  int16_t fc_y = (int16_t)(from_t.origin.y + from_t.size.h / 2);
  int src_sz = (int)from_t.size.w;

  int16_t cx, cy, sw, sh;

  if (progress <= CS) {
    int32_t t = weather_scale_i32(progress, ANIMATION_NORMALIZED_MAX, CS);
    int32_t motion_t = prv_interpolate_icon_landing_progress(t);
    int32_t size_t = motion_t;
    if (size_t > ANIMATION_NORMALIZED_MAX) size_t = ANIMATION_NORMALIZED_MAX;
    if (layout->clock_transit.shake_phase) {
      layout->clock_transit.shake_phase = false;
      layer_set_hidden(layout->outgoing_weather_icon_layer, false);
    }
    cx = (int16_t)((int32_t)fc_x + (int32_t)(to_cx - fc_x) * motion_t / ANIMATION_NORMALIZED_MAX);
    cy = (int16_t)((int32_t)fc_y + (int32_t)(to_cy - fc_y) * motion_t / ANIMATION_NORMALIZED_MAX);
    int cur_sz = src_sz - (int)((int32_t)src_sz * size_t / ANIMATION_NORMALIZED_MAX);
    if (cur_sz < 2) cur_sz = 2;
    // Squash-stretch: bell-curve peaks at mid-flight (~t=0.5).
    // Width squashes, height stretches by the same amount.
    int32_t bell = weather_norm_bell(t);
    int squash = (int)((int32_t)cur_sz * 45 / 100 * bell / ANIMATION_NORMALIZED_MAX);
    sw = (int16_t)(cur_sz - squash < 2 ? 2 : cur_sz - squash);
    sh = (int16_t)(cur_sz + squash);
  } else {
    int32_t shake_t = weather_scale_i32(progress - CS,
                                        ANIMATION_NORMALIZED_MAX,
                                        ANIMATION_NORMALIZED_MAX - CS);
    int32_t shake_angle = (int32_t)TRIG_MAX_ANGLE * 4 * shake_t / ANIMATION_NORMALIZED_MAX;
    int32_t amp_norm = weather_norm_bell(shake_t);
    int amp = 3 + (int)(amp_norm * 4 / ANIMATION_NORMALIZED_MAX);
    cx = (int16_t)(to_cx + (int)((int32_t)sin_lookup(shake_angle) * amp / TRIG_MAX_RATIO));
    cy = (int16_t)(to_cy - (int)((int32_t)cos_lookup(shake_angle) * amp / TRIG_MAX_RATIO));
    sw = 16; sh = 16;  // black dot during shake
    if (!layout->clock_transit.shake_phase) {
      // First time entering shake: hide the bitmap scaler so only the black dot shows.
      layer_set_hidden(layout->outgoing_weather_icon_layer, true);
    }
    layout->clock_transit.shake_phase = true;
  }

  if (sw < 2) sw = 2;
  if (sh < 2) sh = 2;
  layer_set_frame(layout->outgoing_weather_icon_layer,
                  GRect(cx - sw / 2, cy - sh / 2, sw, sh));

  // ---- Tomorrow icon (BitmapLayer) — delayed 12% so it visibly chases today ----
  const int32_t TMR_DELAY = ANIMATION_NORMALIZED_MAX * 6 / 100;
  int32_t tp = (progress > TMR_DELAY)
      ? weather_scale_i32(progress - TMR_DELAY, ANIMATION_NORMALIZED_MAX,
                          ANIMATION_NORMALIZED_MAX - TMR_DELAY)
      : 0;

  GRect from_m = layout->tomorrow_icon_rest_frame;
  int16_t fm_x = (int16_t)(from_m.origin.x + from_m.size.w / 2);
  int16_t fm_y = (int16_t)(from_m.origin.y + from_m.size.h / 2);
  int tmr_sz = (int)from_m.size.w;

  int16_t mx, my, msw, msh;

  if (tp <= CS) {
    int32_t t = weather_scale_i32(tp, ANIMATION_NORMALIZED_MAX, CS);
    int32_t motion_t = prv_interpolate_icon_landing_progress(t);
    int32_t size_t = motion_t;
    if (size_t > ANIMATION_NORMALIZED_MAX) size_t = ANIMATION_NORMALIZED_MAX;
    mx = (int16_t)((int32_t)fm_x + (int32_t)(to_cx - fm_x) * motion_t / ANIMATION_NORMALIZED_MAX);
    my = (int16_t)((int32_t)fm_y + (int32_t)(to_cy - fm_y) * motion_t / ANIMATION_NORMALIZED_MAX);
    int cur_sz = tmr_sz - (int)((int32_t)tmr_sz * size_t / ANIMATION_NORMALIZED_MAX);
    if (cur_sz < 2) cur_sz = 2;
    int32_t bell = weather_norm_bell(t);
    int squash = (int)((int32_t)cur_sz * 45 / 100 * bell / ANIMATION_NORMALIZED_MAX);
    msw = (int16_t)(cur_sz - squash < 2 ? 2 : cur_sz - squash);
    msh = (int16_t)(cur_sz + squash);
  } else {
    mx = cx; my = cy;  // follow today during shake
    msw = 4; msh = 4;
  }

  if (msw < 2) msw = 2;
  if (msh < 2) msh = 2;
  layer_set_frame(layout->tomorrow_outgoing_icon_layer,
                  GRect(mx - msw / 2, my - msh / 2, msw, msh));

  layer_mark_dirty(layout->root_layer);
}

static void prv_clock_transit_update(Animation *anim, AnimationProgress progress) {
  WeatherAppLayout *layout = (WeatherAppLayout *)animation_get_context(anim);
  prv_apply_clock_transition_progress(layout, progress);
}

static void prv_clock_transit_stopped(Animation *anim, bool finished, void *context) {
  WeatherAppLayout *layout = (WeatherAppLayout *)context;
  layout->icon_animation = NULL;

  // Restore layers to their resting states.
  layer_set_hidden(layout->outgoing_weather_icon_layer, true);
  layer_set_hidden(layout->tomorrow_outgoing_icon_layer, true);
  layer_set_hidden(bitmap_layer_get_layer(layout->current_weather_icon_layer), false);
  layer_set_hidden(bitmap_layer_get_layer(layout->tomorrow_weather_icon_layer), false);
  layer_set_frame(bitmap_layer_get_layer(layout->current_weather_icon_layer),
                  layout->today_icon_rest_frame);
  layer_set_frame(bitmap_layer_get_layer(layout->tomorrow_weather_icon_layer),
                  layout->tomorrow_icon_rest_frame);
#if WEATHER_APP_LAYOUT_USE_PDC_WEATHER_ICONS
  prv_set_current_weather_pdc(layout, layout->forecast);
#endif
  layout->clock_transit.transitioning_to_clock = false;
  layout->clock_transit.shake_phase = false;
  layout->animation_state.hide_bottom_half_text = false;
  layer_mark_dirty(layout->root_layer);

  animation_destroy(anim);

  if (finished) {
    void (*cb)(void *) = layout->clock_transit.on_clock_done;
    void *ctx = layout->clock_transit.on_clock_done_ctx;
    layout->clock_transit.on_clock_done = NULL;
    if (cb) cb(ctx);
  }
}

static const AnimationImplementation s_clock_transit_impl = {
  .update = prv_clock_transit_update,
};

static bool prv_prepare_clock_transition(WeatherAppLayout *layout,
                                         void (*done_cb)(void *ctx),
                                         void *done_ctx) {
  if (layout->icon_animation) return false;
  if (layout->interactive.kind != WeatherInteractiveTransition_None) return false;
  if (layout->pull_active || layout->pull_snapping) {
    prv_pull_cancel_all(layout);
  }

  layout->clock_transit.transitioning_to_clock  = true;
  layout->clock_transit.on_clock_done           = done_cb;
  layout->clock_transit.on_clock_done_ctx       = done_ctx;
  layout->animation_state.hide_bottom_half_text = true;

  // Load today's icon into the per-pixel scaler layer.
  if (layout->outgoing_weather_icon) {
    gbitmap_destroy(layout->outgoing_weather_icon);
    layout->outgoing_weather_icon = NULL;
  }
  if (layout->forecast) {
    layout->outgoing_weather_icon = gbitmap_create_with_resource(
        weather_type_get_icon_res_today(layout->forecast->current_weather_type));
  }
  layout->anim_params.outgoing_weather_type =
      layout->forecast ? layout->forecast->current_weather_type : WeatherType_Unknown;

  layer_set_frame(layout->outgoing_weather_icon_layer, layout->today_icon_rest_frame);
  layer_set_hidden(layout->outgoing_weather_icon_layer, false);
  // Hide the BitmapLayer while the scaler handles today's icon.
  layer_set_hidden(bitmap_layer_get_layer(layout->current_weather_icon_layer), true);
  // Hide the tomorrow BitmapLayer; its scaler renders the small icon so it
  // squash/stretches like the large one. Show the scaler only if there's a next
  // day to animate.
  layer_set_hidden(bitmap_layer_get_layer(layout->tomorrow_weather_icon_layer), true);
  layer_set_hidden(layout->tomorrow_outgoing_icon_layer, layout->next_forecast == NULL);
  if (layout->fin_layer) {
    layer_set_hidden(layout->fin_layer, true);
  }

  prv_apply_clock_transition_progress(layout, 0);
  return true;
}

void weather_app_layout_start_clock_transition(WeatherAppLayout *layout,
                                               void (*done_cb)(void *ctx),
                                               void *done_ctx) {
  if (!prv_prepare_clock_transition(layout, done_cb, done_ctx)) return;

  Animation *anim = animation_create();
  animation_set_duration(anim, CLOCK_TRANSIT_MS);
  animation_set_curve(anim, AnimationCurveEaseOut);
  animation_set_implementation(anim, &s_clock_transit_impl);
  animation_set_handlers(anim,
      (AnimationHandlers){ .stopped = prv_clock_transit_stopped }, layout);
  layout->icon_animation = anim;
  animation_schedule(anim);
  layer_mark_dirty(layout->root_layer);
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

static void prv_restore_static_weather_state(WeatherAppLayout *layout,
                                             const WeatherLocationForecast *today,
                                             const WeatherLocationForecast *next) {
#if WEATHER_APP_LAYOUT_USE_PDC_WEATHER_ICONS
  prv_move_day_icons_to_content(layout);
  prv_hide_current_escape_icon(layout);
#endif
  if (layout->anim_params.tomorrow_reparented || layout->anim_params.tomorrow_incoming) {
    Layer *tmr = bitmap_layer_get_layer(layout->tomorrow_weather_icon_layer);
    layer_remove_from_parent(tmr);
    layer_add_child(layout->content_layer, tmr);
    layout->anim_params.tomorrow_reparented = false;
    layout->anim_params.tomorrow_incoming = false;
  }

  if (layout->outgoing_weather_icon) {
    gbitmap_destroy(layout->outgoing_weather_icon);
    layout->outgoing_weather_icon = NULL;
  }
  if (layout->current_weather_icon) {
    gbitmap_destroy(layout->current_weather_icon);
    layout->current_weather_icon = NULL;
  }
  if (layout->tomorrow_weather_icon) {
    gbitmap_destroy(layout->tomorrow_weather_icon);
    layout->tomorrow_weather_icon = NULL;
  }

  layout->forecast = today;
  layout->next_forecast = next;

#if WEATHER_APP_LAYOUT_USE_PDC_WEATHER_ICONS
  bitmap_layer_set_bitmap(layout->current_weather_icon_layer, NULL);
#else
  if (today) {
    layout->current_weather_icon = gbitmap_create_with_resource(
        weather_type_get_icon_res_today(today->current_weather_type));
    bitmap_layer_set_bitmap(layout->current_weather_icon_layer,
                            layout->current_weather_icon);
  } else {
    bitmap_layer_set_bitmap(layout->current_weather_icon_layer, NULL);
  }
#endif
  if (next) {
    layout->tomorrow_weather_icon = gbitmap_create_with_resource(
        weather_type_get_icon_res_tiny(next->current_weather_type));
    bitmap_layer_set_bitmap(layout->tomorrow_weather_icon_layer,
                            layout->tomorrow_weather_icon);
  } else {
    bitmap_layer_set_bitmap(layout->tomorrow_weather_icon_layer, NULL);
  }

  layer_set_hidden(layout->outgoing_weather_icon_layer, true);
  layer_set_hidden(layout->tomorrow_outgoing_icon_layer, true);
  layer_set_hidden(bitmap_layer_get_layer(layout->current_weather_icon_layer), false);
  layer_set_hidden(bitmap_layer_get_layer(layout->tomorrow_weather_icon_layer), false);
  layer_set_frame(layout->outgoing_weather_icon_layer, layout->today_icon_rest_frame);
  layer_set_frame(bitmap_layer_get_layer(layout->current_weather_icon_layer),
                  layout->today_icon_rest_frame);
  layer_set_frame(bitmap_layer_get_layer(layout->tomorrow_weather_icon_layer),
                  layout->tomorrow_icon_rest_frame);

#if WEATHER_APP_LAYOUT_USE_PDC_WEATHER_ICONS
  prv_set_current_weather_pdc(layout, today);
#endif

  layout->text_anim.active = false;
  layout->animation_state.hide_bottom_half_text = false;
  layout->animation_state.transitioning_to_list = false;
  layout->animation_state.returning_from_list = false;
  layout->animation_state.list_transit_progress = 0;
  layout->clock_transit.transitioning_to_clock = false;
  layout->clock_transit.shake_phase = false;
  prv_set_bar_height(layout, WEATHER_APP_LAYOUT_MAIN_BAR_HEIGHT);
  layout->bar_anim_progress = 0;
  prv_restore_fin_rest(layout);
  layer_mark_dirty(layout->root_layer);
}

static void prv_complete_day_transition(WeatherAppLayout *layout) {
  layout->text_anim.active = false;
#if WEATHER_APP_LAYOUT_USE_PDC_WEATHER_ICONS
  prv_move_day_icons_to_content(layout);
  prv_hide_current_escape_icon(layout);
#endif

  if (layout->anim_params.animate_down) {
#if !WEATHER_APP_LAYOUT_USE_PDC_WEATHER_ICONS
    if (layout->current_weather_icon) gbitmap_destroy(layout->current_weather_icon);
    layout->current_weather_icon = layout->forecast ?
        gbitmap_create_with_resource(
            weather_type_get_icon_res_today(layout->forecast->current_weather_type)) : NULL;
    bitmap_layer_set_bitmap(layout->current_weather_icon_layer,
                            layout->current_weather_icon);
#endif
    if (layout->outgoing_weather_icon) {
      gbitmap_destroy(layout->outgoing_weather_icon);
      layout->outgoing_weather_icon = NULL;
    }
  }
  layer_set_frame(bitmap_layer_get_layer(layout->current_weather_icon_layer),
                  layout->today_icon_rest_frame);
  layer_set_hidden(layout->outgoing_weather_icon_layer, true);

  if (layout->anim_params.tomorrow_reparented || layout->anim_params.tomorrow_incoming) {
    bool was_reparented = layout->anim_params.tomorrow_reparented;
    Layer *tmr = bitmap_layer_get_layer(layout->tomorrow_weather_icon_layer);
    layer_remove_from_parent(tmr);
    layer_add_child(layout->content_layer, tmr);
    layer_set_frame(tmr, layout->tomorrow_icon_rest_frame);
    layout->anim_params.tomorrow_reparented = false;
    layout->anim_params.tomorrow_incoming = false;
    if (was_reparented) {
      if (layout->tomorrow_weather_icon) {
        gbitmap_destroy(layout->tomorrow_weather_icon);
        layout->tomorrow_weather_icon = NULL;
      }
      if (layout->next_forecast) {
        layout->tomorrow_weather_icon = gbitmap_create_with_resource(
            weather_type_get_icon_res_tiny(layout->next_forecast->current_weather_type));
        bitmap_layer_set_bitmap(layout->tomorrow_weather_icon_layer,
                                layout->tomorrow_weather_icon);
      } else {
        bitmap_layer_set_bitmap(layout->tomorrow_weather_icon_layer, NULL);
      }
    }
  }
  layer_set_hidden(bitmap_layer_get_layer(layout->current_weather_icon_layer), false);
  layer_set_hidden(bitmap_layer_get_layer(layout->tomorrow_weather_icon_layer), false);
  layer_set_frame(bitmap_layer_get_layer(layout->tomorrow_weather_icon_layer),
                  layout->tomorrow_icon_rest_frame);
#if WEATHER_APP_LAYOUT_USE_PDC_WEATHER_ICONS
  prv_set_current_weather_pdc(layout, layout->forecast);
#endif
  layer_mark_dirty(layout->root_layer);
}

static void prv_complete_clock_transition(WeatherAppLayout *layout, bool commit) {
  (void)commit;
  layer_set_hidden(layout->outgoing_weather_icon_layer, true);
  layer_set_hidden(layout->tomorrow_outgoing_icon_layer, true);
  layer_set_hidden(bitmap_layer_get_layer(layout->current_weather_icon_layer), false);
  layer_set_hidden(bitmap_layer_get_layer(layout->tomorrow_weather_icon_layer), false);
  layer_set_frame(bitmap_layer_get_layer(layout->current_weather_icon_layer),
                  layout->today_icon_rest_frame);
  layer_set_frame(bitmap_layer_get_layer(layout->tomorrow_weather_icon_layer),
                  layout->tomorrow_icon_rest_frame);
#if WEATHER_APP_LAYOUT_USE_PDC_WEATHER_ICONS
  prv_set_current_weather_pdc(layout, layout->forecast);
#endif
  layout->clock_transit.transitioning_to_clock = false;
  layout->clock_transit.shake_phase = false;
  layout->animation_state.hide_bottom_half_text = false;
  if (!commit) {
    prv_restore_fin_rest(layout);
  }
  layer_mark_dirty(layout->root_layer);
}

static void prv_clear_interactive_state(WeatherAppLayout *layout) {
  layout->interactive.kind = WeatherInteractiveTransition_None;
  layout->interactive.progress = 0;
  layout->interactive.from_progress = 0;
  layout->interactive.to_progress = 0;
  layout->interactive.commit = false;
  layout->interactive.old_today = NULL;
  layout->interactive.old_next = NULL;
  layout->interactive.new_today = NULL;
  layout->interactive.new_next = NULL;
  layout->interactive.day_animate_down = false;
  layout->interactive.use_floaty_day = false;
  layout->interactive.done_cb = NULL;
  layout->interactive.done_ctx = NULL;
}

static void prv_finish_interactive_now(WeatherAppLayout *layout) {
  WeatherInteractiveTransition kind = layout->interactive.kind;
  bool commit = layout->interactive.commit;
  void (*done_cb)(void *) = commit ? layout->interactive.done_cb : NULL;
  void *done_ctx = layout->interactive.done_ctx;

  if (kind == WeatherInteractiveTransition_Day) {
    if (commit) {
      weather_app_layout_update_interactive(layout, ANIMATION_NORMALIZED_MAX);
      prv_complete_day_transition(layout);
      if (!layout->next_forecast) {
        prv_restore_fin_rest(layout);
      }
    } else {
      weather_app_layout_update_interactive(layout, 0);
      prv_restore_static_weather_state(layout, layout->interactive.old_today,
                                       layout->interactive.old_next);
    }
  } else if (kind == WeatherInteractiveTransition_Clock) {
    if (commit) {
      weather_app_layout_update_interactive(layout, ANIMATION_NORMALIZED_MAX);
    } else {
      weather_app_layout_update_interactive(layout, 0);
    }
    prv_complete_clock_transition(layout, commit);
  }

  prv_clear_interactive_state(layout);
  if (done_cb) done_cb(done_ctx);
}

static void prv_interactive_anim_update(Animation *anim, AnimationProgress progress) {
  WeatherAppLayout *layout = (WeatherAppLayout *)animation_get_context(anim);
  int32_t from = (int32_t)layout->interactive.from_progress;
  int32_t to = (int32_t)layout->interactive.to_progress;
  int32_t p = from + weather_scale_i32(to - from, progress,
                                       ANIMATION_NORMALIZED_MAX);
  if (p < 0) p = 0;
  if (p > ANIMATION_NORMALIZED_MAX) p = ANIMATION_NORMALIZED_MAX;
  weather_app_layout_update_interactive(layout, (AnimationProgress)p);
}

static void prv_interactive_anim_stopped(Animation *anim, bool finished, void *context) {
  WeatherAppLayout *layout = (WeatherAppLayout *)context;
  layout->icon_animation = NULL;
  animation_destroy(anim);

  if (finished) {
    prv_finish_interactive_now(layout);
  } else {
    prv_restore_static_weather_state(layout, layout->interactive.old_today,
                                     layout->interactive.old_next);
    prv_clear_interactive_state(layout);
  }
}

static const AnimationImplementation s_interactive_anim_impl = {
  .update = prv_interactive_anim_update,
};

bool weather_app_layout_begin_interactive_day(WeatherAppLayout *layout,
                                              const WeatherLocationForecast *new_today,
                                              const WeatherLocationForecast *new_next,
                                              bool animate_down) {
  if (!layout || layout->interactive.kind != WeatherInteractiveTransition_None) return false;
  const WeatherLocationForecast *old_today = layout->forecast;
  const WeatherLocationForecast *old_next = layout->next_forecast;
  if (!prv_prepare_day_transition(layout, new_today, new_next, animate_down)) return false;

  layout->interactive.kind = WeatherInteractiveTransition_Day;
  layout->interactive.old_today = old_today;
  layout->interactive.old_next = old_next;
  layout->interactive.new_today = new_today;
  layout->interactive.new_next = new_next;
  layout->interactive.day_animate_down = animate_down;
  layout->interactive.use_floaty_day = false;
  layout->interactive.progress = 0;
  return true;
}

bool weather_app_layout_scrub_interactive_day(WeatherAppLayout *layout,
                                              const WeatherLocationForecast *old_today,
                                              const WeatherLocationForecast *old_next,
                                              const WeatherLocationForecast *new_today,
                                              const WeatherLocationForecast *new_next,
                                              bool animate_down,
                                              AnimationProgress progress) {
  if (!layout) return false;
  if (progress > ANIMATION_NORMALIZED_MAX) progress = ANIMATION_NORMALIZED_MAX;

  if (!new_today || progress == 0) {
    if (layout->interactive.kind != WeatherInteractiveTransition_None) {
      prv_restore_static_weather_state(layout, old_today, old_next);
      prv_clear_interactive_state(layout);
    } else if (layout->forecast != old_today || layout->next_forecast != old_next) {
      prv_restore_static_weather_state(layout, old_today, old_next);
    }
    return true;
  }

  bool same_transition =
      layout->interactive.kind == WeatherInteractiveTransition_Day
      && layout->interactive.old_today == old_today
      && layout->interactive.old_next == old_next
      && layout->interactive.new_today == new_today
      && layout->interactive.new_next == new_next
      && layout->interactive.day_animate_down == animate_down;

  if (!same_transition) {
    if (layout->interactive.kind != WeatherInteractiveTransition_None) {
      prv_restore_static_weather_state(layout, old_today, old_next);
      prv_clear_interactive_state(layout);
    } else if (layout->forecast != old_today || layout->next_forecast != old_next) {
      prv_restore_static_weather_state(layout, old_today, old_next);
    }
    if (!prv_prepare_day_transition(layout, new_today, new_next, animate_down)) {
      return false;
    }
    layout->interactive.kind = WeatherInteractiveTransition_Day;
    layout->interactive.old_today = old_today;
    layout->interactive.old_next = old_next;
    layout->interactive.new_today = new_today;
    layout->interactive.new_next = new_next;
    layout->interactive.day_animate_down = animate_down;
    layout->interactive.use_floaty_day = true;
  }

  weather_app_layout_update_interactive(layout, progress);
  return true;
}

bool weather_app_layout_begin_interactive_clock(WeatherAppLayout *layout) {
  if (!layout || layout->interactive.kind != WeatherInteractiveTransition_None) return false;
  const WeatherLocationForecast *old_today = layout->forecast;
  const WeatherLocationForecast *old_next = layout->next_forecast;
  if (!prv_prepare_clock_transition(layout, NULL, NULL)) return false;
  layout->interactive.kind = WeatherInteractiveTransition_Clock;
  layout->interactive.old_today = old_today;
  layout->interactive.old_next = old_next;
  layout->interactive.progress = 0;
  return true;
}

void weather_app_layout_update_interactive(WeatherAppLayout *layout,
                                           AnimationProgress progress) {
  if (!layout || layout->interactive.kind == WeatherInteractiveTransition_None) return;
  if (progress > ANIMATION_NORMALIZED_MAX) progress = ANIMATION_NORMALIZED_MAX;
  layout->interactive.progress = progress;
  switch (layout->interactive.kind) {
    case WeatherInteractiveTransition_Day:
      if (layout->interactive.use_floaty_day) {
        prv_apply_day_transition_progress_floaty(layout, progress);
      } else {
        prv_apply_day_transition_progress(layout, progress);
      }
      break;
    case WeatherInteractiveTransition_Clock:
      prv_apply_clock_transition_progress(layout, progress);
      break;
    case WeatherInteractiveTransition_None:
    default:
      break;
  }
}

void weather_app_layout_finish_interactive(WeatherAppLayout *layout,
                                           bool commit,
                                           void (*done_cb)(void *ctx),
                                           void *done_ctx) {
  if (!layout || layout->interactive.kind == WeatherInteractiveTransition_None) return;
  if (layout->icon_animation) return;

  AnimationProgress target = commit ? ANIMATION_NORMALIZED_MAX : 0;
  AnimationProgress from = layout->interactive.progress;
  layout->interactive.commit = commit;
  layout->interactive.done_cb = done_cb;
  layout->interactive.done_ctx = done_ctx;
  layout->interactive.from_progress = from;
  layout->interactive.to_progress = target;

  int32_t distance = (int32_t)target - (int32_t)from;
  if (distance < 0) distance = -distance;
  if (distance <= 0) {
    prv_finish_interactive_now(layout);
    return;
  }

  bool floaty_day = layout->interactive.kind == WeatherInteractiveTransition_Day
      && layout->interactive.use_floaty_day;
  uint32_t duration = commit
      ? weather_scale_u32(floaty_day ? 400 : 180, distance,
                          ANIMATION_NORMALIZED_MAX)
      : weather_scale_u32(floaty_day ? 220 : 130, distance,
                          ANIMATION_NORMALIZED_MAX);
  if (commit) {
    uint32_t min_duration = floaty_day ? 120 : 70;
    uint32_t max_duration = floaty_day ? 360 : 180;
    if (duration < min_duration) duration = min_duration;
    if (duration > max_duration) duration = max_duration;
  } else {
    uint32_t min_duration = floaty_day ? 100 : 80;
    uint32_t max_duration = floaty_day ? 220 : 130;
    if (duration < min_duration) duration = min_duration;
    if (duration > max_duration) duration = max_duration;
  }

  if (commit && layout->interactive.kind == WeatherInteractiveTransition_Day
      && !layout->interactive.new_next && layout->fin_allowed) {
    prv_animate_fin_in(layout, duration);
  }

  Animation *anim = animation_create();
  animation_set_duration(anim, duration);
  animation_set_curve(anim, floaty_day ? AnimationCurveEaseInOut : AnimationCurveEaseOut);
  animation_set_implementation(anim, &s_interactive_anim_impl);
  animation_set_handlers(anim,
      (AnimationHandlers){ .stopped = prv_interactive_anim_stopped }, layout);
  layout->icon_animation = anim;
  animation_schedule(anim);
}

void weather_app_layout_abort_interactive(WeatherAppLayout *layout,
                                          const WeatherLocationForecast *today,
                                          const WeatherLocationForecast *next) {
  if (!layout) return;
  if (layout->icon_animation) {
    animation_unschedule(layout->icon_animation);
    layout->icon_animation = NULL;
  }
  prv_restore_static_weather_state(layout, today, next);
  prv_clear_interactive_state(layout);
}

void weather_app_layout_show_refresh_banner(WeatherAppLayout *layout, time_t refresh_time) {
  if (layout->refresh_banner_anim) {
    animation_unschedule(layout->refresh_banner_anim);
    layout->refresh_banner_anim = NULL;
  }

#if PBL_ROUND
  strncpy(layout->refresh_banner_text, "refresh: just now",
          sizeof(layout->refresh_banner_text));
#endif
  (void)refresh_time;

  layout->refresh_banner_progress  = 0;
  layout->refresh_banner_active    = true;

  Animation *anim = animation_create();
  animation_set_duration(anim, 2400);
  animation_set_curve(anim, AnimationCurveEaseOut);
  animation_set_implementation(anim, &s_refresh_banner_impl);
  animation_set_handlers(anim,
      (AnimationHandlers){ .stopped = prv_refresh_banner_stopped }, layout);
  layout->refresh_banner_anim = anim;
  animation_schedule(anim);
}

void weather_app_layout_init(WeatherAppLayout *layout, const GRect *frame) {
#if PBL_DISPLAY_HEIGHT >= 200
#if PBL_ROUND
  layout->location_font        = fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);
#else
  layout->location_font        = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
#endif
  layout->temperature_font     = fonts_get_system_font(FONT_KEY_LECO_36_BOLD_NUMBERS);
  layout->high_low_phrase_font = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
  layout->metrics_font         = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
  layout->tomorrow_font        = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
#else
  layout->location_font        = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
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
  layer_set_update_proc(layout->root_layer, prv_render_root_layer);

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
  layer_add_child(layout->root_layer, layout->content_layer);

  // Store the screen-absolute origin of the content layer for the per-pixel scaler.
  // root_layer is at *frame.origin (0,0 for fullscreen), content_layer is inset from that.
  layout->content_layer_origin = GPoint(
      frame->origin.x + content_layer_frame.origin.x,
      frame->origin.y + content_layer_frame.origin.y);
  // Save the rest frame so pull-to-refresh can shift and restore the layer.
  layout->pull_content_rest_frame = layer_get_frame(layout->content_layer);

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

  // Per-pixel scaler for the tomorrow icon (used during the clock transition so
  // the small icon squash/stretches like the large one instead of clipping).
  layout->tomorrow_outgoing_icon_layer =
      layer_create_with_data(tomorrow_icon_frame, sizeof(WeatherAppLayout *));
  *(WeatherAppLayout **)layer_get_data(layout->tomorrow_outgoing_icon_layer) = layout;
  layer_set_update_proc(layout->tomorrow_outgoing_icon_layer,
                        prv_draw_tomorrow_outgoing_icon_scaled);
  layer_set_hidden(layout->tomorrow_outgoing_icon_layer, true);
  layer_add_child(layout->content_layer, layout->tomorrow_outgoing_icon_layer);

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

  // Start the glow ring animation timer and the inactivity settle timer.
  weather_app_layout_note_interaction(layout);
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
  if (layout->glow_timer) {
    app_timer_cancel(layout->glow_timer);
    layout->glow_timer = NULL;
  }
  if (layout->pull_snap_timer) {
    app_timer_cancel(layout->pull_snap_timer);
    layout->pull_snap_timer = NULL;
  }
  if (layout->pull_spin_timer) {
    app_timer_cancel(layout->pull_spin_timer);
    layout->pull_spin_timer = NULL;
  }
  if (layout->icon_animation) {
    Animation *anim = layout->icon_animation;
    layout->icon_animation = NULL;
    animation_unschedule(anim);
  }
  if (layout->refresh_banner_anim) {
    animation_unschedule(layout->refresh_banner_anim);
    layout->refresh_banner_anim = NULL;
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
  layer_destroy(layout->tomorrow_outgoing_icon_layer);
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
  if (layout->interactive.kind != WeatherInteractiveTransition_None) return false;
  // Cancel any in-progress pull gesture to avoid frame conflicts.
  if (layout->pull_active || layout->pull_snapping) {
    prv_pull_cancel_all(layout);
  }
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
      if (layout->current_weather_icon) gbitmap_destroy(layout->current_weather_icon);
      layout->current_weather_icon = layout->forecast ?
          gbitmap_create_with_resource(
              weather_type_get_icon_res_today(layout->forecast->current_weather_type)) : NULL;
      bitmap_layer_set_bitmap(layout->current_weather_icon_layer, layout->current_weather_icon);
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
    if (new_today) {
      layout->current_weather_icon = gbitmap_create_with_resource(
          weather_type_get_icon_res_today(new_today->current_weather_type));
      bitmap_layer_set_bitmap(layout->current_weather_icon_layer, layout->current_weather_icon);
    } else {
      bitmap_layer_set_bitmap(layout->current_weather_icon_layer, NULL);
    }
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

  // Create and schedule animation
  Animation *anim = animation_create();
  animation_set_duration(anim, ICON_ANIM_DURATION_MS);
  animation_set_curve(anim, AnimationCurveEaseOut);
  animation_set_implementation(anim, &s_icon_anim_impl);
  animation_set_handlers(anim, (AnimationHandlers){ .stopped = prv_icon_anim_stopped }, layout);
  layout->icon_animation = anim;
  animation_schedule(anim);

  // If this scroll lands on the last day (no next forecast), kick off the FIN
  // ribbon slide at the same time so it arrives exactly as the icon settles.
  if (!new_next && layout->fin_allowed) {
    prv_animate_fin_in(layout, ICON_ANIM_DURATION_MS);
  }

  layer_mark_dirty(layout->root_layer);
}

static bool prv_prepare_list_transition(WeatherAppLayout *layout,
                                        int current_day_index,
                                        int num_days,
                                        void (*done_cb)(void *ctx),
                                        void *done_ctx) {
  if (layout->icon_animation) return false;
  if (layout->interactive.kind != WeatherInteractiveTransition_None) return false;

  layout->animation_state.transitioning_to_list = true;
  layout->animation_state.on_list_done          = done_cb;
  layout->animation_state.on_list_done_ctx      = done_ctx;
  layout->animation_state.list_start_day        = current_day_index;
  layout->animation_state.list_num_days         = num_days;

  // Hide the FIN ribbon immediately so it doesn't linger during the transition.
  if (layout->fin_layer) {
    layer_set_hidden(layout->fin_layer, true);
  }

  // Load today bitmap into the per-pixel scaler layer.
  if (layout->outgoing_weather_icon) {
    gbitmap_destroy(layout->outgoing_weather_icon);
    layout->outgoing_weather_icon = NULL;
  }
  if (layout->forecast) {
    layout->outgoing_weather_icon = gbitmap_create_with_resource(
        weather_type_get_icon_res_today(layout->forecast->current_weather_type));
  }
  layout->anim_params.outgoing_weather_type =
      layout->forecast ? layout->forecast->current_weather_type : WeatherType_Unknown;

  layer_set_frame(layout->outgoing_weather_icon_layer, layout->today_icon_rest_frame);
  layer_set_hidden(layout->outgoing_weather_icon_layer, false);
  // Hide the BitmapLayer — scaler handles today icon during this transition.
  layer_set_hidden(bitmap_layer_get_layer(layout->current_weather_icon_layer), true);

  prv_apply_list_transition_progress(layout, 0);
  return true;
}

void weather_app_layout_start_list_transition(WeatherAppLayout *layout,
                                              int current_day_index,
                                              int num_days,
                                              void (*done_cb)(void *ctx),
                                              void *done_ctx) {
  if (!prv_prepare_list_transition(layout, current_day_index, num_days,
                                   done_cb, done_ctx)) return;

  Animation *anim = animation_create();
  animation_set_duration(anim, 220);
  animation_set_curve(anim, AnimationCurveEaseOut);
  animation_set_implementation(anim, &s_list_transit_impl);
  animation_set_handlers(anim,
      (AnimationHandlers){ .stopped = prv_list_transit_stopped }, layout);
  layout->icon_animation = anim;
  animation_schedule(anim);
  layer_mark_dirty(layout->root_layer);
}

// ---- Return transition: list screen → main screen ----
// Mirror of the forward transition: icons collapse to flat bars at their list
// positions, fly right back to their main-screen rest frames, then spring open.

static void prv_apply_return_transition_progress(WeatherAppLayout *layout,
                                                 AnimationProgress progress) {
  layout->animation_state.list_transit_progress = progress;

  // Grow bar from LOCATION_BAR_HEIGHT → MAIN_BAR_HEIGHT as we return to main screen.
  int bar_h = WEATHER_APP_LAYOUT_LOCATION_BAR_HEIGHT
      + (int)((int32_t)(WEATHER_APP_LAYOUT_MAIN_BAR_HEIGHT - WEATHER_APP_LAYOUT_LOCATION_BAR_HEIGHT)
              * (int32_t)progress / (int32_t)ANIMATION_NORMALIZED_MAX);
  prv_set_bar_height(layout, bar_h);
  layout->bar_anim_progress = progress;
  layout->bar_anim_shrinking = false;

  int icon_sz  = (int)layout->tomorrow_icon_rest_frame.size.w;  // 25 px

#if defined(PBL_PLATFORM_GABBRO)
  int from_x0_c, from_x1_c, from_y0_c, from_y1_c;
  prv_list_gabbro_icon_center(layout, 0, icon_sz, &from_x0_c, &from_y0_c);
  prv_list_gabbro_icon_center(layout, 1, icon_sz, &from_x1_c, &from_y1_c);
#else
  int screen_h = layer_get_bounds(layout->root_layer).size.h;
  int rh       = (screen_h - WEATHER_APP_LAYOUT_LOCATION_BAR_HEIGHT) / LIST_ROWS_VISIBLE;

  // Compute which visual row the focused day lands on after scroll clamping.
  int _max_s_rows = layout->animation_state.list_num_days - LIST_ROWS_VISIBLE;
  if (_max_s_rows < 0) _max_s_rows = 0;
  int _eff = layout->animation_state.list_start_day < _max_s_rows
             ? layout->animation_state.list_start_day : _max_s_rows;
  int vis_row = layout->animation_state.list_start_day - _eff;

  // Source (list) centres in content-layer coords
  int from_x0_c   = LIST_ICON_X + icon_sz / 2 - layout->content_layer_origin.x;
  int from_x1_c   = from_x0_c;
  int row0_y_scr = WEATHER_APP_LAYOUT_LOCATION_BAR_HEIGHT + vis_row * rh + (rh - icon_sz) / 2;
  int row1_y_scr = row0_y_scr + rh;
  int from_y0_c  = row0_y_scr + icon_sz / 2 - layout->content_layer_origin.y;
  int from_y1_c  = row1_y_scr + icon_sz / 2 - layout->content_layer_origin.y;
#endif

  const int32_t P1 = ANIMATION_NORMALIZED_MAX * 28 / 100;
  const int32_t P2 = ANIMATION_NORMALIZED_MAX * 80 / 100;

  // ---- Today icon: list pos (25 px) → today_icon_rest_frame (50 px) ----
  GRect to_t   = layout->today_icon_rest_frame;
  int to_x_c   = to_t.origin.x + to_t.size.w / 2;
  int to_y_c   = to_t.origin.y + to_t.size.h / 2;
  int dst_sz   = (int)to_t.size.w;  // 50 px on emery

  const int16_t BAR_W = (int16_t)(icon_sz * 130 / 100);
  const int16_t BAR_H = 4;
  int16_t cx, cy, sw, sh;

  if (progress <= P1) {
    // Collapse at list position: icon_sz → flat bar (EaseIn t²)
    int32_t t  = weather_scale_i32(progress, ANIMATION_NORMALIZED_MAX, P1);
    int32_t t2 = weather_norm_square(t);
    cx = (int16_t)from_x0_c;
    cy = (int16_t)from_y0_c;
    sw = (int16_t)(icon_sz + (int32_t)(BAR_W - icon_sz) * t2 / ANIMATION_NORMALIZED_MAX);
    sh = (int16_t)(icon_sz + (int32_t)(BAR_H - icon_sz) * t2 / ANIMATION_NORMALIZED_MAX);
  } else if (progress <= P2) {
    // Fly flat bar from list position to rest position
    int32_t t = weather_scale_i32(progress - P1, ANIMATION_NORMALIZED_MAX,
                                  P2 - P1);
    cx = (int16_t)((int32_t)from_x0_c + (int32_t)(to_x_c - from_x0_c) * t / ANIMATION_NORMALIZED_MAX);
    cy = (int16_t)((int32_t)from_y0_c + (int32_t)(to_y_c - from_y0_c) * t / ANIMATION_NORMALIZED_MAX);
    sw = BAR_W;
    sh = BAR_H;
  } else {
    // Expand at rest position: flat bar → dst_sz (EaseOut 1-(1-t)²) with landing squash
    int32_t t   = weather_scale_i32(progress - P2, ANIMATION_NORMALIZED_MAX,
                                    ANIMATION_NORMALIZED_MAX - P2);
    int32_t inv = ANIMATION_NORMALIZED_MAX - t;
    int32_t te  = ANIMATION_NORMALIZED_MAX - weather_norm_square(inv);
    cx = (int16_t)to_x_c;
    cy = (int16_t)to_y_c;
    sw = (int16_t)(BAR_W + (int32_t)(dst_sz - BAR_W) * te / ANIMATION_NORMALIZED_MAX);
    sh = (int16_t)(BAR_H + (int32_t)(dst_sz - BAR_H) * te / ANIMATION_NORMALIZED_MAX);
    int32_t bell = weather_norm_bell(t);
    sw = (int16_t)((int32_t)sw + (int32_t)(dst_sz * 28 / 100) * bell / ANIMATION_NORMALIZED_MAX);
    sh = (int16_t)((int32_t)sh - (int32_t)(dst_sz * 20 / 100) * bell / ANIMATION_NORMALIZED_MAX);
  }
  if (sw < 2) sw = 2;
  if (sh < 2) sh = 2;
  layer_set_frame(layout->outgoing_weather_icon_layer, GRect(cx - sw / 2, cy - sh / 2, sw, sh));
  layout->animation_state.list_today_icon_pos = GPoint(cx, cy);

  // ---- Tomorrow icon: list row 1 → tomorrow_icon_rest_frame, delayed 15% ----
  const int32_t TMR_DELAY = ANIMATION_NORMALIZED_MAX * 15 / 100;
  int32_t tp = (progress > TMR_DELAY)
      ? weather_scale_i32(progress - TMR_DELAY, ANIMATION_NORMALIZED_MAX,
                          ANIMATION_NORMALIZED_MAX - TMR_DELAY)
      : 0;

  GRect to_m   = layout->tomorrow_icon_rest_frame;
  int to_mx    = to_m.origin.x + to_m.size.w / 2;
  int to_my    = to_m.origin.y + to_m.size.h / 2;

  const int16_t TBAR_W = (int16_t)(icon_sz * 130 / 100);
  const int16_t TBAR_H = 3;
  int16_t mx, my, msw, msh;

  if (tp <= P1) {
    int32_t t  = weather_scale_i32(tp, ANIMATION_NORMALIZED_MAX, P1);
    int32_t t2 = weather_norm_square(t);
    mx = (int16_t)from_x1_c;  my = (int16_t)from_y1_c;
    msw = (int16_t)(icon_sz + (int32_t)(TBAR_W - icon_sz) * t2 / ANIMATION_NORMALIZED_MAX);
    msh = (int16_t)(icon_sz + (int32_t)(TBAR_H - icon_sz) * t2 / ANIMATION_NORMALIZED_MAX);
  } else if (tp <= P2) {
    int32_t t = weather_scale_i32(tp - P1, ANIMATION_NORMALIZED_MAX,
                                  P2 - P1);
    mx  = (int16_t)((int32_t)from_x1_c + (int32_t)(to_mx - from_x1_c) * t / ANIMATION_NORMALIZED_MAX);
    my  = (int16_t)((int32_t)from_y1_c + (int32_t)(to_my - from_y1_c) * t / ANIMATION_NORMALIZED_MAX);
    msw = TBAR_W;  msh = TBAR_H;
  } else {
    int32_t t   = weather_scale_i32(tp - P2, ANIMATION_NORMALIZED_MAX,
                                    ANIMATION_NORMALIZED_MAX - P2);
    int32_t inv = ANIMATION_NORMALIZED_MAX - t;
    int32_t te  = ANIMATION_NORMALIZED_MAX - weather_norm_square(inv);
    mx = (int16_t)to_mx;  my = (int16_t)to_my;
    msw = (int16_t)(TBAR_W + (int32_t)(icon_sz - TBAR_W) * te / ANIMATION_NORMALIZED_MAX);
    msh = (int16_t)(TBAR_H + (int32_t)(icon_sz - TBAR_H) * te / ANIMATION_NORMALIZED_MAX);
    int32_t bell = weather_norm_bell(t);
    msw = (int16_t)((int32_t)msw + (int32_t)(icon_sz * 22 / 100) * bell / ANIMATION_NORMALIZED_MAX);
    msh = (int16_t)((int32_t)msh - (int32_t)(icon_sz * 16 / 100) * bell / ANIMATION_NORMALIZED_MAX);
  }
  if (msw < 2) msw = 2;
  if (msh < 2) msh = 2;
  layer_set_frame(bitmap_layer_get_layer(layout->tomorrow_weather_icon_layer),
                  GRect(mx - msw / 2, my - msh / 2, msw, msh));
  layout->animation_state.list_tmr_icon_pos = GPoint(mx, my);

  layer_mark_dirty(layout->root_layer);
}

static void prv_complete_return_transition(WeatherAppLayout *layout) {
  // Restore everything to the at-rest state the main screen expects.
  layer_set_hidden(layout->outgoing_weather_icon_layer, true);
  layer_set_hidden(bitmap_layer_get_layer(layout->current_weather_icon_layer), false);
  layer_set_frame(bitmap_layer_get_layer(layout->current_weather_icon_layer),
                  layout->today_icon_rest_frame);
  layer_set_frame(bitmap_layer_get_layer(layout->tomorrow_weather_icon_layer),
                  layout->tomorrow_icon_rest_frame);
  layout->animation_state.returning_from_list = false;
#if WEATHER_APP_LAYOUT_USE_PDC_WEATHER_ICONS
  prv_set_current_weather_pdc(layout, layout->forecast);
#endif

  // Snap bar back to main-screen size.
  prv_set_bar_height(layout, WEATHER_APP_LAYOUT_MAIN_BAR_HEIGHT);
  layout->bar_anim_progress = 0;

  // If we returned to the last day (no next forecast), show the FIN ribbon instantly.
  bool has_next = (layout->animation_state.list_start_day + 1 < layout->animation_state.list_num_days);
  if (!has_next && layout->fin_allowed && layout->fin_layer && layout->fin_pdc) {
    Layer *fin = layout->fin_layer;
    GRect cl = layer_get_frame(layout->content_layer);
    GSize fin_size = gdraw_command_image_get_bounds_size(layout->fin_pdc);
    GRect rest = (GRect){
      GPoint(cl.origin.x + (cl.size.w - fin_size.w) / 2,
             cl.origin.y + cl.size.h - fin_size.h - 12),
      fin_size
    };
    layer_set_frame(fin, rest);
    layer_set_hidden(fin, false);
  }

  layer_mark_dirty(layout->root_layer);
}

static void prv_return_transit_update(Animation *anim, AnimationProgress progress) {
  WeatherAppLayout *layout = (WeatherAppLayout *)animation_get_context(anim);
  prv_apply_return_transition_progress(layout, progress);
}

static void prv_return_transit_stopped(Animation *anim, bool finished, void *context) {
  WeatherAppLayout *layout = (WeatherAppLayout *)context;
  layout->icon_animation = NULL;
  if (finished) {
    prv_complete_return_transition(layout);
  }
  animation_destroy(anim);
}

static const AnimationImplementation s_return_transit_impl = {
  .update = prv_return_transit_update,
};

static bool prv_prepare_return_transition(WeatherAppLayout *layout) {
  if (layout->icon_animation) return false;  // another animation already running
  if (layout->interactive.kind != WeatherInteractiveTransition_None) return false;

  // Compute list start positions in content-layer coords
  int icon_sz  = (int)layout->tomorrow_icon_rest_frame.size.w;  // 25 px

#if defined(PBL_PLATFORM_GABBRO)
  int from_x0_c, from_x1_c, from_y0_c, from_y1_c;
  prv_list_gabbro_icon_center(layout, 0, icon_sz, &from_x0_c, &from_y0_c);
  prv_list_gabbro_icon_center(layout, 1, icon_sz, &from_x1_c, &from_y1_c);
#else
  int screen_h = layer_get_bounds(layout->root_layer).size.h;
  int rh       = (screen_h - WEATHER_APP_LAYOUT_LOCATION_BAR_HEIGHT) / LIST_ROWS_VISIBLE;

  // Compute which visual row the focused day lands on after scroll clamping.
  int _max_s_rows2 = layout->animation_state.list_num_days - LIST_ROWS_VISIBLE;
  if (_max_s_rows2 < 0) _max_s_rows2 = 0;
  int _eff2 = layout->animation_state.list_start_day < _max_s_rows2
              ? layout->animation_state.list_start_day : _max_s_rows2;
  int vis_row2 = layout->animation_state.list_start_day - _eff2;

  int from_x0_c  = LIST_ICON_X + icon_sz / 2 - layout->content_layer_origin.x;
  int from_x1_c  = from_x0_c;
  int row0_y_scr = WEATHER_APP_LAYOUT_LOCATION_BAR_HEIGHT + vis_row2 * rh + (rh - icon_sz) / 2;
  int row1_y_scr = row0_y_scr + rh;
  int from_y0_c  = row0_y_scr + icon_sz / 2 - layout->content_layer_origin.y;
  int from_y1_c  = row1_y_scr + icon_sz / 2 - layout->content_layer_origin.y;
#endif

  // Load today bitmap into the per-pixel scaler (may already be loaded; reload to be safe).
  if (layout->outgoing_weather_icon) {
    gbitmap_destroy(layout->outgoing_weather_icon);
    layout->outgoing_weather_icon = NULL;
  }
  if (layout->forecast) {
    layout->outgoing_weather_icon = gbitmap_create_with_resource(
        weather_type_get_icon_res_today(layout->forecast->current_weather_type));
  }
  layout->anim_params.outgoing_weather_type =
      layout->forecast ? layout->forecast->current_weather_type : WeatherType_Unknown;

  // Position layers at their list-screen start locations.
  layer_set_frame(layout->outgoing_weather_icon_layer,
                  GRect(from_x0_c - icon_sz / 2, from_y0_c - icon_sz / 2, icon_sz, icon_sz));
  layer_set_hidden(layout->outgoing_weather_icon_layer, false);
  layer_set_hidden(bitmap_layer_get_layer(layout->current_weather_icon_layer), true);

  layer_set_frame(bitmap_layer_get_layer(layout->tomorrow_weather_icon_layer),
                  GRect(from_x1_c - icon_sz / 2, from_y1_c - icon_sz / 2, icon_sz, icon_sz));

  layout->animation_state.returning_from_list = true;
  layout->animation_state.list_transit_progress = 0;
  layout->animation_state.list_today_icon_pos = GPoint(from_x0_c, from_y0_c);
  layout->animation_state.list_tmr_icon_pos   = GPoint(from_x1_c, from_y1_c);
  layer_mark_dirty(layout->root_layer);
  return true;
}

void weather_app_layout_start_return_transition(WeatherAppLayout *layout) {
  if (!prv_prepare_return_transition(layout)) return;

  Animation *anim = animation_create();
  animation_set_duration(anim, 220);
  animation_set_curve(anim, AnimationCurveEaseOut);
  animation_set_implementation(anim, &s_return_transit_impl);
  animation_set_handlers(anim,
      (AnimationHandlers){ .stopped = prv_return_transit_stopped }, layout);
  layout->icon_animation = anim;
  animation_schedule(anim);
}
