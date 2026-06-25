/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include "pebble_compat.h"
#include <stdint.h>
#include "weather_types.h"

#define WEATHER_APP_LAYOUT_ARROW_LAYER_HEIGHT (18)
#define WEATHER_APP_LAYOUT_LOCATION_BAR_HEIGHT (18)
// Main-screen bar is taller than the forecast-list bar for readability.
#define WEATHER_APP_LAYOUT_MAIN_BAR_HEIGHT (26)
// Round (gabbro) draws the large 75px vector weather icon (WX_WEATHER_ICONS_PDC)
// + the day swoop. Emery (PBL_ROUND==0) keeps the raster BitmapLayer path.
// IMPORTANT: a system app's PDC resource is mmap'd READ-ONLY into flash, so the
// sequence must be CLONED into RAM before any write (gdraw_command_sequence_set_
// bounds_size writes sequence->size and faulted on launch). See the clone in
// weather_app_layout_init.
#define WEATHER_APP_LAYOUT_USE_PDC_WEATHER_ICONS PBL_ROUND

typedef enum {
  WeatherInteractiveTransition_None = 0,
  WeatherInteractiveTransition_Day,
  WeatherInteractiveTransition_Clock,
} WeatherInteractiveTransition;

typedef struct WeatherAppLayout {
  Layer *root_layer;
  Layer *content_layer;
  BitmapLayer *current_weather_icon_layer;
  Layer *outgoing_weather_icon_layer;
  Layer *tomorrow_outgoing_icon_layer;  // per-pixel scaler for the small icon (clock transition)
  BitmapLayer *tomorrow_weather_icon_layer;
#if WEATHER_APP_LAYOUT_USE_PDC_WEATHER_ICONS
  GDrawCommandSequence *weather_icon_pdc_sequence;
  Layer *current_weather_escape_layer;
#endif
  GBitmap *current_weather_icon;
  GBitmap *outgoing_weather_icon;
  GBitmap *tomorrow_weather_icon;
  GDrawCommandImage *fin_pdc;  // real timeline 'fin' flag (END_OF_TIMELINE PDC)
  Layer *fin_layer;
  Animation *fin_animation;
  const WeatherLocationForecast *forecast;
  const WeatherLocationForecast *next_forecast;
  GFont location_font;
  GFont temperature_font;
  GFont high_low_phrase_font;
  GFont metrics_font;
  GFont tomorrow_font;
  Layer *down_arrow_layer;
  Layer *location_bar_layer;
  char location_name[32];
  bool fin_allowed;
  GPoint content_layer_origin;   // screen-absolute origin of content_layer
  GRect today_icon_rest_frame;
  GRect tomorrow_icon_rest_frame;
  Animation *icon_animation;
  AppTimer *glow_timer;          // drives the lava-lamp glow ring animation
  uint32_t glow_phase;           // current rotation phase (0..TRIG_MAX_ANGLE)
  uint8_t glow_idle_ticks;
  bool glow_paused;              // true while hold-scrolling to save CPU
  struct {
    GPoint circle_center;
    int32_t radius;
    int32_t outgoing_start_angle;
    int32_t outgoing_end_angle;
    int32_t incoming_start_angle;
    // incoming always ends at ICON_ARC_TODAY_REST
    WeatherType outgoing_weather_type;
    WeatherType incoming_weather_type;
    WeatherType tomorrow_exit_weather_type; // weather type of tomorrow icon while it exits/enters
    bool animate_down;
    bool tomorrow_reparented;  // true when tomorrow_layer lives in root_layer during animation
    bool tomorrow_incoming;    // true when tomorrow_layer is animating IN during DOWN animation
#if WEATHER_APP_LAYOUT_USE_PDC_WEATHER_ICONS
    bool current_root_overlay; // Gabbro-only: current icon exits above content clipping
#endif
  } anim_params;
  struct {
    GRect from;
    GRect to;
  } fin_anim;
  struct {
    bool hide_bottom_half_text;
    bool transitioning_to_list;      // right-swipe exit animation active
    bool returning_from_list;        // return-swipe animation active (list → main)
    void (*on_list_done)(void *ctx); // called when transition completes
    void *on_list_done_ctx;
    int32_t list_transit_progress;   // 0..ANIMATION_NORMALIZED_MAX; for speed-line drawing
    GPoint  list_today_icon_pos;     // current icon centre in content-layer coords; for speed lines
    GPoint  list_tmr_icon_pos;       // current tomorrow icon centre
    int     list_start_day;          // current_day_index when transition started
    int     list_num_days;           // total days available when transition started
  } animation_state;
  struct {
    // Pre-formatted strings snapshot of the OUTGOING text (captured before forecast pointer updates)
    char top_label[16];
    char top_temp[15];
    char top_highlow[15];
    char top_rain[12];
    char top_wind[16];
    char bot_label[16];
    char bot_highlow[15];
    bool bot_valid;
    AnimationProgress progress;
    bool dir_down;
    bool active;
  } text_anim;
  // ---- Refresh banner ----
  Animation *refresh_banner_anim;
  AnimationProgress refresh_banner_progress;
#if PBL_ROUND
  char refresh_banner_text[18];
#endif
  bool refresh_banner_active;
  // ---- Bar resize animation ----
  AnimationProgress bar_anim_progress;  // 0 = at-rest, >0 = mid-transition
  bool bar_anim_shrinking;              // true = main→list, false = list→main
  // ---- Pull-to-refresh ----
  GRect    pull_content_rest_frame;  // content_layer rest frame (saved at init)
  int      pull_offset;              // current y-shift in pixels (0 = rest)
  bool     pull_active;              // drag currently in progress
  bool     pull_triggered;           // crossed full-pull threshold (haptic fired)
  bool     pull_snapping;            // snap-back timer running
  int      pull_snap_from;           // pull_offset when snap-back began
  int      pull_snap_step;           // elapsed steps in snap-back ease
  AppTimer *pull_snap_timer;         // snap-back driver
  AppTimer *pull_spin_timer;         // spinner rotation driver
  int32_t  pull_spin_angle;          // current spinner arc start angle
  // ---- Clock-face transition ----
  struct {
    bool  transitioning_to_clock;
    bool  shake_phase;            // true once icons have merged into the shake dot
    void (*on_clock_done)(void *ctx);
    void *on_clock_done_ctx;
  } clock_transit;
  // ---- Finger-driven transition preview ----
  struct {
    WeatherInteractiveTransition kind;
    AnimationProgress progress;
    AnimationProgress from_progress;
    AnimationProgress to_progress;
    bool commit;
    const WeatherLocationForecast *old_today;
    const WeatherLocationForecast *old_next;
    const WeatherLocationForecast *new_today;
    const WeatherLocationForecast *new_next;
    bool day_animate_down;
    bool use_floaty_day;
    void (*done_cb)(void *ctx);
    void *done_ctx;
  } interactive;
} WeatherAppLayout;

