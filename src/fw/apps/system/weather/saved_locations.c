/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "saved_locations.h"

#include "city_presets.h"

_Static_assert(CITY_PRESET_COUNT <= INT8_MAX, "SavedLocationEntry.preset_index is int8_t");
#include "resource_ids.pin.h"
#include "weather_data_source.h"
#include "pbl/services/comm_session/session.h"
#include "pbl/services/system_task.h"
#include "weather_types.h"

#define SAVED_LOCATIONS_PERSIST_COUNT_KEY 6100
#define SAVED_LOCATIONS_PERSIST_LABEL_KEY_BASE 6110
#define SAVED_LOCATIONS_PERSIST_QUERY_KEY_BASE 6120
#define SAVED_LOCATIONS_PERSIST_DELETED_PRESETS_KEY 6130
// (persist key 6131, CURRENT_VISIBLE, retired — the row is always shown)
#define SAVED_LOCATIONS_PERSIST_DEFAULT_PRESETS_V1_KEY 6132
#define SAVED_LOCATIONS_PERSIST_LAT_KEY_BASE 6140
#define SAVED_LOCATIONS_PERSIST_LON_KEY_BASE 6150
#define SAVED_LOCATIONS_PERSIST_HAS_COORDS_KEY_BASE 6160

#define SAVED_LOCATIONS_ROW_ADD 0
#define SAVED_LOCATIONS_RESULT_TICK_MS 33
#define SAVED_LOCATIONS_RESULT_HOLD_MS 650
#define SAVED_LOCATIONS_ROW_HEIGHT 44
#define SAVED_LOCATIONS_TOUCH_AXIS_THRESHOLD_PX 5
#define SAVED_LOCATIONS_TOUCH_TAP_THRESHOLD_PX 10
#define SAVED_LOCATIONS_TOUCH_SWIPE_BACK_PX 20  // horizontal right-swipe = BACK to the globe
#define SAVED_LOCATIONS_FLING_PROJECT_MS 340    // how far a flick coasts (ms of velocity)
#define SAVED_LOCATIONS_FLING_MIN_VELOCITY 120  // px/s; below this a release just stops
#define SAVED_LOCATIONS_DEFAULT_PRESET_MASK \
  ((uint16_t)((1u << 0) | (1u << 4) | (1u << 5) | \
              (1u << 10) | (1u << 11)))

typedef struct {
  char label[SAVED_LOCATION_LABEL_SIZE];
  char query[SAVED_LOCATION_QUERY_SIZE];
  int16_t latitude_e2;
  int16_t longitude_e2;
  bool has_coordinates;
} SavedCustomLocation;

typedef struct {
  Window *window;
  MenuLayer *menu_layer;
  DictationSession *dictation_session;
  Window *confirm_window;
  Layer *confirm_layer;
  Window *result_window;
  Layer *result_layer;
  GDrawCommandSequence *shred_sequence;
  AppTimer *result_timer;
  SavedLocationsSelectCallback select_callback;
  void *select_context;
  char current_location_label[SAVED_LOCATION_LABEL_SIZE];
  char active_custom_query[SAVED_LOCATION_QUERY_SIZE];
  char pending_delete_label[SAVED_LOCATION_LABEL_SIZE];
  int active_city_index;
  SavedLocationKind pending_delete_kind;
  int pending_delete_row;
  int pending_delete_preset_index;
  int pending_delete_custom_index;
  uint32_t result_elapsed_ms;
  int scroll_at_drag_start;
  int16_t touch_start_x;
  int16_t touch_start_y;
  bool touch_active;
  bool drag_axis_set;
  bool drag_is_vertical;
  int16_t last_drag_y;       // momentum: most recent drag sample position
  uint32_t last_drag_ms;     // ... and its time, for velocity
  int fling_velocity;        // most recent finger velocity, px/s (down = +)
} SavedLocationsView;

static SavedLocationsView *s_view;
static SavedCustomLocation *s_custom_locations;
static int s_custom_count;
static uint16_t s_deleted_preset_mask;
static bool s_custom_loaded;

// ---- At-a-glance weather for the list rows ---------------------------------
// Snapshot of the weather-service locations, refreshed when the list opens.
// Rows are matched to snapshots by case-insensitive substring (the service
// stores "New York, United States"; the preset row is "New York"; a dictated
// "Jamaica" matches "Kingston, Jamaica"), so cities without synced weather
// just fall back to the plain two-line row.
#define GLANCE_MAX_LOCATIONS 12
#define GLANCE_WEATHER_TYPES 9  // WeatherType_PartlyCloudy(0) .. WeatherType_RainAndSnow(8)
typedef struct {
  char name[64];
  int16_t temp;
  uint8_t type;
  bool is_current;
} GlanceEntry;

// Heap-allocated while the saved-locations window is open (freed at unload): as firmware
// statics these were ~850 B of ALWAYS-resident .bss even when the Weather app never ran.
static GlanceEntry *s_glance;
static int s_glance_count;
static GBitmap *s_glance_icons[GLANCE_WEATHER_TYPES];

static void prv_glance_refresh(void) {
  s_glance_count = 0;
  if (!weather_ds_supported()) return;
  if (!s_glance) {
    s_glance = malloc_try(sizeof(GlanceEntry) * GLANCE_MAX_LOCATIONS);
    if (!s_glance) return;   // out of heap: rows fall back to the plain two-line layout
  }
  WxDsForecast *scratch = malloc_try(sizeof(*scratch));  // ~400 B; keep off the task stack
  if (!scratch) return;
  int count = weather_ds_location_count();
  if (count > GLANCE_MAX_LOCATIONS) count = GLANCE_MAX_LOCATIONS;
  for (int i = 0; i < count; i++) {
    if (!weather_ds_read_index(i, scratch)) continue;
    if (scratch->current_temp == WX_DS_UNKNOWN_TEMP) continue;
    GlanceEntry *entry = &s_glance[s_glance_count++];
    strncpy(entry->name, scratch->location_name, sizeof(entry->name) - 1);
    entry->name[sizeof(entry->name) - 1] = '\0';
    entry->temp = (int16_t)scratch->current_temp;
    entry->type = scratch->current_weather_type;
    entry->is_current = scratch->is_current_location;
  }
  free(scratch);
}

static void prv_glance_free(void) {
  s_glance_count = 0;
  if (s_glance) {
    free(s_glance);
    s_glance = NULL;
  }
}

static void prv_glance_destroy_icons(void) {
  for (int i = 0; i < GLANCE_WEATHER_TYPES; i++) {
    if (s_glance_icons[i]) {
      gbitmap_destroy(s_glance_icons[i]);
      s_glance_icons[i] = NULL;
    }
  }
}

static GBitmap *prv_glance_icon(uint8_t type) {
  if (type >= GLANCE_WEATHER_TYPES) return NULL;
  if (!s_glance_icons[type]) {
    s_glance_icons[type] =
        gbitmap_create_with_resource(weather_type_icon_tiny_resource(type));
  }
  return s_glance_icons[type];
}

static const GlanceEntry *prv_glance_find(const char *label, bool want_current) {
  if (want_current) {
    for (int i = 0; i < s_glance_count; i++) {
      if (s_glance[i].is_current) return &s_glance[i];
    }
    return NULL;
  }
  // Prefix matches beat bare substring matches (see weather_ds_name_matches),
  // so the "London" row can't pick up an "East London, SA" record's temp while
  // real London is also synced.
  for (int i = 0; i < s_glance_count; i++) {
    if (weather_ds_name_prefix(s_glance[i].name, label)) return &s_glance[i];
  }
  for (int i = 0; i < s_glance_count; i++) {
    if (weather_ds_name_matches(s_glance[i].name, label)) return &s_glance[i];
  }
  return NULL;
}

