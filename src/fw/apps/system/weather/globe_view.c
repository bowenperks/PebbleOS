/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

/**
 * globe_view.c
 * Globe animation view implementation
 */

#include "globe_view.h"
// GLOBE_* resource ids come from the real resource_ids.auto.h (via
// pebble_compat.h); the stored-app pinned header is not used in the system app.

#include "city_presets.h"
#include "weather_math.h"
#include "weather.h"
#include "applib/ui/app_window_stack.h"

#include <limits.h>

#define GLOBE_FRAME_INTERVAL_MS 35  // ~28.6 FPS, matching Pebble's shredder PDC cadence
#define GLOBE_IDLE_TIMEOUT_MS 5000
#define GLOBE_IDLE_SLOWDOWN_STEPS 8
#define GLOBE_IDLE_SLOWDOWN_STEP_MS 6
#define NUM_BW_FRAMES 60
#define NUM_COLOR_LON_FRAMES 24
#define GLOBE_CUBEMAP_FACE_COUNT 6
#define GLOBE_CUBEMAP_FACE_SIZE 64
#define GLOBE_CUBEMAP_DATA_SIZE \
    ((GLOBE_CUBEMAP_FACE_COUNT * GLOBE_CUBEMAP_FACE_SIZE * GLOBE_CUBEMAP_FACE_SIZE) / 2)
#define GLOBE_CUBEMAP_PALETTE_SIZE 12
#define GLOBE_ROT_SHIFT 10
#define GLOBE_ROT_SCALE (1 << GLOBE_ROT_SHIFT)
#define GLOBE_DRAG_TRIGANGLE_PER_PX (TRIG_MAX_ANGLE / 420)
#define GLOBE_RENDER_BASE_DIAMETER 110
#define GLOBE_CITY_ROTATION_DURATION_MS 260
#define GLOBE_CITY_BOUNCE_DURATION_MS 160
#define GLOBE_CITY_BOUNCE_PX 8
#define GLOBE_CITY_LABEL_HEIGHT 28
#define GLOBE_CITY_LABEL_ROUND_BOTTOM_INSET PBL_IF_ROUND_ELSE(9, 0)
#define GLOBE_CITY_LABEL_ROUND_SIDE_INSET PBL_IF_ROUND_ELSE(50, 0)
#define GLOBE_INTRO_TITLE_HEIGHT 34
#define GLOBE_SAVED_LABEL_HEIGHT 34
#define GLOBE_SAVED_LABEL_ROUND_BOTTOM_INSET PBL_IF_ROUND_ELSE(18, 0)
#define GLOBE_SAVED_COG_SIZE 18
#define GLOBE_SAVED_LABEL_GAP 6
#define GLOBE_INTRO_SELECTION_DURATION_MS 175
#define GLOBE_INTRO_SELECTION_OFFSET_PX 5
#define GLOBE_PLANET_CENTER_Y_OFFSET -13
#define GLOBE_COLOR_PLANET_CENTER_Y_OFFSET \
    PBL_IF_ROUND_ELSE(-GLOBE_CRADLE_CENTER_Y_OFFSET, GLOBE_PLANET_CENTER_Y_OFFSET)
#define GLOBE_REVEALED_CENTER_Y_OFFSET \
    PBL_IF_ROUND_ELSE(0, GLOBE_CRADLE_CENTER_Y_OFFSET + GLOBE_PLANET_CENTER_Y_OFFSET)
#define GLOBE_CURRENT_LOCATION_INDEX 0
#define GLOBE_FIRST_PRESET_INDEX 1
#define GLOBE_CUSTOM_LOCATION_INDEX (CITY_PRESET_COUNT + GLOBE_FIRST_PRESET_INDEX)
#define GLOBE_SELECTOR_COUNT (CITY_PRESET_COUNT + 2)
#define GLOBE_CRUMPLE_DELAY_SEGMENTS 8
#define GLOBE_CRUMPLE_POINT_DURATION_NUM 2
#define GLOBE_CRUMPLE_POINT_DURATION_DEN 3
#define GLOBE_CRUMPLE_SWEEP_ANGLE DEG_TO_TRIGANGLE(-45)
#define GLOBE_CRADLE_CENTER_Y_OFFSET 5
#define GLOBE_REVEAL_DURATION_MS 900
#define GLOBE_REVEAL_REVERSE_DURATION_MS 420
#define GLOBE_REVEAL_SPIN_STEPS 4
#define GLOBE_REVEAL_COLOR_SPIN_DEGREES_E2 72000
#define GLOBE_REVEAL_BW_SPEED_NUM 4
#define GLOBE_REVEAL_BW_SPEED_DEN 1
#define GLOBE_CRADLE_DROP_PX 24
#define GLOBE_COLOR_FINAL_SCALE_PERCENT PBL_IF_ROUND_ELSE(164, 150)
#define GLOBE_COLOR_GROW_START_PERCENT 22
#define GLOBE_CRADLE_ARM_CENTER_X 62
#define GLOBE_CRADLE_ARM_CENTER_Y 60
#define GLOBE_CRADLE_ARM_OUTER_RADIUS 64
#define GLOBE_CRADLE_ARM_THICKNESS 8
#define GLOBE_CRADLE_ARM_START_ANGLE DEG_TO_TRIGANGLE(20)
#define GLOBE_CRADLE_ARM_END_ANGLE DEG_TO_TRIGANGLE(205)
#define GLOBE_CRADLE_WIPE_MARGIN 8
#define GLOBE_TAP_THRESHOLD 20
#define GLOBE_DRAG_AXIS_THRESHOLD 5
#define GLOBE_COAST_FRAME_MS 48
#define GLOBE_COAST_STEP_NUM 3
#define GLOBE_COAST_STEP_DEN 2
#define GLOBE_COAST_DECAY_NUM 86
#define GLOBE_COAST_DECAY_DEN 100
#define GLOBE_COAST_MIN_SPEED_Q8 90
#define GLOBE_COAST_SETTLE_SPEED_Q8 210
#define GLOBE_COAST_START_SPEED_Q8 170
#define GLOBE_COAST_MAX_SPEED_Q8 2300
#define GLOBE_LOCK_RADIUS_PX 50
#define GLOBE_LOCK_SWOOP_DURATION_MS 300
#define GLOBE_LOCK_SETTLE_STEPS 2
#define GLOBE_LOCK_SETTLE_DIVISOR 2
#define GLOBE_LOCK_SETTLE_DEADZONE_PX 2
#define GLOBE_COMMIT_LOCK_RADIUS_PX 40
#define GLOBE_Q8_ONE 256
#define GLOBE_STARFIELD_WIDTH 400
#define GLOBE_STARFIELD_HEIGHT 228
#define GLOBE_ATMOSPHERE_GLOW_PX 6
#define GLOBE_ATMOSPHERE_LAYER_MARGIN_PX 2
#define GLOBE_OUTLINE_PX 5
#define GLOBE_PIN_EDGE_FRONT_Q10 (GLOBE_ROT_SCALE / 5)
#define GLOBE_SPACE_FADE_DITHER_SIZE 4
#define GLOBE_SPACE_FADE_FULL_PERCENT 86
#define GLOBE_SPACE_STAR_REVEAL_START_PERCENT 72
#define GLOBE_CRUMPLE_MAX_POINTS 96

static const uint8_t GLOBE_SPACE_FADE_DITHER[GLOBE_SPACE_FADE_DITHER_SIZE]
                                           [GLOBE_SPACE_FADE_DITHER_SIZE] = {
    { 0,  8,  2, 10 },
    { 12, 4, 14,  6 },
    { 3, 11,  1,  9 },
    { 15, 7, 13,  5 },
};

static const uint8_t GLOBE_CUBEMAP_PALETTE[GLOBE_CUBEMAP_PALETTE_SIZE] = {
    0xC2,  // GColorDukeBlue       #0000AA
    0xC7,  // GColorBlueMoon       #0055FF
    0xCB,  // GColorVividCerulean  #00AAFF
    0xDB,  // GColorPictonBlue     #55AAFF
    0xEF,  // GColorCeleste        #AAFFFF
    0xFF,  // GColorWhite          #FFFFFF
    0xD9,  // GColorMayGreen       #55AA55
    0xC4,  // GColorDarkGreen      #005500
    0xC9,  // GColorJaegerGreen    #00AA55
    0xEC,  // GColorSpringBud      #AAFF00
    0xDD,  // GColorScreaminGreen  #55FF55
    0xEE,  // GColorMintGreen      #AAFFAA
};

static void animation_timer_handler(void *context);
static void start_globe_idle_timer(GlobeView *view);
static GRect clip_rect_to_bounds(GRect rect, GRect bounds);
static void update_city_label_layer(GlobeView *view);
static void set_free_roam_enabled(GlobeView *view, bool enabled);
static void show_intro_canvas(GlobeView *view);
static void show_revealed_space_layers(GlobeView *view);
static void mark_dynamic_globe_dirty(GlobeView *view);
static void ensure_visual_resources(GlobeView *view);
static void release_visual_resources(GlobeView *view);
static int bounce_offset(GlobeView *view);
static void framebuffer_draw_starfield(GBitmap *fb, GlobeView *view,
                                       GRect bounds, GRect clip_rect);
#if WEATHER_PLATFORM_TOUCH_COLOR
static void stop_globe_coast(GlobeView *view, bool mark_dirty);
static bool try_start_magnetic_city_lock(GlobeView *view, int radius_px);
static bool point_is_on_revealed_globe(GlobeView *view, int16_t x, int16_t y);
static void apply_globe_screen_delta_q8(GlobeView *view,
                                        int32_t dx_q8,
                                        int32_t dy_q8);
#endif

static uint32_t bw_frame_count_for_view(GlobeView *view) {
    return (view && view->bw_frame_count > 0) ? view->bw_frame_count : NUM_BW_FRAMES;
}

static int bw_frame_index_for_view(GlobeView *view, int frame) {
    uint32_t frame_count = bw_frame_count_for_view(view);
    if (frame_count == 0) return 0;

    int result = frame % (int)frame_count;
    return result < 0 ? result + (int)frame_count : result;
}

static uint32_t bw_sequence_duration_for_view(GlobeView *view) {
    if (view && view->bw_sequence_duration_ms > 0) {
        return view->bw_sequence_duration_ms;
    }
    return bw_frame_count_for_view(view) * GLOBE_FRAME_INTERVAL_MS;
}

static void sync_bw_elapsed_to_current_frame(GlobeView *view) {
    if (!view) return;

    uint32_t duration = bw_sequence_duration_for_view(view);
    if (duration == 0) {
        view->bw_elapsed_ms = 0;
        return;
    }

    uint32_t frame = (uint32_t)bw_frame_index_for_view(view, view->current_frame);
    view->bw_elapsed_ms = (frame * GLOBE_FRAME_INTERVAL_MS) % duration;
}

// positive_modulo is provided by util/math.h (same semantics) — using that.

static int32_t abs_i32(int32_t value) {
    return value < 0 ? -value : value;
}

static int rounded_divide(int value, int divisor) {
    if (divisor == 0) return 0;
    return value >= 0 ? (value + (divisor / 2)) / divisor
                      : (value - (divisor / 2)) / divisor;
}

static int bw_frame_for_longitude_e2(int16_t longitude_e2) {
    return positive_modulo(rounded_divide(-longitude_e2, 600), NUM_BW_FRAMES);
}

static int32_t longitude_e2_for_bw_frame(int bw_frame) {
    int frame = ((bw_frame * NUM_COLOR_LON_FRAMES) + (NUM_BW_FRAMES / 2)) /
        NUM_BW_FRAMES % NUM_COLOR_LON_FRAMES;
    return -frame * 1500;
}

static int32_t shortest_longitude_delta_e2(int32_t from_e2, int32_t to_e2);

static int32_t selected_city_latitude_e2(GlobeView *view);
static int32_t selected_city_longitude_e2(GlobeView *view);

static SavedLocationEntry *saved_entry_for_index(GlobeView *view, int index) {
    if (!view || index < 0 || index >= view->saved_entry_count) return NULL;
    return &view->saved_entries[index];
}

static SavedLocationEntry *selected_saved_entry(GlobeView *view) {
    if (!view || view->saved_entry_count <= 0) return NULL;
    if (view->selected_city_index < 0) {
        view->selected_city_index = 0;
    } else if (view->selected_city_index >= view->saved_entry_count) {
        view->selected_city_index = view->saved_entry_count - 1;
    }
    return saved_entry_for_index(view, view->selected_city_index);
}

static int find_current_saved_entry_index(GlobeView *view) {
    if (!view) return -1;
    for (int i = 0; i < view->saved_entry_count; i++) {
        if (view->saved_entries[i].kind == SavedLocationKindCurrent) {
            return i;
        }
    }
    return view->saved_entry_count > 0 ? 0 : -1;
}

static bool saved_entry_matches(const SavedLocationEntry *a,
                                const SavedLocationEntry *b) {
    if (!a || !b || a->kind != b->kind) return false;
    switch (a->kind) {
        case SavedLocationKindCurrent:
            return true;
        case SavedLocationKindPreset:
            return a->preset_index == b->preset_index;
        case SavedLocationKindCustom:
            if (a->query[0] && b->query[0]) {
                return strcmp(a->query, b->query) == 0;
            }
            if (a->label[0] && b->label[0]) {
                return strcmp(a->label, b->label) == 0;
            }
            return a->latitude_e2 == b->latitude_e2 &&
                   a->longitude_e2 == b->longitude_e2;
    }
    return false;
}

static int find_saved_entry_index(GlobeView *view,
                                  const SavedLocationEntry *needle) {
    if (!view || !needle) return -1;
    for (int i = 0; i < view->saved_entry_count; i++) {
        if (saved_entry_matches(&view->saved_entries[i], needle)) {
            return i;
        }
    }
    return -1;
}

static int find_preset_saved_entry_index(GlobeView *view, int preset_index) {
    if (!view) return -1;
    for (int i = 0; i < view->saved_entry_count; i++) {
        SavedLocationEntry *entry = &view->saved_entries[i];
        if (entry->kind == SavedLocationKindPreset &&
            entry->preset_index == preset_index) {
            return i;
        }
    }
    return -1;
}

static int find_custom_saved_entry_index(GlobeView *view,
                                         const char *label,
                                         int16_t latitude_e2,
                                         int16_t longitude_e2) {
    if (!view) return -1;
    for (int i = 0; i < view->saved_entry_count; i++) {
        SavedLocationEntry *entry = &view->saved_entries[i];
        if (entry->kind != SavedLocationKindCustom) continue;
        if (label && label[0] && strcmp(entry->label, label) == 0) {
            return i;
        }
        if (entry->latitude_e2 == latitude_e2 &&
            entry->longitude_e2 == longitude_e2) {
            return i;
        }
    }
    return -1;
}

static void update_selected_transition_target(GlobeView *view) {
    if (!view) return;
    view->transition_color_target_latitude_e2 = selected_city_latitude_e2(view);
    view->transition_color_target_longitude_e2 = selected_city_longitude_e2(view);
}

static bool selected_city_is_current_location(GlobeView *view) {
    SavedLocationEntry *entry = selected_saved_entry(view);
    return entry && entry->kind == SavedLocationKindCurrent;
}

static int globe_max_selector_index(GlobeView *view) {
    return (view && view->saved_entry_count > 0)
               ? view->saved_entry_count - 1
               : 0;
}

static int32_t selected_city_latitude_e2(GlobeView *view) {
    SavedLocationEntry *entry = selected_saved_entry(view);
    return entry ? entry->latitude_e2 : 0;
}

static int32_t selected_city_longitude_e2(GlobeView *view) {
    SavedLocationEntry *entry = selected_saved_entry(view);
    return entry ? entry->longitude_e2 : 0;
}

static bool selected_city_is_valid(GlobeView *view, int city_index) {
    return saved_entry_for_index(view, city_index) != NULL;
}

#if WEATHER_PLATFORM_TOUCH_COLOR
static bool saved_entry_has_globe_coordinates(GlobeView *view,
                                              SavedLocationEntry *entry) {
    if (!entry) return false;
    if (entry->has_coordinates) return true;
    return entry->kind == SavedLocationKindCurrent &&
           view && view->has_current_location;
}

static int16_t saved_entry_latitude_e2(GlobeView *view,
                                       SavedLocationEntry *entry) {
    if (entry && entry->kind == SavedLocationKindCurrent &&
        view && view->has_current_location) {
        return (int16_t)view->current_location_latitude_e2;
    }
    return entry ? entry->latitude_e2 : 0;
}

static int16_t saved_entry_longitude_e2(GlobeView *view,
                                        SavedLocationEntry *entry) {
    if (entry && entry->kind == SavedLocationKindCurrent &&
        view && view->has_current_location) {
        return (int16_t)view->current_location_longitude_e2;
    }
    return entry ? entry->longitude_e2 : 0;
}

static int nearest_city_index_for_orientation(GlobeView *view) {
    if (!view) return 0;

    int best_index = 0;
    int32_t best_score = INT32_MAX;
    int max_index = globe_max_selector_index(view);
    for (int i = 0; i <= max_index; i++) {
        if (!selected_city_is_valid(view, i)) continue;
        SavedLocationEntry *entry = saved_entry_for_index(view, i);
        if (!saved_entry_has_globe_coordinates(view, entry)) continue;

        int32_t candidate_latitude_e2 =
            saved_entry_latitude_e2(view, entry);
        int32_t candidate_longitude_e2 =
            saved_entry_longitude_e2(view, entry);

        int32_t lat_delta = candidate_latitude_e2 - view->color_latitude_e2;
        int32_t lon_delta = shortest_longitude_delta_e2(
            view->color_longitude_e2,
            candidate_longitude_e2);
        int32_t score = (lat_delta * lat_delta) + (lon_delta * lon_delta);
        if (score < best_score) {
            best_score = score;
            best_index = i;
        }
    }

    return best_index;
}
#endif

