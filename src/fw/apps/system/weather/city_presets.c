/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "city_presets.h"

static const CityPreset s_city_presets[CITY_PRESET_COUNT] = {
  { "New York", "United States", 4071, -7401 },
  { "", "", 0, 0 },   // (non-default preset slots emptied — never listable;
  { "", "", 0, 0 },   //  indices must stay stable for the deleted-preset persist mask)
  { "", "", 0, 0 },
  { "London", "United Kingdom", 5151, -13 },
  { "Paris", "France", 4886, 235 },
  { "", "", 0, 0 },
  { "", "", 0, 0 },
  { "", "", 0, 0 },
  { "", "", 0, 0 },
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
