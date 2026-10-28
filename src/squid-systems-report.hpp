#pragma once

// The last two ECS systems of the frame: the sound effects and the state log. They read the round and the view from their
// components; the frame's flags are set by the earlier systems.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

#include "pdk/rv_err.h"
#include "pdk/rv_pdko.h"
#include "pdkkit/ecs/rv_ecs.hpp"
#include "pdkkit/ecs/rv_ecs_frame.hpp"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "squid-components.hpp"
#include "squid-params.hpp"
#include "squid-scene.hpp"
#include "squid-sound.hpp"
#include "squid-state.hpp"

namespace squid
{

// Log tag of the report systems' messages; the runs compare these lines, so the tag stays.
constexpr const char *SQUID_LOG_TAG = "maid";

// The state line is logged on frame 0 and then every this many frames.
constexpr int64_t SQUID_STATE_PERIOD_FRAMES = 60;

struct squid_sound_link {
    squid_frame_event event;
    squid_sound_event sound;
};

// One sound per frame, the first link whose event happened: the table order is the priority, the single voice would cut
// the others anyway.
constexpr auto SQUID_SOUND_LINKS = std::to_array<squid_sound_link>({
    { squid_frame_event::calibrate, squid_sound_event::calibrate },
    { squid_frame_event::bell, squid_sound_event::bell },
    { squid_frame_event::cheers, squid_sound_event::cheers },
    { squid_frame_event::can_glass, squid_sound_event::can_glass },
    { squid_frame_event::split, squid_sound_event::split },
    { squid_frame_event::glass_hit, squid_sound_event::glass_hit },
    { squid_frame_event::bounce, squid_sound_event::bounce },
});

// The flags and the links must agree: every frame event has a link.
constexpr bool squid_sound_links_cover_events()
{
    for (std::size_t event = 0; event < SQUID_FRAME_EVENT_COUNT; ++event) {
        const auto covers = [event](const squid_sound_link &link) {
            return static_cast<std::size_t>(link.event) == event;
        };
        if (std::none_of(SQUID_SOUND_LINKS.begin(), SQUID_SOUND_LINKS.end(), covers)) {
            return false;
        }
    }
    return true;
}
static_assert(squid_sound_links_cover_events(), "SQUID_SOUND_LINKS needs a link for every squid_frame_event");

inline const squid_sound_link *squid_sound_link_of(const squid_frame_flags &happened)
{
    const auto it = std::find_if(SQUID_SOUND_LINKS.begin(), SQUID_SOUND_LINKS.end(), [&happened](const squid_sound_link &link) {
        return happened[static_cast<std::size_t>(link.event)];
    });
    return it == SQUID_SOUND_LINKS.end() ? nullptr : &*it;
}

// Events of the frame that carry a sound: the bell from the goal's frame, the rest from the physics counts of a stepped
// frame. A split is a count of splits, however many it holds, and a bounce a count of bounces. A step refused for its dt
// keeps last frame's counts, so only a stepped frame can carry them. A fridge split with no bottle (at the 16-bottle cap)
// is a plain split. An unmarked drink is a glass touch too. The frame plays the first link that happened.
inline int squid_sound_changes(squid_round &round, squid_effects &effects, rv_pdko *pdk)
{
    const squid_event_counts &events = round.game.events;
    const auto mark = [&round](squid_frame_event event, bool on) {
        round.happened[static_cast<std::size_t>(event)] = on;
    };
    mark(squid_frame_event::bell, round.descended);
    mark(squid_frame_event::cheers, round.stepped && events.marked_drunk > 0);
    mark(squid_frame_event::can_glass, round.stepped && events.bottles_born > 0);
    mark(squid_frame_event::split, round.stepped && events.splits > 0);
    const bool unmarked_drunk = events.bottles_drunk > events.marked_drunk;
    mark(squid_frame_event::glass_hit, round.stepped && (events.bottle_bounces > 0 || unmarked_drunk));
    mark(squid_frame_event::bounce, round.stepped && events.bounces > 0);

    // A frame with no event plays nothing.
    const squid_sound_link *link = squid_sound_link_of(round.happened);
    if (link == nullptr) {
        return RV_OK;
    }
    bool started = false;
    const int code = squid_sound_play(pdk, effects.sound, link->sound, started);
    if (started) {
        ++round.sounds_started;
    }
    return code;
}

// Logs the state, then restarts the count of sounds started, so each line counts the frames since the last one.
inline void squid_log_state(squid_round &round)
{
    RV_LOG_INFO(SQUID_LOG_TAG,
        "frame {} phase {} source {} tilt x {:+.2f} z {:+.2f} bodies {}, splits {} absorbs {}, "
        "sounds {}, refusals {} last rv_err {}",
        round.frame,
        SQUID_PHASE_NAMES[static_cast<std::size_t>(round.game.phase)],
        round.sensor ? "sensor" : "reserve",
        round.game.input.smoothed.x_deg,
        round.game.input.smoothed.z_deg,
        round.game.bodies.count,
        round.game.events.splits,
        round.game.events.absorbs,
        round.sounds_started,
        round.refusals,
        round.last_refusal);
    round.sounds_started = 0;
}

// Input parameters, input source and the start layout: what a report needs to reproduce a round.
inline int squid_log_config(bool sensor)
{
    RV_LOG_INFO(SQUID_LOG_TAG,
        "input: tilt limit {:.0f} deg, full input at {:.0f} deg, dead zone {:.0f} deg, smoothing {:.2f} s, source {}",
        SQUID_TILT_LIMIT_DEG,
        SQUID_FULL_INPUT_TILT_DEG,
        SQUID_DEAD_ZONE_DEG,
        SQUID_SMOOTHING_S,
        sensor ? "sensor" : "reserve");
    // Rebuilt from the seed, not read from the bodies: the set is stepped by the time a start is logged.
    squid_bodies start{};
    const int built = squid_layout_build(start);
    if (built != RV_OK) {
        RV_LOG_ERR(SQUID_LOG_TAG, "start layout not built: rv_err {}", built);
        return built;
    }
    int total_mass = 0;
    for (int i = 0; i < start.count; ++i) {
        total_mass += start.items[i].mass;
    }
    RV_LOG_INFO(SQUID_LOG_TAG, "start layout: {} bodies, total mass {}", start.count, total_mass);
    return RV_OK;
}

// Frame system: the frame's sound. A refusal is counted for the state line; the frame goes on, as the disc's note did.
// A code comes back only with no holder or two, when the system has nothing to do.
inline int squid_system_effects(rv_pdkkit::ecs_world &world, const rv_pdkkit::ecs_frame &frame)
{
    squid_round *round = nullptr;
    const int round_code = squid_single(world, round);
    if (round_code != RV_OK) {
        return round_code;
    }
    squid_effects *effects = nullptr;
    const int effects_code = squid_single(world, effects);
    if (effects_code != RV_OK) {
        return effects_code;
    }
    squid_round_note(*round, squid_sound_changes(*round, *effects, frame.pdk));
    return RV_OK;
}

// The state line and the scene's tilt on frame 0 and every SQUID_STATE_PERIOD_FRAMES frames after. A refused scene log is
// counted for the state line; the frame goes on.
inline void squid_log_period(squid_round &round, rv_pdkkit::ecs_world &world, const squid_view &view)
{
    const bool period_due = round.frame % SQUID_STATE_PERIOD_FRAMES == 0;
    if (!period_due) {
        return;
    }
    squid_log_state(round);
    squid_round_note(round, squid_scene_log(world, view.objects));
}

// Frame system: the config at the round's start, the period's lines, then the frame counter. Refusals are counted for the
// state line; the frame goes on, as the disc's note did. A code comes back only with no holder or two, when there is
// nothing to do.
inline int squid_system_log(rv_pdkkit::ecs_world &world, const rv_pdkkit::ecs_frame &)
{
    squid_round *round = nullptr;
    const int round_code = squid_single(world, round);
    if (round_code != RV_OK) {
        return round_code;
    }
    squid_view *view = nullptr;
    const int view_code = squid_single(world, view);
    if (view_code != RV_OK) {
        return view_code;
    }
    const bool started = round->phase_before == squid_phase::ready && round->game.phase == squid_phase::play;
    squid_round_note(*round, started ? squid_log_config(round->sensor) : RV_OK);
    squid_log_period(*round, world, *view);
    ++round->frame;
    return RV_OK;
}

// Names of the report systems; the world keeps the name views, so they are literals.
constexpr const char *SQUID_SYSTEM_EFFECTS = "squid-effects";
constexpr const char *SQUID_SYSTEM_LOG = "squid-log";

// Registers the effects system, then the log system: the frame runs them in this order.
inline int squid_systems_report_register(rv_pdkkit::ecs_world &world)
{
    const int effects_code = rv_pdkkit::ecs_system_register(world, SQUID_SYSTEM_EFFECTS, squid_system_effects);
    if (effects_code != RV_OK) {
        RV_LOG_ERR(SQUID_LOG_TAG, "system {} refused: rv_err {}", SQUID_SYSTEM_EFFECTS, effects_code);
        return effects_code;
    }
    const int log_code = rv_pdkkit::ecs_system_register(world, SQUID_SYSTEM_LOG, squid_system_log);
    if (log_code != RV_OK) {
        RV_LOG_ERR(SQUID_LOG_TAG, "system {} refused: rv_err {}", SQUID_SYSTEM_LOG, log_code);
        return log_code;
    }
    return RV_OK;
}

} // namespace squid
