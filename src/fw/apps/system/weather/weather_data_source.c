/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */
//! Implementation of the weather data-source adapter. This translation unit is
//! the only one that includes the firmware weather headers.
#include "weather_data_source.h"

#include "pbl/services/weather/weather_service.h"
#include "pbl/services/weather/weather_service_private.h"  // SerializedWeatherAppPrefs
#include "pbl/services/weather/weather_types.h"
#include "pbl/services/blob_db/weather_db.h"
#include "pbl/services/blob_db/watch_app_prefs_db.h"
#include "kernel/pbl_malloc.h"  // task_zalloc_check / task_free

#include <limits.h>
#include <string.h>

static void prv_copy_str(char *dst, size_t dst_size, const char *src) {
  if (!src) {
    dst[0] = '\0';
    return;
  }
  strncpy(dst, src, dst_size - 1);
  dst[dst_size - 1] = '\0';
}

static void prv_fill_from_fw(WxDsForecast *out, const WeatherLocationForecast *f, int index) {
  memset(out, 0, sizeof(*out));
  prv_copy_str(out->location_name, sizeof(out->location_name), f->location_name);
  prv_copy_str(out->short_phrase, sizeof(out->short_phrase), f->current_weather_phrase);
  out->is_current_location = (index == 0);
  out->current_temp = f->current_temp;
  out->today_high = f->today_high;
  out->today_low = f->today_low;
  out->current_weather_type = (uint8_t)f->current_weather_type;
  out->tomorrow_high = f->tomorrow_high;
  out->tomorrow_low = f->tomorrow_low;
  out->tomorrow_weather_type = (uint8_t)f->tomorrow_weather_type;
  // v4 metrics are not in weather_service's v3 forecast; prv_overlay_v4 reads
  // them from the v4 WeatherDBEntry directly (no-op for v3 records).
  out->today_uv = -1;
  out->today_precip = -1;
  out->today_wind = -1;
  out->latitude_e2 = INT16_MIN;
  out->longitude_e2 = INT16_MIN;
  out->time_updated_utc = (int32_t)f->time_updated_utc;
}

//! Overlay the v4-only fields (UV/precip/wind, lat-lon, 7-day daily, today's
//! hourly) onto *out by reading the active location's full WeatherDBEntry.
//! weather_service exposes only the v3 prefix + ordering; it stores the prefs
//! ordering index in node->id, so we map id -> prefs->locations[id] -> the
//! record key and read it directly. No-op (graceful degradation) when the
//! record is still a v3 entry written by a pre-v4 phone.
static void prv_overlay_v4(WxDsForecast *out, int location_id) {
  if (location_id < 0) {
    return;
  }
  SerializedWeatherAppPrefs *prefs = watch_app_prefs_get_weather();
  if (!prefs) {
    return;
  }
  if ((size_t)location_id >= prefs->num_locations) {
    watch_app_prefs_destroy_weather(prefs);
    return;
  }
  WeatherDBKey key = prefs->locations[location_id];
  watch_app_prefs_destroy_weather(prefs);

  const int len = weather_db_get_len((uint8_t *)&key, sizeof(key));
  if (len < (int)sizeof(WeatherDBEntryV3)) {
    return;
  }
  WeatherDBEntry *entry = task_zalloc_check(len);
  const status_t rv =
      weather_db_read((uint8_t *)&key, sizeof(key), (uint8_t *)entry, len);
  if ((rv == S_SUCCESS) && (entry->version == WEATHER_DB_CURRENT_VERSION)) {
    out->is_v4 = true;
    if (entry->today_uv_index_x10 >= 0) {
      out->today_uv = entry->today_uv_index_x10 / 10;  // schema stores UV*10
    }
    if (entry->today_precip_probability >= 0) {
      out->today_precip = entry->today_precip_probability;
    }
    if (entry->today_wind_speed > 0) {
      out->today_wind = entry->today_wind_speed;
    }
    out->latitude_e2 = entry->latitude_e2;
    out->longitude_e2 = entry->longitude_e2;

    uint8_t nd = entry->num_daily;
    if (nd > WX_DS_DAYS) {
      nd = WX_DS_DAYS;
    }
    out->num_daily = nd;
    for (uint8_t i = 0; i < nd; i++) {
      out->daily[i].high = entry->daily[i].high_temp;
      out->daily[i].low = entry->daily[i].low_temp;
      out->daily[i].type = entry->daily[i].weather_type;
      out->daily[i].precip = -1;  // v4 schema has no per-day precip yet
    }
    if (entry->today_hourly_count == WEATHER_DB_HOURLY_COUNT) {
      out->hourly_count = WX_DS_HOURLY;
      memcpy(out->hourly_type, entry->today_hourly_weather_type, WX_DS_HOURLY);
      memcpy(out->hourly_temp, entry->today_hourly_temp, WX_DS_HOURLY);
    }
  }
  task_free(entry);
}

