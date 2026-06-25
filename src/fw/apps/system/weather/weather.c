/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

//! Weather system app — the rich ported UI, fed by the weather BlobDB.
//!
//! The full touch-gesture system is ported from the standalone app's
//! original SDK orchestrator. The main screen renders the active location's
//! forecast from the BlobDB; touch and buttons both drive it:
//!   - vertical drag  → scrub/flick between forecast days
//!   - pull down (day 0) → pull-to-refresh (re-reads the BlobDB)
//!   - swipe left / SELECT / tap icon → smooth clock-face transition
//!   - swipe right / UP-at-day-0 → condensed forecast list
//!   - UP/DOWN buttons (with hold-to-accelerate) → browse days
//! Multi-day data is today + tomorrow from a v3 record (the full 7-day + hourly
//! arrive once the phone writes v4 records and we switch to the direct v4 read,
//! which also wires city switching from the forecast list).
//! No AppMessage — data comes from weather_db + a PEBBLE_WEATHER_EVENT
//! subscription; the touch service is the firmware-internal applib service.

#include "weather.h"
#include "weather_app_layout.h"
#include "clock_face.h"
#include "forecast_list.h"
#include "globe_view.h"
#include "saved_locations.h"
#include "weather_types.h"
#include "weather_math.h"
#include "weather_data_source.h"

#include "pebble_compat.h"
#include "pbl/services/timeline/timeline.h"  // UUID_WEATHER_DATA_SOURCE

#define WX_MAX_DAYS 7

typedef enum {
  MainTouchMode_None = 0,
  MainTouchMode_Pull,
  MainTouchMode_Day,
  MainTouchMode_Clock,
} MainTouchMode;

typedef struct WeatherAppData {
  Window window;
  WeatherAppLayout layout;
  GlobeView *globe_view;
  EventServiceInfo weather_event_info;

  int location_count;
  int active_index;          // which configured location is shown
  unsigned int current_day_index;
  size_t days_received;

  WeatherLocationForecast days[WX_MAX_DAYS];
  char location_buf[64];
  char phrase_buf[WX_MAX_DAYS][32];
  char label_buf[WX_MAX_DAYS][16];

  // Today's hourly series (v4; clock dial glyphs + Select temperature reveal).
  // The v4 record carries hourly for TODAY only, so this applies to day 0.
  uint8_t hourly_type[24];
  int8_t  hourly_temp[24];
  bool    hourly_valid;
  // Active location coordinates (v4; for the 3D globe).
  int16_t latitude_e2;
  int16_t longitude_e2;

  // ---- Touch gesture state (ported from the original orchestrator) ----
  int16_t touch_start_x;   // X coordinate of the most recent Touchdown event
  int16_t touch_start_y;   // Y coordinate of the most recent Touchdown event
  bool touch_active;       // true while a finger is down
  bool pull_in_progress;   // true while tracking a downward pull gesture
  bool touch_axis_locked;
  MainTouchMode touch_mode;
  int touch_start_day_index;
  int16_t touch_day_anchor_y;
  int touch_day_anchor_index;
  time_t touch_start_time_s;
  uint16_t touch_start_time_ms;
  int touch_day_update_count;
  bool touch_day_preview_started;
  int preview_day_index;
  int preview_old_day_index;
  AnimationProgress preview_progress;
  bool pending_return_anim;  // fire the globe->main fly-in when main next appears
} WeatherAppData;

static WeatherAppData *s_data;

static void prv_navigate(WeatherAppData *data, bool is_down);
static void prv_refresh(WeatherAppData *data);
static void prv_on_clock_transition_done(void *ctx);
static void prv_on_list_transition_done(void *ctx);
static void prv_on_city_select_requested(void *ctx);

// ---- Data: fill days[] from the active location's BlobDB record ----

// Derive a short condition phrase from the weather type. v4 records don't store
// per-day phrases (the phone used to send them via AppMessage), so future days
// get a derived phrase; day 0 keeps the record's real short_phrase.
static const char *prv_phrase_for_type(uint8_t type) {
  switch ((WeatherType)type) {
    case WeatherType_Sun:          return "Sunny";
    case WeatherType_PartlyCloudy: return "Partly Cloudy";
    case WeatherType_CloudyDay:    return "Cloudy";
    case WeatherType_LightRain:    return "Light Rain";
    case WeatherType_HeavyRain:    return "Rain";
    case WeatherType_LightSnow:    return "Light Snow";
    case WeatherType_HeavySnow:    return "Snow";
    case WeatherType_RainAndSnow:  return "Rain & Snow";
    case WeatherType_Generic:
    case WeatherType_Unknown:
    default:                       return "";
  }
}

