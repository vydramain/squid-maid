#pragma once

// Drinking a bottle: Alkoldun touching a bottle takes it out of the set, with no response on either body. The pair pass
// removes the bottle and counts it; every drink adds points toward SQUID_BEER_GOAL: a marked one SQUID_BEER_POINTS_MARKED,
// a plain one SQUID_BEER_POINTS_PLAIN.

#include "squid-body.hpp"

namespace squid
{

// Whether two touching bodies are Alkoldun and a bottle, in either order.
inline bool squid_is_drink(const squid_body &a, const squid_body &b)
{
    const bool alkoldun_bottle = a.kind == squid_kind::alkoldun && b.kind == squid_kind::bottle;
    const bool bottle_alkoldun = a.kind == squid_kind::bottle && b.kind == squid_kind::alkoldun;
    return alkoldun_bottle || bottle_alkoldun;
}

} // namespace squid
