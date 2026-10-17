#pragma once

// Game state of the squid round (docs/disdoc.md, 7.2, 7.3, 9.1-9.3): phase, timer, orbit, hits and free mode.
// Rules and transitions only: no drawing and no button reads; the caller passes the pad state and the frame time.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "squid-collide.hpp"
#include "squid-input.hpp"
#include "squid-orbit.hpp"
#include "squid-params.hpp"
#include "squid-sequence.hpp"
#include "pdk/cio/rv_isource.h"
#include "pdk/rv_err.h"
#include "pdklib/rv_logs/rv_logs.hpp"

namespace squid
{

// Log tag of state messages.
inline constexpr const char *squid_state_tag = "state";

// Sum of the radii: Alkoldun touches a target whose centre is this close.
constexpr float SQUID_TOUCH_DISTANCE = SQUID_ALKOLDUN_RADIUS + SQUID_DEBRIS_RADIUS;

enum class squid_phase {
    prepare,
    ready,
    play,
    pause,
    result
};
enum class squid_command {
    calibrate,
    start,
    pause,
    restart,
    free_mode
};
// Events move the phase without a command: the pad is lost in play, or the eighth hit ends the round.
enum class squid_event {
    pad_lost,
    round_over
};

// Enumerator counts, from the enums; the name and event tables are sized by them.
constexpr std::size_t SQUID_PHASE_COUNT = static_cast<std::size_t>(squid_phase::result) + 1;
constexpr std::size_t SQUID_COMMAND_COUNT = static_cast<std::size_t>(squid_command::free_mode) + 1;
constexpr std::size_t SQUID_EVENT_COUNT = static_cast<std::size_t>(squid_event::round_over) + 1;

// Names for the refusal log, indexed by the enum value.
inline constexpr std::array<const char *, SQUID_PHASE_COUNT> SQUID_PHASE_NAMES{ "prepare", "ready", "play", "pause", "result" };
inline constexpr std::array<const char *, SQUID_COMMAND_COUNT> SQUID_COMMAND_NAMES{ "calibrate",
    "start",
    "pause",
    "restart",
    "free_mode" };
inline constexpr std::array<const char *, SQUID_EVENT_COUNT> SQUID_EVENT_NAMES{ "pad_lost", "round_over" };

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
    { squid_phase::prepare, squid_command::free_mode, squid_phase::prepare },
    { squid_phase::ready, squid_command::free_mode, squid_phase::ready },
    { squid_phase::ready, squid_command::start, squid_phase::play },
    { squid_phase::play, squid_command::pause, squid_phase::pause },
    { squid_phase::pause, squid_command::pause, squid_phase::play },
    { squid_phase::ready, squid_command::restart, squid_phase::ready },
    { squid_phase::pause, squid_command::restart, squid_phase::ready },
    { squid_phase::result, squid_command::restart, squid_phase::ready },
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
    { squid_phase::play, squid_event::round_over, squid_phase::result },
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
    squid_orbit orbit{};
    squid_sequence sequence{};
    squid_targets targets{};
    double time_s = 0.0;
    int touches = 0;
    // Free mode: no active target and no end; debris touches count instead of hits.
    bool free_mode = false;
    // Contact per target; the watched targets are swept each frame.
    std::array<squid_contact, SQUID_TARGET_COUNT> contacts{};
};

// Collision results of one frame. Computed before the state is written, so a refused frame changes nothing.
struct squid_frame_contacts {
    // Watched at the start of the frame: the active target, or every target in free mode.
    std::array<bool, SQUID_TARGET_COUNT> watched{};
    // The step path touches the watched target.
    std::array<bool, SQUID_TARGET_COUNT> swept{};
    // The ball at the end pose overlaps the target: the contact a target starts with when it becomes active.
    std::array<bool, SQUID_TARGET_COUNT> overlap{};
};

