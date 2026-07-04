/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "weather_math.h"
#include "applib/ui/animation_interpolate.h"
#include <string.h>
#include <time.h>

int64_t weather_interpolate_moook_soft1(int32_t n, int64_t from, int64_t to) {
  return interpolate_moook_soft(n, from, to, 1);
}

const uint8_t weather_diurnal_curve[24] = {
   10,  6,  3,  0,  0,  3,  8, 16, 28, 42, 56, 70,
   82, 92, 98, 100, 96, 88, 76, 62, 48, 36, 26, 17,
};

#if !PBL_ROUND
// One jelly edge — leading edge delay 0, trailing edge delay 1/6 over a 5/6 point-duration, on
// the exact interpolate_moook_soft curve Timeline feeds its scale_segmented transform.
static int prv_jelly_edge(AnimationProgress m, int delay_num, int from, int to) {
  const int32_t MAX = ANIMATION_NORMALIZED_MAX;
  int32_t local = (int32_t)m - MAX * delay_num / 6;
  if (local < 0) local = 0;
  local = (int32_t)((int64_t)local * 6 / 5);
  if (local > MAX) local = MAX;
  return (int)interpolate_moook_soft(local, from, to, 3);
}

// Capture the fully-drawn screen from `ctx` and re-blit it vertically squash-stretched.
// Reads the one-shot `scratch` snapshot so re-sampling never hits already-overwritten
// framebuffer rows. Rect (emery/obelix) only — assumes contiguous full-width rows.
void weather_render_squash(GContext *ctx, uint8_t *scratch, AnimationProgress m, int mode) {
  GBitmap *fb = graphics_capture_frame_buffer(ctx);
  if (!fb) return;
  GRect b = gbitmap_get_bounds(fb);
  const int W = b.size.w, H = b.size.h;
  for (int y = 0; y < H; y++) {
    GBitmapDataRowInfo ri = gbitmap_get_data_row_info(fb, (uint16_t)y);
    memcpy(scratch + (uint32_t)y * W, ri.data + ri.min_x, W);
  }
  int top_edge, bot_edge;
  if (mode == WEATHER_SQUASH_DOWN_EXIT) {   // down exit: bottom leads off the bottom
    // Compress the exit into the first ~75% of the animation so the screen has fully
    // cleared before the card's glance text sweeps across (see prv_draw_flying_content).
    AnimationProgress me = m + m / 3;
    if (me > ANIMATION_NORMALIZED_MAX) me = ANIMATION_NORMALIZED_MAX;
    top_edge = prv_jelly_edge(me, 1, 0, H);
    bot_edge = prv_jelly_edge(me, 0, H, 2 * H);
  } else if (mode == WEATHER_SQUASH_CLOCK_EXIT) {   // clock exit: same geometry, full timeline
    top_edge = prv_jelly_edge(m, 1, 0, H);
    bot_edge = prv_jelly_edge(m, 0, H, 2 * H);
  } else if (mode == WEATHER_SQUASH_RISE_IN) {   // rise from below: top leads up into place
    top_edge = prv_jelly_edge(m, 0, H,     0);
    bot_edge = prv_jelly_edge(m, 1, 2 * H, H);
  } else {                                       // drop-in: bottom leads down into place
    top_edge = prv_jelly_edge(m, 1, -H, 0);
    bot_edge = prv_jelly_edge(m, 0,  0, H);
  }
  int dst_h = bot_edge - top_edge;
  if (dst_h < 1) dst_h = 1;
  const int32_t sy_step = ((int32_t)H << 16) / dst_h;
  const uint8_t white = GColorWhite.argb;
  for (int ay = 0; ay < H; ay++) {
    GBitmapDataRowInfo ri = gbitmap_get_data_row_info(fb, (uint16_t)ay);
    if (ay >= top_edge && ay < bot_edge) {
      int sy = (int)((((int32_t)(ay - top_edge)) * sy_step) >> 16);
      if (sy < 0) sy = 0; else if (sy >= H) sy = H - 1;
      memcpy(ri.data + ri.min_x, scratch + (uint32_t)sy * W, W);
    } else {
      memset(ri.data + ri.min_x, white, W);
    }
  }
  graphics_release_frame_buffer(ctx, fb);
}
#endif  // !PBL_ROUND

