#pragma once

// The scene of the squid game in the ECS: the tilt root at the dome centre with the structure as its children, and
// the ball body template that bodies are loaded from under the root. The disc writes the tilt each frame; the ECS
// transform turns it into world matrices.

#include <array>
#include <cmath>
#include <cstdint>

#include "squid-input.hpp"
#include "squid-params.hpp"
#include "pdk/cd/rv_cd.h"
#include "pdk/cg/rv_cg.h"
#include "pdk/rv_err.h"
#include "pdklib/rv_ecs/rv_ecs.hpp"
#include "pdklib/rv_ecs/rv_ecs_component.hpp"
#include "pdklib/rv_ecs/rv_ecs_frame.hpp"
#include "pdklib/rv_ecs/rv_ecs_loads.hpp"
#include "pdklib/rv_ecs/rv_ecs_transform.hpp"
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
// The frame system that sets the camera of the scene's models; it is registered under this name.
inline constexpr const char *SQUID_SCENE_CAMERA_SYSTEM = "squid-camera";

// Entity of the scene object the disc moves or reports.
struct squid_scene_objects {
    rv_pdklib::ecs_id tilt{};
};

// Entity of the object with the name, in load order. A missing object is an error; the name is in the log.
inline int squid_scene_find(rv_pdklib::ecs_world &world, const char *name, rv_pdklib::ecs_id &out)
{
    for (const rv_pdklib::ecs_load &load : world.loads) {
        for (const rv_pdklib::ecs_id id : load.made) {
            const auto *name_component = rv_pdklib::ecs_component_get<rv_pdklib::ecs_component_name>(world, id);
            if (name_component != nullptr && name_component->text == name) {
                out = id;
                return RV_OK;
            }
        }
    }
    RV_LOG_ERR(SQUID_SCENE_TAG, "no '{}' object in the scene", name);
    return RV_ERR_NOENT;
}

// The camera of the scene's models and of the debug lines: one view for both, looking at the Earth's centre.
inline void squid_scene_camera_make(rv_cg_camera &camera)
{
    const float elevation = SQUID_CAMERA_ELEVATION_DEG * SQUID_DEG_TO_RAD;
    camera.eye = rv_cg_vec3{ 0.0f, SQUID_CAMERA_DISTANCE * std::sin(elevation), SQUID_CAMERA_DISTANCE * std::cos(elevation) };
    camera.target = rv_cg_vec3{ 0.0f, 0.0f, 0.0f };
    camera.up = rv_cg_vec3{ 0.0f, 1.0f, 0.0f };
    camera.fov_y = SQUID_CAMERA_FOV_DEG * SQUID_DEG_TO_RAD;
    camera.near_plane = SQUID_CAMERA_NEAR;
    camera.far_plane = SQUID_CAMERA_FAR;
}