// Derive the day label from the date: day 0 = "Today", day 1 = "Tomorrow",
// day 2+ = weekday abbreviation (v4 records don't carry labels).
static void prv_day_label(int day_offset, char *buf, size_t bufsize) {
  if (bufsize == 0) {
    return;
  }
  if (day_offset == 0) {
    strncpy(buf, "Today", bufsize - 1);
  } else if (day_offset == 1) {
    strncpy(buf, "Tomorrow", bufsize - 1);
  } else {
    static const char *const kWday[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    time_t t = rtc_get_time() + (time_t)day_offset * SECONDS_PER_DAY;
    struct tm *lt = localtime(&t);  // compat maps to pbl_override_localtime
    int w = (lt && lt->tm_wday >= 0 && lt->tm_wday < 7) ? lt->tm_wday : 0;
    strncpy(buf, kWday[w], bufsize - 1);
  }
  buf[bufsize - 1] = '\0';
}

static void prv_fill_days_from_ds(WeatherAppData *data, const WxDsForecast *ds) {
  strncpy(data->location_buf, ds->location_name, sizeof(data->location_buf) - 1);
  data->location_buf[sizeof(data->location_buf) - 1] = '\0';

  // Today's hourly series + coordinates (v4 only; otherwise left invalid).
  data->hourly_valid = (ds->hourly_count == 24);
  if (data->hourly_valid) {
    memcpy(data->hourly_type, ds->hourly_type, sizeof(data->hourly_type));
    memcpy(data->hourly_temp, ds->hourly_temp, sizeof(data->hourly_temp));
  }
  data->latitude_e2 = ds->latitude_e2;
  data->longitude_e2 = ds->longitude_e2;

  // Day 0 = today (full current metrics; v4 fills UV/precip/wind, else -1).
  snprintf(data->phrase_buf[0], sizeof(data->phrase_buf[0]), "%s", ds->short_phrase);
  prv_day_label(0, data->label_buf[0], sizeof(data->label_buf[0]));
  data->days[0] = (WeatherLocationForecast) {
    .location_name = data->location_buf,
    .is_current_location = ds->is_current_location,
    .current_temp = ds->current_temp,
    .today_high = ds->today_high,
    .today_low = ds->today_low,
    .today_uv = ds->today_uv,
    .today_precip_mm = ds->today_precip,
    .today_wind_mph = ds->today_wind,
    .current_weather_type = (WeatherType)ds->current_weather_type,
    .current_weather_phrase = data->phrase_buf[0],
    .label = data->label_buf[0],
    .time_updated_utc = ds->time_updated_utc,
  };
  data->days_received = 1;

  if (ds->num_daily >= 2) {
    // v4: days 1..N-1 from the daily[] array (high/low/type; current_temp and
    // the today-only metrics don't apply to future days).
    size_t nd = ds->num_daily;
    if (nd > WX_MAX_DAYS) {
      nd = WX_MAX_DAYS;
    }
    for (size_t i = 1; i < nd; i++) {
      prv_day_label((int)i, data->label_buf[i], sizeof(data->label_buf[i]));
      snprintf(data->phrase_buf[i], sizeof(data->phrase_buf[i]), "%s",
               prv_phrase_for_type(ds->daily[i].type));
      data->days[i] = (WeatherLocationForecast) {
        .location_name = data->location_buf,
        .is_current_location = ds->is_current_location,
        .current_temp = WX_DS_UNKNOWN_TEMP,
        .today_high = ds->daily[i].high,
        .today_low = ds->daily[i].low,
        .today_uv = -1,
        .today_precip_mm = ds->daily[i].precip,  // per-day precip (seed; -1 for real v4)
        .today_wind_mph = -1,
        .current_weather_type = (WeatherType)ds->daily[i].type,
        .current_weather_phrase = data->phrase_buf[i],
        .label = data->label_buf[i],
        .time_updated_utc = ds->time_updated_utc,
      };
    }
    data->days_received = nd;
  } else {
    // v3 fallback: day 1 = tomorrow from the v3 prefix fields (high/low/type).
    const bool have_tomorrow = (ds->tomorrow_high != WX_DS_UNKNOWN_TEMP) ||
                               (ds->tomorrow_low != WX_DS_UNKNOWN_TEMP);
    if (have_tomorrow) {
      prv_day_label(1, data->label_buf[1], sizeof(data->label_buf[1]));
      snprintf(data->phrase_buf[1], sizeof(data->phrase_buf[1]), "%s",
               prv_phrase_for_type(ds->tomorrow_weather_type));
      data->days[1] = (WeatherLocationForecast) {
        .location_name = data->location_buf,
        .is_current_location = ds->is_current_location,
        .current_temp = WX_DS_UNKNOWN_TEMP,
        .today_high = ds->tomorrow_high,
        .today_low = ds->tomorrow_low,
        .today_uv = -1,
        .today_precip_mm = -1,
        .today_wind_mph = -1,
        .current_weather_type = (WeatherType)ds->tomorrow_weather_type,
        .current_weather_phrase = data->phrase_buf[1],
        .label = data->label_buf[1],
        .time_updated_utc = ds->time_updated_utc,
      };
      data->days_received = 2;
    }
  }
}

static void prv_update_display(WeatherAppData *data) {
  unsigned int i = data->current_day_index;
  if (i >= data->days_received) {
    i = data->current_day_index = 0;
  }
  const WeatherLocationForecast *today = &data->days[i];
  const WeatherLocationForecast *next =
      (i + 1 < data->days_received) ? &data->days[i + 1] : NULL;
  weather_app_layout_set_fin_allowed(&data->layout,
                                     data->days_received > 1
                                     && i + 1 >= data->days_received);
  weather_app_layout_set_location(&data->layout, data->location_buf);
  weather_app_layout_set_data(&data->layout, today, next);
  weather_app_layout_set_down_arrow_visible(&data->layout, (i + 1 < data->days_received));
}

static void prv_refresh(WeatherAppData *data) {
  data->location_count = weather_ds_location_count();
  if (data->location_count <= 0) {
    data->days_received = 0;
    weather_app_layout_set_down_arrow_visible(&data->layout, false);
    weather_app_layout_set_data(&data->layout, NULL, NULL);
    return;
  }
  if (data->active_index >= data->location_count) {
    data->active_index = 0;
  }
  WxDsForecast ds;
  if (weather_ds_read_index(data->active_index, &ds)) {
    prv_fill_days_from_ds(data, &ds);
    if (data->current_day_index >= data->days_received) {
      data->current_day_index = 0;
    }
    prv_update_display(data);
  }
}

// ---- Navigation ----

// Carousel ring: the four screens are pages on a wrap-around ring. The window stack stays
// shallow — main is the persistent base window, and at most one of {list, clock, globe} sits
// on top of it. weather_carousel_navigate() tears the current top down and brings the target
// up. Order: main(0) -> 7-day list(1) -> clock(2) -> globe(3) -> main, swipe-left = +1 (next),
// swipe-right = -1 (prev).
typedef enum { PAGE_MAIN = 0, PAGE_LIST, PAGE_CLOCK, PAGE_GLOBE, PAGE_COUNT } WeatherCarouselPage;
static WeatherCarouselPage s_page = PAGE_MAIN;

static void prv_navigate(WeatherAppData *data, bool is_down) {
  if (is_down) {
    if (data->current_day_index + 1 >= data->days_received) {
      return;
    }
    data->current_day_index++;
  } else {
    if (data->current_day_index == 0) {
      return;
    }
    data->current_day_index--;
  }
  unsigned int i = data->current_day_index;
  const WeatherLocationForecast *new_today = &data->days[i];
  const WeatherLocationForecast *new_next =
      (i + 1 < data->days_received) ? &data->days[i + 1] : NULL;
  weather_app_layout_set_fin_allowed(&data->layout,
                                     data->days_received > 1
                                     && i + 1 >= data->days_received);
  weather_app_layout_animate(&data->layout, new_today, new_next, is_down);
  weather_app_layout_set_down_arrow_visible(&data->layout, (i + 1 < data->days_received));
}

// ---- Sub-view transitions ----

// Push the clock face for the current day. static_push=true skips the clock's
// spiral intro (a plain appearance — used arriving from the globe); false plays
// the spiral intro (used after the main->clock icon shrink).
static void prv_push_clock(WeatherAppData *data, bool static_push) {
  unsigned int di = data->current_day_index;
  if (di >= WX_MAX_DAYS) {
    di = 0;
  }
  // Build the day's 24h hourly series so the clock dial + tap-to-reveal work for
  // EVERY day. Day 0 uses the real record's hourly when present; every other day
  // is synthesized from that day's high/low + type (a placeholder until the v4 struct is
  // extended to carry per-day hourly for the phone to populate — today is the only
  // day the record carries hourly for today).
  static uint8_t s_hourly_type[24];
  static int8_t  s_hourly_temp[24];
  const uint8_t *hourly_types = NULL;
  bool have_hourly = false;

  if (di == 0 && data->hourly_valid) {
    memcpy(s_hourly_type, data->hourly_type, sizeof(s_hourly_type));
    memcpy(s_hourly_temp, data->hourly_temp, sizeof(s_hourly_temp));
    hourly_types = s_hourly_type;
    have_hourly = true;
  } else if (di < data->days_received) {
    const WeatherLocationForecast *d = &data->days[di];
    int hi = (d->today_high != WX_DS_UNKNOWN_TEMP) ? d->today_high : 20;
    int lo = (d->today_low  != WX_DS_UNKNOWN_TEMP) ? d->today_low  : 12;
    if (hi <= lo) {
      hi = lo + 6;
    }
    // Diurnal curve (0..100): cool overnight, peak mid-afternoon — same shape as
    // the today seed so every day's dial reads naturally.
    static const uint8_t kDiurnal[24] = {
       10,  6,  3,  0,  0,  3,  8, 16, 28, 42, 56, 70,
       82, 92, 98, 100, 96, 88, 76, 62, 48, 36, 26, 17,
    };
    const int span = hi - lo;
    for (int h = 0; h < 24; h++) {
      s_hourly_type[h] = d->current_weather_type;
      s_hourly_temp[h] = (int8_t)(lo + (span * kDiurnal[h]) / 100);
    }
    hourly_types = s_hourly_type;
    have_hourly = true;
  }

  if (static_push) {
    clock_face_push_static(data->days, data->days_received, (int)di,
                           hourly_types, have_hourly ? 24 : 0, false);
  } else {
    clock_face_push(data->days, data->days_received, (int)di,
                    hourly_types, have_hourly ? 24 : 0, false);
  }
  if (have_hourly) {
    clock_face_update_hourly_temps_for_day((int)di, s_hourly_temp, 24);
  }
}

static void prv_on_clock_transition_done(void *ctx) {
  prv_push_clock((WeatherAppData *)ctx, false);  // with spiral intro, after the shrink
}

static void prv_on_list_transition_done(void *ctx) {
  WeatherAppData *data = (WeatherAppData *)ctx;
  // Carousel: the 7-day list is one ring page sitting on top of the persistent main window.
  // No on-pop main-return animation — main is never flown away in the ring.
  forecast_list_push(data->days, data->days_received,
                     (int)data->current_day_index, false,
                     NULL, NULL,
                     prv_on_city_select_requested, data);
}

// ---- Globe (3D earth) ----
// The globe renders + touch-spins and is reachable from the forecast list's
// city-select and from the clock's wrap gesture. Actually changing the active
// location from the globe (location_select) is not yet wired up — for now
// selecting a location just returns to the main screen.

static void prv_push_globe(WeatherAppData *data, bool animated) {
  if (!data->globe_view) {
    return;
  }
  if (data->latitude_e2 != INT16_MIN && data->longitude_e2 != INT16_MIN) {
    globe_view_set_current_location(data->globe_view, data->location_buf,
                                    data->latitude_e2, data->longitude_e2);
  }
  globe_view_push_animated(data->globe_view, animated);
}

static void prv_on_city_select_requested(void *ctx) {
  prv_push_globe((WeatherAppData *)ctx, true);
}

static void prv_globe_wrap_to_main(void *ctx) {
  WeatherAppData *data = (WeatherAppData *)ctx;
  if (data->globe_view) {
    globe_view_dismiss(data->globe_view, false);
  }
  forecast_list_dismiss(false);
  if (clock_face_is_showing()) {
    clock_face_dismiss(false);
  }
}

static void prv_globe_location_selected(SavedLocationKind kind, int preset_index,
                                        const char *query, bool force, void *ctx) {
  WeatherAppData *data = (WeatherAppData *)ctx;
  (void)preset_index;
  (void)query;
  (void)force;
  // IMPORTANT: do NOT dismiss the globe here. The globe pops itself right after
  // this callback returns (commit_hovered_location() → globe_view_pop() pops the
  // top window). Removing the globe here too made that pop hit the main window
  // and exit the app. Only remove the list so the globe's self-pop lands on main.
  if (kind == SavedLocationKindCurrent) {
    // The synced current/default location is the one record the firmware weather
    // DB always has; switch to it and re-read.
    data->active_index = 0;
    data->current_day_index = 0;
    prv_refresh(data);
  }
  // Preset/custom cities are a phone-geocode feature (the phone fetched them on
  // demand). The firmware weather DB has no record for a city the phone hasn't
  // synced, so there is nothing to display — just return to the main screen.
  forecast_list_dismiss(false);
  // The globe self-pops to main after committing a city — keep the ring page in sync,
  // and fly the main icons back in when it re-appears (globe -> 7-day screen transition).
  data->pending_return_anim = true;
  s_page = PAGE_MAIN;
}

// ---- Saved-locations list (restored from the original app) ----
// Reached by swiping up on the globe's intro cradle. The screen manages the saved
// cities (view list / voice-add / delete). Selecting the current-location entry
// re-reads the synced weather DB record; preset/custom cities only have data once
// the phone has synced them. The screen dismisses itself.
static void prv_on_saved_location_selected(SavedLocationKind kind, int preset_index,
                                           const char *query, void *ctx) {
  WeatherAppData *data = (WeatherAppData *)ctx;
  (void)preset_index;
  (void)query;
  if (kind == SavedLocationKindCurrent) {
    data->active_index = 0;
    data->current_day_index = 0;
    prv_refresh(data);
  }
}

static void prv_open_saved_locations(void *ctx) {
  WeatherAppData *data = (WeatherAppData *)ctx;
  if (!data) {
    return;
  }
  SavedLocationsConfig config = {
    .current_location_label = data->location_buf[0] ? data->location_buf : "Current Location",
    .active_city_index = data->active_index,
    .active_custom_query = NULL,
    .select_callback = prv_on_saved_location_selected,
    .select_context = data,
  };
  saved_locations_push(&config);
}

static void prv_on_clock_wrap_to_globe(void *ctx) {
  WeatherAppData *data = (WeatherAppData *)ctx;
  if (clock_face_is_showing()) {
    clock_face_dismiss(false);
  }
  prv_push_globe(data, true);
}

// ---- Carousel ring ----

// Bring page `target` to the front, tearing down whatever ring page is currently on top of
// main. Main is the persistent base, so going TO main just dismisses the current top, and
// going FROM main just pushes the target; non-main -> non-main does both (a replace). All
// pushes/dismisses are un-animated here (stage 1) — stage 2 adds the uniform left/right slide.
static void prv_carousel_show(WeatherAppData *data, WeatherCarouselPage target) {
  if (target == s_page) {
    return;
  }
  const WeatherCarouselPage source = s_page;  // remember where we came from
  switch (s_page) {  // tear down the current top page (main is never torn down)
    case PAGE_LIST:  forecast_list_dismiss(false); break;
    case PAGE_CLOCK: if (clock_face_is_showing()) clock_face_dismiss(false); break;
    case PAGE_GLOBE: if (data->globe_view) globe_view_dismiss(data->globe_view, false); break;
    default: break;
  }
  s_page = target;
  switch (target) {  // bring up the target (main is just revealed by the teardown)
    case PAGE_LIST:  prv_on_list_transition_done(data); break;
    case PAGE_CLOCK:
      // The icon shrink/squash plays on the MAIN scrub screen's icons, so only run
      // it when arriving from main. From the globe (or anywhere else), the icons
      // aren't the focus — use the default un-animated push.
      if (source == PAGE_MAIN) {
        weather_app_layout_start_clock_transition(&data->layout, prv_on_clock_transition_done, data);
      } else {
        prv_push_clock(data, true);  // globe etc. -> plain static push, no spiral intro
      }
      break;
    case PAGE_GLOBE: prv_push_globe(data, false); break;
    default: break;
  }
}

// Visible ring order — swipe-left advances along this list, swipe-right reverses, both wrap.
// The PAGE_* values are just window identities; THIS array defines the page order on screen:
//   animated forecast -> main (scrub) -> clock -> globe -> back to forecast.
static const WeatherCarouselPage s_ring[PAGE_COUNT] = {
  PAGE_LIST, PAGE_MAIN, PAGE_CLOCK, PAGE_GLOBE,
};

void weather_carousel_navigate(int dir) {
  if (!s_data) {
    return;
  }
  int pos = 0;
  for (int i = 0; i < PAGE_COUNT; i++) {
    if (s_ring[i] == s_page) { pos = i; break; }
  }
  int next = (pos + dir + PAGE_COUNT) % PAGE_COUNT;
  prv_carousel_show(s_data, s_ring[next]);
}

static void prv_handle_weather(PebbleEvent *event, void *context) {
  prv_refresh(s_data);
}

// ===========================================================================
// Touch gesture system (ported from the original orchestrator weather.c).
// ===========================================================================

#define SWIPE_THRESHOLD_PX 20
#define MAIN_DRAG_AXIS_LOCK_PX 8
#define MAIN_DRAG_COMMIT_PX 36
#define MAIN_DRAG_PREVIEW_PX 64
#define MAIN_DRAG_PREVIEW_MAX_PROGRESS \
    ((AnimationProgress)((int32_t)ANIMATION_NORMALIZED_MAX * 88 / 100))
#define MAIN_DRAG_COMMIT_PROGRESS \
    ((AnimationProgress)((int32_t)ANIMATION_NORMALIZED_MAX * 45 / 100))
#define MAIN_DAY_SCRUB_UNIT_PX 74
#define MAIN_DAY_FLICK_MAX_MS 300
#define MAIN_DAY_FLICK_PREVIEW_DELAY_MS 190
#define MAIN_CLOCK_DRAG_AUTO_COMMIT_PROGRESS MAIN_DRAG_PREVIEW_MAX_PROGRESS
#define MAIN_DAY_PROGRESS_EPSILON \
    ((AnimationProgress)((int32_t)ANIMATION_NORMALIZED_MAX / 24))
#define MAIN_DAY_EDGE_DEADBAND \
    ((AnimationProgress)((int32_t)ANIMATION_NORMALIZED_MAX / 32))

static AnimationProgress prv_main_drag_progress(int signed_distance) {
  if (signed_distance < 0) signed_distance = 0;
  if (signed_distance > MAIN_DRAG_PREVIEW_PX) signed_distance = MAIN_DRAG_PREVIEW_PX;
  return (AnimationProgress)weather_scale_i32(MAIN_DRAG_PREVIEW_MAX_PROGRESS,
                                              signed_distance,
                                              MAIN_DRAG_PREVIEW_PX);
}

static const WeatherLocationForecast *prv_day_at(int index) {
  if (!s_data || index < 0 || index >= (int)s_data->days_received) return NULL;
  return &s_data->days[index];
}

static int32_t prv_day_drag_position_fp(int16_t touch_y) {
  if (!s_data || s_data->days_received == 0) return 0;

  const int32_t unit = (int32_t)ANIMATION_NORMALIZED_MAX;
  int16_t dy = touch_y - s_data->touch_day_anchor_y;
  int32_t start = (int32_t)s_data->touch_day_anchor_index * unit;
  int32_t delta = (int32_t)(-dy) * unit / MAIN_DAY_SCRUB_UNIT_PX;
  int32_t pos = start + delta;
  int32_t max_pos = (int32_t)(s_data->days_received - 1) * unit;

  if (pos < 0) pos = 0;
  if (pos > max_pos) pos = max_pos;
  return pos;
}

static int prv_day_drag_nearest_index(int32_t position_fp) {
  if (!s_data || s_data->days_received == 0) return 0;
  const int32_t unit = (int32_t)ANIMATION_NORMALIZED_MAX;
  int index = (int)((position_fp + unit / 2) / unit);
  if (index < 0) index = 0;
  if (index >= (int)s_data->days_received) index = (int)s_data->days_received - 1;
  return index;
}

static bool prv_day_progress_changed(AnimationProgress a, AnimationProgress b) {
  return (a > b) ? (a - b >= MAIN_DAY_PROGRESS_EPSILON)
                 : (b - a >= MAIN_DAY_PROGRESS_EPSILON);
}

static void prv_capture_touch_start_time(void) {
  if (!s_data) return;
  time_ms(&s_data->touch_start_time_s, &s_data->touch_start_time_ms);
}

static uint32_t prv_touch_elapsed_ms(void) {
  if (!s_data) return 0;
  time_t now_s = 0;
  uint16_t now_ms = 0;
  time_ms(&now_s, &now_ms);

  int32_t elapsed = (int32_t)(now_s - s_data->touch_start_time_s) * 1000
      + (int32_t)now_ms - (int32_t)s_data->touch_start_time_ms;
  return elapsed > 0 ? (uint32_t)elapsed : 0;
}

static bool prv_should_hold_day_preview_for_flick(void) {
  return !s_data->touch_day_preview_started
      && prv_touch_elapsed_ms() <= MAIN_DAY_FLICK_PREVIEW_DELAY_MS;
}

static void prv_begin_day_preview_at(int16_t touch_y) {
  if (!s_data) return;
  s_data->touch_day_preview_started = true;
  s_data->touch_day_anchor_y = touch_y;
  s_data->touch_day_anchor_index = (int)s_data->current_day_index;
  s_data->preview_old_day_index = (int)s_data->current_day_index;
  s_data->preview_day_index = (int)s_data->current_day_index;
  s_data->preview_progress = 0;
}

static void prv_set_current_day_after_drag(int index) {
  if (index < 0 || index >= (int)s_data->days_received) return;
  s_data->current_day_index = (unsigned int)index;
  weather_app_layout_set_fin_allowed(&s_data->layout,
                                     s_data->days_received > 1
                                     && s_data->current_day_index + 1 >= s_data->days_received);
  bool can_scroll_down = (s_data->current_day_index + 1 < s_data->days_received);
  weather_app_layout_set_down_arrow_visible(&s_data->layout, can_scroll_down);
}

static int prv_main_drag_signed_distance(MainTouchMode mode, int16_t dx, int16_t dy) {
  switch (mode) {
    case MainTouchMode_Clock: return -dx;
    case MainTouchMode_Day:
      return (s_data->preview_day_index > (int)s_data->current_day_index) ? -dy : dy;
    case MainTouchMode_Pull:
    case MainTouchMode_None:
    default:
      return 0;
  }
}

static bool prv_main_drag_should_commit(int signed_distance,
                                        AnimationProgress progress) {
  return signed_distance >= MAIN_DRAG_COMMIT_PX
      || progress >= MAIN_DRAG_COMMIT_PROGRESS;
}

static void prv_reset_main_touch_state(void) {
  if (!s_data) return;
  s_data->touch_active = false;
  s_data->pull_in_progress = false;
  s_data->touch_axis_locked = false;
  s_data->touch_mode = MainTouchMode_None;
  s_data->layout.glow_paused = false;
  layer_mark_dirty(s_data->layout.content_layer);
  s_data->touch_start_day_index = (int)s_data->current_day_index;
  s_data->touch_day_anchor_y = s_data->touch_start_y;
  s_data->touch_day_anchor_index = (int)s_data->current_day_index;
  s_data->touch_day_update_count = 0;
  s_data->touch_day_preview_started = false;
  s_data->preview_day_index = -1;
  s_data->preview_old_day_index = -1;
  s_data->preview_progress = 0;
}

static bool prv_maybe_auto_commit_clock_drag(AnimationProgress progress) {
  if (!s_data || s_data->touch_mode != MainTouchMode_Clock
      || progress < MAIN_CLOCK_DRAG_AUTO_COMMIT_PROGRESS) {
    return false;
  }

  weather_app_layout_update_interactive(&s_data->layout,
                                        ANIMATION_NORMALIZED_MAX);
  prv_reset_main_touch_state();
  weather_app_layout_finish_interactive(&s_data->layout, true,
                                        prv_on_clock_transition_done, s_data);
  return true;
}

static bool prv_maybe_auto_commit_page_drag(AnimationProgress progress) {
  return prv_maybe_auto_commit_clock_drag(progress);
}

static void prv_update_vertical_day_scrub(int16_t touch_y, bool force) {
  if (!s_data || s_data->days_received == 0) return;

  int32_t pos = prv_day_drag_position_fp(touch_y);
  const int32_t unit = (int32_t)ANIMATION_NORMALIZED_MAX;
  int base = (int)(pos / unit);
  AnimationProgress progress = (AnimationProgress)(pos % unit);

  if (!force && base > 0 && progress < MAIN_DAY_EDGE_DEADBAND) {
    base--;
    progress = ANIMATION_NORMALIZED_MAX;
  } else if (!force && base > 0 && progress >= MAIN_DAY_EDGE_DEADBAND) {
    progress = (AnimationProgress)weather_scale_i32(
        progress - MAIN_DAY_EDGE_DEADBAND,
        ANIMATION_NORMALIZED_MAX,
        ANIMATION_NORMALIZED_MAX - MAIN_DAY_EDGE_DEADBAND);
  }

  if (base >= (int)s_data->days_received - 1) {
    base = (int)s_data->days_received - 2;
    if (base < 0) base = 0;
    progress = (s_data->days_received > 1) ? ANIMATION_NORMALIZED_MAX : 0;
  }

  if (progress == 0) {
    if (force || s_data->preview_old_day_index != base
        || s_data->preview_day_index != base
        || s_data->preview_progress != 0) {
      s_data->preview_old_day_index = base;
      s_data->preview_day_index = base;
      s_data->preview_progress = 0;
      weather_app_layout_scrub_interactive_day(&s_data->layout,
                                               prv_day_at(base),
                                               prv_day_at(base + 1),
                                               NULL,
                                               NULL,
                                               true,
                                               0);
    }
    return;
  }

  int next = base + 1;
  if (!force && s_data->preview_old_day_index == base
      && s_data->preview_day_index == next
      && !prv_day_progress_changed(s_data->preview_progress, progress)) {
    return;
  }

  s_data->preview_old_day_index = base;
  s_data->preview_day_index = next;
  s_data->preview_progress = progress;
  weather_app_layout_scrub_interactive_day(&s_data->layout,
                                           prv_day_at(base),
                                           prv_day_at(base + 1),
                                           prv_day_at(next),
                                           prv_day_at(next + 1),
                                           true,
                                           progress);
}

static void prv_touch_handler_interactive(const TouchEvent *event, void *context) {
  if (event->type == TouchEvent_Touchdown) {
    weather_app_layout_note_interaction(&s_data->layout);
    s_data->touch_start_x = event->x;
    s_data->touch_start_y = event->y;
    s_data->touch_active = true;
    s_data->layout.glow_paused = true;
    s_data->pull_in_progress = false;
    s_data->touch_axis_locked = false;
    s_data->touch_mode = MainTouchMode_None;
    s_data->touch_start_day_index = (int)s_data->current_day_index;
    s_data->touch_day_anchor_y = event->y;
    s_data->touch_day_anchor_index = (int)s_data->current_day_index;
    s_data->touch_day_update_count = 0;
    s_data->touch_day_preview_started = false;
    prv_capture_touch_start_time();
    s_data->preview_day_index = -1;
    s_data->preview_old_day_index = -1;
    s_data->preview_progress = 0;
  } else if (event->type == TouchEvent_PositionUpdate && s_data->touch_active) {
    int16_t dy = event->y - s_data->touch_start_y;
    int16_t dx = event->x - s_data->touch_start_x;
    int16_t ady = dy < 0 ? -dy : dy;
    int16_t adx = dx < 0 ? -dx : dx;

    if (s_data->touch_mode == MainTouchMode_Pull) {
      if (dy > 0 && ady >= adx) {
        weather_app_layout_pull_update(&s_data->layout, (int)dy);
      } else {
        weather_app_layout_pull_abort(&s_data->layout);
        prv_reset_main_touch_state();
      }
      return;
    }

    if (s_data->touch_mode != MainTouchMode_None) {
      if (s_data->touch_mode == MainTouchMode_Day) {
        s_data->touch_day_update_count++;
        if (prv_should_hold_day_preview_for_flick()) {
          return;
        }
        if (!s_data->touch_day_preview_started) {
          prv_begin_day_preview_at(event->y);
        }
        prv_update_vertical_day_scrub(event->y, false);
      } else {
        int signed_distance = prv_main_drag_signed_distance(s_data->touch_mode, dx, dy);
        AnimationProgress progress = prv_main_drag_progress(signed_distance);
        s_data->preview_progress = progress;
        weather_app_layout_update_interactive(&s_data->layout, progress);
        if (prv_maybe_auto_commit_page_drag(progress)) {
          return;
        }
      }
      return;
    }

    if (!s_data->touch_axis_locked
        && (adx >= MAIN_DRAG_AXIS_LOCK_PX || ady >= MAIN_DRAG_AXIS_LOCK_PX)) {
      s_data->touch_axis_locked = true;

      if (ady >= adx) {
        if (dy > 0 && s_data->current_day_index == 0) {
          s_data->touch_mode = MainTouchMode_Pull;
          s_data->pull_in_progress = true;
          weather_app_layout_pull_update(&s_data->layout, (int)dy);
          return;
        }

        if (s_data->days_received > 1) {
          s_data->touch_mode = MainTouchMode_Day;
          s_data->preview_old_day_index = (int)s_data->current_day_index;
          s_data->preview_day_index = (int)s_data->current_day_index;
        }
      } else {
        // Horizontal swipe (either direction) — the carousel hop commits on liftoff;
        // no finger-tracked preview in stage 1 (stage 2 adds the uniform slide).
      }

      if (s_data->touch_mode != MainTouchMode_None) {
        if (s_data->touch_mode == MainTouchMode_Day) {
          s_data->touch_day_update_count++;
          if (prv_should_hold_day_preview_for_flick()) {
            return;
          }
          if (!s_data->touch_day_preview_started) {
            prv_begin_day_preview_at(event->y);
          }
          prv_update_vertical_day_scrub(event->y, true);
        } else {
          int signed_distance = prv_main_drag_signed_distance(s_data->touch_mode, dx, dy);
          AnimationProgress progress = prv_main_drag_progress(signed_distance);
          s_data->preview_progress = progress;
          weather_app_layout_update_interactive(&s_data->layout, progress);
          if (prv_maybe_auto_commit_page_drag(progress)) {
            return;
          }
        }
      }
    }
  } else if (event->type == TouchEvent_Liftoff && s_data->touch_active) {
    if (s_data->touch_mode == MainTouchMode_Pull || s_data->pull_in_progress) {
      bool triggered = weather_app_layout_pull_release(&s_data->layout);
      if (triggered) {
        // No AppMessage in the system app: pull-to-refresh re-reads the BlobDB.
        prv_refresh(s_data);
      }
      prv_reset_main_touch_state();
      return;
    }

    int16_t dx = event->x - s_data->touch_start_x;
    int16_t dy = event->y - s_data->touch_start_y;
    int16_t adx = dx < 0 ? -dx : dx;
    int16_t ady = dy < 0 ? -dy : dy;

    if (s_data->touch_mode != MainTouchMode_None) {
      MainTouchMode mode = s_data->touch_mode;
      if (mode == MainTouchMode_Day) {
        int16_t day_distance = dy < 0 ? -dy : dy;
        uint32_t elapsed_ms = prv_touch_elapsed_ms();
        bool flick_like = day_distance >= SWIPE_THRESHOLD_PX
            && day_distance >= adx
            && elapsed_ms <= MAIN_DAY_FLICK_MAX_MS;
        bool has_transition = weather_app_layout_is_interactive_active(&s_data->layout);

        if (flick_like) {
          bool is_down = dy < 0;
          weather_app_layout_abort_interactive(&s_data->layout,
                                               prv_day_at((int)s_data->current_day_index),
                                               prv_day_at((int)s_data->current_day_index + 1));
          prv_reset_main_touch_state();
          prv_navigate(s_data, is_down);
          return;
        }

        if (!s_data->touch_day_preview_started) {
          prv_begin_day_preview_at(event->y);
        }
        prv_update_vertical_day_scrub(event->y, true);
        int32_t position_fp = prv_day_drag_position_fp(event->y);
        int target_day = prv_day_drag_nearest_index(position_fp);
        int old_segment = s_data->preview_old_day_index;
        int next_segment = s_data->preview_day_index;
        has_transition = weather_app_layout_is_interactive_active(&s_data->layout);
        prv_reset_main_touch_state();

        if (!has_transition || old_segment == next_segment) {
          prv_set_current_day_after_drag(target_day);
          weather_app_layout_set_data(&s_data->layout,
                                      prv_day_at(target_day),
                                      prv_day_at(target_day + 1));
        } else if (target_day == next_segment) {
          prv_set_current_day_after_drag(target_day);
          weather_app_layout_finish_interactive(&s_data->layout, true, NULL, NULL);
        } else if (target_day == old_segment) {
          prv_set_current_day_after_drag(target_day);
          weather_app_layout_finish_interactive(&s_data->layout, false, NULL, NULL);
        } else {
          prv_set_current_day_after_drag(target_day);
          weather_app_layout_abort_interactive(&s_data->layout,
                                               prv_day_at(target_day),
                                               prv_day_at(target_day + 1));
        }
        return;
      }

      int signed_distance = prv_main_drag_signed_distance(mode, dx, dy);
      bool commit = prv_main_drag_should_commit(signed_distance,
                                                s_data->preview_progress);
      int target_day = s_data->preview_day_index;
      prv_reset_main_touch_state();

      if (commit) {
        if (mode == MainTouchMode_Day && target_day >= 0) {
          s_data->current_day_index = (unsigned int)target_day;
          weather_app_layout_set_fin_allowed(&s_data->layout,
                                             s_data->days_received > 1
                                             && s_data->current_day_index + 1 >= s_data->days_received);
          bool can_scroll_down = (s_data->current_day_index + 1 < s_data->days_received);
          weather_app_layout_set_down_arrow_visible(&s_data->layout, can_scroll_down);
          weather_app_layout_finish_interactive(&s_data->layout, true, NULL, NULL);
        } else if (mode == MainTouchMode_Clock) {
          weather_app_layout_finish_interactive(&s_data->layout, true,
                                                prv_on_clock_transition_done, s_data);
        } else {
          weather_app_layout_cancel_interactive(&s_data->layout);
        }
      } else {
        weather_app_layout_cancel_interactive(&s_data->layout);
      }
      return;
    }

    s_data->touch_active = false;
    s_data->layout.glow_paused = false;
    layer_mark_dirty(s_data->layout.content_layer);

    if (dx < -SWIPE_THRESHOLD_PX && adx > ady) {
      weather_carousel_navigate(+1);   // swipe left = next page (7-day list)
    } else if (dx > SWIPE_THRESHOLD_PX && adx > ady) {
      weather_carousel_navigate(-1);   // swipe right = previous page (globe, wraps)
    } else if (dy < -SWIPE_THRESHOLD_PX && ady >= adx) {
      prv_navigate(s_data, true);      // swipe up = next day (within-screen)
    } else if (dy > SWIPE_THRESHOLD_PX && ady >= adx) {
      prv_navigate(s_data, false);     // swipe down = previous day (within-screen)
    }
  }
}

// ---- Location-bar clock tick ----

static void prv_tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  // Redraw the location bar every minute so the digital time stays current.
  if (s_data && s_data->layout.location_bar_layer) {
    layer_mark_dirty(s_data->layout.location_bar_layer);
  }
}

