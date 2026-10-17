#pragma once

// Debug view of the tilt input and the orbit (docs/disdoc.md, 15.1): the orbit circle, a cross at the world centre and
// the two tilt angles as text. Drawing only; the state comes from squid-input.hpp and squid-orbit.hpp.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <numbers>
#include <string>
#include <vector>

#include "squid-input.hpp"
#include "squid-orbit.hpp"
#include "squid-params.hpp"
#include "pdk/cg/rv_cg.h"
#include "pdk/cv/rv_cv.h"
#include "pdk/cv/rv_primitives.h"
#include "pdk/cv/rv_texture.h"
#include "pdk/rv_err.h"
#include "pdklib/rv_font/rv_font.hpp"
#include "pdklib/rv_logs/rv_logs.hpp"

namespace squid
{

// Log tag of drawing messages.
constexpr const char *SQUID_DRAW_TAG = "draw";

// Degrees to radians, the inverse of SQUID_RAD_TO_DEG.
constexpr float SQUID_DEG_TO_RAD = std::numbers::pi_v<float> / 180.0f;

// Colours: the view's background (dark, so the lines read), the orbit ring, the centre cross, and the ink of the text.
constexpr rv_color SQUID_DRAW_BACKGROUND = { 12, 14, 22 };
constexpr rv_color SQUID_DRAW_RING_COLOR = { 120, 220, 160 };
constexpr rv_color SQUID_DRAW_CROSS_COLOR = { 230, 230, 230 };
constexpr rv_color SQUID_DRAW_TEXT_INK = { 220, 226, 240 };

// Chords of the orbit circle: 360 / 64 = 5.6 degrees each, which reads as a circle at this frame size.
constexpr int SQUID_DRAW_RING_SEGMENTS = 64;

// Half length of each cross arm at the world centre, in world units.
constexpr float SQUID_DRAW_CROSS_HALF_LEN = 0.5f;

// Ordering-table window of the world lines (larger = nearer), and the depth of the text above them.
constexpr int32_t SQUID_DRAW_WORLD_DEPTH_LO = -400;
constexpr int32_t SQUID_DRAW_WORLD_DEPTH_HI = 400;
constexpr int32_t SQUID_DRAW_TEXT_DEPTH = 900;

// The vertex coordinates are int16; a projected point beyond that range is clamped, not wrapped.
constexpr float SQUID_DRAW_COORD_LIMIT = 32767.0f;

constexpr const char *SQUID_DRAW_AXIS_X = "X";
constexpr const char *SQUID_DRAW_AXIS_Z = "Z";
constexpr const char *SQUID_DRAW_SOURCE_SENSOR = "source sensor";
constexpr const char *SQUID_DRAW_SOURCE_RESERVE = "source reserve";

// Text sits in the top-left corner. The font is 8x8 cells, so a row step of 10 leaves a 2-pixel gap.
constexpr int SQUID_DRAW_TEXT_X = 4;
constexpr int SQUID_DRAW_TEXT_Y = 4;
constexpr int SQUID_DRAW_TEXT_ROW_STEP = 10;

// Integer pixel multiplier of the font: 1 draws each 8x8 glyph cell over 8x8 pixels; a fractional scale gives stems of
// uneven width.
constexpr int SQUID_DRAW_FONT_SCALE = 1;

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

// Puts one world segment as a line. A refused projection or vertex puts nothing and returns its code.
inline int squid_draw_segment(rv_cg *cg, rv_cv *cv, rv_pdklib::rv_vec3 from, rv_pdklib::rv_vec3 to, rv_color color)
{
    const rv_cg_vec3 world_from{ from.x, from.y, from.z };
    const rv_cg_vec3 world_to{ to.x, to.y, to.z };
    rv_cg_screen screen_from{};
    rv_cg_screen screen_to{};
    const int64_t from_code = rv_cg_point(cg, &world_from, SQUID_DRAW_WORLD_DEPTH_LO, SQUID_DRAW_WORLD_DEPTH_HI, &screen_from);
    if (from_code != RV_OK) {
        return static_cast<int>(from_code);
    }
    const int64_t to_code = rv_cg_point(cg, &world_to, SQUID_DRAW_WORLD_DEPTH_LO, SQUID_DRAW_WORLD_DEPTH_HI, &screen_to);
    if (to_code != RV_OK) {
        return static_cast<int>(to_code);
    }

    rv_primitive primitive{};
    primitive.type = RV_PRIMITIVE_LINE;
    // Larger depth is nearer, so the segment sorts by its nearer end.
    primitive.depth = std::max(screen_from.depth, screen_to.depth);
    const int from_vertex_code = squid_draw_vertex(screen_from, color, primitive.data.line.vertexes[0]);
    if (from_vertex_code != RV_OK) {
        return from_vertex_code;
    }
    const int to_vertex_code = squid_draw_vertex(screen_to, color, primitive.data.line.vertexes[1]);
    if (to_vertex_code != RV_OK) {
        return to_vertex_code;
    }
    const int64_t put_code = rv_cv_frame_put(cv, &primitive);
    if (put_code != RV_OK) {
        return static_cast<int>(put_code);
    }
    return RV_OK;
}

// The orbit point at index / SQUID_DRAW_RING_SEGMENTS of a turn; index == segments is the start again.
inline rv_pdklib::rv_vec3 squid_draw_ring_point(int index, rv_pdklib::fx_vec3 rotation)
{
    squid_orbit at{};
    at.phase_rad = SQUID_ORBIT_TAU_RAD * index / SQUID_DRAW_RING_SEGMENTS;
    return squid_orbit_position(at, rotation);
}

// The orbit as a closed polyline. Stops at the first refused segment and returns its code.
inline int squid_draw_ring(rv_cg *cg, rv_cv *cv, rv_pdklib::fx_vec3 rotation)
{
    for (int i = 0; i < SQUID_DRAW_RING_SEGMENTS; ++i) {
        const int code = squid_draw_segment(cg,
            cv,
            squid_draw_ring_point(i, rotation),
            squid_draw_ring_point(i + 1, rotation),
            SQUID_DRAW_RING_COLOR);
        if (code != RV_OK) {
            return code;
        }
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
        SQUID_DRAW_CROSS_COLOR);
    if (x_code != RV_OK) {
        return x_code;
    }
    const int z_code = squid_draw_segment(cg,
        cv,
        rv_pdklib::rv_vec3{ 0.0f, 0.0f, -half },
        rv_pdklib::rv_vec3{ 0.0f, 0.0f, half },
        SQUID_DRAW_CROSS_COLOR);
    if (z_code != RV_OK) {
        return z_code;
    }
    return RV_OK;
}

// Perspective camera above the orbit plane, looking at the Earth's centre. Sets the view for the puts that follow.
inline int squid_draw_camera(rv_cg *cg)
{
    const float elevation = SQUID_CAMERA_ELEVATION_DEG * SQUID_DEG_TO_RAD;
    rv_cg_camera camera{};
    camera.eye = rv_cg_vec3{ 0.0f, SQUID_CAMERA_DISTANCE * std::sin(elevation), SQUID_CAMERA_DISTANCE * std::cos(elevation) };
    camera.target = rv_cg_vec3{ 0.0f, 0.0f, 0.0f };
    camera.up = rv_cg_vec3{ 0.0f, 1.0f, 0.0f };
    camera.fov_y = SQUID_CAMERA_FOV_DEG * SQUID_DEG_TO_RAD;
    camera.near_plane = SQUID_CAMERA_NEAR;
    camera.far_plane = SQUID_CAMERA_FAR;
    const int64_t camera_code = rv_cg_frame_camera(cg, &camera);
    if (camera_code != RV_OK) {
        return static_cast<int>(camera_code);
    }
    return RV_OK;
}

// The two angles and the source, one row each. Returns the first refused put, or RV_OK.
inline int squid_draw_text(rv_cv *cv, rv_pdklib::rv_font_style style, squid_tilt tilt, bool sensor)
{
    const std::string angles =
        std::format("{} {:+.1f} {} {:+.1f}", SQUID_DRAW_AXIS_X, tilt.x_deg, SQUID_DRAW_AXIS_Z, tilt.z_deg);
    const char *source = sensor ? SQUID_DRAW_SOURCE_SENSOR : SQUID_DRAW_SOURCE_RESERVE;
    int64_t put_code = RV_OK;
    const auto sink = [cv, &put_code](const rv_primitive &primitive) {
        const int64_t code = rv_cv_frame_put(cv, &primitive);
        if (code != RV_OK && put_code == RV_OK) {
            put_code = code;
        }
    };
    rv_pdklib::rv_font_draw(style, SQUID_DRAW_TEXT_X, SQUID_DRAW_TEXT_Y, angles, sink);
    rv_pdklib::rv_font_draw(style, SQUID_DRAW_TEXT_X, SQUID_DRAW_TEXT_Y + SQUID_DRAW_TEXT_ROW_STEP, source, sink);
    if (put_code != RV_OK) {
        return static_cast<int>(put_code);
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
    std::vector<uint8_t> atlas(rv_pdklib::rv_font_atlas_size, 0);
    const int atlas_code = rv_pdklib::rv_font_build_atlas(atlas.data(), atlas.size());
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

    const rv_texture atlas_texture = rv_pdklib::rv_font_atlas_texture(atlas.data());
    int64_t font_asset = 0;
    const int atlas_upload =
        squid_draw_upload(cv, atlas_texture, static_cast<int64_t>(rv_pdklib::rv_font_atlas_size), "font atlas", font_asset);
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
