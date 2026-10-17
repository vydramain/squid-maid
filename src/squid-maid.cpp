// squid-maid entry point: the game state machine driven by the front buttons, its sounds, the scene of the orbit and
// the debug view (docs/disdoc.md, 6, 7, 9, 11, 15.1, 15.2).
#include <algorithm>
#include <array>
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
#include "squid-input.hpp"
#include "squid-orbit.hpp"
#include "squid-params.hpp"
#include "squid-scene.hpp"
#include "squid-sound.hpp"
#include "squid-state.hpp"

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

// Port 0 front buttons by phase. SOUTH is the one "next" button: calibrate while preparing, start when ready.
constexpr auto SQUID_BUTTON_ROWS = std::to_array<squid_button_row>({
    { RV_ISOURCE_FRONT_BTTN_SOUTH, squid_phase::prepare, squid_command::calibrate },
    { RV_ISOURCE_FRONT_BTTN_SOUTH, squid_phase::ready, squid_command::start },
    { RV_ISOURCE_FRONT_BTTN_EAST, squid_phase::prepare, squid_command::free_mode },
    { RV_ISOURCE_FRONT_BTTN_EAST, squid_phase::ready, squid_command::free_mode },
    { RV_ISOURCE_FRONT_BTTN_NORTH, squid_phase::ready, squid_command::calibrate },
    { RV_ISOURCE_FRONT_BTTN_NORTH, squid_phase::pause, squid_command::calibrate },
    { RV_ISOURCE_BUMPER_RIGHT, squid_phase::play, squid_command::pause },
    { RV_ISOURCE_BUMPER_RIGHT, squid_phase::pause, squid_command::pause },
    { RV_ISOURCE_MENU_BTTN_VIEW, squid_phase::ready, squid_command::restart },
    { RV_ISOURCE_MENU_BTTN_VIEW, squid_phase::pause, squid_command::restart },
    { RV_ISOURCE_MENU_BTTN_VIEW, squid_phase::result, squid_command::restart },
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

// Events of one frame that may carry a sound: a press's calibration, a new hit, the entry into play, the end.
enum class squid_frame_event : int {
    calibrate = 0,
    hit = 1,
    start = 2,
    finish = 3,
};
// Count of the enum above; the flags are indexed by it.
constexpr std::size_t SQUID_FRAME_EVENT_COUNT = 4;
using squid_frame_flags = std::array<bool, SQUID_FRAME_EVENT_COUNT>;

struct squid_sound_rule {
    squid_frame_event event;
    squid_sound_event sound;
};

// One sound per frame, the first rule whose event happened: the single voice would cut the others anyway.
constexpr auto SQUID_SOUND_RULES = std::to_array<squid_sound_rule>({
    { squid_frame_event::finish, squid_sound_event::finish },
    { squid_frame_event::start, squid_sound_event::start },
    { squid_frame_event::hit, squid_sound_event::hit },
    { squid_frame_event::calibrate, squid_sound_event::calibrate },
});

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
            RV_LOG_ERR(SQUID_MAID_TAG, "no geometry controller: the orbit cannot be drawn");
            return RV_ERR_INVAL;
        }
        // The machine logs the cause; without its targets there is no round to play.
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
        // The loader logs the cause; without a font the view draws the orbit but no text.
        font_ready_ = squid_draw_font_load(rv_pdko_cv(pdk_), addr_font_, addr_palette_) == RV_OK;
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

        // Sounds compare the phase and hits with their values before this frame's press and step.
        const squid_phase phase_before = game_.phase;
        const int hits_before = game_.sequence.hits;
        squid_frame_flags happened{};
        note(press(pressed, state, abilities, happened));
        note(squid_game_step(game_, state, abilities, dt));
        note(sound_changes(phase_before, hits_before, happened));
        note(squid_orbit_tilt(game_.input.smoothed.x_deg, game_.input.smoothed.z_deg, rotation_));
        if (scene_ready_) {
            note(squid_scene_pose(world_, scene_, rotation_, game_.orbit));
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

    // The command of the frame's press, with its calibration flag in happened. WEST is not a machine command: it levels
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
        // Only a gyro calibration sets the neutral, so only it plays the calibrate sound.
        if (code == RV_OK) {
            happened[static_cast<std::size_t>(squid_frame_event::calibrate)] = calibrating && sensor_;
        }
        return code;
    }

    // Round events by comparison with the frame's start. Start is only the ready -> play entry: a resume from pause
    // is not a start. Then the frame's sound is played.
    int sound_changes(squid_phase phase_before, int hits_before, squid_frame_flags &happened)
    {
        happened[static_cast<std::size_t>(squid_frame_event::hit)] = game_.sequence.hits > hits_before;
        happened[static_cast<std::size_t>(squid_frame_event::start)] =
            phase_before == squid_phase::ready && game_.phase == squid_phase::play;
        happened[static_cast<std::size_t>(squid_frame_event::finish)] =
            game_.phase == squid_phase::result && phase_before != squid_phase::result;
        return play(happened);
    }

    // Plays the sound of the first rule that happened; a frame with no event plays nothing.
    int play(const squid_frame_flags &happened)
    {
        const squid_sound_rule *rule = sound_rule(happened);
        if (rule == nullptr) {
            return RV_OK;
        }
        return squid_sound_play(pdk_, sound_, rule->sound);
    }

    void log_state()
    {
        RV_LOG_INFO(SQUID_MAID_TAG,
            "frame {} phase {} source {} tilt x {:+.2f} z {:+.2f} orbit {:.4f} turn, "
            "hits {} of {}, time {:.2f} s, refusals {} last rv_err {}",
            frame_index_,
            SQUID_PHASE_NAMES[static_cast<std::size_t>(game_.phase)],
            sensor_ ? "sensor" : "reserve",
            game_.input.smoothed.x_deg,
            game_.input.smoothed.z_deg,
            game_.orbit.phase_rad / SQUID_ORBIT_TAU_RAD,
            game_.sequence.hits,
            SQUID_TARGET_COUNT,
            game_.time_s,
            refusals_,
            last_refusal_);
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
        draw_step("orbit", squid_draw_ring(cg, cv, rotation_));
        draw_step("cross", squid_draw_cross(cg, cv));
        if (font_ready_) {
            draw_step("text", squid_draw_text(cv, font_style(), game_.input.smoothed, sensor_));
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
        return rv_pdklib::rv_font_style_make(addr_font_, addr_palette_, SQUID_DRAW_TEXT_DEPTH, SQUID_DRAW_FONT_SCALE);
    }

    squid_game game_;
    squid_sound sound_;
    // The orbit's tilt, taken from the shown tilt each frame; kept when the conversion refuses.
    rv_pdklib::fx_vec3 rotation_{};
    rv_pdklib::ecs_world world_;
    squid_scene_objects scene_;
    bool scene_ready_ = false;
    uint64_t pad_buttons_ = 0;
    bool sensor_ = false;
    int64_t frame_index_ = 0;
    int refusals_ = 0;
    int last_refusal_ = RV_OK;
    int64_t addr_font_ = 0;
    int64_t addr_palette_ = 0;
    bool font_ready_ = false;
    bool draw_error_logged_ = false;
};

} // namespace squid

RV_MPPC_DISC_ENTRY_DEF(squid::squid_maid);
