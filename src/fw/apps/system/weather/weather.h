/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include "process_management/pebble_process_md.h"

const PebbleProcessMd* weather_app_get_info();

// Carousel ring navigation, called by the child screens (7-day list, clock, globe) on a
// horizontal swipe. dir = +1 (swipe left / next page), -1 (swipe right / previous page).
// Pages wrap: main -> 7-day -> clock -> globe -> main.
void weather_carousel_navigate(int dir);
