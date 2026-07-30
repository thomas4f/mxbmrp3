// ============================================================================
// tests/unit/test_peak_marker.cpp
// Unit tests for hud/peak_marker.h — the shared "max marker" state machine.
//
// This logic existed as six copies across LeanWidget, BarsWidget and RumbleHud,
// none of them tested: every copy sat inside a rebuildRenderData() reachable
// only through a DLL under Wine, driven by telemetry. Now one implementation
// serves all six, so it is worth pinning properly — a regression here is a
// regression in every gauge at once, which cuts both ways.
//
// THE BEHAVIOUR THAT LOOKS LIKE A BUG AND ISN'T: climbing to a new peak HIDES
// the marker. It is only revealed once the reading retreats. `climbThenRetreat`
// walks a full arc so the sequence is legible in one place, because reading it
// off the four-branch implementation is what makes people "fix" it.
// ============================================================================
#include "doctest.h"

#include "hud/peak_marker.h"
#include <initializer_list>

namespace {

constexpr float kThreshold = 1.0f;
constexpr int kLinger = 5;

// A marker's two-value state, bundled for readability in the tests only — the
// production callers pass their own members by reference.
struct M {
    float value = 0.0f;
    int frames = 0;

    void active(float magnitude, float threshold = kThreshold, int linger = kLinger) {
        PeakMarker::advanceActive(value, frames, magnitude, threshold, linger);
    }
    void idle() { PeakMarker::advanceIdle(value, frames); }
    bool visible() const { return frames > 0; }
};

}  // namespace

TEST_CASE("peak marker: starts hidden and empty") {
    M m;
    CHECK(m.value == doctest::Approx(0.0f));
    CHECK_FALSE(m.visible());
}

TEST_CASE("peak marker: climbing sets the peak but keeps it hidden") {
    M m;
    m.active(10.0f);
    CHECK(m.value == doctest::Approx(10.0f));
    CHECK_FALSE(m.visible());   // nothing to mark while still climbing

    m.active(20.0f);
    CHECK(m.value == doctest::Approx(20.0f));
    CHECK_FALSE(m.visible());
}

TEST_CASE("peak marker: retreating past the deadband reveals the marker") {
    M m;
    m.active(20.0f);
    CHECK_FALSE(m.visible());

    m.active(15.0f);            // dropped more than the threshold below the peak
    CHECK(m.visible());
    CHECK(m.frames == kLinger);
    CHECK(m.value == doctest::Approx(20.0f));   // still holding the peak, not the live value
}

TEST_CASE("peak marker: the threshold is a deadband on both sides") {
    M m;
    m.active(20.0f);

    SUBCASE("a rise within the deadband does not re-peak") {
        m.active(20.5f);        // < value + threshold
        CHECK(m.value == doctest::Approx(20.0f));
        CHECK_FALSE(m.visible());
    }
    SUBCASE("a drop within the deadband does not arm the linger") {
        m.active(19.5f);        // > value - threshold
        CHECK_FALSE(m.visible());
        CHECK(m.value == doctest::Approx(20.0f));
    }
    SUBCASE("a rise past the deadband re-peaks") {
        m.active(21.5f);
        CHECK(m.value == doctest::Approx(21.5f));
        CHECK_FALSE(m.visible());
    }
    SUBCASE("a drop past the deadband arms") {
        m.active(18.5f);
        CHECK(m.visible());
    }
}

TEST_CASE("peak marker: a new peak while lingering hides the marker again") {
    M m;
    m.active(20.0f);
    m.active(15.0f);
    REQUIRE(m.visible());

    m.active(25.0f);            // rider goes further than before
    CHECK(m.value == doctest::Approx(25.0f));
    CHECK_FALSE(m.visible());   // back to "nothing to mark"
}

TEST_CASE("peak marker: the linger counts down and clears the peak on expiry") {
    M m;
    m.active(20.0f);
    m.active(15.0f);
    REQUIRE(m.frames == kLinger);

    // Holding steady inside the deadband of the live value ticks the linger down.
    for (int i = kLinger; i > 1; --i) {
        CHECK(m.frames == i);
        m.active(15.0f);
    }
    CHECK(m.frames == 1);
    CHECK(m.value == doctest::Approx(20.0f));

    m.active(15.0f);
    CHECK(m.frames == 0);
    CHECK(m.value == doctest::Approx(0.0f));   // cleared, so it disappears
}

TEST_CASE("peak marker: an idle marker only counts down, never re-arms") {
    M m;
    m.active(20.0f);
    m.active(15.0f);
    REQUIRE(m.visible());

    for (int i = 0; i < kLinger; ++i) m.idle();
    CHECK_FALSE(m.visible());
    CHECK(m.value == doctest::Approx(0.0f));

    // Idling a cleared marker is a no-op, not an underflow.
    m.idle();
    m.idle();
    CHECK(m.frames == 0);
    CHECK(m.value == doctest::Approx(0.0f));
}

TEST_CASE("peak marker: idle never arms a held-but-hidden peak") {
    M m;
    m.active(20.0f);            // peak held, linger unarmed
    REQUIRE_FALSE(m.visible());

    m.idle();
    CHECK_FALSE(m.visible());   // the opposite side must not reveal this one
    CHECK(m.value == doctest::Approx(20.0f));
}

