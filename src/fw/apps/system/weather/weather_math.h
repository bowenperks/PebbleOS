/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include "pebble_compat.h"

uint32_t weather_scale_u32(uint32_t value, uint32_t numerator,
                           uint32_t denominator);
int32_t weather_scale_i32(int32_t value, int32_t numerator,
                          int32_t denominator);
int32_t weather_isqrt(int32_t value);
int32_t weather_norm_square(int32_t value);
int32_t weather_norm_bell(int32_t value);
void weather_capture_framebuffer_rect(GBitmap *fb, GBitmap *dst,
                                      GRect src_rect, int dst_x);

#define WEATHER_GLOW_WRAP_SPIN_TICKS 12
#define WEATHER_GLOW_WRAP_CLOSE_TICKS 4
#define WEATHER_GLOW_WRAP_TICKS \
  (WEATHER_GLOW_WRAP_SPIN_TICKS + WEATHER_GLOW_WRAP_CLOSE_TICKS)

void weather_draw_lava_ring(GContext *ctx, GPoint center, int outer_r,
                            GColor glow_color, uint32_t phase,
                            uint8_t idle_progress);
