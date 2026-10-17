#pragma once

// Game state of the squid toy: phase, the body set the physics steps in play, and the event counts of the last
// play frame. Rules and transitions only: no drawing and no button reads; the caller passes the pad state and the frame time.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <random>

#include "squid-input.hpp"
#include "squid-params.hpp"
#include "squid-physics.hpp"
#include "pdk/cio/rv_isource.h"
#include "pdk/rv_err.h"
#include "pdklib/rv_logs/rv_logs.hpp"

namespace squid
{

// Log tag of state messages.
inline constexpr const char *squid_state_tag = "state";

// Seed of the start layout: every start and restart draws the same bodies. The generator and the unit mapping are fixed,
// and the overlap and inside tests are float, so the layout is the same on this build; not claimed across platforms.
constexpr std::uint32_t SQUID_LAYOUT_SEED = 1018;

// Draws per body before the layout is refused. A spot that is not found in this many tries is an error, not a retry.
constexpr int SQUID_LAYOUT_ATTEMPTS = 10000;

enum class squid_phase {
    prepare,
    ready,
    play,
    pause
};
enum class squid_command {
    calibrate,
    start,
    pause,
    restart
};
// Events move the phase without a command.
enum class squid_event {
    pad_lost
};

// Enumerator counts, from the enums; the name and event tables are sized by them.
constexpr std::size_t SQUID_PHASE_COUNT = static_cast<std::size_t>(squid_phase::pause) + 1;
constexpr std::size_t SQUID_COMMAND_COUNT = static_cast<std::size_t>(squid_command::restart) + 1;
constexpr std::size_t SQUID_EVENT_COUNT = static_cast<std::size_t>(squid_event::pad_lost) + 1;

// Names for the refusal log, indexed by the enum value.
inline constexpr std::array<const char *, SQUID_PHASE_COUNT> SQUID_PHASE_NAMES{ "prepare", "ready", "play", "pause" };
inline constexpr std::array<const char *, SQUID_COMMAND_COUNT> SQUID_COMMAND_NAMES{ "calibrate", "start", "pause", "restart" };
inline constexpr std::array<const char *, SQUID_EVENT_COUNT> SQUID_EVENT_NAMES{ "pad_lost" };

template <std::size_t N>
constexpr bool squid_names_set(const std::array<const char *, N> &names)
{
    for (const char *name : names) {
        if (name == nullptr) {
            return false;
        }
    }
    return true;
}
static_assert(squid_names_set(SQUID_PHASE_NAMES) && squid_names_set(SQUID_COMMAND_NAMES) && squid_names_set(SQUID_EVENT_NAMES),
    "every phase, command and event needs a name");

// Legal (phase, command) pairs and the phase each leads to. Any other pair is refused.
struct squid_edge {
    squid_phase from;
    squid_command command;
    squid_phase to;
};

inline constexpr auto SQUID_EDGES = std::to_array<squid_edge>({
    { squid_phase::prepare, squid_command::calibrate, squid_phase::ready },
    { squid_phase::ready, squid_command::calibrate, squid_phase::ready },
    { squid_phase::pause, squid_command::calibrate, squid_phase::pause },
    { squid_phase::ready, squid_command::start, squid_phase::play },
    { squid_phase::play, squid_command::pause, squid_phase::pause },
    { squid_phase::pause, squid_command::pause, squid_phase::play },
    { squid_phase::ready, squid_command::restart, squid_phase::ready },
    { squid_phase::pause, squid_command::restart, squid_phase::ready },
});

// Phase changes that come from the round itself, not from a command.
struct squid_event_edge {
    squid_phase from;
    squid_event event;
    squid_phase to;
};

// One row per event, in enum order (checked below).
inline constexpr std::array<squid_event_edge, SQUID_EVENT_COUNT> SQUID_EVENT_EDGES{ {
    { squid_phase::play, squid_event::pad_lost, squid_phase::pause },
} };

constexpr bool squid_event_table_in_order()
{
    for (std::size_t i = 0; i < SQUID_EVENT_COUNT; ++i) {
        if (static_cast<std::size_t>(SQUID_EVENT_EDGES[i].event) != i) {
            return false;
        }
    }
    return true;
}
static_assert(squid_event_table_in_order(), "SQUID_EVENT_EDGES needs one row per squid_event, in enum order");

struct squid_game {
    squid_phase phase = squid_phase::prepare;
    squid_input input{};
    // The body set stepped in play, and the splits and absorbs of the current frame; zero on frames that do not step
    // the physics (prepare, ready, pause, pad loss, a frame the physics refused); a step refused for its dt changes nothing.
    squid_bodies bodies{};
    squid_event_counts events{};
    // Draws the body drift in play; reseeded with the layout seed on every build, so a round and its restart repeat.
    std::minstd_rand drift_rng{ SQUID_LAYOUT_SEED };
};

// The start layout has one Alkoldun and balls of the largest mass; the body capacity must hold them.
static_assert(SQUID_START_BALLS + 1 <= SQUID_BODY_CAPACITY, "the start layout must fit the body set");

// Variant of a fresh body, drawn from the layout generator.
inline int squid_layout_variant(std::minstd_rand &rng)
{
    return static_cast<int>(rng() % static_cast<std::uint_fast32_t>(SQUID_MODEL_VARIANTS));
}

// Uniform in [-1, 1] from the generator: the generator and this mapping are fixed, so the draws repeat on this build.
inline float squid_layout_unit(std::minstd_rand &rng)
{
    const float span = static_cast<float>(rng.max() - rng.min());
    return 2.0f * static_cast<float>(rng() - rng.min()) / span - 1.0f;
}

// True when a body of this radius at pos is inside the disc and clear of every body in the set.
inline bool squid_layout_clear(const squid_bodies &set, squid_flat pos, float radius)
{
    const float centre = std::sqrt(pos.x * pos.x + pos.z * pos.z);
    if (centre + radius > SQUID_DISC_RADIUS) {
        return false;
    }
    for (int i = 0; i < set.count; ++i) {
        const squid_body &other = set.items[i];
        const float dx = other.pos.x - pos.x;
        const float dz = other.pos.z - pos.z;
        if (std::sqrt(dx * dx + dz * dz) < radius + squid_radius_checked(other)) {
            return false;
        }
    }
    return true;
}

// Appends the body at the first clear draw of the generator; refused with the set unchanged if no draw is clear.
inline int squid_layout_place(squid_bodies &set, std::minstd_rand &rng, squid_body body)
{
    const float radius = squid_radius_checked(body);
    for (int attempt = 0; attempt < SQUID_LAYOUT_ATTEMPTS; ++attempt) {
        const float x = SQUID_DISC_RADIUS * squid_layout_unit(rng);
        const float z = SQUID_DISC_RADIUS * squid_layout_unit(rng);
        body.pos = squid_flat{ x, z };
        if (squid_layout_clear(set, body.pos, radius)) {
            set.items[set.count++] = body;
            return RV_OK;
        }
    }
    RV_LOG_ERR(squid_state_tag, "layout refused: no clear spot for body {} in {} attempts", set.count, SQUID_LAYOUT_ATTEMPTS);
    return RV_ERR_NOMEM;
}

// The start layout: Alkoldun at the centre, then SQUID_START_BALLS balls of the largest mass around it. Built into a fresh
// set and copied out only when every body fits, so a refused layout leaves the old set as it was.
inline int squid_layout_build(squid_bodies &out)
{
    std::minstd_rand rng{ SQUID_LAYOUT_SEED };
    squid_bodies set{};
    squid_body alkoldun{};
    if (squid_body_alkoldun(alkoldun, squid_layout_variant(rng), squid_flat{}) != RV_OK) {
        return RV_ERR_INVAL;
    }
    set.items[set.count++] = alkoldun;
    for (int i = 0; i < SQUID_START_BALLS; ++i) {
        squid_body ball{};
        if (squid_body_ball(ball, SQUID_BALL_MASS_MAX, squid_layout_variant(rng), squid_flat{}) != RV_OK) {
            return RV_ERR_INVAL;
        }
        const int placed = squid_layout_place(set, rng, ball);
        if (placed != RV_OK) {
            return placed;
        }
    }
    out = set;
    return RV_OK;
}

// Builds the start layout of a fresh game; the game starts in prepare.
inline int squid_game_init(squid_game &g)
{
    const int layout = squid_layout_build(g.bodies);
    if (layout != RV_OK) {
        return layout;
    }
    g.drift_rng.seed(SQUID_LAYOUT_SEED);
    g.phase = squid_phase::prepare;
    return RV_OK;
}

inline const squid_edge *squid_game_find_edge(squid_phase from, squid_command command)
{
    const auto it = std::find_if(SQUID_EDGES.begin(), SQUID_EDGES.end(), [from, command](const squid_edge &e) {
        return e.from == from && e.command == command;
    });
    return it == SQUID_EDGES.end() ? nullptr : &*it;
}

// Calibration takes the neutral only with a gyro; on the reserve the keys and stick need no neutral.
inline int squid_game_calibrate(squid_game &g, const rv_istate &state, uint64_t abilities)
{
    const bool sensor = (abilities & RV_ISOURCE_GYRO_MOVE) != 0;
    if (!sensor) {
        return RV_OK;
    }
    return squid_input_calibrate(g.input, state);
}

// Back to the round start: a fresh layout and the shown tilt and keys level; the neutral is kept.
// A refused layout changes nothing.
inline int squid_game_restart(squid_game &g)
{
    const int layout = squid_layout_build(g.bodies);
    if (layout != RV_OK) {
        return layout;
    }
    g.drift_rng.seed(SQUID_LAYOUT_SEED);
    g.events = squid_event_counts{};
    squid_input_level(g.input);
    g.input.smoothed = squid_tilt{};
    return RV_OK;
}

// Runs the effect of an accepted command. A refused effect (bad calibration or layout) is returned and the phase stays.
inline int squid_game_apply(squid_game &g, squid_command command, const rv_istate &state, uint64_t abilities)
{
    switch (command) {
    case squid_command::calibrate:
        return squid_game_calibrate(g, state, abilities);
    case squid_command::restart:
        return squid_game_restart(g);
    case squid_command::start:
    case squid_command::pause:
        break;
    }
    return RV_OK;
}

// Applies a command to the phase table; a pair not in the table is refused with the reason and nothing changes.
inline int squid_game_command(squid_game &g, squid_command command, const rv_istate &state, uint64_t abilities)
{
    const squid_edge *edge = squid_game_find_edge(g.phase, command);
    if (edge == nullptr) {
        RV_LOG_WARN(squid_state_tag,
            "{} refused in phase {}: no such transition",
            SQUID_COMMAND_NAMES[static_cast<std::size_t>(command)],
            SQUID_PHASE_NAMES[static_cast<std::size_t>(g.phase)]);
        return RV_ERR_INVAL;
    }
    const int rc = squid_game_apply(g, command, state, abilities);
    if (rc != RV_OK) {
        return rc;
    }
    g.phase = edge->to;
    return RV_OK;
}

// Moves the phase on an event the table lists for the current phase; a missing row is refused.
inline int squid_game_fire(squid_game &g, squid_event event)
{
    const auto it = std::find_if(SQUID_EVENT_EDGES.begin(), SQUID_EVENT_EDGES.end(), [&g, event](const squid_event_edge &e) {
        return e.from == g.phase && e.event == event;
    });
    if (it == SQUID_EVENT_EDGES.end()) {
        RV_LOG_WARN(squid_state_tag,
            "event {} refused in phase {}: no such transition",
            SQUID_EVENT_NAMES[static_cast<std::size_t>(event)],
            SQUID_PHASE_NAMES[static_cast<std::size_t>(g.phase)]);
        return RV_ERR_INVAL;
    }
    g.phase = it->to;
    return RV_OK;
}

// One play frame: the input moves first, then the physics steps with its smoothed tilt. The input and body set
// are written only when the physics accepts the frame.
inline int squid_game_play(squid_game &g, const rv_istate &state, uint64_t abilities, float dt)
{
    squid_input next_input = g.input;
    const int input_rc = squid_input_step(next_input, state, abilities, dt);
    if (input_rc != RV_OK) {
        return input_rc;
    }
    squid_event_counts counts{};
    const int stepped = squid_physics_frame(g.bodies, next_input.smoothed, dt, counts, g.drift_rng);
    if (stepped != RV_OK) {
        return stepped;
    }
    g.input = next_input;
    g.events = counts;
    return RV_OK;
}

// One frame of dt seconds. Ready takes input only; play also steps the physics. Losing the pad in play
// pauses the round. Prepare and pause do not move.
inline int squid_game_step(squid_game &g, const rv_istate &state, uint64_t abilities, float dt)
{
    const bool dt_bad = !std::isfinite(dt) || dt <= 0.0f;
    if (dt_bad) {
        RV_LOG_ERR(squid_state_tag, "step refused: dt {} is not a positive finite number", dt);
        return RV_ERR_INVAL;
    }
    // The counts are of this frame only: a frame that steps no physics leaves them zero.
    g.events = squid_event_counts{};
    const bool playing = g.phase == squid_phase::play;
    const bool listening = playing || g.phase == squid_phase::ready;
    if (!listening) {
        return RV_OK;
    }
    const bool pad_lost = playing && abilities == 0;
    if (pad_lost) {
        RV_LOG_INFO(squid_state_tag, "pad lost: round paused");
        return squid_game_fire(g, squid_event::pad_lost);
    }
    return playing ? squid_game_play(g, state, abilities, dt) : squid_input_step(g.input, state, abilities, dt);
}

} // namespace squid