static GSize get_vector_or_bitmap_size(GlobeView *view) {
    if (view->bw_sequence) {
        return gdraw_command_sequence_get_bounds_size(view->bw_sequence);
    }
    if (view->bw_crumple_sequence) {
        return gdraw_command_sequence_get_bounds_size(view->bw_crumple_sequence);
    }
    if (view->cradle_pdc) {
        return gdraw_command_image_get_bounds_size(view->cradle_pdc);
    }
    return GSize(124, 144);
}

static void draw_bw_frame(GContext *ctx, GlobeView *view, GPoint origin, GSize frame_size) {
    (void)frame_size;
    if (view->bw_sequence) {
        uint32_t duration = bw_sequence_duration_for_view(view);
        uint32_t elapsed = duration > 0 ? view->bw_elapsed_ms % duration : 0;
        GDrawCommandFrame *frame =
            gdraw_command_sequence_get_frame_by_elapsed(view->bw_sequence, elapsed);
        if (!frame) {
            frame = gdraw_command_sequence_get_frame_by_index(
                view->bw_sequence,
                bw_frame_index_for_view(view, view->current_frame));
        }
        if (frame) {
            gdraw_command_frame_draw(ctx, view->bw_sequence, frame, origin);
            return;
        }
    }
}

static AnimationProgress ease_in_out(AnimationProgress progress) {
    int32_t p = (int32_t)progress;
    int32_t square = weather_norm_square(p);
    int32_t shape = (3 * ANIMATION_NORMALIZED_MAX) - (2 * p);
    return (AnimationProgress)weather_scale_i32(shape, square,
                                                ANIMATION_NORMALIZED_MAX);
}

static int crumple_delay_index(GPoint point, GPoint center) {
    int angle = positive_modulo(
        atan2_lookup(point.y - center.y, point.x - center.x) + GLOBE_CRUMPLE_SWEEP_ANGLE,
        TRIG_MAX_ANGLE);
    int distance_from_sweep = abs(angle - (TRIG_MAX_ANGLE / 2));
    return (int)weather_scale_i32(distance_from_sweep,
                                  GLOBE_CRUMPLE_DELAY_SEGMENTS,
                                  TRIG_MAX_ANGLE / 2);
}

static AnimationProgress segmented_crumple_progress(AnimationProgress amount, int delay_index) {
    int duration = (ANIMATION_NORMALIZED_MAX * GLOBE_CRUMPLE_POINT_DURATION_NUM) /
                   GLOBE_CRUMPLE_POINT_DURATION_DEN;
    int delay_per_item = (ANIMATION_NORMALIZED_MAX - duration) /
                         GLOBE_CRUMPLE_DELAY_SEGMENTS;
    int offset = amount - (delay_index * delay_per_item);
    if (offset <= 0) return 0;

    int32_t relative = weather_scale_i32(offset, ANIMATION_NORMALIZED_MAX,
                                         duration);
    if (relative >= ANIMATION_NORMALIZED_MAX) return ANIMATION_NORMALIZED_MAX;
    return ease_in_out((AnimationProgress)relative);
}

static GPoint crumple_point(GPoint point, GPoint center, AnimationProgress amount) {
    AnimationProgress progress =
        segmented_crumple_progress(amount, crumple_delay_index(point, center));
    int scale = ANIMATION_NORMALIZED_MAX - progress;
    return GPoint(
        center.x + (int16_t)((int32_t)(point.x - center.x) * scale / ANIMATION_NORMALIZED_MAX),
        center.y + (int16_t)((int32_t)(point.y - center.y) * scale / ANIMATION_NORMALIZED_MAX)
    );
}

static void destroy_bw_crumple_sequence(GlobeView *view) {
    if (view->bw_crumple_sequence) {
        gdraw_command_sequence_destroy(view->bw_crumple_sequence);
        view->bw_crumple_sequence = NULL;
    }
    view->bw_crumple_amount = -1;
    view->bw_crumple_frame = -1;
}

typedef struct {
    GContext *ctx;
    GPoint origin;
    GPoint center;
    AnimationProgress amount;
    bool drew_command;
} DirectCrumpleContext;

static bool draw_direct_crumple_command(GDrawCommand *command,
                                        uint32_t index,
                                        void *context) {
    (void)index;
    DirectCrumpleContext *crumple = context;
    if (!command || gdraw_command_get_hidden(command)) return true;

    GDrawCommandType type = gdraw_command_get_type(command);
    GColor fill_color = gdraw_command_get_fill_color(command);
    GColor stroke_color = gdraw_command_get_stroke_color(command);
    uint8_t stroke_width = gdraw_command_get_stroke_width(command);
    uint32_t stroke_scale = ANIMATION_NORMALIZED_MAX -
                            ((uint32_t)crumple->amount * 55 / 100);
    uint8_t scaled_stroke =
        (uint8_t)((uint32_t)stroke_width * stroke_scale / ANIMATION_NORMALIZED_MAX);
    if (scaled_stroke == 0) scaled_stroke = 1;

    if (type == GDrawCommandTypeCircle) {
        uint16_t radius = gdraw_command_get_radius(command);
        AnimationProgress progress = ease_in_out(crumple->amount);
        uint16_t scaled_radius =
            (uint16_t)((uint32_t)radius * (ANIMATION_NORMALIZED_MAX - progress) /
                       ANIMATION_NORMALIZED_MAX);
        if (scaled_radius < 2) {
            return true;
        }

        GPoint center = gdraw_command_get_point(command, 0);
        center = GPoint(center.x + crumple->origin.x,
                        center.y + crumple->origin.y);

        if (!gcolor_equal(fill_color, GColorClear)) {
            graphics_context_set_fill_color(crumple->ctx, fill_color);
            graphics_fill_circle(crumple->ctx, center, scaled_radius);
        }
        if (!gcolor_equal(stroke_color, GColorClear)) {
            graphics_context_set_stroke_color(crumple->ctx, stroke_color);
            graphics_context_set_stroke_width(crumple->ctx, scaled_stroke);
            graphics_draw_circle(crumple->ctx, center, scaled_radius);
        }
        crumple->drew_command = true;
        return true;
    }

    if (type != GDrawCommandTypePath && type != GDrawCommandTypePrecisePath) {
        return true;
    }

    uint16_t num_points = gdraw_command_get_num_points(command);
    if (num_points < 3) return true;
    if (num_points > GLOBE_CRUMPLE_MAX_POINTS) {
        num_points = GLOBE_CRUMPLE_MAX_POINTS;
    }

    GPoint points[GLOBE_CRUMPLE_MAX_POINTS];
    int16_t min_x = INT16_MAX;
    int16_t min_y = INT16_MAX;
    int16_t max_x = INT16_MIN;
    int16_t max_y = INT16_MIN;
    for (uint16_t i = 0; i < num_points; i++) {
        GPoint point = gdraw_command_get_point(command, i);
        GPoint crumpled = crumple_point(point, crumple->center, crumple->amount);
        crumpled = GPoint(crumpled.x + crumple->origin.x,
                          crumpled.y + crumple->origin.y);
        points[i] = crumpled;

        if (crumpled.x < min_x) min_x = crumpled.x;
        if (crumpled.y < min_y) min_y = crumpled.y;
        if (crumpled.x > max_x) max_x = crumpled.x;
        if (crumpled.y > max_y) max_y = crumpled.y;
    }

    if ((max_x - min_x) < 2 && (max_y - min_y) < 2) {
        return true;
    }

    GPath path = {
        .num_points = num_points,
        .points = points,
        .rotation = 0,
        .offset = GPoint(0, 0),
    };

    if (!gdraw_command_get_path_open(command) &&
        !gcolor_equal(fill_color, GColorClear)) {
        graphics_context_set_fill_color(crumple->ctx, fill_color);
        gpath_draw_filled(crumple->ctx, &path);
    }
    if (!gcolor_equal(stroke_color, GColorClear)) {
        graphics_context_set_stroke_color(crumple->ctx, stroke_color);
        graphics_context_set_stroke_width(crumple->ctx, scaled_stroke);
        if (gdraw_command_get_path_open(command)) {
            gpath_draw_outline_open(crumple->ctx, &path);
        } else {
            gpath_draw_outline(crumple->ctx, &path);
        }
    }
    crumple->drew_command = true;
    return true;
}

static bool draw_bw_crumple_frame(GContext *ctx, GlobeView *view, GPoint origin,
                                  GSize frame_size, int amount) {
    if (!view || !view->bw_sequence) return false;

    int bw_frame = bw_frame_index_for_view(view, view->transition_bw_frame);
    GDrawCommandFrame *frame =
        gdraw_command_sequence_get_frame_by_index(view->bw_sequence, bw_frame);
    if (!frame) return false;

    GDrawCommandList *list = gdraw_command_frame_get_command_list(frame);
    if (!list) return false;

    DirectCrumpleContext context = {
        .ctx = ctx,
        .origin = origin,
        .center = GPoint(frame_size.w / 2, (frame_size.h / 2) + GLOBE_PLANET_CENTER_Y_OFFSET),
        .amount = (AnimationProgress)amount,
        .drew_command = false,
    };
    gdraw_command_list_iterate(list, draw_direct_crumple_command, &context);
    return context.drew_command;
}

static bool color_highlight_bw_command(GDrawCommand *command,
                                       uint32_t index,
                                       void *context) {
    (void)index;
    (void)context;

    GDrawCommandType type = gdraw_command_get_type(command);
    gdraw_command_set_stroke_color(command, GColorBlack);
    if (type == GDrawCommandTypeCircle) {
        gdraw_command_set_fill_color(command, GColorVividCerulean);
    } else if (type == GDrawCommandTypePath ||
               type == GDrawCommandTypePrecisePath) {
        gdraw_command_set_fill_color(command, GColorScreaminGreen);
    }
    return true;
}

static void destroy_bw_highlight_sequence(GlobeView *view) {
    if (view->bw_highlight_sequence) {
        gdraw_command_sequence_destroy(view->bw_highlight_sequence);
        view->bw_highlight_sequence = NULL;
    }
}

static bool prepare_bw_highlight_sequence(GlobeView *view) {
    if (!view || !view->bw_sequence) return false;
    if (view->bw_highlight_sequence) return true;

    view->bw_highlight_sequence = gdraw_command_sequence_clone(view->bw_sequence);
    if (!view->bw_highlight_sequence) return false;

    uint32_t frame_count =
        gdraw_command_sequence_get_num_frames(view->bw_highlight_sequence);
    for (uint32_t i = 0; i < frame_count; i++) {
        GDrawCommandFrame *frame =
            gdraw_command_sequence_get_frame_by_index(view->bw_highlight_sequence, i);
        if (!frame) continue;
        GDrawCommandList *list = gdraw_command_frame_get_command_list(frame);
        if (!list) continue;
        gdraw_command_list_iterate(list, color_highlight_bw_command, NULL);
    }
    return true;
}

static void draw_intro_world_frame(GContext *ctx, GlobeView *view,
                                   GPoint origin, GSize frame_size) {
    if (view && view->intro_world_selected &&
        prepare_bw_highlight_sequence(view)) {
        uint32_t duration = bw_sequence_duration_for_view(view);
        uint32_t elapsed = duration > 0 ? view->bw_elapsed_ms % duration : 0;
        GDrawCommandFrame *frame =
            gdraw_command_sequence_get_frame_by_elapsed(
                view->bw_highlight_sequence, elapsed);
        if (!frame) {
            frame = gdraw_command_sequence_get_frame_by_index(
                view->bw_highlight_sequence,
                bw_frame_index_for_view(view, view->current_frame));
        }
        if (frame) {
            gdraw_command_frame_draw(ctx, view->bw_highlight_sequence, frame, origin);
            return;
        }
    }
    draw_bw_frame(ctx, view, origin, frame_size);
}

static void draw_cradle(GContext *ctx, GlobeView *view, GPoint origin, GSize frame_size) {
    (void)frame_size;
    graphics_context_set_antialiased(ctx, false);
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_radial(
        ctx,
        GRect(
            origin.x + GLOBE_CRADLE_ARM_CENTER_X - GLOBE_CRADLE_ARM_OUTER_RADIUS,
            origin.y + GLOBE_CRADLE_ARM_CENTER_Y - GLOBE_CRADLE_ARM_OUTER_RADIUS,
            GLOBE_CRADLE_ARM_OUTER_RADIUS * 2,
            GLOBE_CRADLE_ARM_OUTER_RADIUS * 2
        ),
        GOvalScaleModeFitCircle,
        GLOBE_CRADLE_ARM_THICKNESS,
        GLOBE_CRADLE_ARM_START_ANGLE,
        GLOBE_CRADLE_ARM_END_ANGLE
    );

    if (view->cradle_pdc) {
        gdraw_command_image_draw(ctx, view->cradle_pdc, origin);
    }
    graphics_context_set_antialiased(ctx, true);
}

static int32_t normalize_longitude_e2(int32_t longitude_e2) {
    while (longitude_e2 < -18000) longitude_e2 += 36000;
    while (longitude_e2 >= 18000) longitude_e2 -= 36000;
    return longitude_e2;
}

static int32_t clamp_latitude_e2(int32_t latitude_e2) {
    if (latitude_e2 > 8900) return 8900;
    if (latitude_e2 < -8900) return -8900;
    return latitude_e2;
}

static int32_t shortest_longitude_delta_e2(int32_t from_e2, int32_t to_e2) {
    int32_t delta = normalize_longitude_e2(to_e2 - from_e2);
    if (delta >= 18000) delta -= 36000;
    if (delta < -18000) delta += 36000;
    return delta;
}

static int trigangle_from_degrees_e2(int32_t degrees_e2) {
    return (int)weather_scale_i32(degrees_e2, TRIG_MAX_ANGLE, 36000);
}

static int32_t trig_sin_q10(int angle) {
    return (int32_t)(sin_lookup(positive_modulo(angle, TRIG_MAX_ANGLE)) *
                     GLOBE_ROT_SCALE / TRIG_MAX_RATIO);
}

static int32_t trig_cos_q10(int angle) {
    return (int32_t)(cos_lookup(positive_modulo(angle, TRIG_MAX_ANGLE)) *
                     GLOBE_ROT_SCALE / TRIG_MAX_RATIO);
}

static int32_t globe_isqrt(int32_t value) {
    if (value <= 0) return 0;
    uint32_t x = (uint32_t)value;
    uint32_t result = 0;
    uint32_t bit = 1UL << 30;
    while (bit > x) bit >>= 2;
    while (bit != 0) {
        if (x >= result + bit) {
            x -= result + bit;
            result = (result >> 1) + bit;
        } else {
            result >>= 1;
        }
        bit >>= 2;
    }
    return (int32_t)result;
}

static void matrix_identity(int32_t matrix[9]) {
    matrix[0] = GLOBE_ROT_SCALE;
    matrix[1] = 0;
    matrix[2] = 0;
    matrix[3] = 0;
    matrix[4] = GLOBE_ROT_SCALE;
    matrix[5] = 0;
    matrix[6] = 0;
    matrix[7] = 0;
    matrix[8] = GLOBE_ROT_SCALE;
}

#if WEATHER_PLATFORM_TOUCH_COLOR
static void matrix_copy(int32_t destination[9], const int32_t source[9]) {
    for (int i = 0; i < 9; i++) {
        destination[i] = source[i];
    }
}

static void matrix_multiply(int32_t destination[9],
                            const int32_t left[9],
                            const int32_t right[9]) {
    int32_t result[9];
    for (int row = 0; row < 3; row++) {
        for (int column = 0; column < 3; column++) {
            int64_t value = 0;
            for (int i = 0; i < 3; i++) {
                value += (int64_t)left[(row * 3) + i] * right[(i * 3) + column];
            }
            result[(row * 3) + column] = (int32_t)(value >> GLOBE_ROT_SHIFT);
        }
    }
    matrix_copy(destination, result);
}

static void matrix_screen_rotation(int32_t matrix[9], int yaw_angle, int pitch_angle) {
    int32_t yaw[9];
    int32_t pitch[9];
    int32_t cy = trig_cos_q10(yaw_angle);
    int32_t sy = trig_sin_q10(yaw_angle);
    int32_t cp = trig_cos_q10(pitch_angle);
    int32_t sp = trig_sin_q10(pitch_angle);

    yaw[0] = cy;
    yaw[1] = 0;
    yaw[2] = sy;
    yaw[3] = 0;
    yaw[4] = GLOBE_ROT_SCALE;
    yaw[5] = 0;
    yaw[6] = -sy;
    yaw[7] = 0;
    yaw[8] = cy;

    pitch[0] = GLOBE_ROT_SCALE;
    pitch[1] = 0;
    pitch[2] = 0;
    pitch[3] = 0;
    pitch[4] = cp;
    pitch[5] = -sp;
    pitch[6] = 0;
    pitch[7] = sp;
    pitch[8] = cp;

    matrix_multiply(matrix, yaw, pitch);
}

static void normalize_axis(int32_t *x, int32_t *y, int32_t *z) {
    int32_t length = globe_isqrt((*x * *x) + (*y * *y) + (*z * *z));
    if (length <= 0) return;

    *x = (*x * GLOBE_ROT_SCALE) / length;
    *y = (*y * GLOBE_ROT_SCALE) / length;
    *z = (*z * GLOBE_ROT_SCALE) / length;
}

