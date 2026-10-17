#pragma once

// The scene of the squid game in the ECS (docs/disdoc.md, 6, 10, 15.2): the tilt root with Alkoldun as its child, and
// the debris targets outside the root. The disc writes the pose each frame; the ECS transform turns it into world matrices.

#include <array>
#include <cmath>
#include <cstdint>

#include "squid-orbit.hpp"
#include "squid-params.hpp"
#include "squid-draw.hpp"
#include "squid-sequence.hpp"
#include "pdk/cd/rv_cd.h"
#include "pdk/cg/rv_cg.h"
#include "pdk/rv_err.h"
#include "pdklib/rv_ecs/rv_ecs.hpp"
#include "pdklib/rv_ecs/rv_ecs_component.hpp"
#include "pdklib/rv_ecs/rv_ecs_frame.hpp"
#include "pdklib/rv_ecs/rv_ecs_loads.hpp"
#include "pdklib/rv_ecs/rv_ecs_transform.hpp"
#include "pdklib/rv_logs/rv_logs.hpp"

namespace squid
{

// Log tag of scene messages.
inline constexpr const char *SQUID_SCENE_TAG = "scene";

// The scene file and the names of the objects the disc moves or reports; the objects are found by name.
inline constexpr const char *SQUID_SCENE_FILE = "main.scene.toml";
inline constexpr const char *SQUID_SCENE_TILT_NAME = "tilt";
inline constexpr const char *SQUID_SCENE_ALKOLDUN_NAME = "alkoldun";
// Scene object of each target, in sequence order (index = target index); the code places each at its target.
inline constexpr std::array<const char *, SQUID_TARGET_COUNT> SQUID_SCENE_DEBRIS_NAMES{
    "junk_crt",
    "junk_floppy",
    "junk_cassette",
    "junk_phone",
    "junk_tube",
    "junk_reel",
    "junk_punchcard",
    "junk_calculator",
};
// The frame system that sets the camera of the scene's models; it is registered under this name.
inline constexpr const char *SQUID_SCENE_CAMERA_SYSTEM = "squid-camera";

// Entities of the scene objects the disc moves or reports; debris[i] is the object of target i.
struct squid_scene_objects {
    rv_pdklib::ecs_id tilt{};
    rv_pdklib::ecs_id alkoldun{};
    std::array<rv_pdklib::ecs_id, SQUID_TARGET_COUNT> debris{};
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

// Each debris object sits at its target's position; the code, not the scene file, sets them.
inline int squid_scene_place_debris(rv_pdklib::ecs_world &world, const squid_scene_objects &objects)
{
    squid_targets targets{};
    const int build_code = squid_sequence_build(targets);
    if (build_code != RV_OK) {
        return build_code;
    }
    for (int i = 0; i < SQUID_TARGET_COUNT; ++i) {
        rv_pdklib::ecs_component_transform *debris =
            rv_pdklib::ecs_component_get<rv_pdklib::ecs_component_transform>(world, objects.debris[i]);
        if (debris == nullptr) {
            RV_LOG_ERR(SQUID_SCENE_TAG, "'{}' object has no transform", SQUID_SCENE_DEBRIS_NAMES[i]);
            return RV_ERR_NOENT;
        }
        const int point_code = squid_scene_fx_point(targets[i].position, debris->position);
        if (point_code != RV_OK) {
            return point_code;
        }
    }
    return RV_OK;
}

// Finds the objects, checks the tilt root (at the origin with scale 1 and no parent, as squid_orbit_position
// assumes) and places the debris. Any refusal is returned with its cause logged.
inline int squid_scene_check_and_place(rv_pdklib::ecs_world &world, squid_scene_objects &objects)
{
    const int tilt_code = squid_scene_find(world, SQUID_SCENE_TILT_NAME, objects.tilt);
    if (tilt_code != RV_OK) {
        return tilt_code;
    }
    const int alkoldun_code = squid_scene_find(world, SQUID_SCENE_ALKOLDUN_NAME, objects.alkoldun);
    if (alkoldun_code != RV_OK) {
        return alkoldun_code;
    }
    for (int i = 0; i < SQUID_TARGET_COUNT; ++i) {
        const int debris_code = squid_scene_find(world, SQUID_SCENE_DEBRIS_NAMES[i], objects.debris[i]);
        if (debris_code != RV_OK) {
            return debris_code;
        }
    }
    const rv_pdklib::ecs_component_transform *tilt =
        rv_pdklib::ecs_component_get<rv_pdklib::ecs_component_transform>(world, objects.tilt);
    if (tilt == nullptr) {
        RV_LOG_ERR(SQUID_SCENE_TAG, "'{}' object has no transform", SQUID_SCENE_TILT_NAME);
        return RV_ERR_NOENT;
    }
    const bool at_origin = tilt->position.x == 0 && tilt->position.y == 0 && tilt->position.z == 0;
    const bool unit_scale =
        tilt->scale.x == rv_pdklib::fx_one && tilt->scale.y == rv_pdklib::fx_one && tilt->scale.z == rv_pdklib::fx_one;
    const bool rootless = tilt->parent == rv_pdklib::ecs_id{};
    if (!at_origin || !unit_scale || !rootless) {
        RV_LOG_ERR(SQUID_SCENE_TAG, "scene refused: the tilt root must sit at the origin with scale 1 and no parent");
        return RV_ERR_INVAL;
    }
    const int place_code = squid_scene_place_debris(world, objects);
    if (place_code != RV_OK) {
        return place_code;
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
    const int check_code = squid_scene_check_and_place(world, objects);
    if (check_code != RV_OK) {
        squid_scene_discard(world, load_number);
        return check_code;
    }
    return RV_OK;
}

// The orbit point without the tilt: Alkoldun's position in the tilt root's space.
inline int squid_scene_orbit_local(const squid_orbit &orbit, rv_pdklib::fx_vec3 &out)
{
    const rv_pdklib::rv_vec3 point{
        static_cast<float>(SQUID_ORBIT_RADIUS * std::cos(orbit.phase_rad)),
        0.0f,
        static_cast<float>(SQUID_ORBIT_RADIUS * std::sin(orbit.phase_rad)),
    };
    const int point_code = squid_scene_fx_point(point, out);
    if (point_code != RV_OK) {
        return point_code;
    }
    return RV_OK;
}

// Writes the shown tilt to the root and the bare orbit point to Alkoldun. The ECS transform turns them into world
// matrices, so Alkoldun's world position equals squid_orbit_position up to the Q20.12 rounding of the local point.
inline int squid_scene_pose(rv_pdklib::ecs_world &world,
    const squid_scene_objects &objects,
    rv_pdklib::fx_vec3 rotation,
    const squid_orbit &orbit)
{
    rv_pdklib::ecs_component_transform *tilt =
        rv_pdklib::ecs_component_get<rv_pdklib::ecs_component_transform>(world, objects.tilt);
    rv_pdklib::ecs_component_transform *alkoldun =
        rv_pdklib::ecs_component_get<rv_pdklib::ecs_component_transform>(world, objects.alkoldun);
    if (tilt == nullptr || alkoldun == nullptr) {
        RV_LOG_ERR(SQUID_SCENE_TAG, "scene object lost its transform; pose not written");
        return RV_ERR_NOENT;
    }
    rv_pdklib::fx_vec3 local{};
    const int local_code = squid_scene_orbit_local(orbit, local);
    if (local_code != RV_OK) {
        return local_code;
    }
    tilt->rotation = rotation;
    alkoldun->position = local;
    return RV_OK;
}

// World position of the entity's origin, from its transform chain; the frame need not have run.
inline int squid_scene_world_point(rv_pdklib::ecs_world &world, rv_pdklib::ecs_id id, rv_pdklib::rv_vec3 &out)
{
    rv_pdklib::rv_mat4 matrix{};
    const int code = rv_pdklib::ecs_transform_world(world, id, matrix);
    if (code != RV_OK) {
        return code;
    }
    out = rv_pdklib::rv_mat4_mul_point(matrix, rv_pdklib::rv_vec3{ 0.0f, 0.0f, 0.0f });
    return RV_OK;
}

// Logs the world positions of the first target's debris and of Alkoldun: the debris stays where it is while the tilt
// changes.
inline int squid_scene_log(rv_pdklib::ecs_world &world, const squid_scene_objects &objects)
{
    rv_pdklib::rv_vec3 debris{};
    const int debris_code = squid_scene_world_point(world, objects.debris[0], debris);
    if (debris_code != RV_OK) {
        return debris_code;
    }
    rv_pdklib::rv_vec3 alkoldun{};
    const int alkoldun_code = squid_scene_world_point(world, objects.alkoldun, alkoldun);
    if (alkoldun_code != RV_OK) {
        return alkoldun_code;
    }
    RV_LOG_INFO(SQUID_SCENE_TAG,
        "world debris ({:+.3f}, {:+.3f}, {:+.3f}) alkoldun ({:+.3f}, {:+.3f}, {:+.3f})",
        debris.x,
        debris.y,
        debris.z,
        alkoldun.x,
        alkoldun.y,
        alkoldun.z);
    return RV_OK;
}

} // namespace squid
