#pragma once

// The music of the round: one stream on the drive's queue, its section chosen by the mood of the round. Each mood keeps a
// bag of its sections, drawn in a random order once per round of the bag; every section loops over its entry.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <random>
#include <span>
#include <type_traits>
#include <utility>

#include "pdk/cd/rv_cd.h"
#include "pdk/rv_err.h"
#include "pdk/rv_pdko.h"
#include "pdkkit/component/rv_component_macro.hpp"
#include "pdkkit/ecs/rv_ecs_frame.hpp"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "squid-components.hpp"
#include "squid-state.hpp"

namespace squid
{

// Log tag of music messages.
inline constexpr const char *SQUID_MUSIC_TAG = "music";

// Seed of the bag's shuffles: a fresh game draws the same order of sections.
constexpr std::uint32_t SQUID_MUSIC_SEED = 1019;

// Sections of the largest mood; a bag holds one slot per section.
constexpr std::size_t SQUID_MUSIC_MAX_SECTIONS = 3;

// Loop 1 over the whole entry (loop_start 0, loop_end 0): a section the queue misses repeats instead of ending.
inline constexpr rv_cd_stream_conf SQUID_MUSIC_CONF = { .loop = 1, .loop_start = 0, .loop_end = 0 };

// Beer points at which the round is in its last bottle: one marked bottle can finish it from here.
constexpr int SQUID_LAST_BOTTLE_POINTS = SQUID_BEER_GOAL - SQUID_BEER_POINTS_MARKED;

enum class squid_mood {
    menu,
    game,
    last_bottle,
    finale,
    count,
};
constexpr std::size_t SQUID_MOOD_COUNT = static_cast<std::size_t>(squid_mood::count);

// The mood of each phase, indexed by squid_phase. The last bottle is a game phase, see squid_mood_of.
inline constexpr squid_mood SQUID_PHASE_MOODS[] = {
    squid_mood::menu,   // language
    squid_mood::menu,   // ready
    squid_mood::game,   // play
    squid_mood::game,   // pause
    squid_mood::finale, // descent
    squid_mood::finale, // descent_pause
    squid_mood::finale, // landed
};
// A phase without a row would fall into the first mood silently; the count must match.
static_assert(std::size(SQUID_PHASE_MOODS) == SQUID_PHASE_COUNT);

// Section names: the [assets.stream] entries of the disc manifest.
inline constexpr const char *SQUID_MENU_SECTIONS[] = { "mus_intro.pcm" };
inline constexpr const char *SQUID_GAME_SECTIONS[] = { "mus_intro_harmonic.pcm", "mus_intro_nasral.pcm", "mus_intro.pcm" };
inline constexpr const char *SQUID_LAST_BOTTLE_SECTIONS[] = { "mus_riff_full.pcm", "mus_riff_sparce.pcm" };
inline constexpr const char *SQUID_FINALE_SECTIONS[] = { "mus_interlude.pcm" };

// One row per mood, in enum order.
inline constexpr std::array<std::span<const char *const>, SQUID_MOOD_COUNT> SQUID_MOOD_SECTIONS = { {
    SQUID_MENU_SECTIONS,
    SQUID_GAME_SECTIONS,
    SQUID_LAST_BOTTLE_SECTIONS,
    SQUID_FINALE_SECTIONS,
} };

static_assert(std::ranges::all_of(SQUID_MOOD_SECTIONS, [](const std::span<const char *const> &row) {
    return !row.empty() && row.size() <= SQUID_MUSIC_MAX_SECTIONS;
}));

// The mood of the round: its phase's row, or the last bottle while the beer count is at the threshold.
inline squid_mood squid_mood_of(const squid_game &game)
{
    const squid_mood mood = SQUID_PHASE_MOODS[static_cast<std::size_t>(game.phase)];
    const bool last_bottle = mood == squid_mood::game && game.beer_drunk >= SQUID_LAST_BOTTLE_POINTS;
    return last_bottle ? squid_mood::last_bottle : mood;
}

// The music state. Standard-layout and trivially copyable: a step works on a copy and keeps it only when the drive accepts.
struct squid_music {
    squid_mood mood = squid_mood::menu;
    // The mood's sections in this round of the bag, as indices into its row, and the next slot to play.
    std::array<std::uint8_t, SQUID_MUSIC_MAX_SECTIONS> bag{};
    std::size_t bag_pos = 0;
    // Shuffles the bag; seeded with SQUID_MUSIC_SEED, so a fresh game draws the same order.
    std::minstd_rand rng{ SQUID_MUSIC_SEED };
    // Set by the first play; a refused play is tried again on the next frame.
    bool started = false;
};
static_assert(std::is_standard_layout_v<squid_music> && std::is_trivially_copyable_v<squid_music>);

} // namespace squid

PDKKIT_COMPONENT(squid::squid_music, "squid_music");