static bool prv_is_default_saved_preset(int preset_index) {
  return preset_index >= 0 && preset_index < CITY_PRESET_COUNT &&
         (SAVED_LOCATIONS_DEFAULT_PRESET_MASK & (1u << preset_index));
}

static bool prv_ensure_custom_locations(void) {
  if (s_custom_locations) return true;
  s_custom_locations = calloc(SAVED_LOCATIONS_MAX_CUSTOM,
                              sizeof(SavedCustomLocation));
  return s_custom_locations != NULL;
}

static void prv_dictation_callback(DictationSession *session,
                                   DictationSessionStatus status,
                                   char *transcription,
                                   void *context);
static void prv_activate_saved_row(SavedLocationsView *view, int row);
#if WEATHER_PLATFORM_TOUCH_COLOR
static void prv_touch_handler(const TouchEvent *event, void *context);
#endif
static void prv_push_delete_confirm(SavedLocationsView *view,
                                   SavedLocationKind kind,
                                   int row,
                                   int preset_index,
                                   int custom_index,
                                   const char *label);
static void prv_confirm_delete_click(ClickRecognizerRef recognizer,
                                     void *context);
static void prv_confirm_window_unload(Window *window);
static void prv_result_window_unload(Window *window);
static void prv_save_custom_locations(void);
static void prv_compact_customs(void);

static void prv_load_custom_locations(void) {
  if (s_custom_loaded) return;
  s_custom_loaded = true;

  s_custom_count = 0;
  s_deleted_preset_mask = 0;

  if (persist_exists(SAVED_LOCATIONS_PERSIST_DELETED_PRESETS_KEY)) {
    s_deleted_preset_mask =
        (uint16_t)persist_read_int(SAVED_LOCATIONS_PERSIST_DELETED_PRESETS_KEY);
  }
  if (!persist_exists(SAVED_LOCATIONS_PERSIST_DEFAULT_PRESETS_V1_KEY)) {
    s_deleted_preset_mask &= (uint16_t)~SAVED_LOCATIONS_DEFAULT_PRESET_MASK;
    persist_write_int(SAVED_LOCATIONS_PERSIST_DELETED_PRESETS_KEY,
                      s_deleted_preset_mask);
    persist_write_int(SAVED_LOCATIONS_PERSIST_DEFAULT_PRESETS_V1_KEY, 1);
  }

  if (persist_exists(SAVED_LOCATIONS_PERSIST_COUNT_KEY)) {
    s_custom_count = persist_read_int(SAVED_LOCATIONS_PERSIST_COUNT_KEY);
    if (s_custom_count < 0) s_custom_count = 0;
    if (s_custom_count > SAVED_LOCATIONS_MAX_CUSTOM) {
      s_custom_count = SAVED_LOCATIONS_MAX_CUSTOM;
    }
  }

  if (s_custom_count > 0 && !prv_ensure_custom_locations()) {
    s_custom_count = 0;
    return;
  }

  for (int i = 0; i < s_custom_count; i++) {
    persist_read_string(SAVED_LOCATIONS_PERSIST_LABEL_KEY_BASE + i,
                        s_custom_locations[i].label,
                        sizeof(s_custom_locations[i].label));
    persist_read_string(SAVED_LOCATIONS_PERSIST_QUERY_KEY_BASE + i,
                        s_custom_locations[i].query,
                        sizeof(s_custom_locations[i].query));
    s_custom_locations[i].latitude_e2 = 0;
    s_custom_locations[i].longitude_e2 = 0;
    s_custom_locations[i].has_coordinates = false;
    if (persist_exists(SAVED_LOCATIONS_PERSIST_HAS_COORDS_KEY_BASE + i)) {
      s_custom_locations[i].has_coordinates =
          persist_read_int(SAVED_LOCATIONS_PERSIST_HAS_COORDS_KEY_BASE + i) != 0;
    }
    if (persist_exists(SAVED_LOCATIONS_PERSIST_LAT_KEY_BASE + i)) {
      s_custom_locations[i].latitude_e2 =
          (int16_t)persist_read_int(SAVED_LOCATIONS_PERSIST_LAT_KEY_BASE + i);
    }
    if (persist_exists(SAVED_LOCATIONS_PERSIST_LON_KEY_BASE + i)) {
      s_custom_locations[i].longitude_e2 =
          (int16_t)persist_read_int(SAVED_LOCATIONS_PERSIST_LON_KEY_BASE + i);
    }
    if (!s_custom_locations[i].query[0]) {
      strncpy(s_custom_locations[i].query, s_custom_locations[i].label,
              sizeof(s_custom_locations[i].query) - 1);
      s_custom_locations[i].query[sizeof(s_custom_locations[i].query) - 1] = '\0';
    }
  }


#if defined(CONFIG_SOC_QEMU)
  // QEMU test artifact (the emulator has no dictation): on a fresh persist store,
  // seed one custom location exactly as the voice-add flow stores it — name only,
  // NO coordinates. Together with the synth "Kingston, Jamaica" record this
  // exercises the real path: coordinate backfill -> globe placement -> commit.
  if (!persist_exists(SAVED_LOCATIONS_PERSIST_COUNT_KEY) && s_custom_count == 0 &&
      prv_ensure_custom_locations()) {
    strncpy(s_custom_locations[0].label, "Jamaica",
            sizeof(s_custom_locations[0].label) - 1);
    strncpy(s_custom_locations[0].query, "Jamaica",
            sizeof(s_custom_locations[0].query) - 1);
    s_custom_locations[0].has_coordinates = false;
    s_custom_count = 1;
  }
#endif

  // Heal any phantom slots (empty label) left by a count/persist desync: they
  // would otherwise draw as blank "ghost" rows in the list. Compacting here and
  // re-persisting means a corrupted store fixes itself on the next open.
  prv_compact_customs();
}

// Drop custom slots with an empty label (phantoms from a count/persist desync)
// and close the gaps, so s_custom_locations[0..count-1] are all real. Re-persist
// only when something actually changed.
static void prv_compact_customs(void) {
  if (!s_custom_locations || s_custom_count <= 0) return;
  int w = 0;
  for (int r = 0; r < s_custom_count; r++) {
    if (s_custom_locations[r].label[0] == '\0') continue;   // skip the phantom
    if (w != r) s_custom_locations[w] = s_custom_locations[r];
    w++;
  }
  if (w != s_custom_count) {
    s_custom_count = w;
    prv_save_custom_locations();
  }
}

static void prv_save_custom_locations(void) {
  if (s_custom_count > 0 && !s_custom_locations) return;
  persist_write_int(SAVED_LOCATIONS_PERSIST_COUNT_KEY, s_custom_count);
  for (int i = 0; i < SAVED_LOCATIONS_MAX_CUSTOM; i++) {
    if (i < s_custom_count) {
      persist_write_string(SAVED_LOCATIONS_PERSIST_LABEL_KEY_BASE + i,
                           s_custom_locations[i].label);
      persist_write_string(SAVED_LOCATIONS_PERSIST_QUERY_KEY_BASE + i,
                           s_custom_locations[i].query);
      persist_write_int(SAVED_LOCATIONS_PERSIST_LAT_KEY_BASE + i,
                        s_custom_locations[i].latitude_e2);
      persist_write_int(SAVED_LOCATIONS_PERSIST_LON_KEY_BASE + i,
                        s_custom_locations[i].longitude_e2);
      persist_write_int(SAVED_LOCATIONS_PERSIST_HAS_COORDS_KEY_BASE + i,
                        s_custom_locations[i].has_coordinates ? 1 : 0);
    } else {
      persist_delete(SAVED_LOCATIONS_PERSIST_LABEL_KEY_BASE + i);
      persist_delete(SAVED_LOCATIONS_PERSIST_QUERY_KEY_BASE + i);
      persist_delete(SAVED_LOCATIONS_PERSIST_LAT_KEY_BASE + i);
      persist_delete(SAVED_LOCATIONS_PERSIST_LON_KEY_BASE + i);
      persist_delete(SAVED_LOCATIONS_PERSIST_HAS_COORDS_KEY_BASE + i);
    }
  }
}

