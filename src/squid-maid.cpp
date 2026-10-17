// squid-maid entry point: the tilt input, the scene of the orbit and the debug view (docs/disdoc.md, 6, 15.1, 15.2).
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

namespace squid
{

namespace
{

// Log tag of the disc's own messages.
constexpr const char *SQUID_MAID_TAG = "maid";

// The state line is logged on frame 0 and then every this many frames.
constexpr int64_t SQUID_LOG_PERIOD_FRAMES = 60;

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
        // Edges, not levels: a held button calibrates or levels once.
        const uint64_t pressed = state.buttons & ~pad_buttons_;
        pad_buttons_ = state.buttons;
        sensor_ = (abilities & RV_ISOURCE_GYRO_MOVE) != 0;

        if (pressed & RV_ISOURCE_FRONT_BTTN_SOUTH) {
            calibrate(state);
        }
        if (pressed & RV_ISOURCE_FRONT_BTTN_WEST) {
            squid_input_level(input_);
        }
        note(squid_input_step(input_, state, abilities, dt));
        note(squid_orbit_step(orbit_, dt));
        note(squid_orbit_tilt(input_.smoothed.x_deg, input_.smoothed.z_deg, rotation_));
        if (scene_ready_) {
            note(squid_scene_pose(world_, scene_, rotation_, orbit_));
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

    // Takes the pose as neutral. Only a pad with a sensor has a pose; on the reserve the press changes nothing.
    void calibrate(const rv_istate &state)
    {
        if (!sensor_) {
            RV_LOG_INFO(SQUID_MAID_TAG, "calibrate not applied: the pad has no sensor");
            return;
        }
        const int code = squid_input_calibrate(input_, state);
        note(code);
        if (code != RV_OK) {
            return;
        }
        RV_LOG_INFO(SQUID_MAID_TAG,
            "calibrated: neutral quat ({:.3f}, {:.3f}, {:.3f}, {:.3f})",
            input_.neutral.x,
            input_.neutral.y,
            input_.neutral.z,
            input_.neutral.w);
    }

    void log_state()
    {
        RV_LOG_INFO(SQUID_MAID_TAG,
            "frame {} source {} tilt x {:+.2f} z {:+.2f} orbit {:.4f} turn, refusals {} last rv_err {}",
            frame_index_,
            sensor_ ? "sensor" : "reserve",
            input_.smoothed.x_deg,
            input_.smoothed.z_deg,
            orbit_.phase_rad / SQUID_ORBIT_TAU_RAD,
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
            draw_step("text", squid_draw_text(cv, font_style(), input_.smoothed, sensor_));
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

    squid_input input_;
    squid_orbit orbit_;
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
