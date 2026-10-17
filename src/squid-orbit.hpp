#pragma once

// Alkoldun's kinematic orbit (docs/disdoc.md, section 6.1): constant angular speed around the Earth's centre.
// The position goes through rv_scene_local_trs with the world tilt, so it matches the drawn tilt root
// when that root sits at the origin with scale 1 (see squid_orbit_position).

#include <cmath>

#include "squid-params.hpp"
#include "pdk/rv_err.h"
#include "pdklib/rv_scene/rv_scene_space.hpp"
#include "pdklib/rv_math/rv_fx.hpp"
#include "pdklib/rv_logs/rv_logs.hpp"

namespace squid
{

// Full turn in radians; pdklib has no 2*pi constant.
constexpr double SQUID_ORBIT_TAU_RAD = 6.28318530717958647692;

// Log tag of orbit messages.
inline constexpr const char *squid_orbit_tag = "orbit";

struct squid_orbit {
    // Double: float rounding of the per-frame sum drifts by ~9e-3 rad per game hour.
    double phase_rad = 0.0; // in [0, 2pi)
};

// Advances the phase by dt seconds; dt = 0 is a pause. Negative or non-finite dt is refused and the phase stays.
inline int squid_orbit_step(squid_orbit &orbit, float dt)
{
    const bool bad = !std::isfinite(dt) || dt < 0.0f;
    if (bad) {
        RV_LOG_ERR(squid_orbit_tag, "step refused: dt {} s is negative or not finite; phase kept", dt);
        return RV_ERR_INVAL;
    }
    const double advanced = orbit.phase_rad + SQUID_ORBIT_TAU_RAD / SQUID_ORBIT_PERIOD_S * dt;
    orbit.phase_rad = std::fmod(advanced, SQUID_ORBIT_TAU_RAD);
    return RV_OK;
}

// Tilt angles in degrees to the Q20.12 rotation; rotation is untouched on error.
inline int squid_orbit_tilt(float tilt_x_deg, float tilt_z_deg, rv_pdklib::fx_vec3 &rotation)
{
    rv_pdklib::fx tilt_x{};
    rv_pdklib::fx tilt_z{};
    const int rc_x = rv_pdklib::fx_from_double(tilt_x_deg, tilt_x);
    if (rc_x != RV_OK) {
        RV_LOG_ERR(squid_orbit_tag, "tilt x {} deg is outside the Q20.12 range; rotation kept", tilt_x_deg);
        return rc_x;
    }
    const int rc_z = rv_pdklib::fx_from_double(tilt_z_deg, tilt_z);
    if (rc_z != RV_OK) {
        RV_LOG_ERR(squid_orbit_tag, "tilt z {} deg is outside the Q20.12 range; rotation kept", tilt_z_deg);
        return rc_z;
    }
    rotation = rv_pdklib::fx_vec3{ tilt_x, 0, tilt_z };
    return RV_OK;
}

// Orbit point (R cos phase, 0, R sin phase) turned by the world tilt.
// Precondition: the tilt root sits at the origin (Earth's centre), scale 1, no parent. Otherwise the orbit
// leaves the drawn tilt root.
inline rv_pdklib::rv_vec3 squid_orbit_position(const squid_orbit &orbit, rv_pdklib::fx_vec3 rotation)
{
    const rv_pdklib::fx_vec3 origin{};
    const rv_pdklib::fx_vec3 unit_scale{ rv_pdklib::fx_one, rv_pdklib::fx_one, rv_pdklib::fx_one };
    const rv_pdklib::rv_mat4 world = rv_pdklib::rv_scene_local_trs(origin, rotation, unit_scale);
    const float x = static_cast<float>(SQUID_ORBIT_RADIUS * std::cos(orbit.phase_rad));
    const float z = static_cast<float>(SQUID_ORBIT_RADIUS * std::sin(orbit.phase_rad));
    return rv_pdklib::rv_mat4_mul_point(world, rv_pdklib::rv_vec3{ x, 0.0f, z });
}

} // namespace squid
