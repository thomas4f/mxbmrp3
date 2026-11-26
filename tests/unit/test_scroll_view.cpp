// ============================================================================
// tests/unit/test_scroll_view.cpp
// The settings lists' scrolling (mxbmrp3/hud/settings/scroll_view.h):
//   - a list rests on a row's top, never past the row that leaves the last one
//     in view, and a list that fits does not scroll at all
//   - a step moves whole rows, at least two lines' worth, and stops at
//     both ends; the track moves a view's worth and always at least one row
//   - the one edge rule every list hands its rows to (listEdge: faded over a
//     third of a line while moving, a part row hidden at rest); the rule
//     itself is test_clip_marks.cpp's
//   - with Motion off a move lands at once; with it on the list glides from
//     where it is DRAWN, so a second move mid-glide does not jump back
//   - the thumb's share and place, and the drag's inverse of them
//   - THE ONE INPUT CORE every list answers its bar with (press / follow; the
//     HUD lists' too, hud_scroll.h): which part a point hits (the hit strip
//     wider than the bar), an arrow's step and hold-repeat that stops off its
//     cell, the thumb held and let go, the track's page; and where the bar sits
// ============================================================================
#include "doctest.h"

#include "../../mxbmrp3/hud/settings/scroll_view.h"

#include <vector>

namespace {
// Twelve one-line rows.
std::vector<float> oneLineRows(int n) {
    std::vector<float> t;
    for (int i = 0; i < n; ++i) t.push_back(static_cast<float>(i));
    return t;
}
// The achievements shape: a heading row, then two-line entries.
std::vector<float> headingAndEntries(int entries) {
    std::vector<float> t{ 0.0f };
    for (int i = 0; i < entries; ++i) t.push_back(1.0f + 2.0f * static_cast<float>(i));
    return t;
}
}  // namespace

TEST_CASE("scroll view: the last rest leaves the last row in view; a list that fits does not scroll") {
    const std::vector<float> rows = oneLineRows(12);
    CHECK(ScrollView::maxRest(rows, 12.0f, 8.0f) == doctest::Approx(4.0f));
    CHECK(ScrollView::maxRest(rows, 12.0f, 12.0f) == doctest::Approx(0.0f));
    CHECK(ScrollView::maxRest(rows, 12.0f, 20.0f) == doctest::Approx(0.0f));
    // Two-line entries under a heading, 13 lines of view: the first top from
    // which everything after fits.
    const std::vector<float> ach = headingAndEntries(10);   // 21 lines
    CHECK(ScrollView::maxRest(ach, 21.0f, 13.5f) == doctest::Approx(9.0f));
}

TEST_CASE("scroll view: snapping picks the nearest resting row, never past the last rest") {
    const std::vector<float> ach = headingAndEntries(10);
    const float limit = ScrollView::maxRest(ach, 21.0f, 13.5f);
    CHECK(ScrollView::snap(ach, limit, 4.2f) == doctest::Approx(5.0f));
    CHECK(ScrollView::snap(ach, limit, 3.9f) == doctest::Approx(3.0f));
    CHECK(ScrollView::snap(ach, limit, 50.0f) == doctest::Approx(limit));
    CHECK(ScrollView::snap(ach, limit, -3.0f) == doctest::Approx(0.0f));
}

TEST_CASE("scroll view: a step is whole rows, two lines at least, clamped at both ends") {
    const std::vector<float> rows = oneLineRows(30);
    const float limit = ScrollView::maxRest(rows, 30.0f, 8.0f);
    CHECK(ScrollView::step(rows, limit, 0.0f, 1) == doctest::Approx(2.0f));
    CHECK(ScrollView::step(rows, limit, 2.0f, 3) == doctest::Approx(8.0f));
    CHECK(ScrollView::step(rows, limit, 4.0f, -1) == doctest::Approx(2.0f));
    CHECK(ScrollView::step(rows, limit, 0.0f, -1) == doctest::Approx(0.0f));
    CHECK(ScrollView::step(rows, limit, 20.0f, 5) == doctest::Approx(limit));
    // A two-line entry is a step by itself; the one-line heading is not.
    const std::vector<float> ach = headingAndEntries(10);
    const float achLimit = ScrollView::maxRest(ach, 21.0f, 13.5f);
    CHECK(ScrollView::step(ach, achLimit, 0.0f, 1) == doctest::Approx(3.0f));
    CHECK(ScrollView::step(ach, achLimit, 3.0f, 1) == doctest::Approx(5.0f));
}