void weather_fill_weekday_abbrev(int day_offset, const char *fallback,
                                 char *buffer, size_t buffer_size) {
  if (!buffer || buffer_size == 0) return;
  time_t target = time(NULL) + (time_t)day_offset * 86400;
  struct tm *lt = localtime(&target);
  if (!lt || strftime(buffer, buffer_size, "%a", lt) == 0) {
    if (fallback && fallback[0]) {
      snprintf(buffer, buffer_size, "%.3s", fallback);
    } else {
      snprintf(buffer, buffer_size, "---");
    }
  }
  for (char *c = buffer; *c; c++) {
    if (*c >= 'a' && *c <= 'z') *c = (char)(*c - 'a' + 'A');
  }
}

uint32_t weather_scale_u32(uint32_t value, uint32_t numerator,
                           uint32_t denominator) {
  if (denominator == 0) return 0;

  uint32_t whole = value / denominator;
  uint32_t remainder = value % denominator;
  return (whole * numerator) + ((remainder * numerator) / denominator);
}

int32_t weather_scale_i32(int32_t value, int32_t numerator,
                          int32_t denominator) {
  if (denominator == 0) return 0;

  bool negative = false;
  if (value < 0) {
    value = -value;
    negative = !negative;
  }
  if (numerator < 0) {
    numerator = -numerator;
    negative = !negative;
  }
  if (denominator < 0) {
    denominator = -denominator;
    negative = !negative;
  }

  uint32_t scaled = weather_scale_u32((uint32_t)value, (uint32_t)numerator,
                                      (uint32_t)denominator);
  return negative ? -(int32_t)scaled : (int32_t)scaled;
}

int32_t weather_isqrt(int32_t value) {
  if (value <= 0) return 0;
  uint32_t x = (uint32_t)value;
  uint32_t result = 0;
  uint32_t bit = 1UL << 30;
  while (bit > x) bit >>= 2;
  while (bit != 0) {
    if (x >= result + bit) {
      x -= result + bit;
      result = (result >> 1) + bit;
    } else {
      result >>= 1;
    }
    bit >>= 2;
  }
  return (int32_t)result;
}

int32_t weather_norm_square(int32_t value) {
  if (value <= 0) return 0;
  if (value >= ANIMATION_NORMALIZED_MAX) return ANIMATION_NORMALIZED_MAX;
  return (int32_t)(((uint32_t)value * (uint32_t)value) /
                   (uint32_t)ANIMATION_NORMALIZED_MAX);
}

int32_t weather_norm_bell(int32_t value) {
  if (value <= 0 || value >= ANIMATION_NORMALIZED_MAX) return 0;
  uint32_t v = (uint32_t)value;
  uint32_t max = (uint32_t)ANIMATION_NORMALIZED_MAX;
  return (int32_t)((4U * v * (max - v)) / max);
}

void weather_capture_framebuffer_rect(GBitmap *fb, GBitmap *dst,
                                      GRect src_rect, int dst_x) {
  uint8_t *dst_data = gbitmap_get_data(dst);
  uint16_t dbpr = gbitmap_get_bytes_per_row(dst);
  int sx = src_rect.origin.x;
  int sy = src_rect.origin.y;
  int w = src_rect.size.w;
  int h = src_rect.size.h;

  for (int row = 0; row < h; row++) {
    GBitmapDataRowInfo ri = gbitmap_get_data_row_info(fb, (uint16_t)(sy + row));
    uint8_t *dst_row = dst_data + row * dbpr + dst_x;
    memset(dst_row, 0xFF, (size_t)w);

    int xs = sx > (int)ri.min_x ? sx : (int)ri.min_x;
    int xe = (sx + w - 1) < (int)ri.max_x ? (sx + w - 1) : (int)ri.max_x;
    if (xs <= xe) {
      size_t n = (size_t)(xe - xs + 1);
      memcpy(dst_row + (xs - sx), ri.data + xs, n);
      memset(ri.data + xs, 0xFF, n);
    }
  }
}

