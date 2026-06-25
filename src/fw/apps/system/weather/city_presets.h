/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include "pebble_compat.h"

#define CITY_PRESET_COUNT 12

typedef struct CityPreset {
  const char *city;
  const char *country;
  int16_t latitude_e2;
  int16_t longitude_e2;
} CityPreset;

const CityPreset *city_presets_get(int index);
void city_presets_format_label(int index, char *buffer, size_t buffer_size);
