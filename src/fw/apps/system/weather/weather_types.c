/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "weather_types.h"
#include "resource_ids.pin.h"

int weather_type_slot_index(WeatherType weather_type) {
  int value = (int)weather_type;
  return (value >= 0 && value <= WeatherType_RainAndSnow) ? value : 9;
}

GColor weather_type_bg_color(WeatherType weather_type) {
  static const GColor s_colors[] = {
    GColorChromeYellow,   // PartlyCloudy
    GColorLightGray,      // CloudyDay
    GColorElectricBlue,   // LightSnow
    GColorPictonBlue,     // LightRain
    GColorBlueMoon,       // HeavyRain
    GColorTiffanyBlue,    // HeavySnow
    GColorLightGray,      // Generic
    GColorOrange,         // Sun
    GColorMidnightGreen,  // RainAndSnow
    GColorLightGray,      // Unknown
  };
  return s_colors[weather_type_slot_index(weather_type)];
}

uint32_t weather_type_icon_tiny_resource(WeatherType weather_type) {
  static const uint32_t s_resources[] = {
    RESOURCE_ID_IMAGE_PARTLY_CLOUDY_TINY,
    RESOURCE_ID_IMAGE_CLOUDY_DAY_TINY,
    RESOURCE_ID_IMAGE_LIGHT_SNOW_TINY,
    RESOURCE_ID_IMAGE_LIGHT_RAIN_TINY,
    RESOURCE_ID_IMAGE_HEAVY_RAIN_TINY,
    RESOURCE_ID_IMAGE_HEAVY_SNOW_TINY,
    RESOURCE_ID_IMAGE_GENERIC_WEATHER_TINY,
    RESOURCE_ID_IMAGE_SUNNY_DAY_TINY,
    RESOURCE_ID_IMAGE_RAIN_AND_SNOW_TINY,
    RESOURCE_ID_IMAGE_GENERIC_WEATHER_TINY,
  };
  return s_resources[weather_type_slot_index(weather_type)];
}

uint32_t weather_type_icon_small_resource(WeatherType weather_type) {
#if PBL_DISPLAY_HEIGHT >= 200
  static const uint32_t s_resources[] = {
    RESOURCE_ID_IMAGE_PARTLY_CLOUDY_SMALL,
    RESOURCE_ID_IMAGE_CLOUDY_DAY_SMALL,
    RESOURCE_ID_IMAGE_LIGHT_SNOW_SMALL,
    RESOURCE_ID_IMAGE_LIGHT_RAIN_SMALL,
    RESOURCE_ID_IMAGE_HEAVY_RAIN_SMALL,
    RESOURCE_ID_IMAGE_HEAVY_SNOW_SMALL,
    RESOURCE_ID_IMAGE_GENERIC_WEATHER_SMALL,
    RESOURCE_ID_IMAGE_SUNNY_DAY_SMALL,
    RESOURCE_ID_IMAGE_RAIN_AND_SNOW_SMALL,
    RESOURCE_ID_IMAGE_GENERIC_WEATHER_SMALL,
  };
  return s_resources[weather_type_slot_index(weather_type)];
#else
  return weather_type_icon_tiny_resource(weather_type);
#endif
}

#if defined(PBL_PLATFORM_GABBRO)
uint32_t weather_type_icon_clock_resource(WeatherType weather_type) {
  static const uint32_t s_resources[] = {
    RESOURCE_ID_IMAGE_PARTLY_CLOUDY_CLOCK,
    RESOURCE_ID_IMAGE_CLOUDY_DAY_CLOCK,
    RESOURCE_ID_IMAGE_LIGHT_SNOW_CLOCK,
    RESOURCE_ID_IMAGE_LIGHT_RAIN_CLOCK,
    RESOURCE_ID_IMAGE_HEAVY_RAIN_CLOCK,
    RESOURCE_ID_IMAGE_HEAVY_SNOW_CLOCK,
    RESOURCE_ID_IMAGE_GENERIC_WEATHER_CLOCK,
    RESOURCE_ID_IMAGE_SUNNY_DAY_CLOCK,
    RESOURCE_ID_IMAGE_RAIN_AND_SNOW_CLOCK,
    RESOURCE_ID_IMAGE_GENERIC_WEATHER_CLOCK,  // Unknown fallback (slot 9) — match the other lookups
  };
  return s_resources[weather_type_slot_index(weather_type)];
}
#endif
