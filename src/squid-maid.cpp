// squid-maid entry point: the ball toy's state machine driven by the front buttons, its sounds, its scene and the
// debug view.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "pdk/cg/rv_cg.h"
#include "pdk/cio/rv_cio.h"
#include "pdk/cio/rv_isource.h"
#include "pdk/cv/rv_cv.h"
#include "pdk/rv_err.h"
#include "pdk/rv_pdko.h"
#include "pdklib/rv_cppdisc/rv_cppdisc.hpp"
#include "pdklib/rv_ecs/rv_ecs.hpp"
#include "pdklib/rv_ecs/rv_ecs_frame.hpp"
#include "pdklib/rv_font/rv_font.hpp"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "squid-draw.hpp"
#include "squid-hud.hpp"
#include "squid-input.hpp"
#include "squid-params.hpp"
#include "squid-scene.hpp"
#include "squid-sound.hpp"
#include "squid-state.hpp"
#include "squid-view.hpp"

namespace squid
{

namespace
{

// Log tag of the disc's own messages.
constexpr const char *SQUID_MAID_TAG = "maid";

// The state line is logged on frame 0 and then every this many frames.
constexpr int64_t SQUID_LOG_PERIOD_FRAMES = 60;

// A front button and the command it gives in one phase. A press with no row in the current phase does nothing.
struct squid_button_row {
    uint64_t button;
    squid_phase phase;
    squid_command command;
};

// Port 0 buttons by phase, named by the pad's labels: A (SOUTH) is the one "next" button, calibrate while preparing and
// start when ready; Y (NORTH) recalibrates; View (MENU_BTTN_VIEW) pauses and resumes; B (EAST) restarts.
constexpr auto SQUID_BUTTON_ROWS = std::to_array<squid_button_row>({
    { RV_ISOURCE_FRONT_BTTN_SOUTH, squid_phase::prepare, squid_command::calibrate },
    { RV_ISOURCE_FRONT_BTTN_SOUTH, squid_phase::ready, squid_command::start },
    { RV_ISOURCE_FRONT_BTTN_NORTH, squid_phase::ready, squid_command::calibrate },
    { RV_ISOURCE_FRONT_BTTN_NORTH, squid_phase::pause, squid_command::calibrate },
    { RV_ISOURCE_MENU_BTTN_VIEW, squid_phase::play, squid_command::pause },
    { RV_ISOURCE_MENU_BTTN_VIEW, squid_phase::pause, squid_command::pause },
    { RV_ISOURCE_FRONT_BTTN_EAST, squid_phase::ready, squid_command::restart },
    { RV_ISOURCE_FRONT_BTTN_EAST, squid_phase::pause, squid_command::restart },
});

// The first row the pressed buttons give in this phase. One row per frame: a command must not chain into the next
// phase's command within the same frame.
const squid_button_row *button_row(uint64_t pressed, squid_phase phase)
{
    const auto it =
        std::find_if(SQUID_BUTTON_ROWS.begin(), SQUID_BUTTON_ROWS.end(), [pressed, phase](const squid_button_row &row) {
            return (pressed & row.button) != 0 && row.phase == phase;
        });
    return it == SQUID_BUTTON_ROWS.end() ? nullptr : &*it;
}

// Events of one frame that may carry a sound: a calibrate-type press, a frame with a split and a frame with a bounce.
enum class squid_frame_event : int {
    calibrate = 0,
    split = 1,
    bounce = 2,
};
// Count of the enum above; the flags are indexed by it.
constexpr std::size_t SQUID_FRAME_EVENT_COUNT = 3;
using squid_frame_flags = std::array<bool, SQUID_FRAME_EVENT_COUNT>;

struct squid_sound_rule {
    squid_frame_event event;
    squid_sound_event sound;
};

// One sound per frame, the first rule whose event happened: the table order is the priority, the single voice would cut
// the others anyway.
constexpr auto SQUID_SOUND_RULES = std::to_array<squid_sound_rule>({
    { squid_frame_event::calibrate, squid_sound_event::calibrate },
    { squid_frame_event::split, squid_sound_event::split },
    { squid_frame_event::bounce, squid_sound_event::bounce },
});

// The flags, the events and the rules must agree: one flag per event, and every event has a rule.
static_assert(static_cast<std::size_t>(squid_frame_event::bounce) + 1 == SQUID_FRAME_EVENT_COUNT,
    "SQUID_FRAME_EVENT_COUNT must equal the number of squid_frame_event values");
constexpr bool squid_sound_rules_cover_events()
{
    for (std::size_t event = 0; event < SQUID_FRAME_EVENT_COUNT; ++event) {
        const auto covers = [event](const squid_sound_rule &rule) {
            return static_cast<std::size_t>(rule.event) == event;
        };
        if (std::none_of(SQUID_SOUND_RULES.begin(), SQUID_SOUND_RULES.end(), covers)) {
            return false;
        }
    }
    return true;
}
static_assert(squid_sound_rules_cover_events(), "SQUID_SOUND_RULES needs a rule for every squid_frame_event");

const squid_sound_rule *sound_rule(const squid_frame_flags &happened)
{
    const auto it = std::find_if(SQUID_SOUND_RULES.begin(), SQUID_SOUND_RULES.end(), [&happened](const squid_sound_rule &rule) {
        return happened[static_cast<std::size_t>(rule.event)];
    });
    return it == SQUID_SOUND_RULES.end() ? nullptr : &*it;
}

} // namespace

// rv_cppdisc.hpp supplies the startup guards, MENU-button tracking and the frame plumbing.
RV_MPPC_DISC_CPP_DEF(squid_maid_base_)

class squid_maid : public squid_maid_base_
{
public:
    int64_t disc_initialize(rv_pdko *pdk)
    {
        const int64_t base_result = squid_maid_base_::disc_initialize(pdk);
        if (base_result < 0) {
            return base_result;
        }
        if (rv_pdko_cg(pdk_) == nullptr) {
            RV_LOG_ERR(SQUID_MAID_TAG, "no geometry controller: the camera cannot be set");
            return RV_ERR_INVAL;
        }
        // The machine logs the cause; without the start layout there is no round to play.
        const int game_code = squid_game_init(game_);
        if (game_code != RV_OK) {
            return game_code;
        }
        // The loader logs the cause; without sounds each event is refused and logged by its play.
        note(squid_sound_init(pdk_, sound_));
        // The loader logs the cause; without the scene the disc draws the debug view only.
        const int scene_code = squid_scene_load(world_, rv_pdko_cd(pdk_), scene_);
        note(scene_code);
        scene_ready_ = scene_code == RV_OK;
        // The pool logs its refusal; without it no body is shown and the rest of the game runs.
        if (scene_ready_) {
            const int pool_code = squid_view_spawn(world_, rv_pdko_cd(pdk_), scene_, pool_);
            note(pool_code);
            bodies_ready_ = pool_code == RV_OK;
        }
        // The loader logs the cause; without a font the view draws no text.
        font_ready_ = squid_draw_font_load(rv_pdko_cv(pdk_), addr_font_, addr_palette_) == RV_OK;
        const uint64_t abilities = rv_cio_iport_abilities(rv_pdko_cio(pdk_), 0);
        note(log_config((abilities & RV_ISOURCE_GYRO_MOVE) != 0));
        return RV_OK;
    }

