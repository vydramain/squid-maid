#pragma once

// Drawing constants and the font's video assets. Drawing only; the state comes from the caller.

#include <cstddef>
#include <cstdint>
#include <vector>

#include "pdk/cv/rv_cv.h"
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

} // namespace squid
