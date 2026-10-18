#pragma once

// Alkoldun's descent after the goal: from his place on the dome to the plate centre over SQUID_DESCENT_S. Holds the start
// and the time run only; the view draws the arc from them. No drawing and no physics.

#include <algorithm>

#include "squid-dome.hpp"
#include "squid-params.hpp"
#include "squid-physics.hpp"
#include "pdk/rv_err.h"
#include "pdklib/rv_logs/rv_logs.hpp"

namespace squid
{

// Log tag of descent messages.
inline constexpr const char *squid_descent_tag = "descent";

// Alkoldun's place on the dome when the descent starts, relative to the dome centre, and the seconds run since then.
struct squid_descent {
    rv_pdklib::rv_vec3 start{};
    float elapsed = 0;
};

// Takes Alkoldun's place on the dome from the body set, lifted by his hover so it matches the drawn body. Refused, with the
// descent unchanged, when the set has no Alkoldun.
inline int squid_descent_start(const squid_bodies &set, squid_descent &d)
{
    const auto end = set.items + set.count;
    const auto it = std::find_if(set.items, end, [](const squid_body &b) {
        return b.kind == squid_kind::alkoldun;
    });
    if (it == end) {
        RV_LOG_ERR(squid_descent_tag, "descent refused: no Alkoldun in the body set of {}", set.count);
        return RV_ERR_INVAL;
    }
    float radius = 0;
    const int rc = squid_body_radius(*it, radius);
    if (rc != RV_OK) {
        RV_LOG_ERR(squid_descent_tag, "descent refused: Alkoldun has no radius: rv_err {}", rc);
        return rc;
    }
    d.start = squid_dome_offset(it->pos.x, it->pos.z, radius, squid_dome_hover_lift(it->hover_phase));
    d.elapsed = 0;
    return RV_OK;
}

// Adds the frame time and returns true when the run is over. The run is capped at its end, so progress stops at 1.
// dt is checked by the caller.
inline bool squid_descent_advance(squid_descent &d, float dt)
{
    d.elapsed = std::min(d.elapsed + dt, SQUID_DESCENT_S);
    return d.elapsed >= SQUID_DESCENT_S;
}

// Share of the descent run: 0 on the dome, 1 at the plate centre.
inline float squid_descent_progress(const squid_descent &d)
{
    return std::clamp(d.elapsed / SQUID_DESCENT_S, 0.0f, 1.0f);
}

} // namespace squid