static void prv_save_builtin_locations(void) {
  persist_write_int(SAVED_LOCATIONS_PERSIST_DELETED_PRESETS_KEY,
                    (int)s_deleted_preset_mask);
}

// ---- Watch -> phone: dictated-location sync (endpoint 6100) ----------------
// A dictated custom location is only a NAME — the watch can't geocode. This
// tells the mobile app about it so it can geocode the query, add it to its
// weather-location list, and sync back a v4 record whose lat/lon then pins
// the city on the globe (see prv_backfill_custom_coords).
//
// Wire format on Pebble Protocol endpoint 6100 (watch -> phone only):
//   uint8_t command;    // 1 = add/geocode this query, 2 = remove it
//   uint8_t query_len;  // UTF-8 byte count (no NUL terminator)
//   char    query[query_len];
//
// Delivery is best-effort: sends are marshalled to the system task (comm
// session objects must not be touched from the app task), a NULL session
// (phone disconnected) just drops the message, and every weather-app launch
// re-sends ADD for customs still missing coordinates — so the phone MUST
// treat ADD idempotently (same query twice = one location).
#define WEATHER_LOCATION_ENDPOINT 6100
#define WEATHER_LOCATION_CMD_ADD 1
#define WEATHER_LOCATION_CMD_REMOVE 2

typedef struct {
  uint8_t cmd;
  uint8_t len;
  char query[SAVED_LOCATION_QUERY_SIZE];
} PendingLocationMsg;   // kernel-heap, one per in-flight send

// Messages live on the KERNEL heap (not app heap — the system-task callback can
// run after the app exits; not firmware .bss — 536 B resident for a rare event).
// kernel_zalloc (NON-_check: on kernel OOM we drop the send silently, exactly the
// old queue-full behavior) + kernel_free in the callback and on queue failure.
static void prv_location_msg_send_cb(void *data) {
  PendingLocationMsg *msg = (PendingLocationMsg *)data;
  CommSession *session = comm_session_get_system_session();
  if (session) {
    uint8_t buf[2 + SAVED_LOCATION_QUERY_SIZE];
    buf[0] = msg->cmd;
    buf[1] = msg->len;
    memcpy(&buf[2], msg->query, msg->len);
    comm_session_send_data(session, WEATHER_LOCATION_ENDPOINT, buf,
                           (size_t)(2 + msg->len),
                           COMM_SESSION_DEFAULT_TIMEOUT);
  }
  kernel_free(msg);
}

static void prv_send_location_request(uint8_t cmd, const char *query) {
  if (!query || !query[0]) return;
  size_t len = strlen(query);
  if (len > SAVED_LOCATION_QUERY_SIZE) len = SAVED_LOCATION_QUERY_SIZE;
  PendingLocationMsg *msg = kernel_zalloc(sizeof(*msg));
  if (!msg) return;   // kernel OOM: drop; the launch re-send recovers ADDs
  msg->cmd = cmd;
  msg->len = (uint8_t)len;
  memcpy(msg->query, query, len);
  if (!system_task_add_callback(prv_location_msg_send_cb, msg)) {
    kernel_free(msg);   // queue full — drop; the launch re-send recovers ADDs
  }
}

// Graceful-exit reset: s_custom_locations lives on the APP heap but is cached
// in firmware statics — without this, the SECOND Weather launch per boot reads
// and writes through a dangling pointer (and can persist garbage). Called from
// the app's deinit; a force-killed app (deinit timeout) skips this, same as the
// other firmware statics here.
void saved_locations_reset(void) {
  if (s_custom_locations) {
    free(s_custom_locations);
    s_custom_locations = NULL;
  }
  s_custom_loaded = false;
  s_custom_count = 0;
}

void saved_locations_send_pending_queries(void) {
  prv_load_custom_locations();
  if (s_custom_count <= 0 || !s_custom_locations) return;
  for (int i = 0; i < s_custom_count; i++) {
    if (s_custom_locations[i].has_coordinates) continue;   // already pinned
    prv_send_location_request(WEATHER_LOCATION_CMD_ADD,
                              s_custom_locations[i].query);
  }
}

static int prv_find_custom_by_query(const char *query) {
  if (!query || !query[0]) return -1;
  prv_load_custom_locations();
  if (s_custom_count > 0 && !s_custom_locations) return -1;

  for (int i = 0; i < s_custom_count; i++) {
    if (strcmp(s_custom_locations[i].query, query) == 0 ||
        strcmp(s_custom_locations[i].label, query) == 0) {
      return i;
    }
  }

  return -1;
}

static void saved_locations_dismiss(bool animated);  // defined below; used by the dictation flow

// In-file only: called from the dictation flow below.
static void saved_locations_add_custom_location(const char *query, const char *label) {
  if (!query || !query[0]) return;
  prv_load_custom_locations();
  if (!prv_ensure_custom_locations()) return;

  int index = prv_find_custom_by_query(query);
  if (index < 0) {
    if (s_custom_count >= SAVED_LOCATIONS_MAX_CUSTOM) {
      memmove(&s_custom_locations[0], &s_custom_locations[1],
              sizeof(SavedCustomLocation) * (SAVED_LOCATIONS_MAX_CUSTOM - 1));
      s_custom_count = SAVED_LOCATIONS_MAX_CUSTOM - 1;
    }
    index = s_custom_count++;
    s_custom_locations[index].latitude_e2 = 0;
    s_custom_locations[index].longitude_e2 = 0;
    s_custom_locations[index].has_coordinates = false;
  }

  strncpy(s_custom_locations[index].query, query,
          sizeof(s_custom_locations[index].query) - 1);
  s_custom_locations[index].query[sizeof(s_custom_locations[index].query) - 1] = '\0';
  strncpy(s_custom_locations[index].label,
          (label && label[0]) ? label : query,
          sizeof(s_custom_locations[index].label) - 1);
  s_custom_locations[index].label[sizeof(s_custom_locations[index].label) - 1] = '\0';

  prv_save_custom_locations();
  if (s_view && s_view->menu_layer) {
    menu_layer_reload_data(s_view->menu_layer);
  }
}


