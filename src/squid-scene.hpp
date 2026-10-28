#pragma once

// The scene of the squid game in the ECS: the tilt root at the dome centre, the pose group the structure and the balls
// hang under, and the ball body template that bodies are loaded from. The disc writes the tilt each frame; the ECS
// transform turns it into world matrices.

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include "squid-input.hpp"
#include "squid-params.hpp"
#include "pdk/cd/rv_cd.h"
#include "pdk/rv_err.h"
#include "pdkkit/ecs/rv_ecs.hpp"
#include "pdkkit/ecs/rv_ecs_component.hpp"
#include "pdkkit/ecs/rv_ecs_frame.hpp"
#include "pdkkit/ecs/rv_ecs_loads.hpp"
#include "pdkkit/ecs/rv_ecs_scene_records.hpp"
#include "pdkkit/ecs/rv_ecs_transform.hpp"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "pdklib/rv_math/rv_fx.hpp"

namespace squid
{

// Log tag of scene messages.
inline constexpr const char *SQUID_SCENE_TAG = "scene";

// The scene file, the ball body template, and the names of the objects the disc moves or reports; the objects are
// found by name.
inline constexpr const char *SQUID_SCENE_FILE = "main.scene.toml";
inline constexpr const char *SQUID_SCENE_BODY_FILE = "body.scene.toml";
inline constexpr const char *SQUID_SCENE_TILT_NAME = "tilt";
// The pose group the balls load under: it carries the structure's rotation but not its scale.
inline constexpr const char *SQUID_SCENE_POSE_YAW_NAME = "pose_yaw";
// The pose group above pose_yaw: it must not rotate, so the yaw is the only turn under the tilt root.
inline constexpr const char *SQUID_SCENE_POSE_PITCH_NAME = "pose_pitch";
// The pose group that scales the structure, and the Earth plate it places; both are checked on load.
inline constexpr const char *SQUID_SCENE_POSE_SCALE_NAME = "pose_scale";
inline constexpr const char *SQUID_SCENE_EARTH_NAME = "earth";
// The lens object: the stage flies it from the far pose to its scene pose.
inline constexpr const char *SQUID_SCENE_CAMERA_NAME = "camera";

// Entities of the scene objects the disc moves or reports, and of the group the balls load under.
struct squid_scene_objects {
    rv_pdkkit::ecs_id tilt{};
    rv_pdkkit::ecs_id pose_yaw{};
    rv_pdkkit::ecs_id pose_scale{};
    rv_pdkkit::ecs_id camera{};
    // The camera's pose in the scene file: the round's view, where the start flies to.
    rv_pdklib::fx_vec3 camera_position{};
    rv_pdklib::fx_vec3 camera_rotation{};
};

// Entity of the object with the name in the load. A missing object is an error; the name and the code are in the log.
inline int squid_scene_find(rv_pdkkit::ecs_world &world, uint32_t load_number, const char *name, rv_pdkkit::ecs_id &out)
{
    rv_pdkkit::ecs_id found{};
    // RV_ERR_ALREADY stops the walk at the match; it is mapped to RV_OK below.
    const int walked = rv_pdkkit::ecs_scene_entities(world, load_number, [&](rv_pdkkit::ecs_id id) -> int {
        const auto *name_component = rv_pdkkit::ecs_component_get<rv_pdkkit::ecs_component_name>(world, id);
        const bool match = name_component != nullptr && std::string_view{ name_component->text } == name;
        if (!match) {
            return RV_OK;
        }
        found = id;
        return RV_ERR_ALREADY;
    });
    if (walked == RV_ERR_ALREADY) {
        out = found;
        return RV_OK;
    }
    if (walked != RV_OK) {
        RV_LOG_ERR(SQUID_SCENE_TAG, "'{}' lookup failed: rv_err {}", name, walked);
        return walked;
    }
    RV_LOG_ERR(SQUID_SCENE_TAG, "no '{}' object in the scene", name);
    return RV_ERR_NOENT;
}

// Rounds a world point to Q20.12; out is written only when all three coordinates fit.
inline int squid_scene_fx_point(rv_pdklib::rv_vec3 point, rv_pdklib::fx_vec3 &out)
{
    rv_pdklib::fx_vec3 fixed{};
    const int x_code = rv_pdklib::fx_from_double(point.x, fixed.x);
    if (x_code != RV_OK) {
        RV_LOG_ERR(SQUID_SCENE_TAG, "x {} is outside the Q20.12 range; position not set", point.x);
        return x_code;
    }
    const int y_code = rv_pdklib::fx_from_double(point.y, fixed.y);
    if (y_code != RV_OK) {
        RV_LOG_ERR(SQUID_SCENE_TAG, "y {} is outside the Q20.12 range; position not set", point.y);
        return y_code;
    }
    const int z_code = rv_pdklib::fx_from_double(point.z, fixed.z);
    if (z_code != RV_OK) {
        RV_LOG_ERR(SQUID_SCENE_TAG, "z {} is outside the Q20.12 range; position not set", point.z);
        return z_code;
    }
    out = fixed;
    return RV_OK;
}

// Game tilt angles in degrees to the tilt root's Q20.12 rotation (x, 0, z), scaled by SQUID_VISIBLE_TILT_SHARE: the
// world on screen tilts by a share of the game tilt. The physics keeps the full game tilt; rotation is untouched on error.
inline int squid_scene_tilt(float tilt_x_deg, float tilt_z_deg, rv_pdklib::fx_vec3 &rotation)
{
    const float shown_x_deg = tilt_x_deg * SQUID_VISIBLE_TILT_SHARE;
    const float shown_z_deg = tilt_z_deg * SQUID_VISIBLE_TILT_SHARE;
    rv_pdklib::fx tilt_x{};
    rv_pdklib::fx tilt_z{};
    const int x_code = rv_pdklib::fx_from_double(shown_x_deg, tilt_x);
    if (x_code != RV_OK) {
        RV_LOG_ERR(SQUID_SCENE_TAG, "shown tilt x {} deg is outside the Q20.12 range; rotation kept", shown_x_deg);
        return x_code;
    }
    const int z_code = rv_pdklib::fx_from_double(shown_z_deg, tilt_z);
    if (z_code != RV_OK) {
        RV_LOG_ERR(SQUID_SCENE_TAG, "shown tilt z {} deg is outside the Q20.12 range; rotation kept", shown_z_deg);
        return z_code;
    }
    rotation = rv_pdklib::fx_vec3{ tilt_x, 0, tilt_z };
    return RV_OK;
}

// Finds the tilt root and checks it: at the dome centre with scale 1 and no parent. Any refusal is returned with its
// cause logged.
inline int squid_scene_check(rv_pdkkit::ecs_world &world, uint32_t load_number, squid_scene_objects &objects)
{
    const int tilt_code = squid_scene_find(world, load_number, SQUID_SCENE_TILT_NAME, objects.tilt);
    if (tilt_code != RV_OK) {
        return tilt_code;
    }
    const rv_pdkkit::ecs_component_transform *tilt =
        rv_pdkkit::ecs_component_get<rv_pdkkit::ecs_component_transform>(world, objects.tilt);
    if (tilt == nullptr) {
        RV_LOG_ERR(SQUID_SCENE_TAG, "'{}' object has no transform", SQUID_SCENE_TILT_NAME);
        return RV_ERR_NOENT;
    }
    rv_pdklib::fx_vec3 centre{};
    const int centre_code =
        squid_scene_fx_point(rv_pdklib::rv_vec3{ SQUID_DOME_CENTER_X, SQUID_DOME_CENTER_Y, SQUID_DOME_CENTER_Z }, centre);
    if (centre_code != RV_OK) {
        return centre_code;
    }
    const bool at_centre = tilt->position.x == centre.x && tilt->position.y == centre.y && tilt->position.z == centre.z;
    const bool unit_scale =
        tilt->scale.x == rv_pdklib::fx_one && tilt->scale.y == rv_pdklib::fx_one && tilt->scale.z == rv_pdklib::fx_one;
    const rv_pdkkit::ecs_component_parent *link =
        rv_pdkkit::ecs_component_get<rv_pdkkit::ecs_component_parent>(world, objects.tilt);
    const bool rootless = link == nullptr || link->parent == rv_pdkkit::ecs_id{};
    if (!at_centre || !unit_scale || !rootless) {
        RV_LOG_ERR(SQUID_SCENE_TAG, "scene refused: the tilt root must sit at the dome centre with scale 1 and no parent");
        return RV_ERR_INVAL;
    }
    return RV_OK;
}

// Finds the pose_scale group and the Earth and checks them against the dome: the group scales by SQUID_STRUCTURE_SCALE,
// and the Earth sits at minus the dome centre, which is where squid_descent_plate_top takes the plate from. Any refusal
// is returned with its cause logged.
inline int squid_scene_check_structure(rv_pdkkit::ecs_world &world, uint32_t load_number)
{
    rv_pdkkit::ecs_id scale_id{};
    const int scale_code = squid_scene_find(world, load_number, SQUID_SCENE_POSE_SCALE_NAME, scale_id);
    if (scale_code != RV_OK) {
        return scale_code;
    }
    rv_pdkkit::ecs_id earth_id{};
    const int earth_code = squid_scene_find(world, load_number, SQUID_SCENE_EARTH_NAME, earth_id);
    if (earth_code != RV_OK) {
        return earth_code;
    }
    const rv_pdkkit::ecs_component_transform *scale_group =
        rv_pdkkit::ecs_component_get<rv_pdkkit::ecs_component_transform>(world, scale_id);
    const rv_pdkkit::ecs_component_transform *earth =
        rv_pdkkit::ecs_component_get<rv_pdkkit::ecs_component_transform>(world, earth_id);
    if (scale_group == nullptr || earth == nullptr) {
        RV_LOG_ERR(SQUID_SCENE_TAG,
            "'{}' or '{}' object has no transform",
            SQUID_SCENE_POSE_SCALE_NAME,
            SQUID_SCENE_EARTH_NAME);
        return RV_ERR_NOENT;
    }
    const float s = SQUID_STRUCTURE_SCALE;
    rv_pdklib::fx_vec3 want_scale{};
    const int want_scale_code = squid_scene_fx_point(rv_pdklib::rv_vec3{ s, s, s }, want_scale);
    if (want_scale_code != RV_OK) {
        return want_scale_code;
    }
    rv_pdklib::fx_vec3 want_earth{};
    const int want_earth_code =
        squid_scene_fx_point(rv_pdklib::rv_vec3{ -SQUID_DOME_CENTER_X, -SQUID_DOME_CENTER_Y, -SQUID_DOME_CENTER_Z },
            want_earth);
    if (want_earth_code != RV_OK) {
        return want_earth_code;
    }
    const bool scaled =
        scale_group->scale.x == want_scale.x && scale_group->scale.y == want_scale.y && scale_group->scale.z == want_scale.z;
    const bool placed =
        earth->position.x == want_earth.x && earth->position.y == want_earth.y && earth->position.z == want_earth.z;
    if (!scaled || !placed) {
        RV_LOG_ERR(SQUID_SCENE_TAG,
            "scene refused: pose_scale must scale the structure by {} and the Earth must sit at minus the dome centre",
            SQUID_STRUCTURE_SCALE);
        return RV_ERR_INVAL;
    }
    rv_pdkkit::ecs_id pitch_id{};
    const int pitch_code = squid_scene_find(world, load_number, SQUID_SCENE_POSE_PITCH_NAME, pitch_id);
    if (pitch_code != RV_OK) {
        return pitch_code;
    }
    rv_pdkkit::ecs_id yaw_id{};
    const int yaw_code = squid_scene_find(world, load_number, SQUID_SCENE_POSE_YAW_NAME, yaw_id);
    if (yaw_code != RV_OK) {
        return yaw_code;
    }
    const rv_pdkkit::ecs_component_transform *pitch =
        rv_pdkkit::ecs_component_get<rv_pdkkit::ecs_component_transform>(world, pitch_id);
    const rv_pdkkit::ecs_component_transform *yaw =
        rv_pdkkit::ecs_component_get<rv_pdkkit::ecs_component_transform>(world, yaw_id);
    if (pitch == nullptr || yaw == nullptr) {
        RV_LOG_ERR(SQUID_SCENE_TAG,
            "'{}' or '{}' object has no transform",
            SQUID_SCENE_POSE_PITCH_NAME,
            SQUID_SCENE_POSE_YAW_NAME);
        return RV_ERR_NOENT;
    }
    const rv_pdklib::fx_vec3 level{};
    rv_pdklib::fx_vec3 want_turn{};
    const int want_turn_code = squid_scene_fx_point(rv_pdklib::rv_vec3{ 0, SQUID_POSE_YAW_DEG, 0 }, want_turn);
    if (want_turn_code != RV_OK) {
        return want_turn_code;
    }
    const bool pitch_level = pitch->rotation.x == level.x && pitch->rotation.y == level.y && pitch->rotation.z == level.z;
    const bool yaw_turned = yaw->rotation.x == want_turn.x && yaw->rotation.y == want_turn.y && yaw->rotation.z == want_turn.z;
    if (!pitch_level || !yaw_turned) {
        RV_LOG_ERR(SQUID_SCENE_TAG,
            "scene refused: '{}' must not rotate and '{}' must turn {} deg about y; the physics assumes it",
            SQUID_SCENE_POSE_PITCH_NAME,
            SQUID_SCENE_POSE_YAW_NAME,
            SQUID_POSE_YAW_DEG);
        return RV_ERR_INVAL;
    }
    return RV_OK;
}

// Unloads a scene refused after its load, so no half scene stays in the world. A failed unload is logged; the
// refusal that caused it is the one the caller reports.
inline void squid_scene_discard(rv_pdkkit::ecs_world &world, uint32_t load_number)
{
    const int unload_code = rv_pdkkit::ecs_scene_unload_now(world, load_number);
    if (unload_code != RV_OK) {
        RV_LOG_WARN(SQUID_SCENE_TAG, "refused scene not unloaded: rv_err {}", unload_code);
    }
}

// Loads the scene and finds its objects. A refusal leaves the scene unused.
inline int squid_scene_load(rv_pdkkit::ecs_world &world, rv_cd *cd, squid_scene_objects &objects)
{
    uint32_t load_number = 0;
    const int load_code = rv_pdkkit::ecs_scene_load(world, cd, SQUID_SCENE_FILE, rv_pdkkit::ecs_id{}, load_number);
    if (load_code != RV_OK) {
        RV_LOG_ERR(SQUID_SCENE_TAG, "{} not loaded: rv_err {}", SQUID_SCENE_FILE, load_code);
        return load_code;
    }
    const int check_code = squid_scene_check(world, load_number, objects);
    if (check_code != RV_OK) {
        squid_scene_discard(world, load_number);
        return check_code;
    }
    const int structure_code = squid_scene_check_structure(world, load_number);
    if (structure_code != RV_OK) {
        squid_scene_discard(world, load_number);
        return structure_code;
    }
    const int pose_code = squid_scene_find(world, load_number, SQUID_SCENE_POSE_YAW_NAME, objects.pose_yaw);
    if (pose_code != RV_OK) {
        squid_scene_discard(world, load_number);
        return pose_code;
    }
    const int scale_code = squid_scene_find(world, load_number, SQUID_SCENE_POSE_SCALE_NAME, objects.pose_scale);
    if (scale_code != RV_OK) {
        squid_scene_discard(world, load_number);
        return scale_code;
    }
    const int camera_code = squid_scene_find(world, load_number, SQUID_SCENE_CAMERA_NAME, objects.camera);
    if (camera_code != RV_OK) {
        squid_scene_discard(world, load_number);
        return camera_code;
    }
    const rv_pdkkit::ecs_component_transform *camera =
        rv_pdkkit::ecs_component_get<rv_pdkkit::ecs_component_transform>(world, objects.camera);
    if (camera == nullptr) {
        RV_LOG_ERR(SQUID_SCENE_TAG, "'{}' object has no transform", SQUID_SCENE_CAMERA_NAME);
        squid_scene_discard(world, load_number);
        return RV_ERR_NOENT;
    }
    objects.camera_position = camera->position;
    objects.camera_rotation = camera->rotation;
    return RV_OK;
}

// Sets the model name of a body entity; the name is the model the disc packs (assets/models).
inline int squid_scene_body_model(rv_pdkkit::ecs_world &world, rv_pdkkit::ecs_id id, const char *model)
{
    if (model == nullptr) {
        RV_LOG_ERR(SQUID_SCENE_TAG, "body model is null; model not set");
        return RV_ERR_INVAL;
    }
    rv_pdkkit::ecs_component_model *component = rv_pdkkit::ecs_component_get<rv_pdkkit::ecs_component_model>(world, id);
    if (component == nullptr) {
        RV_LOG_ERR(SQUID_SCENE_TAG, "body has no model; model '{}' not set", model);
        return RV_ERR_NOENT;
    }
    const std::string text{ model };
    const int copy_code = rv_pdkkit::ecs_scene_copy_text(text, component->name, sizeof component->name);
    if (copy_code != RV_OK) {
        RV_LOG_ERR(SQUID_SCENE_TAG, "model '{}' does not fit the model name or holds a NUL: rv_err {}", model, copy_code);
        return copy_code;
    }
    return RV_OK;
}

// Loads one ball body under pose_yaw and sets its model. On success load_number is the load to unload later and
// id is the body's entity; on error neither is written and no part of the body stays in the world.
inline int squid_scene_body_spawn(rv_pdkkit::ecs_world &world,
    rv_cd *cd,
    const squid_scene_objects &objects,
    const char *model,
    uint32_t &load_number,
    rv_pdkkit::ecs_id &id)
{
    uint32_t number = 0;
    const int load_code = rv_pdkkit::ecs_scene_load(world, cd, SQUID_SCENE_BODY_FILE, objects.pose_yaw, number);
    if (load_code != RV_OK) {
        RV_LOG_ERR(SQUID_SCENE_TAG, "{} not loaded: rv_err {}", SQUID_SCENE_BODY_FILE, load_code);
        return load_code;
    }
    rv_pdkkit::ecs_id body{};
    std::size_t count = 0;
    const int walk_code = rv_pdkkit::ecs_scene_entities(world, number, [&](rv_pdkkit::ecs_id entity) -> int {
        body = entity;
        ++count;
        return RV_OK;
    });
    if (walk_code != RV_OK) {
        RV_LOG_ERR(SQUID_SCENE_TAG, "{} objects not listed: rv_err {}", SQUID_SCENE_BODY_FILE, walk_code);
        squid_scene_discard(world, number);
        return walk_code;
    }
    if (count != 1) {
        RV_LOG_ERR(SQUID_SCENE_TAG, "{} must hold one object, holds {}", SQUID_SCENE_BODY_FILE, count);
        squid_scene_discard(world, number);
        return RV_ERR_INVAL;
    }
    const int model_code = squid_scene_body_model(world, body, model);
    if (model_code != RV_OK) {
        squid_scene_discard(world, number);
        return model_code;
    }
    load_number = number;
    id = body;
    return RV_OK;
}

// Places a body under pose_yaw: model name, position in pose_yaw's space (Q20.12) and Euler rotation.
inline int squid_scene_body_set(rv_pdkkit::ecs_world &world,
    rv_pdkkit::ecs_id id,
    const char *model,
    rv_pdklib::fx_vec3 position,
    rv_pdklib::fx_vec3 rotation)
{
    rv_pdkkit::ecs_component_transform *transform = rv_pdkkit::ecs_component_get<rv_pdkkit::ecs_component_transform>(world, id);
    if (transform == nullptr) {
        RV_LOG_ERR(SQUID_SCENE_TAG, "body has no transform; pose not set");
        return RV_ERR_NOENT;
    }
    const int model_code = squid_scene_body_model(world, id, model);
    if (model_code != RV_OK) {
        return model_code;
    }
    transform->position = position;
    transform->rotation = rotation;
    return RV_OK;
}

// Scales a body entity uniformly; the stage grows the bodies from a point at the start.
inline int squid_scene_body_scale(rv_pdkkit::ecs_world &world, rv_pdkkit::ecs_id id, rv_pdklib::fx scale)
{
    rv_pdkkit::ecs_component_transform *transform = rv_pdkkit::ecs_component_get<rv_pdkkit::ecs_component_transform>(world, id);
    if (transform == nullptr) {
        RV_LOG_ERR(SQUID_SCENE_TAG, "body has no transform; scale not set");
        return RV_ERR_NOENT;
    }
    transform->scale = rv_pdklib::fx_vec3{ scale, scale, scale };
    return RV_OK;
}

// Unloads a body by its load number. In a frame the unload waits for the frame's end.
inline int squid_scene_body_unload(rv_pdkkit::ecs_world &world, uint32_t load_number)
{
    const int unload_code = rv_pdkkit::ecs_scene_unload(world, load_number);
    if (unload_code != RV_OK) {
        RV_LOG_ERR(SQUID_SCENE_TAG, "body load {} not unloaded: rv_err {}", load_number, unload_code);
        return unload_code;
    }
    return RV_OK;
}

// Hides or shows a body entity: a hidden entity is not drawn. A stale id is refused.
inline int squid_scene_body_hidden(rv_pdkkit::ecs_world &world, rv_pdkkit::ecs_id id, bool hidden)
{
    if (!rv_pdkkit::ecs_entity_alive(world, id)) {
        RV_LOG_ERR(SQUID_SCENE_TAG, "body entity is stale; visibility not set");
        return RV_ERR_NOENT;
    }
    if (hidden) {
        rv_pdkkit::ecs_component_hidden *flag = nullptr;
        const int ensure_code = rv_pdkkit::ecs_component_ensure<rv_pdkkit::ecs_component_hidden>(world, id, flag);
        if (ensure_code != RV_OK) {
            RV_LOG_ERR(SQUID_SCENE_TAG, "body visibility not set: rv_err {}", ensure_code);
            return ensure_code;
        }
        return RV_OK;
    }
    // Not held means shown already: the remove's NOENT is not a refusal here.
    const int remove_code = rv_pdkkit::ecs_component_remove<rv_pdkkit::ecs_component_hidden>(world, id);
    const bool refused = remove_code != RV_OK && remove_code != RV_ERR_NOENT;
    if (refused) {
        RV_LOG_ERR(SQUID_SCENE_TAG, "body visibility not set: rv_err {}", remove_code);
        return remove_code;
    }
    return RV_OK;
}

// Writes the shown tilt to the tilt root; the ECS transform turns it into the world matrices of the dome and structure.
inline int squid_scene_pose(rv_pdkkit::ecs_world &world, const squid_scene_objects &objects, rv_pdklib::fx_vec3 rotation)
{
    rv_pdkkit::ecs_component_transform *tilt =
        rv_pdkkit::ecs_component_get<rv_pdkkit::ecs_component_transform>(world, objects.tilt);
    if (tilt == nullptr) {
        RV_LOG_ERR(SQUID_SCENE_TAG, "'{}' object lost its transform; pose not written", SQUID_SCENE_TILT_NAME);
        return RV_ERR_NOENT;
    }
    tilt->rotation = rotation;
    return RV_OK;
}

// Logs the tilt root's rotation in degrees: the tilt the structure is drawn at.
inline int squid_scene_log(rv_pdkkit::ecs_world &world, const squid_scene_objects &objects)
{
    const rv_pdkkit::ecs_component_transform *tilt =
        rv_pdkkit::ecs_component_get<rv_pdkkit::ecs_component_transform>(world, objects.tilt);
    if (tilt == nullptr) {
        RV_LOG_ERR(SQUID_SCENE_TAG, "'{}' object lost its transform; not logged", SQUID_SCENE_TILT_NAME);
        return RV_ERR_NOENT;
    }
    const rv_pdklib::rv_vec3 rotation = rv_pdklib::fx_vec3_to_float(tilt->rotation);
    RV_LOG_INFO(SQUID_SCENE_TAG, "tilt rotation x {:+.2f} z {:+.2f} deg", rotation.x, rotation.z);
    return RV_OK;
}

} // namespace squid
