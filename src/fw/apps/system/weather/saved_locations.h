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
  int8_t preset_index;   // -1..CITY_PRESET_COUNT-1 (value-context only)
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
  int active_city_index;           // CityPreset index (NOT a weather-ds location index); -1 = none
  const char *active_custom_query;
  SavedLocationsSelectCallback select_callback;
  void *select_context;
} SavedLocationsConfig;

void saved_locations_push(const SavedLocationsConfig *config);

//! Re-send the watch->phone "add location" request (Pebble Protocol endpoint
//! 6100) for every dictated custom location still missing coordinates. Call
//! at app launch: delivery is best-effort, so this recovers requests lost
//! while the phone was disconnected. The phone treats ADD idempotently.
void saved_locations_send_pending_queries(void);

//! Free the app-heap custom-location cache at app exit (the statics survive the
//! app; the heap doesn't — see saved_locations_reset in the .c).
void saved_locations_reset(void);
int saved_locations_get_entries(SavedLocationEntry *entries,
                                int max_entries,
                                const char *current_location_label,
                                int16_t current_latitude_e2,
                                int16_t current_longitude_e2,
                                bool has_current_location);
