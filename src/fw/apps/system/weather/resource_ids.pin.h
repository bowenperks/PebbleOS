/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */
//! Pinned resource ids for the Weather stored app — v4.18 (main) obelix/emery build.
//! Stored apps resolve RESOURCE_ID_* against the SYSTEM pack; the build emits only an
//! empty resource_ids.auto.h, so the app's icon names are defined here, pinned to the
//! ids the firmware assigns (build/src/fw/resource/resource_ids.auto.h). The 18 weather
//! PNGs were appended to resource_map.json as WX_* (ids 492-509 on this build; append
//! keeps existing ids stable). Re-read if resource_map ordering changes.
#pragma once

// ---- TINY (25x25 PNG / WX_*_TINY) ----
#define RESOURCE_ID_IMAGE_SUNNY_DAY_TINY        492
#define RESOURCE_ID_IMAGE_PARTLY_CLOUDY_TINY    494
#define RESOURCE_ID_IMAGE_CLOUDY_DAY_TINY       496
#define RESOURCE_ID_IMAGE_LIGHT_RAIN_TINY       498
#define RESOURCE_ID_IMAGE_HEAVY_RAIN_TINY       500
#define RESOURCE_ID_IMAGE_LIGHT_SNOW_TINY       502
#define RESOURCE_ID_IMAGE_HEAVY_SNOW_TINY       504
#define RESOURCE_ID_IMAGE_RAIN_AND_SNOW_TINY    506
#define RESOURCE_ID_IMAGE_GENERIC_WEATHER_TINY  508

// ---- SMALL (50x50 PNG / WX_*_SMALL) ----
// RESOURCE_ID_IMAGE_SUNNY_DAY_SMALL provided by the real resource_ids.auto.h (id 8)
#define RESOURCE_ID_IMAGE_PARTLY_CLOUDY_SMALL   495
#define RESOURCE_ID_IMAGE_CLOUDY_DAY_SMALL      497
#define RESOURCE_ID_IMAGE_LIGHT_RAIN_SMALL      499
#define RESOURCE_ID_IMAGE_HEAVY_RAIN_SMALL      501
#define RESOURCE_ID_IMAGE_LIGHT_SNOW_SMALL      503
#define RESOURCE_ID_IMAGE_HEAVY_SNOW_SMALL      505
#define RESOURCE_ID_IMAGE_RAIN_AND_SNOW_SMALL   507
#define RESOURCE_ID_IMAGE_GENERIC_WEATHER_SMALL 509

// ---- CLOCK icons: gabbro-only (compiled out on emery). Mapped to the TINY
//      (25x25) ids so the source matches CLOCK_ICON_SIZE (25) — previously
//      aliased to the 50x50 SMALL ids, which made the round clock draw a
//      cropped/off-centre top-left 25-of-50 icon. ----
#define RESOURCE_ID_IMAGE_SUNNY_DAY_CLOCK       492
#define RESOURCE_ID_IMAGE_PARTLY_CLOUDY_CLOCK   494
#define RESOURCE_ID_IMAGE_CLOUDY_DAY_CLOCK      496
#define RESOURCE_ID_IMAGE_LIGHT_RAIN_CLOCK      498
#define RESOURCE_ID_IMAGE_HEAVY_RAIN_CLOCK      500
#define RESOURCE_ID_IMAGE_LIGHT_SNOW_CLOCK      502
#define RESOURCE_ID_IMAGE_HEAVY_SNOW_CLOCK      504
#define RESOURCE_ID_IMAGE_RAIN_AND_SNOW_CLOCK   506
#define RESOURCE_ID_IMAGE_GENERIC_WEATHER_CLOCK 508

// ---- Not in the system pack: pin to a valid PNG (WX_SUNNY_SMALL=493) so the app
//      links; cosmetic/secondary paths only, null-checked at runtime. ----
#define RESOURCE_ID_IMAGE_FIN_TIMELINE          493
#define RESOURCE_ID_RESULT_SHREDDED_LARGE       493

// ---- PDC sequences (round/gabbro only). The main weather-icons sequence is now
//      shipped in the pack as WX_WEATHER_ICONS_PDC; alias the app's name to the
//      real auto id (the auto header is included via pebble_compat before this).
//      WEATHER_CLOCK_ICONS_PDC has no asset yet (forecast_list null-checks it). ----
#define RESOURCE_ID_WEATHER_ICONS_PDC           RESOURCE_ID_WX_WEATHER_ICONS_PDC
#define RESOURCE_ID_WEATHER_CLOCK_ICONS_PDC     0

// ---- Globe resources (type raw). Placeholder 0 until read from the build. ----
#define RESOURCE_ID_GLOBE_CUBEMAP      510
#define RESOURCE_ID_GLOBE_STARFIELD      511
#define RESOURCE_ID_GLOBE_BW_SEQUENCE      512
#define RESOURCE_ID_GLOBE_CRADLE_PDC      513
