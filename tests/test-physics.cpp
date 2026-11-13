// Host test of the physics pair reject in squid-physics.hpp:
//   bool squid_pair_reach(squid_flat a, squid_flat b, float reach)
//       true when the centres are closer than reach: the same answer, bit for bit, as the old pair pass
//       `std::sqrt(dx*dx + dz*dz) < reach` (strict: exactly at contact the bodies do not touch), with the squared
//       distance rejecting the pairs clearly apart.
//   void squid_pair_radii(const squid_bodies &set, float (&radii)[SQUID_BODY_CAPACITY])
//       radii[i] = radius of set.items[i] for i < set.count, once per substep.
#include <cmath>
#include <cstdio>

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

static bool old_touch(squid_flat a, squid_flat b, float reach)
{
    const float dx = b.x - a.x;
    const float dz = b.z - a.z;
    return !(std::sqrt(dx * dx + dz * dz) >= reach);
}

static void test_reach_boundaries()
{
    const squid_flat o{ 0, 0 };
    CHECK(!squid_pair_reach(o, { 3.0f, 4.0f }, 5.0f));        // exactly at contact: no touch
    CHECK(squid_pair_reach(o, { 3.0f, 3.9375f }, 5.0f));      // just inside
    CHECK(!squid_pair_reach(o, { 3.0f, 4.0625f }, 5.0f));     // just outside
    CHECK(!squid_pair_reach({ 1, 1 }, { 4.0f, 5.0f }, 5.0f)); // contact away from the origin
    CHECK(squid_pair_reach({ 1, 1 }, { 4.0f, 4.5f }, 5.0f));
    CHECK(squid_pair_reach({ 2, 2 }, { 2, 2 }, 0.45f));            // coincident centres touch
    CHECK(!squid_pair_reach({ 2, 2 }, { 2, 2 }, 0.0f));            // zero reach: never
    CHECK(squid_pair_reach(squid_flat{ 1e4f, 0 }, o, 10001.0f));   // large distance, inside
    CHECK(!squid_pair_reach(squid_flat{ 1e4f, 0 }, o, 1e4f));      // large distance, exactly at contact
    CHECK(!squid_pair_reach(squid_flat{ 1e4f, 0 }, o, 0.9f));      // large distance, far outside
    CHECK(!squid_pair_reach(squid_flat{ 1e20f, 1e20f }, o, 1.0f)); // squared distance overflows to inf: still no touch
    CHECK(squid_pair_reach(o, { 0.0f, -0.4f }, 0.45f));            // sign of the offset does not matter
    CHECK(squid_pair_reach({ 0.4f, 0 }, o, 0.45f));
}

// The reject and the old sqrt test agree on every pair, at the rounding edge too.
static void test_reach_matches_sqrt_path()
{
    const float reaches[] = { 0.45f, 0.51f, 0.57f, 0.63f, 0.675f, 0.72f, 0.81f, 0.9f };
    int checked = 0;
    for (float reach : reaches) {
        for (int ix = -40; ix <= 40; ++ix) {
            for (int iz = -40; iz <= 40; ++iz) {
                const squid_flat a{ 0.37f, -0.21f };
                const squid_flat b{ a.x + static_cast<float>(ix) * 0.025f, a.z + static_cast<float>(iz) * 0.025f };
                ++checked;
                CHECK(squid_pair_reach(a, b, reach) == old_touch(a, b, reach));
            }
        }
    }
    CHECK(checked > 10000);
}

// Pairs a few float steps either side of contact, in many directions: where a bare squared test would round wrongly.
static void test_reach_at_the_rounding_edge()
{
    const float reaches[] = { 0.45f, 0.51f, 0.675f, 0.9f };
    for (float reach : reaches) {
        for (int angle = 0; angle < 360; angle += 7) {
            const float r = static_cast<float>(angle) * 0.0174533f;
            const squid_flat a{ 1.3f, -0.7f };
            float d = reach;
            for (int k = 0; k < 40; ++k) {
                d = std::nextafter(d, 0.0f);
            }
            for (int k = 0; k < 80; ++k) {
                const squid_flat b{ a.x + d * std::cos(r), a.z + d * std::sin(r) };
                CHECK(squid_pair_reach(a, b, reach) == old_touch(a, b, reach));
                d = std::nextafter(d, 2.0f * reach);
            }
        }
    }
}

static void test_radii_table()
{
    squid_bodies set;
    int n = 0;
    for (int mass = SQUID_BALL_MASS_MIN; mass <= SQUID_BALL_MASS_MAX; ++mass) {
        CHECK(squid_body_ball(set.items[n++], mass, 0, { 0, 0 }) == RV_OK);
    }
    CHECK(squid_body_alkoldun(set.items[n++], 0, { 0, 0 }) == RV_OK);
    CHECK(squid_body_bottle(set.items[n++], { 0, 0 }, { 0, 0 }, false) == RV_OK);
    CHECK(squid_body_bottle(set.items[n++], { 0, 0 }, { 0, 0 }, true) == RV_OK);
    set.count = n;

    float radii[SQUID_BODY_CAPACITY];
    for (float &r : radii) {
        r = -1.0f;
    }
    squid_pair_radii(set, radii);
    for (int i = 0; i < set.count; ++i) {
        float expect = 0;
        CHECK(squid_body_radius(set.items[i], expect) == RV_OK);
        CHECK(radii[i] == expect);
        CHECK(radii[i] == squid_radius_checked(set.items[i]));
    }
    CHECK(radii[0] == SQUID_BALL_RADIUS[0]); // mass 1 is size 1
    CHECK(radii[7] == SQUID_BALL_RADIUS[3]); // mass 8 is size 4
    CHECK(radii[8] == SQUID_BALL_RADIUS[SQUID_ALKOLDUN_SIZE - 1]);
    CHECK(radii[9] == SQUID_BOTTLE_RADIUS);
    CHECK(radii[set.count] == -1.0f); // slots past count are left alone
}

int main()
{
    test_reach_boundaries();
    test_reach_matches_sqrt_path();
    test_reach_at_the_rounding_edge();
    test_radii_table();
    if (failures != 0) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("test-physics: ok\n");
    return 0;
}
