#pragma once

// Physics of the flat disc: gravity from the game tilt, damping, speed cap, substeps, edge bounce and ball-ball bounce.
// The caller owns the body set and the frame clock; refusals are logged.

#include <cmath>
#include <numbers>

#include "squid-body.hpp"
#include "squid-input.hpp"
#include "squid-params.hpp"
#include "pdk/rv_err.h"
#include "pdklib/rv_logs/rv_logs.hpp"

namespace squid
{

constexpr const char *SQUID_PHYSICS_TAG = "physics";

// Every body has mass >= SQUID_BALL_MASS_MIN and the total mass is conserved, so the start total bounds the count:
// the start balls (mass SQUID_BALL_MASS_MAX each) and Alkoldun make 84 smallest units, so at most 84 bodies.
constexpr int SQUID_BODY_CAPACITY = SQUID_START_BALLS * SQUID_BALL_MASS_MAX + SQUID_ALKOLDUN_MASS;

// 60 Hz frame; a frame runs SQUID_SUBSTEPS equal substeps.
constexpr float SQUID_FRAME_S = 1.0f / 60.0f;

// Below this centre distance two balls have no contact direction; the x axis separates them.
constexpr float SQUID_PAIR_COINCIDENT_EPS = 1e-6f;

constexpr float SQUID_DEG_TO_RAD = std::numbers::pi_v<float> / 180.0f;

// Travel per substep must stay under a quarter of the smallest radius, so no ball skips past another.
static_assert(SQUID_SPEED_MAX * (SQUID_FRAME_S / SQUID_SUBSTEPS) < SQUID_BALL_RADIUS[0] / 4.0f,
    "speed cap times the substep must stay under a quarter of the smallest ball radius");

// Fixed-capacity body set; bodies 0..count-1 are live.
struct squid_bodies {
    squid_body items[SQUID_BODY_CAPACITY];
    int count = 0;
};

// Acceleration of the disc from the game tilt: the x and z components of the world down vector in the tilted disc's
// axes, for R = Rx(tx) * Rz(tz) (rv_scene_local_trs with no yaw), times SQUID_GRAVITY. Zero tilt gives exactly zero.
inline squid_flat squid_gravity(squid_tilt tilt)
{
    const float tx = tilt.x_deg * SQUID_DEG_TO_RAD;
    const float tz = tilt.z_deg * SQUID_DEG_TO_RAD;
    return squid_flat{ -SQUID_GRAVITY * std::sin(tz) * std::cos(tx), SQUID_GRAVITY * std::sin(tx) };
}

// Radius of a body whose size squid_substep checked in the same substep; call it only after that check. Split and
// absorb (next task) change masses only after the pair loops, and only within SQUID_BALL_MASS_MIN..SQUID_BALL_MASS_MAX.
inline float squid_radius_checked(const squid_body &b)
{
    float radius = 0;
    squid_body_radius(b, radius);
    return radius;
}

// Acceleration, damping and speed cap, then the move by h seconds. The cap bounds this move only: an impulse in
// squid_pair_step may leave a body faster until its next move, where it is clamped; momentum is conserved within each
// collision, not across the cap.
inline void squid_body_move(squid_body &b, squid_flat accel, float h, float damping)
{
    b.vel.x = (b.vel.x + accel.x * h) * damping;
    b.vel.z = (b.vel.z + accel.z * h) * damping;
    const float speed = std::sqrt(b.vel.x * b.vel.x + b.vel.z * b.vel.z);
    const float cap = speed > SQUID_SPEED_MAX ? SQUID_SPEED_MAX / speed : 1.0f;
    b.vel.x *= cap;
    b.vel.z *= cap;
    b.pos.x += b.vel.x * h;
    b.pos.z += b.vel.z * h;
}

// Keeps a ball inside the disc, |pos| + r <= SQUID_DISC_RADIUS: it goes back onto the edge, and an outward normal
// speed reflects with SQUID_RIM_RESTITUTION. The edge never splits or changes the body.
inline void squid_edge_step(squid_body &b)
{
    const float dist = std::sqrt(b.pos.x * b.pos.x + b.pos.z * b.pos.z);
    const float limit = SQUID_DISC_RADIUS - squid_radius_checked(b);
    if (dist <= limit) {
        return;
    }
    const float nx = b.pos.x / dist;
    const float nz = b.pos.z / dist;
    b.pos = squid_flat{ nx * limit, nz * limit };
    const float vn = b.vel.x * nx + b.vel.z * nz;
    if (vn <= 0.0f) {
        return;
    }
    const float k = (1.0f + SQUID_RIM_RESTITUTION) * vn;
    b.vel = squid_flat{ b.vel.x - k * nx, b.vel.z - k * nz };
}

// Ball-ball contact over all pairs (O(n^2), n <= SQUID_BODY_CAPACITY). A touching pair is separated along the normal
// in inverse proportion to mass and, if approaching, bounces with SQUID_BALL_RESTITUTION; momentum is conserved.
// Split and absorb extend this function: they need the normal relative speed vn, computed here before the impulse.
inline void squid_pair_step(squid_bodies &set)
{
    for (int i = 0; i < set.count; ++i) {
        for (int j = i + 1; j < set.count; ++j) {
            squid_body &a = set.items[i];
            squid_body &b = set.items[j];
            const float dx = b.pos.x - a.pos.x;
            const float dz = b.pos.z - a.pos.z;
            const float dist = std::sqrt(dx * dx + dz * dz);
            const float reach = squid_radius_checked(a) + squid_radius_checked(b);
            if (dist >= reach) {
                continue;
            }
            const bool coincident = dist < SQUID_PAIR_COINCIDENT_EPS;
            const float nx = coincident ? 1.0f : dx / dist;
            const float nz = coincident ? 0.0f : dz / dist;
            const float ma = static_cast<float>(a.mass);
            const float mb = static_cast<float>(b.mass);
            const float push = reach - dist;
            a.pos.x -= nx * push * mb / (ma + mb);
            a.pos.z -= nz * push * mb / (ma + mb);
            b.pos.x += nx * push * ma / (ma + mb);
            b.pos.z += nz * push * ma / (ma + mb);
            const float vn = (b.vel.x - a.vel.x) * nx + (b.vel.z - a.vel.z) * nz;
            if (vn >= 0.0f) {
                continue;
            }
            const float impulse = -(1.0f + SQUID_BALL_RESTITUTION) * vn / (1.0f / ma + 1.0f / mb);
            a.vel.x -= impulse / ma * nx;
            a.vel.z -= impulse / ma * nz;
            b.vel.x += impulse / mb * nx;
            b.vel.z += impulse / mb * nz;
        }
    }
}

// One substep of h seconds for every body: move, pairs, then the edge last, so no body ends outside the disc.
// Count and sizes are checked first; a refusal logs its reason and moves nothing.
inline int squid_substep(squid_bodies &set, squid_flat accel, float h)
{
    const bool count_bad = set.count < 0 || set.count > SQUID_BODY_CAPACITY;
    if (count_bad) {
        RV_LOG_ERR(SQUID_PHYSICS_TAG, "substep refused: count {} is outside 0..{}", set.count, SQUID_BODY_CAPACITY);
        return RV_ERR_INVAL;
    }
    for (int i = 0; i < set.count; ++i) {
        float radius = 0;
        if (squid_body_radius(set.items[i], radius) != RV_OK) {
            RV_LOG_ERR(SQUID_PHYSICS_TAG, "substep refused: body {} has no size", i);
            return RV_ERR_INVAL;
        }
    }
    const float damping = std::exp(-SQUID_DAMPING_PER_S * h);
    for (int i = 0; i < set.count; ++i) {
        squid_body_move(set.items[i], accel, h, damping);
    }
    squid_pair_step(set);
    for (int i = 0; i < set.count; ++i) {
        squid_edge_step(set.items[i]);
    }
    return RV_OK;
}

// One frame of dt seconds as SQUID_SUBSTEPS substeps. Refused with state untouched on a non-positive or non-finite dt
// or a non-finite tilt. A count or size refusal comes from the first substep, before any move: masses and count do not
// change within a frame.
inline int squid_physics_frame(squid_bodies &set, squid_tilt tilt, float dt)
{
    const bool dt_bad = !std::isfinite(dt) || dt <= 0.0f;
    if (dt_bad) {
        RV_LOG_ERR(SQUID_PHYSICS_TAG, "frame refused: dt {} is not a positive finite number", dt);
        return RV_ERR_INVAL;
    }
    const bool tilt_bad = !std::isfinite(tilt.x_deg) || !std::isfinite(tilt.z_deg);
    if (tilt_bad) {
        RV_LOG_ERR(SQUID_PHYSICS_TAG, "frame refused: tilt ({}, {}) is not finite", tilt.x_deg, tilt.z_deg);
        return RV_ERR_INVAL;
    }
    const squid_flat accel = squid_gravity(tilt);
    const float h = dt / SQUID_SUBSTEPS;
    for (int step = 0; step < SQUID_SUBSTEPS; ++step) {
        const int rc = squid_substep(set, accel, h);
        if (rc != RV_OK) {
            return rc;
        }
    }
    return RV_OK;
}

} // namespace squid
