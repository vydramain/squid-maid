#pragma once

// Disc on the dome: the disc maps by azimuthal equidistant projection, and a ball hovers above its point by the lift
// of its hover phase. Display only: physics never reads these results.

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

// Centre of a ball of radius ball_r on the dome, relative to the dome centre, lifted by lift (world units) along the
// normal. The disc point (x, z) at distance d from the disc centre has polar angle d / R; the centre sits on the normal
// at R + ball_r + lift. Continuous at d = 0 and phi = +-pi.
inline rv_pdklib::rv_vec3 squid_dome_offset(float disc_x, float disc_z, float ball_r, float lift)
{
    const float d = std::sqrt(disc_x * disc_x + disc_z * disc_z);
    const float theta = d / SQUID_DOME_RADIUS;
    const float phi = std::atan2(disc_z, disc_x);
    const float reach = SQUID_DOME_RADIUS + ball_r + lift;
    return rv_pdklib::rv_vec3{ reach * std::sin(theta) * std::cos(phi),
        reach * std::cos(theta),
        reach * std::sin(theta) * std::sin(phi) };
}

// Hover lift in world units for a hover phase: 0 at phase 0, SQUID_HOVER_MAX at phase pi, smooth in between, so the
// ball bobs over the dome and never sinks into it. 1 - cos spans 0..2; the half maps it onto 0..1 of SQUID_HOVER_MAX.
inline float squid_dome_hover_lift(float phase)
{
    return 0.5f * SQUID_HOVER_MAX * (1.0f - std::cos(phase));
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
