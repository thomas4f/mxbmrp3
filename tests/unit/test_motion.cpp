// ============================================================================
// tests/unit/test_motion.cpp
// Unit tests for core/motion.h - Motion's timing and easing: how far a HUD is
// through fading in or out, given whether it draws and how much time passed.
//
// The cases worth pinning are the ones that show as a visible fault in game:
// a HUD fading in on the frame Motion is switched on (it must start settled),
// a one-frame gap in a notice blinking it back to zero (the fade must turn
// round from where it is), and a long gap in drawing finishing every fade at
// once (one frame may only step so far). The collect side is motion_test.
// ============================================================================
#include "doctest.h"

#include "core/motion.h"

using namespace Motion;

namespace {

const Profile kNormal = profileFor(Level::NORMAL);
const Profile kSubtle = profileFor(Level::SUBTLE);

// Run `ms` of frames at `frameMs` each, returning the last step.
Frame run(Track& t, bool drawn, float ms, const Profile& pr, float frameMs = 4.0f, uint32_t epoch = 0) {
    Frame f = step(t, drawn, epoch, 0.0f, pr);
    for (float done = 0.0f; done < ms; done += frameMs) f = step(t, drawn, epoch, frameMs, pr);
    return f;
}

}  // namespace

TEST_CASE("motion: a HUD seen for the first time is settled, shown or hidden") {
    Track shown;
    const Frame a = step(shown, true, 0, 16.0f, kNormal);
    CHECK(a.emit);
    CHECK_FALSE(a.moving());

    Track hidden;
    const Frame b = step(hidden, false, 0, 16.0f, kNormal);
    CHECK_FALSE(b.emit);
}

TEST_CASE("motion: appearing fades in and rises, monotonically in alpha, settled after inMs") {
    for (const Profile& pr : { kSubtle, kNormal }) {
        Track t;
        step(t, false, 0, 0.0f, pr);
        float prevAlpha = -1.0f;
        bool sawBelow = false;
        for (float ms = 0.0f; ms < pr.inMs; ms += 4.0f) {
            const Frame f = step(t, true, 0, 4.0f, pr);
            CHECK(f.emit);
            CHECK(f.alpha >= prevAlpha);
            prevAlpha = f.alpha;
            if (f.dy > 0.0f) sawBelow = true;   // starts below its place
        }
        CHECK(sawBelow);
        const Frame done = step(t, true, 0, 4.0f, pr);
        CHECK_FALSE(done.moving());
        CHECK(done.alpha == 1.0f);
        CHECK(done.dy == 0.0f);
    }
}

TEST_CASE("motion: Normal springs past its place on the way in, Subtle does not") {
    auto overshoots = [](const Profile& pr) {
        Track t;
        step(t, false, 0, 0.0f, pr);
        bool above = false;
        for (float ms = 0.0f; ms <= pr.inMs; ms += 2.0f)
            if (step(t, true, 0, 2.0f, pr).dy < 0.0f) above = true;
        return above;
    };
    CHECK(overshoots(kNormal));
    CHECK_FALSE(overshoots(kSubtle));
}

TEST_CASE("motion: disappearing keeps emitting while it fades, then stops") {
    Track t;
    step(t, true, 0, 0.0f, kNormal);
    const Frame mid = run(t, false, kNormal.outMs * 0.5f, kNormal);
    CHECK(mid.emit);
    CHECK(mid.alpha > 0.0f);
    CHECK(mid.alpha < 1.0f);
    CHECK(mid.dy > 0.0f);   // sinks as it goes
    const Frame gone = run(t, false, kNormal.outMs, kNormal);
    CHECK_FALSE(gone.emit);
}

TEST_CASE("motion: shown again mid-fade turns round from where it is, not from zero") {
    Track t;
    step(t, true, 0, 0.0f, kSubtle);
    const Frame out = step(t, false, 0, 8.0f, kSubtle);   // one frame of a gap
    const Frame back = step(t, true, 0, 8.0f, kSubtle);
    CHECK(out.alpha > 0.9f);
    CHECK(back.alpha >= out.alpha);
}

TEST_CASE("motion: one frame steps at most MAX_STEP_MS, so a long gap still animates") {
    Track t;
    step(t, false, 0, 0.0f, kNormal);
    const Frame f = step(t, true, 0, 5000.0f, kNormal);   // drawing resumed after menus
    CHECK(f.moving());
    CHECK(t.p == doctest::Approx(MAX_STEP_MS / kNormal.inMs));
}

TEST_CASE("motion: a part epoch change fades the part in over a settled HUD only") {
    Track t;
    step(t, true, 1, 0.0f, kNormal);           // settled, epoch 1
    const Frame f = step(t, true, 2, 4.0f, kNormal);
    CHECK_FALSE(f.moving());
    CHECK(f.partMoving());
    const Frame done = run(t, true, kNormal.inMs, kNormal, 4.0f, 2);
    CHECK_FALSE(done.partMoving());

    // While the whole HUD is still coming in, its own fade covers the part.
    Track coming;
    step(coming, false, 1, 0.0f, kNormal);
    const Frame g = step(coming, true, 2, 4.0f, kNormal);
    CHECK(g.moving());
    CHECK_FALSE(g.partMoving());
}

TEST_CASE("motion: scaleAlpha scales the alpha byte only") {
    CHECK(scaleAlpha(0xFF123456ul, 1.0f) == 0xFF123456ul);
    CHECK(scaleAlpha(0xFF123456ul, 0.0f) == 0x00123456ul);
    CHECK(scaleAlpha(0x80123456ul, 0.5f) == 0x40123456ul);
    CHECK(scaleAlpha(0xFF123456ul, 2.0f) == 0xFF123456ul);   // clamped
}

TEST_CASE("motion: Off is no motion at all") {
    const Profile off = profileFor(Level::OFF);
    Track t;
    step(t, true, 0, 0.0f, off);
    const Frame hide = step(t, false, 0, 4.0f, off);
    CHECK_FALSE(hide.emit);
    const Frame show = step(t, true, 0, 4.0f, off);
    CHECK_FALSE(show.moving());
}
