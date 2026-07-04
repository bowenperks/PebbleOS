/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include "pebble_compat.h"
#include "weather_types.h"

// The "condensed view" (historically the app's main view): a large weather icon for the focused
// day on top, a small icon for the next day at the bottom. Hold UP/DOWN to scroll through the
// multi-day forecast (the weather_app_layout screen, hosted in its own window). Pushed by SELECT
// on the forecast list. `days` is BORROWED (not copied) — the caller (weather.c) owns it for the
// app's lifetime.
void condensed_view_push(const WeatherLocationForecast *days, size_t num_days, int start_day_index);

// Arm the SELECT expand-in (today bitmap squash-stretches in from the right) for the NEXT push.
void condensed_view_arm_select_in(void);

bool condensed_view_is_showing(void);

// Re-point the borrowed days array after a data refresh (clamps the focused day). No-op when
// the view isn't showing.
void condensed_view_update_data(const WeatherLocationForecast *days, size_t num_days);
