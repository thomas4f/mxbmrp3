// ============================================================================
// tests/unit/test_session_charts_math.cpp
// Unit tests for the pure race-progression chart derivations in
// hud/session_charts_math.h — cumulative time, reference pace, trace value,
// per-lap position, gap to leader, and the pace-chart outlier predicate.
//
// The header is dependency-free (standard library only), so it compiles and
// runs here with no game engine, no Windows, no PluginData. This TU does NOT
// define the doctest main (test_plugin_utils.cpp does) — see run_tests.sh.
// ============================================================================
#include "doctest.h"

#include "hud/session_charts_math.h"

#include <vector>
#include <string>

namespace RC = SessionChartsMath;

TEST_CASE("cumulative: running sum, oldest-first") {
    CHECK(RC::cumulative({}) == std::vector<long long>{});
    CHECK(RC::cumulative({1000}) == std::vector<long long>{1000});
    CHECK(RC::cumulative({1000, 1010, 990}) ==
          std::vector<long long>{1000, 2010, 3000});
}

TEST_CASE("bestLapSoFar: running minimum, oldest-first") {
    CHECK(RC::bestLapSoFar({}) == std::vector<long long>{});
    CHECK(RC::bestLapSoFar({61000}) == std::vector<long long>{61000});
    // Improves on lap 2, holds when lap 3 is slower, improves again on lap 4.
    CHECK(RC::bestLapSoFar({61000, 60000, 60500, 59500}) ==
          std::vector<long long>{61000, 60000, 60000, 59500});
}

TEST_CASE("bestLapSoFar (validity-aware): invalid laps don't set the running best") {
    // Lap 2 is invalid (e.g. a cut-track 58.0 that must NOT become the quali time).
    // The running best holds the valid lap-1 time until a faster VALID lap arrives.
    std::vector<int>  laps  = {60000, 58000, 59000, 57000};
    std::vector<char> valid = {1,     0,     1,     0    };
    // lap1 60.0 (valid) -> 60.0; lap2 58.0 (INVALID) -> still 60.0; lap3 59.0 (valid)
    // -> 59.0; lap4 57.0 (INVALID) -> still 59.0.
    CHECK(RC::bestLapSoFar(laps, valid) ==
          std::vector<long long>{60000, 60000, 59000, 59000});

    // No valid lap yet -> sentinel (ranks last), then the first valid lap takes over.
    std::vector<int>  laps2  = {58000, 61000};
    std::vector<char> valid2 = {0,     1    };
    auto out = RC::bestLapSoFar(laps2, valid2);
    CHECK(out[0] == RC::kNoValidLap);
    CHECK(out[1] == 61000);

    // A missing validity entry is treated as valid (defensive), matching the plain
    // overload for an all-valid series.
    CHECK(RC::bestLapSoFar({61000, 60000}, {}) == RC::bestLapSoFar({61000, 60000}));
}

TEST_CASE("bestLapSoFar feeds positions/gaps like cumulative (provisional quali)") {
    // #5 sets a 60.0 then improves to 59.0; #9 sets 59.5 on lap 2 (was slower lap 1).
    std::vector<std::vector<long long>> best = {
        RC::bestLapSoFar({60000, 59000}),  // #5 -> {60000, 59000}
        RC::bestLapSoFar({61000, 59500}),  // #9 -> {61000, 59500}
    };
    std::vector<int> nums = {5, 9};
    auto pos = RC::positionsPerLap(best, nums);
    CHECK(pos[0][0] == 1);  // lap1: #5 (60.0) ahead of #9 (61.0)
    CHECK(pos[1][0] == 2);
    CHECK(pos[0][1] == 1);  // lap2: #5 (59.0) still ahead of #9 (59.5)
    CHECK(pos[1][1] == 2);
    auto gap = RC::gapToLeaderPerLap(best);
    CHECK(gap[0][1] == 0);      // #5 is session-best
    CHECK(gap[1][1] == 500);    // #9 is 0.5s off the best lap
}

TEST_CASE("referencePaceMs: leader average, divide-by-zero guarded") {
    CHECK(RC::referencePaceMs(60000, 0) == 0);       // no laps -> guarded
    CHECK(RC::referencePaceMs(0, 5) == 0);
    CHECK(RC::referencePaceMs(300000, 5) == 60000);  // 5 x 60s
    CHECK(RC::referencePaceMs(90000, 4) == 22500);
}

