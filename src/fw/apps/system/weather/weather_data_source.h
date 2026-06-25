/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */
//! Data-source adapter: the ONE place that touches the firmware weather types
//! (WeatherType / WeatherLocationForecast / WeatherDBEntry). It exposes a
//! neutral snapshot struct so weather.c and the views can keep the app's own
//! WeatherType / WeatherLocationForecast without a name collision.
//!
//! Milestone-1 reads the v3 fields via weather_service (temp/high/low/type/
//! phrase/location). Milestone-2 will read the active record's v4 WeatherDBEntry
//! directly for UV/precip/wind/daily[7]/hourly/lat-lon (fields already present
//! in weather_db.h but absent from weather_service's v3 WeatherLocationForecast).
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define WX_DS_HOURLY 24
#define WX_DS_DAYS   7
#define WX_DS_UNKNOWN_TEMP 32767  // matches WEATHER_SERVICE_..._UNKNOWN_TEMP (INT16_MAX) + app sentinel

//! One day of the multi-day forecast (v4 daily[] array). daily[0] == today.
typedef struct {
  int high;        // WX_DS_UNKNOWN_TEMP if unknown
  int low;         // WX_DS_UNKNOWN_TEMP if unknown
  uint8_t type;    // WeatherType (app + fw enum values are identical)
  int precip;      // % rain chance, -1 if unknown (real v4 schema is today-only)
} WxDsDaily;

typedef struct {
  char location_name[64];
  char short_phrase[32];
  bool is_current_location;
  int current_temp;        // WX_DS_UNKNOWN_TEMP if unknown
  int today_high;
  int today_low;
  uint8_t current_weather_type;  // 0-8 / 255 (app + fw enum values are identical)
  int tomorrow_high;
  int tomorrow_low;
  uint8_t tomorrow_weather_type;
  int today_uv;            // UV index 0-11, -1 unknown (v4)
  int today_precip;        // % , -1 unknown (v4)
  int today_wind;          // mph, -1 unknown (v4)
  int32_t time_updated_utc;

  // ---- v4-only extras (zeroed/unknown when the record is still v3) ----
  bool is_v4;              // true if the active record was a v4 WeatherDBEntry
  uint8_t num_daily;       // valid entries in daily[] (0 = no v4 daily array)
  WxDsDaily daily[WX_DS_DAYS];   // daily[0] == today
  uint8_t hourly_count;    // 0 or WX_DS_HOURLY (today only, per the v4 schema)
  uint8_t hourly_type[WX_DS_HOURLY];  // WeatherType per hour 0-23
  int8_t  hourly_temp[WX_DS_HOURLY];  // temperature per hour 0-23
  int16_t latitude_e2;     // latitude * 100 (for the globe), INT16_MIN unknown
  int16_t longitude_e2;    // longitude * 100 (for the globe), INT16_MIN unknown
} WxDsForecast;

//! @return true if the phone supports/provides weather (gates app visibility).
bool weather_ds_supported(void);

//! Number of configured weather locations (current + saved), 0 if none.
int weather_ds_location_count(void);

//! Fill *out with the forecast for the location at `index` (0-based, ordered as
//! weather_service orders them; index 0 is treated as the current location).
//! @return true on success, false if the index/location is unavailable.
bool weather_ds_read_index(int index, WxDsForecast *out);
