#pragma once

// Game state of the squid toy: phase, the body set the physics steps in play, and the event counts of the last
// play frame. Rules and transitions only: no drawing and no button reads; the caller passes the pad state and the frame time.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <numbers>
#include <random>
#include <ranges>

#include "squid-descent.hpp"
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

// language: the player picks the language; intro: the stars light, then the Earth grows (timed); ready: the tilt
// calibration over the far Earth; arrive: the camera flies in and the bodies grow (timed); then the round.
enum class squid_phase {
    language,
    intro,
    ready,
    arrive,
    play,
    pause,
    descent,
    descent_pause,
    landed
};
enum class squid_command {
    pick,
    calibrate,
    start,
    pause,
    restart,
    split_all
};
// Events move the phase without a command.
enum class squid_event {
    pad_lost,
    goal_reached,
    landed,
    intro_done,
    arrived
};

// Enumerator counts, from the enums; the name and event tables are sized by them.
constexpr std::size_t SQUID_PHASE_COUNT = static_cast<std::size_t>(squid_phase::landed) + 1;
constexpr std::size_t SQUID_COMMAND_COUNT = static_cast<std::size_t>(squid_command::split_all) + 1;
constexpr std::size_t SQUID_EVENT_COUNT = static_cast<std::size_t>(squid_event::arrived) + 1;

// Names for the refusal log, indexed by the enum value.
inline constexpr std::array<const char *, SQUID_PHASE_COUNT>
    SQUID_PHASE_NAMES{ "language", "intro", "ready", "arrive", "play", "pause", "descent", "descent_pause", "landed" };
inline constexpr std::array<const char *, SQUID_COMMAND_COUNT> SQUID_COMMAND_NAMES{ "pick",
    "calibrate",
    "start",
    "pause",
    "restart",
    "split_all" };
inline constexpr std::array<const char *, SQUID_EVENT_COUNT> SQUID_EVENT_NAMES{ "pad_lost",
    "goal_reached",
    "landed",
    "intro_done",
    "arrived" };

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
    { squid_phase::language, squid_command::pick, squid_phase::intro },
    { squid_phase::ready, squid_command::calibrate, squid_phase::ready },
    { squid_phase::pause, squid_command::calibrate, squid_phase::pause },
    { squid_phase::ready, squid_command::start, squid_phase::arrive },
    { squid_phase::play, squid_command::pause, squid_phase::pause },
    { squid_phase::pause, squid_command::pause, squid_phase::play },
    { squid_phase::ready, squid_command::restart, squid_phase::ready },
    { squid_phase::pause, squid_command::restart, squid_phase::ready },
    { squid_phase::play, squid_command::split_all, squid_phase::play },
    { squid_phase::pause, squid_command::split_all, squid_phase::pause },
    { squid_phase::descent, squid_command::pause, squid_phase::descent_pause },
    { squid_phase::descent_pause, squid_command::pause, squid_phase::descent },
    { squid_phase::descent_pause, squid_command::restart, squid_phase::ready },
    { squid_phase::descent_pause, squid_command::calibrate, squid_phase::descent_pause },
    { squid_phase::landed, squid_command::restart, squid_phase::ready },
});

// Phase changes that come from the round itself, not from a command.
struct squid_event_edge {
    squid_phase from;
    squid_event event;
    squid_phase to;
};

inline constexpr auto SQUID_EVENT_EDGES = std::to_array<squid_event_edge>({
    { squid_phase::play, squid_event::pad_lost, squid_phase::pause },
    { squid_phase::descent, squid_event::pad_lost, squid_phase::descent_pause },
    { squid_phase::play, squid_event::goal_reached, squid_phase::descent },
    { squid_phase::descent, squid_event::landed, squid_phase::landed },
    { squid_phase::intro, squid_event::intro_done, squid_phase::ready },
    { squid_phase::arrive, squid_event::arrived, squid_phase::play },
});
// Every event needs a row, or the round could never make that move.
static_assert(std::ranges::all_of(std::views::iota(0uz, SQUID_EVENT_COUNT), [](std::size_t e) {
    return std::ranges::contains(SQUID_EVENT_EDGES, static_cast<squid_event>(e), &squid_event_edge::event);
}));

