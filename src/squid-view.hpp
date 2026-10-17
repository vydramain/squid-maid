#pragma once

// Display of the squid game's bodies: one scene entity per body slot under the tilt root, placed at the body's disc
// position on the dome and rolled by its last step. Display only: physics never reads these results.

#include <cstdint>
#include <iterator>

#include "squid-body.hpp"
#include "squid-dome.hpp"
#include "squid-physics.hpp"
#include "squid-scene.hpp"
#include "pdk/cd/rv_cd.h"
#include "pdk/rv_err.h"
#include "pdklib/rv_ecs/rv_ecs.hpp"
#include "pdklib/rv_logs/rv_logs.hpp"

namespace squid
{

// Log tag of this module's messages.
inline constexpr const char *SQUID_VIEW_TAG = "view";

// Baked model resource names of the balls by size (row, size 1 first) and variant (column): the console resolves a
// model by this name, and the burner bakes each file in assets/models to it.
inline constexpr const char *SQUID_VIEW_MODELS[][SQUID_MODEL_VARIANTS] = {
    { "m1_floppy.mppcmod", "m1_punchcard.mppcmod", "m1_battery.mppcmod", "m1_bulb.mppcmod" },
    { "m2_cassette.mppcmod", "m2_calculator.mppcmod", "m2_tube.mppcmod", "m2_iron.mppcmod" },
    { "m3_crt.mppcmod", "m3_phone.mppcmod", "m3_reel.mppcmod", "m3_vacuum.mppcmod" },
    { "m4_satellite.mppcmod", "m4_sedan.mppcmod", "m4_washer.mppcmod", "m4_microwave.mppcmod" },
};
// One row per ball size: the table must cover every size the radius table has.
static_assert(std::size(SQUID_VIEW_MODELS) == std::size(SQUID_BALL_RADIUS));

// Model file of Alkoldun; also the placeholder every pool entity is loaded with before its first frame.
inline constexpr const char *SQUID_VIEW_ALKOLDUN_MODEL = "alkoldun.mppcmod";

// Model file of a body by its size and variant, or the Alkoldun model. A refused body leaves model untouched.
inline int squid_view_model(const squid_body &body, const char *&model)
{
    if (body.kind == squid_kind::alkoldun) {
        model = SQUID_VIEW_ALKOLDUN_MODEL;
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
    model = SQUID_VIEW_MODELS[size - 1][body.variant];
    return RV_OK;
}

// Rolls the body by its last step (from the position dt ago, to the position now) and writes its model, position and
// rotation to its entity. A refusal keeps the entity's previous pose; its cause is logged where it is found.
inline int squid_view_body_set(rv_pdklib::ecs_world &world, rv_pdklib::ecs_id id, squid_body &body, float dt)
{
    float radius = 0.0f;
    if (squid_body_radius(body, radius) != RV_OK) {
        return RV_ERR_INVAL;
    }
    const char *model = nullptr;
    if (squid_view_model(body, model) != RV_OK) {
        return RV_ERR_INVAL;
    }
    const rv_pdklib::rv_vec3 from = squid_dome_offset(body.pos.x - body.vel.x * dt, body.pos.z - body.vel.z * dt, radius);
    const rv_pdklib::rv_vec3 to = squid_dome_offset(body.pos.x, body.pos.z, radius);
    const int roll_code = squid_dome_roll(body.rot, from, to, radius);
    if (roll_code != RV_OK) {
        return roll_code;
    }
    rv_pdklib::fx_vec3 rotation{};
    const int euler_code = squid_dome_euler(body.rot, rotation);
    if (euler_code != RV_OK) {
        return euler_code;
    }
    rv_pdklib::fx_vec3 position{};
    const int point_code = squid_scene_fx_point(to, position);
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

// Loads one body entity per slot under the tilt root. A refused load unloads the slots already made, so no body is
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

// Shows slot i as body i with its pose and hides the slots past the count. Every slot is visited; the first refusal
// is returned.
inline int squid_view_update(rv_pdklib::ecs_world &world, const squid_view_pool &pool, squid_bodies &bodies, float dt)
{
    int first = RV_OK;
    for (int i = 0; i < SQUID_BODY_CAPACITY; ++i) {
        const squid_view_slot &slot = pool.slots[i];
        const bool shown = i < bodies.count;
        const int hidden_code = squid_scene_body_hidden(world, slot.id, !shown);
        const int pose_code = shown ? squid_view_body_set(world, slot.id, bodies.items[i], dt) : RV_OK;
        const int code = hidden_code != RV_OK ? hidden_code : pose_code;
        if (code != RV_OK && first == RV_OK) {
            first = code;
        }
    }
    return first;
}

} // namespace squid
