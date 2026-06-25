/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "detail_face.h"
#include "weather_math.h"
#include "weather_types.h"
#include "pebble_compat.h"
#include <string.h>

// ---- Gauge arc geometry ----
// 270° sweep, opening at the bottom (gap from 4:30 to 7:30).
// angle 0 = 12 o'clock; increases clockwise.
#define DEG_TO_ANGLE(d) ((int32_t)(TRIG_MAX_ANGLE * (d) / 360))
#define GAUGE_ARC_START  DEG_TO_ANGLE(225)   // 7:30 position
#define GAUGE_ARC_SPAN   DEG_TO_ANGLE(270)   // 270° clockwise sweep
#define GAUGE_RING_W     5                   // ring thickness in pixels
#define UPDATED_BAR_H    26
#define UPDATED_BAR_Y    PBL_IF_ROUND_ELSE(18, 0)
#define UPDATED_BAR_FILL_Y PBL_IF_ROUND_ELSE(0, UPDATED_BAR_Y)
#define UPDATED_BAR_FILL_H PBL_IF_ROUND_ELSE(UPDATED_BAR_Y + UPDATED_BAR_H, UPDATED_BAR_H)
#define UPDATED_BAR_TEXT_INSET PBL_IF_ROUND_ELSE(36, 0)
#define UPDATED_BAR_TEXT_Y PBL_IF_ROUND_ELSE(9, UPDATED_BAR_Y + 1)

typedef struct {
  Window *window;
  Layer  *canvas;
  char    location[64];
  char    condition[64];
  int     temp;
  int     high;
  int     low;
  int     uv;        // UV index 0-11, or -1 if unknown
  int     precip_mm; // precipitation in mm, or -1 if unknown
  time_t  updated;   // wall-clock time of last data update
  // Entry animation
  AnimationProgress entry_p;   // 0 → MAX while entry animation plays
  Animation        *entry_anim;
  bool              animated_entry; // true if this push came from the clock exit
  // Temperature bitmap (same capture technique as clock_face)
  GBitmap  *temp_bmp;
  GSize     temp_bmp_sz;
  int16_t   temp_bmp_cx;
  int16_t   temp_bmp_cy;
  // Exit animation (detail → clock)
  AnimationProgress exit_p;
  Animation        *exit_anim;
  AppTimer         *exit_pop_timer;
  bool              exit_active;
  // Weekday animation (non-today day transitions)
  bool              is_weekday_animation;
  char              full_weekday_name[16];  // e.g. "WEDNESDAY"
  int16_t           wd_split_x;             // src column where suffix begins
  int16_t           wd_abbrev_cx;           // src column of the abbreviation's visual centre
  int16_t           wd_word_w;              // true rendered ink width of the full word
  int16_t           wd_ink_l;               // src left edge of the whole word's ink
  int16_t           wd_letter_edge[8];      // src right edge (excl) of each suffix letter
  uint8_t           wd_letter_n;            // number of suffix letters detected
  // Touch gesture tracking (Emery)
  int16_t           touch_start_x;
  int16_t           touch_start_y;
  bool              touch_active;
} DetailFaceData;

static DetailFaceData *s_df;
static bool s_pending_weekday_anim = false;
static char s_pending_weekday[16] = {0};

