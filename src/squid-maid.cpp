// squid-maid entry point: registers the squid components and systems, loads the scene and draws the frame. The round's
// logic runs in the systems (squid-systems-play.hpp, squid-systems-report.hpp, squid-music.hpp).
#include <cstdint>

#include "pdk/cio/rv_cio.h"
#include "pdk/cio/rv_isource.h"
#include "pdk/cv/rv_cv.h"
#include "pdk/rv_err.h"
#include "pdk/rv_pdko.h"
#include "pdkkit/disc/rv_pdkkit_disc.hpp"
#include "pdklib/rv_font/rv_font.hpp"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "squid-components.hpp"
#include "squid-draw.hpp"
#include "squid-hud.hpp"
#include "squid-music.hpp"
#include "squid-scene.hpp"
#include "squid-state.hpp"
#include "squid-systems-play.hpp"
#include "squid-systems-report.hpp"
#include "squid-view.hpp"

namespace squid
{

// The base supplies the world, the built-in components, the frame clock and the input.
class squid_maid : public rv_pdkkit::pdkkit_disc
{
public:
    int64_t disc_initialize(rv_pdko *pdk)
    {
        const int64_t base_result = rv_pdkkit::pdkkit_disc::disc_initialize(pdk);
        if (base_result != RV_OK) {
            return base_result;
        }
        if (rv_pdko_cg(pdk_) == nullptr) {
            RV_LOG_ERR(SQUID_LOG_TAG, "no geometry controller: the scene cannot be drawn");
            return RV_ERR_INVAL;
        }
        // Before the scene: its objects are created under the registered names, and the systems read them.
        const int components_code = squid_components_register(world_);
        if (components_code != RV_OK) {
            return components_code;
        }
        const int play_code = squid_systems_play_register(world_);
        if (play_code != RV_OK) {
            return play_code;
        }
        const int report_code = squid_systems_report_register(world_);
        if (report_code != RV_OK) {
            return report_code;
        }
        const int music_code = squid_music_register(world_);
        if (music_code != RV_OK) {
            return music_code;
        }
        // The loader logs the cause; without the scene there is no round and the disc draws the stars only.
        squid_scene_objects objects{};
        scene_ready_ = squid_scene_load(world_, rv_pdko_cd(pdk_), objects) == RV_OK;
        if (scene_ready_) {
            const int start_code = start_round(objects);
            if (start_code != RV_OK) {
                return start_code;
            }
        }
        // The loader logs the cause; without a font the view draws no text.
        font_ready_ = squid_draw_font_load(rv_pdko_cv(pdk_), addr_font_, addr_palette_) == RV_OK;
        const uint64_t abilities = rv_cio_iport_abilities(rv_pdko_cio(pdk_), 0);
        note(squid_log_config((abilities & RV_ISOURCE_GYRO_MOVE) != 0));
        return RV_OK;
    }

    void frame_update(float dt)
    {
        rv_pdkkit::pdkkit_disc::frame_update(dt);
        // The systems read dt from the round; a missing round is logged by the systems that need it.
        squid_round *round = nullptr;
        if (scene_ready_ && squid_single(world_, round) == RV_OK) {
            round->dt = dt;
        }
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
    // The scene's round and view start the game: the round's layout, the view's objects and bodies. A scene without the
    // round object is unused and the disc draws the stars only; a refused game start is returned.
    int start_round(const squid_scene_objects &objects)
    {
        squid_round *round = nullptr;
        squid_view *view = nullptr;
        const int holder_code = squid_round_view_of(world_, round, view);
        if (holder_code != RV_OK) {
            RV_LOG_ERR(SQUID_LOG_TAG,
                "the scene needs one round object with squid_round and squid_view: rv_err {}",
                holder_code);
            scene_ready_ = false;
            return RV_OK;
        }
        // The machine logs the cause; without the start layout there is no round to play.
        const int game_code = squid_game_init(round->game);
        if (game_code != RV_OK) {
            return game_code;
        }
        view->objects = objects;
        // The pool logs its refusal; without it no body is shown and the rest of the game runs.
        const int pool_code = squid_view_spawn(world_, rv_pdko_cd(pdk_), view->objects, view->pool);
        squid_round_note(*round, pool_code);
        view->bodies_ready = pool_code == RV_OK;
        return RV_OK;
    }

    // Refusals of init and of the draw steps count in the round when it is there; before it, the module has logged the cause.
    void note(int code)
    {
        if (code == RV_OK) {
            return;
        }
        squid_round *round = nullptr;
        if (squid_single(world_, round) == RV_OK) {
            squid_round_note(*round, code);
        }
    }

    // The HUD's view of the round: the phase and the beer counter.
    static squid_hud_round hud_round(const squid_round &round)
    {
        return squid_hud_round{ round.game.phase, round.game.beer_drunk };
    }

    static squid_hud_debug hud_debug(const squid_round &round)
    {
        return squid_hud_debug{ round.target_tilt,
            round.game.input.smoothed,
            round.sensor,
            squid_census_of(round.game.bodies),
            round.game.round_splits,
            round.game.round_absorbs };
    }

    // The scene's models, drawn by the kit's frame, which also sets the scene's camera. world_frame runs the systems first.
    void draw_scene()
    {
        if (!scene_ready_) {
            return;
        }
        draw_step("scene", world_frame());
    }

    // The HUD needs no camera: the scene's camera is an object of the scene, set by the kit's frame.
    void draw_view()
    {
        squid_round *round = nullptr;
        if (font_ready_ && squid_single(world_, round) == RV_OK) {
            rv_cv *cv = rv_pdko_cv(pdk_);
            draw_step("hud", squid_hud_draw(cv, font_style(), hud_round(*round), hud_debug(*round), round->debug_panel));
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
        RV_LOG_ERR(SQUID_LOG_TAG, "{} not drawn: rv_err {}", step, code);
    }

    rv_pdklib::rv_font_style font_style() const
    {
        return rv_pdklib::rv_font_style_make(addr_font_,
            addr_palette_,
            SQUID_DRAW_TEXT_DEPTH,
            SQUID_DRAW_FONT_SCALE,
            SQUID_DRAW_FONT_BLOCKS);
    }

    // scene_ready_ once the scene is loaded and its round starts the game.
    bool scene_ready_ = false;
    int64_t addr_font_ = 0;
    int64_t addr_palette_ = 0;
    bool font_ready_ = false;
    bool draw_error_logged_ = false;
};

} // namespace squid

RV_MPPC_DISC_ENTRY_DEF(squid::squid_maid);