// A voice-added custom location starts with a name only — the watch can't geocode.
// The phone geocodes it and syncs a weather-db record that DOES carry lat/lon, so
// lazily copy those coordinates into the custom slot (matched by name) and persist.
// This is what puts a dictated city like "Jamaica" on the globe: entries without
// coordinates can't be placed, so until the phone syncs its record the city only
// shows in this list, then appears on the globe on the next reveal.
static void prv_backfill_custom_coords(void) {
  prv_load_custom_locations();
  if (s_custom_count <= 0 || !s_custom_locations) return;
  bool any_missing = false;
  for (int i = 0; i < s_custom_count; i++) {
    if (!s_custom_locations[i].has_coordinates) { any_missing = true; break; }
  }
  if (!any_missing || !weather_ds_supported()) return;

  WxDsForecast *scratch = malloc_try(sizeof(*scratch));  // ~400 B; keep off the task stack
  if (!scratch) return;
  bool changed = false;
  const int count = weather_ds_location_count();
  // Two passes: PREFIX matches first, bare substring as a fallback — so a record
  // whose name merely CONTAINS the custom's name ("New York..." for a dictated
  // "York") can't claim it while a better record exists in the same sync.
  for (int pass = 0; pass < 2; pass++) {
    for (int r = 0; r < count; r++) {
      if (!weather_ds_read_index(r, scratch)) continue;
      if (scratch->is_current_location) continue;  // customs are never the current location
      // Only trust coordinates from a REAL v4 record: the phone geocoded these. The
      // v3-era placeholder seed fabricates coords (see WEATHER_V4_TEST_SEED), and
      // persisting those would pin the city in the wrong place with no self-heal.
      if (!scratch->is_v4) continue;
      if (scratch->latitude_e2 == INT16_MIN || scratch->longitude_e2 == INT16_MIN) continue;
      for (int i = 0; i < s_custom_count; i++) {
        SavedCustomLocation *c = &s_custom_locations[i];
        if (c->has_coordinates) continue;
        const bool match = (pass == 0)
            ? (weather_ds_name_prefix(scratch->location_name, c->query) ||
               weather_ds_name_prefix(scratch->location_name, c->label))
            : (weather_ds_name_matches(scratch->location_name, c->query) ||
               weather_ds_name_matches(scratch->location_name, c->label));
        if (match) {
          c->latitude_e2 = scratch->latitude_e2;
          c->longitude_e2 = scratch->longitude_e2;
          c->has_coordinates = true;
          changed = true;
        }
      }
    }
  }
  free(scratch);
  if (changed) prv_save_custom_locations();
}

int saved_locations_get_entries(SavedLocationEntry *entries,
                                int max_entries,
                                const char *current_location_label,
                                int16_t current_latitude_e2,
                                int16_t current_longitude_e2,
                                bool has_current_location) {
  if (!entries || max_entries <= 0) return 0;
  prv_load_custom_locations();
  prv_backfill_custom_coords();

  int count = 0;
  entries[count] = (SavedLocationEntry) {
    .kind = SavedLocationKindCurrent,
    .preset_index = -1,
    .latitude_e2 = current_latitude_e2,
    .longitude_e2 = current_longitude_e2,
    .has_coordinates = has_current_location,
  };
  strncpy(entries[count].label,
          (current_location_label && current_location_label[0])
              ? current_location_label
              : "Current Location",
          sizeof(entries[count].label) - 1);
  entries[count].label[sizeof(entries[count].label) - 1] = '\0';
  entries[count].query[0] = '\0';
  count++;

  for (int i = 0; i < CITY_PRESET_COUNT && count < max_entries; i++) {
    if (!prv_is_default_saved_preset(i)) continue;
    if (s_deleted_preset_mask & (1u << i)) continue;
    const CityPreset *preset = city_presets_get(i);
    if (!preset) continue;

    entries[count] = (SavedLocationEntry) {
      .kind = SavedLocationKindPreset,
      .preset_index = i,
      .latitude_e2 = preset->latitude_e2,
      .longitude_e2 = preset->longitude_e2,
      .has_coordinates = true,
    };
    city_presets_format_label(i, entries[count].label,
                              sizeof(entries[count].label));
    entries[count].query[0] = '\0';
    count++;
  }

  for (int i = 0; i < s_custom_count && count < max_entries; i++) {
    if (!s_custom_locations[i].has_coordinates) continue;
    entries[count] = (SavedLocationEntry) {
      .kind = SavedLocationKindCustom,
      .preset_index = -1,
      .latitude_e2 = s_custom_locations[i].latitude_e2,
      .longitude_e2 = s_custom_locations[i].longitude_e2,
      .has_coordinates = true,
    };
    strncpy(entries[count].label, s_custom_locations[i].label,
            sizeof(entries[count].label) - 1);
    entries[count].label[sizeof(entries[count].label) - 1] = '\0';
    strncpy(entries[count].query, s_custom_locations[i].query,
            sizeof(entries[count].query) - 1);
    entries[count].query[sizeof(entries[count].query) - 1] = '\0';
    count++;
  }

  return count;
}

// A custom slot with an empty label is a phantom (count/persist desync). It must
// never occupy a row, or it draws as a blank "ghost" cell (user-reported after a
// voice-add). prv_compact_customs() heals the persisted store on load; these two
// helpers make the row model count/map only real slots, so even a phantom created
// mid-session (before the next compaction) can't render.
static bool prv_custom_slot_valid(int i) {
  return s_custom_locations && i >= 0 && i < s_custom_count &&
         s_custom_locations[i].label[0] != '\0';
}

static int prv_valid_custom_count(void) {
  int n = 0;
  for (int i = 0; i < s_custom_count; i++) {
    if (prv_custom_slot_valid(i)) n++;
  }
  return n;
}

static int prv_num_rows(void) {
  prv_load_custom_locations();
  int rows = 2;  // add row + the always-visible Current Location row
  for (int i = 0; i < CITY_PRESET_COUNT; i++) {
    if (!prv_is_default_saved_preset(i)) continue;
    if ((s_deleted_preset_mask & (1u << i)) == 0) rows++;
  }
  return rows + prv_valid_custom_count();
}

static int prv_visible_preset_count(void) {
  int count = 0;
  for (int i = 0; i < CITY_PRESET_COUNT; i++) {
    if (!prv_is_default_saved_preset(i)) continue;
    if ((s_deleted_preset_mask & (1u << i)) == 0) count++;
  }
  return count;
}

static int prv_current_row(void) {
  return 1;   // the Current Location row is always visible
}

static int prv_preset_start_row(void) {
  return 2;   // add row + Current Location
}

static int prv_custom_start_row(void) {
  return prv_preset_start_row() + prv_visible_preset_count();
}

static bool prv_row_is_current(int row) {
  return row == prv_current_row();
}

static int prv_preset_index_for_row(int row) {
  int visible_row = prv_preset_start_row();
  for (int i = 0; i < CITY_PRESET_COUNT; i++) {
    if (!prv_is_default_saved_preset(i)) continue;
    if (s_deleted_preset_mask & (1u << i)) continue;
    if (row == visible_row) return i;
    visible_row++;
  }
  return -1;
}

static int prv_row_for_preset_index(int preset_index) {
  if (preset_index < 0 || preset_index >= CITY_PRESET_COUNT ||
      !prv_is_default_saved_preset(preset_index) ||
      (s_deleted_preset_mask & (1u << preset_index))) {
    return -1;
  }

  int visible_row = prv_preset_start_row();
  for (int i = 0; i < CITY_PRESET_COUNT; i++) {
    if (!prv_is_default_saved_preset(i)) continue;
    if (s_deleted_preset_mask & (1u << i)) continue;
    if (i == preset_index) return visible_row;
    visible_row++;
  }
  return -1;
}

// Map a list row to a real custom slot, skipping phantom (empty-label) slots so
// the k-th custom ROW resolves to the k-th VALID slot (not the k-th array slot).
static int prv_custom_index_for_row(int row) {
  int k = row - prv_custom_start_row();
  if (k < 0) return -1;
  for (int i = 0; i < s_custom_count; i++) {
    if (!prv_custom_slot_valid(i)) continue;
    if (k == 0) return i;
    k--;
  }
  return -1;
}

static bool prv_row_is_custom(int row) {
  return prv_custom_index_for_row(row) >= 0;
}

static void prv_draw_plus_row(GContext *ctx, const Layer *cell_layer) {
  GRect bounds = layer_get_bounds(cell_layer);
  GColor color = menu_cell_layer_is_highlighted(cell_layer)
                     ? GColorWhite
                     : PBL_IF_COLOR_ELSE(GColorLightGray, GColorBlack);
  graphics_context_set_fill_color(ctx, color);

  const int size = 18;
  const int thickness = 4;
  GPoint center = GPoint(bounds.size.w / 2, bounds.size.h / 2);
  graphics_fill_rect(ctx,
                     GRect(center.x - thickness / 2, center.y - size / 2,
                           thickness, size),
                     0, GCornerNone);
  graphics_fill_rect(ctx,
                     GRect(center.x - size / 2, center.y - thickness / 2,
                           size, thickness),
                     0, GCornerNone);
}