// ---- Buttons ----

static void prv_up_down_click_handler(ClickRecognizerRef recognizer, void *context) {
  weather_app_layout_note_interaction(&s_data->layout);
  const bool is_down = (click_recognizer_get_button_id(recognizer) == BUTTON_ID_DOWN);
  prv_navigate(s_data, is_down);  // UP/DOWN = day scrub (within-screen)
}

// ---- Hold-to-scroll with acceleration (raw click subscriber + AppTimer) ----
#define HOLD_INITIAL_MS  300
#define HOLD_MIN_MS       90

static AppTimer *s_hold_timer = NULL;
static bool      s_hold_is_down = false;
static int       s_hold_repeat  = 0;

static void prv_hold_timer_cb(void *ctx) {
  s_hold_timer = NULL;
  if (!s_data) return;
  prv_navigate(s_data, s_hold_is_down);
  s_hold_repeat++;
  int interval = HOLD_INITIAL_MS - s_hold_repeat * 50;
  if (interval < HOLD_MIN_MS) interval = HOLD_MIN_MS;
  s_hold_timer = app_timer_register(interval, prv_hold_timer_cb, NULL);
}

static void prv_raw_up_down(ButtonId btn, bool pressed) {
  if (pressed) {
    weather_app_layout_note_interaction(&s_data->layout);
    s_hold_is_down = (btn == BUTTON_ID_DOWN);
    s_hold_repeat  = 0;
    if (s_hold_timer) { app_timer_cancel(s_hold_timer); s_hold_timer = NULL; }
    s_hold_timer = app_timer_register(HOLD_INITIAL_MS, prv_hold_timer_cb, NULL);
    s_data->layout.glow_paused = true;
  } else {
    if (s_hold_timer) { app_timer_cancel(s_hold_timer); s_hold_timer = NULL; }
    s_data->layout.glow_paused = false;
  }
}

