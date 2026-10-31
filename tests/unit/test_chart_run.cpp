// ============================================================================
// tests/unit/test_chart_run.cpp
// ChartRun (hud/chart_run.h): when a graph's next piece may be folded into the
// open run. The look depends on the quarter-pixel line: a joint left within it
// merges, one past it starts a new piece. Coordinates are written in 1080p
// pixels and handed over screen-normalized, as BaseHud::addChartSegment does.
// ============================================================================
#include "doctest.h"

#include "hud/chart_run.h"

namespace {
float nx(float px) { return px / ChartRun::PX_W; }
float ny(float px) { return px / ChartRun::PX_H; }

// A run from (0,0) to a joint at (10, jointY), offered the piece on to (20, 0):
// the joint sits jointY pixels (vertically) off the merged line.
bool mergesWithJointOff(float jointY) {
    ChartRun run;
    run.start(0, 0xFFFFFFFFul, 1.0f, nx(0), ny(100), nx(10), ny(100 + jointY));
    return run.extend(nx(10), ny(100 + jointY), nx(20), ny(100));
}
}  // namespace

TEST_CASE("chart run: a joint within a quarter pixel merges, one past it does not") {
    CHECK(mergesWithJointOff(0.0f));
    CHECK(mergesWithJointOff(0.24f));
    CHECK(mergesWithJointOff(-0.24f));
    CHECK_FALSE(mergesWithJointOff(0.26f));
    CHECK_FALSE(mergesWithJointOff(-0.26f));
}

TEST_CASE("chart run: every joint so far still holds, not only the last") {
    // Joints at +0.2 and then -0.2 px off the line to the end: each alone is
    // within the quarter pixel, but the cone of the first must still bind the
    // third piece's end.
    ChartRun run;
    run.start(0, 1ul, 1.0f, nx(0), ny(0), nx(10), ny(0.2f));
    REQUIRE(run.extend(nx(10), ny(0.2f), nx(20), ny(0.0f)));
    // An end that keeps the second joint on the line but pulls the first one
    // 0.5 px off: refused.
    CHECK_FALSE(run.extend(nx(20), ny(0.0f), nx(30), ny(-1.5f)));
    // A level end: both joints within reach, accepted.
    CHECK(run.extend(nx(20), ny(0.0f), nx(30), ny(0.0f)));
}

TEST_CASE("chart run: a level line runs the whole graph") {
    ChartRun run;
    run.start(0, 1ul, 1.0f, nx(0), ny(50), nx(4), ny(50));
    for (int i = 1; i < 500; ++i)
        REQUIRE(run.extend(nx(4.0f * i), ny(50), nx(4.0f * (i + 1)), ny(50)));
    CHECK(run.ex == nx(2000));
}

TEST_CASE("chart run: only a piece carrying on to the right extends") {
    ChartRun run;
    CHECK_FALSE(run.extend(nx(0), ny(0), nx(10), ny(0)));        // no open run
    run.start(0, 1ul, 1.0f, nx(0), ny(0), nx(10), ny(0));
    CHECK_FALSE(run.extend(nx(11), ny(0), nx(20), ny(0)));       // a gap
    CHECK_FALSE(run.extend(nx(10), ny(0), nx(10), ny(30)));      // vertical step
    CHECK_FALSE(run.extend(nx(10), ny(0), nx(5), ny(0)));        // backwards
    run.reset();
    CHECK_FALSE(run.extend(nx(10), ny(0), nx(20), ny(0)));       // reset
}
