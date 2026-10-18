#pragma once

// Physics of the flat disc: gravity from the game tilt, damping, speed cap, substeps, edge bounce, ball-ball bounce,
// split and absorb, and the display spin and hover phase that the contacts and substeps feed. The caller owns the
// body set and the frame clock; refusals are logged.

#include <algorithm>
#include <cmath>
#include <numbers>
#include <random>

#include "squid-beer.hpp"
#include "squid-body.hpp"
#include "squid-input.hpp"
#include "squid-params.hpp"
#include "pdk/rv_err.h"
#include "pdklib/rv_logs/rv_logs.hpp"

namespace squid
{

constexpr const char *SQUID_PHYSICS_TAG = "physics";

// Every ball has mass >= SQUID_BALL_MASS_MIN and the ball mass is conserved, so the start total bounds the balls: the
// start balls (mass SQUID_BALL_MASS_MAX each) and Alkoldun make 84 smallest units, so at most 84 balls. Bottles are
// outside that budget and never split or absorb; at most SQUID_BOTTLE_MAX of them live.
constexpr int SQUID_BODY_CAPACITY = SQUID_START_BALLS * SQUID_BALL_MASS_MAX + SQUID_ALKOLDUN_MASS + SQUID_BOTTLE_MAX;

// 60 Hz frame; a frame runs SQUID_SUBSTEPS equal substeps.
constexpr float SQUID_FRAME_S = 1.0f / 60.0f;

// Below this centre distance two balls have no contact direction; the x axis separates them.
constexpr float SQUID_PAIR_COINCIDENT_EPS = 1e-6f;

// Travel per substep must stay under a quarter of the smallest radius, so no ball skips past another.
static_assert(SQUID_SPEED_MAX * (SQUID_FRAME_S / SQUID_SUBSTEPS) < SQUID_BALL_RADIUS[0] / 4.0f,
    "speed cap times the substep must stay under a quarter of the smallest ball radius");

// Fixed-capacity body set; bodies 0..count-1 are live.
struct squid_bodies {
    squid_body items[SQUID_BODY_CAPACITY];
    int count = 0;
};

// Splits, absorbs and bounces, counted over a pass, a substep or a frame; the main loop plays one sound per frame, by priority,
// from them. A bounce is counted only at an approach of SQUID_BOUNCE_SOUND_MIN_SPEED or more. A bottle contact counts
// in bottle_bounces, not bounces; a bottle born at a fridge split counts in bottles_born. A drink counts in
// bottles_drunk, and in marked_drunk when the bottle is marked.
struct squid_event_counts {
    int splits = 0;
    int absorbs = 0;
    int bounces = 0;
    int bottles_born = 0;
    int bottle_bounces = 0;
    int bottles_drunk = 0;
    int marked_drunk = 0;
};

// A touching approaching pair with a split or an absorb, recorded in the pair pass and applied after its loops.
struct squid_pair_event {
    int i = 0;
    int j = 0;
    squid_pair_outcome out{};
    squid_flat normal{};
    bool alkoldun_hit = false;
};

// Acceleration of the disc from the game tilt: the x and z components of the world down vector in the tilted disc's
// axes, for R = Rx(tx) * Rz(tz) (rv_scene_local_trs with no yaw), times SQUID_GRAVITY. Zero tilt gives exactly zero.
inline squid_flat squid_gravity(squid_tilt tilt)
{
    const float tx = tilt.x_deg * SQUID_DEG_TO_RAD;
    const float tz = tilt.z_deg * SQUID_DEG_TO_RAD;
    return squid_flat{ -SQUID_GRAVITY * std::sin(tz) * std::cos(tx), SQUID_GRAVITY * std::sin(tx) };
}

// Radius of a body whose size squid_substep checked in the same substep; call it only after that check. Masses stay in
// SQUID_BALL_MASS_MIN..SQUID_BALL_MASS_MAX through split and absorb, so the check holds in every substep.
inline float squid_radius_checked(const squid_body &b)
{
    float radius = 0;
    squid_body_radius(b, radius);
    return radius;
}

// Standard normal sample by Box-Muller; each uniform is a draw over (max + 1), so it lies in (0, 1) and the log is finite.
inline float squid_normal(std::minstd_rand &rng)
{
    const double scale = static_cast<double>(rng.max()) + 1.0;
    const double u1 = static_cast<double>(rng()) / scale;
    const double u2 = static_cast<double>(rng()) / scale;
    return static_cast<float>(std::sqrt(-2.0 * std::log(u1)) * std::cos(2.0 * std::numbers::pi * u2));
}

// Exact Ornstein-Uhlenbeck step of the drift over h seconds: it relaxes to zero with time constant SQUID_DRIFT_TAU_S and
// keeps the stationary deviation SQUID_DRIFT_SIGMA / sqrt(mass), so heavier bodies drift less. One normal sample per
// axis, x then z; each sample takes two generator draws.
inline void squid_drift_step(squid_body &b, float h, std::minstd_rand &rng)
{
    const float decay = std::exp(-h / SQUID_DRIFT_TAU_S);
    const float sigma = SQUID_DRIFT_SIGMA / std::sqrt(static_cast<float>(b.mass));
    const float kick = sigma * std::sqrt(1.0f - decay * decay);
    b.drift.x = b.drift.x * decay + kick * squid_normal(rng);
    b.drift.z = b.drift.z * decay + kick * squid_normal(rng);
}

// Acceleration (tilt plus the body's drift), damping and speed cap, then the move by h seconds. The cap bounds this move
// only: an impulse in squid_pair_step may leave a body faster until its next move, where it is clamped; momentum is
// conserved within each collision, not across the cap.
inline void squid_body_move(squid_body &b, squid_flat accel, float h, float damping)
{
    b.vel.x = (b.vel.x + (accel.x + b.drift.x) * h) * damping;
    b.vel.z = (b.vel.z + (accel.z + b.drift.z) * h) * damping;
    const float speed = std::sqrt(b.vel.x * b.vel.x + b.vel.z * b.vel.z);
    const float cap = speed > SQUID_SPEED_MAX ? SQUID_SPEED_MAX / speed : 1.0f;
    b.vel.x *= cap;
    b.vel.z *= cap;
    b.pos.x += b.vel.x * h;
    b.pos.z += b.vel.z * h;
}

// Keeps a ball inside the disc, |pos| + r <= SQUID_DISC_RADIUS: it goes back onto the edge, and an outward normal
// speed reflects with SQUID_RIM_RESTITUTION and adds spin about y. The edge never splits, and never changes the body's
// mass or size.
// Called only after squid_substep's size check, so the radius is valid.
inline void squid_edge_step(squid_body &b)
{
    const float dist = std::sqrt(b.pos.x * b.pos.x + b.pos.z * b.pos.z);
    const float radius = squid_radius_checked(b);
    const float limit = SQUID_DISC_RADIUS - radius;
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
    // Tangential speed along t = (-nz, nx). The contact at +n * r slips at vt - spin * r; the kick removes that slip, so a
    // ball resting on the rim stops spinning up.
    const float vt = b.vel.z * nx - b.vel.x * nz;
    const float slip = vt - b.spin.y * radius;
    b.spin.y += SQUID_SPIN_IMPACT_GAIN * slip / radius;
    const float k = (1.0f + SQUID_RIM_RESTITUTION) * vn;
    b.vel = squid_flat{ b.vel.x - k * nx, b.vel.z - k * nz };
}

// Split of a ball in place: floor(m/2) keeps slot i and ceil(m/2) is appended. The halves touch along the tangent of the
// contact, keep the centre of mass, and move apart at SQUID_SPLIT_SEPARATION_SPEED shared inversely to mass, so the
// momentum of the parent is kept. Skipped with a warning at capacity: unreachable in practice (Alkoldun always exists, so
// at most 81 non-bottle bodies, 80 balls of mass 1 and Alkoldun, plus 16 bottles make 97 < 100), kept as a guard. An Alkoldun
// never gets here (its rows never split).
inline int squid_split_body(squid_bodies &set, int i, squid_flat normal)
{
    if (set.count >= SQUID_BODY_CAPACITY) {
        RV_LOG_WARN(SQUID_PHYSICS_TAG, "split skipped: the set is at capacity {}", SQUID_BODY_CAPACITY);
        return RV_ERR_NOMEM;
    }
    squid_body &parent = set.items[i];
    const int mass_lo = parent.mass / 2;
    const int mass_hi = parent.mass - mass_lo;
    int size_lo = 0;
    int size_hi = 0;
    if (squid_size_of_mass(mass_lo, size_lo) != RV_OK || squid_size_of_mass(mass_hi, size_hi) != RV_OK) {
        return RV_ERR_INVAL;
    }
    const float m = static_cast<float>(parent.mass);
    const float m_lo = static_cast<float>(mass_lo);
    const float m_hi = static_cast<float>(mass_hi);
    const float r_lo = SQUID_BALL_RADIUS[size_lo - 1];
    const float r_hi = SQUID_BALL_RADIUS[size_hi - 1];
    const float sum_r = r_lo + r_hi;
    // Distances from the centre of mass: m_lo * d_lo = m_hi * d_hi, so the centre of mass stays on the parent.
    const float d_lo = sum_r * m_hi / m;
    const float d_hi = sum_r * m_lo / m;
    // Relative tangential speed is SQUID_SPLIT_SEPARATION_SPEED; the speed offsets are inverse to the masses.
    const float w_lo = SQUID_SPLIT_SEPARATION_SPEED * m_hi / m;
    const float w_hi = SQUID_SPLIT_SEPARATION_SPEED * m_lo / m;
    const squid_flat t{ -normal.z, normal.x };
    const squid_flat c = parent.pos;
    const squid_flat v = parent.vel;
    squid_body hi = parent;
    hi.mass = mass_hi;
    hi.variant = (parent.variant + 1) % SQUID_MODEL_VARIANTS;
    hi.pos = squid_flat{ c.x + t.x * d_hi, c.z + t.z * d_hi };
    hi.vel = squid_flat{ v.x + t.x * w_hi, v.z + t.z * w_hi };
    // Both halves start from the parent's spin; the hi half takes +sep / r and the lo half -sep / r about y (a choice of
    // the split, not of the contact rule).
    hi.spin.y += SQUID_SPLIT_SEPARATION_SPEED / r_hi;
    // The hi half bobs half a period behind the lo half; both keep the parent's period.
    hi.hover_phase = std::fmod(parent.hover_phase + std::numbers::pi_v<float>, 2.0f * std::numbers::pi_v<float>);
    parent.mass = mass_lo;
    parent.pos = squid_flat{ c.x - t.x * d_lo, c.z - t.z * d_lo };
    parent.vel = squid_flat{ v.x - t.x * w_lo, v.z - t.z * w_lo };
    parent.spin.y -= SQUID_SPLIT_SEPARATION_SPEED / r_lo;
    set.items[set.count++] = hi;
    return RV_OK;
}

// Absorb: the kept body takes the gone body's mass and the centre-of-mass velocity of the pair; its position stays.
// The caller removes the gone body. The pair table keeps the mass within SQUID_BALL_MASS_MAX: only size-1 bodies (mass 1)
// are absorbed, and only into sizes 1..3 (masses up to 7).
inline void squid_absorb_body(squid_bodies &set, int keep, int gone)
{
    squid_body &k = set.items[keep];
    const squid_body &g = set.items[gone];
    const float mk = static_cast<float>(k.mass);
    const float mg = static_cast<float>(g.mass);
    const float total = mk + mg;
    k.vel = squid_flat{ (mk * k.vel.x + mg * g.vel.x) / total, (mk * k.vel.z + mg * g.vel.z) / total };
    k.mass += g.mass;
}

// Largest mass among the balls in the set; 0 with none.
inline int squid_heaviest_ball(const squid_bodies &set)
{
    int heaviest = 0;
    for (int i = 0; i < set.count; ++i) {
        if (set.items[i].kind == squid_kind::ball) {
            heaviest = std::max(heaviest, set.items[i].mass);
        }
    }
    return heaviest;
}

// Bottle of a fridge split at centre with velocity vel, with the hover of the split half at index from. Refused with the
// set unchanged at SQUID_BOTTLE_MAX bottles alive or at capacity; the split itself stands. The halves overlap it at
// birth by 0.225; the substep pair passes push it clear in about 12 substeps (1/20 s). Alkoldun drinks it on any touch.
inline int squid_bottle_birth(squid_bodies &set, int from, squid_flat centre, squid_flat vel, bool marked)
{
    int alive = 0;
    for (int i = 0; i < set.count; ++i) {
        alive += set.items[i].kind == squid_kind::bottle ? 1 : 0;
    }
    if (alive >= SQUID_BOTTLE_MAX) {
        RV_LOG_WARN(SQUID_PHYSICS_TAG, "bottle not born: {} alive, the limit is {}", alive, SQUID_BOTTLE_MAX);
        return RV_ERR_NOMEM;
    }
    if (set.count >= SQUID_BODY_CAPACITY) {
        RV_LOG_WARN(SQUID_PHYSICS_TAG, "bottle skipped: the set is at capacity {}", SQUID_BODY_CAPACITY);
        return RV_ERR_NOMEM;
    }
    squid_body bottle{};
    const int made = squid_body_bottle(bottle, centre, vel, marked);
    if (made != RV_OK) {
        return made;
    }
    bottle.hover_period = set.items[from].hover_period;
    bottle.hover_phase = set.items[from].hover_phase;
    set.items[set.count++] = bottle;
    return RV_OK;
}

// Split of the body at index i for event e. A fridge (size 4) split also gives a bottle at its centre, where the halves
// part, marked when Alkoldun's hit split the heaviest ball of the pass (heaviest, taken at the pass start).
inline void squid_event_split(squid_bodies &set, int i, const squid_pair_event &e, int heaviest, squid_event_counts &counts)
{
    const bool fridge = set.items[i].mass == SQUID_BALL_MASS_MAX;
    const bool marked = e.alkoldun_hit && set.items[i].mass == heaviest;
    // The split moves the parent to the lo half, so the centre and velocity are taken before it.
    const squid_flat centre = set.items[i].pos;
    const squid_flat vel = set.items[i].vel;
    if (squid_split_body(set, i, e.normal) != RV_OK) {
        return;
    }
    ++counts.splits;
    if (fridge && squid_bottle_birth(set, i, centre, vel, marked) == RV_OK) {
        ++counts.bottles_born;
    }
}

// Split-all split of the body at index i along normal. A fridge (size 4) also gives an unmarked bottle at the centre of
// the split, where the halves meet, with the parent's velocity; a refused bottle leaves the split standing.
inline int squid_split_all_split(squid_bodies &set, int i, squid_flat normal)
{
    const bool fridge = set.items[i].mass == SQUID_BALL_MASS_MAX;
    // The split moves the parent to the lo half, so the centre and velocity are taken before it.
    const squid_flat centre = set.items[i].pos;
    const squid_flat vel = set.items[i].vel;
    const int rc = squid_split_body(set, i, normal);
    if (rc != RV_OK) {
        return rc;
    }
    if (fridge) {
        squid_bottle_birth(set, i, centre, vel, false);
    }
    return RV_OK;
}

// Ball-ball contact over all pairs (O(n^2), n <= SQUID_BODY_CAPACITY). A touching pair is separated along the normal
// in inverse proportion to mass and, if approaching, bounces with SQUID_BALL_RESTITUTION; momentum is conserved.
// An approaching pair whose outcome splits or absorbs is recorded here and applied after the loops, so indices do not
// shift inside them; an absorbing pair is neither separated nor bounced. A body takes part in one event per pass, the first in
// i/j order; later events with it are dropped and the pair bounces. Called only after squid_substep's size check, so every body
// has a radius. A touching Alkoldun and bottle is a drink: the bottle is removed and the pair takes no response.
inline void squid_pair_step(squid_bodies &set, squid_event_counts &counts)
{
    bool taken[SQUID_BODY_CAPACITY] = {};
    bool removed[SQUID_BODY_CAPACITY] = {};
    // Two bodies per event and one event per body per pass: capacity / 2 events always fit.
    squid_pair_event events[SQUID_BODY_CAPACITY / 2]{};
    int event_count = 0;
    const int heaviest = squid_heaviest_ball(set);
    for (int i = 0; i < set.count; ++i) {
        for (int j = i + 1; j < set.count; ++j) {
            // A drunk bottle is still in the set until the compaction; it takes no more contacts this pass.
            if (removed[i] || removed[j]) {
                continue;
            }
            squid_body &a = set.items[i];
            squid_body &b = set.items[j];
            const float dx = b.pos.x - a.pos.x;
            const float dz = b.pos.z - a.pos.z;
            const float dist = std::sqrt(dx * dx + dz * dz);
            const float ra = squid_radius_checked(a);
            const float rb = squid_radius_checked(b);
            const float reach = ra + rb;
            if (dist >= reach) {
                continue;
            }
            // Touching Alkoldun and bottle: the bottle is drunk, with no push and no bounce on either body.
            if (squid_is_drink(a, b)) {
                const int bottle = a.kind == squid_kind::bottle ? i : j;
                removed[bottle] = true;
                ++counts.bottles_drunk;
                counts.marked_drunk += set.items[bottle].marked ? 1 : 0;
                continue;
            }
            const bool coincident = dist < SQUID_PAIR_COINCIDENT_EPS;
            const float nx = coincident ? 1.0f : dx / dist;
            const float nz = coincident ? 0.0f : dz / dist;
            const float wa = 1.0f / static_cast<float>(a.mass);
            const float wb = 1.0f / static_cast<float>(b.mass);
            const float vn = (b.vel.x - a.vel.x) * nx + (b.vel.z - a.vel.z) * nz;
            const bool approaching = vn < 0.0f;
            squid_pair_outcome out{};
            const bool resolved = approaching && squid_pair_resolve(a, b, -vn, out) == RV_OK;
            const bool free_pair = !taken[i] && !taken[j];
            const bool has_event = resolved && free_pair && (out.split_a || out.split_b || out.a_absorbed || out.b_absorbed);
            if (has_event) {
                const bool alkoldun_hit = a.kind == squid_kind::alkoldun || b.kind == squid_kind::alkoldun;
                events[event_count++] = squid_pair_event{ i, j, out, squid_flat{ nx, nz }, alkoldun_hit };
                taken[i] = true;
                taken[j] = true;
            }
            const bool absorbed = has_event && (out.a_absorbed || out.b_absorbed);
            if (absorbed) {
                continue;
            }
            const float push = reach - dist;
            a.pos.x -= nx * push * wa / (wa + wb);
            a.pos.z -= nz * push * wa / (wa + wb);
            b.pos.x += nx * push * wb / (wa + wb);
            b.pos.z += nz * push * wb / (wa + wb);
            if (!approaching) {
                continue;
            }
            // A resolved, approaching pair with no split or absorb is a plain bounce; a split pair is counted by its split.
            if (resolved && !has_event && -vn >= SQUID_BOUNCE_SOUND_MIN_SPEED) {
                const bool bottle_pair = a.kind == squid_kind::bottle || b.kind == squid_kind::bottle;
                if (bottle_pair) {
                    ++counts.bottle_bounces;
                } else {
                    ++counts.bounces;
                }
            }
            // The slide turns into rolling: a and b share one constraint, the slip at the contact. Each spin takes half of
            // the kick that removes it (gain 1), so both keep the same sense about y.
            const float slide = (b.vel.z - a.vel.z) * nx - (b.vel.x - a.vel.x) * nz;
            const float slip = -slide - a.spin.y * ra - b.spin.y * rb;
            a.spin.y += SQUID_SPIN_IMPACT_GAIN * slip / (2.0f * ra);
            b.spin.y += SQUID_SPIN_IMPACT_GAIN * slip / (2.0f * rb);
            const float impulse = -(1.0f + SQUID_BALL_RESTITUTION) * vn / (wa + wb);
            a.vel.x -= impulse * wa * nx;
            a.vel.z -= impulse * wa * nz;
            b.vel.x += impulse * wb * nx;
            b.vel.z += impulse * wb * nz;
        }
    }
    for (int k = 0; k < event_count; ++k) {
        const squid_pair_event &e = events[k];
        if (e.out.a_absorbed || e.out.b_absorbed) {
            const int gone = e.out.a_absorbed ? e.i : e.j;
            const int keep = e.out.a_absorbed ? e.j : e.i;
            squid_absorb_body(set, keep, gone);
            removed[gone] = true;
            ++counts.absorbs;
            continue;
        }
        if (e.out.split_a) {
            squid_event_split(set, e.i, e, heaviest, counts);
        }
        if (e.out.split_b) {
            squid_event_split(set, e.j, e, heaviest, counts);
        }
    }
    // Stable compaction: the survivors keep their order, so a later pass pairs them the same way every run.
    int kept = 0;
    for (int k = 0; k < set.count; ++k) {
        if (!removed[k]) {
            set.items[kept++] = set.items[k];
        }
    }
    set.count = kept;
}

// Display spin and hover over h seconds: rot turns by spin * h (exact axis-angle step, renormalised), the spin decays
// with SQUID_SPIN_DECAY_S, and the hover phase advances by 2 pi h / period. Motion never reads these.
inline void squid_motion_step(squid_body &b, float h)
{
    const float rate = std::sqrt(b.spin.x * b.spin.x + b.spin.y * b.spin.y + b.spin.z * b.spin.z);
    const float angle = rate * h;
    // Zero spin gives the identity turn: the axis scale is taken as zero, not divided by zero.
    const float scale = rate > 0.0f ? std::sin(angle / 2.0f) / rate : 0.0f;
    const rv_pdklib::rv_vec4 turn{ b.spin.x * scale, b.spin.y * scale, b.spin.z * scale, std::cos(angle / 2.0f) };
    const rv_pdklib::rv_vec4 turned = squid_quat_mul(turn, b.rot);
    const float norm = std::sqrt(turned.x * turned.x + turned.y * turned.y + turned.z * turned.z + turned.w * turned.w);
    b.rot = rv_pdklib::rv_vec4{ turned.x / norm, turned.y / norm, turned.z / norm, turned.w / norm };
    const float decay = std::exp(-h / SQUID_SPIN_DECAY_S);
    b.spin.x *= decay;
    b.spin.y *= decay;
    b.spin.z *= decay;
    const float full_turn = 2.0f * std::numbers::pi_v<float>;
    b.hover_phase = std::fmod(b.hover_phase + full_turn * h / b.hover_period, full_turn);
}

// One substep of h seconds for every body: move, pairs, then the edge and the spin and hover last, so no body ends
// outside the disc. Count and sizes are checked first; a refusal logs its reason and moves nothing. Splits and absorbs
// add to counts. Each body's drift advances here, in index order, from the generator rng.
inline int squid_substep(squid_bodies &set, squid_flat accel, float h, squid_event_counts &counts, std::minstd_rand &rng)
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
        squid_drift_step(set.items[i], h, rng);
        squid_body_move(set.items[i], accel, h, damping);
    }
    squid_pair_step(set, counts);
    for (int i = 0; i < set.count; ++i) {
        squid_edge_step(set.items[i]);
        squid_motion_step(set.items[i], h);
    }
    return RV_OK;
}

// One frame of dt seconds as SQUID_SUBSTEPS substeps; counts holds this frame's splits and absorbs. Refused with state
// untouched on a non-positive or non-finite dt or a non-finite tilt. A count or size refusal comes from the first
// substep, before any move. Later substeps cannot refuse: masses stay in 1..8 and their total is at most 84, which
// bounds the count, so no refusal is reachable mid-frame. The drift draws come from rng, which the round owns.
inline int squid_physics_frame(squid_bodies &set, squid_tilt tilt, float dt, squid_event_counts &counts, std::minstd_rand &rng)
{
    counts = squid_event_counts{};
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
        const int rc = squid_substep(set, accel, h, counts, rng);
        if (rc != RV_OK) {
            return rc;
        }
    }
    return RV_OK;
}

} // namespace squid