static uint16_t prv_get_num_sections(MenuLayer *menu_layer, void *context) {
  (void)menu_layer;
  (void)context;
  return 1;
}

static uint16_t prv_get_num_rows(MenuLayer *menu_layer, uint16_t section,
                                 void *context) {
  (void)menu_layer;
  (void)section;
  (void)context;
  return prv_num_rows();
}

static int16_t prv_get_cell_height(MenuLayer *menu_layer, MenuIndex *cell_index,
                                   void *context) {
  (void)menu_layer;
  (void)cell_index;
  (void)context;
  return SAVED_LOCATIONS_ROW_HEIGHT;
}

// Dense "at a glance" row: [condition icon] City name          21°
// Falls back to the classic two-line menu cell when the city has no synced
// weather snapshot.
static void prv_draw_glance_row(GContext *ctx, const Layer *cell_layer,
                                const char *title, const char *subtitle,
                                const GlanceEntry *glance) {
  if (!glance) {
    menu_cell_basic_draw(ctx, cell_layer, title, subtitle, NULL);
    return;
  }

  GRect bounds = layer_get_bounds(cell_layer);
  const bool highlighted = menu_cell_layer_is_highlighted(cell_layer);
  GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
  graphics_context_set_text_color(ctx,
                                  highlighted ? GColorWhite : GColorBlack);

  char temp_text[12];
  snprintf(temp_text, sizeof(temp_text), "%d°", (int)glance->temp);
  const int temp_w = 48;
  const int text_y = (bounds.size.h - 28) / 2 - 3;
  graphics_draw_text(ctx, temp_text, font,
                     GRect(bounds.size.w - temp_w - 4, text_y, temp_w, 30),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentRight,
                     NULL);

  int title_x = 8;
  GBitmap *icon = prv_glance_icon(glance->type);
  if (icon) {
    graphics_context_set_compositing_mode(ctx, GCompOpSet);
    graphics_draw_bitmap_in_rect(
        ctx, icon, GRect(5, (bounds.size.h - 25) / 2, 25, 25));
    title_x = 5 + 25 + 5;
  }

  graphics_draw_text(ctx, title, font,
                     GRect(title_x, text_y,
                           bounds.size.w - title_x - temp_w - 8, 30),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft,
                     NULL);
}

static void prv_draw_row(GContext *ctx, const Layer *cell_layer,
                         MenuIndex *cell_index, void *context) {
  SavedLocationsView *view = (SavedLocationsView *)context;
  int row = cell_index->row;

  if (row == SAVED_LOCATIONS_ROW_ADD) {
    prv_draw_plus_row(ctx, cell_layer);
    return;
  }

  if (prv_row_is_current(row)) {
    const char *title = view->current_location_label[0]
                            ? view->current_location_label
                            : "Current Location";
    prv_draw_glance_row(ctx, cell_layer, title, "Current Location",
                        prv_glance_find(NULL, true));
    return;
  }

  int preset_index = prv_preset_index_for_row(row);
  if (preset_index >= 0) {
    const CityPreset *preset = city_presets_get(preset_index);
    if (preset) {
      prv_draw_glance_row(ctx, cell_layer, preset->city, preset->country,
                          prv_glance_find(preset->city, false));
    }
    return;
  }

  if (prv_row_is_custom(row)) {
    int custom_index = prv_custom_index_for_row(row);
    // NULL subtitle: a custom city without a synced glance draws as a clean
    // single-line cell — the old "Saved Location" category line underneath
    // read as its own hoverable row (user-reported).
    prv_draw_glance_row(ctx, cell_layer,
                        s_custom_locations[custom_index].label,
                        NULL,
                        prv_glance_find(s_custom_locations[custom_index].label,
                                        false));
  }
}

static void prv_activate_saved_row(SavedLocationsView *view, int row) {
  if (!view) return;

  if (row == SAVED_LOCATIONS_ROW_ADD) {
    if (!view->dictation_session) {
      view->dictation_session = dictation_session_create(64,
                                                         prv_dictation_callback,
                                                         view);
      if (view->dictation_session) {
        dictation_session_enable_confirmation(view->dictation_session, true);
        dictation_session_enable_error_dialogs(view->dictation_session, true);
      }
    }
    if (view->dictation_session) {
      dictation_session_start(view->dictation_session);
    }
    return;
  }

  if (prv_row_is_current(row)) {
    vibes_short_pulse();
    return;
  }

  int preset_index = prv_preset_index_for_row(row);
  if (preset_index >= 0) {
    const CityPreset *preset = city_presets_get(preset_index);
    if (preset) {
      char label[SAVED_LOCATION_LABEL_SIZE];
      snprintf(label, sizeof(label), "%s, %s",
               preset->city, preset->country);
      prv_push_delete_confirm(view, SavedLocationKindPreset,
                             row, preset_index, -1, label);
    }
    return;
  }

  if (prv_row_is_custom(row)) {
    int custom_index = prv_custom_index_for_row(row);
    prv_push_delete_confirm(view, SavedLocationKindCustom,
                           row, -1, custom_index,
                           s_custom_locations[custom_index].label);
  }
}

#if WEATHER_PLATFORM_TOUCH_COLOR
static uint32_t prv_now_ms(void) {
  time_t s = 0;
  uint16_t ms = 0;
  time_ms(&s, &ms);
  return (uint32_t)s * 1000u + ms;
}

static int prv_touch_max_scroll(SavedLocationsView *view) {
  if (!view || !view->menu_layer) return 0;
  int rows = prv_num_rows();
  GRect bounds = layer_get_bounds(menu_layer_get_layer(view->menu_layer));
  int max_scroll = rows * SAVED_LOCATIONS_ROW_HEIGHT - bounds.size.h;
  return max_scroll > 0 ? max_scroll : 0;
}

static int prv_touch_clamp_scroll(SavedLocationsView *view, int amount) {
  if (amount < 0) return 0;
  int max_scroll = prv_touch_max_scroll(view);
  return amount > max_scroll ? max_scroll : amount;
}

static int prv_touch_scroll_amount(SavedLocationsView *view) {
  if (!view || !view->menu_layer) return 0;
  ScrollLayer *scroll_layer = menu_layer_get_scroll_layer(view->menu_layer);
  if (!scroll_layer) return 0;
  return -scroll_layer_get_content_offset(scroll_layer).y;
}

static void prv_touch_set_scroll(SavedLocationsView *view, int amount,
                                 bool animated) {
  if (!view || !view->menu_layer) return;
  ScrollLayer *scroll_layer = menu_layer_get_scroll_layer(view->menu_layer);
  if (!scroll_layer) return;
  amount = prv_touch_clamp_scroll(view, amount);
  scroll_layer_set_content_offset(scroll_layer, GPoint(0, -amount), animated);
}

static void prv_touch_select_row(SavedLocationsView *view, int row) {
  if (!view || !view->menu_layer) return;
  int rows = prv_num_rows();
  if (rows <= 0) return;
  if (row < 0) row = 0;
  if (row >= rows) row = rows - 1;
  menu_layer_set_selected_index(view->menu_layer, MenuIndex(0, row),
                                MenuRowAlignNone, false);
}

static int prv_touch_row_at_y(SavedLocationsView *view, int16_t y) {
  int row = (prv_touch_scroll_amount(view) + y) / SAVED_LOCATIONS_ROW_HEIGHT;
  int rows = prv_num_rows();
  if (row < 0 || row >= rows) return -1;
  return row;
}

