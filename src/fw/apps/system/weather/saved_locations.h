/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include "pebble_compat.h"
#include "city_presets.h"
#include "weather_platform.h"

#define SAVED_LOCATIONS_MAX_CUSTOM 6
#define SAVED_LOCATIONS_DEFAULT_PRESET_COUNT 5
#define SAVED_LOCATIONS_MAX_ENTRIES (1 + SAVED_LOCATIONS_DEFAULT_PRESET_COUNT + SAVED_LOCATIONS_MAX_CUSTOM)
#define SAVED_LOCATION_LABEL_SIZE 48
#define SAVED_LOCATION_QUERY_SIZE 64

typedef enum {
  SavedLocationKindCurrent = 0,
  SavedLocationKindPreset,
  SavedLocationKindCustom,
} SavedLocationKind;

typedef struct {
  SavedLocationKind kind;
  int preset_index;
  char label[SAVED_LOCATION_LABEL_SIZE];
  char query[SAVED_LOCATION_QUERY_SIZE];
  int16_t latitude_e2;
  int16_t longitude_e2;
  bool has_coordinates;
} SavedLocationEntry;

typedef void (*SavedLocationsSelectCallback)(SavedLocationKind kind,
                                             int preset_index,
                                             const char *query,
                                             void *context);

typedef struct {
  const char *current_location_label;
  int active_city_index;
  const char *active_custom_query;
  SavedLocationsSelectCallback select_callback;
  void *select_context;
} SavedLocationsConfig;

void saved_locations_push(const SavedLocationsConfig *config);
void saved_locations_dismiss(bool animated);
bool saved_locations_is_showing(void);

void saved_locations_add_custom_location(const char *query, const char *label);
void saved_locations_update_custom_label(const char *query, const char *label);
void saved_locations_update_custom_details(const char *query,
                                           const char *label,
                                           int16_t latitude_e2,
                                           int16_t longitude_e2);
int saved_locations_get_entries(SavedLocationEntry *entries,
                                int max_entries,
                                const char *current_location_label,
                                int16_t current_latitude_e2,
                                int16_t current_longitude_e2,
                                bool has_current_location);
