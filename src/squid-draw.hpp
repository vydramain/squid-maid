#pragma once

// Drawing constants, the font's video assets and the star field. Drawing only; the state comes from the caller.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <vector>

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

// Colours: the frame's clear colour (dark, so the text reads) and the text ink.
constexpr rv_color SQUID_DRAW_BACKGROUND = { 12, 14, 22 };
constexpr rv_color SQUID_DRAW_TEXT_INK = { 220, 226, 240 };

// The text sits at the top key, above everything.
constexpr int32_t SQUID_DRAW_TEXT_DEPTH = RV_DEPTH_MAX;

// Integer pixel multiplier of the font: 1 draws each 8x8 glyph cell over 8x8 pixels; a fractional scale gives stems of
// uneven width.
constexpr int SQUID_DRAW_FONT_SCALE = 1;

// Glyph blocks of the font atlas: ASCII plus Cyrillic, the HUD's text in squid-texts.hpp (about 5.5 KiB of video memory).
constexpr uint32_t SQUID_DRAW_FONT_BLOCKS = rv_pdklib::rv_font_block_cyrillic;

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

// The star field: flat sprites in screen pixels, so the sky stays put while the scene tilts.
// The lowest key: a star sorts behind every model, which covers it. Submitted before the scene, since primitives in one
// bucket draw in submission order and the models' windows reach this key too.
constexpr int32_t SQUID_STAR_DEPTH = RV_DEPTH_MIN;

// Screen the star positions are generated for: the disc's 320x240 frame.
constexpr int SQUID_STAR_SCREEN_WIDTH = 320;
constexpr int SQUID_STAR_SCREEN_HEIGHT = 240;

// Seed of the position generator: the same field on every frame and every run.
constexpr uint32_t SQUID_STAR_SEED = 0x2F5A7C3BU;

// Most stars are a single pixel; a few are 2x2 so the field has some depth.
constexpr std::size_t SQUID_STAR_SMALL_COUNT = 160;
constexpr std::size_t SQUID_STAR_LARGE_COUNT = 20;
constexpr int SQUID_STAR_SMALL_PX = 1;
constexpr int SQUID_STAR_LARGE_PX = 2;
constexpr std::size_t SQUID_STAR_COUNT = SQUID_STAR_SMALL_COUNT + SQUID_STAR_LARGE_COUNT;

// Three brightnesses of grey-blue, dim to bright; star i takes shade i modulo three.
constexpr std::array<rv_color, 3> SQUID_STAR_SHADES = { {
    { 56, 64, 88 },
    { 110, 122, 150 },
    { 176, 186, 210 },
} };

// x, y: upper-left corner in screen pixels; size: edge in pixels (1 or 2); colour: its shade.
struct squid_star {
    int16_t x;
    int16_t y;
    uint16_t size;
    rv_color colour;
};

// xorshift32 step (shifts 13, 17, 5: Marsaglia's fixed triple). One generator for the whole field, so the table
// is the same in every build.
constexpr uint32_t squid_star_next(uint32_t state)
{
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

// Builds the table at compile time. Each star takes two generator steps, one for x and one for y, and its position is
// kept where the whole star fits on screen.
constexpr std::array<squid_star, SQUID_STAR_COUNT> squid_star_table()
{
    std::array<squid_star, SQUID_STAR_COUNT> table{};
    uint32_t state = SQUID_STAR_SEED;
    for (std::size_t i = 0; i < table.size(); ++i) {
        const int size = i < SQUID_STAR_SMALL_COUNT ? SQUID_STAR_SMALL_PX : SQUID_STAR_LARGE_PX;
        state = squid_star_next(state);
        const uint32_t span_x = static_cast<uint32_t>(SQUID_STAR_SCREEN_WIDTH - size + 1);
        table[i].x = static_cast<int16_t>(state % span_x);
        state = squid_star_next(state);
        const uint32_t span_y = static_cast<uint32_t>(SQUID_STAR_SCREEN_HEIGHT - size + 1);
        table[i].y = static_cast<int16_t>(state % span_y);
        table[i].size = static_cast<uint16_t>(size);
        table[i].colour = SQUID_STAR_SHADES[i % SQUID_STAR_SHADES.size()];
    }
    return table;
}

constexpr auto SQUID_STARS = squid_star_table();

// The order the stars light in: star i is the (i * step mod count)-th to light. The step shares no factor with the count,
// so every star gets its own turn, and the turns jump across the table.
constexpr std::size_t SQUID_STAR_LIGHT_STEP = 97;
static_assert(std::gcd(SQUID_STAR_LIGHT_STEP, SQUID_STAR_COUNT) == 1, "the light order must visit every star once");

// Puts the lit share (0..1) of the stars as flat sprites at SQUID_STAR_DEPTH. A refused put is kept as the first code
// and the rest are still put; the caller logs the code.
inline int squid_draw_stars(rv_cv *cv, float lit)
{
    const auto lit_count = static_cast<std::size_t>(std::clamp(lit, 0.0f, 1.0f) * static_cast<float>(SQUID_STAR_COUNT));
    int64_t first_code = RV_OK;
    for (std::size_t i = 0; i < SQUID_STAR_COUNT; ++i) {
        if (i * SQUID_STAR_LIGHT_STEP % SQUID_STAR_COUNT >= lit_count) {
            continue;
        }
        const squid_star &star = SQUID_STARS[i];
        rv_primitive primitive{};
        primitive.type = RV_PRIMITIVE_SPRITE;
        primitive.depth = SQUID_STAR_DEPTH;
        primitive.data.sprite.fill_mode = RV_PRIMITIVE_FILL_MODE_FLAT_COLOURED;
        primitive.data.sprite.mapping = RV_TEXWRAP_CLAMP;
        primitive.data.sprite.color = star.colour;
        primitive.data.sprite.x = star.x;
        primitive.data.sprite.y = star.y;
        primitive.data.sprite.width = star.size;
        primitive.data.sprite.height = star.size;
        const int64_t code = rv_cv_frame_put(cv, &primitive);
        if (code != RV_OK && first_code == RV_OK) {
            first_code = code;
        }
    }
    return static_cast<int>(first_code);
}

} // namespace squid
