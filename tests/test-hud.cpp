// Host test of the HUD's screen cache (squid_hud_cache in src/squid-hud.hpp). The wrapped screen lines and their widths
// are rebuilt only when the phase, the beer count, the language, the width or the font scale change, and equal what the
// uncached squid_hud_screen_lines builds. The font measure is pure, so no console is needed.

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "squid-hud.hpp"

namespace
{

int failures = 0;

#define CHECK(cond)                                                                       \
    do {                                                                                  \
        if (!(cond)) {                                                                    \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                                   \
        }                                                                                 \
    } while (0)

rv_pdklib::rv_font_style style_of(int scale)
{
    return rv_pdklib::rv_font_style_make(0, 0, squid::SQUID_HUD_DEPTH, scale);
}

using squid::squid_lang;

squid::squid_hud_round round_of(squid::squid_phase phase, int beers, squid_lang lang = squid_lang::ru)
{
    return squid::squid_hud_round{ phase, beers, lang };
}

// The screen built from scratch, without the cache.
squid::squid_hud_lines
uncached(rv_pdklib::rv_font_style style, int max_px, squid::squid_phase phase, int beers, squid_lang lang = squid_lang::ru)
{
    squid::squid_hud_lines lines;
    squid::squid_hud_screen_lines(style, max_px, round_of(phase, beers, lang), lines);
    return lines;
}

std::vector<int> widths_of(rv_pdklib::rv_font_style style, const squid::squid_hud_lines &lines)
{
    std::vector<int> widths;
    for (const std::string &line : lines) {
        widths.push_back(rv_pdklib::rv_font_measure_width(line, style.scale));
    }
    return widths;
}

constexpr int WIDTHS[] = { 60, 120, 240, 312, 632 };
constexpr int SCALES[] = { 1, 2 };

void cached_lines_equal_uncached_for_every_phase_and_width()
{
    for (int scale : SCALES) {
        const auto style = style_of(scale);
        for (int max_px : WIDTHS) {
            for (std::size_t p = 0; p < squid::SQUID_PHASE_COUNT; ++p) {
                const auto phase = static_cast<squid::squid_phase>(p);
                for (int beers : { 0, 3, 12 }) {
                    for (squid_lang lang : { squid_lang::ru, squid_lang::en }) {
                        squid::squid_hud_cache cache;
                        CHECK(cache.refresh(style, max_px, round_of(phase, beers, lang))); // the first fill always builds
                        const auto want = uncached(style, max_px, phase, beers, lang);
                        CHECK(cache.lines == want);
                        CHECK(cache.widths == widths_of(style, want));
                    }
                }
            }
        }
    }
}

void unchanged_inputs_do_not_rebuild()
{
    const auto style = style_of(1);
    squid::squid_hud_cache cache;
    CHECK(cache.refresh(style, 312, round_of(squid::squid_phase::play, 2)));
    const auto lines = cache.lines;
    const auto widths = cache.widths;
    for (int i = 0; i < 5; ++i) {
        CHECK(!cache.refresh(style, 312, round_of(squid::squid_phase::play, 2)));
    }
    CHECK(cache.lines == lines);
    CHECK(cache.widths == widths);
}

void phase_change_rebuilds()
{
    const auto style = style_of(1);
    squid::squid_hud_cache cache;
    CHECK(cache.refresh(style, 312, round_of(squid::squid_phase::language, 0)));
    for (auto phase : { squid::squid_phase::ready, squid::squid_phase::play, squid::squid_phase::pause }) {
        CHECK(cache.refresh(style, 312, round_of(phase, 0)));
        CHECK(cache.lines == uncached(style, 312, phase, 0));
        CHECK(!cache.refresh(style, 312, round_of(phase, 0)));
    }
    // Back to an earlier phase: rebuilt, and equal to the fresh wrap, not the stale one.
    CHECK(cache.refresh(style, 312, round_of(squid::squid_phase::language, 0)));
    CHECK(cache.lines == uncached(style, 312, squid::squid_phase::language, 0));
}

void beer_change_rebuilds_in_a_phase_that_shows_it()
{
    const auto style = style_of(1);
    squid::squid_hud_cache cache;
    CHECK(cache.refresh(style, 312, round_of(squid::squid_phase::play, 1)));
    const auto before = cache.lines;
    CHECK(cache.refresh(style, 312, round_of(squid::squid_phase::play, 2)));
    CHECK(cache.lines == uncached(style, 312, squid::squid_phase::play, 2));
    CHECK(cache.lines != before); // the counter line carries the number
    CHECK(!cache.refresh(style, 312, round_of(squid::squid_phase::play, 2)));
}

void language_change_rebuilds()
{
    const auto style = style_of(1);
    squid::squid_hud_cache cache;
    CHECK(cache.refresh(style, 312, round_of(squid::squid_phase::play, 1, squid_lang::ru)));
    CHECK(cache.refresh(style, 312, round_of(squid::squid_phase::play, 1, squid_lang::en)));
    CHECK(cache.lines == uncached(style, 312, squid::squid_phase::play, 1, squid_lang::en));
    CHECK(!cache.refresh(style, 312, round_of(squid::squid_phase::play, 1, squid_lang::en)));
}

void width_or_scale_change_rebuilds()
{
    squid::squid_hud_cache cache;
    CHECK(cache.refresh(style_of(1), 312, round_of(squid::squid_phase::ready, 0)));
    CHECK(cache.refresh(style_of(1), 100, round_of(squid::squid_phase::ready, 0)));
    CHECK(cache.lines == uncached(style_of(1), 100, squid::squid_phase::ready, 0));
    CHECK(cache.refresh(style_of(2), 100, round_of(squid::squid_phase::ready, 0)));
    CHECK(cache.lines == uncached(style_of(2), 100, squid::squid_phase::ready, 0));
    CHECK(cache.widths == widths_of(style_of(2), cache.lines));
}

} // namespace

int main()
{
    cached_lines_equal_uncached_for_every_phase_and_width();
    unchanged_inputs_do_not_rebuild();
    phase_change_rebuilds();
    beer_change_rebuilds_in_a_phase_that_shows_it();
    language_change_rebuilds();
    width_or_scale_change_rebuilds();
    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    std::puts("test-hud: ok");
    return EXIT_SUCCESS;
}
