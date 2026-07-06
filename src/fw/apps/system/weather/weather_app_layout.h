/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include "pebble_compat.h"
#include <stdint.h>
#include "weather_types.h"

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

typedef struct WeatherAppLayout {
  Layer *root_layer;
  Layer *content_layer;
  BitmapLayer *current_weather_icon_layer;
  Layer *outgoing_weather_icon_layer;
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
  GFont metrics_value_font;   // bold metric VALUES (Timeline label/value texture)
  GFont tomorrow_font;
  Layer *down_arrow_layer;
  Layer *location_bar_layer;
  char location_name[32];
  bool fin_allowed;
  GPoint content_layer_origin;   // screen-absolute origin of content_layer
  GRect today_icon_rest_frame;
  GRect tomorrow_icon_rest_frame;
  Animation *icon_animation;
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
    // Pre-formatted strings snapshot of the OUTGOING text (captured before forecast pointer updates)
    char top_label[16];
    char top_phrase[20];   // condition text (rect right column)
    char top_feels[16];    // "FEELS 21°" (rect left column; empty = hidden)
    char top_temp[15];
    char top_highlow[15];
    char top_rain[12];
    char top_wind[16];
    char top_uv[12];
    char bot_label[16];
    char bot_highlow[15];
    bool bot_valid;
    AnimationProgress progress;
    bool dir_down;
    bool active;
  } text_anim;
} WeatherAppLayout;

void weather_app_layout_init(WeatherAppLayout *layout, const GRect *frame);

void weather_app_layout_set_data(WeatherAppLayout *layout,
                                 const WeatherLocationForecast *today_forecast,
                                 const WeatherLocationForecast *next_forecast);

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

