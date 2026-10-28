#pragma once

// ECS (entity-component-system) components of the squid game: working state of the round and the frame, not scene
// data. They sit on the scene's round object; the systems reach them through squid_single.

#include <array>
#include <cstddef>
#include <cstdint>

#include "pdk/rv_err.h"
#include "pdkkit/component/rv_component_macro.hpp"
#include "pdkkit/component/rv_component_register.hpp"
#include "pdkkit/ecs/rv_ecs.hpp"
#include "pdkkit/ecs/rv_ecs_store.hpp"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "pdklib/rv_math/rv_fx.hpp"
#include "squid-input.hpp"
#include "squid-scene.hpp"
#include "squid-sound.hpp"
#include "squid-state.hpp"
#include "squid-texts.hpp"
#include "squid-view.hpp"

namespace squid
{

// Events of one frame that may carry a sound: a calibrate-type press, the goal's bell, a marked drink, a fridge split, a
// split, a glass hit and a bounce.
enum class squid_frame_event : int {
    calibrate = 0,
    bell = 1,
    cheers = 2,
    can_glass = 3,
    split = 4,
    glass_hit = 5,
    bounce = 6,
    count,
};
// Count of the enum above; the flags are indexed by it.
constexpr std::size_t SQUID_FRAME_EVENT_COUNT = static_cast<std::size_t>(squid_frame_event::count);
using squid_frame_flags = std::array<bool, SQUID_FRAME_EVENT_COUNT>;

// Working state of the round and the frame. The disc writes dt in frame_update, before the systems run.
struct squid_round {
    squid_game game{};
    float dt = 0;
    // Port 0 has a gyroscope.
    bool sensor = false;
    // L4 toggles it; closed at start.
    bool debug_panel = false;
    // Set by the language pick: (A) Russian, (B) English.
    squid_lang lang = squid_lang::ru;
    // Input tilt target, shown by the panel.
    squid_tilt target_tilt{};
    // Phase before this frame's press.
    squid_phase phase_before{};
    bool stepped = false;
    bool descended = false;
    // Events of this frame that carry a sound.
    squid_frame_flags happened{};
    // Number of the systems' frame; the state line is logged every period.
    int64_t frame = 0;
    int refusals = 0;
    int last_refusal = RV_OK;
    // Sounds since the last state line.
    int sounds_started = 0;
};

// The view of the round. rotation is held when a turn is refused, so the tilt root does not jump.
struct squid_view {
    squid_scene_objects objects{};
    squid_view_pool pool{};
    rv_pdklib::fx_vec3 rotation{};
    bool bodies_ready = false; // the pool is loaded whole
};

struct squid_effects {
    squid_sound sound{};
};

} // namespace squid

PDKKIT_COMPONENT(squid::squid_round, "squid_round");
PDKKIT_COMPONENT(squid::squid_view, "squid_view");
PDKKIT_COMPONENT(squid::squid_effects, "squid_effects");

namespace squid
{

// Log tag of the component registration's messages.
constexpr const char *SQUID_COMPONENTS_TAG = "components";

// Registers T; a refusal is logged with the component's name and the code, which is returned.
template <typename T>
int squid_component_register(rv_pdkkit::ecs_world &world)
{
    const int code = rv_pdkkit::ecs_component_register<T>(world);
    if (code != RV_OK) {
        RV_LOG_ERR(SQUID_COMPONENTS_TAG, "component {} refused: code {}", rv_pdkkit::ecs_component_traits<T>::desc.name, code);
        return code;
    }
    return RV_OK;
}

// Registers the round, view and effects components, in this order.
inline int squid_components_register(rv_pdkkit::ecs_world &world)
{
    const int round_code = squid_component_register<squid_round>(world);
    if (round_code != RV_OK) {
        return round_code;
    }
    const int view_code = squid_component_register<squid_view>(world);
    if (view_code != RV_OK) {
        return view_code;
    }
    const int effects_code = squid_component_register<squid_effects>(world);
    if (effects_code != RV_OK) {
        return effects_code;
    }
    return RV_OK;
}

// The one holder of T: RV_ERR_NOENT with none, RV_ERR_ALREADY with two or more; out is set only on RV_OK. Silent, since
// systems call it every frame: ecs_frame_run logs the system's refusal once per run of the same failure.
template <typename T>
int squid_single(rv_pdkkit::ecs_world &world, T *&out)
{
    T *found = nullptr;
    int count = 0;
    rv_pdkkit::ecs_component_each<T>(world, [&](rv_pdkkit::ecs_id, T &component) {
        found = &component;
        ++count;
    });
    if (count == 0) {
        return RV_ERR_NOENT;
    }
    if (count > 1) {
        return RV_ERR_ALREADY;
    }
    out = found;
    return RV_OK;
}

// Counts a failed step for the round's state line; returns code so the caller can pass it on.
inline int squid_round_note(squid_round &round, int code)
{
    if (code == RV_OK) {
        return code;
    }
    ++round.refusals;
    round.last_refusal = code;
    return code;
}

} // namespace squid
