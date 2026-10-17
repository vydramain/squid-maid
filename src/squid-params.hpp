#pragma once

#include <numbers>

// Starting values for tuning the squid game; not a checked balance.
// World units are abstract; angles are in degrees.

namespace squid
{

// Game tilt limit per axis, in degrees; each axis is clamped independently.
constexpr float SQUID_TILT_LIMIT_DEG = 35.0f;

// Physical pad tilt that gives full game tilt (25 -> 35 degrees); within the comfortable range of the hands.
constexpr float SQUID_FULL_INPUT_TILT_DEG = 25.0f;

// Smooth dead zone around neutral, in degrees, with no jump at its edge; damps small hand tremor.
constexpr float SQUID_DEAD_ZONE_DEG = 2.0f;

// Time constant of input smoothing, in seconds; hand tremor does not become tremor of the world.
constexpr float SQUID_SMOOTHING_S = 0.08f;

// Speed of the target tilt change by the reserve keys, in degrees per second: full travel 0 -> 35 takes half a second.
constexpr float SQUID_KEY_TILT_RATE_DEG_S = 70.0f;

// Radius of the invisible dome; 3.04, measured from the scene meshes, is the plate corner's distance from the mass centre,
// so the 0.46 gap keeps the whole structure under it.
constexpr float SQUID_DOME_RADIUS = 3.5f;

// Dome centre = mass centre of the Earth, the three elephants and the turtle (mesh volumes in the scene transforms).
constexpr float SQUID_DOME_CENTER_X = -0.012f;
constexpr float SQUID_DOME_CENTER_Y = -0.974f;
constexpr float SQUID_DOME_CENTER_Z = -0.059f;

// Disc edge as the angle from the dome top; at 90 deg (equator) edge balls are squeezed 36 percent along the circle,
// at 60 deg 17 percent, and the edge ring sits above the plate.
constexpr float SQUID_DISC_EDGE_DEG = 60.0f;

// Flat disc radius (about 3.67): the dome arc length from the top to the edge.
constexpr float SQUID_DISC_RADIUS = SQUID_DOME_RADIUS * SQUID_DISC_EDGE_DEG * std::numbers::pi_v<float> / 180.0f;

// Ball radius by size, index size - 1 (size 1 smallest .. 4); grows as mass^(1/3) for masses 1, 2, 4, 8.
// Size 4 is estimated ~36 px on 320x240.
constexpr float SQUID_BALL_RADIUS[] = { 0.30f, 0.38f, 0.48f, 0.60f };

// Balls of size 4 at the start; with Alkoldun the total mass is 84 smallest units: 28.5 percent of the disc area,
// 56.3 percent after a full split.
constexpr int SQUID_START_BALLS = 10;

// Alkoldun's mass in smallest units: it weighs like size 3; it never splits.
constexpr int SQUID_ALKOLDUN_MASS = 4;

// Gravity along the tilted disc, in units/s^2; at 35 deg tilt a = 5.2 and centre to edge takes 1.2 s, so the tilt
// is felt at once and does not throw the balls.
constexpr float SQUID_GRAVITY = 9.0f;

// Speed decays with a 2 s time constant (exp(-0.5 t)); a ball rolls about 2 s after the pad is levelled.
constexpr float SQUID_DAMPING_PER_S = 0.5f;

// Speed cap in units/s; 8 is below the ~8.7 run across the whole disc without damping, so it caps that run
// and bounds the tunnelling check.
constexpr float SQUID_SPEED_MAX = 8.0f;

// Physics substeps per 60 Hz frame (1/240 s each): travel per substep 8/240 = 0.033 is under r1/4 = 0.075,
// a quarter of the smallest radius, so a ball cannot skip past another.
constexpr int SQUID_SUBSTEPS = 4;

// Threshold T of the normal relative speed that splits a pair of equal sizes; uniform gravity moves all balls alike,
// so the relative speed comes from the edge and collisions.
constexpr float SQUID_SPLIT_SPEED = 3.0f;

// A pair of different sizes needs this times the threshold (6 units/s): a head-on hit, or one ball back from the edge.
constexpr float SQUID_SPLIT_UNEQUAL_FACTOR = 2.0f;

// Ball-ball restitution: billiard-like elastic balls.
constexpr float SQUID_BALL_RESTITUTION = 0.85f;

// Edge restitution: the edge absorbs more, so balls do not race around forever.
constexpr float SQUID_RIM_RESTITUTION = 0.6f;

// Tangential speed in units/s that moves the two halves of a split apart, shared inversely to their masses,
// so they do not merge again on a weak touch.
constexpr float SQUID_SPLIT_SEPARATION_SPEED = 0.6f;

// Camera elevation for the dome view; estimated: at the old 35 deg the far half of the disc is squeezed into a strip.
constexpr float SQUID_DOME_CAMERA_ELEVATION_DEG = 55.0f;

// Share of the game tilt that the world on screen tilts by (up to ~12 deg); estimated: at the full 35 deg the dome leaves
// the frame, or the scale drops to ~23 px per unit.
constexpr float SQUID_VISIBLE_TILT_SHARE = 1.0f / 3.0f;

// Debug view camera: the eye sits this far above the disc plane, looking at the Earth's centre.
constexpr float SQUID_CAMERA_ELEVATION_DEG = 35.0f;

// Vertical field of view in degrees; narrow, so the perspective reads close to a flat picture.
constexpr float SQUID_CAMERA_FOV_DEG = 12.0f;

// Eye distance from the Earth's centre for the old orbit picture. The frame (320x240, so the vertical fov is the limit)
// must hold a sphere of radius 6.4 (orbit plus debris): at a half fov of 6 degrees it needs 6.4 / sin(6 deg) = 61.2;
// 70 leaves about 14 percent of margin. A later change replaces these numbers with the dome camera.
constexpr float SQUID_CAMERA_DISTANCE = 70.0f;

// Clip planes. The nearest point of the sphere is 70 - 6.4 = 63.6 away, the farthest 76.4; the planes lie outside that
// range, so nothing of the scene is cut.
constexpr float SQUID_CAMERA_NEAR = 10.0f;
constexpr float SQUID_CAMERA_FAR = 150.0f;

} // namespace squid