static void prv_raw_up_pressed(ClickRecognizerRef r, void *ctx)   { prv_raw_up_down(BUTTON_ID_UP,   true);  }
static void prv_raw_up_released(ClickRecognizerRef r, void *ctx)  { prv_raw_up_down(BUTTON_ID_UP,   false); }
static void prv_raw_down_pressed(ClickRecognizerRef r, void *ctx) { prv_raw_up_down(BUTTON_ID_DOWN, true);  }
static void prv_raw_down_released(ClickRecognizerRef r, void *ctx){ prv_raw_up_down(BUTTON_ID_DOWN, false); }

static void prv_click_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_UP,     prv_up_down_click_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN,   prv_up_down_click_handler);
  window_raw_click_subscribe(BUTTON_ID_UP,   prv_raw_up_pressed,   prv_raw_up_released,   NULL);
  window_raw_click_subscribe(BUTTON_ID_DOWN, prv_raw_down_pressed, prv_raw_down_released, NULL);
}

// ---- Window lifecycle ----

static void prv_window_load(Window *window) {
  WeatherAppData *data = app_state_get_user_data();
  layer_add_child(&window->layer, data->layout.root_layer);
}

static void prv_window_appear(Window *window) {
  WeatherAppData *data = app_state_get_user_data();
  data->weather_event_info = (EventServiceInfo) {
    .type = PEBBLE_WEATHER_EVENT,
    .handler = prv_handle_weather,
  };
  event_service_client_subscribe(&data->weather_event_info);
  // Keep the location-bar clock current after returning from any child window.
  tick_timer_service_subscribe(MINUTE_UNIT, prv_tick_handler);
  touch_service_subscribe(prv_touch_handler_interactive, data);
  weather_app_layout_note_interaction(&data->layout);
#if PBL_ROUND
  // The globe -> main "fly-in" the carousel had left unwired. Only when returning
  // from a globe city-commit (flag set there), not on every appear.
  if (data->pending_return_anim) {
    data->pending_return_anim = false;
    weather_app_layout_start_return_transition(&data->layout);
  }
#endif
}