static void normalize_globe_rotation(GlobeView *view) {
    if (!view) return;

    int32_t rx = view->globe_rotation[0];
    int32_t ry = view->globe_rotation[3];
    int32_t rz = view->globe_rotation[6];
    int32_t ux = view->globe_rotation[1];
    int32_t uy = view->globe_rotation[4];
    int32_t uz = view->globe_rotation[7];
    normalize_axis(&rx, &ry, &rz);
    normalize_axis(&ux, &uy, &uz);

    int32_t fx = ((ry * uz) - (rz * uy)) >> GLOBE_ROT_SHIFT;
    int32_t fy = ((rz * ux) - (rx * uz)) >> GLOBE_ROT_SHIFT;
    int32_t fz = ((rx * uy) - (ry * ux)) >> GLOBE_ROT_SHIFT;
    normalize_axis(&fx, &fy, &fz);

    ux = ((fy * rz) - (fz * ry)) >> GLOBE_ROT_SHIFT;
    uy = ((fz * rx) - (fx * rz)) >> GLOBE_ROT_SHIFT;
    uz = ((fx * ry) - (fy * rx)) >> GLOBE_ROT_SHIFT;
    normalize_axis(&ux, &uy, &uz);

    view->globe_rotation[0] = rx;
    view->globe_rotation[1] = ux;
    view->globe_rotation[2] = fx;
    view->globe_rotation[3] = ry;
    view->globe_rotation[4] = uy;
    view->globe_rotation[5] = fy;
    view->globe_rotation[6] = rz;
    view->globe_rotation[7] = uz;
    view->globe_rotation[8] = fz;
}
#endif

static void matrix_from_lat_lon(int32_t matrix[9],
                                int32_t latitude_e2,
                                int32_t longitude_e2) {
    latitude_e2 = clamp_latitude_e2(latitude_e2);
    longitude_e2 = normalize_longitude_e2(longitude_e2);

    int lat_angle = trigangle_from_degrees_e2(latitude_e2);
    int lon_angle = trigangle_from_degrees_e2(longitude_e2);
    int32_t sin_lat = trig_sin_q10(lat_angle);
    int32_t cos_lat = trig_cos_q10(lat_angle);
    int32_t sin_lon = trig_sin_q10(lon_angle);
    int32_t cos_lon = trig_cos_q10(lon_angle);

    int32_t right_x = cos_lon;
    int32_t right_y = 0;
    int32_t right_z = -sin_lon;
    int32_t forward_x = (int32_t)((int64_t)sin_lon * cos_lat >> GLOBE_ROT_SHIFT);
    int32_t forward_y = sin_lat;
    int32_t forward_z = (int32_t)((int64_t)cos_lon * cos_lat >> GLOBE_ROT_SHIFT);
    int32_t up_x = (int32_t)(-((int64_t)sin_lat * sin_lon >> GLOBE_ROT_SHIFT));
    int32_t up_y = cos_lat;
    int32_t up_z = (int32_t)(-((int64_t)sin_lat * cos_lon >> GLOBE_ROT_SHIFT));

    matrix[0] = right_x;
    matrix[1] = up_x;
    matrix[2] = forward_x;
    matrix[3] = right_y;
    matrix[4] = up_y;
    matrix[5] = forward_y;
    matrix[6] = right_z;
    matrix[7] = up_z;
    matrix[8] = forward_z;
}

static void set_color_orientation(GlobeView *view,
                                  int32_t latitude_e2,
                                  int32_t longitude_e2) {
    if (!view) return;

    latitude_e2 = clamp_latitude_e2(latitude_e2);
    longitude_e2 = normalize_longitude_e2(longitude_e2);
    view->color_latitude_e2 = latitude_e2;
    view->color_longitude_e2 = longitude_e2;
    view->current_frame = bw_frame_for_longitude_e2((int16_t)longitude_e2);
    sync_bw_elapsed_to_current_frame(view);
    matrix_from_lat_lon(view->globe_rotation, latitude_e2, longitude_e2);
#if WEATHER_PLATFORM_TOUCH_COLOR
    view->starfield_offset_x_q8 =
        (positive_modulo(longitude_e2, 36000) *
         GLOBE_STARFIELD_WIDTH * GLOBE_Q8_ONE) / 36000;
    view->starfield_offset_y_q8 =
        (latitude_e2 * GLOBE_STARFIELD_HEIGHT * GLOBE_Q8_ONE) / 36000;
#endif
}

static bool load_cubemap_resource(GlobeView *view) {
    if (!view) return false;
    if (view->cubemap_data && view->cubemap_size >= GLOBE_CUBEMAP_DATA_SIZE) {
        return true;
    }

    ResHandle handle = resource_get_handle(RESOURCE_ID_GLOBE_CUBEMAP);
    size_t size = resource_size(handle);
    if (size < GLOBE_CUBEMAP_DATA_SIZE) {
        return false;
    }

    uint8_t *data = malloc(size);
    if (!data) {
        return false;
    }

    size_t loaded = resource_load(handle, data, size);
    if (loaded < GLOBE_CUBEMAP_DATA_SIZE) {
        free(data);
        return false;
    }

    if (view->cubemap_data) {
        free(view->cubemap_data);
    }
    view->cubemap_data = data;
    view->cubemap_size = loaded;
    return true;
}

static void unload_cubemap_resource(GlobeView *view) {
    if (!view || !view->cubemap_data) return;

    free(view->cubemap_data);
    view->cubemap_data = NULL;
    view->cubemap_size = 0;
}

static bool load_starfield_resource(GlobeView *view) {
    if (!view) return false;
    if (view->starfield_data && view->starfield_size > 2) return true;

    ResHandle handle = resource_get_handle(RESOURCE_ID_GLOBE_STARFIELD);
    size_t size = resource_size(handle);
    if (size <= 2) return false;

    uint8_t *data = malloc(size);
    if (!data) return false;

    size_t loaded = resource_load(handle, data, size);
    if (loaded != size) {
        free(data);
        return false;
    }

    if (view->starfield_data) free(view->starfield_data);
    view->starfield_data = data;
    view->starfield_size = loaded;
    return true;
}

static void unload_starfield_resource(GlobeView *view) {
    if (!view || !view->starfield_data) return;

    free(view->starfield_data);
    view->starfield_data = NULL;
    view->starfield_size = 0;
}

static void ensure_visual_resources(GlobeView *view) {
    if (!view) return;

    if (!view->bw_sequence) {
        view->bw_sequence = gdraw_command_sequence_create_with_resource(
            RESOURCE_ID_GLOBE_BW_SEQUENCE);
        if (view->bw_sequence) {
            view->bw_frame_count =
                gdraw_command_sequence_get_num_frames(view->bw_sequence);
            view->bw_sequence_duration_ms =
                gdraw_command_sequence_get_total_duration(view->bw_sequence);
            if (view->bw_frame_count == 0) {
                view->bw_frame_count = NUM_BW_FRAMES;
            }
            if (view->bw_sequence_duration_ms == 0) {
                view->bw_sequence_duration_ms =
                    view->bw_frame_count * GLOBE_FRAME_INTERVAL_MS;
            }
        }
    }

    if (!view->cradle_pdc) {
        view->cradle_pdc = gdraw_command_image_create_with_resource(
            RESOURCE_ID_GLOBE_CRADLE_PDC);
    }

    load_cubemap_resource(view);
    load_starfield_resource(view);
}

static void release_visual_resources(GlobeView *view) {
    if (!view) return;

    unload_cubemap_resource(view);
    unload_starfield_resource(view);
    if (view->bw_sequence) {
        gdraw_command_sequence_destroy(view->bw_sequence);
        view->bw_sequence = NULL;
    }
    destroy_bw_highlight_sequence(view);
    destroy_bw_crumple_sequence(view);
    if (view->cradle_pdc) {
        gdraw_command_image_destroy(view->cradle_pdc);
        view->cradle_pdc = NULL;
    }
}

static uint8_t cubemap_sample(GlobeView *view, int32_t x, int32_t y, int32_t z) {
    if (!view || !view->cubemap_data) {
        return GColorBlueMoonARGB8;
    }

    int32_t ax = x < 0 ? -x : x;
    int32_t ay = y < 0 ? -y : y;
    int32_t az = z < 0 ? -z : z;
    int face = 4;
    int32_t u = 0;
    int32_t v = 0;
    int32_t major = az;

    if (ax >= ay && ax >= az) {
        major = ax;
        if (x >= 0) {
            face = 0;
            u = (int32_t)(-(z * GLOBE_ROT_SCALE / major));
            v = (int32_t)(-(y * GLOBE_ROT_SCALE / major));
        } else {
            face = 1;
            u = (int32_t)(z * GLOBE_ROT_SCALE / major);
            v = (int32_t)(-(y * GLOBE_ROT_SCALE / major));
        }
    } else if (ay >= ax && ay >= az) {
        major = ay;
        if (y >= 0) {
            face = 2;
            u = (int32_t)(x * GLOBE_ROT_SCALE / major);
            v = (int32_t)(z * GLOBE_ROT_SCALE / major);
        } else {
            face = 3;
            u = (int32_t)(x * GLOBE_ROT_SCALE / major);
            v = (int32_t)(-(z * GLOBE_ROT_SCALE / major));
        }
    } else if (z >= 0) {
        face = 4;
        u = (int32_t)(x * GLOBE_ROT_SCALE / major);
        v = (int32_t)(-(y * GLOBE_ROT_SCALE / major));
    } else {
        face = 5;
        u = (int32_t)(-(x * GLOBE_ROT_SCALE / major));
        v = (int32_t)(-(y * GLOBE_ROT_SCALE / major));
    }

    int sx = ((u + GLOBE_ROT_SCALE) * GLOBE_CUBEMAP_FACE_SIZE) /
             (GLOBE_ROT_SCALE * 2);
    int sy = ((v + GLOBE_ROT_SCALE) * GLOBE_CUBEMAP_FACE_SIZE) /
             (GLOBE_ROT_SCALE * 2);
    if (sx < 0) sx = 0;
    if (sy < 0) sy = 0;
    if (sx >= GLOBE_CUBEMAP_FACE_SIZE) sx = GLOBE_CUBEMAP_FACE_SIZE - 1;
    if (sy >= GLOBE_CUBEMAP_FACE_SIZE) sy = GLOBE_CUBEMAP_FACE_SIZE - 1;

    int face_area = GLOBE_CUBEMAP_FACE_SIZE * GLOBE_CUBEMAP_FACE_SIZE;
    int pixel_index = (face * face_area) + (sy * GLOBE_CUBEMAP_FACE_SIZE) + sx;
    uint8_t packed = view->cubemap_data[pixel_index >> 1];
    uint8_t palette_index = (pixel_index & 1) ? (packed & 0x0F) : (packed >> 4);
    if (palette_index >= GLOBE_CUBEMAP_PALETTE_SIZE) {
        return GColorBlueMoonARGB8;
    }
    return GLOBE_CUBEMAP_PALETTE[palette_index];
}

static uint8_t starfield_color_from_flags(uint8_t flags) {
    switch (flags & 0x03) {
        case 3: return GColorWhiteARGB8;
        case 2: return GColorCelesteARGB8;
        default: return GColorLightGrayARGB8;
    }
}

static void draw_forward_space_fade(GContext *ctx, GRect bounds, int amount,
                                    GlobeView *view) {
    if (amount <= 0) return;

    const int full_at =
        (ANIMATION_NORMALIZED_MAX * GLOBE_SPACE_FADE_FULL_PERCENT) / 100;
    const int stars_at =
        (ANIMATION_NORMALIZED_MAX * GLOBE_SPACE_STAR_REVEAL_START_PERCENT) / 100;

    if (amount >= full_at) {
        graphics_context_set_fill_color(ctx, GColorBlack);
        graphics_fill_rect(ctx, bounds, 0, GCornerNone);
        if (amount >= stars_at) {
            GBitmap *fb = graphics_capture_frame_buffer(ctx);
            if (fb) {
                framebuffer_draw_starfield(fb, view, bounds,
                                           clip_rect_to_bounds(bounds, gbitmap_get_bounds(fb)));
                graphics_release_frame_buffer(ctx, fb);
            }
        }
        return;
    }

    int relative = weather_scale_i32(amount, ANIMATION_NORMALIZED_MAX, full_at);
    int inv = ANIMATION_NORMALIZED_MAX - relative;
    int eased = ANIMATION_NORMALIZED_MAX -
        weather_norm_square(inv);
    int coverage =
        (eased * 16 + (ANIMATION_NORMALIZED_MAX - 1)) /
        ANIMATION_NORMALIZED_MAX;
    if (coverage <= 0) return;
    if (coverage >= 16) {
        graphics_context_set_fill_color(ctx, GColorBlack);
        graphics_fill_rect(ctx, bounds, 0, GCornerNone);
        return;
    }

    GBitmap *fb = graphics_capture_frame_buffer(ctx);
    if (!fb) return;

    GRect fbb = gbitmap_get_bounds(fb);
    GRect clipped = clip_rect_to_bounds(bounds, fbb);
    int left = clipped.origin.x;
    int right = clipped.origin.x + clipped.size.w - 1;
    int bottom = clipped.origin.y + clipped.size.h;

    for (int ay = clipped.origin.y; ay < bottom; ay++) {
        GBitmapDataRowInfo ri = gbitmap_get_data_row_info(fb, (uint16_t)ay);
        int row_left = left < (int)ri.min_x ? (int)ri.min_x : left;
        int row_right = right > (int)ri.max_x ? (int)ri.max_x : right;
        for (int ax = row_left; ax <= row_right; ax++) {
            if (GLOBE_SPACE_FADE_DITHER[ay % GLOBE_SPACE_FADE_DITHER_SIZE]
                                      [ax % GLOBE_SPACE_FADE_DITHER_SIZE] <
                coverage) {
                ri.data[ax] = GColorBlackARGB8;
            }
        }
    }

    if (amount >= stars_at) {
        framebuffer_draw_starfield(fb, view, bounds, clipped);
    }
    graphics_release_frame_buffer(ctx, fb);
}

static int revealed_globe_radius(void) {
    return ((GLOBE_RENDER_BASE_DIAMETER * GLOBE_COLOR_FINAL_SCALE_PERCENT) /
            100) / 2;
}

static GPoint revealed_globe_center_for_bounds(GRect bounds, int offset) {
    return GPoint(bounds.origin.x + (bounds.size.w / 2),
                  bounds.origin.y + (bounds.size.h / 2) +
                      GLOBE_REVEALED_CENTER_Y_OFFSET + offset);
}

static GRect clip_rect_to_bounds(GRect rect, GRect bounds) {
    int left = rect.origin.x;
    int top = rect.origin.y;
    int right = rect.origin.x + rect.size.w;
    int bottom = rect.origin.y + rect.size.h;
    int bounds_right = bounds.origin.x + bounds.size.w;
    int bounds_bottom = bounds.origin.y + bounds.size.h;

    if (left < bounds.origin.x) left = bounds.origin.x;
    if (top < bounds.origin.y) top = bounds.origin.y;
    if (right > bounds_right) right = bounds_right;
    if (bottom > bounds_bottom) bottom = bounds_bottom;
    if (right <= left || bottom <= top) return GRectZero;

    return GRect(left, top, right - left, bottom - top);
}

static GRect revealed_globe_layer_frame(GRect bounds) {
    int extent = revealed_globe_radius() + GLOBE_ATMOSPHERE_GLOW_PX +
                 GLOBE_CITY_BOUNCE_PX + GLOBE_ATMOSPHERE_LAYER_MARGIN_PX;
    GPoint center = revealed_globe_center_for_bounds(bounds, 0);
    return clip_rect_to_bounds(
        GRect(center.x - extent, center.y - extent,
              (extent * 2) + 1, (extent * 2) + 1),
        bounds);
}

static void clear_framebuffer_rect(GBitmap *fb, GRect rect) {
    if (!fb || rect.size.w <= 0 || rect.size.h <= 0) return;

    GRect fbb = gbitmap_get_bounds(fb);
    GRect clipped = clip_rect_to_bounds(rect, fbb);
    int left = clipped.origin.x;
    int right = clipped.origin.x + clipped.size.w - 1;
    int bottom = clipped.origin.y + clipped.size.h;

    for (int ay = clipped.origin.y; ay < bottom; ay++) {
        GBitmapDataRowInfo ri = gbitmap_get_data_row_info(fb, (uint16_t)ay);
        int row_left = left < (int)ri.min_x ? (int)ri.min_x : left;
        int row_right = right > (int)ri.max_x ? (int)ri.max_x : right;
        for (int ax = row_left; ax <= row_right; ax++) {
            ri.data[ax] = GColorBlackARGB8;
        }
    }
}

static void framebuffer_draw_starfield(GBitmap *fb, GlobeView *view,
                                       GRect bounds, GRect clip_rect) {
    if (!fb || !view || !view->starfield_data || view->starfield_size <= 2 ||
        bounds.size.w <= 0 || bounds.size.h <= 0) {
        return;
    }

    uint16_t count = view->starfield_data[0] |
                     ((uint16_t)view->starfield_data[1] << 8);
    size_t available = (view->starfield_size - 2) / 3;
    if (count > available) count = (uint16_t)available;

    int offset_x = positive_modulo(
        (int)(view->starfield_offset_x_q8 / GLOBE_Q8_ONE),
        GLOBE_STARFIELD_WIDTH);
    int offset_y = positive_modulo(
        (int)(view->starfield_offset_y_q8 / GLOBE_Q8_ONE),
        GLOBE_STARFIELD_HEIGHT);
    int clip_left = clip_rect.origin.x;
    int clip_top = clip_rect.origin.y;
    int clip_right = clip_rect.origin.x + clip_rect.size.w - 1;
    int clip_bottom = clip_rect.origin.y + clip_rect.size.h - 1;
    const uint8_t *records = view->starfield_data + 2;

    for (uint16_t i = 0; i < count; i++) {
        uint8_t flags = records[(i * 3) + 2];
        int source_x = records[i * 3] + ((flags & 0x80) ? 256 : 0);
        int source_y = records[(i * 3) + 1];
        int first_x = positive_modulo(source_x - offset_x,
                                      GLOBE_STARFIELD_WIDTH);
        int first_y = positive_modulo(source_y - offset_y,
                                      GLOBE_STARFIELD_HEIGHT);
        for (int local_y = first_y; local_y < bounds.size.h;
             local_y += GLOBE_STARFIELD_HEIGHT) {
            for (int local_x = first_x; local_x < bounds.size.w;
                 local_x += GLOBE_STARFIELD_WIDTH) {
                int ax = bounds.origin.x + local_x;
                int ay = bounds.origin.y + local_y;
                if (ay < clip_top || ay > clip_bottom ||
                    ax < clip_left || ax > clip_right) {
                    continue;
                }

                GBitmapDataRowInfo ri =
                    gbitmap_get_data_row_info(fb, (uint16_t)ay);
                if (ax < (int)ri.min_x || ax > (int)ri.max_x) continue;
                ri.data[ax] = starfield_color_from_flags(flags);
            }
        }
    }
}

