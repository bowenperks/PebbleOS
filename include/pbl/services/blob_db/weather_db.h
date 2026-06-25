/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include "pbl/services/weather/weather_service.h"
#include "pbl/services/weather/weather_types.h"
#include "system/status_codes.h"
#include "util/attributes.h"
#include "util/pstring.h"
#include "util/time/time.h"
#include "util/uuid.h"

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

// ---------------------------------------------------------------------------
// Weather BlobDB schema version
//
// v3 = legacy schema (current/today/tomorrow only). See WeatherDBEntryV3.
// v4 = rich schema for the full Weather app. Appends today's extended metrics
//      (feels-like, UV, precip probability, wind), the location's coordinates
//      (for the globe), a multi-day daily forecast array, and today's hourly
//      type+temp series. All v4 fixed fields are appended AFTER the v3 prefix
//      (whose offsets are preserved byte-for-byte) and BEFORE the trailing
//      variable-length pstring16s, so a v3-era reader still finds the v3 fields
//      and the version byte is the single source of compatibility truth.
//
// The phone only writes v4 records when the firmware advertises
// `weather_db_v4_support` (see session_remote_version.h / system_versions.c);
// otherwise it keeps writing v3. The firmware parses BOTH during rollout.
// ---------------------------------------------------------------------------
#define WEATHER_DB_CURRENT_VERSION (4)
#define WEATHER_DB_CURRENT_MINOR_VERSION (0)
#define WEATHER_DB_LEGACY_VERSION (3)

// Days of daily forecast a v4 record carries (today + 6).
#define WEATHER_DB_MAX_FORECAST_DAYS (7)
// Hours of hourly data a v4 record carries (today only — keeps the record small).
#define WEATHER_DB_HOURLY_COUNT (24)

typedef Uuid WeatherDBKey;

// ---------------------------------------------------------------------------
// Legacy v3 record. Kept verbatim so the firmware can still read records
// written by an older mobile app during the rollout window. Do not change.
// ---------------------------------------------------------------------------
typedef struct PACKED {
  uint8_t version;
  int16_t current_temp;
  WeatherType current_weather_type;
  int16_t today_high_temp;
  int16_t today_low_temp;
  WeatherType tomorrow_weather_type;
  int16_t tomorrow_high_temp;
  int16_t tomorrow_low_temp;
  time_t last_update_time_utc;
  bool is_current_location;
  SerializedArray pstring16s;
} WeatherDBEntryV3;

// ---------------------------------------------------------------------------
// One day of daily forecast (v4+). Weather type stored as uint8_t (values map
// 1:1 to WeatherType; WeatherType_Unknown == 255). Cast on read.
// ---------------------------------------------------------------------------
typedef struct PACKED {
  int16_t high_temp;       // WEATHER_SERVICE_LOCATION_FORECAST_UNKNOWN_TEMP if unknown
  int16_t low_temp;        // WEATHER_SERVICE_LOCATION_FORECAST_UNKNOWN_TEMP if unknown
  uint8_t weather_type;    // WeatherType; 255 if unknown
} WeatherDBDailyForecast;