TEST_CASE("traceValueMs: zero at reference pace, positive when ahead") {
    // Rider exactly at reference pace after 3 laps -> zero (hugs the zero line).
    CHECK(RC::traceValueMs(60000, 3, 180000) == 0);
    // Faster than reference (less cumulative time) -> positive (plotted higher).
    CHECK(RC::traceValueMs(60000, 3, 179000) == 1000);
    // Slower than reference -> negative.
    CHECK(RC::traceValueMs(60000, 3, 181000) == -1000);
}

TEST_CASE("leaderIndex: most laps, then lowest cumulative, then raceNum") {
    // Rider 7 and 3 both did 2 laps; 3 is faster overall -> 3 leads.
    std::vector<std::vector<long long>> cum = {
        {1000, 2000},   // #7  total 2000
        {1000, 1900},   // #3  total 1900  <- leader
    };
    std::vector<int> nums = {7, 3};
    CHECK(RC::leaderIndex(cum, nums) == 1);

    // More laps beats lower cumulative-so-far: #9 has 3 laps.
    std::vector<std::vector<long long>> cum2 = {
        {1000, 1900},        // #3  2 laps
        {1000, 2000, 3000},  // #9  3 laps  <- leader (completed more)
    };
    std::vector<int> nums2 = {3, 9};
    CHECK(RC::leaderIndex(cum2, nums2) == 1);

    // Empty -> -1.
    CHECK(RC::leaderIndex({}, {}) == -1);
    CHECK(RC::leaderIndex({{}}, {5}) == -1);
}

TEST_CASE("positionsPerLap: overtake changes rank mid-race") {
    // Lap 1: #1 leads (1000 < 1100). Lap 2: #2 puts in a flyer and takes the
    // lead on cumulative time (1900 < 2050) -> the lines cross.
    std::vector<std::vector<long long>> cum = {
        {1000, 2050},   // #1
        {1100, 1900},   // #2
    };
    std::vector<int> nums = {1, 2};
    auto pos = RC::positionsPerLap(cum, nums);
    // Lap index 0
    CHECK(pos[0][0] == 1);  // #1 leads
    CHECK(pos[1][0] == 2);
    // Lap index 1 (overtake)
    CHECK(pos[0][1] == 2);
    CHECK(pos[1][1] == 1);  // #2 now leads
}

TEST_CASE("positionsPerLap: ties broken by raceNum, absent laps are 0") {
    std::vector<std::vector<long long>> cum = {
        {1000, 2000},   // #5  (raceNum 5)
        {1000},         // #2  only one lap -> no data at lap index 1
    };
    std::vector<int> nums = {5, 2};
    auto pos = RC::positionsPerLap(cum, nums);
    // Lap 0 tie on 1000 -> lower raceNum (2) ranks first.
    CHECK(pos[1][0] == 1);  // #2
    CHECK(pos[0][0] == 2);  // #5
    // Rider #2 has no lap index 1 (row length matches completed laps).
    CHECK(pos[1].size() == 1);
    // At lap 1 only #5 present -> position 1.
    CHECK(pos[0][1] == 1);
}

TEST_CASE("gapToLeaderPerLap: leader pinned to 0, others positive") {
    std::vector<std::vector<long long>> cum = {
        {1000, 2000},   // #1 leader both laps
        {1100, 2150},   // #2 behind
    };
    auto gap = RC::gapToLeaderPerLap(cum);
    CHECK(gap[0][0] == 0);
    CHECK(gap[0][1] == 0);
    CHECK(gap[1][0] == 100);
    CHECK(gap[1][1] == 150);
}

TEST_CASE("gapToLeaderPerLap: leader can change per lap") {
    std::vector<std::vector<long long>> cum = {
        {1000, 2100},   // #1 leads lap0, loses lap1
        {1100, 2000},   // #2 behind lap0, leads lap1
    };
    auto gap = RC::gapToLeaderPerLap(cum);
    CHECK(gap[0][0] == 0);     // #1 leads lap 0
    CHECK(gap[1][0] == 100);
    CHECK(gap[0][1] == 100);   // #1 now 100ms behind
    CHECK(gap[1][1] == 0);     // #2 leads lap 1
}

TEST_CASE("medianMs: odd, even, empty") {
    CHECK(RC::medianMs({}) == 0);
    CHECK(RC::medianMs({1500}) == 1500);
    CHECK(RC::medianMs({1000, 3000, 2000}) == 2000);       // odd
    CHECK(RC::medianMs({1000, 2000, 3000, 4000}) == 2500); // even -> avg of middle two
}