    void frame_update(float dt)
    {
        squid_maid_base_::frame_update(dt);
        rv_cio *cio = rv_pdko_cio(pdk_);
        const rv_istate state = rv_cio_iport_state(cio, 0);
        const uint64_t abilities = rv_cio_iport_abilities(cio, 0);
        // Edges, not levels: a held button gives its command once.
        const uint64_t pressed = state.buttons & ~pad_buttons_;
        pad_buttons_ = state.buttons;
        sensor_ = (abilities & RV_ISOURCE_GYRO_MOVE) != 0;

        // The start log compares the phase with its value before this frame's press and step.
        const squid_phase phase_before = game_.phase;
        squid_frame_flags happened{};
        // The debug panel's switch: each press of the upper left rear button (L4) flips it; it starts off.
        const bool rear = (pressed & RV_ISOURCE_REAR_BTTN_LEFT_UPPER) != 0;
        debug_panel_ = debug_panel_ != rear;
        note(press(pressed, state, abilities, happened));
        const squid_phase phase_before_step = game_.phase;
        const int step_code = squid_game_step(game_, state, abilities, dt);
        note(step_code);
        // Only a frame that stepped the physics moves the bodies.
        const bool stepped = step_code == RV_OK && phase_before_step == squid_phase::play && game_.phase == squid_phase::play;
        // A frame the input refused may carry a non-finite stick or quaternion: the panel keeps the last target then.
        const squid_tilt target = target_tilt(state);
        if (std::isfinite(target.x_deg) && std::isfinite(target.z_deg)) {
            target_tilt_ = target;
        }
        note(sound_changes(stepped, happened));
        // The round start has no sound; it logs the config once.
        const bool started = phase_before == squid_phase::ready && game_.phase == squid_phase::play;
        if (started) {
            note(log_config(sensor_));
        }
        note(squid_scene_tilt(game_.input.smoothed.x_deg, game_.input.smoothed.z_deg, rotation_));
        if (scene_ready_) {
            note(squid_scene_pose(world_, scene_, rotation_));
        }
        // After the step, so the bodies show this frame's positions and display rotation; a paused frame leaves both as
        // they were.
        if (bodies_ready_) {
            note(squid_view_update(world_, pool_, game_.bodies));
        }

        if (frame_index_ % SQUID_LOG_PERIOD_FRAMES == 0) {
            log_state();
            if (scene_ready_) {
                note(squid_scene_log(world_, scene_));
            }
        }
        ++frame_index_;
    }

