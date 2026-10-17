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

// Time constant of input smoothing, in seconds; hand tremor does not become tremor of the world, and the tilt
// reaches the world more slowly.
constexpr float SQUID_SMOOTHING_S = 0.2f;

// Cubic share of the pad-to-game tilt curve; the linear rest is 1 minus it. The linear part sets the slope at the dead
// zone edge (0.46 game deg per pad deg, against 1.52 for the old linear gain); the cubic part steepens the curve
// toward full tilt, and full input still reaches the limit.
constexpr float SQUID_TILT_CUBIC_SHARE = 0.7f;

// Speed of the target tilt change by the reserve keys, in degrees per second: full travel 0 -> 35 takes half a second.
constexpr float SQUID_KEY_TILT_RATE_DEG_S = 70.0f;

// Radius of the invisible dome; the plate corner is 1.25 * 3.04 = 3.80 from the mass centre once the structure
// is scaled by 1.25, so the 0.70 gap keeps the whole structure under it.
constexpr float SQUID_DOME_RADIUS = 4.5f;

// Dome centre = mass centre of the Earth, the three elephants and the turtle (mesh volumes in the scene transforms).
constexpr float SQUID_DOME_CENTER_X = -0.012f;
constexpr float SQUID_DOME_CENTER_Y = -0.974f;
constexpr float SQUID_DOME_CENTER_Z = -0.059f;

// Disc edge as the angle from the dome top; at 90 deg (equator) edge balls are squeezed 36 percent along the circle,
// at 60 deg 17 percent, and the edge ring sits above the plate.
constexpr float SQUID_DISC_EDGE_DEG = 60.0f;

// Flat disc radius (about 4.71): the dome arc length from the top to the edge.
constexpr float SQUID_DISC_RADIUS = SQUID_DOME_RADIUS * SQUID_DISC_EDGE_DEG * std::numbers::pi_v<float> / 180.0f;

// Ball radius by size, index size - 1 (size 1 smallest .. 4); grows as mass^(1/3) for masses 1, 2, 4, 8.
// Size 4 is about 20 px across on 320x240 at 21.8 px per unit, size 1 about 10 px.
constexpr float SQUID_BALL_RADIUS[] = { 0.225f, 0.285f, 0.36f, 0.45f };

// Balls of size 4 at the start; with Alkoldun the total mass is 84 smallest units: 9.7 percent of the disc area,
// 19 percent after a full split.
constexpr int SQUID_START_BALLS = 10;

// Marked bottles Alkoldun drinks to finish the round.
constexpr int SQUID_BEER_GOAL = 3;

// Alkoldun's mass in smallest units: it weighs like size 3; it never splits.
constexpr int SQUID_ALKOLDUN_MASS = 4;

// Gravity along the tilted disc, in units/s^2; at 35 deg tilt a = 3.44 and centre to edge (4.71) without damping
// takes 1.65 s, so the tilt is felt at once and does not throw the balls.
constexpr float SQUID_GRAVITY = 6.0f;

// Speed decays with a 5 s time constant (exp(-0.2 t)); after the pad is levelled the balls float on as in weightlessness.
constexpr float SQUID_DAMPING_PER_S = 0.2f;

// Random drift of every body, as in weightlessness: an Ornstein-Uhlenbeck acceleration per axis, in units/s^2, of
// standard deviation SQUID_DRIFT_SIGMA for mass 1; a body of mass m gets SQUID_DRIFT_SIGMA / sqrt(m).
constexpr float SQUID_DRIFT_SIGMA = 0.35f;

// Memory time of the drift in seconds: a push fades over about 1.5 s, so the motion is smooth, not jerky.
constexpr float SQUID_DRIFT_TAU_S = 1.5f;

// Birth spin of every body: magnitude uniform in these bounds, in deg/s, about a uniformly random axis. Slow enough to read
// as a turn, fast enough that the rim shows it.
constexpr float SQUID_SPIN_MIN_DEG_S = 20.0f;
constexpr float SQUID_SPIN_MAX_DEG_S = 90.0f;

