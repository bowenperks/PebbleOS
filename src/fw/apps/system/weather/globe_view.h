/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

/**
 * globe_view.h
 * Globe animation view for Pebble Weather
 * Displays rotating globe with weather forecast overlay
 */

#pragma once

#include "pebble_compat.h"
#include "saved_locations.h"
#include "weather_platform.h"

typedef void (*GlobeLocationSelectCallback)(SavedLocationKind kind,
                                            int preset_index,
                                            const char *query,
                                            bool force,
                                            void *context);
typedef void (*GlobeDictationCallback)(const char *query, void *context);
typedef void (*GlobeWrapCallback)(void *context);
typedef void (*GlobeSavedLocationsCallback)(void *context);
typedef void (*GlobeForecastListCallback)(void *context);
typedef void (*GlobeMainCallback)(void *context);

typedef struct {
    Window *window;
    Layer *canvas_layer;
    Layer *space_layer;
    Layer *globe_layer;
    TextLayer *city_label_layer;
    uint8_t *cubemap_data;
    size_t cubemap_size;
    uint8_t *starfield_data;
    size_t starfield_size;
    GDrawCommandSequence *bw_sequence;
    GDrawCommandSequence *bw_highlight_sequence;
    GDrawCommandSequence *bw_crumple_sequence;
    GDrawCommandImage *cradle_pdc;
    int bw_crumple_amount;
    int bw_crumple_frame;
    uint32_t bw_frame_count;
    uint32_t bw_sequence_duration_ms;
    uint32_t bw_elapsed_ms;
    int current_frame;
    int32_t color_latitude_e2;
    int32_t color_longitude_e2;
    int32_t transition_color_start_latitude_e2;
    int32_t transition_color_start_longitude_e2;
    int32_t transition_color_target_latitude_e2;
    int32_t transition_color_target_longitude_e2;
    int32_t city_anim_start_latitude_e2;
    int32_t city_anim_start_longitude_e2;
    int32_t city_anim_target_latitude_e2;
    int32_t city_anim_target_longitude_e2;
    int32_t city_anim_latitude_delta_e2;
    int32_t city_anim_longitude_delta_e2;
    int32_t globe_rotation[9];
    int transition_bw_frame;
    Animation *reveal_anim;
    Animation *city_anim;
    Animation *bounce_anim;
    AnimationProgress reveal_progress;
    AnimationProgress city_anim_progress;
    AnimationProgress bounce_progress;
    int reveal_direction;
    int selected_city_index;
    SavedLocationEntry saved_entries[SAVED_LOCATIONS_MAX_ENTRIES];
    int saved_entry_count;
    int32_t current_location_latitude_e2;
    int32_t current_location_longitude_e2;
    int32_t custom_location_latitude_e2;
    int32_t custom_location_longitude_e2;
    int bounce_direction;
    char current_location_label[48];
    char custom_location_label[48];
    char city_label_text[48];
    GlobeLocationSelectCallback location_select_callback;
    void *location_select_context;
    GlobeDictationCallback dictation_callback;
    void *dictation_context;
    GlobeWrapCallback wrap_callback;
    void *wrap_context;
    GlobeSavedLocationsCallback saved_locations_callback;
    void *saved_locations_context;
    GlobeForecastListCallback forecast_list_callback;
    void *forecast_list_context;
    GlobeMainCallback main_callback;
    void *main_context;
    AppTimer *animation_timer;
    AppTimer *idle_timer;
#if WEATHER_PLATFORM_TOUCH_COLOR
    AppTimer *coast_timer;
#endif
    bool is_animating;
    bool is_revealing;
    bool is_revealed;
    bool has_current_location;
    bool has_custom_location;
    bool is_free_roam;
    bool intro_world_selected;
    bool bw_idle;
    uint8_t bw_idle_slowdown_step;
    uint8_t intro_selection_ms;
#if WEATHER_PLATFORM_TOUCH_COLOR
    int16_t touch_start_x;
    int16_t touch_start_y;
    int16_t touch_last_x;
    int16_t touch_last_y;
    int32_t touch_velocity_x_q8;
    int32_t touch_velocity_y_q8;
    int32_t coast_velocity_x_q8;
    int32_t coast_velocity_y_q8;
    int32_t starfield_offset_x_q8;
    int32_t starfield_offset_y_q8;
    int hover_city_index;
    bool touch_active;
    bool touch_drag_axis_set;
    bool touch_drag_rotated;
    bool touch_down_on_globe;
    bool touch_controls_globe;
    bool coast_active;
    bool hover_lock_active;
#endif
} GlobeView;

/**
 * Create a new globe view
 * @return Pointer to initialized GlobeView, or NULL on error
 */
GlobeView *globe_view_create(void);

void globe_view_set_location_select_callback(GlobeView *view,
                                             GlobeLocationSelectCallback callback,
                                             void *context);

void globe_view_set_dictation_callback(GlobeView *view,
                                       GlobeDictationCallback callback,
                                       void *context);

void globe_view_set_wrap_callback(GlobeView *view,
                                  GlobeWrapCallback callback,
                                  void *context);

void globe_view_set_saved_locations_callback(GlobeView *view,
                                             GlobeSavedLocationsCallback callback,
                                             void *context);

void globe_view_set_forecast_list_callback(GlobeView *view,
                                           GlobeForecastListCallback callback,
                                           void *context);

void globe_view_set_main_callback(GlobeView *view,
                                  GlobeMainCallback callback,
                                  void *context);

void globe_view_set_current_location(GlobeView *view,
                                     const char *label,
                                     int16_t latitude_e2,
                                     int16_t longitude_e2);

void globe_view_set_custom_location(GlobeView *view,
                                    const char *label,
                                    int16_t latitude_e2,
                                    int16_t longitude_e2,
                                    bool select);

void globe_view_set_selected_city(GlobeView *view, int city_index);

void globe_view_reload_saved_locations(GlobeView *view);

/**
 * Destroy the globe view and free all resources
 * @param view Pointer to GlobeView to destroy
 */
void globe_view_destroy(GlobeView *view);

/**
 * Start the globe animation
 * @param view Pointer to GlobeView
 */
void globe_view_start_animation(GlobeView *view);

/**
 * Stop the globe animation
 * @param view Pointer to GlobeView
 */
void globe_view_stop_animation(GlobeView *view);

/**
 * Push the globe view to the window stack
 * @param view Pointer to GlobeView
 */
void globe_view_push(GlobeView *view);

void globe_view_push_animated(GlobeView *view, bool animated);

/**
 * Pop the globe view from the window stack
 * @param view Pointer to GlobeView
 */
void globe_view_pop(GlobeView *view);

void globe_view_dismiss(GlobeView *view, bool animated);
