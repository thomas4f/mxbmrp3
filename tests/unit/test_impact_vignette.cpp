// ============================================================================
// tests/unit/test_impact_vignette.cpp
// Unit tests for core/impact_vignette.h - the impact vignette's envelope: how
// far the screen edges darken for a hit, and how that swells in and clears.
//
// The cases worth pinning are the ones that show in game: riding and braking
// must never darken the screen, a single-sample landing spike must still read
// as a full hit (telemetry is 100 Hz, frames can be slower), the fade must be
// the same at 60 and 480 fps, and a stall must not jump it.
// ============================================================================
#include "doctest.h"

#include <limits>

#include "core/impact_vignette.h"
#include "core/plausible_g.h"

using namespace ImpactVignette;

namespace {

// Run `seconds` of frames at `frameS` each with no new hit.
void coast(Envelope& e, float seconds, float frameS) {
    for (float t = 0.0f; t < seconds; t += frameS) e.step(0.0f, frameS);
}

}  // namespace

TEST_CASE("everyday load does not darken the screen") {
    CHECK(hitFromG(1.0f) == 0.0f);     // at rest, gravity
    CHECK(hitFromG(4.5f) == 0.0f);     // hard braking, kart cornering, a firm landing
    CHECK(hitFromG(G_ONSET) == 0.0f);
    CHECK(hitFromG(G_FULL) == 1.0f);
    CHECK(hitFromG(40.0f) == 1.0f);
    CHECK(hitFromG(std::numeric_limits<float>::quiet_NaN()) == 0.0f);
}

TEST_CASE("a one-sample spike swells to a full hit, then clears") {
    Envelope e;
    e.step(1.0f, 1.0f / 60.0f);        // the spike, then nothing
    CHECK(e.peak == doctest::Approx(1.0f));
    CHECK(e.level > 0.3f);             // visible on the very frame
    coast(e, 0.12f, 1.0f / 60.0f);
    CHECK(e.level > 0.75f);            // swelled close to the peak
    coast(e, 3.0f, 1.0f / 60.0f);
    CHECK(e.idle());
    CHECK(e.level == 0.0f);            // settles to exactly nothing
}

TEST_CASE("the fade is frame-rate independent") {
    Envelope slow, fast;
    slow.step(0.8f, 1.0f / 60.0f);
    fast.step(0.8f, 1.0f / 480.0f);
    coast(slow, 0.5f, 1.0f / 60.0f);
    coast(fast, 0.5f, 1.0f / 480.0f);
    CHECK(slow.level == doctest::Approx(fast.level).epsilon(0.08));
}

TEST_CASE("a stall does not jump the fade") {
    Envelope e;
    e.step(1.0f, 0.0f);
    coast(e, 0.2f, 1.0f / 120.0f);
    const float before = e.level;
    e.step(0.0f, 5.0f);                // five-second hitch, clamped to 0.1 s
    CHECK(e.level > before * 0.6f);
}

TEST_CASE("a second hit during the fade swells from where it is") {
    Envelope e;
    e.step(1.0f, 0.0f);
    coast(e, 0.6f, 1.0f / 120.0f);
    const float mid = e.level;
    e.step(0.3f, 1.0f / 120.0f);       // smaller than what is left: no dip
    CHECK(e.level >= mid * 0.97f);
}

TEST_CASE("a light hit sits further out than a hard one") {
    CHECK(halfExtent(1.0f) == doctest::Approx(0.5f));
    CHECK(halfExtent(0.2f) > halfExtent(0.8f));
    CHECK(opacity(0.5f, 0.0f) == 0.0f);   // Off draws nothing
    CHECK(opacity(1.0f, 1.0f) == 1.0f);
}

TEST_CASE("a spawn or reset glitch is not a hit") {
    CHECK(PlausibleG::ok(12.0f));
    CHECK(PlausibleG::ok(PlausibleG::MAX));
    CHECK_FALSE(PlausibleG::ok(PlausibleG::MAX + 1.0f));   // physics losing its footing
    CHECK_FALSE(PlausibleG::ok(std::numeric_limits<float>::infinity()));
    CHECK_FALSE(PlausibleG::ok(std::numeric_limits<float>::quiet_NaN()));
    CHECK_FALSE(PlausibleG::ok(0.0f));
}

TEST_CASE("the fade setting sets how long a full hit takes to clear") {
    Envelope quick, slow;
    quick.step(1.0f, 0.0f, 0.5f);
    slow.step(1.0f, 0.0f, 3.0f);
    for (int i = 0; i < 90; ++i) { quick.step(0.0f, 1.0f / 60.0f, 0.5f); slow.step(0.0f, 1.0f / 60.0f, 3.0f); }
    CHECK(quick.idle());                       // a second past its 0.5 s fade
    CHECK(slow.level > 0.15f);                 // halfway into 3 s, still showing
    CHECK(releaseRate(0.0f) == releaseRate(FADE_MIN_S));   // clamped, never divides by 0
}

TEST_CASE("a small hit clears sooner than a big one, in its share of the fade") {
    // Fade 2 s: a full hit lasts 2 s, a quarter-size hit half a second.
    Envelope big, small;
    big.step(1.0f, 0.0f, 2.0f);
    small.step(0.25f, 0.0f, 2.0f);
    for (int i = 0; i < 48; ++i) { big.step(0.0f, 1.0f / 60.0f, 2.0f); small.step(0.0f, 1.0f / 60.0f, 2.0f); }
    CHECK(small.peak == 0.0f);   // 0.8 s: the small hit's 0.5 s is past
    CHECK(big.peak > 0.5f);      // the big one has 1.2 s still to go
    for (int i = 0; i < 90; ++i) big.step(0.0f, 1.0f / 60.0f, 2.0f);
    CHECK(big.idle());           // 2.3 s: past its 2 s
}