// Builds the targets of a fresh game and checks their gaps; the game starts in prepare.
inline int squid_game_init(squid_game &g)
{
    const int built = squid_sequence_build(g.targets);
    if (built != RV_OK) {
        RV_LOG_ERR(squid_state_tag, "init refused: the targets cannot be built");
        return built;
    }
    const int checked = squid_sequence_check(g.targets);
    if (checked != RV_OK) {
        return checked;
    }
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

// Back to the round start: the neutral is kept, the shown tilt and keys level, the free mode stays.
inline void squid_game_restart(squid_game &g)
{
    g.orbit = squid_orbit{};
    g.time_s = 0.0;
    g.touches = 0;
    squid_input_level(g.input);
    g.input.smoothed = squid_tilt{};
    squid_sequence_restart(g.sequence);
    if (g.free_mode) {
        squid_sequence_free(g.sequence);
    }
    g.contacts = {};
}

inline void squid_game_set_free(squid_game &g)
{
    g.free_mode = true;
    squid_sequence_free(g.sequence);
}

// Runs the effect of an accepted command. A refused effect (bad calibration) is returned and the phase stays.
inline int squid_game_apply(squid_game &g, squid_command command, const rv_istate &state, uint64_t abilities)
{
    switch (command) {
    case squid_command::calibrate:
        return squid_game_calibrate(g, state, abilities);
    case squid_command::restart:
        squid_game_restart(g);
        break;
    case squid_command::free_mode:
        squid_game_set_free(g);
        break;
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

// Pose of the orbit at a frame: phase and the shown tilt.
inline squid_sweep squid_game_pose(const squid_game &g)
{
    return squid_sweep{ g.orbit.phase_rad, g.input.smoothed.x_deg, g.input.smoothed.z_deg };
}

// Sweeps the step path and tests the end pose for every target, writing nothing. Watched is taken from the state
// as it is now, which is the start of the frame.
inline int squid_game_evaluate(const squid_game &g, const squid_sweep &from, const squid_sweep &to, squid_frame_contacts &out)
{
    for (int i = 0; i < SQUID_TARGET_COUNT; ++i) {
        const rv_pdklib::rv_vec3 center = g.targets[i].position;
        const int point = squid_collide_sweep(to, to, center, SQUID_TOUCH_DISTANCE, out.overlap[i]);
        if (point != RV_OK) {
            return point;
        }
        out.watched[i] = g.free_mode || i == g.sequence.active;
        const int swept = out.watched[i] ? squid_collide_sweep(from, to, center, SQUID_TOUCH_DISTANCE, out.swept[i]) : RV_OK;
        if (swept != RV_OK) {
            return swept;
        }
    }
    return RV_OK;
}

// A contact that starts: a touch of debris in free mode; a hit on the active target, which hands over to the next
// one. The next target starts with the contact its ball has at this pose, so only a new contact can count.
inline int squid_game_on_contact(squid_game &g, const squid_frame_contacts &frame)
{
    if (g.free_mode) {
        g.touches += 1;
        return RV_OK;
    }
    const int hit = squid_sequence_hit(g.sequence);
    if (hit != RV_OK) {
        return hit;
    }
    const int next = g.sequence.active;
    if (next != SQUID_SEQUENCE_NONE) {
        g.contacts[static_cast<std::size_t>(next)].touching = frame.overlap[static_cast<std::size_t>(next)];
    }
    return RV_OK;
}

// Per disdoc 9.2 a pass through the next target in the frame of a hit does not count. At the console's fixed 1/60 s
// step the ball moves at most ~2.8 units per frame, less than the 4.8 it needs to reach the next target (gaps of 6.1);
// a longer frame could pass it uncounted.
inline int squid_game_commit(squid_game &g, const squid_frame_contacts &frame)
{
    for (int i = 0; i < SQUID_TARGET_COUNT; ++i) {
        if (!frame.watched[i]) {
            continue;
        }
        const bool entered = squid_contact_enter(g.contacts[i], frame.swept[i]);
        if (!entered) {
            continue;
        }
        const int counted = squid_game_on_contact(g, frame);
        if (counted != RV_OK) {
            return counted;
        }
        if (!g.free_mode) {
            break;
        }
    }
    return RV_OK;
}

// Orbit, timer and collisions of a play step. Collisions are evaluated from the next input and orbit before any
// state is written; the input, orbit and timer are written after the contacts.
inline int squid_game_play(squid_game &g, const rv_istate &state, uint64_t abilities, float dt)
{
    squid_input next_input = g.input;
    const int input_rc = squid_input_step(next_input, state, abilities, dt);
    if (input_rc != RV_OK) {
        return input_rc;
    }
    squid_orbit next_orbit = g.orbit;
    const int stepped = squid_orbit_step(next_orbit, dt);
    if (stepped != RV_OK) {
        return stepped;
    }
    const squid_sweep from = squid_game_pose(g);
    const squid_sweep to{ next_orbit.phase_rad, next_input.smoothed.x_deg, next_input.smoothed.z_deg };
    squid_frame_contacts frame{};
    const int evaluated = squid_game_evaluate(g, from, to, frame);
    if (evaluated != RV_OK) {
        return evaluated;
    }
    const int committed = squid_game_commit(g, frame);
    if (committed != RV_OK) {
        return committed;
    }
    g.input = next_input;
    g.orbit = next_orbit;
    g.time_s += dt;
    if (!squid_sequence_done(g.sequence)) {
        return RV_OK;
    }
    RV_LOG_INFO(squid_state_tag, "round over after {} s", g.time_s);
    return squid_game_fire(g, squid_event::round_over);
}

// One frame of dt seconds. Ready takes input only; play also moves the orbit and timer. Losing the pad in play
// pauses the round. Prepare, pause and result do not move.
inline int squid_game_step(squid_game &g, const rv_istate &state, uint64_t abilities, float dt)
{
    const bool dt_bad = !std::isfinite(dt) || dt < 0.0f;
    if (dt_bad) {
        RV_LOG_ERR(squid_state_tag, "step refused: dt {} is negative or not finite", dt);
        return RV_ERR_INVAL;
    }
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