static void draw_space_background(GContext *ctx, GRect bounds, GlobeView *view) {
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_rect(ctx, bounds, 0, GCornerNone);

    if (!view || !view->starfield_data) return;

    GBitmap *fb = graphics_capture_frame_buffer(ctx);
    if (!fb) return;
    framebuffer_draw_starfield(fb, view, bounds,
                               clip_rect_to_bounds(bounds, gbitmap_get_bounds(fb)));
    graphics_release_frame_buffer(ctx, fb);
}

static void framebuffer_fill_circle(GBitmap *fb, GPoint center, int radius,
                                    uint8_t color, GRect clip_rect) {
    if (!fb || radius <= 0) return;

    GRect fbb = gbitmap_get_bounds(fb);
    GRect clipped = clip_rect_to_bounds(clip_rect, fbb);
    int radius_sq = radius * radius;
    int top = center.y - radius;
    int bottom = center.y + radius;
    int clip_left = clipped.origin.x;
    int clip_top = clipped.origin.y;
    int clip_right = clipped.origin.x + clipped.size.w - 1;
    int clip_bottom = clipped.origin.y + clipped.size.h - 1;

    for (int ay = top; ay <= bottom; ay++) {
        if (ay < clip_top || ay > clip_bottom) continue;
        int dy = ay - center.y;
        GBitmapDataRowInfo ri = gbitmap_get_data_row_info(fb, (uint16_t)ay);
        for (int ax = center.x - radius; ax <= center.x + radius; ax++) {
            if (ax < clip_left || ax > clip_right ||
                ax < (int)ri.min_x || ax > (int)ri.max_x) {
                continue;
            }
            int dx = ax - center.x;
            if ((dx * dx) + (dy * dy) <= radius_sq) {
                ri.data[ax] = color;
            }
        }
    }
}

static void framebuffer_draw_raised_pin(GBitmap *fb, GPoint globe_center,
                                        GPoint surface, GRect clip_rect) {
    int dx = surface.x - globe_center.x;
    int dy = surface.y - globe_center.y;
    int distance = globe_isqrt((dx * dx) + (dy * dy));
    GPoint head = surface;
    if (distance > 0) {
        head.x += (dx * 4) / distance;
        head.y += (dy * 4) / distance;
    } else {
        head.y -= 4;
    }

    framebuffer_fill_circle(fb, surface, 2, GColorBlackARGB8, clip_rect);
    framebuffer_fill_circle(fb, head, 5, GColorBlackARGB8, clip_rect);
    framebuffer_fill_circle(fb, head, 3, GColorWhiteARGB8, clip_rect);
}

static bool project_lat_lon_to_globe_point_with_depth(GlobeView *view,
                                                     int32_t latitude_e2,
                                                     int32_t longitude_e2,
                                                     GPoint center,
                                                     int radius,
                                                     int32_t min_front_scale,
                                                     GPoint *point_out,
                                                     int *depth_q8_out) {
    if (!view || !point_out || radius <= 0) return false;

    latitude_e2 = clamp_latitude_e2(latitude_e2);
    longitude_e2 = normalize_longitude_e2(longitude_e2);

    int lat_angle = trigangle_from_degrees_e2(latitude_e2);
    int lon_angle = trigangle_from_degrees_e2(longitude_e2);
    int32_t sin_lat = trig_sin_q10(lat_angle);
    int32_t cos_lat = trig_cos_q10(lat_angle);
    int32_t sin_lon = trig_sin_q10(lon_angle);
    int32_t cos_lon = trig_cos_q10(lon_angle);

    int32_t wx = (int32_t)((int64_t)sin_lon * cos_lat >> GLOBE_ROT_SHIFT);
    int32_t wy = sin_lat;
    int32_t wz = (int32_t)((int64_t)cos_lon * cos_lat >> GLOBE_ROT_SHIFT);

    int32_t sx =
        (int32_t)(((int64_t)view->globe_rotation[0] * wx +
                   (int64_t)view->globe_rotation[3] * wy +
                   (int64_t)view->globe_rotation[6] * wz) >>
                  GLOBE_ROT_SHIFT);
    int32_t sy =
        (int32_t)(((int64_t)view->globe_rotation[1] * wx +
                   (int64_t)view->globe_rotation[4] * wy +
                   (int64_t)view->globe_rotation[7] * wz) >>
                  GLOBE_ROT_SHIFT);
    int32_t sz =
        (int32_t)(((int64_t)view->globe_rotation[2] * wx +
                   (int64_t)view->globe_rotation[5] * wy +
                   (int64_t)view->globe_rotation[8] * wz) >>
                  GLOBE_ROT_SHIFT);

    if (sz <= min_front_scale) return false;

    point_out->x = center.x + (int)(sx * radius / GLOBE_ROT_SCALE);
    point_out->y = center.y - (int)(sy * radius / GLOBE_ROT_SCALE);
    if (depth_q8_out) {
        int depth_q8 = (sz * 256) / GLOBE_ROT_SCALE;
        if (depth_q8 < 0) depth_q8 = 0;
        else if (depth_q8 > 255) depth_q8 = 255;
        *depth_q8_out = depth_q8;
    }
    return true;
}

static bool project_lat_lon_to_globe_point(GlobeView *view,
                                           int32_t latitude_e2,
                                           int32_t longitude_e2,
                                           GPoint center,
                                           int radius,
                                           GPoint *point_out) {
    return project_lat_lon_to_globe_point_with_depth(view, latitude_e2, longitude_e2,
                                                    center, radius,
                                                    GLOBE_ROT_SCALE / 10,
                                                    point_out, NULL);
}

#if WEATHER_PLATFORM_TOUCH_COLOR
static int nearest_centered_city_index(GlobeView *view, int radius_px) {
    if (!view || !view->window || radius_px <= 0) return -1;

    Layer *root_layer = window_get_root_layer(view->window);
    if (!root_layer) return -1;

    GRect bounds = layer_get_bounds(root_layer);
    GPoint center = revealed_globe_center_for_bounds(bounds, bounce_offset(view));
    int globe_radius = revealed_globe_radius() - GLOBE_OUTLINE_PX;
    if (globe_radius <= 0) return -1;
    int best_index = -1;
    int best_distance_sq = (radius_px * radius_px) + 1;

    int max_index = globe_max_selector_index(view);
    for (int i = 0; i <= max_index; i++) {
        SavedLocationEntry *entry = saved_entry_for_index(view, i);
        if (!saved_entry_has_globe_coordinates(view, entry)) continue;

        GPoint point;
        if (!project_lat_lon_to_globe_point(view,
                                            saved_entry_latitude_e2(view, entry),
                                            saved_entry_longitude_e2(view, entry),
                                            center,
                                            globe_radius,
                                            &point)) {
            continue;
        }

        int dx = point.x - center.x;
        int dy = point.y - center.y;
        int distance_sq = (dx * dx) + (dy * dy);
        if (distance_sq < best_distance_sq) {
            best_distance_sq = distance_sq;
            best_index = i;
        }
    }

    return best_index;
}

static bool city_pin_offset_from_center(GlobeView *view,
                                        int city_index,
                                        int *dx_out,
                                        int *dy_out) {
    if (!view || !view->window || !dx_out || !dy_out) return false;

    SavedLocationEntry *entry = saved_entry_for_index(view, city_index);
    if (!saved_entry_has_globe_coordinates(view, entry)) return false;

    Layer *root_layer = window_get_root_layer(view->window);
    if (!root_layer) return false;

    GRect bounds = layer_get_bounds(root_layer);
    GPoint center = revealed_globe_center_for_bounds(bounds, bounce_offset(view));
    int globe_radius = revealed_globe_radius() - GLOBE_OUTLINE_PX;
    if (globe_radius <= 0) return false;
    GPoint point;
    if (!project_lat_lon_to_globe_point(view,
                                        saved_entry_latitude_e2(view, entry),
                                        saved_entry_longitude_e2(view, entry),
                                        center,
                                        globe_radius,
                                        &point)) {
        return false;
    }

    *dx_out = point.x - center.x;
    *dy_out = point.y - center.y;
    return true;
}

static void settle_city_pin_to_center(GlobeView *view, int city_index) {
    int deadzone_sq = GLOBE_LOCK_SETTLE_DEADZONE_PX *
                      GLOBE_LOCK_SETTLE_DEADZONE_PX;

    for (int i = 0; i < GLOBE_LOCK_SETTLE_STEPS; i++) {
        int dx;
        int dy;
        if (!city_pin_offset_from_center(view, city_index, &dx, &dy)) return;
        if ((dx * dx) + (dy * dy) <= deadzone_sq) {
            return;
        }

        int nudge_x = -dx / GLOBE_LOCK_SETTLE_DIVISOR;
        int nudge_y = -dy / GLOBE_LOCK_SETTLE_DIVISOR;
        if (nudge_x == 0 && dx != 0) nudge_x = dx < 0 ? 1 : -1;
        if (nudge_y == 0 && dy != 0) nudge_y = dy < 0 ? 1 : -1;

        apply_globe_screen_delta_q8(view,
                                    (int32_t)nudge_x * GLOBE_Q8_ONE,
                                    (int32_t)nudge_y * GLOBE_Q8_ONE);
    }
}
#endif

static void draw_saved_location_pins(GBitmap *fb, GlobeView *view,
                                     GPoint center, int radius,
                                     GRect clip_rect) {
    if (!fb || !view || !view->is_revealed || view->is_revealing) return;

    int pin_radius = radius - GLOBE_OUTLINE_PX;
    if (pin_radius <= 0) return;

    int max_index = globe_max_selector_index(view);
    for (int i = 0; i <= max_index; i++) {
        SavedLocationEntry *entry = saved_entry_for_index(view, i);
        if (!saved_entry_has_globe_coordinates(view, entry)) continue;
        if (entry->kind == SavedLocationKindCurrent) continue;

        GPoint pin;
        if (!project_lat_lon_to_globe_point_with_depth(view,
                                                       saved_entry_latitude_e2(view, entry),
                                                       saved_entry_longitude_e2(view, entry),
                                                       center, pin_radius,
                                                       GLOBE_PIN_EDGE_FRONT_Q10,
                                                       &pin, NULL)) {
            continue;
        }

        framebuffer_draw_raised_pin(fb, center, pin, clip_rect);
    }

    if (view->has_current_location) {
        GPoint pin;
        if (project_lat_lon_to_globe_point_with_depth(view,
                                                       view->current_location_latitude_e2,
                                                       view->current_location_longitude_e2,
                                                       center, pin_radius,
                                                       GLOBE_PIN_EDGE_FRONT_Q10,
                                                       &pin, NULL)) {
            framebuffer_draw_raised_pin(fb, center, pin, clip_rect);
        }
    }
}

static void draw_cubemap_globe_at_center(GContext *ctx, GlobeView *view,
                                         GPoint center, int scale_percent,
                                         GRect clip_rect,
                                         bool clear_clip) {
    if (!view || !view->cubemap_data || scale_percent <= 0) return;

    int diameter = (GLOBE_RENDER_BASE_DIAMETER * scale_percent) / 100;
    if (diameter < 4) return;

    int radius = diameter / 2;
    int radius_sq = radius * radius;
    int outline_inner_radius = radius - GLOBE_OUTLINE_PX;
    if (outline_inner_radius < 0) outline_inner_radius = 0;
    int outline_inner_radius_sq = outline_inner_radius * outline_inner_radius;
    int glow_mid_radius = radius + 3;
    int glow_outer_radius = radius + GLOBE_ATMOSPHERE_GLOW_PX;
    int glow_mid_radius_sq = glow_mid_radius * glow_mid_radius;
    int glow_outer_radius_sq = glow_outer_radius * glow_outer_radius;

    GBitmap *fb = graphics_capture_frame_buffer(ctx);
    if (!fb) return;

    GRect fbb = gbitmap_get_bounds(fb);
    GRect clipped = clip_rect_to_bounds(clip_rect, fbb);
    if (clear_clip) {
        clear_framebuffer_rect(fb, clipped);
        framebuffer_draw_starfield(fb, view, fbb, clipped);
    }

    int clip_left = clipped.origin.x;
    int clip_top = clipped.origin.y;
    int clip_right = clipped.origin.x + clipped.size.w - 1;
    int clip_bottom = clipped.origin.y + clipped.size.h - 1;

    for (int dy = -glow_outer_radius; dy <= glow_outer_radius; dy++) {
        int ay = center.y + dy;
        if (ay < clip_top || ay > clip_bottom) continue;

        int span_sq = glow_outer_radius_sq - (dy * dy);
        if (span_sq < 0) continue;
        int span = globe_isqrt(span_sq);
        GBitmapDataRowInfo ri = gbitmap_get_data_row_info(fb, (uint16_t)ay);

        for (int dx = -span; dx <= span; dx++) {
            int ax = center.x + dx;
            if (ax < clip_left || ax > clip_right ||
                ax < (int)ri.min_x || ax > (int)ri.max_x) {
                continue;
            }

            int dist_sq = (dx * dx) + (dy * dy);
            if (dist_sq > radius_sq) {
                ri.data[ax] = dist_sq > glow_mid_radius_sq
                                  ? GColorDukeBlueARGB8
                                  : GColorVividCeruleanARGB8;
                continue;
            }
            if (dist_sq >= outline_inner_radius_sq) {
                ri.data[ax] = GColorBlackARGB8;
                continue;
            }

            int32_t sx = ((int32_t)dx * GLOBE_ROT_SCALE) / radius;
            int32_t sy = ((int32_t)-dy * GLOBE_ROT_SCALE) / radius;
            int32_t sz_sq = (GLOBE_ROT_SCALE * GLOBE_ROT_SCALE) -
                            (sx * sx) - (sy * sy);
            int32_t sz = globe_isqrt(sz_sq);

            int32_t wx =
                (int32_t)(((int64_t)view->globe_rotation[0] * sx +
                           (int64_t)view->globe_rotation[1] * sy +
                           (int64_t)view->globe_rotation[2] * sz) >>
                          GLOBE_ROT_SHIFT);
            int32_t wy =
                (int32_t)(((int64_t)view->globe_rotation[3] * sx +
                           (int64_t)view->globe_rotation[4] * sy +
                           (int64_t)view->globe_rotation[5] * sz) >>
                          GLOBE_ROT_SHIFT);
            int32_t wz =
                (int32_t)(((int64_t)view->globe_rotation[6] * sx +
                           (int64_t)view->globe_rotation[7] * sy +
                           (int64_t)view->globe_rotation[8] * sz) >>
                          GLOBE_ROT_SHIFT);

            ri.data[ax] = cubemap_sample(view, wx, wy, wz);
        }
    }

    draw_saved_location_pins(fb, view, center, radius, clipped);

    graphics_release_frame_buffer(ctx, fb);
}

static void draw_cubemap_globe(GContext *ctx, GlobeView *view, GPoint origin,
                               GSize frame_size, int scale_percent,
                               GRect clip_rect, bool clear_clip) {
    if (!view || !view->cubemap_data || scale_percent <= 0) return;

    int diameter = (GLOBE_RENDER_BASE_DIAMETER * scale_percent) / 100;
    if (diameter < 4) return;

    GPoint center = GPoint(origin.x + (frame_size.w / 2),
                           origin.y + (frame_size.h / 2) +
                               GLOBE_COLOR_PLANET_CENTER_Y_OFFSET);

    draw_cubemap_globe_at_center(ctx, view, center, scale_percent,
                                 clip_rect, clear_clip);
}

static void reload_current_frame(GlobeView *view) {
    if (!view) return;
    if (view->is_revealed && !view->is_revealing) {
        update_city_label_layer(view);
    }
    mark_dynamic_globe_dirty(view);
}

static void reload_color_frame(GlobeView *view) {
    if (!view) return;
    if (view->is_revealed && !view->is_revealing) {
        update_city_label_layer(view);
    }
    mark_dynamic_globe_dirty(view);
}

static void mark_dynamic_globe_dirty(GlobeView *view) {
    if (!view) return;

    if (view->is_revealed && !view->is_revealing && view->globe_layer) {
        layer_mark_dirty(view->globe_layer);
    } else if (view->canvas_layer) {
        layer_mark_dirty(view->canvas_layer);
    }
}

static int32_t ease_out_quad(AnimationProgress progress) {
    int32_t inv = ANIMATION_NORMALIZED_MAX - (int32_t)progress;
    return ANIMATION_NORMALIZED_MAX - weather_norm_square(inv);
}

static AnimationProgress color_transition_grow_progress(int amount) {
    const int grow_start =
        (ANIMATION_NORMALIZED_MAX * GLOBE_COLOR_GROW_START_PERCENT) / 100;
    if (amount <= grow_start) {
        return 0;
    }

    return (AnimationProgress)weather_scale_i32(amount - grow_start,
                                                ANIMATION_NORMALIZED_MAX,
                                                ANIMATION_NORMALIZED_MAX -
                                                    grow_start);
}

static int color_transition_scale_percent(int amount) {
    AnimationProgress relative = color_transition_grow_progress(amount);
    if (relative <= 0) {
        return 0;
    }

    int eased = ease_out_quad(relative);
    return weather_scale_i32(eased, GLOBE_COLOR_FINAL_SCALE_PERCENT,
                             ANIMATION_NORMALIZED_MAX);
}

static int transition_color_amount(GlobeView *view) {
    int progress = (int)view->reveal_progress;
    return view->reveal_direction > 0
        ? progress
        : (ANIMATION_NORMALIZED_MAX - progress);
}

