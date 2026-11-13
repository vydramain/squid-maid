#pragma once

// HUD text over the frame: the screen of the phase in the chosen language, and the debug panel.
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
// Backdrop colour: near-black and opaque. The console has no blending yet (pdk/README.md), so a see-through panel is out.
constexpr rv_color SQUID_HUD_BACKDROP_COLOUR = { 8, 9, 14 };

// What the screen and the debug panel read from the round. Built positionally by squid-maid.cpp: keep the field order.
struct squid_hud_round {
    squid_phase phase = squid_phase::language;
    int beer_drunk = 0;
    squid_lang lang = squid_lang::ru;
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

// The phase's label on the debug panel. The switch names every phase, so a new phase without a label is a warning.
inline std::string_view squid_hud_label(const squid_texts &texts, squid_phase phase)
{
    switch (phase) {
    case squid_phase::language:
        return texts.phase_language;
    case squid_phase::intro:
        return texts.phase_intro;
    case squid_phase::ready:
        return texts.phase_ready;
    case squid_phase::arrive:
        return texts.phase_arrive;
    case squid_phase::play:
        return texts.phase_play;
    case squid_phase::pause:
    case squid_phase::descent_pause:
        return texts.phase_pause;
    case squid_phase::descent:
        return texts.phase_descent;
    case squid_phase::landed:
        return texts.phase_landed;
    }
    return {};
}

// The phase's screen, a format string of the beer points and the goal; empty in the intro and the start, which show none.
inline std::string_view squid_hud_screen(const squid_texts &texts, squid_phase phase)
{
    switch (phase) {
    case squid_phase::language:
        return SQUID_TEXT_SCREEN_LANGUAGE;
    case squid_phase::intro:
    case squid_phase::arrive:
        return {};
    case squid_phase::ready:
        return texts.screen_ready;
    case squid_phase::play:
        return texts.screen_play;
    case squid_phase::pause:
    case squid_phase::descent_pause:
        return texts.screen_pause;
    case squid_phase::descent:
        return texts.screen_descent;
    case squid_phase::landed:
        return texts.screen_landed;
    }
    return {};
}

// Vertical step of one line: the font line plus the gap.
inline int squid_hud_line_step(rv_pdklib::rv_font_style style)
{
    return rv_pdklib::rv_font_line_height(style.scale) + SQUID_HUD_LINE_GAP_PX;
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

// The screen of the phase with its beer numbers filled in; nothing for a phase without a screen.
inline void
squid_hud_screen_lines(rv_pdklib::rv_font_style style, int max_px, const squid_hud_round &round, squid_hud_lines &lines)
{
    const std::string_view screen = squid_hud_screen(squid_texts_of(round.lang), round.phase);
    if (screen.empty()) {
        return;
    }
    const int beer = round.beer_drunk;
    const int goal = SQUID_BEER_GOAL;
    squid_hud_wrap(style, max_px, std::vformat(screen, std::make_format_args(beer, goal)), lines);
}

// The screen's lines and their widths, kept between frames: rebuilt only when the font scale, the width, the phase, the
// beer counter or the language change. Equal to what squid_hud_screen_lines builds.
struct squid_hud_cache {
    squid_hud_lines lines;
    std::vector<int> widths;

    // Returns true when it rebuilt.
    bool refresh(rv_pdklib::rv_font_style style, int max_px, const squid_hud_round &round)
    {
        if (valid_ && scale_ == style.scale && max_px_ == max_px && round_.phase == round.phase &&
            round_.beer_drunk == round.beer_drunk && round_.lang == round.lang) {
            return false;
        }
        lines.clear();
        squid_hud_screen_lines(style, max_px, round, lines);
        widths.clear();
        for (const std::string &line : lines) {
            widths.push_back(rv_pdklib::rv_font_measure_width(line, style.scale));
        }
        valid_ = true;
        scale_ = style.scale;
        max_px_ = max_px;
        round_ = round;
        return true;
    }

private:
    bool valid_ = false;
    int scale_ = 0;
    int max_px_ = 0;
    squid_hud_round round_{};
};

// The debug panel's lines, one string each: title, phase, target and actual tilt, source, ball counts by size, total mass,
// the round's splits and absorbs, and the split-all key. Pure: the panel draws them, the harness reads them.
inline auto squid_hud_debug_lines(const squid_hud_round &round, const squid_hud_debug &debug)
{
    const squid_texts &t = squid_texts_of(round.lang);
    const squid_census &c = debug.census;
    const std::string_view label = squid_hud_label(t, round.phase);
    const std::string_view source = debug.sensor ? t.source_sensor : t.source_reserve;
    const squid_tilt target = debug.target;
    const squid_tilt actual = debug.actual;
    const int splits = debug.round_splits;
    const int absorbs = debug.round_absorbs;
    return std::array{
        std::string{ t.debug_title },
        std::vformat(t.debug_phase, std::make_format_args(label)),
        std::vformat(t.debug_target, std::make_format_args(SQUID_TEXT_AXIS_X, target.x_deg, SQUID_TEXT_AXIS_Z, target.z_deg)),
        std::vformat(t.debug_actual, std::make_format_args(SQUID_TEXT_AXIS_X, actual.x_deg, SQUID_TEXT_AXIS_Z, actual.z_deg)),
        std::vformat(t.debug_source, std::make_format_args(source)),
        std::vformat(t.debug_balls, std::make_format_args(c.balls[0], c.balls[1], c.balls[2], c.balls[3])),
        std::vformat(t.debug_mass, std::make_format_args(c.total_mass)),
        std::vformat(t.debug_round, std::make_format_args(splits, absorbs)),
        std::string{ t.debug_split },
    };
}

// The debug panel's lines, wrapped like the screen.
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

// One flat sprite centred on the bottom edge: as wide as the widest line plus the margin on each side, from the margin
// above the first line at top down to the bottom edge. Refused puts return their code.
inline int squid_hud_backdrop_put(rv_cv *cv, int widest, int screen_w, int screen_h, int top)
{
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

// Draws the HUD over the frame at the centre of the bottom edge from the cache of the screen: a backdrop behind the
// screen, and the debug panel under it when show_debug is set. No lines, no backdrop. Every part is drawn even when one is
// refused; the first refusal is returned. Without a screen size there is nothing to lay out to: that refusal is returned
// before anything is drawn.
inline int squid_hud_draw(rv_cv *cv,
    rv_pdklib::rv_font_style style,
    squid_hud_cache &cache,
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
    cache.refresh(style, max_px, round);
    // The debug panel is laid out each frame while it is open; the screen comes from the cache.
    squid_hud_lines panel;
    std::vector<int> panel_widths;
    if (show_debug) {
        panel = cache.lines;
        if (!panel.empty()) {
            panel.emplace_back(); // a blank line between the screen and the panel
        }
        squid_hud_debug_panel_lines(style, max_px, round, debug, panel);
        for (const std::string &line : panel) {
            panel_widths.push_back(rv_pdklib::rv_font_measure_width(line, style.scale));
        }
    }
    const squid_hud_lines &lines = show_debug ? panel : cache.lines;
    const std::vector<int> &widths = show_debug ? panel_widths : cache.widths;
    if (lines.empty()) {
        return RV_OK;
    }

    // Same depth key: primitives of one bucket draw in submission order, so the backdrop goes first and the text lands on it.
    const int step = squid_hud_line_step(style);
    int y = static_cast<int>(height) - SQUID_HUD_MARGIN_PX - static_cast<int>(lines.size()) * step + SQUID_HUD_LINE_GAP_PX;
    const int widest = *std::max_element(widths.begin(), widths.end());
    int first_code = squid_hud_backdrop_put(cv, widest, static_cast<int>(width), static_cast<int>(height), y);
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const int x = (static_cast<int>(width) - widths[i]) / 2;
        const int put_code = squid_hud_put(cv, style, x, y, lines[i]);
        first_code = first_code != RV_OK ? first_code : put_code; // every line is drawn, the first refusal is kept
        y += step;
    }
    return first_code;
}

} // namespace squid