static void prv_window_disappear(Window *window) {
  WeatherAppData *data = app_state_get_user_data();
  event_service_client_unsubscribe(&data->weather_event_info);
  tick_timer_service_unsubscribe();
  touch_service_unsubscribe();
}

static void prv_window_unload(Window *window) {
  WeatherAppData *data = app_state_get_user_data();
  if (s_hold_timer) { app_timer_cancel(s_hold_timer); s_hold_timer = NULL; }
  clock_face_set_wrap_callback(NULL, NULL);
  if (data->globe_view) {
    globe_view_destroy(data->globe_view);
    data->globe_view = NULL;
  }
  weather_app_layout_deinit(&data->layout);
}

static NOINLINE void prv_init(void) {
  WeatherAppData *data = app_zalloc_check(sizeof(WeatherAppData));
  app_state_set_user_data(data);
  s_data = data;
  s_hold_timer = NULL;

  Window *window = &data->window;
  window_init(window, WINDOW_NAME("Weather"));
  const WindowHandlers handlers = {
    .load = prv_window_load,
    .appear = prv_window_appear,
    .disappear = prv_window_disappear,
    .unload = prv_window_unload,
  };
  window_set_window_handlers(window, handlers);  // macro maps to _by_value
  window_set_click_config_provider(window, prv_click_provider);

  weather_app_layout_init(&data->layout, &window->layer.bounds);

  prv_refresh(data);

  // Create the globe once (held for the app's lifetime; ~31KB of cubemap +
  // starfield + sequence resources). prv_refresh ran first, so lat/lon is set.
  data->globe_view = globe_view_create();
  if (data->globe_view) {
    globe_view_set_location_select_callback(data->globe_view,
                                            prv_globe_location_selected, data);
    globe_view_set_main_callback(data->globe_view, prv_globe_wrap_to_main, data);
    globe_view_set_saved_locations_callback(data->globe_view, prv_open_saved_locations, data);
    if (data->latitude_e2 != INT16_MIN && data->longitude_e2 != INT16_MIN) {
      globe_view_set_current_location(data->globe_view, data->location_buf,
                                      data->latitude_e2, data->longitude_e2);
    }
  }
  clock_face_set_wrap_callback(prv_on_clock_wrap_to_globe, data);

  const bool animated = true;
  app_window_stack_push(window, animated);
#if PBL_ROUND
  // The carousel opens on the animated forecast screen. Swipe left: forecast -> main ->
  // clock -> globe -> forecast; swipe right reverses.
  prv_carousel_show(data, PAGE_LIST);
#endif
}

static void prv_deinit(void) {
  // Layout teardown happens in prv_window_unload; app heap is reclaimed on exit.
  s_data = NULL;
}

static void prv_main(void) {
  prv_init();
  app_event_loop();
  prv_deinit();
}

const PebbleProcessMd* weather_app_get_info(void) {
  static const PebbleProcessMdSystem s_weather_app_info = {
    .common = {
      .main_func = prv_main,
      .uuid = UUID_WEATHER_DATA_SOURCE,
    },
    .name = i18n_noop("Weather"),
    .icon_resource_id = RESOURCE_ID_GENERIC_WEATHER_TINY,
  };
  return weather_ds_supported() ? (const PebbleProcessMd *)&s_weather_app_info : NULL;
}