void weather_app_layout_init(WeatherAppLayout *layout, const GRect *frame);

void weather_app_layout_set_data(WeatherAppLayout *layout,
                                 const WeatherLocationForecast *today_forecast,
                                 const WeatherLocationForecast *next_forecast);

void weather_app_layout_note_interaction(WeatherAppLayout *layout);

#define weather_app_layout_set_down_arrow_visible(layout, is_down_visible) \
  do { (void)(layout); (void)(is_down_visible); } while (0)

void weather_app_layout_set_fin_allowed(WeatherAppLayout *layout,
                                        bool fin_allowed);

void weather_app_layout_set_location(WeatherAppLayout *layout, const char *name);

void weather_app_layout_deinit(WeatherAppLayout *layout);

void weather_app_layout_animate(WeatherAppLayout *layout,
                                const WeatherLocationForecast *new_today,
                                const WeatherLocationForecast *new_next,
                                bool animate_down);

// Animate today+tomorrow icons to their list-screen positions (all text hidden),
// then call done_cb(done_ctx). Push the forecast list window inside done_cb.
// current_day_index: the day currently shown as "today" on the main screen.
// num_days: total number of days available.
void weather_app_layout_start_list_transition(WeatherAppLayout *layout,
                                              int current_day_index,
                                              int num_days,
                                              void (*done_cb)(void *ctx),
                                              void *done_ctx);

// Animate the location bar to briefly show "refresh: HH:MM" then return to
// the normal location + time display. Call this whenever the user returns to the
// main screen. refresh_time is the wall-clock time of the most recent data fetch.
void weather_app_layout_show_refresh_banner(WeatherAppLayout *layout, time_t refresh_time);

// Reverse transition: animate icons from their list-screen positions back to rest.
// Call this when the forecast list window is dismissed.
void weather_app_layout_start_return_transition(WeatherAppLayout *layout);

// ---- Pull-to-refresh API ----
// Feed accumulated downward drag delta (positive = pulling down).
// Call during TouchEvent_Dragging when at day 0.
void weather_app_layout_pull_update(WeatherAppLayout *layout, int total_dy);

// Finger lifted. Returns true if the full pull threshold was reached
// and the caller should request a data refresh.
bool weather_app_layout_pull_release(WeatherAppLayout *layout);

// Cancel pull gesture without triggering (direction changed, etc.).
void weather_app_layout_pull_abort(WeatherAppLayout *layout);

// Animate today + tomorrow icons toward screen centre, shake, then call
// done_cb so the caller can push the clock face window (with animated=false).
void weather_app_layout_start_clock_transition(WeatherAppLayout *layout,
                                               void (*done_cb)(void *ctx),
                                               void *done_ctx);

bool weather_app_layout_begin_interactive_day(WeatherAppLayout *layout,
                                              const WeatherLocationForecast *new_today,
                                              const WeatherLocationForecast *new_next,
                                              bool animate_down);
bool weather_app_layout_scrub_interactive_day(WeatherAppLayout *layout,
                                              const WeatherLocationForecast *old_today,
                                              const WeatherLocationForecast *old_next,
                                              const WeatherLocationForecast *new_today,
                                              const WeatherLocationForecast *new_next,
                                              bool animate_down,
                                              AnimationProgress progress);
bool weather_app_layout_begin_interactive_clock(WeatherAppLayout *layout);
void weather_app_layout_update_interactive(WeatherAppLayout *layout,
                                           AnimationProgress progress);
void weather_app_layout_finish_interactive(WeatherAppLayout *layout,
                                           bool commit,
                                           void (*done_cb)(void *ctx),
                                           void *done_ctx);
#define weather_app_layout_cancel_interactive(layout) \
  weather_app_layout_finish_interactive((layout), false, NULL, NULL)
void weather_app_layout_abort_interactive(WeatherAppLayout *layout,
                                          const WeatherLocationForecast *today,
                                          const WeatherLocationForecast *next);
#define weather_app_layout_is_interactive_active(layout) \
  ((layout) && (layout)->interactive.kind != WeatherInteractiveTransition_None)
