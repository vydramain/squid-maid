#pragma once

// Debug view of the tilt input, the orbit with Alkoldun's trail, the centre cross and the active target
// (docs/disdoc.md, 10, 15.1). Drawing only; the state comes from squid-input.hpp, squid-orbit.hpp and the caller's samples.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

#include "squid-input.hpp"
#include "squid-orbit.hpp"
#include "squid-params.hpp"
#include "pdk/cg/rv_cg.h"
#include "pdk/cv/rv_cv.h"
#include "pdk/cv/rv_primitives.h"
#include "pdk/cv/rv_texture.h"
#include "pdk/rv_err.h"
#include "pdklib/rv_ecs/rv_ecs_frame.hpp"
#include "pdklib/rv_font/rv_font.hpp"
#include "pdklib/rv_logs/rv_logs.hpp"

namespace squid
{

// Log tag of drawing messages.
constexpr const char *SQUID_DRAW_TAG = "draw";

// Colours: the background (dark, so the lines read), the orbit's near and far halves, the trail's newest segment (older
// ones scale down), the active target's ring, the centre cross and the text ink.
constexpr rv_color SQUID_DRAW_BACKGROUND = { 12, 14, 22 };
constexpr rv_color SQUID_DRAW_RING_COLOR = { 120, 220, 160 };
// The far half: about a third of the near colour, so it reads as behind the Earth.
constexpr rv_color SQUID_DRAW_RING_BACK_COLOR = { 40, 73, 53 };
constexpr rv_color SQUID_DRAW_TRAIL_COLOR = { 255, 200, 90 };
constexpr rv_color SQUID_DRAW_TARGET_COLOR = { 255, 120, 90 };
constexpr rv_color SQUID_DRAW_CROSS_COLOR = { 230, 230, 230 };
constexpr rv_color SQUID_DRAW_TEXT_INK = { 220, 226, 240 };

// Ordering-table window: a key range that lines sort in (larger = nearer). The world window is the ECS world window, so
// the decal plate, 260 keys nearer at the same depth, covers the lines behind it. The overlay is the top key alone: no
// window lies above the decals, and keys in one bucket draw in submission order, so the overlay drawn after the scene
// lies over the plate.
struct squid_depth_window {
    int32_t lo;
    int32_t hi;
};
constexpr squid_depth_window SQUID_DRAW_WORLD_WINDOW{ rv_pdklib::ecs_depth_world_min, rv_pdklib::ecs_depth_world_max };
constexpr squid_depth_window SQUID_DRAW_OVERLAY_WINDOW{ RV_DEPTH_MAX, RV_DEPTH_MAX };
// The text sits in the overlay too.
constexpr int32_t SQUID_DRAW_TEXT_DEPTH = RV_DEPTH_MAX;

// Chords of the orbit circle: 360 / 64 = 5.6 degrees each, which reads as a circle at this frame size.
constexpr int SQUID_DRAW_RING_SEGMENTS = 64;

// Chords of the target ring: at a radius of some pixels, 32 chords read as a circle.
constexpr int SQUID_DRAW_TARGET_SEGMENTS = 32;

// The target ring is the sphere's projected radius plus a margin of 1.5 pixels, and it swings up to 2 pixels over each
// pulse of 0.8 s of game time.
constexpr float SQUID_DRAW_TARGET_MARGIN_PX = 1.5f;
constexpr float SQUID_DRAW_TARGET_PULSE_PX = 2.0f;
constexpr double SQUID_DRAW_TARGET_PULSE_PERIOD_S = 0.8;

// The console's frame rate; the trail takes one sample per frame.
constexpr int SQUID_TRAIL_FRAME_HZ = 60;
// Samples of the trail: SQUID_TRAIL_LENGTH_S at one sample per frame (0.4 s at 60 Hz is 24).
constexpr int SQUID_TRAIL_POINTS = static_cast<int>(SQUID_TRAIL_LENGTH_S * SQUID_TRAIL_FRAME_HZ + 0.5f);
using squid_trail_samples = std::array<rv_pdklib::rv_vec3, SQUID_TRAIL_POINTS>;

// Half length of each cross arm at the world centre, in world units.
constexpr float SQUID_DRAW_CROSS_HALF_LEN = 0.5f;

// The vertex coordinates are int16; a projected point beyond that range is clamped, not wrapped.
constexpr float SQUID_DRAW_COORD_LIMIT = 32767.0f;

// Integer pixel multiplier of the font: 1 draws each 8x8 glyph cell over 8x8 pixels; a fractional scale gives stems of
// uneven width.
constexpr int SQUID_DRAW_FONT_SCALE = 1;

// Glyph blocks of the font atlas: ASCII plus Cyrillic, the HUD's text in squid-texts.hpp (about 5.5 KiB of video memory).
constexpr uint32_t SQUID_DRAW_FONT_BLOCKS = rv_pdklib::rv_font_block_cyrillic;

// Clamps a coordinate to the int16 range of a vertex.
inline int16_t squid_draw_coord(float value)
{
    const float clamped = std::clamp(value, -SQUID_DRAW_COORD_LIMIT, SQUID_DRAW_COORD_LIMIT);
    return static_cast<int16_t>(std::lround(clamped));
}

// Fills a line vertex from a projected point. A non-finite coordinate has no place on screen: refused, out untouched.
inline int squid_draw_vertex(const rv_cg_screen &screen, rv_color color, rv_vertex &out)
{
    if (!std::isfinite(screen.x) || !std::isfinite(screen.y)) {
        return RV_ERR_INVAL;
    }
    out.x = squid_draw_coord(screen.x);
    out.y = squid_draw_coord(screen.y);
    out.color = color;
    return RV_OK;
}

// The colour at a fraction of its brightness, each channel scaled and rounded; the trail's fade uses it.
inline rv_color squid_draw_scale(rv_color color, float fraction)
{
    const auto channel = [fraction](uint8_t value) {
        return static_cast<uint8_t>(std::lround(value * fraction));
    };
    return rv_color{ channel(color.r), channel(color.g), channel(color.b) };
}

// Projects a world point into a window. A refused projection returns its code and writes nothing to out.
inline int squid_draw_project(rv_cg *cg, rv_pdklib::rv_vec3 point, squid_depth_window window, rv_cg_screen &out)
{
    const rv_cg_vec3 world{ point.x, point.y, point.z };
    const int64_t code = rv_cg_point(cg, &world, window.lo, window.hi, &out);
    return static_cast<int>(code);
}

// Puts one line between two projected points. A refused vertex puts nothing and returns its code.
inline int squid_draw_screen_line(rv_cv *cv, const rv_cg_screen &from, const rv_cg_screen &to, rv_color color)
{
    rv_primitive primitive{};
    primitive.type = RV_PRIMITIVE_LINE;
    // Larger depth is nearer, so the line sorts by its nearer end.
    primitive.depth = std::max(from.depth, to.depth);
    const int from_vertex_code = squid_draw_vertex(from, color, primitive.data.line.vertexes[0]);
    if (from_vertex_code != RV_OK) {
        return from_vertex_code;
    }
    const int to_vertex_code = squid_draw_vertex(to, color, primitive.data.line.vertexes[1]);
    if (to_vertex_code != RV_OK) {
        return to_vertex_code;
    }
    const int64_t put_code = rv_cv_frame_put(cv, &primitive);
    if (put_code != RV_OK) {
        return static_cast<int>(put_code);
    }
    return RV_OK;
}

// Puts one world segment as a line in the window. A refused projection or vertex puts nothing and returns its code.
inline int squid_draw_segment(rv_cg *cg,
    rv_cv *cv,
    rv_pdklib::rv_vec3 from,
    rv_pdklib::rv_vec3 to,
    rv_color color,
    squid_depth_window window)
{
    rv_cg_screen screen_from{};
    rv_cg_screen screen_to{};
    const int from_code = squid_draw_project(cg, from, window, screen_from);
    if (from_code != RV_OK) {
        return from_code;
    }
    const int to_code = squid_draw_project(cg, to, window, screen_to);
    if (to_code != RV_OK) {
        return to_code;
    }
    return squid_draw_screen_line(cv, screen_from, screen_to, color);
}

// The orbit point at index / SQUID_DRAW_RING_SEGMENTS of a turn; index == segments is the start again.
inline rv_pdklib::rv_vec3 squid_draw_ring_point(int index, rv_pdklib::fx_vec3 rotation)
{
    squid_orbit at{};
    at.phase_rad = SQUID_ORBIT_TAU_RAD * index / SQUID_DRAW_RING_SEGMENTS;
    return squid_orbit_position(at, rotation);
}

// The orbit as a closed polyline. A chord is far when its start has a smaller key than the Earth's centre: a larger key
// is nearer (rv_primitives.h), so the key order is the depth order. Stops at the first refused chord and returns its code.
inline int squid_draw_ring(rv_cg *cg, rv_cv *cv, rv_pdklib::fx_vec3 rotation)
{
    rv_cg_screen centre{};
    const int centre_code = squid_draw_project(cg, rv_pdklib::rv_vec3{ 0.0f, 0.0f, 0.0f }, SQUID_DRAW_WORLD_WINDOW, centre);
    if (centre_code != RV_OK) {
        return centre_code;
    }
    for (int i = 0; i < SQUID_DRAW_RING_SEGMENTS; ++i) {
        rv_cg_screen from{};
        rv_cg_screen to{};
        const int from_code = squid_draw_project(cg, squid_draw_ring_point(i, rotation), SQUID_DRAW_WORLD_WINDOW, from);
        if (from_code != RV_OK) {
            return from_code;
        }
        const int to_code = squid_draw_project(cg, squid_draw_ring_point(i + 1, rotation), SQUID_DRAW_WORLD_WINDOW, to);
        if (to_code != RV_OK) {
            return to_code;
        }
        const rv_color color = from.depth >= centre.depth ? SQUID_DRAW_RING_COLOR : SQUID_DRAW_RING_BACK_COLOR;
        const int code = squid_draw_screen_line(cv, from, to, color);
        if (code != RV_OK) {
            return code;
        }
    }
    return RV_OK;
}

// The trail, oldest sample first over count samples. The newest segment is full colour; each older one is dimmer.
inline int squid_draw_trail(rv_cg *cg, rv_cv *cv, const squid_trail_samples &samples, int count)
{
    for (int i = 0; i + 1 < count; ++i) {
        rv_cg_screen from{};
        rv_cg_screen to{};
        const int from_code = squid_draw_project(cg, samples[i], SQUID_DRAW_WORLD_WINDOW, from);
        if (from_code != RV_OK) {
            return from_code;
        }
        const int to_code = squid_draw_project(cg, samples[i + 1], SQUID_DRAW_WORLD_WINDOW, to);
        if (to_code != RV_OK) {
            return to_code;
        }
        // Segment i ends at sample i + 1; (i + 2) is its rank from the oldest, so the newest gets 1, the oldest 2 / count.
        const float fraction = static_cast<float>(i + 2) / static_cast<float>(count);
        const int code = squid_draw_screen_line(cv, from, to, squid_draw_scale(SQUID_DRAW_TRAIL_COLOR, fraction));
        if (code != RV_OK) {
            return code;
        }
    }
    return RV_OK;
}

// A ring of screen-space chords around the target: the sphere's projected radius plus a margin, pulsing with game time.
inline int squid_draw_target(rv_cg *cg, rv_cv *cv, rv_pdklib::rv_vec3 centre, double time_s)
{
    rv_cg_screen screen_centre{};
    const int centre_code = squid_draw_project(cg, centre, SQUID_DRAW_OVERLAY_WINDOW, screen_centre);
    if (centre_code != RV_OK) {
        return centre_code;
    }
    // The sphere's edge along world x. The chord f*r/d stands for the silhouette f*r/sqrt(d^2-r^2): within 1e-4 relative
    // at the camera distance.
    const rv_pdklib::rv_vec3 edge{ centre.x + SQUID_DEBRIS_RADIUS, centre.y, centre.z };
    rv_cg_screen screen_edge{};
    const int edge_code = squid_draw_project(cg, edge, SQUID_DRAW_OVERLAY_WINDOW, screen_edge);
    if (edge_code != RV_OK) {
        return edge_code;
    }
    const float radius_px = std::hypot(screen_edge.x - screen_centre.x, screen_edge.y - screen_centre.y);
    const double swing = 0.5 * (1.0 + std::sin(SQUID_ORBIT_TAU_RAD * time_s / SQUID_DRAW_TARGET_PULSE_PERIOD_S));
    const float ring_px = radius_px + SQUID_DRAW_TARGET_MARGIN_PX + SQUID_DRAW_TARGET_PULSE_PX * static_cast<float>(swing);
    rv_cg_screen previous{};
    for (int i = 0; i <= SQUID_DRAW_TARGET_SEGMENTS; ++i) {
        const double angle = SQUID_ORBIT_TAU_RAD * i / SQUID_DRAW_TARGET_SEGMENTS;
        rv_cg_screen point{};
        point.x = screen_centre.x + ring_px * static_cast<float>(std::cos(angle));
        point.y = screen_centre.y + ring_px * static_cast<float>(std::sin(angle));
        point.depth = SQUID_DRAW_OVERLAY_WINDOW.hi;
        if (i > 0) {
            const int code = squid_draw_screen_line(cv, previous, point, SQUID_DRAW_TARGET_COLOR);
            if (code != RV_OK) {
                return code;
            }
        }
        previous = point;
    }
    return RV_OK;
}

// Two world-axis arms through the centre: the reference the orbit's tilt is read against.
inline int squid_draw_cross(rv_cg *cg, rv_cv *cv)
{
    constexpr float half = SQUID_DRAW_CROSS_HALF_LEN;
    const int x_code = squid_draw_segment(cg,
        cv,
        rv_pdklib::rv_vec3{ -half, 0.0f, 0.0f },
        rv_pdklib::rv_vec3{ half, 0.0f, 0.0f },
        SQUID_DRAW_CROSS_COLOR,
        SQUID_DRAW_OVERLAY_WINDOW);
    if (x_code != RV_OK) {
        return x_code;
    }
    const int z_code = squid_draw_segment(cg,
        cv,
        rv_pdklib::rv_vec3{ 0.0f, 0.0f, -half },
        rv_pdklib::rv_vec3{ 0.0f, 0.0f, half },
        SQUID_DRAW_CROSS_COLOR,
        SQUID_DRAW_OVERLAY_WINDOW);
    if (z_code != RV_OK) {
        return z_code;
    }
    return RV_OK;
}

// Frees one video asset. A failed free is logged here with its code and returned to the caller.
inline int squid_draw_release(rv_cv *cv, int64_t addr)
{
    const int64_t free_code = rv_cv_video_asset_free(cv, addr);
    if (free_code != RV_OK) {
        RV_LOG_WARN(SQUID_DRAW_TAG, "video asset {} not freed, rv_err {}", addr, free_code);
        return static_cast<int>(free_code);
    }
    return RV_OK;
}

// Allocates video memory for one texture and writes it. On success addr holds the asset; on failure nothing stays
// allocated and addr is untouched.
inline int squid_draw_upload(rv_cv *cv, const rv_texture &texture, int64_t size, const char *what, int64_t &addr)
{
    const int64_t asset = rv_cv_video_asset_malloc(cv, size);
    if (asset < 0) {
        RV_LOG_WARN(SQUID_DRAW_TAG, "{}: no video memory, rv_err {}", what, asset);
        return static_cast<int>(asset);
    }
    const int64_t written = rv_cv_video_asset_write(cv, asset, &texture);
    if (written < 0) {
        RV_LOG_WARN(SQUID_DRAW_TAG, "{}: not written to video memory, rv_err {}", what, written);
        // The write's failure is the one reported; a failed free is already logged by squid_draw_release.
        (void)squid_draw_release(cv, asset);
        return static_cast<int>(written);
    }
    addr = asset;
    return RV_OK;
}

// Uploads the font atlas and the text palette. Both are built first, so a build failure allocates nothing. The
// addresses are written to addr_font and addr_palette only when both uploads succeed; a failed palette upload frees
// the atlas it has. Each failure is logged where it happens.
inline int squid_draw_font_load(rv_cv *cv, int64_t &addr_font, int64_t &addr_palette)
{
    const std::size_t atlas_size = rv_pdklib::rv_font_atlas_size_for(SQUID_DRAW_FONT_BLOCKS);
    std::vector<uint8_t> atlas(atlas_size, 0);
    const int atlas_code = rv_pdklib::rv_font_build_atlas(atlas.data(), atlas.size(), SQUID_DRAW_FONT_BLOCKS);
    if (atlas_code != RV_OK) {
        RV_LOG_WARN(SQUID_DRAW_TAG, "font atlas not built, rv_err {}", atlas_code);
        return atlas_code;
    }
    std::vector<uint16_t> palette(rv_pdklib::rv_font_palette_entries, 0);
    const int palette_code = rv_pdklib::rv_font_build_palette(SQUID_DRAW_TEXT_INK, palette.data(), palette.size());
    if (palette_code != RV_OK) {
        RV_LOG_WARN(SQUID_DRAW_TAG, "font palette not built, rv_err {}", palette_code);
        return palette_code;
    }

    const rv_texture atlas_texture = rv_pdklib::rv_font_atlas_texture(atlas.data(), SQUID_DRAW_FONT_BLOCKS);
    int64_t font_asset = 0;
    const int atlas_upload = squid_draw_upload(cv, atlas_texture, static_cast<int64_t>(atlas_size), "font atlas", font_asset);
    if (atlas_upload != RV_OK) {
        return atlas_upload;
    }
    const rv_texture palette_texture = rv_pdklib::rv_font_palette_texture(palette.data());
    int64_t palette_asset = 0;
    const int palette_upload = squid_draw_upload(cv,
        palette_texture,
        static_cast<int64_t>(rv_pdklib::rv_font_palette_size),
        "font palette",
        palette_asset);
    if (palette_upload != RV_OK) {
        // The palette's failure is the one reported; a failed free is already logged by squid_draw_release.
        (void)squid_draw_release(cv, font_asset);
        return palette_upload;
    }
    addr_font = font_asset;
    addr_palette = palette_asset;
    return RV_OK;
}

} // namespace squid