// The full arc, in one readable sequence.
TEST_CASE("peak marker: climbThenRetreat — a whole lean in and out") {
    M m;
    for (float v : {5.0f, 15.0f, 25.0f, 35.0f}) {   // leaning in
        m.active(v);
        CHECK_FALSE(m.visible());
        CHECK(m.value == doctest::Approx(v));
    }
    m.active(30.0f);                                 // starting to pick it up
    CHECK(m.visible());
    CHECK(m.value == doctest::Approx(35.0f));        // marks the apex, not the live angle

    for (int i = 0; i < kLinger - 1; ++i) m.active(5.0f);
    CHECK(m.visible());
    CHECK(m.value == doctest::Approx(35.0f));

    m.active(5.0f);
    CHECK_FALSE(m.visible());                        // faded out
}

TEST_CASE("peak marker: snapOnImpact pins the reading when nothing is drawn") {
    float value = 0.0f;
    int frames = 0;
    PeakMarker::snapOnImpact(value, frames, 12.0f, 1.0f, kLinger);
    CHECK(value == doctest::Approx(12.0f));
    CHECK(frames == kLinger);
}

TEST_CASE("peak marker: snapOnImpact respects the deadzone") {
    float value = 0.0f;
    int frames = 0;
    PeakMarker::snapOnImpact(value, frames, 0.5f, 1.0f, kLinger);   // below deadzone
    CHECK(frames == 0);
    CHECK(value == doctest::Approx(0.0f));

    PeakMarker::snapOnImpact(value, frames, 1.0f, 1.0f, kLinger);   // exactly at it: excluded
    CHECK(frames == 0);
}

// The reason snapOnImpact gates on frames and not value: a still-visible marker
// holds an earlier, higher peak, which is the more informative reading.
TEST_CASE("peak marker: snapOnImpact never overwrites a still-visible higher peak") {
    M m;
    m.active(40.0f);
    m.active(30.0f);
    REQUIRE(m.visible());

    PeakMarker::snapOnImpact(m.value, m.frames, 5.0f, 1.0f, kLinger);
    CHECK(m.value == doctest::Approx(40.0f));   // the apex survived the impact value
}

// ... but a held-but-hidden peak (steady input, linger never armed) IS the case
// snapOnImpact exists for: without it the crash display shows nothing at all.
TEST_CASE("peak marker: snapOnImpact fires over a held-but-hidden peak") {
    M m;
    m.active(40.0f);
    REQUIRE_FALSE(m.visible());

    PeakMarker::snapOnImpact(m.value, m.frames, 5.0f, 1.0f, kLinger);
    CHECK(m.value == doctest::Approx(5.0f));
    CHECK(m.visible());
}

TEST_CASE("peak marker: collapseToMostRecent keeps the newer of two markers") {
    SUBCASE("B is more recent (higher remaining count)") {
        float va = 10.0f, vb = 20.0f;
        int fa = 2, fb = 4;
        PeakMarker::collapseToMostRecent(va, fa, vb, fb);
        CHECK(fa == 0);
        CHECK(va == doctest::Approx(0.0f));
        CHECK(fb == 4);
        CHECK(vb == doctest::Approx(20.0f));
    }
    SUBCASE("A is more recent") {
        float va = 10.0f, vb = 20.0f;
        int fa = 5, fb = 1;
        PeakMarker::collapseToMostRecent(va, fa, vb, fb);
        CHECK(fb == 0);
        CHECK(vb == doctest::Approx(0.0f));
        CHECK(fa == 5);
    }
    SUBCASE("a tie keeps side A") {
        float va = 10.0f, vb = 20.0f;
        int fa = 3, fb = 3;
        PeakMarker::collapseToMostRecent(va, fa, vb, fb);
        CHECK(fa == 3);
        CHECK(fb == 0);
    }
    SUBCASE("only one visible: left alone") {
        float va = 10.0f, vb = 0.0f;
        int fa = 3, fb = 0;
        PeakMarker::collapseToMostRecent(va, fa, vb, fb);
        CHECK(fa == 3);
        CHECK(va == doctest::Approx(10.0f));
    }
    SUBCASE("neither visible: no-op") {
        float va = 7.0f, vb = 9.0f;
        int fa = 0, fb = 0;
        PeakMarker::collapseToMostRecent(va, fa, vb, fb);
        CHECK(va == doctest::Approx(7.0f));   // values untouched when nothing is drawn
        CHECK(vb == doctest::Approx(9.0f));
    }
}

TEST_CASE("peak marker: clear resets both halves of the state") {
    float value = 33.0f;
    int frames = 4;
    PeakMarker::clear(value, frames);
    CHECK(value == doctest::Approx(0.0f));
    CHECK(frames == 0);
}

// Whatever the input sequence, the two values must stay consistent: the linger
// never goes negative, and a cleared linger never leaves a stale peak behind.
TEST_CASE("peak marker: invariants hold across an arbitrary input sweep") {
    M m;
    float v = 0.0f;
    for (int step = 0; step < 4000; ++step) {
        // A deterministic zig-zag that repeatedly crosses the deadband.
        v = static_cast<float>((step * 7) % 53);
        if (step % 11 == 0) m.idle(); else m.active(v);

        CAPTURE(step);
        CAPTURE(v);
        CHECK(m.frames >= 0);
        CHECK(m.value >= 0.0f);
        CHECK(m.frames <= kLinger);
        if (m.frames == 0) CHECK(((m.value == doctest::Approx(0.0f)) || !m.visible()));
    }
}
