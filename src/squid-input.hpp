#pragma once

// Tilt input of the squid game (docs/disdoc.md, 7.1-7.3): the pad's tilt from its neutral pose, or the stick and
// D-pad as a reserve, shaped and smoothed into the orbit tilt. Math and state only: no drawing and no button events;
// the D-pad direction bits are read from the state.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>

#include "squid-params.hpp"
#include "pdk/rv_err.h"
#include "pdk/cio/rv_isource.h"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "pdklib/rv_math/rv_math.hpp"

namespace squid
{

constexpr const char *SQUID_INPUT_TAG = "input";

// Quaternion norm below which calibration refuses the pose. A pad without sensors reports (0, 0, 0, 1), a level pose;
// a zero quaternion is no rotation at all, so it cannot be a neutral.
constexpr float SQUID_QUAT_MIN_NORM = 1e-6f;

// Cosine of x below which z reads 0: at 90 degrees of x the roll is not defined.
constexpr float SQUID_MIN_COS_TILT = 1e-6f;

// Gain from the dead zone edge to the game limit: full physical tilt maps to the limit.
constexpr float SQUID_SHAPE_GAIN = SQUID_TILT_LIMIT_DEG / (SQUID_FULL_INPUT_TILT_DEG - SQUID_DEAD_ZONE_DEG);

// Radians to degrees; the tilt angles come from asin in radians and are shown in degrees.
constexpr float SQUID_RAD_TO_DEG = 180.0f / std::numbers::pi_v<float>;

struct squid_tilt {
    float x_deg = 0;
    float z_deg = 0;
};

struct squid_input {
    // Pad frame to world at calibration; the sensor tilt is measured from it.
    rv_pdklib::rv_vec4 neutral{ 0, 0, 0, 1 };
    // Target tilt set by the reserve keys (D-pad); kept after release until level.
    squid_tilt keys{};
    // The shown tilt; follows the target through the smoothing.
    squid_tilt smoothed{};
};

// Clamps each axis to the game tilt limit.
inline squid_tilt squid_tilt_clamp(squid_tilt t)
{
    const float lim = SQUID_TILT_LIMIT_DEG;
    return squid_tilt{ std::clamp(t.x_deg, -lim, lim), std::clamp(t.z_deg, -lim, lim) };
}

// Zero inside the dead zone; outside it, the excess times the gain. NaN gives 0.
inline float squid_shape_axis(float deg)
{
    const float beyond = std::fabs(deg) - SQUID_DEAD_ZONE_DEG;
    const float shaped = beyond > 0.0f ? beyond * SQUID_SHAPE_GAIN : 0.0f;
    return std::copysign(shaped, deg);
}

// Dead zone and gain per axis, then the limit; the dead zone starts at zero, so there is no jump at its edge.
inline squid_tilt squid_input_shape(squid_tilt physical)
{
    return squid_tilt_clamp(squid_tilt{ squid_shape_axis(physical.x_deg), squid_shape_axis(physical.z_deg) });
}

// Hamilton product a * b: for unit quaternions, applies b first, then a.
inline rv_pdklib::rv_vec4 squid_quat_mul(rv_pdklib::rv_vec4 a, rv_pdklib::rv_vec4 b)
{
    return rv_pdklib::rv_vec4{ a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z };
}

// Rotates v by the unit quaternion q, in the sense of rv_mat4_rotate_*.
inline rv_pdklib::rv_vec3 squid_quat_rotate(rv_pdklib::rv_vec4 q, rv_pdklib::rv_vec3 v)
{
    const rv_pdklib::rv_vec3 u{ q.x, q.y, q.z };
    const rv_pdklib::rv_vec3 t = 2.0f * rv_pdklib::rv_cross(u, v);
    return v + q.w * t + rv_pdklib::rv_cross(u, t);
}

// Tilt of the pad in degrees, from its forward (+z) and right (+x) axes in world; only their y components are used,
// so the heading does not change the reading. Exact for |x| < 90 degrees, the inverse of the orbit's Rx * Rz;
// past 90 degrees the readings fold back.
inline squid_tilt squid_pad_angles(rv_pdklib::rv_vec4 quat)
{
    const rv_pdklib::rv_vec3 forward = squid_quat_rotate(quat, rv_pdklib::rv_vec3{ 0, 0, 1 });
    const rv_pdklib::rv_vec3 right = squid_quat_rotate(quat, rv_pdklib::rv_vec3{ 1, 0, 0 });
    const float x = std::asin(std::clamp(-forward.y, -1.0f, 1.0f));
    const float cos_x = std::cos(x);
    const float ratio = cos_x > SQUID_MIN_COS_TILT ? right.y / cos_x : 0.0f;
    const float z = std::asin(std::clamp(ratio, -1.0f, 1.0f));
    return squid_tilt{ x * SQUID_RAD_TO_DEG, z * SQUID_RAD_TO_DEG };
}

// Physical tilt of the pad from neutral, in degrees: the difference of the tilt angles per axis.
// Per axis the difference is exact and does not depend on heading; for a mixed tilt from a tilted neutral it is
// approximate (up to about 3 degrees at a neutral of 30).
inline squid_tilt squid_input_pad_tilt(rv_pdklib::rv_vec4 neutral, rv_pdklib::rv_vec4 quat)
{
    const squid_tilt now = squid_pad_angles(quat);
    const squid_tilt base = squid_pad_angles(neutral);
    return squid_tilt{ now.x_deg - base.x_deg, now.z_deg - base.z_deg };
}

// Takes the pad's current pose as neutral. The shown tilt is not touched; smoothing moves it to the new zero.
inline int squid_input_calibrate(squid_input &in, const rv_istate &state)
{
    const rv_pdklib::rv_vec4 raw{ state.motion.quat_x, state.motion.quat_y, state.motion.quat_z, state.motion.quat_w };
    const float norm = std::sqrt(rv_pdklib::rv_dot(raw, raw));
    const bool usable = std::isfinite(norm) && norm > SQUID_QUAT_MIN_NORM;
    if (!usable) {
        RV_LOG_ERR(SQUID_INPUT_TAG,
            "calibrate refused: quaternion ({}, {}, {}, {}) is zero or not finite",
            raw.x,
            raw.y,
            raw.z,
            raw.w);
        return RV_ERR_INVAL;
    }
    in.neutral = raw * (1.0f / norm);
    return RV_OK;
}

// Level command of the reserve: the keys return to zero.
inline void squid_input_level(squid_input &in)
{
    in.keys = squid_tilt{};
}

// +1 when only the plus key is held, -1 when only the minus key is, 0 otherwise.
inline float squid_axis_dir(bool plus, bool minus)
{
    return (plus ? 1.0f : 0.0f) - (minus ? 1.0f : 0.0f);
}

// Moves the keys at SQUID_KEY_TILT_RATE_DEG_S: NORTH tilts +x, WEST +z, SOUTH -x, EAST -z.
inline squid_tilt squid_input_keys_step(squid_tilt keys, uint64_t buttons, float dt)
{
    const float rate = SQUID_KEY_TILT_RATE_DEG_S * dt;
    const float x_dir = squid_axis_dir((buttons & RV_ISOURCE_DPAD_NORTH) != 0, (buttons & RV_ISOURCE_DPAD_SOUTH) != 0);
    const float z_dir = squid_axis_dir((buttons & RV_ISOURCE_DPAD_WEST) != 0, (buttons & RV_ISOURCE_DPAD_EAST) != 0);
    return squid_tilt_clamp(squid_tilt{ keys.x_deg + rate * x_dir, keys.z_deg + rate * z_dir });
}

// Reserve source: the D-pad moves the keys, then returns the target, the stick plus the keys, clamped per axis.
// Stick forward tilts +x; stick right tilts -z, so the right edge goes down.
inline squid_tilt squid_input_reserve_step(squid_input &in, const rv_istate &state, float dt)
{
    in.keys = squid_input_keys_step(in.keys, state.buttons, dt);
    const float lim = SQUID_TILT_LIMIT_DEG;
    return squid_tilt_clamp(squid_tilt{ in.keys.x_deg + state.left_stick.y * lim, in.keys.z_deg - state.left_stick.x * lim });
}

// Gyro source: the pad's tilt from neutral, shaped.
inline squid_tilt squid_input_sensor_target(rv_pdklib::rv_vec4 neutral, const rv_imotion &motion)
{
    const rv_pdklib::rv_vec4 quat{ motion.quat_x, motion.quat_y, motion.quat_z, motion.quat_w };
    return squid_input_shape(squid_input_pad_tilt(neutral, quat));
}

// One frame of dt seconds. The gyro is the source when the pad has one; otherwise the stick and D-pad.
// A refused frame (bad dt, non-finite stick, or non-finite quaternion on the gyro path) changes nothing.
inline int squid_input_step(squid_input &in, const rv_istate &state, uint64_t abilities, float dt)
{
    const bool dt_bad = !std::isfinite(dt) || dt < 0.0f;
    if (dt_bad) {
        RV_LOG_ERR(SQUID_INPUT_TAG, "step refused: dt {} is negative or not finite", dt);
        return RV_ERR_INVAL;
    }
    const bool stick_bad = !std::isfinite(state.left_stick.x) || !std::isfinite(state.left_stick.y);
    if (stick_bad) {
        RV_LOG_ERR(SQUID_INPUT_TAG, "step refused: left stick ({}, {}) is not finite", state.left_stick.x, state.left_stick.y);
        return RV_ERR_INVAL;
    }
    const bool sensor = (abilities & RV_ISOURCE_GYRO_MOVE) != 0;
    const rv_pdklib::rv_vec4 quat{ state.motion.quat_x, state.motion.quat_y, state.motion.quat_z, state.motion.quat_w };
    const bool quat_bad = sensor && !std::isfinite(rv_pdklib::rv_dot(quat, quat));
    if (quat_bad) {
        RV_LOG_ERR(SQUID_INPUT_TAG, "step refused: quaternion ({}, {}, {}, {}) is not finite", quat.x, quat.y, quat.z, quat.w);
        return RV_ERR_INVAL;
    }
    const squid_tilt target =
        sensor ? squid_input_sensor_target(in.neutral, state.motion) : squid_input_reserve_step(in, state, dt);
    const float follow = 1.0f - std::exp(-dt / SQUID_SMOOTHING_S);
    in.smoothed.x_deg += (target.x_deg - in.smoothed.x_deg) * follow;
    in.smoothed.z_deg += (target.z_deg - in.smoothed.z_deg) * follow;
    return RV_OK;
}

} // namespace squid