// Azimuth of the camera the phase is seen from: the far camera in the calibration, the scene camera otherwise. The tilt
// is read in that camera's screen frame, so a calibration over the far Earth carries over to the round's view.
inline float squid_phase_azimuth(squid_phase phase)
{
    return phase == squid_phase::ready ? SQUID_FAR_AZIMUTH_DEG : SQUID_CAMERA_AZIMUTH_DEG;
}

struct squid_game {
    squid_phase phase = squid_phase::language;
    // Seconds since the phase was entered; the timed phases (intro, arrive) end on it, and the stage animates by it.
    float phase_s = 0;
    squid_input input{};
    // The body set stepped in play, and the splits and absorbs of the current frame; zero on frames that do not step
    // the physics (prepare, ready, pause, pad loss, a frame the physics refused); a step refused for its dt changes nothing.
    squid_bodies bodies{};
    squid_event_counts events{};
    // Splits and absorbs since the round start: the physics frames and the split-all command both add to them.
    int round_splits = 0;
    int round_absorbs = 0;
    // Beer points this round; each play frame adds its drinks at SQUID_BEER_POINTS_PLAIN and SQUID_BEER_POINTS_MARKED.
    int beer_drunk = 0;
    squid_descent descent{}; // Taken at the goal, cleared by restart.
    // Draws the body drift in play; reseeded with the layout seed on every build, so a round and its restart repeat.
    std::minstd_rand drift_rng{ SQUID_LAYOUT_SEED };
};

// The start layout has one Alkoldun and the start balls; the body capacity must hold them.
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

// The start layout: Alkoldun at the centre, then the balls of SQUID_START_ROWS around it, row by row. Built into a fresh
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
    for (const squid_start_row &row : SQUID_START_ROWS) {
        for (int i = 0; i < row.count; ++i) {
            squid_body ball{};
            if (squid_body_ball(ball, row.mass, squid_layout_variant(rng), squid_flat{}) != RV_OK) {
                return RV_ERR_INVAL;
            }
            const int placed = squid_layout_place(set, rng, ball);
            if (placed != RV_OK) {
                return placed;
            }
        }
    }
    out = set;
    return RV_OK;
}

// Uniform in [lo, hi) from the generator: one draw. The float cast can round up to hi, so the result is kept below it.
inline float squid_uniform(std::minstd_rand &rng, float lo, float hi)
{
    const double span = static_cast<double>(rng.max()) - static_cast<double>(rng.min()) + 1.0;
    const double u = static_cast<double>(rng() - rng.min()) / span;
    const float x = lo + (hi - lo) * static_cast<float>(u);
    return x < hi ? x : std::nextafter(hi, lo);
}

// Birth spin and hover of every body in index order, from the round's drift generator: a spin magnitude, a uniform
// axis (height uniform in -1..1, azimuth uniform), a hover period and a hover phase; five draws per body.
inline void squid_motion_birth(squid_bodies &set, std::minstd_rand &rng)
{
    for (int i = 0; i < set.count; ++i) {
        squid_body &b = set.items[i];
        const float rate = squid_uniform(rng, SQUID_SPIN_MIN_DEG_S, SQUID_SPIN_MAX_DEG_S) * SQUID_DEG_TO_RAD;
        const float height = squid_uniform(rng, -1.0f, 1.0f);
        const float azimuth = squid_uniform(rng, 0.0f, 2.0f * std::numbers::pi_v<float>);
        const float ring = std::sqrt(1.0f - height * height);
        b.spin = rv_pdklib::rv_vec3{ rate * ring * std::cos(azimuth), rate * height, rate * ring * std::sin(azimuth) };
        b.hover_period = squid_uniform(rng, SQUID_HOVER_PERIOD_MIN_S, SQUID_HOVER_PERIOD_MAX_S);
        b.hover_phase = squid_uniform(rng, 0.0f, 2.0f * std::numbers::pi_v<float>);
    }
}

