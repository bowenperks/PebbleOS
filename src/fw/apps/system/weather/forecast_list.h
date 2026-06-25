/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include "pebble_compat.h"
#include "weather_types.h"

// Push the condensed weekly forecast list onto the window stack.
// days/num_days are the same arrays maintained by weather.c.
// start_day_index: the day that was focused in the main view; list opens scrolled to it.
// animated: true for normal push, false for instant cut (used after transition anim).
// on_pop/on_pop_ctx: optional callback fired when the list window is dismissed.
void forecast_list_push(const WeatherLocationForecast *days, size_t num_days,
                        int start_day_index, bool animated,
                        void (*on_pop)(void *ctx), void *on_pop_ctx,
                        void (*on_city_select)(void *ctx), void *on_city_select_ctx);

// Dismiss the condensed list if it is currently on the stack.
void forecast_list_dismiss(bool animated);

bool forecast_list_is_showing(void);

// Call whenever new forecast data arrives so the list stays current.
// No-op if the list window is not currently showing.
void forecast_list_update_data(const WeatherLocationForecast *days, size_t num_days);
