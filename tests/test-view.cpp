// Host test of the view's model choice: which model a body gets and when the entity's model must be rewritten.
// Build: g++ -std=c++23 -O1 -Wall -Wextra -I src -I $C/pdk/include -I $C/pdk/lib/include -I $C/pdk/kit/include
//   tests/test-view.cpp
#include <cstdio>
#include <cstring>

#include "squid-view.hpp"

using namespace squid;

static int failures = 0;
#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                 \
        }                                                               \
    } while (0)

static squid_body make(squid_kind kind, int mass, int variant = 0, bool marked = false)
{
    squid_body b;
    b.kind = kind;
    b.mass = mass;
    b.variant = variant;
    b.marked = marked;
    return b;
}

static bool is(const squid_body &b, const char *want)
{
    const char *got = squid_view_model_name(b);
    return got != nullptr && std::strcmp(got, want) == 0;
}

int main()
{
    // Balls: size by mass, model by size and variant (size 4 is the fridge for every variant; variant wraps).
    CHECK(is(make(squid_kind::ball, 1, 0), "m1_floppy.mppcmod"));
    CHECK(is(make(squid_kind::ball, 1, 3), "m1_bulb.mppcmod"));
    CHECK(is(make(squid_kind::ball, 1, 4), "m1_floppy.mppcmod"));
    CHECK(is(make(squid_kind::ball, 2, 1), "m2_calculator.mppcmod"));
    CHECK(is(make(squid_kind::ball, 3, 2), "m2_tube.mppcmod"));
    CHECK(is(make(squid_kind::ball, 4, 0), "m3_crt.mppcmod"));
    CHECK(is(make(squid_kind::ball, 7, 3), "m3_vacuum.mppcmod"));
    CHECK(is(make(squid_kind::ball, 8, 0), "m4_fridge.mppcmod"));
    CHECK(is(make(squid_kind::ball, 8, 4), "m4_fridge.mppcmod"));
    // Every ball mass and variant gets the model squid_view_model picks, as a pointer into the same tables.
    for (int mass = SQUID_BALL_MASS_MIN; mass <= SQUID_BALL_MASS_MAX; ++mass) {
        for (int v = 0; v < SQUID_MODEL_VARIANTS; ++v) {
            const squid_body b = make(squid_kind::ball, mass, v);
            const char *picked = nullptr;
            CHECK(squid_view_model(b, picked) == RV_OK);
            CHECK(squid_view_model_name(b) == picked);
        }
    }
    // Alkoldun and bottles.
    CHECK(is(make(squid_kind::alkoldun, 0), "alkoldun.mppcmod"));
    CHECK(is(make(squid_kind::bottle, 0, 0, false), "bottle.mppcmod"));
    CHECK(is(make(squid_kind::bottle, 0, 0, true), "bottle_marked.mppcmod"));
    // Refused bodies give no name.
    CHECK(squid_view_model_name(make(squid_kind::ball, 0)) == nullptr);
    CHECK(squid_view_model_name(make(squid_kind::ball, 9)) == nullptr);
    CHECK(squid_view_model_name(make(squid_kind::ball, 1, -1)) == nullptr);
    CHECK(squid_view_model_name(make(squid_kind::ball, 1, SQUID_MODEL_VARIANTS)) == nullptr);
    CHECK(squid_view_model_name(make(static_cast<squid_kind>(9), 1)) == nullptr);

    // Needs update: nothing set yet, or a different model; the same model (by name, not address) needs none.
    const char *crt = squid_view_model_name(make(squid_kind::ball, 4, 0));
    CHECK(squid_view_model_changed(nullptr, crt));
    CHECK(!squid_view_model_changed(crt, crt));
    const char copy[] = "m3_crt.mppcmod";
    CHECK(!squid_view_model_changed(copy, crt));
    // A split changes the size, so the model changes; a same-size body keeps it.
    CHECK(squid_view_model_changed(squid_view_model_name(make(squid_kind::ball, 4)),
        squid_view_model_name(make(squid_kind::ball, 2))));
    CHECK(squid_view_model_changed(squid_view_model_name(make(squid_kind::ball, 8)),
        squid_view_model_name(make(squid_kind::ball, 7))));
    CHECK(!squid_view_model_changed(squid_view_model_name(make(squid_kind::ball, 4)),
        squid_view_model_name(make(squid_kind::ball, 6))));
    // A variant change inside a size changes the model too; plain and marked bottles differ.
    CHECK(squid_view_model_changed(squid_view_model_name(make(squid_kind::ball, 1, 0)),
        squid_view_model_name(make(squid_kind::ball, 1, 1))));
    CHECK(squid_view_model_changed(squid_view_model_name(make(squid_kind::bottle, 0, 0, false)),
        squid_view_model_name(make(squid_kind::bottle, 0, 0, true))));
    // Alkoldun's flame flight swaps his model on the phase and back on landing.
    const char *plain = squid_view_model_name(make(squid_kind::alkoldun, 0));
    const squid_view_flight_row *flame = squid_view_flight_row_of(squid_phase::descent);
    CHECK(flame != nullptr && squid_view_model_changed(plain, flame->model));
    CHECK(flame != nullptr &&
        !squid_view_model_changed(flame->model, squid_view_flight_row_of(squid_phase::descent_pause)->model));
    CHECK(!squid_view_model_changed(plain, squid_view_flight_row_of(squid_phase::landed)->model));

    std::printf(failures == 0 ? "ok\n" : "%d failed\n", failures);
    return failures == 0 ? 0 : 1;
}