static void prv_touch_handler(const TouchEvent *event, void *context) {
  SavedLocationsView *view = (SavedLocationsView *)context;
  if (!view || !view->menu_layer || view->result_window) {
    return;
  }

  if (view->confirm_window) {
    if (event->type == TouchEvent_Liftoff) {
      prv_confirm_delete_click(NULL, view);
    }
    return;
  }

  if (event->type == TouchEvent_Touchdown) {
    view->touch_active = true;
    view->touch_start_x = event->x;
    view->touch_start_y = event->y;
    view->scroll_at_drag_start = prv_touch_scroll_amount(view);
    view->drag_axis_set = false;
    view->drag_is_vertical = false;
    view->last_drag_y = event->y;
    view->last_drag_ms = prv_now_ms();
    view->fling_velocity = 0;
    // Phone-style: don't highlight a row on touchdown. Only a deliberate tap
    // selects (decided on liftoff) — this stops the selection from jumping to
    // wherever your finger lands while you're really just starting a scroll.
    return;
  }

  if (event->type == TouchEvent_PositionUpdate && view->touch_active) {
    int16_t dx = event->x - view->touch_start_x;
    int16_t dy = event->y - view->touch_start_y;
    int16_t adx = dx < 0 ? -dx : dx;
    int16_t ady = dy < 0 ? -dy : dy;

    if (!view->drag_axis_set &&
        (adx > SAVED_LOCATIONS_TOUCH_AXIS_THRESHOLD_PX ||
         ady > SAVED_LOCATIONS_TOUCH_AXIS_THRESHOLD_PX)) {
      view->drag_axis_set = true;
      view->drag_is_vertical = ady >= adx;
    }

    if (view->drag_is_vertical) {
      // 1:1 finger tracking.
      int amount = prv_touch_clamp_scroll(
          view, view->scroll_at_drag_start - (int)dy);
      prv_touch_set_scroll(view, amount, false);
      // Track finger velocity over the latest segment (px/s, down = +) for the
      // release fling. Light smoothing rejects single-sample jitter; if the finger
      // pauses before lifting, velocity decays to ~0 so there's no fling (correct).
      uint32_t now = prv_now_ms();
      uint32_t seg_dt = now - view->last_drag_ms;
      if (seg_dt > 0) {
        int seg_v = ((int)(event->y - view->last_drag_y) * 1000) / (int)seg_dt;
        view->fling_velocity = (view->fling_velocity + seg_v * 2) / 3;
        view->last_drag_y = event->y;
        view->last_drag_ms = now;
      }
    }
    return;
  }

  if (event->type == TouchEvent_Liftoff && view->touch_active) {
    view->touch_active = false;
    int16_t dx = event->x - view->touch_start_x;
    int16_t dy = event->y - view->touch_start_y;
    if (dx >= -SAVED_LOCATIONS_TOUCH_TAP_THRESHOLD_PX &&
        dx <= SAVED_LOCATIONS_TOUCH_TAP_THRESHOLD_PX &&
        dy >= -SAVED_LOCATIONS_TOUCH_TAP_THRESHOLD_PX &&
        dy <= SAVED_LOCATIONS_TOUCH_TAP_THRESHOLD_PX) {
      int row = prv_touch_row_at_y(view, event->y);
      if (row >= 0) {
        prv_touch_select_row(view, row);
        prv_activate_saved_row(view, row);
      }
      return;
    }

    if (view->drag_axis_set && !view->drag_is_vertical &&
        dx > SAVED_LOCATIONS_TOUCH_SWIPE_BACK_PX) {
      window_stack_pop(true);   // swipe right = BACK to the globe cradle
      return;
    }

    if (view->drag_is_vertical) {
      // Momentum: coast in the fling direction and ease to a stop. Project a
      // target from the release velocity; the scroll layer's animated move
      // decelerates into it (a fast flick travels further than a gentle one).
      int v = view->fling_velocity;          // px/s, down = +
      int av = v < 0 ? -v : v;
      int target = prv_touch_scroll_amount(view);
      if (av >= SAVED_LOCATIONS_FLING_MIN_VELOCITY) {
        int dist = (v * SAVED_LOCATIONS_FLING_PROJECT_MS) / 1000;
        target = prv_touch_clamp_scroll(view, target - dist);
        prv_touch_set_scroll(view, target, true);   // animated = ease-out deceleration
      } else {
        prv_touch_set_scroll(view, prv_touch_clamp_scroll(view, target), false);
      }
    }
  }
}
#endif

static void prv_delete_custom_at_index(int custom_index) {
  if (custom_index < 0 || custom_index >= s_custom_count) return;

  prv_send_location_request(WEATHER_LOCATION_CMD_REMOVE,
                            s_custom_locations[custom_index].query);
  if (custom_index + 1 < s_custom_count) {
    memmove(&s_custom_locations[custom_index],
            &s_custom_locations[custom_index + 1],
            sizeof(SavedCustomLocation) *
                (s_custom_count - custom_index - 1));
  }
  s_custom_count--;
  prv_save_custom_locations();
  if (s_view && s_view->menu_layer) {
    menu_layer_reload_data(s_view->menu_layer);
  }
}

static void prv_delete_pending_location(SavedLocationsView *view) {
  if (!view) return;

  if (view->pending_delete_kind == SavedLocationKindCurrent) {
    return;
  } else if (view->pending_delete_kind == SavedLocationKindPreset) {
    int preset_index = view->pending_delete_preset_index;
    if (preset_index >= 0 && preset_index < CITY_PRESET_COUNT) {
      s_deleted_preset_mask |= (uint16_t)(1u << preset_index);
      prv_save_builtin_locations();
    }
  } else if (view->pending_delete_kind == SavedLocationKindCustom) {
    prv_delete_custom_at_index(view->pending_delete_custom_index);
  }

  if (view->menu_layer) {
    menu_layer_reload_data(view->menu_layer);
    int rows = prv_num_rows();
    if (rows > 0) {
      int row = view->pending_delete_row;
      if (row < 0 || row >= rows) row = rows - 1;
      menu_layer_set_selected_index(view->menu_layer,
                                    MenuIndex(0, row),
                                    MenuRowAlignCenter, false);
    }
  }
}

static void prv_result_timer_cb(void *context) {
  SavedLocationsView *view = (SavedLocationsView *)context;
  if (!view || !view->result_window || !view->result_layer) return;

  view->result_timer = NULL;
  uint32_t total_ms = view->shred_sequence
                          ? gdraw_command_sequence_get_total_duration(
                                view->shred_sequence)
                          : 0;
  view->result_elapsed_ms += SAVED_LOCATIONS_RESULT_TICK_MS;
  layer_mark_dirty(view->result_layer);

  uint32_t done_ms = total_ms + SAVED_LOCATIONS_RESULT_HOLD_MS;
  if (view->result_elapsed_ms < done_ms) {
    view->result_timer = app_timer_register(SAVED_LOCATIONS_RESULT_TICK_MS,
                                            prv_result_timer_cb, view);
  } else {
    window_stack_remove(view->result_window, true);
  }
}

static GFont prv_result_text_font(void) {
#if PBL_DISPLAY_HEIGHT >= 200
  return fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);
#else
  return fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
#endif
}