// Spin decay time in seconds: exp(-t / 10 s), so a body keeps about 37 percent of its spin after 10 s without contact.
constexpr float SQUID_SPIN_DECAY_S = 10.0f;

// Spin per unit of tangential speed over the radius (1 / s) that a contact adds about y: gain 1, as if the slide
// turned into rolling.
constexpr float SQUID_SPIN_IMPACT_GAIN = 1.0f;

// Top of the hover bob in world units, so the hover lift stays in 0..SQUID_HOVER_MAX; the physics does not read it.
constexpr float SQUID_HOVER_MAX = 0.4f;

// Hover period in seconds, uniform in these bounds at birth: a slow bob, not a jitter.
constexpr float SQUID_HOVER_PERIOD_MIN_S = 3.0f;
constexpr float SQUID_HOVER_PERIOD_MAX_S = 6.0f;

// Speed cap in units/s; 8 is below the ~8.05 run across the whole disc without damping at this gravity, so it caps
// that run and bounds the tunnelling check.
constexpr float SQUID_SPEED_MAX = 8.0f;

// Physics substeps per 60 Hz frame (1/240 s each): travel per substep 8/240 = 0.033 is under r1/4 = 0.056,
// a quarter of the smallest radius, so a ball cannot skip past another.
constexpr int SQUID_SUBSTEPS = 4;

// Threshold T of the normal relative speed that splits a pair of equal sizes; uniform gravity moves all balls alike,
// so the relative speed comes from the edge and collisions.
constexpr float SQUID_SPLIT_SPEED = 3.0f;

// A pair of different sizes needs this times the threshold (6 units/s): a head-on hit, or one ball back from the edge.
constexpr float SQUID_SPLIT_UNEQUAL_FACTOR = 2.0f;

// Ball-ball restitution: billiard-like elastic balls.
constexpr float SQUID_BALL_RESTITUTION = 0.85f;

// Smallest approach speed, in units/s, of a plain bounce that counts for the bounce sound: a slower contact is a pile
// settling, not a hit. Starting value, tuned by ear.
constexpr float SQUID_BOUNCE_SOUND_MIN_SPEED = 1.0f;

// Edge restitution: the edge absorbs more, so balls do not race around forever.
constexpr float SQUID_RIM_RESTITUTION = 0.6f;

// Tangential speed in units/s that moves the two halves of a split apart, shared inversely to their masses,
// so they do not merge again on a weak touch.
constexpr float SQUID_SPLIT_SEPARATION_SPEED = 0.6f;

// Share of the game tilt that the world on screen tilts by: up to about 11.7 deg (35 / 3).
constexpr float SQUID_VISIBLE_TILT_SHARE = 1.0f / 3.0f;

// Scene camera elevation above the disc plane, in degrees: the eye looks down from above. The azimuth below turns it
// to the upper right corner of the scene.
constexpr float SQUID_CAMERA_ELEVATION_DEG = 30.0f;

// Scene camera azimuth in degrees: the eye's turn about the vertical axis through the look-at point; positive moves
// the eye toward +x, the viewer's right, so the camera looks from the upper right.
constexpr float SQUID_CAMERA_AZIMUTH_DEG = 30.0f;

// Vertical field of view in degrees; the distance below is computed for it.
constexpr float SQUID_CAMERA_FOV_DEG = 30.0f;

// Eye distance from the look-at point. Checked on frames 8 and 128 at pitch 30: no ball leaves the 320x240 frame.
constexpr float SQUID_CAMERA_DISTANCE = 20.5f;

// Height of the look-at point above the origin, in world units; raised so the Earth sits low in the frame.
constexpr float SQUID_CAMERA_TARGET_Y = 0.6f;

// Clip planes. The dome sphere with size-4 balls lies about 16 to 26 from the eye; near 8 and far 40 cut none of it.
constexpr float SQUID_CAMERA_NEAR = 8.0f;
constexpr float SQUID_CAMERA_FAR = 40.0f;

} // namespace squid