TEST_CASE("isOutlierLap: opening lap and slow laps flagged") {
    const int median = 60000;   // 60s racing pace
    const float factor = 1.4f;
    // Opening lap (index 0) is always an outlier.
    CHECK(RC::isOutlierLap(0, 59000, median, factor) == true);
    // A normal racing lap is not.
    CHECK(RC::isOutlierLap(3, 60500, median, factor) == false);
    // A lap slower than median * 1.4 (a crash/off) is.
    CHECK(RC::isOutlierLap(3, 90000, median, factor) == true);
    // Right at the threshold is not an outlier (strictly greater).
    CHECK(RC::isOutlierLap(3, static_cast<int>(median * factor), median, factor) == false);
    // Non-positive median disables the ratio test (only opening lap filters).
    CHECK(RC::isOutlierLap(3, 999999, 0, factor) == false);
}

TEST_CASE("percentileSorted: interpolates on a sorted sample") {
    std::vector<long long> s = {0, 10, 20, 30, 40};  // sorted
    CHECK(RC::percentileSorted(s, 0.0) == 0);
    CHECK(RC::percentileSorted(s, 1.0) == 40);
    CHECK(RC::percentileSorted(s, 0.5) == 20);        // exact middle
    CHECK(RC::percentileSorted(s, 0.25) == 10);       // exact quartile
    CHECK(RC::percentileSorted(s, 0.75) == 30);
    CHECK(RC::percentileSorted({}, 0.5) == 0);        // empty
    CHECK(RC::percentileSorted({7}, 0.5) == 7);       // single
    // Between grid points it interpolates linearly.
    std::vector<long long> t = {0, 100};
    CHECK(RC::percentileSorted(t, 0.5) == 50);
}

TEST_CASE("robustRange: well-behaved data is NOT clipped") {
    // An even spread: the Tukey fence lies outside the data, so the range equals
    // plain min/max — clipping must not kick in without a real outlier.
    std::vector<long long> v = {0, 1000, 2000, 3000, 4000, 5000, 6000, 7000};
    RC::AxisRange r = RC::robustRange(v);
    CHECK(r.valid);
    CHECK(r.lo == 0);
    CHECK(r.hi == 7000);
}

TEST_CASE("robustRange: a catastrophic outlier is fenced out of the range") {
    // A tight pack (0..8s) plus one rider who lost ~3.5 minutes. The huge value
    // must NOT set the axis maximum — the range should stay near the pack so the
    // outlier is clipped to the edge, not the pack crushed into a sliver.
    std::vector<long long> v = {0, 1000, 2000, 3000, 4000, 5000, 6000, 8000, 218000};
    RC::AxisRange r = RC::robustRange(v);
    CHECK(r.valid);
    CHECK(r.lo == 0);
    CHECK(r.hi < 20000);       // pack-scale, not 218000
    CHECK(r.hi >= 8000);       // still covers the real (non-outlier) pack top
}

TEST_CASE("robustRange: symmetric outlier (race-trace shape) fenced on the low side") {
    // Trace values: pack near 0, one rider way negative (behind the reference).
    std::vector<long long> v = {3000, 2000, 1000, 0, -1000, -2000, -3000, -220000};
    RC::AxisRange r = RC::robustRange(v);
    CHECK(r.valid);
    CHECK(r.hi == 3000);       // pack top preserved
    CHECK(r.lo > -20000);      // -220000 fenced out
    CHECK(r.lo <= -3000);      // still covers the real pack bottom
}

TEST_CASE("robustRange: tiny samples and degenerate spread fall back to min/max") {
    // Fewer than 4 points -> plain min/max (no meaningful quartiles).
    RC::AxisRange a = RC::robustRange({5000, 100000});
    CHECK(a.valid);
    CHECK(a.lo == 5000);
    CHECK(a.hi == 100000);
    // All identical -> IQR is 0 -> plain min/max.
    RC::AxisRange b = RC::robustRange({2000, 2000, 2000, 2000});
    CHECK(b.valid);
    CHECK(b.lo == 2000);
    CHECK(b.hi == 2000);
    // Empty -> invalid.
    RC::AxisRange c = RC::robustRange({});
    CHECK_FALSE(c.valid);
}