// ---------------------------------------------------------------------------
// v4 record. Layout: [ v3 fixed prefix, unchanged offsets ] + [ v4 fixed
// fields ] + [ trailing pstring16s ]. The trailing pstring array MUST be last.
// ---------------------------------------------------------------------------
typedef struct PACKED {
  // --- v3-compatible fixed prefix (identical offsets to WeatherDBEntryV3) ---
  uint8_t version;                 // == WEATHER_DB_CURRENT_VERSION (4)
  int16_t current_temp;
  WeatherType current_weather_type;
  int16_t today_high_temp;
  int16_t today_low_temp;
  WeatherType tomorrow_weather_type;
  int16_t tomorrow_high_temp;
  int16_t tomorrow_low_temp;
  time_t last_update_time_utc;
  bool is_current_location;

  // --- v4 additions (fixed-size, appended) ---
  uint8_t minor_version;            // == WEATHER_DB_CURRENT_MINOR_VERSION
  int16_t today_feels_like_temp;    // WEATHER_SERVICE_LOCATION_FORECAST_UNKNOWN_TEMP if unknown
  int16_t today_uv_index_x10;       // UV index * 10 (0..110), or -1 if unknown
  int16_t today_precip_probability; // 0..100 (%), or -1 if unknown
  uint16_t today_wind_speed;        // whole units (km/h or mph per phone), 0 if unknown
  uint16_t today_wind_direction;    // degrees 0..359, 0xFFFF if unknown
  int16_t latitude_e2;              // latitude * 100 (for the globe), INT16_MIN if unknown
  int16_t longitude_e2;             // longitude * 100 (for the globe), INT16_MIN if unknown
  uint8_t num_daily;                // valid entries in daily[] (0..WEATHER_DB_MAX_FORECAST_DAYS)
  WeatherDBDailyForecast daily[WEATHER_DB_MAX_FORECAST_DAYS];
  uint8_t today_hourly_count;       // 0 or WEATHER_DB_HOURLY_COUNT
  uint8_t today_hourly_weather_type[WEATHER_DB_HOURLY_COUNT]; // WeatherType per hour 0-23
  int8_t today_hourly_temp[WEATHER_DB_HOURLY_COUNT];          // temp per hour 0-23

  // --- variable-length trailing strings (MUST stay last) ---
  SerializedArray pstring16s;
} WeatherDBEntry;

typedef enum WeatherDbStringIndex {
  WeatherDbStringIndex_LocationName,
  WeatherDbStringIndex_ShortPhrase,
  WeatherDbStringIndexCount,
} WeatherDbStringIndex;

// Fixed portion of a v4 record up to and including the hourly arrays, i.e.
// everything except the trailing pstring16s SerializedArray header/payload.
#define WEATHER_DB_V4_FIXED_SIZE (offsetof(WeatherDBEntry, pstring16s))

// Smallest acceptable record is a legacy v3 record (smaller fixed prefix).
#define MIN_ENTRY_SIZE (sizeof(WeatherDBEntryV3))
#define MAX_ENTRY_SIZE (sizeof(WeatherDBEntry) + \
                        WEATHER_SERVICE_MAX_WEATHER_LOCATION_BUFFER_SIZE + \
                        WEATHER_SERVICE_MAX_SHORT_PHRASE_BUFFER_SIZE)

//! @return true if the firmware can parse a record stamped with this major version.
static inline bool weather_db_version_is_supported(uint8_t version) {
  return (version == WEATHER_DB_CURRENT_VERSION) || (version == WEATHER_DB_LEGACY_VERSION);
}

//! @return the byte offset of the trailing pstring16s array for a record of the
//! given version. v3 and v4 place it differently; the version byte decides.
static inline size_t weather_db_entry_strings_offset(uint8_t version) {
  return (version >= WEATHER_DB_CURRENT_VERSION) ? offsetof(WeatherDBEntry, pstring16s)
                                                 : offsetof(WeatherDBEntryV3, pstring16s);
}

//! @return a pointer to the trailing pstring16s array, located correctly for the
//! record's version. Use this instead of &entry->pstring16s so v3 records still
//! resolve their strings after the v4 fields were inserted before the array.
static inline SerializedArray *weather_db_entry_get_strings(WeatherDBEntry *entry) {
  return (SerializedArray *)((uint8_t *)entry + weather_db_entry_strings_offset(entry->version));
}

// Memory ownership: pointer to key and entry must not be saved, as they become invalid after
// the callback finishes
typedef void (*WeatherDBIteratorCallback)(WeatherDBKey *key, WeatherDBEntry *entry, void *context);

// ------------------------------------------------------------------------------------
// WeatherDB functions
status_t weather_db_for_each(WeatherDBIteratorCallback cb, void *context);

// ------------------------------------------------------------------------------------
// BlobDB Implementation

void weather_db_init(void);

status_t weather_db_flush(void);

status_t weather_db_compact(void);

status_t weather_db_insert(const uint8_t *key, int key_len, const uint8_t *val, int val_len);

int weather_db_get_len(const uint8_t *key, int key_len);

status_t weather_db_read(const uint8_t *key, int key_len, uint8_t *val_out, int val_out_len);

status_t weather_db_delete(const uint8_t *key, int key_len);
