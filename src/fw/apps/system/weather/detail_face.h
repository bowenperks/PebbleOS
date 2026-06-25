/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include "weather_types.h"

// Push the full-screen weather detail view onto the window stack.
// Tap (Emery) or back button dismisses it.
// forecast: pointer to the forecast to display; may be NULL.
void detail_face_push(const WeatherLocationForecast *forecast);
// Same as detail_face_push but plays an entry animation (for use after the clock exit transition).
void detail_face_push_animated(const WeatherLocationForecast *forecast);
// Set up weekday animation for the next animated push (must be called before detail_face_push_animated).
// full_weekday_name: e.g. "WEDNESDAY" (abbreviation from forecast->location_name will be shown first).
void detail_face_set_weekday_animation(const char *full_weekday_name);