TEST_CASE("scroll view: the track moves a view's worth, at least one row") {
    const std::vector<float> rows = oneLineRows(30);
    const float limit = ScrollView::maxRest(rows, 30.0f, 8.0f);
    CHECK(ScrollView::page(rows, limit, 0.0f, 8.0f, 1) == doctest::Approx(8.0f));
    CHECK(ScrollView::page(rows, limit, 8.0f, 8.0f, -1) == doctest::Approx(0.0f));
    CHECK(ScrollView::page(rows, limit, 20.0f, 8.0f, 1) == doctest::Approx(limit));
    // A view shorter than a row still moves.
    const std::vector<float> tall{ 0.0f, 3.0f, 6.0f, 9.0f };
    CHECK(ScrollView::page(tall, 6.0f, 0.0f, 2.0f, 1) == doctest::Approx(3.0f));
    CHECK(ScrollView::page(tall, 6.0f, 6.0f, 2.0f, -1) == doctest::Approx(3.0f));
}

TEST_CASE("scroll view: Motion off lands at once; on, it glides from where it is drawn") {
    ScrollView::State s;
    ScrollView::glideTo(s, 4.0f, 1000, Motion::Level::OFF);
    CHECK(s.shown == doctest::Approx(4.0f));
    CHECK_FALSE(s.gliding);
    CHECK_FALSE(ScrollView::tick(s, 2000, Motion::Level::OFF));

    ScrollView::State g;
    const long long t0 = 1000000;
    const long long dur = static_cast<long long>(ScrollView::glideMs(Motion::Level::SUBTLE) * 1000.0f);
    ScrollView::glideTo(g, 4.0f, t0, Motion::Level::SUBTLE);
    CHECK(g.shown == doctest::Approx(0.0f));
    CHECK(ScrollView::tick(g, t0 + dur / 2, Motion::Level::SUBTLE));
    const float mid = g.shown;
    CHECK(mid > 2.0f);   // ease-out: past halfway at half time
    CHECK(mid < 4.0f);
    // A second move now heads on from the drawn position, not from 0 or 4.
    ScrollView::glideTo(g, 6.0f, t0 + dur / 2, Motion::Level::SUBTLE);
    CHECK(g.from == doctest::Approx(mid));
    ScrollView::tick(g, t0 + dur / 2 + 1, Motion::Level::SUBTLE);
    CHECK(g.shown >= mid);
    CHECK(ScrollView::tick(g, t0 + dur * 2, Motion::Level::SUBTLE));
    CHECK(g.shown == doctest::Approx(6.0f));
    CHECK_FALSE(g.gliding);
    CHECK(ScrollView::glideMs(Motion::Level::NORMAL) > ScrollView::glideMs(Motion::Level::SUBTLE));
}

TEST_CASE("scroll view: the thumb is the share in view, placed by the rest, and a drag inverts it") {
    float y = 0.0f, h = 0.0f;
    ScrollView::thumb(0.4f, 40.0f, 10.0f, 0.0f, 30.0f, 0.02f, y, h);
    CHECK(h == doctest::Approx(0.1f));
    CHECK(y == doctest::Approx(0.0f));
    ScrollView::thumb(0.4f, 40.0f, 10.0f, 30.0f, 30.0f, 0.02f, y, h);
    CHECK(y + h == doctest::Approx(0.4f));
    // A very long list keeps a thumb worth grabbing.
    ScrollView::thumb(0.4f, 4000.0f, 10.0f, 0.0f, 3990.0f, 0.02f, y, h);
    CHECK(h == doctest::Approx(0.02f));
    // The drag's inverse.
    CHECK(ScrollView::offsetForThumb(0.4f, 0.1f, 0.15f, 30.0f) == doctest::Approx(15.0f));
    CHECK(ScrollView::offsetForThumb(0.4f, 0.1f, -1.0f, 30.0f) == doctest::Approx(0.0f));
    CHECK(ScrollView::offsetForThumb(0.4f, 0.1f, 9.0f, 30.0f) == doctest::Approx(30.0f));
}

TEST_CASE("scroll view: every list's edge rule fades a third of a line while moving, hides a part row at rest") {
    const Edge moving = ScrollView::listEdge(0.03f, true);
    CHECK(moving.start == 0.0f);
    CHECK(moving.ramp == doctest::Approx(0.01f));
    CHECK_FALSE(moving.hidePart);
    CHECK(ScrollView::listEdge(0.03f, false).hidePart);
}

namespace {
// A list's moves, recorded: what the core asked for, nothing moved itself.
struct Recorded {
    int steps = 0, pages = 0, drags = 0, ends = 0;
    float lastDrag = -1.0f;
    bool step(int dir) { steps += dir; return true; }
    bool page(int dir) { pages += dir; return true; }
    bool dragTo(float lines) { ++drags; lastDrag = lines; return true; }
    void dragEnd() { ++ends; }
};
// A bar 0.01 wide at x 0.5, a 0.3 view from 0.1, 0.03 cells, thumb 0.06 tall
// a third of the way down a 0.24 track.
ScrollView::State laidOutBar() {
    ScrollView::State s;
    s.barX = 0.5f; s.barW = 0.01f;
    s.viewY = 0.1f; s.viewH = 0.3f; s.lineH = 0.03f;
    s.trackY = 0.13f; s.trackH = 0.24f;
    s.thumbY = 0.19f; s.thumbH = 0.06f;
    s.maxRest = 30.0f;
    return s;
}
}  // namespace