    void frame_render()
    {
        frame_begin(SQUID_DRAW_BACKGROUND);
        draw_step("stars", squid_draw_stars(rv_pdko_cv(pdk_)));
        draw_scene();
        draw_view();
        frame_end();
    }

    void disc_shutdown()
    {
        // The font's video assets belong to this disc; they are freed here, once. A failed free is logged by
        // squid_draw_release; there is no caller left to return it to.
        if (font_ready_) {
            rv_cv *cv = rv_pdko_cv(pdk_);
            (void)squid_draw_release(cv, addr_font_);
            (void)squid_draw_release(cv, addr_palette_);
            font_ready_ = false;
        }
    }

    const char *disc_title() const
    {
        return "squid-maid";
    }

private:
    // Refusals are logged by the module that refused; here they are only counted, so the state line shows them.
    void note(int code)
    {
        if (code != RV_OK) {
            ++refusals_;
            last_refusal_ = code;
        }
    }

    // The command of the frame's press, with its calibration flag in happened. X (WEST) is not a machine command: it levels
    // the reserve's keys, an input action with no state edge in the machine, so it stays outside the table.
    int press(uint64_t pressed, const rv_istate &state, uint64_t abilities, squid_frame_flags &happened)
    {
        if (pressed & RV_ISOURCE_FRONT_BTTN_WEST) {
            squid_input_level(game_.input);
        }
        const squid_button_row *row = button_row(pressed, game_.phase);
        if (row == nullptr) {
            return RV_OK;
        }
        const bool calibrating = row->command == squid_command::calibrate;
        if (calibrating && !sensor_) {
            RV_LOG_INFO(SQUID_MAID_TAG, "calibration without a gyro: the neutral stays as it is");
        }
        const int code = squid_game_command(game_, row->command, state, abilities);
        // Only a gyro calibration sets the neutral; pause, resume and restart play the calibrate sound too, a start none.
        const bool silent = row->command == squid_command::start || (calibrating && !sensor_);
        if (code == RV_OK) {
            happened[static_cast<std::size_t>(squid_frame_event::calibrate)] = !silent;
        }
        return code;
    }

    // Round events of a stepped frame: a split or a bounce. A split is a count of splits, however many it holds, and a
    // bounce a count of bounces. A step refused for its dt keeps last frame's counts, so only a stepped frame can carry
    // either. The frame plays one sound: the first rule of SQUID_SOUND_RULES that happened.
    int sound_changes(bool stepped, squid_frame_flags &happened)
    {
        happened[static_cast<std::size_t>(squid_frame_event::split)] = stepped && game_.events.splits > 0;
        happened[static_cast<std::size_t>(squid_frame_event::bounce)] = stepped && game_.events.bounces > 0;
        return play(happened);
    }

    // Plays the sound of the first rule that happened; a frame with no event plays nothing.
    int play(const squid_frame_flags &happened)
    {
        const squid_sound_rule *rule = sound_rule(happened);
        if (rule == nullptr) {
            return RV_OK;
        }
        bool started = false;
        const int code = squid_sound_play(pdk_, sound_, rule->sound, started);
        if (started) {
            ++sounds_started_;
        }
        return code;
    }

    // Logs the state, then restarts the count of sounds started, so each line counts the frames since the last one.
    void log_state()
    {
        RV_LOG_INFO(SQUID_MAID_TAG,
            "frame {} phase {} source {} tilt x {:+.2f} z {:+.2f} bodies {}, splits {} absorbs {}, "
            "sounds {}, refusals {} last rv_err {}",
            frame_index_,
            SQUID_PHASE_NAMES[static_cast<std::size_t>(game_.phase)],
            sensor_ ? "sensor" : "reserve",
            game_.input.smoothed.x_deg,
            game_.input.smoothed.z_deg,
            game_.bodies.count,
            game_.events.splits,
            game_.events.absorbs,
            sounds_started_,
            refusals_,
            last_refusal_);
        sounds_started_ = 0;
    }