static int transition_bw_amount(GlobeView *view) {
    int amount = transition_color_amount(view);
    if (view && view->reveal_direction > 0) {
        amount = (amount * GLOBE_REVEAL_BW_SPEED_NUM) /
                 GLOBE_REVEAL_BW_SPEED_DEN;
        if (amount > ANIMATION_NORMALIZED_MAX) {
            amount = ANIMATION_NORMALIZED_MAX;
        }
    }
    return amount;
}

static int intro_selection_offset(GlobeView *view) {
    if (!view || view->intro_selection_ms == 0) return 0;
    return (view->intro_selection_ms * GLOBE_INTRO_SELECTION_OFFSET_PX) /
           GLOBE_INTRO_SELECTION_DURATION_MS;
}

static int bounce_offset(GlobeView *view) {
    if (!view || !view->bounce_anim) return 0;

    int p = (int)view->bounce_progress;
    int peak = p <= ANIMATION_NORMALIZED_MAX / 2
        ? p
        : ANIMATION_NORMALIZED_MAX - p;
    return (view->bounce_direction * GLOBE_CITY_BOUNCE_PX * peak * 2) /
           ANIMATION_NORMALIZED_MAX;
}

static void cancel_city_animation(GlobeView *view) {
    if (!view || !view->city_anim) return;

    Animation *anim = view->city_anim;
    view->city_anim = NULL;
    animation_unschedule(anim);
    animation_destroy(anim);
}

static void cancel_bounce_animation(GlobeView *view) {
    if (!view || !view->bounce_anim) return;

    Animation *anim = view->bounce_anim;
    view->bounce_anim = NULL;
    animation_unschedule(anim);
    animation_destroy(anim);
}

#if WEATHER_PLATFORM_TOUCH_COLOR
static void stop_globe_coast(GlobeView *view, bool mark_dirty) {
    if (!view) return;

    if (view->coast_timer) {
        app_timer_cancel(view->coast_timer);
        view->coast_timer = NULL;
    }
    view->coast_active = false;
    view->coast_velocity_x_q8 = 0;
    view->coast_velocity_y_q8 = 0;

    if (mark_dirty) {
        mark_dynamic_globe_dirty(view);
    }
}
#endif

static void notify_city_selected(GlobeView *view, bool force) {
    if (!view) return;

    SavedLocationEntry *entry = selected_saved_entry(view);
    if (entry && view->location_select_callback) {
        view->location_select_callback(entry->kind,
                                       entry->preset_index,
                                       entry->query,
                                       force,
                                       view->location_select_context);
    }
}

static bool commit_hovered_location(GlobeView *view) {
    if (!view || !view->is_revealed || view->is_revealing) return false;

    if (view->city_anim) {
        set_color_orientation(view,
                              view->city_anim_target_latitude_e2,
                              view->city_anim_target_longitude_e2);
        cancel_city_animation(view);
    }

#if WEATHER_PLATFORM_TOUCH_COLOR
    stop_globe_coast(view, false);
    if (view->is_free_roam) {
        int city_index = nearest_centered_city_index(view,
                                                     GLOBE_COMMIT_LOCK_RADIUS_PX);
        if (city_index < 0) {
            mark_dynamic_globe_dirty(view);
            return false;
        }
        view->selected_city_index = city_index;
        view->hover_city_index = city_index;
        view->hover_lock_active = true;
        set_color_orientation(view,
                              selected_city_latitude_e2(view),
                              selected_city_longitude_e2(view));
        set_free_roam_enabled(view, false);
    }
#endif

    update_city_label_layer(view);
    notify_city_selected(view, true);
    return true;
}

// Dictation (dictate-a-city search) is cut in the system app: the transcription
// would need the phone's geocoder (an AppMessage round-trip) which the firmware
// weather service does not provide. The dictate gesture is now inert.
static void start_city_dictation(GlobeView *view) {
    (void)view;
}

static void format_selected_label(GlobeView *view, char *buffer, size_t buffer_size) {
    if (!buffer || buffer_size == 0) return;

    SavedLocationEntry *entry = selected_saved_entry(view);
    const char *label = "Saved Location";
    if (entry && entry->label[0]) {
        label = entry->label;
    } else if (entry && entry->kind == SavedLocationKindCurrent) {
        label = "Current Location";
    } else if (entry && entry->kind == SavedLocationKindCustom) {
        label = "Dictated Location";
    }

#if PBL_ROUND
    const char *first_comma = strchr(label, ',');
    if (first_comma) {
        const char *last_comma = strrchr(label, ',');
        const char *country_start = last_comma ? last_comma + 1 : first_comma + 1;
        while (*country_start == ' ') country_start++;

        size_t city_len = (size_t)(first_comma - label);
        while (city_len > 0 && label[city_len - 1] == ' ') city_len--;

        size_t country_len = 0;
        while (country_start[country_len]) country_len++;
        while (country_len > 0 && country_start[country_len - 1] == ' ') country_len--;

        const char *abbr = NULL;
        if (country_len == 13 && strncmp(country_start, "United States", 13) == 0) abbr = "USA";
        else if (country_len == 14 && strncmp(country_start, "United Kingdom", 14) == 0) abbr = "UK";
        else if (country_len == 9 && strncmp(country_start, "Australia", 9) == 0) abbr = "AUS";
        else if (country_len == 20 && strncmp(country_start, "United Arab Emirates", 20) == 0) abbr = "UAE";
        else if (country_len == 12 && strncmp(country_start, "South Africa", 12) == 0) abbr = "SA";

        if (city_len > 0 && (abbr || country_len > 0)) {
            if (abbr) {
                snprintf(buffer, buffer_size, "%.*s, %s", (int)city_len, label, abbr);
            } else {
                snprintf(buffer, buffer_size, "%.*s, %.*s",
                         (int)city_len, label,
                         (int)country_len, country_start);
            }
            buffer[buffer_size - 1] = '\0';
            return;
        }
    }
#endif

    strncpy(buffer, label, buffer_size - 1);
    buffer[buffer_size - 1] = '\0';
}

static void update_city_label_layer(GlobeView *view) {
    if (!view || !view->city_label_layer) return;

    format_selected_label(view, view->city_label_text,
                          sizeof(view->city_label_text));
    text_layer_set_text(view->city_label_layer, view->city_label_text);
}

static void set_text_layer_hidden(TextLayer *text_layer, bool hidden) {
    if (text_layer) {
        layer_set_hidden(text_layer_get_layer(text_layer), hidden);
    }
}

static void set_free_roam_enabled(GlobeView *view, bool enabled) {
    if (!view) return;

    if (enabled) {
        cancel_city_animation(view);
        cancel_bounce_animation(view);
        view->hover_lock_active = false;
        view->hover_city_index = -1;
    }

    view->is_free_roam = enabled;

    if (view->is_revealed && !view->is_revealing) {
        if (!enabled) {
            update_city_label_layer(view);
        }
        set_text_layer_hidden(view->city_label_layer,
                              enabled && !view->hover_lock_active);
        mark_dynamic_globe_dirty(view);
    }
}

static void show_intro_canvas(GlobeView *view) {
    if (!view) return;

    if (view->window) {
        window_set_background_color(view->window, GColorWhite);
    }
    if (view->canvas_layer) layer_set_hidden(view->canvas_layer, false);
    if (view->space_layer) layer_set_hidden(view->space_layer, true);
    if (view->globe_layer) layer_set_hidden(view->globe_layer, true);
    set_text_layer_hidden(view->city_label_layer, true);

    if (view->canvas_layer) layer_mark_dirty(view->canvas_layer);
}

static void show_revealed_space_layers(GlobeView *view) {
    if (!view) return;

    if (view->window) {
        window_set_background_color(view->window, GColorBlack);
    }
    update_city_label_layer(view);

    if (view->canvas_layer) layer_set_hidden(view->canvas_layer, true);
    if (view->space_layer) {
        layer_set_hidden(view->space_layer, false);
        layer_mark_dirty(view->space_layer);
    }
    if (view->globe_layer) {
        Layer *root_layer = window_get_root_layer(view->window);
        layer_set_frame(view->globe_layer,
                        revealed_globe_layer_frame(layer_get_bounds(root_layer)));
        layer_set_hidden(view->globe_layer, false);
        layer_mark_dirty(view->globe_layer);
    }
    set_text_layer_hidden(view->city_label_layer,
                          view->is_free_roam && !view->hover_lock_active);
}

static void city_anim_update(Animation *anim, AnimationProgress progress) {
    GlobeView *view = (GlobeView *)animation_get_context(anim);
    if (!view) return;

    view->city_anim_progress = progress;

#if WEATHER_PLATFORM_TOUCH_COLOR
    if (view->hover_lock_active && view->hover_city_index >= 0) {
        int eased = ease_out_quad(progress);
        int remaining = ANIMATION_NORMALIZED_MAX - eased;
        int target_dx = weather_scale_i32(view->city_anim_longitude_delta_e2,
                                          remaining,
                                          ANIMATION_NORMALIZED_MAX);
        int target_dy = weather_scale_i32(view->city_anim_latitude_delta_e2,
                                          remaining,
                                          ANIMATION_NORMALIZED_MAX);
        int current_dx;
        int current_dy;
        if (city_pin_offset_from_center(view, view->hover_city_index,
                                        &current_dx, &current_dy)) {
            int correction_x = target_dx - current_dx;
            int correction_y = target_dy - current_dy;
            if (correction_x || correction_y) {
                apply_globe_screen_delta_q8(
                    view,
                    (int32_t)correction_x * GLOBE_Q8_ONE,
                    (int32_t)correction_y * GLOBE_Q8_ONE);
                return;
            }
        }
        mark_dynamic_globe_dirty(view);
        return;
    }
#endif

    int eased = ease_out_quad(progress);
    int32_t latitude_e2 = view->city_anim_start_latitude_e2 +
        weather_scale_i32(view->city_anim_latitude_delta_e2, eased,
                          ANIMATION_NORMALIZED_MAX);
    int32_t longitude_e2 = view->city_anim_start_longitude_e2 +
        weather_scale_i32(view->city_anim_longitude_delta_e2, eased,
                          ANIMATION_NORMALIZED_MAX);

    set_color_orientation(view, latitude_e2, longitude_e2);
    mark_dynamic_globe_dirty(view);
}

static void city_anim_stopped(Animation *anim, bool finished, void *context) {
    GlobeView *view = (GlobeView *)context;
    if (!view) return;

    bool owns_anim = view->city_anim == anim;
    if (owns_anim) {
        view->city_anim = NULL;
    }

#if WEATHER_PLATFORM_TOUCH_COLOR
    if (view->hover_lock_active && view->hover_city_index >= 0) {
        if (owns_anim && finished) {
            settle_city_pin_to_center(view, view->hover_city_index);
            update_city_label_layer(view);
            mark_dynamic_globe_dirty(view);
        }
        if (owns_anim) {
            animation_destroy(anim);
        }
        return;
    }
#endif

    if (owns_anim && finished) {
        set_color_orientation(view,
                              view->city_anim_target_latitude_e2,
                              view->city_anim_target_longitude_e2);
        reload_color_frame(view);
    }

    if (owns_anim) {
        animation_destroy(anim);
    }
}

static const AnimationImplementation s_city_anim_impl = {
    .update = city_anim_update
};

static void bounce_anim_update(Animation *anim, AnimationProgress progress) {
    GlobeView *view = (GlobeView *)animation_get_context(anim);
    if (!view) return;

    view->bounce_progress = progress;
    mark_dynamic_globe_dirty(view);
}

static void bounce_anim_stopped(Animation *anim, bool finished, void *context) {
    (void)finished;
    GlobeView *view = (GlobeView *)context;
    if (!view) return;

    bool owns_anim = view->bounce_anim == anim;
    if (owns_anim) {
        view->bounce_anim = NULL;
    }
    view->bounce_progress = 0;
    mark_dynamic_globe_dirty(view);

    if (owns_anim) {
        animation_destroy(anim);
    }
}

static const AnimationImplementation s_bounce_anim_impl = {
    .update = bounce_anim_update
};

static void start_city_bounce(GlobeView *view, bool is_down) {
    if (!view) return;

    cancel_bounce_animation(view);
    view->bounce_direction = is_down ? -1 : 1;
    view->bounce_progress = 0;
    view->bounce_anim = animation_create();
    animation_set_duration(view->bounce_anim, GLOBE_CITY_BOUNCE_DURATION_MS);
    animation_set_curve(view->bounce_anim, AnimationCurveEaseOut);
    animation_set_implementation(view->bounce_anim, &s_bounce_anim_impl);
    animation_set_handlers(view->bounce_anim,
                           (AnimationHandlers){ .stopped = bounce_anim_stopped },
                           view);
    animation_schedule(view->bounce_anim);
}

static void start_city_rotation(GlobeView *view, int city_index) {
    if (!view || view->city_anim) return;

    if (!selected_city_is_valid(view, city_index)) return;

#if WEATHER_PLATFORM_TOUCH_COLOR
    stop_globe_coast(view, false);
#endif
    cancel_bounce_animation(view);
    view->selected_city_index = city_index;
#if WEATHER_PLATFORM_TOUCH_COLOR
    set_free_roam_enabled(view, false);
    view->hover_lock_active = false;
    view->hover_city_index = -1;
#endif
    view->city_anim_start_latitude_e2 = view->color_latitude_e2;
    view->city_anim_start_longitude_e2 = view->color_longitude_e2;
    view->city_anim_target_latitude_e2 = selected_city_latitude_e2(view);
    view->city_anim_target_longitude_e2 = selected_city_longitude_e2(view);
    view->city_anim_latitude_delta_e2 = view->city_anim_target_latitude_e2 -
                                        view->city_anim_start_latitude_e2;
    view->city_anim_longitude_delta_e2 = shortest_longitude_delta_e2(
        view->city_anim_start_longitude_e2,
        view->city_anim_target_longitude_e2);
    view->city_anim_progress = 0;

    if (view->city_anim_longitude_delta_e2 == 0 &&
        view->city_anim_latitude_delta_e2 == 0) {
        update_city_label_layer(view);
        mark_dynamic_globe_dirty(view);
        return;
    }

    view->city_anim = animation_create();
    animation_set_duration(view->city_anim, GLOBE_CITY_ROTATION_DURATION_MS);
    animation_set_curve(view->city_anim, AnimationCurveEaseInOut);
    animation_set_implementation(view->city_anim, &s_city_anim_impl);
    animation_set_handlers(view->city_anim,
                           (AnimationHandlers){ .stopped = city_anim_stopped },
                           view);
    animation_schedule(view->city_anim);
    update_city_label_layer(view);
    mark_dynamic_globe_dirty(view);
}

#if WEATHER_PLATFORM_TOUCH_COLOR
static bool try_start_magnetic_city_lock(GlobeView *view, int radius_px) {
    if (!view || view->city_anim) return false;

    int city_index = nearest_centered_city_index(view, radius_px);
    if (city_index < 0) return false;

    int start_dx = 0;
    int start_dy = 0;
    bool has_offset = city_pin_offset_from_center(view, city_index,
                                                 &start_dx, &start_dy);
    int deadzone_sq = GLOBE_LOCK_SETTLE_DEADZONE_PX *
                      GLOBE_LOCK_SETTLE_DEADZONE_PX;

    stop_globe_coast(view, false);
    cancel_bounce_animation(view);
    view->selected_city_index = city_index;
    view->hover_city_index = city_index;
    view->hover_lock_active = true;
    set_free_roam_enabled(view, false);

    if (!has_offset || (start_dx * start_dx) + (start_dy * start_dy) <=
        deadzone_sq) {
        mark_dynamic_globe_dirty(view);
        return true;
    }

    view->city_anim_start_latitude_e2 = view->color_latitude_e2;
    view->city_anim_start_longitude_e2 = view->color_longitude_e2;
    view->city_anim_target_latitude_e2 = view->color_latitude_e2;
    view->city_anim_target_longitude_e2 = view->color_longitude_e2;
    view->city_anim_latitude_delta_e2 = start_dy;
    view->city_anim_longitude_delta_e2 = start_dx;
    view->city_anim_progress = 0;

    view->city_anim = animation_create();
    if (!view->city_anim) {
        settle_city_pin_to_center(view, city_index);
        mark_dynamic_globe_dirty(view);
        return true;
    }

    animation_set_duration(view->city_anim, GLOBE_LOCK_SWOOP_DURATION_MS);
    animation_set_curve(view->city_anim, AnimationCurveLinear);
    animation_set_implementation(view->city_anim, &s_city_anim_impl);
    animation_set_handlers(view->city_anim,
                           (AnimationHandlers){ .stopped = city_anim_stopped },
                           view);
    animation_schedule(view->city_anim);
    return true;
}
#endif

static void navigate_city(GlobeView *view, bool is_down) {
    if (!view || view->city_anim) return;

#if WEATHER_PLATFORM_TOUCH_COLOR
    stop_globe_coast(view, false);
    if (view->is_free_roam) {
        int nearest_index = nearest_centered_city_index(view, revealed_globe_radius());
        view->selected_city_index = nearest_index >= 0
            ? nearest_index
            : nearest_city_index_for_orientation(view);
        set_free_roam_enabled(view, false);
    }
#endif

    int current_index = view->selected_city_index;
    int max_index = globe_max_selector_index(view);
    int next_index = current_index;
    int selector_count = view->saved_entry_count > 0 ? view->saved_entry_count : 1;
    for (int attempt = 0; attempt < selector_count; attempt++) {
        next_index += is_down ? 1 : -1;
        if (next_index > max_index) {
            next_index = 0;
        } else if (next_index < 0) {
            next_index = max_index;
        }

        if (selected_city_is_valid(view, next_index)) {
            start_city_rotation(view, next_index);
            return;
        }
    }

    start_city_bounce(view, is_down);
}

