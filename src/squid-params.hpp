#pragma once

// Starting values for tuning the squid game (docs/disdoc.md, section 13); not a checked balance.
// World units are abstract; angles are in degrees.

namespace squid
{

// Side length of the Earth.
constexpr float SQUID_EARTH_SIDE = 4.0f;

// Radius of Alkoldun's orbit around the Earth's centre; the room to maneuver around the structure.
constexpr float SQUID_ORBIT_RADIUS = 6.0f;

// Radius of Alkoldun (the colored ball on the orbit); both its visible size and its collision radius.
constexpr float SQUID_ALKOLDUN_RADIUS = 0.25f;

// Radius of a debris ball; both its visible size and its collision radius. Debris are targets to hit, not obstacles.
constexpr float SQUID_DEBRIS_RADIUS = 0.4f;

// Time for one full orbit, in seconds.
constexpr float SQUID_ORBIT_PERIOD_S = 8.0f;

// Game tilt limit of the orbit per axis, in degrees; each axis is clamped independently.
constexpr float SQUID_TILT_LIMIT_DEG = 35.0f;

// Physical pad tilt that gives full game tilt (25 -> 35 degrees); within the comfortable range of the hands.
constexpr float SQUID_FULL_INPUT_TILT_DEG = 25.0f;

// Smooth dead zone around neutral, in degrees, with no jump at its edge; damps small hand tremor.
constexpr float SQUID_DEAD_ZONE_DEG = 2.0f;

// Time constant of input smoothing, in seconds; hand tremor does not become tremor of the world.
constexpr float SQUID_SMOOTHING_S = 0.08f;

// Number of targets in a round's sequence; one is active, and the next starts after a hit.
constexpr int SQUID_TARGET_COUNT = 8;

// Length of Alkoldun's trail in seconds of flight; shows the direction of motion.
constexpr float SQUID_TRAIL_LENGTH_S = 0.4f;

} // namespace squid