    // The tilt the input asks for this frame; the smoothed game tilt follows it.
    squid_tilt target_tilt(const rv_istate &state) const
    {
        if (sensor_) {
            return squid_input_sensor_target(game_.input.neutral, state.motion);
        }
        // A zero step moves no key and returns the target; the copy keeps the game's keys as they are.
        squid_input probe = game_.input;
        return squid_input_reserve_step(probe, state, 0.0f);
    }

    squid_hud_round hud_round() const
    {
        return squid_hud_round{ game_.phase, game_.beer_drunk };
    }

    squid_hud_debug hud_debug() const
    {
        return squid_hud_debug{ target_tilt_, game_.input.smoothed, sensor_ };
    }

    // Input parameters, input source and the start layout: what a report needs to reproduce a round.
    int log_config(bool sensor) const
    {
        RV_LOG_INFO(SQUID_MAID_TAG,
            "input: tilt limit {:.0f} deg, full input at {:.0f} deg, dead zone {:.0f} deg, smoothing {:.2f} s, source {}",
            SQUID_TILT_LIMIT_DEG,
            SQUID_FULL_INPUT_TILT_DEG,
            SQUID_DEAD_ZONE_DEG,
            SQUID_SMOOTHING_S,
            sensor ? "sensor" : "reserve");
        // Rebuilt from the seed, not read from game_.bodies: the set is stepped by the time a start is logged.
        squid_bodies start{};
        const int built = squid_layout_build(start);
        if (built != RV_OK) {
            return built;
        }
        int total_mass = 0;
        for (int i = 0; i < start.count; ++i) {
            total_mass += start.items[i].mass;
        }
        RV_LOG_INFO(SQUID_MAID_TAG, "start layout: {} bodies, total mass {}", start.count, total_mass);
        return RV_OK;
    }

    // The scene's models, in the ECS frame; the debug view is drawn over them.
    void draw_scene()
    {
        if (!scene_ready_) {
            return;
        }
        const int code = rv_pdklib::ecs_world_frame(world_, rv_pdklib::ecs_frame{ pdk_, frame_index_ });
        draw_step("scene", code);
    }

    void draw_view()
    {
        rv_cg *cg = rv_pdko_cg(pdk_);
        rv_cv *cv = rv_pdko_cv(pdk_);
        rv_cg_camera camera{};
        squid_scene_camera_make(camera);
        const int camera_code = static_cast<int>(rv_cg_frame_camera(cg, &camera));
        draw_step("camera", camera_code);
        if (camera_code != RV_OK) {
            return;
        }
        if (font_ready_) {
            draw_step("hud", squid_hud_draw(cv, font_style(), hud_round(), hud_debug(), debug_panel_));
        }
    }

    // A refused draw step counts as a refusal and is logged once, with its cause.
    void draw_step(const char *step, int code)
    {
        note(code);
        log_draw_error(step, code);
    }

    // Logs the first failure only: the same failure would repeat in every frame.
    void log_draw_error(const char *step, int code)
    {
        if (code == RV_OK || draw_error_logged_) {
            return;
        }
        draw_error_logged_ = true;
        RV_LOG_ERR(SQUID_MAID_TAG, "{} not drawn: rv_err {}", step, code);
    }

    rv_pdklib::rv_font_style font_style() const
    {
        return rv_pdklib::rv_font_style_make(addr_font_,
            addr_palette_,
            SQUID_DRAW_TEXT_DEPTH,
            SQUID_DRAW_FONT_SCALE,
            SQUID_DRAW_FONT_BLOCKS);
    }

    squid_game game_;
    squid_sound sound_;
    // The tilt root's rotation, taken from the shown tilt each frame; kept when the conversion refuses.
    rv_pdklib::fx_vec3 rotation_{};
    // The input's target tilt; the debug panel shows it beside the smoothed game tilt, not the 1/3 picture.
    squid_tilt target_tilt_{};
    // Upper left rear button (L4) toggles the debug panel; off at start.
    bool debug_panel_ = false;
    rv_pdklib::ecs_world world_;
    squid_scene_objects scene_;
    bool scene_ready_ = false;
    // One scene entity per body slot; bodies_ready_ when every slot is loaded.
    squid_view_pool pool_{};
    bool bodies_ready_ = false;
    uint64_t pad_buttons_ = 0;
    bool sensor_ = false;
    int64_t frame_index_ = 0;
    int refusals_ = 0;
    int last_refusal_ = RV_OK;
    // Sounds started since the last state line.
    int sounds_started_ = 0;
    int64_t addr_font_ = 0;
    int64_t addr_palette_ = 0;
    bool font_ready_ = false;
    bool draw_error_logged_ = false;
};

} // namespace squid

RV_MPPC_DISC_ENTRY_DEF(squid::squid_maid);