TEST_CASE("formatSecs: compact seconds labels, sign, and the minute carry") {
    char buf[24];
    auto fmt = [&](long long ms, bool sign) { RC::formatSecs(buf, sizeof(buf), ms, sign); return std::string(buf); };

    // Sub-minute: one decimal, optional 's'.
    CHECK(fmt(12300, false) == "12.3s");
    CHECK(fmt(59940, false) == "59.9s");   // just under a minute stays in the seconds form

    // Rounding to tenths (round half up, matching the JS mirror).
    CHECK(fmt(12340, false) == "12.3s");
    CHECK(fmt(12350, false) == "12.4s");

    // Sign handling (trace chart): +ahead / -behind / 0 unsigned.
    CHECK(fmt(3000, true)  == "+3.0s");
    CHECK(fmt(-3000, true) == "-3.0s");
    CHECK(fmt(0, true)     == "0.0s");

    // Minutes: zero-padded seconds field, unpadded minutes.
    CHECK(fmt(65300, false) == "1:05.3");
    CHECK(fmt(65300, true)  == "+1:05.3");
    CHECK(fmt(604000, false) == "10:04.0");

    // The carry: a value that rounds the seconds up to 60 must roll into the minute,
    // never print ":60.0" (the bug this guards — 59.96s -> "1:00.0", not "0:60.0").
    CHECK(fmt(59960, false)  == "1:00.0");
    CHECK(fmt(59950, false)  == "1:00.0");
    CHECK(fmt(119960, false) == "2:00.0");

    // Sign is applied to the magnitude, so a negative value that also rounds across
    // the minute boundary carries the same way (gap/trace charts can go negative).
    CHECK(fmt(-59960, true)  == "-1:00.0");
    CHECK(fmt(-119960, true) == "-2:00.0");
}

// ---------------------------------------------------------------------------
// Sector resolution
// ---------------------------------------------------------------------------

TEST_CASE("charts: cumulativeBySector sums sector times into a per-sector series") {
    using SessionChartsMath::cumulativeBySector;

    // Two 3-sector laps: 30/31/29 then 30/30/30.
    const std::vector<int> sectors = { 30000, 31000, 29000, 30000, 30000, 30000 };
    const auto cum = cumulativeBySector(sectors, 3);
    REQUIRE(cum.size() == 6);
    CHECK(cum[0] == 30000);
    CHECK(cum[2] == 90000);    // end of lap 1 == that lap's per-lap cumulative
    CHECK(cum[5] == 180000);   // end of lap 2

    // The per-lap series must remain a strict subset: every lap boundary in the sector
    // series equals the same lap's value from cumulative(). That equality is what lets a
    // chart switch resolution without the line moving.
    const auto perLap = SessionChartsMath::cumulative(std::vector<int>{ 90000, 90000 });
    REQUIRE(perLap.size() == 2);
    CHECK(cum[2] == perLap[0]);
    CHECK(cum[5] == perLap[1]);
}

TEST_CASE("charts: a sector series stops at the first hole rather than skipping it") {
    using SessionChartsMath::cumulativeBySector;

    // Lap in progress: two sectors crossed, the third not yet.
    CHECK(cumulativeBySector({ 30000, 31000, 0 }, 3).size() == 2);
    // A missing split mid-history truncates too. Continuing past it would sum a lap's
    // worth of time that never happened and show the rider gaining time — worse than
    // simply having a shorter line.
    const auto truncated = cumulativeBySector({ 30000, 31000, 29000, 30000, -1, 30000 }, 3);
    REQUIRE(truncated.size() == 4);
    CHECK(truncated.back() == 120000);
    // Degenerate stride is rejected rather than dividing by zero.
    CHECK(cumulativeBySector({ 30000 }, 0).empty());
    CHECK(cumulativeBySector({}, 3).empty());
}

TEST_CASE("charts: sector indices land on the right fraction of a lap") {
    using SessionChartsMath::lapsAtSectorIndex;
    // 3 sectors: the third sector of lap 1 IS one completed lap.
    CHECK(lapsAtSectorIndex(0, 3) == doctest::Approx(1.0f / 3.0f));
    CHECK(lapsAtSectorIndex(2, 3) == doctest::Approx(1.0f));
    CHECK(lapsAtSectorIndex(5, 3) == doctest::Approx(2.0f));
    // 4-sector games (GP Bikes) divide the same way.
    CHECK(lapsAtSectorIndex(3, 4) == doctest::Approx(1.0f));
    CHECK(lapsAtSectorIndex(7, 4) == doctest::Approx(2.0f));
    CHECK(lapsAtSectorIndex(0, 0) == doctest::Approx(0.0f));   // guarded stride
}