TEST_CASE("scroll bar: the part a point hits, on a strip wider than the bar") {
    const ScrollView::State s = laidOutBar();
    CHECK(ScrollView::partAt(s, 0.505f, 0.11f) == ScrollView::PART_UP);
    CHECK(ScrollView::partAt(s, 0.505f, 0.2f) == ScrollView::PART_TRACK);
    CHECK(ScrollView::partAt(s, 0.505f, 0.39f) == ScrollView::PART_DOWN);
    // The strip reaches (HIT_CHARS - BAR_CHARS) / 2 cells past each side
    const float slop = s.barW * ((ScrollView::HIT_CHARS - ScrollView::BAR_CHARS) * 0.5f / ScrollView::BAR_CHARS);
    CHECK(ScrollView::partAt(s, 0.5f - slop * 0.9f, 0.2f) == ScrollView::PART_TRACK);
    CHECK(ScrollView::partAt(s, 0.5f - slop * 1.1f, 0.2f) == ScrollView::PART_NONE);
    CHECK(ScrollView::partAt(s, 0.505f, 0.41f) == ScrollView::PART_NONE);
    // Centred in the padding; a padding thinner than the bar takes it flush
    CHECK(ScrollView::barLeft(0.8f, 0.9f, 0.02f) == doctest::Approx(0.84f));
    CHECK(ScrollView::barLeft(0.895f, 0.9f, 0.02f) == doctest::Approx(0.88f));
}

TEST_CASE("scroll bar: an arrow steps and repeats while held on it; the thumb drags; the track pages") {
    const Motion::Level off = Motion::Level::OFF;
    ScrollView::State s = laidOutBar();
    Recorded m;
    const long long t0 = 1'000'000;
    // The down arrow: a step on the press...
    ScrollView::press(s, ScrollView::PART_DOWN, 0.39f, t0, m);
    CHECK(m.steps == 1);
    CHECK(s.arrowDir == 1);
    // ...none while the hold delay runs, then one on the curve
    ScrollView::follow(s, 0.505f, 0.39f, true, true, t0 + (HoldRepeat::INITIAL_DELAY_MS - 1) * 1000, off, 50, m);
    CHECK(m.steps == 1);
    ScrollView::follow(s, 0.505f, 0.39f, true, true, t0 + HoldRepeat::INITIAL_DELAY_MS * 1000, off, 50, m);
    CHECK(m.steps == 2);
    // Held on, the repeats quicken along the curve to the fast interval:
    // more steps in the next 5 s than the slow interval alone would give
    const int before = m.steps;
    for (long long ms = HoldRepeat::INITIAL_DELAY_MS + 1; ms <= HoldRepeat::INITIAL_DELAY_MS + 5000; ++ms)
        ScrollView::follow(s, 0.505f, 0.39f, true, true, t0 + ms * 1000, off, 50, m);
    CHECK(m.steps - before > 5000 / HoldRepeat::SLOW_MS + 10);
    CHECK(s.arrowRepeats > HoldRepeat::ACCEL_REPEATS);
    // Off its cell (onto the track) it stops, held or not
    const int held = m.steps;
    CHECK(ScrollView::follow(s, 0.505f, 0.2f, true, true, t0 + 6'000'000, off, 50, m));
    CHECK(s.arrowDir == 0);
    CHECK(m.steps == held);

    // The thumb: held, it follows the pointer; let go, the list rests
    ScrollView::press(s, ScrollView::PART_TRACK, 0.2f, t0, m);
    REQUIRE(s.dragging);
    CHECK(s.grabY == doctest::Approx(0.01f));
    CHECK(ScrollView::follow(s, 0.505f, 0.37f, true, true, t0, off, 50, m));   // thumb to the track's foot
    CHECK(m.lastDrag == doctest::Approx(30.0f));
    CHECK_FALSE(ScrollView::follow(s, 0.505f, 0.37f, true, true, t0, off, 50, m));   // no move, nothing redrawn
    ScrollView::follow(s, 0.505f, 0.37f, true, false, t0, off, 50, m);
    CHECK_FALSE(s.dragging);
    CHECK(m.ends == 1);

    // The track either side of the thumb: a page toward the press
    ScrollView::press(s, ScrollView::PART_TRACK, 0.14f, t0, m);
    CHECK(m.pages == -1);
    ScrollView::press(s, ScrollView::PART_TRACK, 0.36f, t0, m);
    CHECK(m.pages == 0);
}
