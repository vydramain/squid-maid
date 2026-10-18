#pragma once

// Display of the squid game's bodies: one scene entity per body slot under pose_yaw, placed at the body's disc
// position on the dome, lifted by its hover, and turned by the display rotation the physics step keeps. Display only:
// the view reads the body and never writes it.

#include <algorithm>
#include <array>
#include <cstdint>
#include <iterator>

#include "squid-body.hpp"
#include "squid-descent.hpp"
#include "squid-dome.hpp"
#include "squid-physics.hpp"
#include "squid-scene.hpp"
#include "squid-state.hpp"
#include "pdk/cd/rv_cd.h"
#include "pdk/rv_err.h"
#include "pdklib/rv_ecs/rv_ecs.hpp"
#include "pdklib/rv_logs/rv_logs.hpp"

namespace squid
{

// Log tag of this module's messages.
inline constexpr const char *SQUID_VIEW_TAG = "view";

// Baked model resource names of the balls by size (row, size 1 first) and variant (column): the console resolves a
// model by this name, and the burner bakes each file in assets/models to it. Columns past a size's count are unused.
inline constexpr const char *SQUID_VIEW_MODELS[][SQUID_MODEL_VARIANTS] = {
    { "m1_floppy.mppcmod", "m1_punchcard.mppcmod", "m1_battery.mppcmod", "m1_bulb.mppcmod", nullptr },
    { "m2_cassette.mppcmod", "m2_calculator.mppcmod", "m2_tube.mppcmod", "m2_iron.mppcmod", nullptr },
    { "m3_crt.mppcmod", "m3_phone.mppcmod", "m3_reel.mppcmod", "m3_vacuum.mppcmod", nullptr },
    { "m4_satellite.mppcmod", "m4_sedan.mppcmod", "m4_washer.mppcmod", "m4_microwave.mppcmod", "m4_fridge.mppcmod" },
};
// One row per ball size: the table must cover every size the radius table has.
static_assert(std::size(SQUID_VIEW_MODELS) == std::size(SQUID_BALL_RADIUS));

// Models per size (size 1 first). A body's variant is taken modulo its size's count, so a split half that inherits a
// variant past a smaller size's count still gets a model.
inline constexpr int SQUID_VIEW_SIZE_VARIANTS[] = { 4, 4, 4, 5 };
static_assert(std::size(SQUID_VIEW_SIZE_VARIANTS) == std::size(SQUID_BALL_RADIUS));
static_assert(std::ranges::max(SQUID_VIEW_SIZE_VARIANTS) == SQUID_MODEL_VARIANTS);

// Every size's first SQUID_VIEW_SIZE_VARIANTS names are set; a nullptr there would reach the console as a model.
constexpr bool squid_view_table_complete()
{
    for (std::size_t size = 0; size < std::size(SQUID_VIEW_MODELS); ++size) {
        for (int variant = 0; variant < SQUID_VIEW_SIZE_VARIANTS[size]; ++variant) {
            if (SQUID_VIEW_MODELS[size][variant] == nullptr) {
                return false;
            }
        }
    }
    return true;
}
static_assert(squid_view_table_complete(), "every size's model names must be set up to its variant count");

// Model file of Alkoldun; also the placeholder every pool entity is loaded with before its first frame.
inline constexpr const char *SQUID_VIEW_ALKOLDUN_MODEL = "alkoldun.mppcmod";

// Model file of a bottle; like the balls, it is made for 4/3 of its collision radius.
inline constexpr const char *SQUID_VIEW_BOTTLE_MODEL = "bottle.mppcmod";

// Fixed model by kind, indexed by squid_kind; nullptr means the ball table decides.
inline constexpr const char *SQUID_VIEW_KIND_MODELS[] = { nullptr, SQUID_VIEW_ALKOLDUN_MODEL, SQUID_VIEW_BOTTLE_MODEL };
static_assert(std::size(SQUID_VIEW_KIND_MODELS) == static_cast<std::size_t>(squid_kind::bottle) + 1);

// Alkoldun with a flame under him, the same figure as the plain model; the flight shows him with it.
inline constexpr const char *SQUID_VIEW_FLAME_MODEL = "alkoldun_flame.mppcmod";

// Alkoldun's flight by phase: the model he is drawn with while the flight draws him at its progress. The phases not
// listed keep him at his physics pose. The landed row is the plain model at the plate centre, the flight's end.
struct squid_view_flight_row {
    squid_phase phase;
    const char *model;
};
constexpr auto SQUID_VIEW_FLIGHT_ROWS = std::to_array<squid_view_flight_row>({
    { squid_phase::descent, SQUID_VIEW_FLAME_MODEL },
    { squid_phase::descent_pause, SQUID_VIEW_FLAME_MODEL },
    { squid_phase::landed, SQUID_VIEW_ALKOLDUN_MODEL },
});

// The flight row of a phase, or nullptr when Alkoldun keeps his physics pose in it.
inline const squid_view_flight_row *squid_view_flight_row_of(squid_phase phase)
{
    const auto it =
        std::find_if(SQUID_VIEW_FLIGHT_ROWS.begin(), SQUID_VIEW_FLIGHT_ROWS.end(), [phase](const squid_view_flight_row &row) {
            return row.phase == phase;
        });
    return it == SQUID_VIEW_FLIGHT_ROWS.end() ? nullptr : &*it;
}

// Model file of a body by its kind, or by its size and variant for a ball. A refused body leaves model untouched.
inline int squid_view_model(const squid_body &body, const char *&model)
{
    const std::size_t kind = static_cast<std::size_t>(body.kind);
    if (kind >= std::size(SQUID_VIEW_KIND_MODELS)) {
        RV_LOG_ERR(SQUID_VIEW_TAG, "body model refused: kind {} is unknown", kind);
        return RV_ERR_INVAL;
    }
    if (SQUID_VIEW_KIND_MODELS[kind] != nullptr) {
        model = SQUID_VIEW_KIND_MODELS[kind];
        return RV_OK;
    }
    int size = 0;
    if (squid_body_size(body, size) != RV_OK) {
        return RV_ERR_INVAL;
    }
    const bool variant_bad = body.variant < 0 || body.variant >= SQUID_MODEL_VARIANTS;
    // Logged every frame for this slot while refused; not reachable in normal play: a variant is set at birth within
    // 0..SQUID_MODEL_VARIANTS-1 (the start layout's generator; a split half takes variant + 1 modulo that count), and
    // squid_body_birth refuses anything else.
    if (variant_bad) {
        RV_LOG_ERR(SQUID_VIEW_TAG, "body model refused: variant {} is outside 0..{}", body.variant, SQUID_MODEL_VARIANTS - 1);
        return RV_ERR_INVAL;
    }
    model = SQUID_VIEW_MODELS[size - 1][body.variant % SQUID_VIEW_SIZE_VARIANTS[size - 1]];
    return RV_OK;
}

// Writes the body's model, position (its dome centre, lifted by its hover) and rotation to its entity. A refusal keeps
// the entity's previous pose and model; its cause is logged where it is found.
inline int squid_view_body_set(rv_pdklib::ecs_world &world, rv_pdklib::ecs_id id, const squid_body &body)
{
    float radius = 0.0f;
    if (squid_body_radius(body, radius) != RV_OK) {
        return RV_ERR_INVAL;
    }
    const char *model = nullptr;
    if (squid_view_model(body, model) != RV_OK) {
        return RV_ERR_INVAL;
    }
    const float lift = squid_dome_hover_lift(body.hover_phase);
    const rv_pdklib::rv_vec3 centre = squid_dome_offset(body.pos.x, body.pos.z, radius, lift);
    rv_pdklib::fx_vec3 rotation{};
    const int euler_code = squid_dome_euler(body.rot, rotation);
    if (euler_code != RV_OK) {
        return euler_code;
    }
    rv_pdklib::fx_vec3 position{};
    const int point_code = squid_scene_fx_point(centre, position);
    if (point_code != RV_OK) {
        return point_code;
    }
    return squid_scene_body_set(world, id, model, position, rotation);
}

// A pool entity and the load that made it; the load is what unloads it.
struct squid_view_slot {
    rv_pdklib::ecs_id id{};
    uint32_t load = 0;
};

// One entity per body slot: slot i shows body i. spawned counts the slots loaded, from the front.
struct squid_view_pool {
    squid_view_slot slots[SQUID_BODY_CAPACITY]{};
    int spawned = 0;
};

// Unloads the loaded slots and forgets them; an unload that fails is logged by the unload.
inline void squid_view_pool_unload(rv_pdklib::ecs_world &world, squid_view_pool &pool)
{
    for (int i = 0; i < pool.spawned; ++i) {
        (void)squid_scene_body_unload(world, pool.slots[i].load);
    }
    pool.spawned = 0;
}

// Loads one body entity per slot under pose_yaw. A refused load unloads the slots already made, so no body is
// shown, and returns the refusal.
inline int squid_view_spawn(rv_pdklib::ecs_world &world, rv_cd *cd, const squid_scene_objects &objects, squid_view_pool &pool)
{
    for (int i = 0; i < SQUID_BODY_CAPACITY; ++i) {
        squid_view_slot &slot = pool.slots[i];
        const int code = squid_scene_body_spawn(world, cd, objects, SQUID_VIEW_ALKOLDUN_MODEL, slot.load, slot.id);
        if (code != RV_OK) {
            RV_LOG_ERR(SQUID_VIEW_TAG,
                "body pool refused at slot {} of {}: rv_err {}; no body is shown",
                i,
                SQUID_BODY_CAPACITY,
                code);
            squid_view_pool_unload(world, pool);
            return code;
        }
        pool.spawned = i + 1;
    }
    return RV_OK;
}

// Writes Alkoldun's pose on the flight: the arc point at the descent's progress, upright so his feet point at the plate.
inline int squid_view_flight_set(rv_pdklib::ecs_world &world, rv_pdklib::ecs_id id, const char *model, const squid_descent &d)
{
    const rv_pdklib::rv_vec3 point = squid_descent_point(d, squid_descent_end());
    rv_pdklib::fx_vec3 position{};
    const int point_code = squid_scene_fx_point(point, position);
    if (point_code != RV_OK) {
        return point_code;
    }
    // A zero Euler angle is the identity: no tilt, no spin.
    return squid_scene_body_set(world, id, model, position, rv_pdklib::fx_vec3{});
}

// Alkoldun in a flight phase is drawn on the arc; every other body keeps its physics pose.
inline int squid_view_pose(rv_pdklib::ecs_world &world,
    rv_pdklib::ecs_id id,
    const squid_body &body,
    squid_phase phase,
    const squid_descent &d)
{
    const squid_view_flight_row *flight = squid_view_flight_row_of(phase);
    if (flight == nullptr || body.kind != squid_kind::alkoldun) {
        return squid_view_body_set(world, id, body);
    }
    return squid_view_flight_set(world, id, flight->model, d);
}

// Shows slot i as body i with its pose and hides the slots past the count. Every slot is visited; the first refusal
// is returned. The phase and descent place Alkoldun on his flight.
inline int squid_view_update(rv_pdklib::ecs_world &world,
    const squid_view_pool &pool,
    const squid_bodies &bodies,
    squid_phase phase,
    const squid_descent &d)
{
    int first = RV_OK;
    for (int i = 0; i < SQUID_BODY_CAPACITY; ++i) {
        const squid_view_slot &slot = pool.slots[i];
        const bool shown = i < bodies.count;
        const int hidden_code = squid_scene_body_hidden(world, slot.id, !shown);
        const int pose_code = shown ? squid_view_pose(world, slot.id, bodies.items[i], phase, d) : RV_OK;
        const int code = hidden_code != RV_OK ? hidden_code : pose_code;
        if (code != RV_OK && first == RV_OK) {
            first = code;
        }
    }
    return first;
}

} // namespace squid
