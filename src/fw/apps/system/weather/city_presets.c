/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "city_presets.h"

static const CityPreset s_city_presets[CITY_PRESET_COUNT] = {
  { "New York", "United States", 4071, -7401 },
  { "Toronto", "Canada", 4365, -7938 },
  { "Rio de Janeiro", "Brazil", -2291, -4317 },
  { "Reykjavik", "Iceland", 6415, -2194 },
  { "London", "United Kingdom", 5151, -13 },
  { "Paris", "France", 4886, 235 },
  { "Cape Town", "South Africa", -3392, 1842 },
  { "Dubai", "United Arab Emirates", 2520, 5527 },
  { "Mumbai", "India", 1908, 7288 },
  { "Singapore", "Singapore", 135, 10382 },
  { "Tokyo", "Japan", 3568, 13969 },
  { "Sydney", "Australia", -3387, 15121 },
};

const CityPreset *city_presets_get(int index) {
  if (index < 0 || index >= CITY_PRESET_COUNT) return NULL;
  return &s_city_presets[index];
}

void city_presets_format_label(int index, char *buffer, size_t buffer_size) {
  if (!buffer || buffer_size == 0) return;

  const CityPreset *preset = city_presets_get(index);
  if (!preset) {
    buffer[0] = '\0';
    return;
  }

  snprintf(buffer, buffer_size, "%s, %s", preset->city, preset->country);
}