namespace squid
{

inline std::size_t squid_mood_section_count(squid_mood mood)
{
    return SQUID_MOOD_SECTIONS[static_cast<std::size_t>(mood)].size();
}

// Sets the bag to the mood's sections in a random order: Fisher-Yates over the generator's modulo, so the order does not
// depend on the standard library's distribution.
inline void squid_music_reshuffle(squid_music &music, squid_mood mood)
{
    const std::size_t count = squid_mood_section_count(mood);
    music.mood = mood;
    music.bag_pos = 0;
    for (std::size_t i = 0; i < count; ++i) {
        music.bag[i] = static_cast<std::uint8_t>(i);
    }
    for (std::size_t i = count - 1; i > 0; --i) {
        const std::size_t j = static_cast<std::size_t>(music.rng()) % (i + 1);
        std::swap(music.bag[i], music.bag[j]);
    }
}

// The bag's next section name. A spent bag starts a new round in a new order, so each section plays once per round.
inline const char *squid_music_take(squid_music &music)
{
    if (music.bag_pos >= squid_mood_section_count(music.mood)) {
        squid_music_reshuffle(music, music.mood);
    }
    const std::span<const char *const> row = SQUID_MOOD_SECTIONS[static_cast<std::size_t>(music.mood)];
    return row[music.bag[music.bag_pos++]];
}

// The drive's int64 result as rv_err: a negative code is the error, RV_OK is zero.
inline int squid_music_result(int64_t code)
{
    return code < 0 ? static_cast<int>(code) : RV_OK;
}

inline int squid_music_play(rv_cd *cd, const char *section)
{
    return squid_music_result(rv_cd_stream_play(cd, section, &SQUID_MUSIC_CONF));
}

inline int squid_music_queue(rv_cd *cd, const char *section)
{
    return squid_music_result(rv_cd_stream_queue(cd, section, &SQUID_MUSIC_CONF));
}

inline int squid_music_switch(rv_cd *cd, const char *section)
{
    return squid_music_result(rv_cd_stream_switch(cd, section, &SQUID_MUSIC_CONF));
}

// The first play: the menu's bag from its top, whatever the first frame's mood. A refusal changes nothing; next frame retries.
inline int squid_music_start(rv_cd *cd, squid_music &music, squid_mood mood)
{
    squid_music next = music;
    squid_music_reshuffle(next, mood);
    const int code = squid_music_play(cd, squid_music_take(next));
    if (code != RV_OK) {
        return code;
    }
    music = next;
    music.started = true;
    return RV_OK;
}

// A mood change. The menu switches at the playing position; RV_ERR_INVAL there (past the menu section) plays it from the
// top. Other moods queue: the change waits for the playing section's end, up to ~9.6 s, or one more section if already taken.
inline int squid_music_change(rv_cd *cd, squid_music &music, squid_mood mood)
{
    squid_music next = music;
    squid_music_reshuffle(next, mood);
    const char *section = squid_music_take(next);
    const bool menu = mood == squid_mood::menu;
    int code = menu ? squid_music_switch(cd, section) : squid_music_queue(cd, section);
    if (menu && code == RV_ERR_INVAL) {
        code = squid_music_play(cd, section);
    }
    if (code != RV_OK) {
        return code;
    }
    music = next;
    return RV_OK;
}

// Queues the next section of the bag once the drive has taken the last queued one; a pending entry is left to play.
inline int squid_music_advance(rv_cd *cd, squid_music &music)
{
    rv_cd_stream_queue_info info = {};
    const int status = squid_music_result(rv_cd_stream_queue_status(cd, &info));
    if (status != RV_OK) {
        return status;
    }
    if (info.pending != 0) {
        return RV_OK;
    }
    squid_music next = music;
    const int code = squid_music_queue(cd, squid_music_take(next));
    if (code != RV_OK) {
        return code;
    }
    music = next;
    return RV_OK;
}

// One frame: the first play, else a mood change, else the queue's refill.
inline int squid_music_step(rv_cd *cd, squid_music &music, const squid_game &game)
{
    const squid_mood mood = squid_mood_of(game);
    if (!music.started) {
        return squid_music_start(cd, music, squid_mood::menu);
    }
    if (mood != music.mood) {
        return squid_music_change(cd, music, mood);
    }
    return squid_music_advance(cd, music);
}

// Name of the music system; the world keeps the name view, so it is a literal.
constexpr const char *SQUID_SYSTEM_MUSIC = "squid-music";

// A drive refusal is counted for the state line; the frame goes on, and the next frame tries again.
inline int squid_system_music(rv_pdkkit::ecs_world &world, const rv_pdkkit::ecs_frame &frame)
{
    squid_round *round = nullptr;
    const int round_code = squid_single(world, round);
    if (round_code != RV_OK) {
        return round_code;
    }
    squid_music *music = nullptr;
    const int music_code = squid_single(world, music);
    if (music_code != RV_OK) {
        return music_code;
    }
    if (frame.pdk == nullptr) {
        RV_LOG_ERR(SQUID_MUSIC_TAG, "frame has no pdk");
        return RV_ERR_INVAL;
    }
    const int step_code = squid_music_step(rv_pdko_cd(frame.pdk), *music, round->game);
    squid_round_note(*round, step_code);
    return RV_OK;
}

// Registers the music component, then its system; a refused system is logged with its name and code.
inline int squid_music_register(rv_pdkkit::ecs_world &world)
{
    const int component_code = squid_component_register<squid_music>(world);
    if (component_code != RV_OK) {
        return component_code;
    }
    const int system_code = rv_pdkkit::ecs_system_register(world, SQUID_SYSTEM_MUSIC, squid_system_music);
    if (system_code != RV_OK) {
        RV_LOG_ERR(SQUID_MUSIC_TAG, "system {} refused: rv_err {}", SQUID_SYSTEM_MUSIC, system_code);
        return system_code;
    }
    return RV_OK;
}

} // namespace squid
