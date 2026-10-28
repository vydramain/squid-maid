#pragma once

// The stage around the round, by phase and its clock: the opening lights the stars and grows the Earth from a point,
// the calibration shows the small Earth from the far camera, and the start flies the camera to the scene's camera and
// grows the bodies from a point. Display only: the stage reads the round and writes the camera and the structure's scale.

#include <algorithm>
#include <cmath>

#include "squid-input.hpp"
#include "squid-params.hpp"
#include "squid-scene.hpp"
#include "squid-state.hpp"
#include "pdk/rv_err.h"
#include "pdkkit/ecs/rv_ecs.hpp"
#include "pdkkit/ecs/rv_ecs_component.hpp"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "pdklib/rv_math/rv_fx.hpp"

namespace squid
{

// Shares from 0 to 1. A default stage is the round's: every star lit, the Earth and the bodies full size, the camera home.
struct squid_stage {
    float stars = 1;  // share of the stars lit
    float earth = 1;  // share of the structure's scale
    float camera = 1; // 0 at the far pose, 1 at the scene's camera
    float bodies = 1; // the bodies' scale; 0 hides them
};

// The structure is never scaled to zero: a zero matrix is not a pose. This small, it is a point.
constexpr float SQUID_STAGE_MIN_SCALE = 0.001f;

// How far t is through the span [start, start + length], clamped to 0..1.
inline float squid_stage_share(float t, float start, float length)
{
    return std::clamp((t - start) / length, 0.0f, 1.0f);
}

// Eases in and out, so a flight or a growth starts and stops softly.
inline float squid_stage_smooth(float u)
{
    return u * u * (3.0f - 2.0f * u);
}

// The stage of a phase t seconds after it was entered. The switch names every phase, so a new phase is a warning.
inline squid_stage squid_stage_of(squid_phase phase, float t)
{
    switch (phase) {
    case squid_phase::language:
        return squid_stage{ 0, 0, 0, 0 };
    case squid_phase::intro:
        return squid_stage{ squid_stage_share(t, 0, SQUID_INTRO_STARS_S),
            squid_stage_smooth(squid_stage_share(t, SQUID_INTRO_STARS_S, SQUID_INTRO_EARTH_S)),
            0,
            0 };
    case squid_phase::ready:
        return squid_stage{ 1, 1, 0, 0 };
    case squid_phase::arrive:
        return squid_stage{ 1,
            1,
            squid_stage_smooth(squid_stage_share(t, 0, SQUID_ARRIVE_CAMERA_S)),
            squid_stage_smooth(squid_stage_share(t, SQUID_ARRIVE_CAMERA_S, SQUID_ARRIVE_BODIES_S)) };
    case squid_phase::play:
    case squid_phase::pause:
    case squid_phase::descent:
    case squid_phase::descent_pause:
    case squid_phase::landed:
        return squid_stage{};
    }
    return squid_stage{};
}

// The camera at a share of the flight: the far pose at 0, the scene's pose at 1. The far camera looks at the structure
// straight from its front, at SQUID_FAR_AZIMUTH_DEG; its yaw is that minus a half turn, taken on the side nearer the
// scene camera's yaw, so the flight swings the short way. Pitch down is positive x; at yaw y and pitch p the view runs
// along (sin y cos p, -sin p, cos y cos p). The far eye sits back from the dome centre along a line
// SQUID_FAR_RAISE_DEG shallower than its pitch, so the centre shows above the middle of the screen.
inline int
squid_stage_camera(const squid_scene_objects &objects, float share, rv_pdklib::fx_vec3 &position, rv_pdklib::fx_vec3 &rotation)
{
    const rv_pdklib::rv_vec3 home_position = rv_pdklib::fx_vec3_to_float(objects.camera_position);
    const rv_pdklib::rv_vec3 home_rotation = rv_pdklib::fx_vec3_to_float(objects.camera_rotation);
    const float far_yaw_deg = SQUID_FAR_AZIMUTH_DEG - SQUID_HALF_TURN_DEG;
    const float yaw = far_yaw_deg * SQUID_DEG_TO_RAD;
    const float line = (SQUID_FAR_PITCH_DEG - SQUID_FAR_RAISE_DEG) * SQUID_DEG_TO_RAD;
    const float d = SQUID_FAR_DISTANCE;
    const rv_pdklib::rv_vec3 far{ SQUID_DOME_CENTER_X - d * std::sin(yaw) * std::cos(line),
        SQUID_DOME_CENTER_Y + d * std::sin(line),
        SQUID_DOME_CENTER_Z - d * std::cos(yaw) * std::cos(line) };
    const auto mix = [share](float a, float b) {
        return a + (b - a) * share;
    };
    const rv_pdklib::rv_vec3 eye{ mix(far.x, home_position.x), mix(far.y, home_position.y), mix(far.z, home_position.z) };
    const rv_pdklib::rv_vec3 turn{ mix(SQUID_FAR_PITCH_DEG, home_rotation.x),
        mix(far_yaw_deg, home_rotation.y),
        home_rotation.z };
    const int eye_code = squid_scene_fx_point(eye, position);
    if (eye_code != RV_OK) {
        return eye_code;
    }
    return squid_scene_fx_point(turn, rotation);
}

// Writes the stage's camera and structure scale to the scene. A refusal keeps the old pose; its cause is logged.
inline int squid_stage_apply(rv_pdkkit::ecs_world &world, const squid_scene_objects &objects, const squid_stage &stage)
{
    rv_pdkkit::ecs_component_transform *camera =
        rv_pdkkit::ecs_component_get<rv_pdkkit::ecs_component_transform>(world, objects.camera);
    rv_pdkkit::ecs_component_transform *structure =
        rv_pdkkit::ecs_component_get<rv_pdkkit::ecs_component_transform>(world, objects.pose_scale);
    if (camera == nullptr || structure == nullptr) {
        RV_LOG_ERR(SQUID_SCENE_TAG,
            "'{}' or '{}' object lost its transform; stage not set",
            SQUID_SCENE_CAMERA_NAME,
            SQUID_SCENE_POSE_SCALE_NAME);
        return RV_ERR_NOENT;
    }
    rv_pdklib::fx_vec3 position{};
    rv_pdklib::fx_vec3 rotation{};
    const int camera_code = squid_stage_camera(objects, stage.camera, position, rotation);
    if (camera_code != RV_OK) {
        RV_LOG_ERR(SQUID_SCENE_TAG, "stage camera at share {} is outside the Q20.12 range; camera kept", stage.camera);
        return camera_code;
    }
    const float s = SQUID_STRUCTURE_SCALE * std::max(stage.earth, SQUID_STAGE_MIN_SCALE);
    rv_pdklib::fx_vec3 scale{};
    const int scale_code = squid_scene_fx_point(rv_pdklib::rv_vec3{ s, s, s }, scale);
    if (scale_code != RV_OK) {
        return scale_code;
    }
    camera->position = position;
    camera->rotation = rotation;
    structure->scale = scale;
    return RV_OK;
}

} // namespace squid
