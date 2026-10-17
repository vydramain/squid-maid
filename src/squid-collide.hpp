#pragma once

// Collision of Alkoldun with a debris ball (docs/disdoc.md, section 8.2): the orbit path of the whole step is
// checked in sub-steps short enough that a fast move cannot jump over the target between two checks, up to
// SQUID_COLLIDE_MAX_STEPS sub-steps (a longer move is logged).

#include <algorithm>
#include <cmath>

#include "squid-orbit.hpp"
#include "pdk/rv_err.h"
#include "pdklib/rv_math/rv_math.hpp"
#include "pdklib/rv_logs/rv_logs.hpp"

namespace squid
{

// Log tag of collision messages.
inline constexpr const char *squid_collide_tag = "collide";

// Target sub-step length as a share of the radius sum. Sub-steps are checked as chords, so a move between two
// checks does not skip the target; the guarantee ends at SQUID_COLLIDE_MAX_STEPS (logged as a warning).
constexpr double SQUID_COLLIDE_STEP_SHARE = 0.25;

// Upper bound of sub-steps per sweep; a longer move is checked coarsely and logged.
constexpr int SQUID_COLLIDE_MAX_STEPS = 256;

// Tilt deltas are in degrees; the path bound counts radians.
constexpr double SQUID_COLLIDE_DEG_TO_RAD = SQUID_ORBIT_TAU_RAD / 360.0;

// Orbit state of a frame: phase in radians, tilt in degrees.
struct squid_sweep {
    double phase_rad;
    float tilt_x_deg;
    float tilt_z_deg;
};

// Touching is true while the ball overlaps the target; enter fires once per contact.
struct squid_contact {
    bool touching = false;
};

// Closest point of segment ab to center is within radius_sum; a degenerate segment is a point.
inline bool squid_collide_segment(rv_pdklib::rv_vec3 a, rv_pdklib::rv_vec3 b, rv_pdklib::rv_vec3 center, float radius_sum)
{
    const rv_pdklib::rv_vec3 ab = b - a;
    const float len2 = rv_pdklib::rv_dot(ab, ab);
    const float along = len2 > 0.0f ? rv_pdklib::rv_dot(center - a, ab) / len2 : 0.0f;
    const float t = std::clamp(along, 0.0f, 1.0f);
    const rv_pdklib::rv_vec3 nearest = a + ab * t;
    const rv_pdklib::rv_vec3 d = nearest - center;
    return rv_pdklib::rv_dot(d, d) <= radius_sum * radius_sum;
}

// Forward phase advance from -> to in [0, 2pi): the orbit never runs backwards.
inline double squid_collide_phase_delta(double from_rad, double to_rad)
{
    const double turn = std::fmod(to_rad - from_rad, SQUID_ORBIT_TAU_RAD);
    return turn < 0.0 ? turn + SQUID_ORBIT_TAU_RAD : turn;
}

inline bool squid_collide_pose_finite(const squid_sweep &s)
{
    return std::isfinite(s.phase_rad) && std::isfinite(s.tilt_x_deg) && std::isfinite(s.tilt_z_deg);
}

// Orbit point at fraction t of the sweep; the phase advances by dphase and the tilt interpolates linearly.
inline int squid_collide_pose(const squid_sweep &from, const squid_sweep &to, double dphase, double t, rv_pdklib::rv_vec3 &out)
{
    const double phase = std::fmod(from.phase_rad + dphase * t, SQUID_ORBIT_TAU_RAD);
    const float tf = static_cast<float>(t);
    const float tilt_x = from.tilt_x_deg * (1.0f - tf) + to.tilt_x_deg * tf;
    const float tilt_z = from.tilt_z_deg * (1.0f - tf) + to.tilt_z_deg * tf;
    const squid_orbit orbit{ phase };
    rv_pdklib::fx_vec3 rot{};
    const int rc = squid_orbit_tilt(tilt_x, tilt_z, rot);
    if (rc != RV_OK) {
        return rc;
    }
    out = squid_orbit_position(orbit, rot);
    return RV_OK;
}

// Sets hit when the orbit path of the step touches the sphere; hit is kept on error.
// from and to are consecutive poses of one simulation step; the phase only runs forward. A restart
// that moves the phase must not be swept: the caller starts from the new pose. Resetting squid_contact is the caller's job.
inline int
squid_collide_sweep(const squid_sweep &from, const squid_sweep &to, rv_pdklib::rv_vec3 center, float radius_sum, bool &hit)
{
    const bool bad_radius = !std::isfinite(radius_sum) || radius_sum <= 0.0f;
    if (bad_radius) {
        RV_LOG_ERR(squid_collide_tag, "sweep refused: radius sum {} is not positive and finite; hit kept", radius_sum);
        return RV_ERR_INVAL;
    }
    const bool bad_pose = !squid_collide_pose_finite(from) || !squid_collide_pose_finite(to);
    if (bad_pose) {
        RV_LOG_ERR(squid_collide_tag, "sweep refused: a pose value is not finite; hit kept");
        return RV_ERR_INVAL;
    }
    const double dphase = squid_collide_phase_delta(from.phase_rad, to.phase_rad);
    const double dx = static_cast<double>(to.tilt_x_deg) - from.tilt_x_deg;
    const double dz = static_cast<double>(to.tilt_z_deg) - from.tilt_z_deg;
    const double bound = SQUID_ORBIT_RADIUS * (dphase + (std::abs(dx) + std::abs(dz)) * SQUID_COLLIDE_DEG_TO_RAD);
    const double wanted = std::ceil(bound / (radius_sum * SQUID_COLLIDE_STEP_SHARE));
    const double max_steps = static_cast<double>(SQUID_COLLIDE_MAX_STEPS);
    if (wanted > max_steps) {
        RV_LOG_WARN(squid_collide_tag, "sweep capped at {} sub-steps, fast moves may skip", SQUID_COLLIDE_MAX_STEPS);
    }
    const int steps = static_cast<int>(std::clamp(wanted, 1.0, max_steps));
    rv_pdklib::rv_vec3 prev{};
    int rc = squid_collide_pose(from, to, dphase, 0.0, prev);
    if (rc != RV_OK) {
        return rc;
    }
    // Both ends are validated before the loop, so an out-of-range end is refused even if the path touches early.
    rv_pdklib::rv_vec3 end{};
    rc = squid_collide_pose(from, to, dphase, 1.0, end);
    if (rc != RV_OK) {
        return rc;
    }
    bool touched = false;
    for (int i = 1; i <= steps && !touched; ++i) {
        rv_pdklib::rv_vec3 cur{};
        rc = squid_collide_pose(from, to, dphase, static_cast<double>(i) / steps, cur);
        if (rc != RV_OK) {
            return rc;
        }
        touched = squid_collide_segment(prev, cur, center, radius_sum);
        prev = cur;
    }
    hit = touched;
    return RV_OK;
}

// True only on the frame where the contact starts; a contact that lasts does not repeat it.
inline bool squid_contact_enter(squid_contact &c, bool hit)
{
    const bool entered = hit && !c.touching;
    c.touching = hit;
    return entered;
}

} // namespace squid