// Frame system: sets the camera the ECS draw uses.
inline int squid_scene_camera(rv_pdklib::ecs_world &world, const rv_pdklib::ecs_frame &)
{
    rv_cg_camera camera{};
    squid_scene_camera_make(camera);
    rv_pdklib::ecs_world_camera(world, camera);
    return RV_OK;
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

// Tilt angles in degrees to the tilt root's Q20.12 rotation (x, 0, z); rotation is untouched on error.
inline int squid_scene_tilt(float tilt_x_deg, float tilt_z_deg, rv_pdklib::fx_vec3 &rotation)
{
    rv_pdklib::fx tilt_x{};
    rv_pdklib::fx tilt_z{};
    const int x_code = rv_pdklib::fx_from_double(tilt_x_deg, tilt_x);
    if (x_code != RV_OK) {
        RV_LOG_ERR(SQUID_SCENE_TAG, "tilt x {} deg is outside the Q20.12 range; rotation kept", tilt_x_deg);
        return x_code;
    }
    const int z_code = rv_pdklib::fx_from_double(tilt_z_deg, tilt_z);
    if (z_code != RV_OK) {
        RV_LOG_ERR(SQUID_SCENE_TAG, "tilt z {} deg is outside the Q20.12 range; rotation kept", tilt_z_deg);
        return z_code;
    }
    rotation = rv_pdklib::fx_vec3{ tilt_x, 0, tilt_z };
    return RV_OK;
}

// Finds the tilt root and checks it: at the dome centre with scale 1 and no parent. Any refusal is returned with its
// cause logged.
inline int squid_scene_check(rv_pdklib::ecs_world &world, squid_scene_objects &objects)
{
    const int tilt_code = squid_scene_find(world, SQUID_SCENE_TILT_NAME, objects.tilt);
    if (tilt_code != RV_OK) {
        return tilt_code;
    }
    const rv_pdklib::ecs_component_transform *tilt =
        rv_pdklib::ecs_component_get<rv_pdklib::ecs_component_transform>(world, objects.tilt);
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
    const bool rootless = tilt->parent == rv_pdklib::ecs_id{};
    if (!at_centre || !unit_scale || !rootless) {
        RV_LOG_ERR(SQUID_SCENE_TAG, "scene refused: the tilt root must sit at the dome centre with scale 1 and no parent");
        return RV_ERR_INVAL;
    }
    return RV_OK;
}

// Unloads a scene refused after its load, so no half scene stays in the world. A failed unload is logged; the
// refusal that caused it is the one the caller reports.
inline void squid_scene_discard(rv_pdklib::ecs_world &world, uint32_t load_number)
{
    const int unload_code = rv_pdklib::ecs_scene_unload_now(world, load_number);
    if (unload_code != RV_OK) {
        RV_LOG_WARN(SQUID_SCENE_TAG, "refused scene not unloaded: rv_err {}", unload_code);
    }
}

// Loads the scene and registers the camera system. A refusal leaves the scene unused.
inline int squid_scene_load(rv_pdklib::ecs_world &world, rv_cd *cd, squid_scene_objects &objects)
{
    const int system_code = rv_pdklib::ecs_system_register(world, SQUID_SCENE_CAMERA_SYSTEM, squid_scene_camera);
    if (system_code != RV_OK) {
        RV_LOG_ERR(SQUID_SCENE_TAG, "camera system not registered: rv_err {}", system_code);
        return system_code;
    }
    uint32_t load_number = 0;
    const int load_code = rv_pdklib::ecs_scene_load(world, cd, SQUID_SCENE_FILE, rv_pdklib::ecs_id{}, load_number);
    if (load_code != RV_OK) {
        RV_LOG_ERR(SQUID_SCENE_TAG, "{} not loaded: rv_err {}", SQUID_SCENE_FILE, load_code);
        return load_code;
    }
    const int check_code = squid_scene_check(world, objects);
    if (check_code != RV_OK) {
        squid_scene_discard(world, load_number);
        return check_code;
    }
    return RV_OK;
}

// Sets the model name of a body entity; the name is the model the disc packs (assets/models).
inline int squid_scene_body_model(rv_pdklib::ecs_world &world, rv_pdklib::ecs_id id, const char *model)
{
    if (model == nullptr) {
        RV_LOG_ERR(SQUID_SCENE_TAG, "body model is null; model not set");
        return RV_ERR_INVAL;
    }
    rv_pdklib::ecs_component_model *component = rv_pdklib::ecs_component_get<rv_pdklib::ecs_component_model>(world, id);
    if (component == nullptr) {
        RV_LOG_ERR(SQUID_SCENE_TAG, "body has no model; model '{}' not set", model);
        return RV_ERR_NOENT;
    }
    component->name = model;
    return RV_OK;
}

// Loads one ball body under the tilt root and sets its model. On success load_number is the load to unload later and
// id is the body's entity; on error neither is written and no part of the body stays in the world.
inline int squid_scene_body_spawn(rv_pdklib::ecs_world &world,
    rv_cd *cd,
    const squid_scene_objects &objects,
    const char *model,
    uint32_t &load_number,
    rv_pdklib::ecs_id &id)
{
    uint32_t number = 0;
    const int load_code = rv_pdklib::ecs_scene_load(world, cd, SQUID_SCENE_BODY_FILE, objects.tilt, number);
    if (load_code != RV_OK) {
        RV_LOG_ERR(SQUID_SCENE_TAG, "{} not loaded: rv_err {}", SQUID_SCENE_BODY_FILE, load_code);
        return load_code;
    }
    // The load was just pushed, so its position is valid.
    const auto &made = world.loads[rv_pdklib::ecs_load_position(world, number)].made;
    if (made.size() != 1) {
        RV_LOG_ERR(SQUID_SCENE_TAG, "{} must hold one object, holds {}", SQUID_SCENE_BODY_FILE, made.size());
        squid_scene_discard(world, number);
        return RV_ERR_INVAL;
    }
    const rv_pdklib::ecs_id body = made[0];
    const int model_code = squid_scene_body_model(world, body, model);
    if (model_code != RV_OK) {
        squid_scene_discard(world, number);
        return model_code;
    }
    load_number = number;
    id = body;
    return RV_OK;
}

// Places a body under the tilt root: model name, position in the tilt root's space (Q20.12) and Euler rotation.
inline int squid_scene_body_set(rv_pdklib::ecs_world &world,
    rv_pdklib::ecs_id id,
    const char *model,
    rv_pdklib::fx_vec3 position,
    rv_pdklib::fx_vec3 rotation)
{
    rv_pdklib::ecs_component_transform *transform = rv_pdklib::ecs_component_get<rv_pdklib::ecs_component_transform>(world, id);
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

// Unloads a body by its load number. In a frame the unload waits for the frame's end.
inline int squid_scene_body_unload(rv_pdklib::ecs_world &world, uint32_t load_number)
{
    const int unload_code = rv_pdklib::ecs_scene_unload(world, load_number);
    if (unload_code != RV_OK) {
        RV_LOG_ERR(SQUID_SCENE_TAG, "body load {} not unloaded: rv_err {}", load_number, unload_code);
        return unload_code;
    }
    return RV_OK;
}

// Hides or shows a body entity: a hidden entity is not drawn. A stale id is refused.
inline int squid_scene_body_hidden(rv_pdklib::ecs_world &world, rv_pdklib::ecs_id id, bool hidden)
{
    if (!rv_pdklib::ecs_entity_alive(world, id)) {
        RV_LOG_ERR(SQUID_SCENE_TAG, "body entity is stale; visibility not set");
        return RV_ERR_NOENT;
    }
    if (hidden) {
        (void)rv_pdklib::ecs_component_ensure<rv_pdklib::ecs_component_hidden>(world, id);
        return RV_OK;
    }
    // Not held means shown already: the remove's NOENT is not a refusal here.
    (void)rv_pdklib::ecs_component_remove<rv_pdklib::ecs_component_hidden>(world, id);
    return RV_OK;
}

// Writes the shown tilt to the tilt root; the ECS transform turns it into the structure's world matrices.
inline int squid_scene_pose(rv_pdklib::ecs_world &world, const squid_scene_objects &objects, rv_pdklib::fx_vec3 rotation)
{
    rv_pdklib::ecs_component_transform *tilt =
        rv_pdklib::ecs_component_get<rv_pdklib::ecs_component_transform>(world, objects.tilt);
    if (tilt == nullptr) {
        RV_LOG_ERR(SQUID_SCENE_TAG, "'{}' object lost its transform; pose not written", SQUID_SCENE_TILT_NAME);
        return RV_ERR_NOENT;
    }
    tilt->rotation = rotation;
    return RV_OK;
}

// Logs the tilt root's rotation in degrees: the tilt the structure is drawn at.
inline int squid_scene_log(rv_pdklib::ecs_world &world, const squid_scene_objects &objects)
{
    const rv_pdklib::ecs_component_transform *tilt =
        rv_pdklib::ecs_component_get<rv_pdklib::ecs_component_transform>(world, objects.tilt);
    if (tilt == nullptr) {
        RV_LOG_ERR(SQUID_SCENE_TAG, "'{}' object lost its transform; not logged", SQUID_SCENE_TILT_NAME);
        return RV_ERR_NOENT;
    }
    const rv_pdklib::rv_vec3 rotation = rv_pdklib::fx_vec3_to_float(tilt->rotation);
    RV_LOG_INFO(SQUID_SCENE_TAG, "tilt rotation x {:+.2f} z {:+.2f} deg", rotation.x, rotation.z);
    return RV_OK;
}

} // namespace squid