#if WEATHER_PLATFORM_TOUCH_COLOR
static int32_t clamp_globe_velocity_q8(int32_t value) {
    if (value > GLOBE_COAST_MAX_SPEED_Q8) return GLOBE_COAST_MAX_SPEED_Q8;
    if (value < -GLOBE_COAST_MAX_SPEED_Q8) return -GLOBE_COAST_MAX_SPEED_Q8;
    return value;
}

static void apply_globe_screen_delta_q8(GlobeView *view,
                                        int32_t dx_q8,
                                        int32_t dy_q8) {
    if (!view || !view->is_revealed || view->is_revealing) return;
    if (dx_q8 == 0 && dy_q8 == 0) {
        return;
    }

    int yaw_angle = -(dx_q8 * GLOBE_DRAG_TRIGANGLE_PER_PX) / GLOBE_Q8_ONE;
    int pitch_angle = -(dy_q8 * GLOBE_DRAG_TRIGANGLE_PER_PX) / GLOBE_Q8_ONE;
    if (yaw_angle == 0 && pitch_angle == 0) return;

    int32_t delta[9];
    int32_t next[9];
    matrix_screen_rotation(delta, yaw_angle, pitch_angle);
    matrix_multiply(next, view->globe_rotation, delta);

    matrix_copy(view->globe_rotation, next);
    normalize_globe_rotation(view);

    view->color_longitude_e2 = normalize_longitude_e2(
        view->color_longitude_e2 - ((dx_q8 * 86) / GLOBE_Q8_ONE));
    view->color_latitude_e2 = clamp_latitude_e2(
        view->color_latitude_e2 - ((dy_q8 * 86) / GLOBE_Q8_ONE));
    view->starfield_offset_x_q8 -= (dx_q8 * 34400) / 36000;
    view->starfield_offset_y_q8 -= dy_q8;
    view->current_frame = bw_frame_for_longitude_e2((int16_t)view->color_longitude_e2);
    sync_bw_elapsed_to_current_frame(view);
    mark_dynamic_globe_dirty(view);
}

static void begin_globe_drag_capture(GlobeView *view,
                                     int16_t x,
                                     int16_t y,
                                     bool whole_screen) {
    if (!view || !view->is_revealed || view->is_revealing) return;

    view->touch_down_on_globe = point_is_on_revealed_globe(view, x, y);
    view->touch_controls_globe = whole_screen || view->touch_down_on_globe;
    if (!view->touch_controls_globe) return;

    stop_globe_coast(view, false);
    cancel_city_animation(view);
    cancel_bounce_animation(view);
    set_free_roam_enabled(view, true);
    view->touch_start_x = x;
    view->touch_start_y = y;
    view->touch_last_x = x;
    view->touch_last_y = y;
    view->touch_velocity_x_q8 = 0;
    view->touch_velocity_y_q8 = 0;
    view->coast_velocity_x_q8 = 0;
    view->coast_velocity_y_q8 = 0;
    view->touch_drag_axis_set = false;
    view->touch_drag_rotated = false;
}

static void update_globe_drag(GlobeView *view, int16_t x, int16_t y) {
    if (!view || !view->touch_controls_globe ||
        !view->is_revealed || view->is_revealing) return;

    int16_t dx = x - view->touch_last_x;
    int16_t dy = y - view->touch_last_y;
    if (dx == 0 && dy == 0) return;

    apply_globe_screen_delta_q8(view,
                                (int32_t)dx * GLOBE_Q8_ONE,
                                (int32_t)dy * GLOBE_Q8_ONE);

    view->touch_last_x = x;
    view->touch_last_y = y;
    view->touch_velocity_x_q8 = clamp_globe_velocity_q8(
        ((view->touch_velocity_x_q8 * 3) + ((int32_t)dx * GLOBE_Q8_ONE)) / 4);
    view->touch_velocity_y_q8 = clamp_globe_velocity_q8(
        ((view->touch_velocity_y_q8 * 3) + ((int32_t)dy * GLOBE_Q8_ONE)) / 4);
    view->coast_velocity_x_q8 = view->touch_velocity_x_q8;
    view->coast_velocity_y_q8 = view->touch_velocity_y_q8;

    int16_t total_dx = x - view->touch_start_x;
    int16_t total_dy = y - view->touch_start_y;
    int16_t total_adx = total_dx < 0 ? -total_dx : total_dx;
    int16_t total_ady = total_dy < 0 ? -total_dy : total_dy;
    if (total_adx > GLOBE_DRAG_AXIS_THRESHOLD ||
        total_ady > GLOBE_DRAG_AXIS_THRESHOLD) {
        view->touch_drag_axis_set = true;
        view->touch_drag_rotated = true;
    }
}

static void globe_coast_timer_handler(void *context) {
    GlobeView *view = (GlobeView *)context;
    if (!view) return;

    view->coast_timer = NULL;
    if (!view->coast_active || view->touch_active ||
        !view->is_revealed || view->is_revealing) {
        view->coast_active = false;
        view->coast_velocity_x_q8 = 0;
        view->coast_velocity_y_q8 = 0;
        return;
    }

    apply_globe_screen_delta_q8(view,
                                (view->coast_velocity_x_q8 *
                                 GLOBE_COAST_STEP_NUM) /
                                    GLOBE_COAST_STEP_DEN,
                                (view->coast_velocity_y_q8 *
                                 GLOBE_COAST_STEP_NUM) /
                                    GLOBE_COAST_STEP_DEN);

    view->coast_velocity_x_q8 =
        (view->coast_velocity_x_q8 * GLOBE_COAST_DECAY_NUM) /
        GLOBE_COAST_DECAY_DEN;
    view->coast_velocity_y_q8 =
        (view->coast_velocity_y_q8 * GLOBE_COAST_DECAY_NUM) /
        GLOBE_COAST_DECAY_DEN;

    int32_t speed = abs_i32(view->coast_velocity_x_q8) +
                    abs_i32(view->coast_velocity_y_q8);
    if (speed <= GLOBE_COAST_SETTLE_SPEED_Q8 &&
        try_start_magnetic_city_lock(view, GLOBE_LOCK_RADIUS_PX)) {
        return;
    }

    if (speed <= GLOBE_COAST_MIN_SPEED_Q8) {
        view->coast_active = false;
        view->coast_velocity_x_q8 = 0;
        view->coast_velocity_y_q8 = 0;
        mark_dynamic_globe_dirty(view);
        return;
    }

    view->coast_timer = app_timer_register(GLOBE_COAST_FRAME_MS,
                                           globe_coast_timer_handler,
                                           view);
}

static bool start_globe_coast(GlobeView *view) {
    if (!view || !view->is_revealed || view->is_revealing) return false;

    int32_t vx = clamp_globe_velocity_q8(view->touch_velocity_x_q8 * 5 / 4);
    int32_t vy = clamp_globe_velocity_q8(view->touch_velocity_y_q8 * 5 / 4);
    int32_t speed = abs_i32(vx) + abs_i32(vy);
    if (speed < GLOBE_COAST_START_SPEED_Q8) {
        view->coast_active = false;
        view->coast_velocity_x_q8 = 0;
        view->coast_velocity_y_q8 = 0;
        return false;
    }

    stop_globe_coast(view, false);
    view->coast_velocity_x_q8 = vx;
    view->coast_velocity_y_q8 = vy;
    view->coast_active = true;
    view->coast_timer = app_timer_register(GLOBE_COAST_FRAME_MS,
                                           globe_coast_timer_handler,
                                           view);
    return true;
}
#endif

static void reveal_anim_update(Animation *anim, AnimationProgress progress) {
    GlobeView *view = (GlobeView *)animation_get_context(anim);
    if (!view) return;

    view->reveal_progress = progress;
    int32_t next_latitude_e2;
    int32_t next_longitude_e2;
    if (view->reveal_direction > 0) {
        int eased = ease_out_quad(progress);
        int32_t lon_delta = shortest_longitude_delta_e2(
            view->transition_color_start_longitude_e2,
            view->transition_color_target_longitude_e2);
        int32_t lat_delta = view->transition_color_target_latitude_e2 -
                            view->transition_color_start_latitude_e2;
        next_latitude_e2 = view->transition_color_start_latitude_e2 +
            weather_scale_i32(lat_delta, eased, ANIMATION_NORMALIZED_MAX);
        int32_t base_longitude_e2 = view->transition_color_start_longitude_e2 +
            weather_scale_i32(lon_delta, eased, ANIMATION_NORMALIZED_MAX);
        int spin_eased = ease_out_quad(color_transition_grow_progress(progress));
        int32_t reveal_spin_e2 =
            weather_scale_i32(GLOBE_REVEAL_COLOR_SPIN_DEGREES_E2,
                              spin_eased, ANIMATION_NORMALIZED_MAX);
        next_longitude_e2 = normalize_longitude_e2(base_longitude_e2 - reveal_spin_e2);
    } else {
        next_latitude_e2 = 0;
        next_longitude_e2 = normalize_longitude_e2(
            view->transition_color_start_longitude_e2 -
            weather_scale_i32((int32_t)progress,
                              GLOBE_REVEAL_SPIN_STEPS * 1500,
                              ANIMATION_NORMALIZED_MAX));
    }

    set_color_orientation(view, next_latitude_e2, next_longitude_e2);
    if (view->reveal_direction < 0) {
        view->transition_bw_frame = view->current_frame;
    }
    layer_mark_dirty(view->canvas_layer);
}

static void reveal_anim_stopped(Animation *anim, bool finished, void *context) {
    GlobeView *view = (GlobeView *)context;
    if (!view) return;

    bool owns_anim = view->reveal_anim == anim;
    if (owns_anim) {
        view->reveal_anim = NULL;
    }
    view->is_revealing = false;
    destroy_bw_crumple_sequence(view);

    if (owns_anim && finished) {
        view->is_revealed = view->reveal_direction > 0;
        view->reveal_progress = 0;
        if (view->is_revealed) {
            set_color_orientation(view,
                                  view->transition_color_target_latitude_e2,
                                  view->transition_color_target_longitude_e2);
            if (view->animation_timer) {
                app_timer_cancel(view->animation_timer);
                view->animation_timer = NULL;
            }
            show_revealed_space_layers(view);
        } else {
            view->is_free_roam = false;
            int landed_bw_frame = bw_frame_index_for_view(view, view->current_frame);
            view->transition_bw_frame = landed_bw_frame;
            set_color_orientation(view, 0,
                                  longitude_e2_for_bw_frame(landed_bw_frame));
            view->current_frame = landed_bw_frame;
            sync_bw_elapsed_to_current_frame(view);
            view->bw_idle = false;
            view->bw_idle_slowdown_step = 0;
            if (view->is_animating && !view->animation_timer) {
                view->animation_timer = app_timer_register(
                    GLOBE_FRAME_INTERVAL_MS,
                    animation_timer_handler,
                    view);
            }
            start_globe_idle_timer(view);
            show_intro_canvas(view);
        }
    }

    if (owns_anim) {
        animation_destroy(anim);
    }
}

static const AnimationImplementation s_reveal_impl = {
    .update = reveal_anim_update
};

static void cancel_reveal_animation(GlobeView *view) {
    if (!view || !view->reveal_anim) return;

    Animation *anim = view->reveal_anim;
    view->reveal_anim = NULL;
    animation_unschedule(anim);
    animation_destroy(anim);
}

static void toggle_reveal(GlobeView *view) {
    if (!view) return;

    if (view->is_revealing) {
        return;
    }

#if WEATHER_PLATFORM_TOUCH_COLOR
    stop_globe_coast(view, false);
    view->hover_lock_active = false;
#endif

    if (!view->is_revealed) {
        globe_view_reload_saved_locations(view);
    }

    view->reveal_direction = view->is_revealed ? -1 : 1;
    view->is_revealing = true;
    view->transition_bw_frame = view->current_frame;
    destroy_bw_crumple_sequence(view);
    set_color_orientation(view, 0,
                          longitude_e2_for_bw_frame(view->current_frame));
    view->transition_color_start_latitude_e2 = view->color_latitude_e2;
    view->transition_color_start_longitude_e2 = view->color_longitude_e2;
    view->transition_color_target_latitude_e2 = selected_city_latitude_e2(view);
    view->transition_color_target_longitude_e2 = selected_city_longitude_e2(view);
    view->reveal_progress = 0;
    show_intro_canvas(view);

    cancel_reveal_animation(view);
    cancel_city_animation(view);
    cancel_bounce_animation(view);
    view->reveal_anim = animation_create();
    animation_set_duration(view->reveal_anim,
                           view->reveal_direction < 0
                               ? GLOBE_REVEAL_REVERSE_DURATION_MS
                               : GLOBE_REVEAL_DURATION_MS);
    animation_set_curve(view->reveal_anim, AnimationCurveEaseOut);
    animation_set_implementation(view->reveal_anim, &s_reveal_impl);
    animation_set_handlers(view->reveal_anim,
                           (AnimationHandlers){ .stopped = reveal_anim_stopped },
                           view);
    animation_schedule(view->reveal_anim);
}

static void note_globe_interaction(GlobeView *view) {
    if (!view) return;
    if (view->idle_timer) {
        app_timer_cancel(view->idle_timer);
        view->idle_timer = NULL;
    }
    view->bw_idle = false;
    view->bw_idle_slowdown_step = 0;
    if (view->is_animating && !view->is_revealed &&
        !view->animation_timer) {
        view->animation_timer = app_timer_register(
            GLOBE_FRAME_INTERVAL_MS,
            animation_timer_handler,
            view);
    }
    start_globe_idle_timer(view);
}

static void globe_idle_timer_handler(void *context) {
    GlobeView *view = (GlobeView *)context;
    if (!view) return;
    view->idle_timer = NULL;
    if (!view->is_animating || view->is_revealed || view->is_revealing) {
        return;
    }
    view->bw_idle = true;
    view->bw_idle_slowdown_step = 0;
    if (!view->animation_timer) {
        view->animation_timer = app_timer_register(
            GLOBE_FRAME_INTERVAL_MS,
            animation_timer_handler,
            view);
    }
}

static void start_globe_idle_timer(GlobeView *view) {
    if (!view || view->idle_timer) return;
    view->idle_timer = app_timer_register(GLOBE_IDLE_TIMEOUT_MS,
                                          globe_idle_timer_handler,
                                          view);
}

/**
 * Animation timer callback - advances to next frame
 */
static void animation_timer_handler(void *context) {
    GlobeView *view = (GlobeView *)context;

    if (!view || !view->is_animating || view->is_revealed) {
        if (view) view->animation_timer = NULL;
        return;
    }

    view->animation_timer = NULL;

    if (!view->is_revealing) {
        if (view->intro_selection_ms > GLOBE_FRAME_INTERVAL_MS) {
            view->intro_selection_ms -= GLOBE_FRAME_INTERVAL_MS;
        } else {
            view->intro_selection_ms = 0;
        }
        if (!view->bw_idle ||
            view->bw_idle_slowdown_step < GLOBE_IDLE_SLOWDOWN_STEPS) {
            uint32_t duration = bw_sequence_duration_for_view(view);
            view->bw_elapsed_ms = duration > 0
                ? (view->bw_elapsed_ms + GLOBE_FRAME_INTERVAL_MS) % duration
                : 0;
            view->current_frame = bw_frame_index_for_view(
                view,
                (int)(view->bw_elapsed_ms / GLOBE_FRAME_INTERVAL_MS));
            if (view->canvas_layer) {
                layer_mark_dirty(view->canvas_layer);
            }
        }
    }

    if (view->bw_idle && !view->is_revealing) {
        if (view->bw_idle_slowdown_step >= GLOBE_IDLE_SLOWDOWN_STEPS) {
            return;
        }
        view->bw_idle_slowdown_step++;
        uint32_t interval = GLOBE_FRAME_INTERVAL_MS +
            (uint32_t)view->bw_idle_slowdown_step *
            GLOBE_IDLE_SLOWDOWN_STEP_MS;
        view->animation_timer = app_timer_register(
            interval,
            animation_timer_handler,
            view);
    } else {
        view->animation_timer = app_timer_register(
            GLOBE_FRAME_INTERVAL_MS,
            animation_timer_handler,
            view);
    }
}

static void draw_transition_state(GContext *ctx, GlobeView *view, GPoint origin,
                                  GSize frame_size, GRect bounds) {
    if (!view->bw_sequence) return;
    if (!view->cradle_pdc) return;

    const int color_amount = transition_color_amount(view);
    const int bw_amount = transition_bw_amount(view);
    const int eased = ease_out_quad((AnimationProgress)bw_amount);
    const int drop = (eased * GLOBE_CRADLE_DROP_PX) / ANIMATION_NORMALIZED_MAX;

    if (view->reveal_direction > 0) {
        draw_forward_space_fade(ctx, bounds, color_amount, view);
    }

    draw_cradle(ctx, view, GPoint(origin.x, origin.y + drop), frame_size);

    int wipe_h = (frame_size.h * bw_amount) / ANIMATION_NORMALIZED_MAX;
    if (wipe_h > 0) {
        GRect wipe_rect =
            GRect(origin.x - GLOBE_CRADLE_WIPE_MARGIN,
                  origin.y + drop - GLOBE_CRADLE_WIPE_MARGIN,
                  frame_size.w + (GLOBE_CRADLE_WIPE_MARGIN * 2),
                  wipe_h + (GLOBE_CRADLE_WIPE_MARGIN * 2));

        graphics_context_set_fill_color(ctx, GColorWhite);
        graphics_fill_rect(ctx, wipe_rect,
                           0, GCornerNone);
        if (view->reveal_direction > 0) {
            draw_forward_space_fade(ctx, wipe_rect, color_amount, view);
        }
    }

    draw_cubemap_globe(ctx, view, origin, frame_size,
                       color_transition_scale_percent(color_amount),
                       bounds, false);

    if (!draw_bw_crumple_frame(ctx, view, origin, frame_size, bw_amount) &&
        bw_amount <= 0) {
        draw_bw_frame(ctx, view, origin, frame_size);
    }
}

