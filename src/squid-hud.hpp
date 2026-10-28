#pragma once

// HUD text over the frame: the hint of the phase, the beer counter and the debug panel.
// Drawing only. The caller owns the font style (squid_draw_font_load in squid-draw.hpp) and the round data.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <vector>

#include "squid-input.hpp"
#include "squid-state.hpp"
#include "squid-texts.hpp"
#include "pdk/cv/rv_cv.h"
#include "pdk/cv/rv_primitives.h"
#include "pdk/rv_err.h"
#include "pdklib/rv_font/rv_font.hpp"

namespace squid
{

// The text and its backdrop sit in the overlay, the top key: nearer than the scene and the decals (rv_primitives.h).
constexpr int32_t SQUID_HUD_DEPTH = RV_DEPTH_MAX;

// Layout from the centre of the bottom edge, in pixels: each line is centred, the last one sits on the bottom margin.
// A line is one font line plus the gap below it.
constexpr int SQUID_HUD_LINE_GAP_PX = 2;
// Text stops this far short of each screen edge; a line wider than that wraps at spaces. The backdrop pads the text by
// this much on each side.
constexpr int SQUID_HUD_MARGIN_PX = 4;
// Blank lines between the hint and the debug panel.
constexpr int SQUID_HUD_PANEL_GAP_LINES = 1;
// Backdrop colour: near-black and opaque. The console has no blending yet (pdk/README.md), so a see-through panel is out.
constexpr rv_color SQUID_HUD_BACKDROP_COLOUR = { 8, 9, 14 };

// What the hint, the beer counter and the debug panel read from the round. Built positionally by squid-maid.cpp: keep
// the field order.
struct squid_hud_round {
    squid_phase phase = squid_phase::prepare;
    int beer_drunk = 0;
};

// What the debug panel shows. Built positionally by squid-maid.cpp: keep the field order.
struct squid_hud_debug {
    squid_tilt target{};
    squid_tilt actual{};
    bool sensor = false;
    squid_census census{};
    int round_splits = 0;
    int round_absorbs = 0;
};

// Tables indexed by squid_phase: one row per phase, in enum order.
constexpr auto SQUID_HUD_PHASE_LABELS = std::to_array<std::string_view>({
    SQUID_TEXT_PHASE_PREPARE,
    SQUID_TEXT_PHASE_READY,
    SQUID_TEXT_PHASE_PLAY,
    SQUID_TEXT_PHASE_PAUSE,
    SQUID_TEXT_PHASE_DESCENT,
    SQUID_TEXT_PHASE_PAUSE,
    SQUID_TEXT_PHASE_LANDED,
});
static_assert(SQUID_HUD_PHASE_LABELS.size() == SQUID_PHASE_COUNT, "SQUID_HUD_PHASE_LABELS needs one label per phase");

// The lead line of the hint: what the player does now. None in the pauses.
constexpr auto SQUID_HUD_PHASE_LEADS = std::to_array<std::string_view>({
    SQUID_TEXT_LEAD_PREPARE,
    SQUID_TEXT_LEAD_PREPARE,
    SQUID_TEXT_LEAD_PLAY,
    std::string_view{},
    SQUID_TEXT_LEAD_DESCENT,
    std::string_view{},
    SQUID_TEXT_LEAD_LANDED,
});
static_assert(SQUID_HUD_PHASE_LEADS.size() == SQUID_PHASE_COUNT, "SQUID_HUD_PHASE_LEADS needs one lead per phase");

constexpr auto SQUID_HUD_PHASE_HINTS = std::to_array<std::string_view>({
    SQUID_TEXT_HINT_PREPARE,
    SQUID_TEXT_HINT_READY,
    SQUID_TEXT_HINT_PLAY,
    SQUID_TEXT_HINT_PAUSE,
    SQUID_TEXT_HINT_DESCENT,
    SQUID_TEXT_HINT_PAUSE,
    SQUID_TEXT_HINT_LANDED,
});
static_assert(SQUID_HUD_PHASE_HINTS.size() == SQUID_PHASE_COUNT, "SQUID_HUD_PHASE_HINTS needs one hint per phase");

// The beer counter shows from play on, not while preparing or ready.
constexpr auto SQUID_HUD_PHASE_SHOWS_BEER = std::to_array<bool>({
    false,
    false,
    true,
    true,
    true,
    true,
    true,
});
static_assert(SQUID_HUD_PHASE_SHOWS_BEER.size() == SQUID_PHASE_COUNT, "SQUID_HUD_PHASE_SHOWS_BEER needs one flag per phase");

// Vertical step of one line: the font line plus the gap.
inline int squid_hud_line_step(rv_pdklib::rv_font_style style)
{
    return rv_pdklib::rv_font_line_height(style.scale) + SQUID_HUD_LINE_GAP_PX;
}

// The first refusal of two, or RV_OK: every part is drawn and the first code is kept for the caller.
inline int squid_hud_first(int first, int next)
{
    return first != RV_OK ? first : next;
}

// Puts one line at (x, y) in the overlay. A refused put returns its code; the caller logs it.
inline int squid_hud_put(rv_cv *cv, rv_pdklib::rv_font_style style, int x, int y, std::string_view text)
{
    style.depth = SQUID_HUD_DEPTH;
    int64_t put_code = RV_OK;
    const auto sink = [cv, &put_code](const rv_primitive &primitive) {
        const int64_t code = rv_cv_frame_put(cv, &primitive);
        if (code != RV_OK && put_code == RV_OK) {
            put_code = code;
        }
    };
    rv_pdklib::rv_font_draw(style, x, y, text, sink);
    if (put_code != RV_OK) {
        return static_cast<int>(put_code);
    }
    return RV_OK;
}

// The lines of the HUD, in draw order. Each entry is one line; an empty entry is a blank line that still takes a step.
using squid_hud_lines = std::vector<std::string>;

// Appends one piece of text wrapped at spaces, each line no wider than max_px. A word wider than max_px gets a line of
// its own and overflows. Empty text still appends one empty line.
inline void squid_hud_wrap_piece(rv_pdklib::rv_font_style style, int max_px, std::string_view text, squid_hud_lines &lines)
{
    std::size_t line_start = 0;
    std::size_t line_end = 0;
    std::size_t word_start = 0;
    while (word_start < text.size()) {
        const std::size_t space = text.find(' ', word_start);
        const std::size_t word_end = space == std::string_view::npos ? text.size() : space;
        const std::string_view candidate = text.substr(line_start, word_end - line_start);
        const bool fits = word_start == line_start || rv_pdklib::rv_font_measure_width(candidate, style.scale) <= max_px;
        if (!fits) {
            lines.emplace_back(text.substr(line_start, line_end - line_start));
            line_start = word_start;
        }
        line_end = word_end;
        word_start = word_end + 1;
    }
    lines.emplace_back(text.substr(line_start, line_end - line_start));
}

// Appends text as lines: a '\n' is a hard break, so each rule of a lead starts its own line. Each piece wraps on its own.
inline void squid_hud_wrap(rv_pdklib::rv_font_style style, int max_px, std::string_view text, squid_hud_lines &lines)
{
    std::size_t piece_start = 0;
    while (true) {
        const std::size_t newline = text.find('\n', piece_start);
        const std::size_t piece_end = newline == std::string_view::npos ? text.size() : newline;
        squid_hud_wrap_piece(style, max_px, text.substr(piece_start, piece_end - piece_start), lines);
        if (newline == std::string_view::npos) {
            return;
        }
        piece_start = newline + 1;
    }
}

// The hint: a lead line, then the buttons of the phase, then the beer counter in the phases that show it.
inline void
squid_hud_hint_lines(rv_pdklib::rv_font_style style, int max_px, const squid_hud_round &round, squid_hud_lines &lines)
{
    const auto index = static_cast<std::size_t>(round.phase);
    squid_hud_wrap(style, max_px, SQUID_HUD_PHASE_LEADS[index], lines);
    squid_hud_wrap(style, max_px, SQUID_HUD_PHASE_HINTS[index], lines);
    if (SQUID_HUD_PHASE_SHOWS_BEER[index]) {
        const std::string text = std::format(SQUID_TEXT_BEER, round.beer_drunk, SQUID_BEER_GOAL);
        squid_hud_wrap(style, max_px, text, lines);
    }
}

// The debug panel's lines, one string each: title, phase, target and actual tilt, source, ball counts by size, total mass,
// the round's splits and absorbs, and the split-all key. Pure: the panel draws them, the harness reads them.
inline auto squid_hud_debug_lines(const squid_hud_round &round, const squid_hud_debug &debug)
{
    const squid_census &c = debug.census;
    return std::array{
        std::string{ SQUID_TEXT_DEBUG_TITLE },
        std::format(SQUID_TEXT_DEBUG_PHASE, SQUID_HUD_PHASE_LABELS[static_cast<std::size_t>(round.phase)]),
        std::format(SQUID_TEXT_DEBUG_TARGET, SQUID_TEXT_AXIS_X, debug.target.x_deg, SQUID_TEXT_AXIS_Z, debug.target.z_deg),
        std::format(SQUID_TEXT_DEBUG_ACTUAL, SQUID_TEXT_AXIS_X, debug.actual.x_deg, SQUID_TEXT_AXIS_Z, debug.actual.z_deg),
        std::format(SQUID_TEXT_DEBUG_SOURCE, debug.sensor ? SQUID_TEXT_SOURCE_SENSOR : SQUID_TEXT_SOURCE_RESERVE),
        std::format(SQUID_TEXT_DEBUG_BALLS, c.balls[0], c.balls[1], c.balls[2], c.balls[3]),
        std::format(SQUID_TEXT_DEBUG_MASS, c.total_mass),
        std::format(SQUID_TEXT_DEBUG_ROUND, debug.round_splits, debug.round_absorbs),
        std::string{ SQUID_TEXT_DEBUG_SPLIT },
    };
}

// The debug panel's lines, wrapped like the hint.
inline void squid_hud_debug_panel_lines(rv_pdklib::rv_font_style style,
    int max_px,
    const squid_hud_round &round,
    const squid_hud_debug &debug,
    squid_hud_lines &lines)
{
    for (const std::string &line : squid_hud_debug_lines(round, debug)) {
        squid_hud_wrap(style, max_px, line, lines);
    }
}

// Every line of the HUD in draw order: the hint, then the debug panel when show_debug is set. Laid out before anything
// is drawn, so the backdrop can be sized to the text it sits behind.
inline squid_hud_lines squid_hud_layout(rv_pdklib::rv_font_style style,
    int max_px,
    const squid_hud_round &round,
    const squid_hud_debug &debug,
    bool show_debug)
{
    squid_hud_lines lines;
    squid_hud_hint_lines(style, max_px, round, lines);
    if (show_debug) {
        for (int i = 0; i < SQUID_HUD_PANEL_GAP_LINES; ++i) {
            lines.emplace_back();
        }
        squid_hud_debug_panel_lines(style, max_px, round, debug, lines);
    }
    return lines;
}

// One flat sprite centred on the bottom edge: as wide as the widest line plus the margin on each side, from the margin
// above the first line at top down to the bottom edge. Refused puts return their code.
inline int
squid_hud_backdrop_put(rv_cv *cv, rv_pdklib::rv_font_style style, const squid_hud_lines &lines, int screen_w, int screen_h, int top)
{
    int widest = 0;
    for (const std::string &line : lines) {
        widest = std::max(widest, rv_pdklib::rv_font_measure_width(line, style.scale));
    }
    const int width = widest + 2 * SQUID_HUD_MARGIN_PX;
    const int y = std::max(0, top - SQUID_HUD_MARGIN_PX);
    const int height = screen_h - y;
    rv_primitive primitive{};
    primitive.type = RV_PRIMITIVE_SPRITE;
    primitive.depth = SQUID_HUD_DEPTH;
    primitive.data.sprite.fill_mode = RV_PRIMITIVE_FILL_MODE_FLAT_COLOURED;
    primitive.data.sprite.mapping = RV_TEXWRAP_CLAMP;
    primitive.data.sprite.color = SQUID_HUD_BACKDROP_COLOUR;
    primitive.data.sprite.x = static_cast<int16_t>((screen_w - width) / 2);
    primitive.data.sprite.y = static_cast<int16_t>(y);
    primitive.data.sprite.width = static_cast<uint16_t>(width);
    primitive.data.sprite.height = static_cast<uint16_t>(height);
    return static_cast<int>(rv_cv_frame_put(cv, &primitive));
}

// Draws the HUD over the frame at the centre of the bottom edge: a backdrop behind the hint, and the debug panel under it
// when show_debug is set. Every part is drawn even when one is refused; the first refusal is returned. Without a screen
// size there is nothing to lay out to: that refusal is returned before anything is drawn.
inline int squid_hud_draw(rv_cv *cv,
    rv_pdklib::rv_font_style style,
    const squid_hud_round &round,
    const squid_hud_debug &debug,
    bool show_debug)
{
    const int64_t width = rv_cv_screen_width(cv);
    if (width < 0) {
        return static_cast<int>(width);
    }
    const int64_t height = rv_cv_screen_height(cv);
    if (height < 0) {
        return static_cast<int>(height);
    }
    const int max_px = static_cast<int>(width) - 2 * SQUID_HUD_MARGIN_PX;
    const squid_hud_lines lines = squid_hud_layout(style, max_px, round, debug, show_debug);

    // Same depth key: primitives of one bucket draw in submission order, so the backdrop goes first and the text lands on it.
    const int step = squid_hud_line_step(style);
    int y = static_cast<int>(height) - SQUID_HUD_MARGIN_PX - static_cast<int>(lines.size()) * step + SQUID_HUD_LINE_GAP_PX;
    int first_code = squid_hud_backdrop_put(cv, style, lines, static_cast<int>(width), static_cast<int>(height), y);
    for (const std::string &line : lines) {
        const int x = (static_cast<int>(width) - rv_pdklib::rv_font_measure_width(line, style.scale)) / 2;
        first_code = squid_hud_first(first_code, squid_hud_put(cv, style, x, y, line));
        y += step;
    }
    if (first_code != RV_OK) {
        return first_code;
    }
    return RV_OK;
}

} // namespace squid