TEST_CASE("charts: the X mapping is unchanged for per-lap points") {
    using SessionChartsMath::xFracForLaps;
    using SessionChartsMath::lapsAtSectorIndex;

    // Per-lap: firstLaps = 1 and laps = lapIndex0 + 1 must reproduce the historical
    // lapIndex0 / (maxLap - 1). This equality is the whole reason the shared mapping is
    // safe to adopt — a chart with sector points OFF must render exactly as before.
    const int maxLap = 5;
    for (int lapIdx = 0; lapIdx < maxLap; ++lapIdx) {
        const float expected = static_cast<float>(lapIdx) / static_cast<float>(maxLap - 1);
        CHECK(xFracForLaps(static_cast<float>(lapIdx + 1), 1.0f, maxLap) == doctest::Approx(expected));
    }

    // Sector resolution over the same race spans the identical [0,1] range, and each lap
    // boundary lands strictly right of the previous one.
    const float firstLaps = lapsAtSectorIndex(0, 3);
    CHECK(xFracForLaps(lapsAtSectorIndex(0, 3), firstLaps, maxLap) == doctest::Approx(0.0f));
    CHECK(xFracForLaps(lapsAtSectorIndex(3 * maxLap - 1, 3), firstLaps, maxLap) == doctest::Approx(1.0f));
    float prev = -1.0f;
    for (int k = 0; k < 3 * maxLap; ++k) {
        const float x = xFracForLaps(lapsAtSectorIndex(k, 3), firstLaps, maxLap);
        CHECK(x > prev);
        prev = x;
    }

    // A single-sample series is centred rather than dividing by a zero span.
    CHECK(xFracForLaps(1.0f, 1.0f, 1) == doctest::Approx(0.5f));
    // Out-of-range input clamps instead of drawing outside the plot rectangle.
    CHECK(xFracForLaps(99.0f, 1.0f, 5) == doctest::Approx(1.0f));
    CHECK(xFracForLaps(0.0f, 1.0f, 5) == doctest::Approx(0.0f));
}

TEST_CASE("charts: ranking math works unchanged at sector resolution") {
    using SessionChartsMath::cumulativeBySector;

    // Two riders, two 3-sector laps. #7 builds a 4s lead at 29s/sector, then loses 5s in
    // sector 2 of lap 2 (35s) and is passed — an overtake that happens mid-lap, which a
    // per-lap chart can only ever show at the following lap boundary.
    const std::vector<int> a = { 30000, 30000, 30000, 30000, 30000, 30000 };  // #4, even
    const std::vector<int> b = { 29000, 29000, 29000, 29000, 35000, 30000 };  // #7, ahead then loses
    std::vector<std::vector<long long>> cums = { cumulativeBySector(a, 3), cumulativeBySector(b, 3) };
    const std::vector<int> raceNums = { 4, 7 };

    const auto pos = SessionChartsMath::positionsPerLap(cums, raceNums);
    REQUIRE(pos.size() == 2);
    REQUIRE(pos[0].size() == 6);
    // #7 leads through lap 1 and into lap 2...
    CHECK(pos[1][2] == 1);
    CHECK(pos[0][2] == 2);
    // ...and the lead changes IN sector 2 of lap 2 (index 4), not at the lap boundary.
    CHECK(pos[0][4] == 1);
    CHECK(pos[1][4] == 2);

    const auto gaps = SessionChartsMath::gapToLeaderPerLap(cums);
    CHECK(gaps[1][2] == 0);        // leader pinned to zero at the end of lap 1
    CHECK(gaps[0][2] == 3000);     // #4 three seconds back
    CHECK(gaps[0][4] == 0);        // #4 now leads
    CHECK(gaps[1][4] == 1000);     // #7 a second back after the bad sector
}

TEST_CASE("charts: the lap-chart axis labels read the LATEST sample, not the latest lap") {
    using SessionChartsMath::latestPositionExtent;

    // Two riders over two 3-sector laps (6 samples). They swap in the final sector, so a
    // search bounded by the LAP count (index 1, a third of the way in) reports the old
    // order while the drawn lines already show the new one — the bug this pins.
    const std::vector<std::vector<int>> positions = {
        { 1, 1, 1, 1, 1, 2 },   // leader until the last sector
        { 2, 2, 2, 2, 2, 1 },
    };
    auto e = latestPositionExtent(positions);
    CHECK(e.top == 1);
    CHECK(e.bottom == 2);

    // The extent is the min/max at that sample, so a sparse field labels its real spread.
    const std::vector<std::vector<int>> spread = { { 3, 3 }, { 8, 9 }, { 5, 5 } };
    auto s = latestPositionExtent(spread);
    CHECK(s.top == 3);
    CHECK(s.bottom == 9);

    // Riders of differing length: the latest sample is the longest row's end, and riders
    // absent there are simply skipped rather than dragging the extent to 0.
    const std::vector<std::vector<int>> ragged = { { 1, 1, 1 }, { 2 } };
    auto r = latestPositionExtent(ragged);
    CHECK(r.top == 1);
    CHECK(r.bottom == 1);

    // Nothing to label.
    CHECK(latestPositionExtent({}).top == 0);
    CHECK(latestPositionExtent({ { 0, 0 } }).top == 0);
}
