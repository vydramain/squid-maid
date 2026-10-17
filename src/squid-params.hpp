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

// Speed of the target tilt change by the reserve keys, in degrees per second: full travel 0 -> 35 takes half a second.
constexpr float SQUID_KEY_TILT_RATE_DEG_S = 70.0f;

// Number of targets in a round's sequence; one is active, and the next starts after a hit.
constexpr int SQUID_TARGET_COUNT = 8;

// Length of Alkoldun's trail in seconds of flight; shows the direction of motion.
constexpr float SQUID_TRAIL_LENGTH_S = 0.4f;

// Debug view camera: the eye sits this far above the orbit plane, looking at the Earth's centre.
constexpr float SQUID_CAMERA_ELEVATION_DEG = 35.0f;

// Vertical field of view in degrees; narrow, so the perspective reads close to the flat orbit picture.
constexpr float SQUID_CAMERA_FOV_DEG = 12.0f;

// Eye distance from the Earth's centre. The frame (320x240, so the vertical fov is the limit) must hold a sphere of
// radius SQUID_ORBIT_RADIUS + SQUID_DEBRIS_RADIUS = 6.4: at a half fov of 6 degrees it needs 6.4 / sin(6 deg) = 61.2;
// 70 leaves about 14 percent of margin.
constexpr float SQUID_CAMERA_DISTANCE = 70.0f;

// Clip planes. The nearest point of the sphere is 70 - 6.4 = 63.6 away, the farthest 76.4; the planes lie outside that
// range, so nothing of the scene is cut.
constexpr float SQUID_CAMERA_NEAR = 10.0f;
constexpr float SQUID_CAMERA_FAR = 150.0f;

} // namespace squid