// ---- Gauge widget ----
// Draws a 270° arc gauge at (cx, cy) with outer radius r.
// label: short string shown above ring (e.g. "UV" or "RAIN").
// value: numeric value. max_val: full-scale reference.
// unit: suffix appended to the displayed value (e.g. "" or "mm").
// unknown: draw "--" with no arc fill.
static void prv_draw_gauge(GContext *ctx, int cx, int cy, int r,
                           const char *label, int value, int max_val,
                           const char *unit, bool unknown, int fade_level) {
  GRect ring_rect = GRect(cx - r, cy - r, 2 * r, 2 * r);

  // Background ring — fades to white
  GColor bg_ring_col = (fade_level == 0) ? GColorLightGray
                     : GColorWhite;
  graphics_context_set_fill_color(ctx, bg_ring_col);
  graphics_fill_radial(ctx, ring_rect, GOvalScaleModeFitCircle,
                       GAUGE_RING_W, GAUGE_ARC_START, GAUGE_ARC_START + GAUGE_ARC_SPAN);

  // Filled ring — fades from vivid → lighter → white
  if (!unknown && max_val > 0) {
    int v = value < 0 ? 0 : (value > max_val ? max_val : value);
    int32_t filled = weather_scale_i32(GAUGE_ARC_SPAN, v, max_val);
    if (filled > 0) {
      GColor fill_col = (fade_level == 0) ? GColorVividCerulean
                      : (fade_level == 1) ? GColorCeleste
                      : GColorWhite;
      graphics_context_set_fill_color(ctx, fill_col);
      graphics_fill_radial(ctx, ring_rect, GOvalScaleModeFitCircle,
                           GAUGE_RING_W, GAUGE_ARC_START, GAUGE_ARC_START + filled);
    }
  }

  // Value text centred inside ring
  GFont vfont = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
  char vbuf[12];
  if (unknown) {
    snprintf(vbuf, sizeof(vbuf), "--");
  } else {
    snprintf(vbuf, sizeof(vbuf), "%d%s", value, unit);
  }
  GColor text_col = (fade_level == 0) ? GColorBlack
                  : (fade_level == 1) ? GColorDarkGray
                  : GColorLightGray;
  graphics_context_set_text_color(ctx, text_col);
  graphics_draw_text(ctx, vbuf, vfont,
      GRect(cx - 18, cy - 8, 36, 16),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

  // Label above ring
  graphics_context_set_text_color(ctx, text_col);
  graphics_draw_text(ctx, label, vfont,
      GRect(cx - 18, cy - r - 16, 36, 14),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}

// ---- Exit animation (detail → clock) ----
static void prv_exit_pop_callback(void *ctx) {
  if (s_df) s_df->exit_pop_timer = NULL;
  window_stack_pop(true);
}

static void prv_detail_exit_update(Animation *anim, AnimationProgress progress) {
  if (!s_df) return;
  s_df->exit_p = progress;
  if (s_df->canvas) layer_mark_dirty(s_df->canvas);
  if (progress >= ANIMATION_NORMALIZED_MAX && !s_df->exit_pop_timer) {
    s_df->exit_pop_timer = app_timer_register(0, prv_exit_pop_callback, NULL);
  }
}
static const AnimationImplementation s_exit_impl = { .update = prv_detail_exit_update };

static void prv_start_exit_animation(void) {
  if (!s_df || s_df->exit_active) return;
  s_df->exit_active = true;
  s_df->exit_p = 0;
  s_df->exit_anim = animation_create();
  animation_set_implementation(s_df->exit_anim, &s_exit_impl);
  animation_set_duration(s_df->exit_anim, 200);
  animation_set_curve(s_df->exit_anim, AnimationCurveEaseIn);
  animation_schedule(s_df->exit_anim);
}

// ---- Entry animation ----
static void prv_entry_update(Animation *anim, AnimationProgress progress) {
  if (!s_df) return;
  s_df->entry_p = progress;
  if (s_df->canvas) layer_mark_dirty(s_df->canvas);
}
static const AnimationImplementation s_entry_impl = { .update = prv_entry_update };

static void prv_start_entry_animation(void) {
  if (!s_df) return;
  s_df->entry_p = 0;
  s_df->entry_anim = animation_create();
  animation_set_implementation(s_df->entry_anim, &s_entry_impl);
  animation_set_duration(s_df->entry_anim, 500);
  animation_set_curve(s_df->entry_anim, AnimationCurveEaseOut);
  animation_schedule(s_df->entry_anim);
}

// ---- Draw proc ----
static void prv_format_updated(char *buffer, size_t buffer_size, time_t updated) {
  if (!buffer || buffer_size == 0) return;

  if (updated <= 0) {
    snprintf(buffer, buffer_size, "Updated --:--");
    return;
  }

  struct tm *lt = localtime(&updated);
  char time_buf[8];
  if (clock_is_24h_style()) {
    strftime(time_buf, sizeof(time_buf), "%H:%M", lt);
  } else {
    strftime(time_buf, sizeof(time_buf), "%I:%M", lt);
    if (time_buf[0] == '0') memmove(time_buf, time_buf + 1, sizeof(time_buf) - 1);
  }
  snprintf(buffer, buffer_size, "Updated %s", time_buf);
}

static void prv_draw_updated_banner(GContext *ctx, GRect bounds,
                                    AnimationProgress entry_p,
                                    AnimationProgress exit_p,
                                    bool exit_active) {
  char ubuf[32];
  prv_format_updated(ubuf, sizeof(ubuf), s_df ? s_df->updated : 0);

  const int bar_w = bounds.size.w;
  int x_off = 0;
  if (exit_active) {
    x_off = weather_scale_i32(bar_w, exit_p, ANIMATION_NORMALIZED_MAX);
  } else {
    const AnimationProgress p_in =
        (AnimationProgress)((int32_t)ANIMATION_NORMALIZED_MAX * 200 / 500);
    if (entry_p < p_in) {
      int32_t t = weather_scale_i32(entry_p, ANIMATION_NORMALIZED_MAX, p_in);
      x_off = -bar_w + (int)((int32_t)bar_w * t / ANIMATION_NORMALIZED_MAX);
    }
  }

  const int safe = UPDATED_BAR_TEXT_INSET;
  GRect fill_rect = GRect(bounds.origin.x + x_off,
                          bounds.origin.y + UPDATED_BAR_FILL_Y,
                          bar_w, UPDATED_BAR_FILL_H);
  GRect bar_rect = GRect(bounds.origin.x + x_off,
                         bounds.origin.y + UPDATED_BAR_Y,
                         bar_w, UPDATED_BAR_H);
  graphics_context_set_fill_color(ctx, GColorVividCerulean);
  graphics_fill_rect(ctx, fill_rect, 0, GCornerNone);
  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, ubuf, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
      GRect(bar_rect.origin.x + safe + 4, bounds.origin.y + UPDATED_BAR_TEXT_Y,
            bar_w - (safe * 2) - 8, UPDATED_BAR_H),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}

static void prv_canvas_draw(Layer *layer, GContext *ctx) {
  if (!s_df) return;
  GRect bounds = layer_get_bounds(layer);
  int W = bounds.size.w;
  int H = bounds.size.h;

  // Entry animation helpers.
  // ep: 0 (start) → ANIMATION_NORMALIZED_MAX (done)
  AnimationProgress ep = s_df->entry_p;
  if (!s_df->animated_entry && !s_df->entry_anim) ep = ANIMATION_NORMALIZED_MAX;
  bool entry_done = (ep >= ANIMATION_NORMALIZED_MAX);
  // Exit animation helpers.
  AnimationProgress xp = s_df->exit_p;
  bool exit_active = s_df->exit_active;
  // During exit: fade elements (0=normal, 1=dim, 2=nearly-gone)
  int fade_level = 0;
  if (exit_active) {
    if      (xp > (AnimationProgress)(ANIMATION_NORMALIZED_MAX * 2 / 3)) fade_level = 2;
    else if (xp > (AnimationProgress)(ANIMATION_NORMALIZED_MAX / 3))     fade_level = 1;
  }
  GColor fade_text = (fade_level == 0) ? GColorBlack
                   : (fade_level == 1) ? GColorDarkGray
                   : GColorLightGray;
  bool show_others = true; // elements always drawn; fade colors handle the disappearance
  // slide_off: how many pixels below target each element still is (0 when done)
  int slide_off = entry_done ? 0
      : (int)(12 * (ANIMATION_NORMALIZED_MAX - (int32_t)ep) / ANIMATION_NORMALIZED_MAX);
  // Staggered reveal thresholds (each element appears at a different progress %)
  bool show_location = entry_done || ep > (AnimationProgress)(ANIMATION_NORMALIZED_MAX * 55 / 100);
  bool show_gauges   = entry_done || ep > (AnimationProgress)(ANIMATION_NORMALIZED_MAX * 70 / 100);
  // Other elements always visible (temp is instantly at its final size)
  bool after_settle  = true;

  // White background
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  // ---- Temperature / Weekday — captured bitmap scaled ----
  int center_label_top_y = H / 2;
  bool is_weekday = s_df->is_weekday_animation && s_df->full_weekday_name[0];
  bool temp_known = (s_df->temp != WEATHER_SERVICE_LOCATION_FORECAST_UNKNOWN_TEMP) || is_weekday;
  if (temp_known) {
    // Lazy-capture: same font + pixel technique as clock_face so the bitmaps are identical.
    if (!s_df->temp_bmp) {
      if (is_weekday) {
        // Capture the WHOLE word ("WEDNESDAY") at the SAME font as every other
        // weekday (BITHAM_30_BLACK). A word can render wider than the framebuffer
        // (only W px), so capturing it in one shot truncates the tail (the 'Y').
        // Instead we assemble the word into an in-memory bitmap that may be wider
        // than the screen: render the word repeatedly, each pass shifted so a new
        // slice lands inside the framebuffer, and copy that slice into the wide
        // bitmap. Short days take a single pass and are byte-identical to before.
        GFont tf = fonts_get_system_font(FONT_KEY_BITHAM_30_BLACK);
        const char *full = s_df->full_weekday_name;
        // The clock shows the 3-letter abbreviation (e.g. "WED"), which is the
        // first 3 letters of the full weekday name. Derive it here so the real
        // location name can stay in s_df->location for the top label.
        int abbrev_len = 3;
        char abbrev[8] = {0};
        strncpy(abbrev, full, abbrev_len < 7 ? abbrev_len : 7);
        GSize fsz = graphics_text_layout_get_content_size(
            full, tf, GRect(0, 0, 400, 50),
            GTextOverflowModeFill, GTextAlignmentLeft);
        GSize asz = graphics_text_layout_get_content_size(
            abbrev, tf, GRect(0, 0, 200, 50),
            GTextOverflowModeFill, GTextAlignmentLeft);
        const int word_w = fsz.w;
        const int stg_h  = fsz.h + 12;
        const int bmp_w  = word_w + 8;                 // in-memory; may exceed W
        const int font_top_pad = -4;
        int ty_s = stg_h / 2 - fsz.h / 2 + font_top_pad;
        const int slice = W;                           // max columns per capture pass
        const int stg_y0 = PBL_IF_ROUND_ELSE((H - stg_h) / 2, 0);

        GBitmap *bmp = gbitmap_create_blank(GSize(bmp_w, stg_h), GBitmapFormat8Bit);
        if (bmp) {
          uint8_t  *dst  = gbitmap_get_data(bmp);
          uint16_t  dbpr = gbitmap_get_bytes_per_row(bmp);
          for (int row = 0; row < stg_h; row++) memset(dst + row * dbpr, 0xFF, bmp_w);

          for (int off = 0; off < word_w; off += slice) {
            int this_w = (word_w - off < slice) ? (word_w - off) : slice;
            // Render the whole word shifted left by 'off' so word-column 'off'
            // lands at framebuffer x=0; the left part clips off-screen harmlessly.
            graphics_context_set_fill_color(ctx, GColorWhite);
            graphics_fill_rect(ctx, GRect(0, stg_y0, this_w + 2, stg_h), 0, GCornerNone);
            graphics_context_set_text_color(ctx, GColorBlack);
            graphics_draw_text(ctx, full, tf,
                GRect(-off, stg_y0 + ty_s, word_w + 8, fsz.h),
                GTextOverflowModeFill, GTextAlignmentLeft, NULL);

            GBitmap *fb = graphics_capture_frame_buffer(ctx);
            if (fb) {
              weather_capture_framebuffer_rect(fb, bmp, GRect(0, stg_y0, this_w, stg_h), off);
              graphics_release_frame_buffer(ctx, fb);
            }
          }

          // Scan the assembled pixels for the true ink extent so the word is
          // centred on its real centre regardless of font-metric quirks.
          int ink_l = bmp_w, ink_r = -1;
          int ink_t = stg_h, ink_b = -1;
          for (int row = 0; row < stg_h; row++) {
            uint8_t *r = dst + row * dbpr;
            for (int col = 0; col < bmp_w; col++) {
              if (r[col] != 0xFF) {
                if (col < ink_l) ink_l = col;
                if (col > ink_r) ink_r = col;
                if (row < ink_t) ink_t = row;
                if (row > ink_b) ink_b = row;
              }
            }
          }
          if (ink_r < ink_l) { ink_l = 0; ink_r = word_w - 1; }
          if (ink_b < ink_t) { ink_t = 0; ink_b = stg_h - 1; }
          int ink_w  = ink_r - ink_l + 1;
          int ink_cx = (ink_l + ink_r) / 2;
          int ink_cy = (ink_t + ink_b) / 2;

          s_df->temp_bmp     = bmp;
          s_df->temp_bmp_sz  = GSize(bmp_w, stg_h);
          s_df->temp_bmp_cx  = (int16_t)ink_cx;                 // true full-word ink centre
          s_df->temp_bmp_cy  = (int16_t)ink_cy;                 // true vertical ink centre
          s_df->wd_word_w    = (int16_t)ink_w;                  // true ink width
          s_df->wd_split_x   = (int16_t)(ink_l + asz.w);        // suffix begins here
          s_df->wd_abbrev_cx = (int16_t)(ink_l + asz.w / 2);    // abbreviation centre
          s_df->wd_ink_l     = (int16_t)ink_l;                  // whole-word ink left

          // Find each suffix letter's right edge so the tail can "type" in one
          // glyph at a time. A letter is a run of ink columns ended by a fully
          // blank column; falls back to an even split if glyphs touch.
          {
            int sfx_l = ink_l + asz.w;
            int n = 0;
            bool in_glyph = false;
            for (int col = sfx_l; col <= ink_r && n < 8; col++) {
              bool has_ink = false;
              for (int row = ink_t; row <= ink_b; row++) {
                if (dst[row * dbpr + col] != 0xFF) { has_ink = true; break; }
              }
              if (has_ink) {
                in_glyph = true;
              } else if (in_glyph) {
                s_df->wd_letter_edge[n++] = (int16_t)col;
                in_glyph = false;
              }
            }
            if (in_glyph && n < 8) s_df->wd_letter_edge[n++] = (int16_t)(ink_r + 1);
            int sfx_chars = 0;
            for (int i = 0; full[i]; i++) sfx_chars++;
            sfx_chars -= abbrev_len;             // suffix = full word minus abbreviation
            if (sfx_chars > 0 && n < sfx_chars) { // touching glyphs: even split
              if (sfx_chars > 8) sfx_chars = 8;
              int span = (ink_r + 1) - sfx_l;
              for (int i = 0; i < sfx_chars; i++) {
                s_df->wd_letter_edge[i] = (int16_t)(sfx_l + (span * (i + 1)) / sfx_chars);
              }
              n = sfx_chars;
            }
            s_df->wd_letter_n = (uint8_t)n;
          }
        }
      } else {
        GFont tf = fonts_get_system_font(FONT_KEY_LECO_26_BOLD_NUMBERS_AM_PM);
        GFont dg = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
        char tbuf[16];
        snprintf(tbuf, sizeof(tbuf), "%d", s_df->temp);
        GSize nsz = graphics_text_layout_get_content_size(
            tbuf, tf, GRect(0, 0, 150, 50),
            GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft);
        const int stg_w  = nsz.w + 18;
        const int stg_h  = nsz.h + 12;
        const int stg_x  = PBL_IF_ROUND_ELSE((W - stg_w) / 2, 0);
        const int stg_y  = PBL_IF_ROUND_ELSE((H - stg_h) / 2, 0);
        const int stg_cx = stg_w / 2;
        const int stg_cy = stg_h / 2;
        const int font_top_pad = -4;
        int ty_s = stg_cy - nsz.h / 2 + font_top_pad;

        graphics_context_set_fill_color(ctx, GColorWhite);
        graphics_fill_rect(ctx, GRect(stg_x, stg_y, stg_w, stg_h), 0, GCornerNone);
        graphics_context_set_text_color(ctx, GColorBlack);
        graphics_draw_text(ctx, tbuf, tf,
            GRect(stg_x + stg_cx - nsz.w / 2, stg_y + ty_s, nsz.w + 2, nsz.h),
            GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
        graphics_draw_text(ctx, "\xC2\xB0", dg,
            GRect(stg_x + stg_cx + nsz.w / 2 + 1, stg_y + ty_s + 2, 12, 18),
            GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

        GBitmap *fb = graphics_capture_frame_buffer(ctx);
        if (fb) {
          GBitmap *bmp = gbitmap_create_blank(GSize(stg_w, stg_h), GBitmapFormat8Bit);
          if (bmp) {
            weather_capture_framebuffer_rect(fb, bmp, GRect(stg_x, stg_y, stg_w, stg_h), 0);
            int ink_t = stg_h;
            int ink_b = -1;
            uint8_t *data = gbitmap_get_data(bmp);
            uint16_t bpr = gbitmap_get_bytes_per_row(bmp);
            for (int row = 0; row < stg_h; row++) {
              uint8_t *r = data + row * bpr;
              for (int col = 0; col < stg_w; col++) {
                if (r[col] != 0xFF) {
                  if (row < ink_t) ink_t = row;
                  if (row > ink_b) ink_b = row;
                  break;
                }
              }
            }
            s_df->temp_bmp    = bmp;
            s_df->temp_bmp_sz = GSize(stg_w, stg_h);
            s_df->temp_bmp_cx = (int16_t)stg_cx;
            s_df->temp_bmp_cy = (ink_b >= ink_t)
                ? (int16_t)((ink_t + ink_b) / 2)
                : (int16_t)(stg_cy - font_top_pad / 2);
          }
          graphics_release_frame_buffer(ctx, fb);
        }
      }
    }

    if (s_df->temp_bmp && is_weekday) {
      // ---- Weekday: scale the single full-word bitmap ----
      int src_w = s_df->temp_bmp_sz.w;
      int src_h = s_df->temp_bmp_sz.h;
      int split_x = s_df->wd_split_x;

      // Same font as every other weekday. The word is captured at native
      // BITHAM_30; if its true ink is wider than the screen, scale the final
      // result down just enough that the whole word fits. Short days have ink
      // narrower than the screen, so they stay at native size (untouched).
      int word_w = s_df->wd_word_w;
      int max_w = W - 16;
      int32_t END_SCALE = (word_w > max_w)
          ? weather_scale_i32(max_w, ANIMATION_NORMALIZED_MAX, word_w)
          : ANIMATION_NORMALIZED_MAX;
      if (END_SCALE > ANIMATION_NORMALIZED_MAX) END_SCALE = ANIMATION_NORMALIZED_MAX;

      int32_t scale;
      int reveal_x;            // src column up to which the suffix is drawn (typing)

      if (exit_active) {
        // Exit: grow a bit, then shrink with squash impact as it returns to clock.
        const int32_t EXIT_MAX_SCALE = (105 * ANIMATION_NORMALIZED_MAX) / 100; // only 1.05x growth on exit
        int32_t base = EXIT_MAX_SCALE - (int32_t)xp;
        int32_t t  = (int32_t)xp;
        int32_t mt = ANIMATION_NORMALIZED_MAX - t;
        // Squash effect for impact: width widens, height shrinks
        int32_t squash = (int32_t)(3 * t / ANIMATION_NORMALIZED_MAX * mt /
                                   ANIMATION_NORMALIZED_MAX *
                                   ANIMATION_NORMALIZED_MAX / 3);
        int scale_x = base + squash / 2;
        int scale_y = base - squash / 2;
        if (scale_x < ANIMATION_NORMALIZED_MAX / 4) scale_x = ANIMATION_NORMALIZED_MAX / 4;
        if (scale_y < ANIMATION_NORMALIZED_MAX / 4) scale_y = ANIMATION_NORMALIZED_MAX / 4;
        // For simplicity, average the scales
        scale = (scale_x + scale_y) / 2;
        reveal_x = src_w;                         // whole word visible
      } else if (!entry_done) {
        // Sequential entry choreography:
        //  Phase 1 (0 -> SPLIT): the 3-letter abbreviation scales up from the
        //     clock hand-off size to its final scale, held centred. Suffix hidden.
        //  Phase 2 (SPLIT -> MAX): scale is locked; the rest of the word is
        //     "typed" in one glyph at a time, left to right, while the word
        //     recentres around the growing ink (see pivot below). No scaling
        //     during the type, so long words stay crisp.
        const int32_t SPLIT = ANIMATION_NORMALIZED_MAX * 45 / 100;
        const int32_t HANDOFF_SCALE = (8 * ANIMATION_NORMALIZED_MAX) / 10; // 0.8x, matches clock

        if (ep < SPLIT) {
          int32_t p = weather_scale_i32(ep, ANIMATION_NORMALIZED_MAX, SPLIT);       // 0..MAX in phase 1
          scale = HANDOFF_SCALE
              + weather_scale_i32(END_SCALE - HANDOFF_SCALE, p,
                                  ANIMATION_NORMALIZED_MAX);
          reveal_x = split_x;                     // suffix hidden while scaling
        } else {
          int32_t p = ((ep - SPLIT) * ANIMATION_NORMALIZED_MAX)
              / (ANIMATION_NORMALIZED_MAX - SPLIT);                  // 0..MAX in phase 2
          scale = END_SCALE;                                        // locked at final scale
          if (s_df->wd_letter_n == 0) {
            reveal_x = src_w;
          } else {
            // Reveal one more glyph per tick; all letters in by 88% so the
            // final stretch holds the finished word before it settles.
            const int32_t TYPE_END = ANIMATION_NORMALIZED_MAX * 88 / 100;
            int letters_done = (p >= TYPE_END)
                ? s_df->wd_letter_n
                : weather_scale_i32(p, s_df->wd_letter_n, TYPE_END);
            if (letters_done <= 0)                      reveal_x = split_x;
            else if (letters_done >= s_df->wd_letter_n) reveal_x = src_w;
            else                                        reveal_x = s_df->wd_letter_edge[letters_done - 1];
          }
        }
      } else {
        scale = END_SCALE;
        reveal_x = src_w;                         // whole word visible
      }

      // Horizontal pivot tracks the centre of the currently-revealed ink, so the
      // word stays balanced as it types in (abbrev centred -> whole word
      // centred). Derived from the ink bounds, so it needs no separate state.
      int ink_l_src = s_df->wd_ink_l;
      int ink_r_src = s_df->wd_ink_l + s_df->wd_word_w - 1;
      int vis_r = (reveal_x - 1 < ink_r_src) ? (reveal_x - 1) : ink_r_src;
      if (vis_r < ink_l_src) vis_r = ink_l_src;
      int pivot = (ink_l_src + vis_r) / 2;

      int dst_w = src_w * scale / ANIMATION_NORMALIZED_MAX;
      int dst_h = src_h * scale / ANIMATION_NORMALIZED_MAX;
      if (dst_w < 1) dst_w = 1;
      if (dst_h < 1) dst_h = 1;
      int pivot_sc = pivot * dst_w / src_w;
      int cy_sc    = s_df->temp_bmp_cy * dst_h / src_h;

      // Vertical resting position: 20px above screen centre. The clock hands
      // the word off at screen centre (H/2); on entry it rises 20px and on exit
      // it drops back to H/2 to realign with the clock's weekday.
      int rest_y = PBL_IF_ROUND_ELSE(H / 2, H / 2 - 20);
      int target_y;
      if (exit_active) {
        // Drop to clock centre throughout the exit, aiming for H/2 to align with clock's weekday
        int32_t mp = (int32_t)xp;
        target_y = rest_y + weather_scale_i32(H / 2 - rest_y, mp,
                                              ANIMATION_NORMALIZED_MAX);
      } else if (!entry_done) {
        // Gracefully rise 20px from the clock hand-off (H/2) up toward the final
        // resting position while the abbreviation scales up. The rise tracks the
        // scale window (phase 1) so the word lifts smoothly as it grows and
        // arrives exactly when scaling completes — no teleport, no overshoot.
        const int32_t SCALE_SPLIT = ANIMATION_NORMALIZED_MAX * 45 / 100;
        int32_t vp = (ep >= SCALE_SPLIT)
            ? ANIMATION_NORMALIZED_MAX
            : (ep * ANIMATION_NORMALIZED_MAX) / SCALE_SPLIT;
        target_y = H / 2 + weather_scale_i32(rest_y - H / 2, vp,
                                             ANIMATION_NORMALIZED_MAX);
      } else {
        target_y = rest_y;
      }
      int abs_x = W / 2 - pivot_sc;
      int abs_y = target_y - cy_sc;
      center_label_top_y = abs_y;

      GBitmap *fb = graphics_capture_frame_buffer(ctx);
      if (fb) {
        GRect fbb = gbitmap_get_bounds(fb);
        int fb_h = fbb.size.h;
        uint8_t *sdata = gbitmap_get_data(s_df->temp_bmp);
        uint16_t sbpr  = gbitmap_get_bytes_per_row(s_df->temp_bmp);
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
          int32_t sx_fp = sx_step >> 1;
          for (int dx = 0; dx < dst_w; dx++, sx_fp += sx_step) {
            int sx = sx_fp >> 16;
            if (sx >= src_w) sx = src_w - 1;
            int ax = abs_x + dx;
            if (ax < (int)ri.min_x || ax > (int)ri.max_x) continue;
            uint8_t pixel = srow[sx];
            if (pixel == 0xFF) continue;          // background
            if (sx >= reveal_x) continue;         // suffix glyph not yet typed in
            ri.data[ax] = pixel;                  // solid, antialiased glyph
          }
        }
        graphics_release_frame_buffer(ctx, fb);
      }
    } else if (s_df->temp_bmp) {
      int src_w = s_df->temp_bmp_sz.w;
      int src_h = s_df->temp_bmp_sz.h;
      int32_t scale_x, scale_y;
      const int32_t FINAL_SCALE = 2 * ANIMATION_NORMALIZED_MAX;
      if (exit_active) {
        int32_t base = 2 * ANIMATION_NORMALIZED_MAX - (int32_t)xp;
        int32_t t  = (int32_t)xp;
        int32_t mt = ANIMATION_NORMALIZED_MAX - t;
        int32_t squash = (int32_t)(4 * t / ANIMATION_NORMALIZED_MAX * mt /
                                   ANIMATION_NORMALIZED_MAX *
                                   ANIMATION_NORMALIZED_MAX / 4);
        scale_x = base + squash / 2;
        scale_y = base - squash / 2;
        if (scale_x < ANIMATION_NORMALIZED_MAX / 4) scale_x = ANIMATION_NORMALIZED_MAX / 4;
        if (scale_y < ANIMATION_NORMALIZED_MAX / 4) scale_y = ANIMATION_NORMALIZED_MAX / 4;
      } else {
        scale_x = scale_y = FINAL_SCALE;
      }
      int dst_w = src_w * scale_x / ANIMATION_NORMALIZED_MAX;
      int dst_h = src_h * scale_y / ANIMATION_NORMALIZED_MAX;
      if (dst_w < 1) dst_w = 1;
      if (dst_h < 1) dst_h = 1;
      int cx_sc = s_df->temp_bmp_cx * dst_w / src_w;
      int cy_sc = s_df->temp_bmp_cy * dst_h / src_h;
      int abs_x = W / 2 - cx_sc;
      int abs_y = H / 2 - cy_sc;
      center_label_top_y = abs_y;
      GBitmap *fb = graphics_capture_frame_buffer(ctx);
      if (fb) {
        GRect fbb = gbitmap_get_bounds(fb);
        int fb_h = fbb.size.h;
        uint8_t *sdata = gbitmap_get_data(s_df->temp_bmp);
        uint16_t sbpr  = gbitmap_get_bytes_per_row(s_df->temp_bmp);
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
            if (pixel == 0xFF) continue; // skip white (background)
            ri.data[ax] = pixel;
          }
        }
        graphics_release_frame_buffer(ctx, fb);
      }
    }
  }

  // ---- Location name (centred between Updated label and temperature) ----
  if (show_location && after_settle && show_others) {
    GFont loc_font = fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);
    int loc_y = H / 2 - 80;
    // Round (getafix): centre the location vertically between the blue banner and
    // the top of the weekday/temperature label. (The original gated this on the
    // SDK macro PBL_PLATFORM_GABBRO, which is not defined in the firmware build,
    // so it was compiled out — use PBL_ROUND, which is set for getafix.)
#if PBL_ROUND
    int banner_bottom_y = UPDATED_BAR_Y + UPDATED_BAR_H;
    int loc_center_y = (banner_bottom_y + center_label_top_y) / 2;
    loc_y = loc_center_y - 8;  // nudged down from centred
#else
    (void)center_label_top_y;
#endif
    graphics_context_set_text_color(ctx, fade_text);
    graphics_draw_text(ctx,
        s_df->location[0] ? s_df->location : "--",
        loc_font,
        GRect(4, loc_y + slide_off, W - 8, 34),
        GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  }

  // (Weather condition phrase removed from the UV/RAIN screen per design.)
  if (show_gauges && after_settle && show_others) {
    int gauge_r  = 26;
    int gauge_cy = H * 4 / 5 + slide_off;
    prv_draw_gauge(ctx, W / 4,     gauge_cy, gauge_r,
                   "UV",   s_df->uv,        11, "",   s_df->uv < 0,        fade_level);
    prv_draw_gauge(ctx, 3 * W / 4, gauge_cy, gauge_r,
                   "RAIN", s_df->precip_mm, 100, "%", s_df->precip_mm < 0, fade_level);
  }

  // Draw last so the top HUD stays clean above the temperature bitmap capture pass.
  prv_draw_updated_banner(ctx, bounds, ep, xp, exit_active);
}

// ---- Input ----
// Back button → main weather menu (pop detail + clock)
static void prv_click_back(ClickRecognizerRef r, void *ctx) {
  window_stack_pop(false);
  window_stack_pop(true);
}
// Select button → clock screen with exit animation
static void prv_click_select(ClickRecognizerRef r, void *ctx) {
  prv_start_exit_animation();
}
static void prv_click_provider(void *ctx) {
  window_single_click_subscribe(BUTTON_ID_BACK,   prv_click_back);
  window_single_click_subscribe(BUTTON_ID_SELECT, prv_click_select);
  window_single_click_subscribe(BUTTON_ID_UP,     prv_click_select);
  window_single_click_subscribe(BUTTON_ID_DOWN,   prv_click_back);
}

#if WEATHER_PLATFORM_TOUCH_COLOR
#define DETAIL_SWIPE_THRESHOLD 20
static void prv_touch_handler(const TouchEvent *event, void *context) {
  if (!s_df) return;
  if (event->type == TouchEvent_Touchdown) {
    s_df->touch_start_x = event->x;
    s_df->touch_start_y = event->y;
    s_df->touch_active  = true;
  } else if (event->type == TouchEvent_Liftoff && s_df->touch_active) {
    s_df->touch_active = false;
    int16_t dx  = event->x - s_df->touch_start_x;
    int16_t dy  = event->y - s_df->touch_start_y;
    int16_t adx = dx < 0 ? -dx : dx;
    int16_t ady = dy < 0 ? -dy : dy;
    if (dx > DETAIL_SWIPE_THRESHOLD && adx > ady) {
      // Swipe right → back to the main weather menu (pop detail + clock).
      window_stack_pop(false);
      window_stack_pop(true);
    } else if (dy > DETAIL_SWIPE_THRESHOLD && ady > adx) {
      // Swipe down → back to the clock screen with the exit animation.
      prv_start_exit_animation();
    } else if (adx <= DETAIL_SWIPE_THRESHOLD && ady <= DETAIL_SWIPE_THRESHOLD) {
      // Tap → back to the clock screen with the exit animation.
      prv_start_exit_animation();
    }
  }
}
#endif

// ---- Window lifecycle ----
static void prv_window_load(Window *window) {
  window_set_background_color(window, GColorWhite);
  GRect bounds = layer_get_bounds(window_get_root_layer(window));
  Layer *canvas = layer_create(bounds);
  layer_set_update_proc(canvas, prv_canvas_draw);
  layer_add_child(window_get_root_layer(window), canvas);
  if (s_df) s_df->canvas = canvas;
  window_set_click_config_provider(window, prv_click_provider);
#if WEATHER_PLATFORM_TOUCH_COLOR
  touch_service_subscribe(prv_touch_handler, NULL);
#endif
  // Start entry animation if this was an animated push
  if (s_df && s_df->animated_entry) prv_start_entry_animation();
}

static void prv_window_unload(Window *window) {
#if WEATHER_PLATFORM_TOUCH_COLOR
  // Only unsubscribe if not already done by touch handler
  touch_service_unsubscribe();
#endif
  if (s_df) {
    if (s_df->exit_anim) {
      animation_unschedule(s_df->exit_anim);
      animation_destroy(s_df->exit_anim);
      s_df->exit_anim = NULL;
    }
    if (s_df->exit_pop_timer) {
      app_timer_cancel(s_df->exit_pop_timer);
      s_df->exit_pop_timer = NULL;
    }
    if (s_df->entry_anim) {
      animation_unschedule(s_df->entry_anim);
      animation_destroy(s_df->entry_anim);
      s_df->entry_anim = NULL;
    }
    if (s_df->temp_bmp) {
      gbitmap_destroy(s_df->temp_bmp);
      s_df->temp_bmp = NULL;
    }
    if (s_df->canvas) {
      layer_destroy(s_df->canvas);
      s_df->canvas = NULL;
    }
  }
  window_destroy(window);
  free(s_df);
  s_df = NULL;
}

// ---- Public API ----
void detail_face_set_weekday_animation(const char *full_weekday_name) {
  if (s_df) return;  // can only set before push
  if (full_weekday_name) {
    s_pending_weekday_anim = true;
    strncpy(s_pending_weekday, full_weekday_name, sizeof(s_pending_weekday) - 1);
    s_pending_weekday[sizeof(s_pending_weekday) - 1] = '\0';
  } else {
    s_pending_weekday_anim = false;
  }
}

// Map a country/region segment to a short code where known (UK, US, ...).
// Falls back to the trimmed segment uppercased.
static void prv_country_code(const char *seg, char *out, size_t out_sz) {
  while (*seg == ' ') seg++;                 // skip leading spaces
  char low[48];
  size_t n = 0;
  for (; seg[n] && n < sizeof(low) - 1; n++) {
    char c = seg[n];
    low[n] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
  }
  while (n > 0 && low[n - 1] == ' ') n--;     // trim trailing spaces
  low[n] = '\0';

  static const struct { const char *key; const char *code; } kMap[] = {
    {"northern ireland", "UK"}, {"united kingdom", "UK"}, {"great britain", "UK"},
    {"england", "UK"}, {"scotland", "UK"}, {"wales", "UK"}, {"gbr", "UK"}, {"gb", "UK"},
    {"united states", "US"}, {"america", "US"}, {"usa", "US"},
    {"australia", "AU"}, {"canada", "CA"}, {"ireland", "IE"}, {"france", "FR"},
    {"germany", "DE"}, {"spain", "ES"}, {"italy", "IT"}, {"netherlands", "NL"},
    {"new zealand", "NZ"}, {"japan", "JP"}, {"china", "CN"}, {"india", "IN"},
    {"brazil", "BR"}, {"mexico", "MX"},
  };
  for (size_t i = 0; i < sizeof(kMap) / sizeof(kMap[0]); i++) {
    if (strstr(low, kMap[i].key)) {
      strncpy(out, kMap[i].code, out_sz - 1);
      out[out_sz - 1] = '\0';
      return;
    }
  }
  size_t o = 0;                               // unknown: uppercase as best effort
  for (size_t i = 0; i < n && o < out_sz - 1; i++) {
    char c = low[i];
    out[o++] = (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
  }
  out[o] = '\0';
}

// Reduce a raw location ("Town, Region, Country") to "Town, CC": the text before
// the first comma + the abbreviated text after the last comma.
static void prv_format_location(const char *raw, char *out, size_t out_sz) {
  if (!raw || !raw[0]) { if (out_sz) out[0] = '\0'; return; }
  const char *first = strchr(raw, ',');
  size_t town_len = first ? (size_t)(first - raw) : strlen(raw);
  while (town_len > 0 && raw[town_len - 1] == ' ') town_len--;
  char town[40];
  size_t tn = town_len < sizeof(town) - 1 ? town_len : sizeof(town) - 1;
  memcpy(town, raw, tn);
  town[tn] = '\0';
  if (!first) {                               // no country part — town only
    strncpy(out, town, out_sz - 1);
    out[out_sz - 1] = '\0';
    return;
  }
  char cc[16];
  prv_country_code(strrchr(raw, ',') + 1, cc, sizeof(cc));
  snprintf(out, out_sz, "%s, %s", town, cc);
}

void detail_face_push(const WeatherLocationForecast *forecast) {
  if (s_df) return;  // already showing
  s_df = calloc(1, sizeof(DetailFaceData));
  if (!s_df) return;

  if (forecast) {
    s_df->temp      = forecast->current_temp;
    s_df->high      = forecast->today_high;
    s_df->low       = forecast->today_low;
    s_df->uv        = forecast->today_uv;
    s_df->precip_mm = forecast->today_precip_mm;
    s_df->updated   = forecast->time_updated_utc;
    if (forecast->location_name) {
      prv_format_location(forecast->location_name, s_df->location, sizeof(s_df->location));
    }
    if (forecast->current_weather_phrase) {
      strncpy(s_df->condition, forecast->current_weather_phrase, sizeof(s_df->condition) - 1);
    }
  } else {
    s_df->temp      = WEATHER_SERVICE_LOCATION_FORECAST_UNKNOWN_TEMP;
    s_df->high      = WEATHER_SERVICE_LOCATION_FORECAST_UNKNOWN_TEMP;
    s_df->low       = WEATHER_SERVICE_LOCATION_FORECAST_UNKNOWN_TEMP;
    s_df->uv        = -1;
    s_df->precip_mm = -1;
    s_df->updated   = 0;
  }

  s_df->window = window_create();
  window_set_window_handlers(s_df->window, (WindowHandlers){
    .load   = prv_window_load,
    .unload = prv_window_unload,
  });
  window_stack_push(s_df->window, false); // no system slide — we do our own
}

// ---- Animated push (called from clock exit transition) ----
void detail_face_push_animated(const WeatherLocationForecast *forecast) {
  if (s_df) return;
  s_df = calloc(1, sizeof(DetailFaceData));
  if (!s_df) return;

  if (forecast) {
    s_df->temp      = forecast->current_temp;
    s_df->high      = forecast->today_high;
    s_df->low       = forecast->today_low;
    s_df->uv        = forecast->today_uv;
    s_df->precip_mm = forecast->today_precip_mm;
    s_df->updated   = forecast->time_updated_utc;
    if (forecast->location_name)
      prv_format_location(forecast->location_name, s_df->location, sizeof(s_df->location));
    if (forecast->current_weather_phrase)
      strncpy(s_df->condition, forecast->current_weather_phrase, sizeof(s_df->condition) - 1);
  } else {
    s_df->temp      = WEATHER_SERVICE_LOCATION_FORECAST_UNKNOWN_TEMP;
    s_df->high      = WEATHER_SERVICE_LOCATION_FORECAST_UNKNOWN_TEMP;
    s_df->low       = WEATHER_SERVICE_LOCATION_FORECAST_UNKNOWN_TEMP;
    s_df->uv        = -1;
    s_df->precip_mm = -1;
    s_df->updated   = 0;
  }
  s_df->animated_entry = true;
  
  // Apply pending weekday animation if set
  if (s_pending_weekday_anim && s_pending_weekday[0]) {
    s_df->is_weekday_animation = true;
    strncpy(s_df->full_weekday_name, s_pending_weekday, sizeof(s_df->full_weekday_name) - 1);
    s_df->full_weekday_name[sizeof(s_df->full_weekday_name) - 1] = '\0';
    s_pending_weekday_anim = false;
    s_pending_weekday[0] = '\0';
  }

  s_df->window = window_create();
  window_set_window_handlers(s_df->window, (WindowHandlers){
    .load   = prv_window_load,
    .unload = prv_window_unload,
  });
  window_stack_push(s_df->window, false);
}
