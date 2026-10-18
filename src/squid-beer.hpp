#pragma once

// Drinking a bottle: Alkoldun touching an aged bottle (SQUID_BOTTLE_FRESH_S old) takes it out of the set, with no response
// on either body. A fresh bottle bounces off Alkoldun, which does not move. The pair pass removes the bottle and counts it;
// a marked one also counts toward SQUID_BEER_GOAL in the round.

#include "squid-body.hpp"
#include "squid-params.hpp"

namespace squid
{

// Whether two touching bodies are Alkoldun and a drinkable bottle, in either order. A fresh bottle is not a drink: it
// is a contact and bounces.
inline bool squid_is_drink(const squid_body &a, const squid_body &b)
{
    const bool alkoldun_bottle = a.kind == squid_kind::alkoldun && b.kind == squid_kind::bottle;
    const bool bottle_alkoldun = a.kind == squid_kind::bottle && b.kind == squid_kind::alkoldun;
    const squid_body &bottle = alkoldun_bottle ? b : a;
    return (alkoldun_bottle || bottle_alkoldun) && bottle.age >= SQUID_BOTTLE_FRESH_S;
}

// Inverse mass of body in a pair response with other. Alkoldun has none against a bottle, so the bottle alone is pushed
// and bounces off it; every other pair keeps 1 / mass.
inline float squid_inverse_mass(const squid_body &body, const squid_body &other)
{
    const bool alkoldun_bottle = body.kind == squid_kind::alkoldun && other.kind == squid_kind::bottle;
    return alkoldun_bottle ? 0.0f : 1.0f / static_cast<float>(body.mass);
}

} // namespace squid
