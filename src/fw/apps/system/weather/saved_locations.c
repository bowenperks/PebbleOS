/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "saved_locations.h"

#include "city_presets.h"
#include "resource_ids.pin.h"

#define SAVED_LOCATIONS_PERSIST_COUNT_KEY 6100
#define SAVED_LOCATIONS_PERSIST_LABEL_KEY_BASE 6110
#define SAVED_LOCATIONS_PERSIST_QUERY_KEY_BASE 6120
#define SAVED_LOCATIONS_PERSIST_DELETED_PRESETS_KEY 6130
#define SAVED_LOCATIONS_PERSIST_CURRENT_VISIBLE_KEY 6131
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
static bool s_current_visible;
static bool s_custom_loaded;

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

static void prv_load_custom_locations(void) {
  if (s_custom_loaded) return;
  s_custom_loaded = true;

  s_custom_count = 0;
  s_deleted_preset_mask = 0;
  s_current_visible = true;

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
  s_current_visible = true;
  persist_write_int(SAVED_LOCATIONS_PERSIST_CURRENT_VISIBLE_KEY, 1);

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
  persist_write_int(SAVED_LOCATIONS_PERSIST_CURRENT_VISIBLE_KEY,
                    s_current_visible ? 1 : 0);
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

void saved_locations_add_custom_location(const char *query, const char *label) {
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

void saved_locations_update_custom_label(const char *query, const char *label) {
  if (!label || !label[0]) return;
  prv_load_custom_locations();

  int index = prv_find_custom_by_query(query);
  if (index < 0 && s_custom_count > 0) {
    index = s_custom_count - 1;
  }
  if (index < 0) return;

  strncpy(s_custom_locations[index].label, label,
          sizeof(s_custom_locations[index].label) - 1);
  s_custom_locations[index].label[sizeof(s_custom_locations[index].label) - 1] = '\0';
  prv_save_custom_locations();
  if (s_view && s_view->menu_layer) {
    menu_layer_reload_data(s_view->menu_layer);
  }
}

void saved_locations_update_custom_details(const char *query,
                                           const char *label,
                                           int16_t latitude_e2,
                                           int16_t longitude_e2) {
  if (!query || !query[0]) return;
  saved_locations_add_custom_location(query, (label && label[0]) ? label : query);
  prv_load_custom_locations();

  int index = prv_find_custom_by_query(query);
  if (index < 0) return;

  if (label && label[0]) {
    strncpy(s_custom_locations[index].label, label,
            sizeof(s_custom_locations[index].label) - 1);
    s_custom_locations[index].label[
        sizeof(s_custom_locations[index].label) - 1] = '\0';
  }
  s_custom_locations[index].latitude_e2 = latitude_e2;
  s_custom_locations[index].longitude_e2 = longitude_e2;
  s_custom_locations[index].has_coordinates = true;
  prv_save_custom_locations();
  if (s_view && s_view->menu_layer) {
    menu_layer_reload_data(s_view->menu_layer);
  }
}

int saved_locations_get_entries(SavedLocationEntry *entries,
                                int max_entries,
                                const char *current_location_label,
                                int16_t current_latitude_e2,
                                int16_t current_longitude_e2,
                                bool has_current_location) {
  if (!entries || max_entries <= 0) return 0;
  prv_load_custom_locations();

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

static int prv_num_rows(void) {
  prv_load_custom_locations();
  int rows = 1;  // add row
  if (s_current_visible) rows++;
  for (int i = 0; i < CITY_PRESET_COUNT; i++) {
    if (!prv_is_default_saved_preset(i)) continue;
    if ((s_deleted_preset_mask & (1u << i)) == 0) rows++;
  }
  return rows + s_custom_count;
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
  return s_current_visible ? 1 : -1;
}

static int prv_preset_start_row(void) {
  return 1 + (s_current_visible ? 1 : 0);
}

static int prv_custom_start_row(void) {
  return prv_preset_start_row() + prv_visible_preset_count();
}

static bool prv_row_is_current(int row) {
  return s_current_visible && row == prv_current_row();
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

static int prv_custom_index_for_row(int row) {
  return row - prv_custom_start_row();
}

static bool prv_row_is_custom(int row) {
  int index = prv_custom_index_for_row(row);
  return index >= 0 && index < s_custom_count;
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
    menu_cell_basic_draw(ctx, cell_layer, title, "Current Location", NULL);
    return;
  }

  int preset_index = prv_preset_index_for_row(row);
  if (preset_index >= 0) {
    const CityPreset *preset = city_presets_get(preset_index);
    if (preset) {
      menu_cell_basic_draw(ctx, cell_layer, preset->city, preset->country, NULL);
    }
    return;
  }

  if (prv_row_is_custom(row)) {
    int custom_index = prv_custom_index_for_row(row);
    menu_cell_basic_draw(ctx, cell_layer,
                         s_custom_locations[custom_index].label,
                         "Saved Location", NULL);
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
#if WEATHER_PLATFORM_TOUCH_COLOR
  touch_service_unsubscribe();
#endif
  window_destroy(view->window);
  s_view = NULL;
  free(view);
}

bool saved_locations_is_showing(void) {
  return s_view && s_view->window;
}

void saved_locations_dismiss(bool animated) {
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
#if WEATHER_PLATFORM_TOUCH_COLOR
    touch_service_subscribe(prv_touch_handler, s_view);
#endif
    window_stack_push(s_view->window, true);
    return;
  }

  SavedLocationsView *view = calloc(1, sizeof(SavedLocationsView));
  if (!view) return;
  s_view = view;
  prv_load_custom_locations();

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
#if WEATHER_PLATFORM_TOUCH_COLOR
  touch_service_subscribe(prv_touch_handler, view);
#endif

  int selected_row = s_current_visible ? prv_current_row() : SAVED_LOCATIONS_ROW_ADD;
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

  view->dictation_session = dictation_session_create(64,
                                                     prv_dictation_callback,
                                                     view);
  if (view->dictation_session) {
    dictation_session_enable_confirmation(view->dictation_session, true);
    dictation_session_enable_error_dialogs(view->dictation_session, true);
  }

  window_stack_push(view->window, true);
}