static void prv_fill_wrapped_radial(GContext *ctx, GRect rect, uint16_t inset,
                                    int32_t start, int32_t span) {
  const int32_t max = (int32_t)TRIG_MAX_ANGLE;
  start &= max - 1;
  if (span >= max) {
    graphics_fill_radial(ctx, rect, GOvalScaleModeFitCircle, inset, 0, max);
    return;
  }

  int32_t end = start + span;
  if (end <= max) {
    graphics_fill_radial(ctx, rect, GOvalScaleModeFitCircle, inset, start, end);
  } else {
    graphics_fill_radial(ctx, rect, GOvalScaleModeFitCircle, inset, start, max);
    graphics_fill_radial(ctx, rect, GOvalScaleModeFitCircle, inset, 0,
                         end - max);
  }
}

void weather_draw_lava_ring(GContext *ctx, GPoint center, int outer_r,
                            GColor glow_color, uint32_t phase_u,
                            uint8_t idle_progress) {
  int32_t phase = (int32_t)phase_u & ((int32_t)TRIG_MAX_ANGLE - 1);
  int32_t half = (int32_t)(TRIG_MAX_ANGLE / 2);
  int32_t span = half;
  bool idle = idle_progress > 0;
  int32_t neg = idle ? phase : (-phase & ((int32_t)TRIG_MAX_ANGLE - 1));

  if (idle_progress > WEATHER_GLOW_WRAP_SPIN_TICKS) {
    uint8_t close = idle_progress - WEATHER_GLOW_WRAP_SPIN_TICKS;
    if (close > WEATHER_GLOW_WRAP_CLOSE_TICKS) close = WEATHER_GLOW_WRAP_CLOSE_TICKS;
    span += (int32_t)close * (half / WEATHER_GLOW_WRAP_CLOSE_TICKS);
  }

  int halo_r = outer_r + 3;
  GRect halo_rect = { GPoint(center.x - halo_r, center.y - halo_r),
                      GSize(halo_r * 2, halo_r * 2) };
  graphics_context_set_fill_color(ctx, PBL_IF_COLOR_ELSE(GColorCeleste, GColorWhite));
  prv_fill_wrapped_radial(ctx, halo_rect, 1, phase, span);
  prv_fill_wrapped_radial(ctx, halo_rect, 1, neg, span);

  GRect ring_rect = { GPoint(center.x - outer_r, center.y - outer_r),
                      GSize(outer_r * 2, outer_r * 2) };
  graphics_context_set_fill_color(ctx, glow_color);
  prv_fill_wrapped_radial(ctx, ring_rect, 2, phase, span);
  prv_fill_wrapped_radial(ctx, ring_rect, 2, neg, span);

  if (idle) return;

  int sx1 = center.x + (int)((int32_t)sin_lookup(phase) * outer_r / TRIG_MAX_RATIO);
  int sy1 = center.y - (int)((int32_t)cos_lookup(phase) * outer_r / TRIG_MAX_RATIO);
  int sx2 = center.x + (int)((int32_t)sin_lookup(neg) * outer_r / TRIG_MAX_RATIO);
  int sy2 = center.y - (int)((int32_t)cos_lookup(neg) * outer_r / TRIG_MAX_RATIO);
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_circle(ctx, GPoint(sx1, sy1), 2);
  graphics_fill_circle(ctx, GPoint(sx2, sy2), 2);

  int32_t b_ang[3] = {
    (phase * 3 / 2) % (int32_t)TRIG_MAX_ANGLE,
    ((int32_t)(TRIG_MAX_ANGLE * 4) - phase * 2) % (int32_t)TRIG_MAX_ANGLE,
    (phase / 2 + (int32_t)(TRIG_MAX_ANGLE * 2 / 3)) % (int32_t)TRIG_MAX_ANGLE,
  };
  graphics_context_set_fill_color(ctx, glow_color);
  for (int b = 0; b < 3; b++) {
    int bx = center.x + (int)((int32_t)sin_lookup(b_ang[b]) * outer_r / TRIG_MAX_RATIO);
    int by = center.y - (int)((int32_t)cos_lookup(b_ang[b]) * outer_r / TRIG_MAX_RATIO);
    graphics_fill_circle(ctx, GPoint(bx, by), 1);
  }
}
