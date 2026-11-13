// Host test of the per-substep constants in squid-physics.hpp:
//   struct squid_step_consts { float drift_decay, drift_kick_unit, damping, spin_decay; }
//   squid_step_consts squid_step_consts_for(float h)
//       drift_decay = exp(-h / SQUID_DRIFT_TAU_S), drift_kick_unit = sqrt(1 - drift_decay^2),
//       damping = exp(-SQUID_DAMPING_PER_S * h), spin_decay = exp(-h / SQUID_SPIN_DECAY_S); exactly the float
//       expressions the body step used inline per body.
//   void squid_drift_step(squid_body &, const squid_step_consts &, std::minstd_rand &)
//   void squid_motion_step(squid_body &, float h, const squid_step_consts &)
//       same body state, bit for bit, as those per-body formulas.
//   const float SQUID_POSE_YAW_COS / SQUID_POSE_YAW_SIN: cos/sin of -SQUID_POSE_YAW_DEG * SQUID_DEG_TO_RAD.
//   squid_gravity_disc keeps its result.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>

#include "squid-physics.hpp"

using namespace squid;

static int failures = 0;
#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                 \
        }                                                               \
    } while (0)

// Bit equality, so a changed order of float operations shows.
static bool same(float a, float b)
{
    return std::memcmp(&a, &b, sizeof a) == 0;
}

static const float HS[] = { SQUID_FRAME_S / 4.0f, SQUID_FRAME_S, 0.005f, 0.1f };

static void test_consts_match_inline_expressions()
{
    for (float h : HS) {
        const squid_step_consts k = squid_step_consts_for(h);
        const float decay = std::exp(-h / SQUID_DRIFT_TAU_S);
        CHECK(same(k.drift_decay, decay));
        CHECK(same(k.drift_kick_unit, std::sqrt(1.0f - decay * decay)));
        CHECK(same(k.damping, std::exp(-SQUID_DAMPING_PER_S * h)));
        CHECK(same(k.spin_decay, std::exp(-h / SQUID_SPIN_DECAY_S)));
    }
}

static void test_yaw_constants()
{
    const float a = -SQUID_POSE_YAW_DEG * SQUID_DEG_TO_RAD;
    CHECK(same(SQUID_POSE_YAW_COS, std::cos(a)));
    CHECK(same(SQUID_POSE_YAW_SIN, std::sin(a)));
}

static void test_gravity_unchanged()
{
    const squid_tilt tilts[] = { { 0, 0 }, { 10, -5 }, { -3.5f, 7.25f }, { 20, 20 } };
    for (squid_tilt t : tilts) {
        const squid_flat g = squid_gravity(t);
        const float a = -SQUID_POSE_YAW_DEG * SQUID_DEG_TO_RAD;
        const squid_flat want{ std::cos(a) * g.x + std::sin(a) * g.z, -std::sin(a) * g.x + std::cos(a) * g.z };
        const squid_flat got = squid_gravity_disc(t);
        CHECK(same(got.x, want.x));
        CHECK(same(got.z, want.z));
    }
}

static squid_body make_body(int mass, float vx, float sx, float sy, float sz)
{
    squid_body b;
    b.mass = mass;
    b.vel = squid_flat{ vx, -vx / 2 };
    b.drift = squid_flat{ 0.3f, -0.2f };
    b.spin = rv_pdklib::rv_vec3{ sx, sy, sz };
    b.hover_phase = 1.5f;
    b.hover_period = 3.0f;
    return b;
}

static bool body_same(const squid_body &a, const squid_body &b)
{
    return same(a.drift.x, b.drift.x) && same(a.drift.z, b.drift.z) && same(a.rot.x, b.rot.x) && same(a.rot.y, b.rot.y) &&
        same(a.rot.z, b.rot.z) && same(a.rot.w, b.rot.w) && same(a.spin.x, b.spin.x) && same(a.spin.y, b.spin.y) &&
        same(a.spin.z, b.spin.z) && same(a.hover_phase, b.hover_phase);
}

// Reference drift step, decay and kick taken per body.
static void ref_drift(squid_body &b, float h, std::minstd_rand &rng)
{
    const float decay = std::exp(-h / SQUID_DRIFT_TAU_S);
    const float sigma = SQUID_DRIFT_SIGMA / std::sqrt(static_cast<float>(b.mass));
    const float kick = sigma * std::sqrt(1.0f - decay * decay);
    b.drift.x = b.drift.x * decay + kick * squid_normal(rng);
    b.drift.z = b.drift.z * decay + kick * squid_normal(rng);
}

// Reference motion step, decay taken per body.
static void ref_motion(squid_body &b, float h)
{
    const float rate = std::sqrt(b.spin.x * b.spin.x + b.spin.y * b.spin.y + b.spin.z * b.spin.z);
    const float angle = rate * h;
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

static void test_drift_step_with_consts()
{
    for (float h : HS) {
        const squid_step_consts k = squid_step_consts_for(h);
        squid_body got[3] = { make_body(1, 0, 0, 0, 0), make_body(4, 0, 0, 0, 0), make_body(8, 0, 0, 0, 0) };
        squid_body want[3] = { got[0], got[1], got[2] };
        std::minstd_rand rg(7), rw(7);
        for (int step = 0; step < 5; ++step) {
            for (int i = 0; i < 3; ++i) {
                squid_drift_step(got[i], k, rg);
                ref_drift(want[i], h, rw);
            }
        }
        for (int i = 0; i < 3; ++i) {
            CHECK(body_same(got[i], want[i]));
        }
        CHECK(rg() == rw()); // the same draws were taken
    }
}

static void test_motion_step_with_consts()
{
    for (float h : HS) {
        const squid_step_consts k = squid_step_consts_for(h);
        squid_body got[3] = { make_body(1, 0, 0, 0, 0), make_body(2, 1, 0.5f, 2.0f, -1.0f), make_body(8, 1, 0, 3.0f, 0) };
        squid_body want[3] = { got[0], got[1], got[2] };
        for (int step = 0; step < 5; ++step) {
            for (int i = 0; i < 3; ++i) {
                squid_motion_step(got[i], h, k);
                ref_motion(want[i], h);
            }
        }
        for (int i = 0; i < 3; ++i) {
            CHECK(body_same(got[i], want[i]));
        }
        CHECK(want[1].spin.y != 2.0f); // the reference does decay the spin
    }
}

int main()
{
    test_consts_match_inline_expressions();
    test_yaw_constants();
    test_gravity_unchanged();
    test_drift_step_with_consts();
    test_motion_step_with_consts();
    if (failures == 0) {
        std::printf("all passed\n");
        return 0;
    }
    std::printf("%d failures\n", failures);
    return 1;
}