// Builds the start layout of a fresh game; the game starts with the language pick.
inline int squid_game_init(squid_game &g)
{
    const int layout = squid_layout_build(g.bodies);
    if (layout != RV_OK) {
        return layout;
    }
    g.drift_rng.seed(SQUID_LAYOUT_SEED);
    squid_motion_birth(g.bodies, g.drift_rng);
    g.phase = squid_phase::language;
    g.phase_s = 0;
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

// Balls per size (index size - 1) and the total mass of the set; Alkoldun counts in the mass only.
struct squid_census {
    std::array<int, std::size(SQUID_BALL_RADIUS)> balls{};
    int total_mass = 0;
};

inline squid_census squid_census_of(const squid_bodies &set)
{
    squid_census census{};
    for (int i = 0; i < set.count; ++i) {
        const squid_body &b = set.items[i];
        // Bottles are outside the ball budget, so the census leaves them out.
        census.total_mass += b.kind == squid_kind::bottle ? 0 : b.mass;
        int size = 0;
        if (b.kind == squid_kind::ball && squid_body_size(b, size) == RV_OK) {
            ++census.balls[static_cast<std::size_t>(size - 1)];
        }
    }
    return census;
}

// Balls of size 2 and up split under split-all; size 1 is the smallest and Alkoldun never splits.
constexpr int SQUID_SPLIT_ALL_MIN_SIZE = 2;

inline bool squid_splittable(const squid_body &b)
{
    int size = 0;
    return b.kind == squid_kind::ball && squid_body_size(b, size) == RV_OK && size >= SQUID_SPLIT_ALL_MIN_SIZE;
}

// Split normal of split-all: the halves sit on its tangent, which is z for this normal.
inline constexpr squid_flat SQUID_SPLIT_ALL_NORMAL{ 1.0f, 0.0f };

// Splits every splittable ball once, in index order; the halves are appended, so none is split twice. The physics pushes
// apart any overlap the halves make. Refused whole, with the set unchanged, when the set has no room for every split. A
// split refused midway stops the command: splits holds the splits made, and the set holds them too. A fridge split also
// gives an unmarked bottle, as in a pair split, at the centre.
inline int squid_split_all(squid_bodies &set, int &splits)
{
    splits = 0;
    const int count = set.count;
    int wanted = 0;
    for (int i = 0; i < count; ++i) {
        wanted += squid_splittable(set.items[i]) ? 1 : 0;
    }
    if (count + wanted > SQUID_BODY_CAPACITY) {
        RV_LOG_WARN(squid_state_tag,
            "split all refused: {} splits need {} bodies, capacity {}",
            wanted,
            count + wanted,
            SQUID_BODY_CAPACITY);
        return RV_ERR_NOMEM;
    }
    for (int i = 0; i < count; ++i) {
        if (!squid_splittable(set.items[i])) {
            continue;
        }
        // Room was checked above, and every mass in the set is 1..8, so a refusal here cannot happen; it is still logged.
        const int rc = squid_split_all_split(set, i, SQUID_SPLIT_ALL_NORMAL);
        if (rc != RV_OK) {
            RV_LOG_ERR(squid_state_tag, "split all stopped after {} splits: rv_err {}", splits, rc);
            return rc;
        }
        ++splits;
    }
    return RV_OK;
}

// Back to the round start: a fresh layout, the smoothed game tilt and keys level, and the round's bottle count; the neutral is
// kept. A refused layout changes nothing.
inline int squid_game_restart(squid_game &g)
{
    const int layout = squid_layout_build(g.bodies);
    if (layout != RV_OK) {
        return layout;
    }
    g.drift_rng.seed(SQUID_LAYOUT_SEED);
    squid_motion_birth(g.bodies, g.drift_rng);
    g.events = squid_event_counts{};
    g.round_splits = 0;
    g.round_absorbs = 0;
    g.beer_drunk = 0;
    g.descent = squid_descent{};
    squid_input_level(g.input);
    g.input.smoothed = squid_tilt{};
    return RV_OK;
}

// Split-all in play or pause: the round's split count takes every split made, also when the command stops midway.
inline int squid_game_split_all(squid_game &g)
{
    int splits = 0;
    const int rc = squid_split_all(g.bodies, splits);
    g.round_splits += splits;
    return rc;
}

// Runs the effect of an accepted command. A refused effect (bad calibration or layout) is returned and the phase stays.
inline int squid_game_apply(squid_game &g, squid_command command, const rv_istate &state, uint64_t abilities)
{
    switch (command) {
    case squid_command::calibrate:
        return squid_game_calibrate(g, state, abilities);
    case squid_command::restart:
        return squid_game_restart(g);
    case squid_command::split_all:
        return squid_game_split_all(g);
    case squid_command::pick:
    case squid_command::start:
    case squid_command::pause:
        break;
    }
    return RV_OK;
}

// Moves to a phase; entering a different phase restarts its clock.
inline void squid_game_enter(squid_game &g, squid_phase to)
{
    g.phase_s = to == g.phase ? g.phase_s : 0.0f;
    g.phase = to;
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
    squid_game_enter(g, edge->to);
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
    squid_game_enter(g, it->to);
    return RV_OK;
}

// One play frame: the input moves first, then the physics steps with its smoothed tilt. The input and body set
// are written only when the physics accepts the frame.
inline int squid_game_play(squid_game &g, const rv_istate &state, uint64_t abilities, float dt)
{
    squid_input next_input = g.input;
    const int input_rc = squid_input_step(next_input, state, abilities, dt, squid_phase_azimuth(g.phase));
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
    g.round_splits += counts.splits;
    g.round_absorbs += counts.absorbs;
    const int plain_drunk = counts.bottles_drunk - counts.marked_drunk;
    g.beer_drunk += plain_drunk * SQUID_BEER_POINTS_PLAIN + counts.marked_drunk * SQUID_BEER_POINTS_MARKED;
    if (g.beer_drunk >= SQUID_BEER_GOAL) {
        const int started = squid_descent_start(g.bodies, g.descent);
        return started != RV_OK ? started : squid_game_fire(g, squid_event::goal_reached);
    }
    return RV_OK;
}

// Pad loss pauses the descent; otherwise the frame advances it, and it lands when its time is up.
inline int squid_game_descend(squid_game &g, uint64_t abilities, float dt)
{
    if (abilities == 0) {
        RV_LOG_INFO(squid_state_tag, "pad lost: descent paused");
        return squid_game_fire(g, squid_event::pad_lost);
    }
    return squid_descent_advance(g.descent, dt) ? squid_game_fire(g, squid_event::landed) : RV_OK;
}

// The timed phases end when their clock passes their length: intro to the calibration, arrive to the round.
inline int squid_game_timed(squid_game &g)
{
    if (g.phase == squid_phase::intro && g.phase_s >= SQUID_INTRO_S) {
        return squid_game_fire(g, squid_event::intro_done);
    }
    if (g.phase == squid_phase::arrive && g.phase_s >= SQUID_ARRIVE_S) {
        return squid_game_fire(g, squid_event::arrived);
    }
    return RV_OK;
}

// One frame of dt seconds. The phase clock runs in every phase and ends the timed ones. Ready takes input only; play
// steps the physics, and losing the pad pauses the round. Language, intro, arrive and pause do not move. Descent runs
// only the descent; its pause does not move, and the landing frame changes only the phase.
inline int squid_game_step(squid_game &g, const rv_istate &state, uint64_t abilities, float dt)
{
    const bool dt_bad = !std::isfinite(dt) || dt <= 0.0f;
    if (dt_bad) {
        RV_LOG_ERR(squid_state_tag, "step refused: dt {} is not a positive finite number", dt);
        return RV_ERR_INVAL;
    }
    // The counts are of this frame only: a frame that steps no physics leaves them zero.
    g.events = squid_event_counts{};
    g.phase_s += dt;
    const bool timed = g.phase == squid_phase::intro || g.phase == squid_phase::arrive;
    if (timed) {
        return squid_game_timed(g);
    }
    if (g.phase == squid_phase::descent) {
        return squid_game_descend(g, abilities, dt);
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
    return playing ? squid_game_play(g, state, abilities, dt) :
                     squid_input_step(g.input, state, abilities, dt, squid_phase_azimuth(g.phase));
}

} // namespace squid