static GRect city_label_frame_for_bounds(GRect bounds) {
    int side = GLOBE_CITY_LABEL_ROUND_SIDE_INSET;
    return GRect(side,
                 bounds.size.h - GLOBE_CITY_LABEL_HEIGHT -
                     GLOBE_CITY_LABEL_ROUND_BOTTOM_INSET,
                 bounds.size.w - (side * 2),
                 GLOBE_CITY_LABEL_HEIGHT);
}

static void draw_city_label(GContext *ctx, GlobeView *view, GRect bounds) {
    char label[48];
    format_selected_label(view, label, sizeof(label));

    GRect label_rect = city_label_frame_for_bounds(bounds);
    graphics_context_set_text_color(ctx, GColorWhite);
    graphics_draw_text(ctx, label,
                       fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                       GRect(label_rect.origin.x + 4, label_rect.origin.y + 3,
                             label_rect.size.w - 8, label_rect.size.h - 3),
                       GTextOverflowModeTrailingEllipsis,
                       GTextAlignmentCenter,
                       NULL);
}

static void draw_intro_title(GContext *ctx, GRect bounds, int globe_y,
                             GSize frame_size) {
    const char *title = "CITY SELECT";
    GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);
    int planet_center_y = globe_y + frame_size.h / 2 +
                          GLOBE_PLANET_CENTER_Y_OFFSET;
    int planet_top = planet_center_y - GLOBE_RENDER_BASE_DIAMETER / 2;
    int title_y = (planet_top - GLOBE_INTRO_TITLE_HEIGHT) / 2;
    if (title_y < 0) title_y = 0;

    graphics_context_set_text_color(ctx, GColorBlack);
    graphics_draw_text(ctx, title,
                       font,
                       GRect(0, title_y - 4,
                             bounds.size.w,
                             GLOBE_INTRO_TITLE_HEIGHT + 8),
                       GTextOverflowModeTrailingEllipsis,
                       GTextAlignmentCenter,
                       NULL);
}

static void draw_saved_locations_pin(GContext *ctx, GPoint origin,
                                     GColor color, GColor bg_color) {
    GPoint center = GPoint(origin.x + GLOBE_SAVED_COG_SIZE / 2,
                           origin.y + 7);

    graphics_context_set_antialiased(ctx, false);
    graphics_context_set_fill_color(ctx, color);

    graphics_fill_circle(ctx, center, 6);

    GPoint tail_points[] = {
        GPoint(origin.x + 4, origin.y + 9),
        GPoint(origin.x + 14, origin.y + 9),
        GPoint(origin.x + 9, origin.y + 18),
    };
    GPath tail_path = { .points = tail_points, .num_points = 3 };
    gpath_draw_filled(ctx, &tail_path);

    graphics_context_set_fill_color(ctx, bg_color);
    graphics_fill_circle(ctx, center, 2);

    graphics_context_set_antialiased(ctx, true);
}

static void draw_saved_locations_label(GContext *ctx, GlobeView *view,
                                       GRect bounds, bool selected) {
    const char *label = "saved locations";
    GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
    GSize text_size = graphics_text_layout_get_content_size(
        label,
        font,
        GRect(0, 0, bounds.size.w, GLOBE_SAVED_LABEL_HEIGHT),
        GTextOverflowModeTrailingEllipsis,
        GTextAlignmentLeft
    );
    int total_w = GLOBE_SAVED_COG_SIZE + GLOBE_SAVED_LABEL_GAP + text_size.w;
    if (total_w > bounds.size.w - 4) {
        total_w = bounds.size.w - 4;
    }

    int x = (bounds.size.w - total_w) / 2;
    int y = bounds.size.h - GLOBE_SAVED_LABEL_HEIGHT -
            GLOBE_SAVED_LABEL_ROUND_BOTTOM_INSET;
    if (selected) y += intro_selection_offset(view);
    if (selected) {
        int fill_h = PBL_IF_ROUND_ELSE(bounds.size.h - y,
                                       GLOBE_SAVED_LABEL_HEIGHT);
        graphics_context_set_fill_color(ctx, GColorVividCerulean);
        graphics_fill_rect(ctx, GRect(0, y, bounds.size.w, fill_h),
                           0, GCornerNone);
    } else {
        graphics_context_set_stroke_color(ctx, GColorLightGray);
        graphics_draw_line(ctx, GPoint(0, y),
                           GPoint(bounds.size.w, y));
    }

    GColor label_color = selected ? GColorWhite : GColorBlack;
    GColor bg_color = selected ? GColorVividCerulean : GColorWhite;
    draw_saved_locations_pin(ctx,
                             GPoint(x, y + (GLOBE_SAVED_LABEL_HEIGHT -
                                            GLOBE_SAVED_COG_SIZE) / 2),
                             label_color, bg_color);

    graphics_context_set_text_color(ctx, label_color);
    graphics_draw_text(ctx, label,
                       font,
                       GRect(x + GLOBE_SAVED_COG_SIZE + GLOBE_SAVED_LABEL_GAP,
                             y + 3,
                             bounds.size.w - x - GLOBE_SAVED_COG_SIZE -
                                 GLOBE_SAVED_LABEL_GAP,
                             GLOBE_SAVED_LABEL_HEIGHT - 3),
                       GTextOverflowModeTrailingEllipsis,
                       GTextAlignmentLeft,
                       NULL);
}

/**
 * Canvas layer draw handler - renders current globe frame
 */
static void canvas_layer_draw(Layer *layer, GContext *ctx) {
    GlobeView *view = *(GlobeView **)layer_get_data(layer);
    
    if (!view || view->current_frame >= (int)bw_frame_count_for_view(view)) return;

    // Draw frame centered in canvas.
    GRect bounds = layer_get_bounds(layer);
    GSize frame_size = get_vector_or_bitmap_size(view);
    
    // Center the planet itself; the cradle extends below the globe bitmap.
    int x = (bounds.size.w - frame_size.w) / 2;
    int y = ((bounds.size.h - frame_size.h) / 2) + GLOBE_CRADLE_CENTER_Y_OFFSET;

    if (view->is_revealing) {
        draw_transition_state(ctx, view, GPoint(x, y), frame_size, bounds);
    } else if (view->is_revealed) {
        int offset = bounce_offset(view);
        draw_space_background(ctx, bounds, view);
        draw_cubemap_globe(ctx, view, GPoint(x, y + offset),
                           frame_size, GLOBE_COLOR_FINAL_SCALE_PERCENT,
                           bounds, false);
        draw_city_label(ctx, view, bounds);
    } else {
        draw_intro_title(ctx, bounds, y, frame_size);
        if (view->intro_world_selected) {
            y += intro_selection_offset(view);
        }
        draw_cradle(ctx, view, GPoint(x, y), frame_size);
        draw_intro_world_frame(ctx, view, GPoint(x, y), frame_size);
        draw_saved_locations_label(ctx, view, bounds,
                                   !view->intro_world_selected);
    }
}

static void space_layer_draw(Layer *layer, GContext *ctx) {
    GlobeView *view = *(GlobeView **)layer_get_data(layer);
    draw_space_background(ctx, layer_get_bounds(layer), view);
}

static void globe_layer_draw(Layer *layer, GContext *ctx) {
    GlobeView *view = *(GlobeView **)layer_get_data(layer);
    if (!view || !view->is_revealed || view->is_revealing) return;

    Layer *root_layer = window_get_root_layer(view->window);
    GRect root_bounds = layer_get_bounds(root_layer);
    GRect frame = layer_get_frame(layer);
    GPoint center = revealed_globe_center_for_bounds(root_bounds,
                                                     bounce_offset(view));
    draw_cubemap_globe_at_center(ctx, view, center,
                                 GLOBE_COLOR_FINAL_SCALE_PERCENT,
                                 frame, true);
}

static void set_intro_world_selected(GlobeView *view, bool selected) {
    if (!view || view->is_revealed || view->is_revealing) return;
    if (view->intro_world_selected == selected) return;
    view->intro_world_selected = selected;
    view->intro_selection_ms = GLOBE_INTRO_SELECTION_DURATION_MS;
    if (view->canvas_layer) {
        layer_mark_dirty(view->canvas_layer);
    }
}

static void activate_intro_selection(GlobeView *view) {
    if (!view || view->is_revealing) return;
    if (view->intro_world_selected) {
        toggle_reveal(view);
    } else if (view->saved_locations_callback) {
        view->saved_locations_callback(view->saved_locations_context);
    } else {
        globe_view_pop(view);
    }
}

/**
 * Create window click handler
 */
static void window_click_handler(ClickRecognizerRef recognizer, void *context) {
    GlobeView *view = (GlobeView *)context;
    note_globe_interaction(view);

    ButtonId button = click_recognizer_get_button_id(recognizer);
    if (button == BUTTON_ID_BACK) {
        if (view->is_revealed && !view->is_revealing) {
            toggle_reveal(view);  // colour earth -> animated un-reveal back to the B+W cradle
        } else if (!view->is_revealing) {
            app_window_stack_pop_all(true);  // cradle BACK exits the app (carousel)
        }
    } else if (button == BUTTON_ID_SELECT) {
        if (!view->is_revealed) {
            activate_intro_selection(view);
        } else if (!view->is_revealing) {
            if (commit_hovered_location(view)) {
                globe_view_pop(view);
            }
        }
    } else if (button == BUTTON_ID_UP) {
        if (view->is_revealed && !view->is_revealing) {
            navigate_city(view, false);
        } else if (!view->is_revealing) {
            if (view->intro_world_selected && view->forecast_list_callback) {
                view->forecast_list_callback(view->forecast_list_context);
            } else {
                set_intro_world_selected(view, true);
            }
        }
    } else if (button == BUTTON_ID_DOWN) {
        if (view->is_revealed && !view->is_revealing) {
            navigate_city(view, true);
        } else if (!view->is_revealing) {
            if (!view->intro_world_selected && view->wrap_callback) {
                view->wrap_callback(view->wrap_context);
            } else {
                set_intro_world_selected(view, false);
            }
        }
    }
}

static void window_select_long_click_handler(ClickRecognizerRef recognizer,
                                             void *context) {
    (void)recognizer;
    GlobeView *view = (GlobeView *)context;
    note_globe_interaction(view);
    start_city_dictation(view);
}

#if WEATHER_PLATFORM_TOUCH_COLOR
static bool point_is_on_intro_globe(GlobeView *view, int16_t x, int16_t y) {
    if (!view || !view->window) return false;

    Layer *root_layer = window_get_root_layer(view->window);
    GRect bounds = layer_get_bounds(root_layer);
    GSize frame_size = get_vector_or_bitmap_size(view);
    int origin_x = (bounds.size.w - frame_size.w) / 2;
    int origin_y = ((bounds.size.h - frame_size.h) / 2) +
        GLOBE_CRADLE_CENTER_Y_OFFSET;
    int center_x = origin_x + frame_size.w / 2;
    int center_y = origin_y + frame_size.h / 2 + GLOBE_PLANET_CENTER_Y_OFFSET;
    int radius = GLOBE_RENDER_BASE_DIAMETER / 2;
    int dx = x - center_x;
    int dy = y - center_y;
    return (dx * dx) + (dy * dy) <= radius * radius;
}

static bool point_is_on_revealed_globe(GlobeView *view, int16_t x, int16_t y) {
    if (!view || !view->window) return false;

    Layer *root_layer = window_get_root_layer(view->window);
    GRect bounds = layer_get_bounds(root_layer);
    GPoint center = revealed_globe_center_for_bounds(bounds, bounce_offset(view));
    int radius = revealed_globe_radius() + GLOBE_ATMOSPHERE_GLOW_PX;
    int dx = x - center.x;
    int dy = y - center.y;
    return (dx * dx) + (dy * dy) <= radius * radius;
}

static void touch_handler(const TouchEvent *event, void *context) {
    GlobeView *view = (GlobeView *)context;
    if (!view) return;

    if (event->type == TouchEvent_Touchdown) {
        note_globe_interaction(view);
        view->touch_start_x = event->x;
        view->touch_start_y = event->y;
        view->touch_last_x = event->x;
        view->touch_last_y = event->y;
        view->touch_velocity_x_q8 = 0;
        view->touch_velocity_y_q8 = 0;
        view->touch_active = true;
        view->touch_drag_axis_set = false;
        view->touch_drag_rotated = false;
        view->touch_down_on_globe = false;
        view->touch_controls_globe = false;

        if (view->is_revealed && !view->is_revealing) {
            bool whole_screen =
                view->is_free_roam || view->coast_active ||
                view->coast_timer || view->city_anim;
            begin_globe_drag_capture(view, event->x, event->y, whole_screen);
        }

    } else if (event->type == TouchEvent_PositionUpdate) {
        if (view->is_revealed && !view->is_revealing &&
            !view->touch_active &&
            (view->is_free_roam || view->coast_active ||
             view->coast_timer || view->city_anim)) {
            view->touch_active = true;
            begin_globe_drag_capture(view, event->x, event->y, true);
            return;
        }

        if (!view->touch_active) return;

        if (view->is_revealed && !view->is_revealing &&
            view->touch_controls_globe) {
            update_globe_drag(view, event->x, event->y);
        }

    } else if (event->type == TouchEvent_Liftoff && view->touch_active) {
        view->touch_active = false;
        int16_t dx = event->x - view->touch_start_x;
        int16_t dy = event->y - view->touch_start_y;
        int16_t adx = dx < 0 ? -dx : dx;
        int16_t ady = dy < 0 ? -dy : dy;

        if (view->is_revealed && !view->is_revealing &&
            view->touch_controls_globe) {
            bool was_dragged = view->touch_drag_rotated;
            bool was_globe_tap = view->touch_down_on_globe &&
                adx <= GLOBE_TAP_THRESHOLD && ady <= GLOBE_TAP_THRESHOLD;
            view->touch_controls_globe = false;
            view->touch_down_on_globe = false;

            if (was_dragged) {
                if (start_globe_coast(view)) {
                    mark_dynamic_globe_dirty(view);
                } else if (!try_start_magnetic_city_lock(view, GLOBE_LOCK_RADIUS_PX)) {
                    mark_dynamic_globe_dirty(view);
                }
            } else if (was_globe_tap) {
                if (commit_hovered_location(view)) {
                    globe_view_pop(view);
                }
            } else {
                mark_dynamic_globe_dirty(view);
            }
            return;
        }

        view->touch_controls_globe = false;
        view->touch_down_on_globe = false;

        if (adx <= GLOBE_TAP_THRESHOLD && ady <= GLOBE_TAP_THRESHOLD) {
            if (!view->is_revealed) {
                if (point_is_on_intro_globe(view, event->x, event->y)) {
                    toggle_reveal(view);
                } else {
                    activate_intro_selection(view);
                }
            }
        } else if (view->is_revealed && !view->is_revealing &&
                   dx < -GLOBE_TAP_THRESHOLD && adx > ady) {
            weather_carousel_navigate(+1);   // swipe left = next ring page (main)
        } else if (view->is_revealed && !view->is_revealing &&
                   dx > GLOBE_TAP_THRESHOLD && adx > ady) {
            weather_carousel_navigate(-1);   // swipe right = previous ring page (clock)
        } else if (view->is_revealed && !view->is_revealing &&
                   view->touch_drag_axis_set) {
            mark_dynamic_globe_dirty(view);
        } else if (!view->is_revealed && adx > ady &&
                   adx > GLOBE_TAP_THRESHOLD && dx < 0) {
            weather_carousel_navigate(+1);   // intro swipe left = next ring page (main) [fixes the globe->list bug]
        } else if (!view->is_revealed && adx > ady &&
                   adx > GLOBE_TAP_THRESHOLD && dx > 0) {
            weather_carousel_navigate(-1);   // intro swipe right = previous ring page (clock)
        } else if (!view->is_revealed && ady > adx &&
                   ady > GLOBE_TAP_THRESHOLD && dy < 0) {
            if (view->saved_locations_callback) {
                view->saved_locations_callback(view->saved_locations_context);
            }
        }
    }
}
#endif

/**
 * Register window click handlers
 */
static void window_click_provider(void *context) {
    window_single_click_subscribe(BUTTON_ID_BACK, window_click_handler);
    window_single_click_subscribe(BUTTON_ID_SELECT, window_click_handler);
    window_long_click_subscribe(BUTTON_ID_SELECT, 500,
                                window_select_long_click_handler, NULL);
    window_single_click_subscribe(BUTTON_ID_UP, window_click_handler);
    window_single_click_subscribe(BUTTON_ID_DOWN, window_click_handler);
}

static void window_appear_handler(Window *window) {
    GlobeView *view = (GlobeView *)window_get_user_data(window);
    if (!view || !view->is_animating) return;

    ensure_visual_resources(view);
    if (!view->is_revealed && !view->animation_timer) {
        view->animation_timer = app_timer_register(
            GLOBE_FRAME_INTERVAL_MS,
            animation_timer_handler,
            view);
        start_globe_idle_timer(view);
    }
#if WEATHER_PLATFORM_TOUCH_COLOR
    touch_service_subscribe(touch_handler, view);
#endif
    mark_dynamic_globe_dirty(view);
}

static void window_disappear_handler(Window *window) {
    GlobeView *view = (GlobeView *)window_get_user_data(window);
    if (!view || !view->is_animating) return;

#if WEATHER_PLATFORM_TOUCH_COLOR
    stop_globe_coast(view, false);
#endif
    if (view->animation_timer) {
        app_timer_cancel(view->animation_timer);
        view->animation_timer = NULL;
    }
    if (view->idle_timer) {
        app_timer_cancel(view->idle_timer);
        view->idle_timer = NULL;
    }
    view->bw_idle = false;
    view->bw_idle_slowdown_step = 0;
    release_visual_resources(view);
}

/**
 * Create a new globe view
 */