static void prv_result_layout(GRect bounds, GSize icon_size,
                              GPoint *icon_origin, GRect *text_box) {
#if PBL_DISPLAY_HEIGHT >= 200
  int text_max_h = 74;
  int icon_top_default = 42;
  int spacing = PBL_IF_ROUND_ELSE(8, 16);
#else
  int text_max_h = 60;
  int icon_top_default = 18;
  int spacing = PBL_IF_ROUND_ELSE(2, 4);
#endif
  int claimed_h = icon_size.h + text_max_h;
  int adjusted = bounds.size.h > claimed_h ? bounds.size.h - claimed_h : 0;
  int icon_top = adjusted < icon_top_default
                     ? adjusted
                     : (adjusted / 2 > icon_top_default
                            ? adjusted / 2
                            : icon_top_default);
  int text_y = icon_top + (icon_size.h > 6 ? icon_size.h : 6) + spacing;

  // Alarm's SimpleDialog nudges single-line result text down with the icon.
  icon_top += 13;
  text_y += 12;

  int text_x = PBL_IF_RECT_ELSE(6, 0);
  int text_w = bounds.size.w - PBL_IF_RECT_ELSE(12, 0);
  *icon_origin = GPoint((bounds.size.w - icon_size.w) / 2, icon_top);
  *text_box = GRect(text_x, text_y, text_w, text_max_h);
}

static void prv_result_layer_draw(Layer *layer, GContext *ctx) {
  SavedLocationsView *view = *(SavedLocationsView **)layer_get_data(layer);
  GRect bounds = layer_get_bounds(layer);
  graphics_context_set_fill_color(ctx,
                                  PBL_IF_COLOR_ELSE(GColorVividCerulean,
                                                    GColorBlack));
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  if (view && view->shred_sequence) {
    uint32_t total_ms = gdraw_command_sequence_get_total_duration(
        view->shred_sequence);
    uint32_t elapsed = view->result_elapsed_ms;
    if (total_ms > 0 && elapsed >= total_ms) elapsed = total_ms - 1;
    GDrawCommandFrame *frame =
        gdraw_command_sequence_get_frame_by_elapsed(view->shred_sequence,
                                                    elapsed);
    if (frame) {
      GSize size = gdraw_command_sequence_get_bounds_size(view->shred_sequence);
      GPoint origin;
      GRect text_box;
      prv_result_layout(bounds, size, &origin, &text_box);
      gdraw_command_frame_draw(ctx, view->shred_sequence, frame, origin);
    }
  }

  GPoint icon_origin;
  GRect text_box;
  prv_result_layout(bounds, GSize(80, 80), &icon_origin, &text_box);
  (void)icon_origin;
  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, "Location Deleted",
                     prv_result_text_font(),
                     text_box,
                     GTextOverflowModeWordWrap,
                     GTextAlignmentCenter, NULL);
}

static void prv_push_delete_result(SavedLocationsView *view) {
  if (!view || view->result_window) return;

  view->result_elapsed_ms = 0;
  view->result_window = window_create();
  if (!view->result_window) return;
  window_set_user_data(view->result_window, view);
  window_set_background_color(view->result_window,
                              PBL_IF_COLOR_ELSE(GColorVividCerulean,
                                                GColorBlack));
  window_set_window_handlers(view->result_window, (WindowHandlers) {
    .unload = prv_result_window_unload,
  });

  GRect bounds = layer_get_bounds(window_get_root_layer(view->result_window));
  view->result_layer = layer_create_with_data(bounds, sizeof(SavedLocationsView *));
  if (!view->result_layer) {
    window_destroy(view->result_window);
    view->result_window = NULL;
    return;
  }
  *(SavedLocationsView **)layer_get_data(view->result_layer) = view;
  layer_set_update_proc(view->result_layer, prv_result_layer_draw);
  layer_add_child(window_get_root_layer(view->result_window),
                  view->result_layer);

  view->shred_sequence =
      gdraw_command_sequence_create_with_resource(
          RESOURCE_ID_RESULT_SHREDDED_LARGE);
  vibes_short_pulse();
  window_stack_push(view->result_window, true);
  view->result_timer = app_timer_register(SAVED_LOCATIONS_RESULT_TICK_MS,
                                          prv_result_timer_cb, view);
}

static void prv_confirm_layer_draw(Layer *layer, GContext *ctx) {
  SavedLocationsView *view = *(SavedLocationsView **)layer_get_data(layer);
  GRect bounds = layer_get_bounds(layer);

  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);
  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, "Delete?",
                     fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD),
                     GRect(0, 28, bounds.size.w, 34),
                     GTextOverflowModeTrailingEllipsis,
                     GTextAlignmentCenter, NULL);
  graphics_draw_text(ctx, view ? view->pending_delete_label : "",
                     fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                     GRect(12, 66, bounds.size.w - 24, 42),
                     GTextOverflowModeTrailingEllipsis,
                     GTextAlignmentCenter, NULL);

  GRect delete_bar = GRect(0, bounds.size.h - 34, bounds.size.w, 34);
  graphics_context_set_fill_color(ctx,
                                  PBL_IF_COLOR_ELSE(GColorVividCerulean,
                                                    GColorBlack));
  graphics_fill_rect(ctx, delete_bar, 0, GCornerNone);
  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, "Delete",
                     fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
                     GRect(0, delete_bar.origin.y + 2, bounds.size.w, 28),
                     GTextOverflowModeTrailingEllipsis,
                     GTextAlignmentCenter, NULL);
}

static void prv_confirm_delete_click(ClickRecognizerRef recognizer,
                                     void *context) {
  (void)recognizer;
  SavedLocationsView *view = (SavedLocationsView *)context;
  if (!view) return;

  prv_delete_pending_location(view);
  if (view->confirm_window) {
    window_stack_remove(view->confirm_window, false);
  }
  prv_push_delete_result(view);
}

static void prv_confirm_cancel_click(ClickRecognizerRef recognizer,
                                     void *context) {
  (void)recognizer;
  SavedLocationsView *view = (SavedLocationsView *)context;
  if (view && view->confirm_window) {
    window_stack_remove(view->confirm_window, true);
  }
}

static void prv_confirm_click_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, prv_confirm_delete_click);
  window_set_click_context(BUTTON_ID_SELECT, context);
  window_single_click_subscribe(BUTTON_ID_BACK, prv_confirm_cancel_click);
  window_set_click_context(BUTTON_ID_BACK, context);
}

static void prv_push_delete_confirm(SavedLocationsView *view,
                                    SavedLocationKind kind,
                                    int row,
                                    int preset_index,
                                    int custom_index,
                                    const char *label) {
  if (!view || !label || !label[0]) return;
  if (kind == SavedLocationKindPreset &&
      (preset_index < 0 || preset_index >= CITY_PRESET_COUNT)) {
    return;
  }
  if (kind == SavedLocationKindCustom &&
      (custom_index < 0 || custom_index >= s_custom_count)) {
    return;
  }
  if (view->confirm_window) return;

  view->pending_delete_kind = kind;
  view->pending_delete_row = row;
  view->pending_delete_preset_index = preset_index;
  view->pending_delete_custom_index = custom_index;
  strncpy(view->pending_delete_label, label,
          sizeof(view->pending_delete_label) - 1);
  view->pending_delete_label[sizeof(view->pending_delete_label) - 1] = '\0';

  view->confirm_window = window_create();
  if (!view->confirm_window) return;
  window_set_user_data(view->confirm_window, view);
  window_set_background_color(view->confirm_window, GColorWhite);
  window_set_window_handlers(view->confirm_window, (WindowHandlers) {
    .unload = prv_confirm_window_unload,
  });
  window_set_click_config_provider_with_context(view->confirm_window,
                                                prv_confirm_click_provider,
                                                view);

  GRect bounds = layer_get_bounds(window_get_root_layer(view->confirm_window));
  view->confirm_layer = layer_create_with_data(bounds, sizeof(SavedLocationsView *));
  if (!view->confirm_layer) {
    window_destroy(view->confirm_window);
    view->confirm_window = NULL;
    return;
  }
  *(SavedLocationsView **)layer_get_data(view->confirm_layer) = view;
  layer_set_update_proc(view->confirm_layer, prv_confirm_layer_draw);
  layer_add_child(window_get_root_layer(view->confirm_window),
                  view->confirm_layer);
  window_stack_push(view->confirm_window, true);
}

