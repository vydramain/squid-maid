#pragma once

// HUD text over the frame (docs/disdoc.md, 12): the counter and clock, the hint of the phase and the debug panel.
// Drawing only. The caller owns the font style (squid_draw_font_load in squid-draw.hpp) and the round data.

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>

#include "squid-input.hpp"
#include "squid-state.hpp"
#include "squid-texts.hpp"
#include "pdk/cv/rv_cv.h"
#include "pdk/cv/rv_primitives.h"
#include "pdk/rv_err.h"
#include "pdklib/rv_font/rv_font.hpp"

namespace squid
{

// The text sits in the overlay, the top key: nearer than the scene and the decals (rv_primitives.h).
constexpr int32_t SQUID_HUD_DEPTH = RV_DEPTH_MAX;

// Layout from the top-left corner, in pixels. A line is one font line plus the gap below it.
constexpr int SQUID_HUD_X = 4;
constexpr int SQUID_HUD_Y = 4;
constexpr int SQUID_HUD_LINE_GAP_PX = 2;
// Text stops this far short of each screen edge; a line wider than that wraps at spaces.
constexpr int SQUID_HUD_MARGIN_PX = 4;
// Fields of the counter line are this many cells apart.
constexpr int SQUID_HUD_FIELD_GAP_CELLS = 2;
// Blank lines between the hint and the debug panel.
constexpr int SQUID_HUD_PANEL_GAP_LINES = 1;

// What the counter and the hint read from the round. Built positionally by squid-maid.cpp: keep the field order.
struct squid_hud_round {
    squid_phase phase = squid_phase::prepare;
    int hits = 0;
    int total = 0;
    double time_s = 0.0;
    bool free_mode = false;
};

// What the debug panel shows. Built positionally by squid-maid.cpp: keep the field order.
struct squid_hud_debug {
    squid_tilt target{};
    squid_tilt actual{};
    bool sensor = false;
    double orbit_turn = 0.0; // orbit phase in turns, [0, 1)
    double lap_s = 0.0;
    float alkoldun_radius = 0.0f;
    float debris_radius = 0.0f;
};

// Tables indexed by squid_phase: one row per phase, in enum order.
constexpr auto SQUID_HUD_PHASE_LABELS = std::to_array<std::string_view>({
    SQUID_TEXT_PHASE_PREPARE,
    SQUID_TEXT_PHASE_READY,
    SQUID_TEXT_PHASE_PLAY,
    SQUID_TEXT_PHASE_PAUSE,
    SQUID_TEXT_PHASE_RESULT,
});
static_assert(SQUID_HUD_PHASE_LABELS.size() == SQUID_PHASE_COUNT, "SQUID_HUD_PHASE_LABELS needs one label per phase");

// The lead line of the hint. The result's lead is formatted from the round, so its row here stays empty.
constexpr auto SQUID_HUD_PHASE_LEADS = std::to_array<std::string_view>({
    SQUID_TEXT_INTRO,
    SQUID_TEXT_INTRO,
    std::string_view{},
    std::string_view{},
    std::string_view{},
});
static_assert(SQUID_HUD_PHASE_LEADS.size() == SQUID_PHASE_COUNT, "SQUID_HUD_PHASE_LEADS needs one lead per phase");

constexpr auto SQUID_HUD_PHASE_HINTS = std::to_array<std::string_view>({
    SQUID_TEXT_HINT_PREPARE,
    SQUID_TEXT_HINT_READY,
    SQUID_TEXT_HINT_PLAY,
    SQUID_TEXT_HINT_PAUSE,
    SQUID_TEXT_HINT_RESULT,
});
static_assert(SQUID_HUD_PHASE_HINTS.size() == SQUID_PHASE_COUNT, "SQUID_HUD_PHASE_HINTS needs one hint per phase");

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

// Puts text as lines wrapped at spaces, each no wider than max_px. Line i goes to y plus i steps; y ends one step past
// the last line. A word wider than max_px gets a line of its own and overflows.
inline int squid_hud_put_wrapped(rv_cv *cv, rv_pdklib::rv_font_style style, int max_px, int &y, std::string_view text)
{
    int first_code = RV_OK;
    const auto put_line = [cv, style, &y, &first_code](std::string_view line) {
        first_code = squid_hud_first(first_code, squid_hud_put(cv, style, SQUID_HUD_X, y, line));
        y += squid_hud_line_step(style);
    };
    std::size_t line_start = 0;
    std::size_t line_end = 0;
    std::size_t word_start = 0;
    while (word_start < text.size()) {
        const std::size_t space = text.find(' ', word_start);
        const std::size_t word_end = space == std::string_view::npos ? text.size() : space;
        const std::string_view candidate = text.substr(line_start, word_end - line_start);
        const bool fits = word_start == line_start || rv_pdklib::rv_font_measure_width(candidate, style.scale) <= max_px;
        if (!fits) {
            put_line(text.substr(line_start, line_end - line_start));
            line_start = word_start;
        }
        line_end = word_end;
        word_start = word_end + 1;
    }
    put_line(text.substr(line_start, line_end - line_start));
    if (first_code != RV_OK) {
        return first_code;
    }
    return RV_OK;
}

// The counter line: the goal, or the free mode in free mode, then the clock. Free mode has no timer, so no clock there.
// Each field follows the last after one gap.
inline int squid_hud_counter(rv_cv *cv, rv_pdklib::rv_font_style style, int y, const squid_hud_round &round)
{
    const std::string goal = std::format(SQUID_TEXT_GOAL, round.hits, round.total);
    const std::string time_text = std::format(SQUID_TEXT_TIME, round.time_s);
    const std::string_view headline = round.free_mode ? SQUID_TEXT_FREE_MODE : std::string_view{ goal };
    const std::string_view clock = round.free_mode ? std::string_view{} : std::string_view{ time_text };
    const std::array fields{ headline, clock };
    const int gap = SQUID_HUD_FIELD_GAP_CELLS * rv_pdklib::rv_font_advance(style.scale);
    int first_code = RV_OK;
    int x = SQUID_HUD_X;
    for (const std::string_view field : fields) {
        first_code = squid_hud_first(first_code, squid_hud_put(cv, style, x, y, field));
        x += rv_pdklib::rv_font_measure_width(field, style.scale) + gap;
    }
    if (first_code != RV_OK) {
        return first_code;
    }
    return RV_OK;
}

// The hint: a lead line, then the buttons of the phase. The result's lead is its score and time.
inline int squid_hud_hint(rv_cv *cv, rv_pdklib::rv_font_style style, int max_px, int &y, const squid_hud_round &round)
{
    const auto index = static_cast<std::size_t>(round.phase);
    const std::string result = std::format(SQUID_TEXT_RESULT, round.hits, round.total, round.time_s);
    const std::string_view lead =
        round.phase == squid_phase::result ? std::string_view{ result } : SQUID_HUD_PHASE_LEADS[index];
    const int lead_code = squid_hud_put_wrapped(cv, style, max_px, y, lead);
    const int buttons_code = squid_hud_put_wrapped(cv, style, max_px, y, SQUID_HUD_PHASE_HINTS[index]);
    const int code = squid_hud_first(lead_code, buttons_code);
    if (code != RV_OK) {
        return code;
    }
    return RV_OK;
}

// The debug panel: title, phase, target and actual tilt, source, orbit, lap time and collider radii, one line each.
inline int squid_hud_debug_panel(rv_cv *cv,
    rv_pdklib::rv_font_style style,
    int max_px,
    int &y,
    const squid_hud_round &round,
    const squid_hud_debug &debug)
{
    const std::array lines{
        std::string{ SQUID_TEXT_DEBUG_TITLE },
        std::format(SQUID_TEXT_DEBUG_PHASE, SQUID_HUD_PHASE_LABELS[static_cast<std::size_t>(round.phase)]),
        std::format(SQUID_TEXT_DEBUG_TARGET, SQUID_TEXT_AXIS_X, debug.target.x_deg, SQUID_TEXT_AXIS_Z, debug.target.z_deg),
        std::format(SQUID_TEXT_DEBUG_ACTUAL, SQUID_TEXT_AXIS_X, debug.actual.x_deg, SQUID_TEXT_AXIS_Z, debug.actual.z_deg),
        std::format(SQUID_TEXT_DEBUG_SOURCE, debug.sensor ? SQUID_TEXT_SOURCE_SENSOR : SQUID_TEXT_SOURCE_RESERVE),
        std::format(SQUID_TEXT_DEBUG_ORBIT, debug.orbit_turn),
        std::format(SQUID_TEXT_DEBUG_LAP, debug.lap_s),
        std::format(SQUID_TEXT_DEBUG_RADII, debug.alkoldun_radius, debug.debris_radius),
    };
    int first_code = RV_OK;
    for (const std::string &line : lines) {
        first_code = squid_hud_first(first_code, squid_hud_put_wrapped(cv, style, max_px, y, line));
    }
    if (first_code != RV_OK) {
        return first_code;
    }
    return RV_OK;
}

// Draws the HUD over the frame: the counter and the hint always, the debug panel when show_debug is set. Every part is
// drawn even when one is refused; the first refusal is returned. Without a screen width there is nothing to wrap to:
// that refusal is returned before anything is drawn.
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
    const int max_px = static_cast<int>(width) - 2 * SQUID_HUD_MARGIN_PX;

    int y = SQUID_HUD_Y;
    const int counter_code = squid_hud_counter(cv, style, y, round);
    y += squid_hud_line_step(style);
    const int hint_code = squid_hud_hint(cv, style, max_px, y, round);
    int debug_code = RV_OK;
    if (show_debug) {
        y += SQUID_HUD_PANEL_GAP_LINES * squid_hud_line_step(style);
        debug_code = squid_hud_debug_panel(cv, style, max_px, y, round, debug);
    }
    const int code = squid_hud_first(squid_hud_first(counter_code, hint_code), debug_code);
    if (code != RV_OK) {
        return code;
    }
    return RV_OK;
}

} // namespace squid
