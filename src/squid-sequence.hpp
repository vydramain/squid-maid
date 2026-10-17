#pragma once

// Target sequence of a round (docs/disdoc.md, sections 8.1, 9.1, 9.2). Each target is a tilt and orbit phase pair,
// so its position is a point of the orbit and is reachable by construction. Pairs are picked by hand: target 1 on
// the neutral orbit, target 2 only x, target 3 only z, 4-8 both axes with the tilt changing between targets.
// squid_sequence_check verifies the gaps below.

#include <array>

#include "squid-orbit.hpp"
#include "squid-params.hpp"
#include "pdk/rv_err.h"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "pdklib/rv_math/rv_math.hpp"

namespace squid
{

// Log tag of sequence messages.
inline constexpr const char *squid_sequence_tag = "sequence";

// Active index of no target: the free mode and the finished round.
constexpr int SQUID_SEQUENCE_NONE = -1;

// Two targets must not overlap: centres more than two debris radii apart.
constexpr float SQUID_TARGET_OVERLAP_GAP = 2.0f * SQUID_DEBRIS_RADIUS;

// Consecutive targets and the start: the touch distance (Alkoldun + debris) times 2. The gap keeps the ball at a hit
// point out of the next target. A move inside one frame is not covered: squid-state does not count the next target in
// the frame of a hit and needs a new contact.
constexpr float SQUID_TARGET_NEXT_GAP = 2.0f * (SQUID_ALKOLDUN_RADIUS + SQUID_DEBRIS_RADIUS);

struct squid_target {
    float tilt_x_deg;
    float tilt_z_deg;
    double phase_rad;
    rv_pdklib::rv_vec3 position;
};

using squid_targets = std::array<squid_target, SQUID_TARGET_COUNT>;

struct squid_target_pair {
    float tilt_x_deg;
    float tilt_z_deg;
    double phase_rad;
};

// Tilt pairs and orbit phases in sequence order; phases are fractions of the turn. Axes per target follow 9.2; phases
// are all distinct; squid_sequence_check verifies the gaps. Phase 0 is the start, so no target sits on it.
inline constexpr std::array<squid_target_pair, SQUID_TARGET_COUNT> SQUID_TARGET_PAIRS{ {
    { 0.0f, 0.0f, SQUID_ORBIT_TAU_RAD * 0.25 },
    { 20.0f, 0.0f, SQUID_ORBIT_TAU_RAD * 0.75 },
    { 0.0f, -20.0f, SQUID_ORBIT_TAU_RAD * 0.5 },
    { -25.0f, 15.0f, SQUID_ORBIT_TAU_RAD * 0.625 },
    { 30.0f, -10.0f, SQUID_ORBIT_TAU_RAD * 0.125 },
    { -15.0f, -30.0f, SQUID_ORBIT_TAU_RAD * 0.875 },
    { 25.0f, 25.0f, SQUID_ORBIT_TAU_RAD * 0.375 },
    { -30.0f, 5.0f, SQUID_ORBIT_TAU_RAD * 0.7 },
} };

// Round progress: hits counts the taken targets; active is the target to hit next, or SQUID_SEQUENCE_NONE.
struct squid_sequence {
    int active = 0;
    int hits = 0;
};

// Fills out with the target positions on the orbit; an orbit error is returned as is.
inline int squid_sequence_build(squid_targets &out)
{
    for (int i = 0; i < SQUID_TARGET_COUNT; ++i) {
        const squid_target_pair &pair = SQUID_TARGET_PAIRS[i];
        rv_pdklib::fx_vec3 rotation{};
        const int rc = squid_orbit_tilt(pair.tilt_x_deg, pair.tilt_z_deg, rotation);
        if (rc != RV_OK) {
            return rc;
        }
        const squid_orbit orbit{ pair.phase_rad };
        out[i] = squid_target{ pair.tilt_x_deg, pair.tilt_z_deg, pair.phase_rad, squid_orbit_position(orbit, rotation) };
    }
    return RV_OK;
}

// Refuses the set when two targets are too close, or the first one is within reach of the start: Alkoldun starts
// at phase 0 on the neutral orbit. The log names the pair or the start and the distance.
inline int squid_sequence_check(const squid_targets &t)
{
    const squid_orbit start{};
    const rv_pdklib::rv_vec3 start_pos = squid_orbit_position(start, rv_pdklib::fx_vec3{});
    const float start_gap = rv_pdklib::rv_length(start_pos - t[0].position);
    if (start_gap <= SQUID_TARGET_NEXT_GAP) {
        RV_LOG_ERR(squid_sequence_tag,
            "target 1 is {} from the start, closer than {}; set refused",
            start_gap,
            SQUID_TARGET_NEXT_GAP);
        return RV_ERR_INVAL;
    }
    for (int i = 0; i < SQUID_TARGET_COUNT; ++i) {
        for (int j = i + 1; j < SQUID_TARGET_COUNT; ++j) {
            const bool consecutive = j == i + 1;
            const float min_gap = consecutive ? SQUID_TARGET_NEXT_GAP : SQUID_TARGET_OVERLAP_GAP;
            const float gap = rv_pdklib::rv_length(t[i].position - t[j].position);
            if (gap <= min_gap) {
                RV_LOG_ERR(squid_sequence_tag,
                    "targets {} and {} are {} apart, closer than {}; set refused",
                    i + 1,
                    j + 1,
                    gap,
                    min_gap);
                return RV_ERR_INVAL;
            }
        }
    }
    return RV_OK;
}

inline bool squid_sequence_done(const squid_sequence &s)
{
    return s.hits == SQUID_TARGET_COUNT;
}

// Counts a hit on the active target and moves to the next one; the eighth hit ends the round.
inline int squid_sequence_hit(squid_sequence &s)
{
    const bool has_target = s.active != SQUID_SEQUENCE_NONE;
    if (!has_target) {
        const bool over = squid_sequence_done(s);
        const char *why = over ? "the round is over" : "free mode has no active target";
        RV_LOG_WARN(squid_sequence_tag, "hit refused: {}", why);
        return over ? RV_ERR_ALREADY : RV_ERR_INVAL;
    }
    s.hits += 1;
    s.active = s.hits == SQUID_TARGET_COUNT ? SQUID_SEQUENCE_NONE : s.hits;
    return RV_OK;
}

inline void squid_sequence_restart(squid_sequence &s)
{
    s.active = 0;
    s.hits = 0;
}

// Free mode: no active target, and hits do not advance the sequence.
inline void squid_sequence_free(squid_sequence &s)
{
    s.active = SQUID_SEQUENCE_NONE;
    s.hits = 0;
}

} // namespace squid