static void prv_select_click(MenuLayer *menu_layer, MenuIndex *cell_index,
                             void *context) {
  (void)menu_layer;
  SavedLocationsView *view = (SavedLocationsView *)context;
  if (!view || !cell_index) return;
  prv_activate_saved_row(view, cell_index->row);
}

static void prv_dictation_callback(DictationSession *session,
                                   DictationSessionStatus status,
                                   char *transcription,
                                   void *context) {
  (void)session;
  SavedLocationsView *view = (SavedLocationsView *)context;
  if (!view || status != DictationSessionStatusSuccess ||
      !transcription || !transcription[0]) {
    return;
  }

  saved_locations_add_custom_location(transcription, transcription);
  prv_send_location_request(WEATHER_LOCATION_CMD_ADD, transcription);
  if (view->select_callback) {
    view->select_callback(SavedLocationKindCustom, -1, transcription,
                          view->select_context);
  }
  saved_locations_dismiss(true);
}

static void prv_confirm_window_unload(Window *window) {
  SavedLocationsView *view = (SavedLocationsView *)window_get_user_data(window);
  if (!view) return;

  if (view->confirm_layer) {
    layer_destroy(view->confirm_layer);
    view->confirm_layer = NULL;
  }
  window_destroy(view->confirm_window);
  view->confirm_window = NULL;
  view->pending_delete_kind = SavedLocationKindCustom;
  view->pending_delete_row = -1;
  view->pending_delete_preset_index = -1;
  view->pending_delete_custom_index = -1;
  view->pending_delete_label[0] = '\0';
}

static void prv_result_window_unload(Window *window) {
  SavedLocationsView *view = (SavedLocationsView *)window_get_user_data(window);
  if (!view) return;

  if (view->result_timer) {
    app_timer_cancel(view->result_timer);
    view->result_timer = NULL;
  }
  if (view->shred_sequence) {
    gdraw_command_sequence_destroy(view->shred_sequence);
    view->shred_sequence = NULL;
  }
  if (view->result_layer) {
    layer_destroy(view->result_layer);
    view->result_layer = NULL;
  }
  window_destroy(view->result_window);
  view->result_window = NULL;
  view->result_elapsed_ms = 0;
}

static void prv_window_unload(Window *window) {
  SavedLocationsView *view = (SavedLocationsView *)window_get_user_data(window);
  if (!view) return;

  if (view->confirm_window) {
    window_stack_remove(view->confirm_window, false);
  }
  if (view->result_window) {
    window_stack_remove(view->result_window, false);
  }
  if (view->dictation_session) {
    dictation_session_destroy(view->dictation_session);
    view->dictation_session = NULL;
  }
  if (view->menu_layer) {
    menu_layer_destroy(view->menu_layer);
    view->menu_layer = NULL;
  }
  prv_glance_destroy_icons();
  prv_glance_free();
#if WEATHER_PLATFORM_TOUCH_COLOR
  touch_service_unsubscribe();
#endif
  window_destroy(view->window);
  s_view = NULL;
  free(view);
}

#if WEATHER_PLATFORM_TOUCH_COLOR
// Subscribe on APPEAR (not before the push): the covered window's disappear handler fires
// during the push transition and releases the single touch slot — a pre-push subscribe
// would be clobbered by it. Appear runs after every disappear/unload in the transition.
static void prv_window_appear(Window *window) {
  SavedLocationsView *view = (SavedLocationsView *)window_get_user_data(window);
  if (view) touch_service_subscribe(prv_touch_handler, view);
}
#endif

// In-file only: called from the dictation flow.
static void saved_locations_dismiss(bool animated) {
  if (!s_view || !s_view->window) return;
  window_stack_remove(s_view->window, animated);
}

void saved_locations_push(const SavedLocationsConfig *config) {
  if (s_view) {
    if (config) {
      if (config->current_location_label) {
        strncpy(s_view->current_location_label,
                config->current_location_label,
                sizeof(s_view->current_location_label) - 1);
        s_view->current_location_label[
            sizeof(s_view->current_location_label) - 1] = '\0';
      }
      s_view->active_city_index = config->active_city_index;
      s_view->select_callback = config->select_callback;
      s_view->select_context = config->select_context;
    }
    s_view->touch_active = false;
    s_view->drag_axis_set = false;
    s_view->drag_is_vertical = false;
    s_view->scroll_at_drag_start = 0;
    s_view->touch_start_x = 0;
    s_view->touch_start_y = 0;
    menu_layer_reload_data(s_view->menu_layer);
    window_stack_push(s_view->window, true);   // touch subscribe happens in .appear
    return;
  }

  SavedLocationsView *view = calloc(1, sizeof(SavedLocationsView));
  if (!view) return;
  s_view = view;
  prv_load_custom_locations();
  prv_glance_refresh();

  view->active_city_index = config ? config->active_city_index : -1;
  view->pending_delete_custom_index = -1;
  view->select_callback = config ? config->select_callback : NULL;
  view->select_context = config ? config->select_context : NULL;
  if (config && config->current_location_label) {
    strncpy(view->current_location_label, config->current_location_label,
            sizeof(view->current_location_label) - 1);
    view->current_location_label[sizeof(view->current_location_label) - 1] = '\0';
  }
  if (config && config->active_custom_query) {
    strncpy(view->active_custom_query, config->active_custom_query,
            sizeof(view->active_custom_query) - 1);
    view->active_custom_query[sizeof(view->active_custom_query) - 1] = '\0';
  }

  view->window = window_create();
  if (!view->window) {
    free(view);
    s_view = NULL;
    return;
  }
  window_set_user_data(view->window, view);
  window_set_background_color(view->window, GColorWhite);
  window_set_window_handlers(view->window, (WindowHandlers) {
#if WEATHER_PLATFORM_TOUCH_COLOR
    .appear = prv_window_appear,
#endif
    .unload = prv_window_unload,
  });

  Layer *root = window_get_root_layer(view->window);
  GRect bounds = layer_get_bounds(root);
  view->menu_layer = menu_layer_create(bounds);
  if (!view->menu_layer) {
    window_destroy(view->window);
    free(view);
    s_view = NULL;
    return;
  }

  menu_layer_set_callbacks(view->menu_layer, view, (MenuLayerCallbacks) {
    .get_num_sections = prv_get_num_sections,
    .get_num_rows = prv_get_num_rows,
    .get_cell_height = prv_get_cell_height,
    .draw_row = prv_draw_row,
    .select_click = prv_select_click,
  });
  menu_layer_set_normal_colors(view->menu_layer, GColorWhite, GColorBlack);
  menu_layer_set_highlight_colors(view->menu_layer,
                                  PBL_IF_COLOR_ELSE(GColorVividCerulean, GColorBlack),
                                  GColorWhite);
  menu_layer_set_click_config_onto_window(view->menu_layer, view->window);
  layer_add_child(root, menu_layer_get_layer(view->menu_layer));

  int selected_row = prv_current_row();
  if (view->active_city_index >= 0) {
    int row = prv_row_for_preset_index(view->active_city_index);
    if (row >= 0) selected_row = row;
  } else if (view->active_custom_query[0]) {
    int custom_index = prv_find_custom_by_query(view->active_custom_query);
    if (custom_index >= 0) {
      selected_row = prv_custom_start_row() + custom_index;
    }
  }
  if (selected_row < 0 || selected_row >= prv_num_rows()) {
    selected_row = SAVED_LOCATIONS_ROW_ADD;
  }
  menu_layer_set_selected_index(view->menu_layer,
                                MenuIndex(0, selected_row),
                                MenuRowAlignCenter, false);

  // (dictation session is created lazily on the + row — the eager duplicate is gone)
  window_stack_push(view->window, true);
}