GlobeView *globe_view_create(void) {
    GlobeView *view = malloc(sizeof(GlobeView));
    if (!view) return NULL;
    
    // Create window
    view->window = window_create();
    if (!view->window) {
        free(view);
        return NULL;
    }
    
    // Configure window
    window_set_user_data(view->window, view);
    window_set_background_color(view->window, GColorWhite);
    window_set_click_config_provider_with_context(view->window, window_click_provider, view);
    window_set_window_handlers(view->window, (WindowHandlers) {
        .appear = window_appear_handler,
        .disappear = window_disappear_handler,
    });
    view->canvas_layer = NULL;
    view->space_layer = NULL;
    view->globe_layer = NULL;
    view->city_label_layer = NULL;
    
    // Create canvas layer for animation
    Layer *root_layer = window_get_root_layer(view->window);
    GRect bounds = layer_get_bounds(root_layer);
    
    view->canvas_layer = layer_create_with_data(bounds, sizeof(GlobeView *));
    if (!view->canvas_layer) {
        window_destroy(view->window);
        free(view);
        return NULL;
    }
    *(GlobeView **)layer_get_data(view->canvas_layer) = view;
    layer_set_update_proc(view->canvas_layer, canvas_layer_draw);
    layer_add_child(root_layer, view->canvas_layer);

    view->space_layer = layer_create_with_data(bounds, sizeof(GlobeView *));
    if (!view->space_layer) {
        layer_destroy(view->canvas_layer);
        window_destroy(view->window);
        free(view);
        return NULL;
    }
    *(GlobeView **)layer_get_data(view->space_layer) = view;
    layer_set_update_proc(view->space_layer, space_layer_draw);
    layer_set_hidden(view->space_layer, true);
    layer_add_child(root_layer, view->space_layer);

    view->globe_layer = layer_create_with_data(
        revealed_globe_layer_frame(bounds), sizeof(GlobeView *));
    if (!view->globe_layer) {
        layer_destroy(view->space_layer);
        layer_destroy(view->canvas_layer);
        window_destroy(view->window);
        free(view);
        return NULL;
    }
    *(GlobeView **)layer_get_data(view->globe_layer) = view;
    layer_set_update_proc(view->globe_layer, globe_layer_draw);
    layer_set_hidden(view->globe_layer, true);
    layer_add_child(root_layer, view->globe_layer);

    view->city_label_layer = text_layer_create(
        city_label_frame_for_bounds(bounds));
    if (!view->city_label_layer) {
        if (view->city_label_layer) {
            text_layer_destroy(view->city_label_layer);
        }
        layer_destroy(view->globe_layer);
        layer_destroy(view->space_layer);
        layer_destroy(view->canvas_layer);
        window_destroy(view->window);
        free(view);
        return NULL;
    }

    text_layer_set_background_color(view->city_label_layer, GColorClear);
    text_layer_set_text_color(view->city_label_layer, GColorWhite);
    text_layer_set_font(view->city_label_layer,
                        fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
    text_layer_set_text_alignment(view->city_label_layer,
                                  GTextAlignmentCenter);
    text_layer_set_overflow_mode(view->city_label_layer,
                                 GTextOverflowModeTrailingEllipsis);
    text_layer_set_text(view->city_label_layer, "");
    layer_set_hidden(text_layer_get_layer(view->city_label_layer), true);
    layer_add_child(root_layer, text_layer_get_layer(view->city_label_layer));
    
    // Initialize animation state
    view->current_frame = 0;
    view->color_latitude_e2 = 0;
    view->color_longitude_e2 = 0;
    view->transition_color_start_latitude_e2 = 0;
    view->transition_color_start_longitude_e2 = 0;
    view->transition_color_target_latitude_e2 = 0;
    view->transition_color_target_longitude_e2 = 0;
    view->city_anim_start_latitude_e2 = 0;
    view->city_anim_start_longitude_e2 = 0;
    view->city_anim_target_latitude_e2 = 0;
    view->city_anim_target_longitude_e2 = 0;
    view->city_anim_latitude_delta_e2 = 0;
    view->city_anim_longitude_delta_e2 = 0;
    matrix_identity(view->globe_rotation);
    view->transition_bw_frame = 0;
    view->cubemap_data = NULL;
    view->cubemap_size = 0;
    view->starfield_data = NULL;
    view->starfield_size = 0;
    view->bw_sequence = NULL;
    view->bw_highlight_sequence = NULL;
    view->bw_crumple_sequence = NULL;
    view->bw_crumple_amount = -1;
    view->bw_crumple_frame = -1;
    view->bw_frame_count = NUM_BW_FRAMES;
    view->bw_sequence_duration_ms = NUM_BW_FRAMES * GLOBE_FRAME_INTERVAL_MS;
    view->bw_elapsed_ms = 0;
    view->cradle_pdc = NULL;
    view->animation_timer = NULL;
    view->idle_timer = NULL;
    view->reveal_anim = NULL;
    view->city_anim = NULL;
    view->bounce_anim = NULL;
    view->is_animating = false;
    view->reveal_progress = 0;
    view->city_anim_progress = 0;
    view->bounce_progress = 0;
    view->reveal_direction = 1;
    view->selected_city_index = 0;
    view->saved_entry_count = 0;
    view->current_location_latitude_e2 = 0;
    view->current_location_longitude_e2 = 0;
    view->custom_location_latitude_e2 = 0;
    view->custom_location_longitude_e2 = 0;
    view->bounce_direction = 0;
    view->current_location_label[0] = '\0';
    view->custom_location_label[0] = '\0';
    view->city_label_text[0] = '\0';
    view->location_select_callback = NULL;
    view->location_select_context = NULL;
    view->dictation_callback = NULL;
    view->dictation_context = NULL;
    view->wrap_callback = NULL;
    view->wrap_context = NULL;
    view->saved_locations_callback = NULL;
    view->saved_locations_context = NULL;
    view->forecast_list_callback = NULL;
    view->forecast_list_context = NULL;
    view->main_callback = NULL;
    view->main_context = NULL;
#if WEATHER_PLATFORM_TOUCH_COLOR
    view->coast_timer = NULL;
#endif
    view->is_revealing = false;
    view->is_revealed = false;
    view->has_current_location = false;
    view->has_custom_location = false;
    view->is_free_roam = false;
    view->intro_world_selected = false;
    view->bw_idle = false;
    view->bw_idle_slowdown_step = 0;
    view->intro_selection_ms = 0;
#if WEATHER_PLATFORM_TOUCH_COLOR
    view->touch_start_x = 0;
    view->touch_start_y = 0;
    view->touch_last_x = 0;
    view->touch_last_y = 0;
    view->touch_velocity_x_q8 = 0;
    view->touch_velocity_y_q8 = 0;
    view->coast_velocity_x_q8 = 0;
    view->coast_velocity_y_q8 = 0;
    view->starfield_offset_x_q8 = 0;
    view->starfield_offset_y_q8 = 0;
    view->hover_city_index = -1;
    view->touch_active = false;
    view->touch_drag_axis_set = false;
    view->touch_drag_rotated = false;
    view->touch_down_on_globe = false;
    view->touch_controls_globe = false;
    view->coast_active = false;
    view->hover_lock_active = false;
#endif
    globe_view_reload_saved_locations(view);
    
    return view;
}

void globe_view_set_location_select_callback(GlobeView *view,
                                             GlobeLocationSelectCallback callback,
                                             void *context) {
    if (!view) return;
    view->location_select_callback = callback;
    view->location_select_context = context;
}

void globe_view_reload_saved_locations(GlobeView *view) {
    if (!view) return;

    SavedLocationEntry previous_entry;
    bool had_previous_entry = false;
    SavedLocationEntry *current_entry = selected_saved_entry(view);
    if (current_entry) {
        previous_entry = *current_entry;
        had_previous_entry = true;
    }

    view->saved_entry_count = saved_locations_get_entries(
        view->saved_entries,
        SAVED_LOCATIONS_MAX_ENTRIES,
        view->current_location_label,
        (int16_t)view->current_location_latitude_e2,
        (int16_t)view->current_location_longitude_e2,
        view->has_current_location);

    if (view->saved_entry_count <= 0) {
        view->saved_entry_count = 1;
        view->saved_entries[0] = (SavedLocationEntry) {
            .kind = SavedLocationKindCurrent,
            .preset_index = -1,
            .latitude_e2 = (int16_t)view->current_location_latitude_e2,
            .longitude_e2 = (int16_t)view->current_location_longitude_e2,
            .has_coordinates = view->has_current_location,
        };
        snprintf(view->saved_entries[0].label,
                 sizeof(view->saved_entries[0].label),
                 "%s",
                 view->current_location_label[0]
                     ? view->current_location_label
                     : "Current Location");
        view->saved_entries[0].query[0] = '\0';
    }

    int next_index = had_previous_entry
        ? find_saved_entry_index(view, &previous_entry)
        : -1;
    if (next_index < 0) {
        next_index = find_current_saved_entry_index(view);
    }
    if (next_index < 0) next_index = 0;

    view->selected_city_index = next_index;
    update_selected_transition_target(view);

    if (view->is_revealed && !view->is_revealing && !view->is_free_roam) {
        set_color_orientation(view,
                              view->transition_color_target_latitude_e2,
                              view->transition_color_target_longitude_e2);
        reload_current_frame(view);
    } else {
        update_city_label_layer(view);
        mark_dynamic_globe_dirty(view);
    }
}

void globe_view_set_dictation_callback(GlobeView *view,
                                       GlobeDictationCallback callback,
                                       void *context) {
    if (!view) return;
    view->dictation_callback = callback;
    view->dictation_context = context;
}

void globe_view_set_wrap_callback(GlobeView *view,
                                  GlobeWrapCallback callback,
                                  void *context) {
    if (!view) return;
    view->wrap_callback = callback;
    view->wrap_context = context;
}

void globe_view_set_saved_locations_callback(GlobeView *view,
                                             GlobeSavedLocationsCallback callback,
                                             void *context) {
    if (!view) return;
    view->saved_locations_callback = callback;
    view->saved_locations_context = context;
}

void globe_view_set_forecast_list_callback(GlobeView *view,
                                           GlobeForecastListCallback callback,
                                           void *context) {
    if (!view) return;
    view->forecast_list_callback = callback;
    view->forecast_list_context = context;
}

void globe_view_set_main_callback(GlobeView *view,
                                  GlobeMainCallback callback,
                                  void *context) {
    if (!view) return;
    view->main_callback = callback;
    view->main_context = context;
}

void globe_view_set_current_location(GlobeView *view,
                                     const char *label,
                                     int16_t latitude_e2,
                                     int16_t longitude_e2) {
    if (!view) return;

    bool should_select_current =
        !view->is_animating || selected_city_is_current_location(view);

    view->has_current_location = true;
    view->current_location_latitude_e2 = latitude_e2;
    view->current_location_longitude_e2 = longitude_e2;
    if (label && label[0]) {
        strncpy(view->current_location_label, label,
                sizeof(view->current_location_label) - 1);
        view->current_location_label[sizeof(view->current_location_label) - 1] = '\0';
    } else {
        strncpy(view->current_location_label, "Current Location",
                sizeof(view->current_location_label) - 1);
        view->current_location_label[sizeof(view->current_location_label) - 1] = '\0';
    }

    globe_view_reload_saved_locations(view);

    if (should_select_current) {
        int current_index = find_current_saved_entry_index(view);
        if (current_index >= 0) {
            view->selected_city_index = current_index;
        }
        update_selected_transition_target(view);
        if (view->is_revealed && !view->is_revealing &&
            !view->is_free_roam) {
            set_color_orientation(view,
                                  view->transition_color_target_latitude_e2,
                                  view->transition_color_target_longitude_e2);
            reload_current_frame(view);
        }
    }
}

void globe_view_set_custom_location(GlobeView *view,
                                    const char *label,
                                    int16_t latitude_e2,
                                    int16_t longitude_e2,
                                    bool select) {
    if (!view) return;

    view->has_custom_location = true;
    view->custom_location_latitude_e2 = latitude_e2;
    view->custom_location_longitude_e2 = longitude_e2;
    if (label && label[0]) {
        strncpy(view->custom_location_label, label,
                sizeof(view->custom_location_label) - 1);
        view->custom_location_label[sizeof(view->custom_location_label) - 1] = '\0';
    } else {
        strncpy(view->custom_location_label, "Dictated Location",
                sizeof(view->custom_location_label) - 1);
        view->custom_location_label[sizeof(view->custom_location_label) - 1] = '\0';
    }

    globe_view_reload_saved_locations(view);

    if (select) {
        int custom_index = find_custom_saved_entry_index(view,
                                                         view->custom_location_label,
                                                         latitude_e2,
                                                         longitude_e2);
        if (custom_index >= 0) {
            view->selected_city_index = custom_index;
        }
        update_selected_transition_target(view);
        set_free_roam_enabled(view, false);
        if (view->is_revealed && !view->is_revealing) {
            set_color_orientation(view,
                                  view->transition_color_target_latitude_e2,
                                  view->transition_color_target_longitude_e2);
            reload_current_frame(view);
        }
    }
}

void globe_view_set_selected_city(GlobeView *view, int city_index) {
    if (!view || !city_presets_get(city_index)) return;

    globe_view_reload_saved_locations(view);
    int preset_entry_index = find_preset_saved_entry_index(view, city_index);
    if (preset_entry_index < 0) {
        preset_entry_index = find_current_saved_entry_index(view);
    }
    if (preset_entry_index >= 0) {
        view->selected_city_index = preset_entry_index;
    }
    update_selected_transition_target(view);
    if (view->is_revealed && !view->is_revealing && !view->is_free_roam) {
        set_color_orientation(view,
                              view->transition_color_target_latitude_e2,
                              view->transition_color_target_longitude_e2);
        reload_current_frame(view);
    }
}

/**
 * Destroy the globe view
 */
void globe_view_destroy(GlobeView *view) {
    if (!view) return;
    
    // Stop animation
    globe_view_stop_animation(view);

    cancel_reveal_animation(view);

    release_visual_resources(view);

    if (view->city_label_layer) {
        text_layer_destroy(view->city_label_layer);
        view->city_label_layer = NULL;
    }
    if (view->globe_layer) {
        layer_destroy(view->globe_layer);
        view->globe_layer = NULL;
    }
    if (view->space_layer) {
        layer_destroy(view->space_layer);
        view->space_layer = NULL;
    }
    
    // Destroy canvas layer
    if (view->canvas_layer) {
        layer_destroy(view->canvas_layer);
        view->canvas_layer = NULL;
    }
    
    // Destroy window
    if (view->window) {
        window_destroy(view->window);
        view->window = NULL;
    }
    
    free(view);
}

/**
 * Start the globe animation
 */
void globe_view_start_animation(GlobeView *view) {
    if (!view || view->is_animating) return;
    
    globe_view_reload_saved_locations(view);

    view->is_animating = true;
    view->current_frame = 0;
    view->bw_elapsed_ms = 0;
    set_color_orientation(view, 0, longitude_e2_for_bw_frame(view->current_frame));
    view->transition_color_start_latitude_e2 = 0;
    view->transition_color_start_longitude_e2 = view->color_longitude_e2;
    view->transition_color_target_latitude_e2 = selected_city_latitude_e2(view);
    view->transition_color_target_longitude_e2 = selected_city_longitude_e2(view);
    view->transition_bw_frame = 0;
    view->reveal_progress = 0;
    view->reveal_direction = 1;
    view->is_revealing = false;
    view->is_revealed = false;
    view->intro_world_selected = false;
    view->bw_idle = false;
    view->bw_idle_slowdown_step = 0;
    view->intro_selection_ms = 0;
#if WEATHER_PLATFORM_TOUCH_COLOR
    stop_globe_coast(view, false);
    view->hover_lock_active = false;
#endif
    cancel_reveal_animation(view);
    cancel_city_animation(view);
    cancel_bounce_animation(view);
    destroy_bw_crumple_sequence(view);
    ensure_visual_resources(view);

    show_intro_canvas(view);
    layer_mark_dirty(view->canvas_layer);
    view->animation_timer = app_timer_register(
        GLOBE_FRAME_INTERVAL_MS,
        animation_timer_handler,
        view
    );
    start_globe_idle_timer(view);
#if WEATHER_PLATFORM_TOUCH_COLOR
    touch_service_subscribe(touch_handler, view);
#endif
}

/**
 * Stop the globe animation
 */
void globe_view_stop_animation(GlobeView *view) {
    if (!view || !view->is_animating) return;
    
    view->is_animating = false;
    
    // Cancel animation timer
    if (view->animation_timer) {
        app_timer_cancel(view->animation_timer);
        view->animation_timer = NULL;
    }
    if (view->idle_timer) {
        app_timer_cancel(view->idle_timer);
        view->idle_timer = NULL;
    }
    view->bw_idle = false;
    view->bw_idle_slowdown_step = 0;

    cancel_reveal_animation(view);
    cancel_city_animation(view);
    cancel_bounce_animation(view);
    view->is_revealing = false;
#if WEATHER_PLATFORM_TOUCH_COLOR
    stop_globe_coast(view, false);
    touch_service_unsubscribe();
    view->touch_active = false;
    view->touch_drag_axis_set = false;
    view->touch_drag_rotated = false;
    view->touch_down_on_globe = false;
    view->touch_controls_globe = false;
    view->coast_active = false;
    view->hover_lock_active = false;
#endif

    release_visual_resources(view);
}

/**
 * Push the globe view to the window stack
 */
void globe_view_push(GlobeView *view) {
    globe_view_push_animated(view, true);
}

void globe_view_push_animated(GlobeView *view, bool animated) {
    if (!view) return;
    
    window_stack_push(view->window, animated);
    globe_view_start_animation(view);
}

/**
 * Pop the globe view from the window stack
 */
void globe_view_pop(GlobeView *view) {
    if (!view) return;

    globe_view_stop_animation(view);
    window_stack_pop(true);
}

void globe_view_dismiss(GlobeView *view, bool animated) {
    if (!view) return;

    globe_view_stop_animation(view);
    window_stack_remove(view->window, animated);
}
