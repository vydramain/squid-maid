#pragma once

// Ball body of the squid game: the body struct, the size table and the pair outcome
// table. Refusals are logged here; the physics step reads position, velocity and mass for motion, never the display
// rotation, spin or hover.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <utility>

#include "squid-params.hpp"
#include "pdk/rv_err.h"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "pdklib/rv_math/rv_math.hpp"

namespace squid
{

inline constexpr const char *SQUID_BODY_TAG = "body";

// Mass of a ball in smallest units; the size table below is indexed by mass.
constexpr int SQUID_BALL_MASS_MIN = 1;
constexpr int SQUID_BALL_MASS_MAX = 8;

// Size by mass, index = mass: 1 -> 1, 2-3 -> 2, 4-7 -> 3, 8 -> 4. Index 0 is unused.
inline constexpr int SQUID_SIZE_BY_MASS[SQUID_BALL_MASS_MAX + 1] = { 0, 1, 2, 2, 3, 3, 3, 3, 4 };

// Every size in the table needs a radius.
static_assert(std::size(SQUID_BALL_RADIUS) == static_cast<std::size_t>(std::ranges::max(SQUID_SIZE_BY_MASS)));

// Size of Alkoldun: its radius and thresholds follow size 3; it never splits, and nothing absorbs into it.
constexpr int SQUID_ALKOLDUN_SIZE = 3;

// Mass of a bottle in collisions; bottles are outside the ball mass budget, so this never enters a total.
constexpr int SQUID_BOTTLE_MASS = 1;

// Model variants chosen at birth, 0..4 (the largest size's count); display only. A smaller size takes the variant
// modulo its own count (squid-view.hpp).
constexpr int SQUID_MODEL_VARIANTS = 5;

// Display rotation at birth: the identity quaternion (x, y, z, w).
inline constexpr rv_pdklib::rv_vec4 SQUID_IDENTITY_QUAT{ 0, 0, 0, 1 };

enum class squid_kind : uint8_t {
    ball,
    alkoldun,
    bottle
};

// Position or velocity on the flat disc; x and z are the world X and Z axes.
struct squid_flat {
    float x = 0;
    float z = 0;
};

struct squid_body {
    squid_kind kind = squid_kind::ball;
    int mass = 0;
    int variant = 0;
    squid_flat pos{};
    squid_flat vel{};
    // Display rotation, identity at birth; physics never reads it.
    rv_pdklib::rv_vec4 rot = SQUID_IDENTITY_QUAT;
    // Random acceleration per axis in units/s^2, advanced by the physics step; zero at birth.
    squid_flat drift{};
    // Spin in rad/s in the dome frame (pose_yaw space: x, z on the disc plane, y up); the physics step turns rot by it
    // and decays it.
    rv_pdklib::rv_vec3 spin{};
    // Hover phase in rad and its period in s; the physics step advances the phase, the view reads it.
    float hover_phase = 0;
    float hover_period = SQUID_HOVER_PERIOD_MIN_S;
    // Bottles only: set at birth when Alkoldun's hit split the heaviest ball; no ball reads it.
    bool marked = false;
};

// Outcome of a touching pair: which bodies split, and which one is absorbed. Every touching pair that is not an
// absorb or a drink gets the elastic response in the physics step; split flags apply in addition, the division itself is the
// physics step's. An absorbed body is removed and its mass (always 1) goes to the other, so a mass never exceeds 8.
struct squid_pair_outcome {
    bool split_a = false;
    bool split_b = false;
    bool a_absorbed = false;
    bool b_absorbed = false;
};

// What a pair does below its threshold (row.below) or at or above it (row.at_or_above).
// Roles: lo is the smaller size (on a tie the non-Alkoldun), hi the larger.
enum class squid_pair_act : uint8_t {
    bounce,
    absorb_lo,
    split_lo,
    split_hi,
    split_both
};

struct squid_pair_row {
    int lo;
    int hi;
    bool lo_alk;
    bool hi_alk;
    squid_pair_act below;
    squid_pair_act at_or_above;
};

// Threshold: SQUID_SPLIT_SPEED for equal sizes, times SQUID_SPLIT_UNEQUAL_FACTOR otherwise. Every pair of sizes
// 1..4 and Alkoldun has one row.
inline constexpr squid_pair_row SQUID_PAIR_ROWS[] = {
    { 1, 1, false, false, squid_pair_act::absorb_lo, squid_pair_act::bounce },
    { 1, 2, false, false, squid_pair_act::absorb_lo, squid_pair_act::split_hi },
    { 1, 3, false, false, squid_pair_act::absorb_lo, squid_pair_act::split_hi },
    { 1, 4, false, false, squid_pair_act::bounce, squid_pair_act::split_hi },
    { 2, 2, false, false, squid_pair_act::bounce, squid_pair_act::split_both },
    { 2, 3, false, false, squid_pair_act::bounce, squid_pair_act::split_hi },
    { 2, 4, false, false, squid_pair_act::bounce, squid_pair_act::split_hi },
    { 3, 3, false, false, squid_pair_act::bounce, squid_pair_act::split_both },
    { 3, 4, false, false, squid_pair_act::bounce, squid_pair_act::split_hi },
    { 4, 4, false, false, squid_pair_act::bounce, squid_pair_act::split_both },
    { 1, 3, false, true, squid_pair_act::bounce, squid_pair_act::bounce },
    { 2, 3, false, true, squid_pair_act::bounce, squid_pair_act::bounce },
    { 3, 3, false, true, squid_pair_act::bounce, squid_pair_act::split_lo },
    { 3, 4, true, false, squid_pair_act::bounce, squid_pair_act::split_hi },
    { 3, 3, true, true, squid_pair_act::bounce, squid_pair_act::bounce },
};

// Size of a ball by its mass; refused outside SQUID_BALL_MASS_MIN..SQUID_BALL_MASS_MAX.
inline int squid_size_of_mass(int mass, int &size)
{
    const bool mass_bad = mass < SQUID_BALL_MASS_MIN || mass > SQUID_BALL_MASS_MAX;
    if (mass_bad) {
        RV_LOG_ERR(SQUID_BODY_TAG, "mass {} is outside {}..{}", mass, SQUID_BALL_MASS_MIN, SQUID_BALL_MASS_MAX);
        return RV_ERR_INVAL;
    }
    size = SQUID_SIZE_BY_MASS[mass];
    return RV_OK;
}

// Alkoldun is always SQUID_ALKOLDUN_SIZE; a ball's size follows its mass. A bottle has no size and is refused
// quietly: its radius and contacts are its own (squid_body_radius, squid_pair_resolve).
inline int squid_body_size(const squid_body &b, int &size)
{
    if (b.kind == squid_kind::alkoldun) {
        size = SQUID_ALKOLDUN_SIZE;
        return RV_OK;
    }
    if (b.kind == squid_kind::bottle) {
        return RV_ERR_INVAL;
    }
    return squid_size_of_mass(b.mass, size);
}

// Radius from the size: SQUID_BALL_RADIUS[size - 1]; a bottle has SQUID_BOTTLE_RADIUS.
inline int squid_body_radius(const squid_body &b, float &radius)
{
    if (b.kind == squid_kind::bottle) {
        radius = SQUID_BOTTLE_RADIUS;
        return RV_OK;
    }
    int size = 0;
    if (squid_body_size(b, size) != RV_OK) {
        return RV_ERR_INVAL;
    }
    radius = SQUID_BALL_RADIUS[size - 1];
    return RV_OK;
}

// Shared birth: refuses a variant outside 0..SQUID_MODEL_VARIANTS - 1; the caller has checked the mass.
inline int squid_body_birth(squid_body &out, squid_kind kind, int mass, int variant, squid_flat pos)
{
    const bool variant_bad = variant < 0 || variant >= SQUID_MODEL_VARIANTS;
    if (variant_bad) {
        RV_LOG_ERR(SQUID_BODY_TAG, "birth refused: variant {} is outside 0..{}", variant, SQUID_MODEL_VARIANTS - 1);
        return RV_ERR_INVAL;
    }
    out = squid_body{ kind, mass, variant, pos, squid_flat{}, SQUID_IDENTITY_QUAT };
    return RV_OK;
}

// A ball of the given mass; refused if the mass or the variant is out of range.
inline int squid_body_ball(squid_body &out, int mass, int variant, squid_flat pos)
{
    int size = 0;
    if (squid_size_of_mass(mass, size) != RV_OK) {
        return RV_ERR_INVAL;
    }
    return squid_body_birth(out, squid_kind::ball, mass, variant, pos);
}

// Alkoldun with its fixed mass SQUID_ALKOLDUN_MASS; refused if the variant is out of range.
inline int squid_body_alkoldun(squid_body &out, int variant, squid_flat pos)
{
    return squid_body_birth(out, squid_kind::alkoldun, SQUID_ALKOLDUN_MASS, variant, pos);
}

// A bottle at pos moving at vel, marked or not; mass SQUID_BOTTLE_MASS and variant 0 (display only).
inline int squid_body_bottle(squid_body &out, squid_flat pos, squid_flat vel, bool marked)
{
    const int made = squid_body_birth(out, squid_kind::bottle, SQUID_BOTTLE_MASS, 0, pos);
    if (made != RV_OK) {
        return made;
    }
    out.vel = vel;
    out.marked = marked;
    return RV_OK;
}

// Row of a sorted size pair; null if there is none.
inline const squid_pair_row *squid_pair_find(int lo, int hi, bool lo_alk, bool hi_alk)
{
    for (const squid_pair_row &row : SQUID_PAIR_ROWS) {
        if (row.lo == lo && row.hi == hi && row.lo_alk == lo_alk && row.hi_alk == hi_alk) {
            return &row;
        }
    }
    return nullptr;
}

// Outcome of two touching bodies. v is the approach speed (units/s, >= 0), for approaching contacts only.
// A refusal returns RV_ERR_INVAL with all flags false. Flags follow the order of a and b; on equal sizes a is
// the lo (the absorbed one in 1-1), so the caller pairs in a deterministic order.
inline int squid_pair_resolve(const squid_body &a, const squid_body &b, float v, squid_pair_outcome &out)
{
    out = squid_pair_outcome{};
    const bool speed_bad = !std::isfinite(v) || v < 0.0f;
    if (speed_bad) {
        RV_LOG_ERR(SQUID_BODY_TAG, "pair refused: relative speed {} is negative or not finite", v);
        return RV_ERR_INVAL;
    }
    // A bottle never splits or absorbs, so the table lists no row for it: a contact with it bounces; Alkoldun's contact
    // with it is a drink, checked before this.
    const bool bottle_pair = a.kind == squid_kind::bottle || b.kind == squid_kind::bottle;
    if (bottle_pair) {
        return RV_OK;
    }
    int size_a = 0;
    int size_b = 0;
    if (squid_body_size(a, size_a) != RV_OK || squid_body_size(b, size_b) != RV_OK) {
        return RV_ERR_INVAL;
    }
    // Sorted roles: the smaller size first; on a tie the non-Alkoldun. The rows guarantee Alkoldun is never split;
    // the tie rule only picks its role.
    const bool a_alk = a.kind == squid_kind::alkoldun;
    const bool b_alk = b.kind == squid_kind::alkoldun;
    const bool a_is_lo = std::make_pair(size_a, a_alk) <= std::make_pair(size_b, b_alk);
    const int size_lo = a_is_lo ? size_a : size_b;
    const int size_hi = a_is_lo ? size_b : size_a;
    const squid_pair_row *row = squid_pair_find(size_lo, size_hi, a_is_lo ? a_alk : b_alk, a_is_lo ? b_alk : a_alk);
    if (row == nullptr) {
        RV_LOG_ERR(SQUID_BODY_TAG, "pair refused: no outcome row for sizes {} and {}", size_lo, size_hi);
        return RV_ERR_INVAL;
    }
    const float threshold = size_lo == size_hi ? SQUID_SPLIT_SPEED : SQUID_SPLIT_SPEED * SQUID_SPLIT_UNEQUAL_FACTOR;
    const squid_pair_act act = v < threshold ? row->below : row->at_or_above;
    const bool splits_lo = act == squid_pair_act::split_lo || act == squid_pair_act::split_both;
    const bool splits_hi = act == squid_pair_act::split_hi || act == squid_pair_act::split_both;
    const bool absorbs_lo = act == squid_pair_act::absorb_lo;
    out.split_a = a_is_lo ? splits_lo : splits_hi;
    out.split_b = a_is_lo ? splits_hi : splits_lo;
    out.a_absorbed = absorbs_lo && a_is_lo;
    out.b_absorbed = absorbs_lo && !a_is_lo;
    return RV_OK;
}

} // namespace squid