// ===========================================================================
// Placeholder forecast data, served until the phone syncs real v4 weather records.
// Synthesizes a v4-shaped dataset (7-day daily[] + 24h hourly) from the real
// current conditions so the full 7-day touch-scrub + clock dial can be verified
// on-device before v4 data exists. Applied only when the active record is NOT
// already v4, so it vanishes automatically the moment real v4 records arrive.
// Set to 0 (or delete this block + its call site) to disable.
// ===========================================================================
#define WEATHER_V4_TEST_SEED 1
#if WEATHER_V4_TEST_SEED
static void prv_seed_v4_test(WxDsForecast *out) {
  if (out->is_v4) {
    return;  // real v4 data present — never overlay placeholder data on top of it
  }
  int hi = (out->today_high != WX_DS_UNKNOWN_TEMP) ? out->today_high : 21;
  int lo = (out->today_low != WX_DS_UNKNOWN_TEMP) ? out->today_low : 13;
  if (hi <= lo) {
    hi = lo + 6;
  }
  const uint8_t t0 = (out->current_weather_type <= 8) ? out->current_weather_type
                                                      : (uint8_t)WeatherType_Sun;

  // Today's extended metrics, so the detail view shows UV/precip/wind.
  if (out->today_uv < 0)     out->today_uv = 5;
  if (out->today_precip < 0) out->today_precip = 20;
  if (out->today_wind < 0)   out->today_wind = 12;

  // Coordinates so the globe has a location to reveal to (San Francisco).
  if (out->latitude_e2 == INT16_MIN)  out->latitude_e2 = 3777;    // 37.77 N
  if (out->longitude_e2 == INT16_MIN) out->longitude_e2 = -12242; // 122.42 W

  static const uint8_t kTypes[WX_DS_DAYS] = {
    WeatherType_Sun, WeatherType_PartlyCloudy, WeatherType_CloudyDay,
    WeatherType_LightRain, WeatherType_HeavyRain, WeatherType_PartlyCloudy,
    WeatherType_Sun,
  };
  // High-contrast spread (test only): fan days = 25/13, 30/16, 20/10, 28/15, 23/12.
  static const int kHiDelta[WX_DS_DAYS] = { 0, -3,  2, -8,  0, -5, -1 };
  static const int kLoDelta[WX_DS_DAYS] = { 0, -5, -2, -8, -3, -6, -1 };
  // Per-day precip % — test seed only (real v4 is today-only → future days = -1).
  static const int kPrecip[WX_DS_DAYS] = { 20, 10, 15, 55, 80, 25, 5 };

  out->num_daily = WX_DS_DAYS;
  out->daily[0].high = hi;
  out->daily[0].low = lo;
  out->daily[0].type = t0;
  out->daily[0].precip = out->today_precip;  // today's value (set above)
  for (int i = 1; i < WX_DS_DAYS; i++) {
    out->daily[i].high = hi + kHiDelta[i];
    out->daily[i].low = lo + kLoDelta[i];
    out->daily[i].type = kTypes[i];
    out->daily[i].precip = kPrecip[i];
  }
  // Don't synthesize today/tomorrow: daily[0] already used the real current conditions above, and
  // daily[1] uses the real next-day forecast from the blobDB when present. Only days 2+ stay
  // synthesized until the phone ships real multi-day (v4) records.
  if (out->tomorrow_high != WX_DS_UNKNOWN_TEMP && out->tomorrow_low != WX_DS_UNKNOWN_TEMP) {
    out->daily[1].high   = out->tomorrow_high;
    out->daily[1].low    = out->tomorrow_low;
    out->daily[1].type   = (out->tomorrow_weather_type <= WeatherType_RainAndSnow)
                           ? out->tomorrow_weather_type : t0;
    out->daily[1].precip = -1;  // v3 carries no tomorrow precip
  }

  // Diurnal hourly curve (0..100): cool overnight, peak mid-afternoon.
  static const uint8_t kDiurnal[WX_DS_HOURLY] = {
     10,  6,  3,  0,  0,  3,  8, 16, 28, 42, 56, 70,
     82, 92, 98, 100, 96, 88, 76, 62, 48, 36, 26, 17,
  };
  out->hourly_count = WX_DS_HOURLY;
  const int span = hi - lo;
  for (int h = 0; h < WX_DS_HOURLY; h++) {
    out->hourly_type[h] = t0;
    out->hourly_temp[h] = (int8_t)(lo + (span * kDiurnal[h]) / 100);
  }
}
#endif  // WEATHER_V4_TEST_SEED

