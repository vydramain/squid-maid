#pragma once

// Disc on the dome: the disc maps by azimuthal equidistant projection, and a rolling ball turns by its path over
// its radius. Display only: physics never reads these results.

#include <cmath>
#include <numbers>

#include "squid-input.hpp"
#include "squid-params.hpp"
#include "pdk/rv_err.h"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "pdklib/rv_math/rv_fx.hpp"
#include "pdklib/rv_math/rv_math.hpp"

namespace squid
{

// Log tag of this module's messages.
inline constexpr const char *squid_dome_tag = "dome";

// Below this |cos x| the decomposition is at gimbal lock (x = +-90 deg): z is taken as 0 and y carries the rest.
constexpr double SQUID_DOME_GIMBAL_COS = 1e-6;

// Degrees per radian: the transform's rotation is in degrees.
constexpr double SQUID_DOME_RAD_TO_DEG = 180.0 / std::numbers::pi;

// Centre of a ball of radius ball_r on the dome, relative to the dome centre. The disc point (x, z) at distance d from
// the disc centre has polar angle d / R; the centre sits on the normal at R + ball_r. Continuous at d = 0 and phi = +-pi.
inline rv_pdklib::rv_vec3 squid_dome_offset(float disc_x, float disc_z, float ball_r)
{
    const float d = std::sqrt(disc_x * disc_x + disc_z * disc_z);
    const float theta = d / SQUID_DOME_RADIUS;
    const float phi = std::atan2(disc_z, disc_x);
    const float reach = SQUID_DOME_RADIUS + ball_r;
    return rv_pdklib::rv_vec3{ reach * std::sin(theta) * std::cos(phi),
        reach * std::cos(theta),
        reach * std::sin(theta) * std::sin(phi) };
}

// Turns the ball by |path| / ball_r about normalize(cross(n, path)), n = the outward normal at from: the world
// rotation that rolls it without slipping. A zero path keeps the rotation; a bad radius or a path with no tangent part
// on the dome is refused and the rotation kept.
inline int squid_dome_roll(rv_pdklib::rv_vec4 &rotation, rv_pdklib::rv_vec3 from, rv_pdklib::rv_vec3 to, float ball_r)
{
    const bool bad_r = !std::isfinite(ball_r) || ball_r <= 0.0f;
    if (bad_r) {
        RV_LOG_ERR(squid_dome_tag, "roll refused: radius {} is not positive and finite; rotation kept", ball_r);
        return RV_ERR_INVAL;
    }
    const rv_pdklib::rv_vec3 delta = to - from;
    const float path = rv_pdklib::rv_length(delta);
    const rv_pdklib::rv_vec3 tangent_axis = rv_pdklib::rv_cross(rv_pdklib::rv_normalize(from), delta);
    const float axis_len = rv_pdklib::rv_length(tangent_axis);
    const bool bad_path = !std::isfinite(path) || (path > 0.0f && axis_len == 0.0f);
    if (bad_path) {
        RV_LOG_ERR(squid_dome_tag, "roll refused: path {} is not finite or has no tangent part; rotation kept", path);
        return RV_ERR_INVAL;
    }
    // A zero path has no axis: the step is the identity.
    const float inv_axis = axis_len > 0.0f ? 1.0f / axis_len : 0.0f;
    const rv_pdklib::rv_vec3 axis = tangent_axis * inv_axis;
    const float half = 0.5f * path / ball_r;
    const float s = std::sin(half);
    const rv_pdklib::rv_vec4 step{ axis.x * s, axis.y * s, axis.z * s, std::cos(half) };
    const rv_pdklib::rv_vec4 turned = squid_quat_mul(step, rotation);
    // Renormalized: float drift of repeated products would otherwise grow.
    const float len = std::sqrt(rv_pdklib::rv_dot(turned, turned));
    rotation = turned * (1.0f / len);
    return RV_OK;
}

// Euler angles (degrees) of the transform's Ry * Rx * Rz that reproduce the matrix of the unit quaternion q.
// Refused, rotation kept, when an angle does not fit Q20.12 (a non-finite q lands here too).
inline int squid_dome_euler(rv_pdklib::rv_vec4 q, rv_pdklib::fx_vec3 &rotation)
{
    const double qx = q.x;
    const double qy = q.y;
    const double qz = q.z;
    const double qw = q.w;
    // Quaternion matrix entries, m[row][col], in the rv_scene_rotate_* convention.
    const double m00 = 1.0 - 2.0 * (qy * qy + qz * qz);
    const double m02 = 2.0 * (qx * qz + qy * qw);
    const double m10 = 2.0 * (qx * qy + qz * qw);
    const double m11 = 1.0 - 2.0 * (qx * qx + qz * qz);
    const double m12 = 2.0 * (qy * qz - qx * qw);
    const double m20 = 2.0 * (qx * qz - qy * qw);
    const double m22 = 1.0 - 2.0 * (qx * qx + qy * qy);
    const double cos_x = std::sqrt(m10 * m10 + m11 * m11);
    const bool gimbal = cos_x <= SQUID_DOME_GIMBAL_COS;
    // atan2, not asin: asin is ill-conditioned at x = +-90 deg, where float rounding would move x by ~0.03 deg.
    const double x_deg = std::atan2(-m12, cos_x) * SQUID_DOME_RAD_TO_DEG;
    const double y_rad = gimbal ? std::atan2(-m20, m00) : std::atan2(m02, m22);
    const double z_rad = gimbal ? 0.0 : std::atan2(m10, m11);
    const double angles[] = { x_deg, y_rad * SQUID_DOME_RAD_TO_DEG, z_rad * SQUID_DOME_RAD_TO_DEG };
    constexpr const char *axis_names[] = { "x", "y", "z" };
    rv_pdklib::fx raw[3]{};
    for (int i = 0; i < 3; ++i) {
        const int rc = rv_pdklib::fx_from_double(angles[i], raw[i]);
        if (rc != RV_OK) {
            RV_LOG_ERR(squid_dome_tag,
                "euler {} {} deg is outside Q20.12 or not finite; rotation kept",
                axis_names[i],
                angles[i]);
            return rc;
        }
    }
    rotation = rv_pdklib::fx_vec3{ raw[0], raw[1], raw[2] };
    return RV_OK;
}

} // namespace squid
