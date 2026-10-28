#pragma once

// The first four systems of the squid frame, in run order: input, round, tilt, bodies. They reach the round and the view
// through their components; the disc supplies the frame's input and the round's dt before they run. A step's refusal is
// counted in the round and the system returns RV_OK: the kit ends the frame at the first returned code. Only a
// holder's code is returned: none or two.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "pdk/cio/rv_cio.h"
#include "pdk/cio/rv_isource.h"
#include "pdk/rv_err.h"
#include "pdk/rv_pdko.h"
#include "pdkkit/ecs/rv_ecs.hpp"
#include "pdkkit/ecs/rv_ecs_frame.hpp"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "squid-components.hpp"
#include "squid-input.hpp"
#include "squid-scene.hpp"
#include "squid-state.hpp"
#include "squid-view.hpp"

namespace squid
{

// Log tag of the registration; the input step logs with SQUID_INPUT_TAG from squid-input.hpp.
inline constexpr const char *SQUID_SYSTEMS_TAG = "systems";

// System names: the world keeps the views, so they are string literals.
inline constexpr std::string_view SQUID_SYSTEM_INPUT = "squid-input";
inline constexpr std::string_view SQUID_SYSTEM_ROUND = "squid-round";
inline constexpr std::string_view SQUID_SYSTEM_TILT = "squid-tilt";
inline constexpr std::string_view SQUID_SYSTEM_BODIES = "squid-bodies";

// A front button and the command it gives in one phase. A press with no row in the current phase does nothing.
struct squid_button {
    uint64_t button;
    squid_phase phase;
    squid_command command;
};

// Port 0 buttons by phase, named by the pad's labels: in the language pick A (SOUTH) is Russian and B (EAST) English;
// A starts from ready and takes no pose; Y (NORTH) sets the pose the pad is held in as level; View (MENU_BTTN_VIEW) pauses and
// resumes; B (EAST) restarts. L5 (lower left rear) splits the balls in play and pause; the input system passes it only while
// the debug panel is open.
inline constexpr auto SQUID_BUTTONS = std::to_array<squid_button>({
    { RV_ISOURCE_FRONT_BTTN_SOUTH, squid_phase::language, squid_command::pick },
    { RV_ISOURCE_FRONT_BTTN_EAST, squid_phase::language, squid_command::pick },
    { RV_ISOURCE_FRONT_BTTN_SOUTH, squid_phase::ready, squid_command::start },
    { RV_ISOURCE_FRONT_BTTN_NORTH, squid_phase::ready, squid_command::calibrate },
    { RV_ISOURCE_FRONT_BTTN_NORTH, squid_phase::pause, squid_command::calibrate },
    { RV_ISOURCE_MENU_BTTN_VIEW, squid_phase::play, squid_command::pause },
    { RV_ISOURCE_MENU_BTTN_VIEW, squid_phase::pause, squid_command::pause },
    { RV_ISOURCE_FRONT_BTTN_EAST, squid_phase::ready, squid_command::restart },
    { RV_ISOURCE_FRONT_BTTN_EAST, squid_phase::pause, squid_command::restart },
    { RV_ISOURCE_MENU_BTTN_VIEW, squid_phase::descent, squid_command::pause },
    { RV_ISOURCE_MENU_BTTN_VIEW, squid_phase::descent_pause, squid_command::pause },
    { RV_ISOURCE_FRONT_BTTN_NORTH, squid_phase::descent_pause, squid_command::calibrate },
    { RV_ISOURCE_FRONT_BTTN_EAST, squid_phase::descent_pause, squid_command::restart },
    { RV_ISOURCE_FRONT_BTTN_EAST, squid_phase::landed, squid_command::restart },
    { RV_ISOURCE_REAR_BTTN_LEFT_LOWER, squid_phase::play, squid_command::split_all },
    { RV_ISOURCE_REAR_BTTN_LEFT_LOWER, squid_phase::pause, squid_command::split_all },
});

// The first row the pressed buttons give in this phase. One row per frame: a command must not chain into the next
// phase's command within the same frame.
inline const squid_button *squid_button_row_of(uint64_t pressed, squid_phase phase)
{
    const auto it = std::find_if(SQUID_BUTTONS.begin(), SQUID_BUTTONS.end(), [pressed, phase](const squid_button &row) {
        return (pressed & row.button) != 0 && row.phase == phase;
    });
    return it == SQUID_BUTTONS.end() ? nullptr : &*it;
}

// The command of the frame's press, with its calibration flag in round.happened. X (WEST) is not a machine command: it
// levels the reserve's keys, so it stays outside the table.
inline int squid_input_press(squid_round &round, uint64_t pressed, const rv_istate &state, uint64_t abilities)
{
    if (pressed & RV_ISOURCE_FRONT_BTTN_WEST) {
        squid_input_level(round.game.input);
    }
    const squid_button *row = squid_button_row_of(pressed, round.game.phase);
    if (row == nullptr) {
        return RV_OK;
    }
    if (row->command == squid_command::pick) {
        round.lang = row->button == RV_ISOURCE_FRONT_BTTN_EAST ? squid_lang::en : squid_lang::ru;
    }
    const bool calibrating = row->command == squid_command::calibrate;
    const bool gyroless = calibrating && !round.sensor;
    if (gyroless) {
        RV_LOG_INFO(SQUID_INPUT_TAG, "calibration without a gyro: the neutral stays as it is");
    }
    const int code = squid_game_command(round.game, row->command, state, abilities);
    // A gyro calibration sets the neutral. The calibrate sound plays on pause, resume and restart too, and on a gyro
    // calibration, not on a start.
    const bool silent = row->command == squid_command::start || gyroless;
    if (code == RV_OK) {
        round.happened[static_cast<std::size_t>(squid_frame_event::calibrate)] = !silent;
    }
    return code;
}

// The tilt the input asks for this frame, in the screen's frame; the smoothed game tilt follows it.
inline squid_tilt squid_target_tilt(const squid_round &round, const rv_istate &state)
{
    if (round.sensor) {
        return squid_input_camera_frame(squid_input_sensor_target(round.game.input.neutral, state.motion));
    }
    // A zero step moves no key and returns the target; the copy keeps the game's keys as they are.
    squid_input probe = round.game.input;
    return squid_input_camera_frame(squid_input_reserve_step(probe, state, 0.0f));
}

// Edges and the debug switch, then the frame's commands. L4 flips the panel, which starts closed; L5 is a command only
// while the panel is open. A refused command is counted, not returned: the kit would stop the frame there.
inline int squid_system_input(rv_pdkkit::ecs_world &world, const rv_pdkkit::ecs_frame &frame)
{
    squid_round *round = nullptr;
    const int holder = squid_single(world, round);
    if (holder != RV_OK) {
        return holder;
    }
    const uint64_t abilities = rv_cio_iport_abilities(rv_pdko_cio(frame.pdk), 0);
    round->sensor = (abilities & RV_ISOURCE_GYRO_MOVE) != 0;
    round->phase_before = round->game.phase;
    round->happened = {};
    const bool rear = (frame.input.pressed & RV_ISOURCE_REAR_BTTN_LEFT_UPPER) != 0;
    round->debug_panel = round->debug_panel != rear;
    const uint64_t commands = round->debug_panel ? frame.input.pressed : frame.input.pressed & ~RV_ISOURCE_REAR_BTTN_LEFT_LOWER;
    squid_round_note(*round, squid_input_press(*round, commands, frame.input.state, abilities));
    return RV_OK;
}

// One physics step of the game, then the input's target tilt, which is kept only while both its axes are finite. A
// refused step is counted, not returned, so the frame goes on as the old disc did.
inline int squid_system_round(rv_pdkkit::ecs_world &world, const rv_pdkkit::ecs_frame &frame)
{
    squid_round *round = nullptr;
    const int holder = squid_single(world, round);
    if (holder != RV_OK) {
        return holder;
    }
    const uint64_t abilities = rv_cio_iport_abilities(rv_pdko_cio(frame.pdk), 0);
    const squid_phase phase_before_step = round->game.phase;
    const int step_code = squid_game_step(round->game, frame.input.state, abilities, round->dt);
    // Only a frame that stepped the physics moves the bodies.
    const bool playing = phase_before_step == squid_phase::play && round->game.phase == squid_phase::play;
    round->stepped = step_code == RV_OK && playing;
    // The goal's frame: the drink that reaches SQUID_BEER_GOAL moves play into descent, and its bell plays.
    round->descended = phase_before_step == squid_phase::play && round->game.phase == squid_phase::descent;
    // A frame the input refused may carry a non-finite stick or quaternion: the panel keeps the last target then.
    const squid_tilt target = squid_target_tilt(*round, frame.input.state);
    const bool target_finite = std::isfinite(target.x_deg) && std::isfinite(target.z_deg);
    round->target_tilt = target_finite ? target : round->target_tilt;
    squid_round_note(*round, step_code);
    return RV_OK;
}

// The round and the view, both needed by the tilt and bodies systems; the first refused holder's code (none or two) is
// returned.
inline int squid_round_view_of(rv_pdkkit::ecs_world &world, squid_round *&round, squid_view *&view)
{
    const int round_code = squid_single(world, round);
    if (round_code != RV_OK) {
        return round_code;
    }
    return squid_single(world, view);
}

// The scene's tilt root turns to the smoothed tilt, then the scene's objects follow the root. Both refusals are counted,
// not returned.
inline int squid_system_tilt(rv_pdkkit::ecs_world &world, const rv_pdkkit::ecs_frame &)
{
    squid_round *round = nullptr;
    squid_view *view = nullptr;
    const int holder = squid_round_view_of(world, round, view);
    if (holder != RV_OK) {
        return holder;
    }
    const float x_deg = round->game.input.smoothed.x_deg;
    const float z_deg = round->game.input.smoothed.z_deg;
    squid_round_note(*round, squid_scene_tilt(x_deg, z_deg, view->rotation));
    squid_round_note(*round, squid_scene_pose(world, view->objects, view->rotation));
    return RV_OK;
}

// The pool shows the bodies after this frame's step; a paused frame leaves them as they were. A refusal is counted, not
// returned.
inline int squid_system_bodies(rv_pdkkit::ecs_world &world, const rv_pdkkit::ecs_frame &)
{
    squid_round *round = nullptr;
    squid_view *view = nullptr;
    const int holder = squid_round_view_of(world, round, view);
    if (holder != RV_OK) {
        return holder;
    }
    if (!view->bodies_ready) {
        return RV_OK;
    }
    squid_round_note(*round, squid_view_update(world, view->pool, round->game.bodies, round->game.phase, round->game.descent));
    return RV_OK;
}

struct squid_system_entry {
    std::string_view name;
    rv_pdkkit::ecs_system run;
};

// The four systems in run order: the frame calls them in registration order.
inline constexpr auto SQUID_PLAY_SYSTEMS = std::to_array<squid_system_entry>({
    { SQUID_SYSTEM_INPUT, squid_system_input },
    { SQUID_SYSTEM_ROUND, squid_system_round },
    { SQUID_SYSTEM_TILT, squid_system_tilt },
    { SQUID_SYSTEM_BODIES, squid_system_bodies },
});

// Adds the four systems in run order. A refusal is logged with the system's name and code, and returned.
inline int squid_systems_play_register(rv_pdkkit::ecs_world &world)
{
    for (const squid_system_entry &entry : SQUID_PLAY_SYSTEMS) {
        const int code = rv_pdkkit::ecs_system_register(world, entry.name, entry.run);
        if (code != RV_OK) {
            RV_LOG_ERR(SQUID_SYSTEMS_TAG, "system {} refused: code {}", entry.name, code);
            return code;
        }
    }
    return RV_OK;
}

} // namespace squid