#if defined(CONFIG_SOC_QEMU)
// QEMU has no phone → no weather_db records, so the weather app would be empty.
// Synthesize one location (current conditions + the v4 test seed) so the whole UI
// incl. the round 5-day screen is reachable for visual testing in the emulator.
// Auto-disabled on real hardware (CONFIG_SOC_QEMU is unset there).
static void prv_qemu_synth(WxDsForecast *out) {
  *out = (WxDsForecast){0};
  const char *loc = "Mexico City, Mexico";
  for (size_t li = 0; loc[li] && li < sizeof(out->location_name) - 1; li++) {
    out->location_name[li] = loc[li];  // struct is zeroed → already null-terminated
  }
  const char *ph = "Heavy Snow";  // day-0 condition uses the record's short_phrase
  for (size_t pi = 0; ph[pi] && pi < sizeof(out->short_phrase) - 1; pi++) {
    out->short_phrase[pi] = ph[pi];
  }
  out->is_current_location = true;
  out->current_temp = 23;
  out->current_weather_type = WeatherType_HeavySnow;  // QEMU: test heavy-snow anim
  out->today_high = 28;
  out->today_low = 18;
  out->tomorrow_high = 24;          // QEMU has no phone/blobDB → stand-in "real" tomorrow
  out->tomorrow_low = 14;
  out->tomorrow_weather_type = WeatherType_CloudyDay;
  out->today_uv = -1;
  out->today_precip = -1;
  out->today_wind = -1;
  out->latitude_e2 = INT16_MIN;
  out->longitude_e2 = INT16_MIN;
  out->is_v4 = false;
#if WEATHER_V4_TEST_SEED
  prv_seed_v4_test(out);
#endif
}
#endif

bool weather_ds_supported(void) {
#if defined(CONFIG_SOC_QEMU)
  return true;
#else
  return weather_service_supported_by_phone();
#endif
}

int weather_ds_location_count(void) {
#if defined(CONFIG_SOC_QEMU)
  return 1;  // synthesized QEMU location
#else
  if (!weather_service_supported_by_phone()) {
    return 0;
  }
  size_t count = 0;
  WeatherDataListNode *head = weather_service_locations_list_create(&count);
  weather_service_locations_list_destroy(head);
  return (int)count;
#endif
}

bool weather_ds_read_index(int index, WxDsForecast *out) {
  if (!out || index < 0) {
    return false;
  }
  size_t count = 0;
  WeatherDataListNode *head = weather_service_locations_list_create(&count);
  if (head && (size_t)index < count) {
    WeatherDataListNode *node =
        weather_service_locations_list_get_location_at_index(head, (unsigned int)index);
    if (node) {
      prv_fill_from_fw(out, &node->forecast, index);
      prv_overlay_v4(out, (int)node->id);  // v4 extras; no-op for v3 records
#if WEATHER_V4_TEST_SEED
      prv_seed_v4_test(out);  // Placeholder 7-day + hourly data until real v4 records arrive.
#endif
      weather_service_locations_list_destroy(head);
      return true;
    }
  }
  weather_service_locations_list_destroy(head);
#if defined(CONFIG_SOC_QEMU)
  if (index == 0) {
    prv_qemu_synth(out);  // no phone in QEMU → synthesize so the UI is reachable
    return true;
  }
#endif
  return false;
}
