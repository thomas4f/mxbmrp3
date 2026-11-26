// ============================================================================
// tests/unit/test_lap_log_plan.cpp
// Unit tests for hud/lap_log_plan.h — the Lap Log's row planning.
//
// This arithmetic used to live inline in LapLogHud::rebuildRenderData(), so the
// only way to exercise it was the DLL under Wine. The cases that matter are the
// two reserved rows and their ORDER (see the header): the best-lap-in-window
// scan runs against the budget the current-lap row has already reduced, so
// toggling live timing can push the best lap out of the window and cost a third
// slot. Reordering those two steps still passes a naive "does it show 5 rows"
// check.
//
// THE STANDING CONTRACT the HUD depends on: with maxDisplayLaps rows configured
// and no gap row, the plan emits exactly maxDisplayLaps rows — placeholders make
// up the difference so the box does not change size as laps come in. `constantHeight`
// sweeps that so a future edit cannot satisfy the hand-written cases and still
// render short (or tall) somewhere in between. The one documented exception is
// pinned explicitly below.
// ============================================================================
#include "doctest.h"

#include "hud/lap_log_plan.h"
#include "core/plugin_constants.h"
#include <algorithm>
#include <string>
#include <vector>

using namespace LapLogPlan;

namespace {

// Lap numbers for a history of n completed laps, index 0 = most recent.
// Lap 1 is the oldest, so a history of 5 reads {5,4,3,2,1}.
std::vector<int> history(int n) {
    std::vector<int> v;
    for (int i = 0; i < n; ++i) v.push_back(n - i);
    return v;
}

int countOf(const std::vector<int>& rows, int kind) {
    return static_cast<int>(std::count(rows.begin(), rows.end(), kind));
}

// Indices of real lap rows, in draw order.
std::vector<int> lapRows(const std::vector<int>& rows) {
    std::vector<int> out;
    for (int r : rows) if (r >= 0) out.push_back(r);
    return out;
}

Input makeInput(const std::vector<int>& lapNums, int maxDisplay) {
    Input in;
    in.lapLogSize = static_cast<int>(lapNums.size());
    in.maxDisplayLaps = maxDisplay;
    in.lapNums = lapNums.empty() ? nullptr : lapNums.data();
    in.lapNumCount = static_cast<int>(lapNums.size());
    return in;
}

}  // namespace

TEST_CASE("lap log plan: empty history is all placeholders") {
    std::vector<int> h;
    Input in = makeInput(h, 5);
    Plan p = compute(in);

    CHECK(p.rows.size() == 5);
    CHECK(countOf(p.rows, kPlaceholder) == 5);
    CHECK(p.dataRowCount == 5);
}

TEST_CASE("lap log plan: a full history fills every row with laps") {
    std::vector<int> h = history(5);
    Input in = makeInput(h, 5);
    in.hasBestLap = true;
    in.bestLapNum = 3;  // inside the window -> no separate row

    Plan p = compute(in);
    CHECK(p.rows.size() == 5);
    CHECK(countOf(p.rows, kPlaceholder) == 0);
    CHECK(countOf(p.rows, kBestLap) == 0);
    CHECK(lapRows(p.rows) == std::vector<int>{0, 1, 2, 3, 4});  // NEWEST_FIRST
}

TEST_CASE("lap log plan: NEWEST_FIRST vs OLDEST_FIRST reverse the lap rows") {
    std::vector<int> h = history(3);
    Input in = makeInput(h, 3);

    in.order = Order::NEWEST_FIRST;
    CHECK(lapRows(compute(in).rows) == std::vector<int>{0, 1, 2});

    in.order = Order::OLDEST_FIRST;
    CHECK(lapRows(compute(in).rows) == std::vector<int>{2, 1, 0});
}

TEST_CASE("lap log plan: the best lap gets its own row only once it scrolls out") {
    std::vector<int> h = history(10);  // laps 10..1, index 0 = lap 10
    Input in = makeInput(h, 5);
    in.hasBestLap = true;

    SUBCASE("best lap inside the visible window -> no separate row") {
        in.bestLapNum = 8;  // index 2, within the 5 shown
        Plan p = compute(in);
        CHECK(countOf(p.rows, kBestLap) == 0);
        CHECK(lapRows(p.rows).size() == 5);
    }

    SUBCASE("best lap scrolled out -> a separate row, costing one lap slot") {
        in.bestLapNum = 2;  // index 8, far outside
        Plan p = compute(in);
        CHECK(countOf(p.rows, kBestLap) == 1);
        CHECK(lapRows(p.rows).size() == 4);  // one slot went to the best lap
        CHECK(p.rows.size() == 5);           // total height unchanged
    }

    SUBCASE("best lap exactly on the window edge counts as inside") {
        in.bestLapNum = 6;  // index 4 == last visible row
        CHECK(countOf(compute(in).rows, kBestLap) == 0);
    }

    SUBCASE("best lap one past the edge falls out") {
        in.bestLapNum = 5;  // index 5, first row past the window
        CHECK(countOf(compute(in).rows, kBestLap) == 1);
    }
}

// The ordering bug this extraction exists to pin. The best-lap scan must run
// against the budget AFTER the current-lap reservation, so a best lap sitting on
// the last visible row falls out of the window the moment live timing is on.
TEST_CASE("lap log plan: the current-lap row shrinks the window the best lap is judged against") {
    std::vector<int> h = history(10);
    Input in = makeInput(h, 5);
    in.hasBestLap = true;
    in.bestLapNum = 6;  // index 4: the last row visible WITHOUT a live row

    SUBCASE("no live row -> best lap is inside") {
        in.showCurrentLap = false;
        Plan p = compute(in);
        CHECK(countOf(p.rows, kBestLap) == 0);
        CHECK(lapRows(p.rows).size() == 5);
    }

    SUBCASE("live row on -> window is 4, best lap falls out and takes a third slot") {
        in.showCurrentLap = true;
        Plan p = compute(in);
        CHECK(countOf(p.rows, kCurrentLap) == 1);
        CHECK(countOf(p.rows, kBestLap) == 1);
        CHECK(lapRows(p.rows).size() == 3);  // 5 - live - best
        CHECK(p.rows.size() == 5);
    }
}

TEST_CASE("lap log plan: no lap numbers means the best lap can never match") {
    Input in;
    in.lapLogSize = 4;
    in.maxDisplayLaps = 5;
    in.lapNums = nullptr;  // caller had no history pointer
    in.lapNumCount = 0;
    in.hasBestLap = true;
    in.bestLapNum = 2;

    Plan p = compute(in);
    CHECK(countOf(p.rows, kBestLap) == 1);  // shown separately, not assumed present
}

TEST_CASE("lap log plan: gap row is extra height and pinned to the outer edge") {
    std::vector<int> h = history(5);
    Input in = makeInput(h, 5);
    in.showGapRow = true;

    SUBCASE("NEWEST_FIRST puts it first") {
        in.order = Order::NEWEST_FIRST;
        Plan p = compute(in);
        CHECK(p.rows.front() == kGap);
        CHECK(p.rows.size() == 6);
        CHECK(p.dataRowCount == 6);  // configured 5 + the extra gap row
    }

    SUBCASE("OLDEST_FIRST puts it last") {
        in.order = Order::OLDEST_FIRST;
        Plan p = compute(in);
        CHECK(p.rows.back() == kGap);
        CHECK(p.rows.size() == 6);
        CHECK(p.dataRowCount == 6);
    }
}

TEST_CASE("lap log plan: synthetic rows sit on the expected edges") {
    std::vector<int> h = history(10);
    Input in = makeInput(h, 5);
    in.hasBestLap = true;
    in.bestLapNum = 1;  // way out of the window
    in.showCurrentLap = true;
    in.showGapRow = true;

    SUBCASE("NEWEST_FIRST: gap, live, laps, best last") {
        in.order = Order::NEWEST_FIRST;
        Plan p = compute(in);
        CHECK(p.rows.front() == kGap);
        CHECK(p.rows[1] == kCurrentLap);
        CHECK(p.rows.back() == kBestLap);
    }

    SUBCASE("OLDEST_FIRST: best first, laps, live, gap last") {
        in.order = Order::OLDEST_FIRST;
        Plan p = compute(in);
        CHECK(p.rows.front() == kBestLap);
        CHECK(p.rows.back() == kGap);
        CHECK(p.rows[p.rows.size() - 2] == kCurrentLap);
    }
}

TEST_CASE("lap log plan: a partial history pads to the configured height") {
    std::vector<int> h = history(2);
    Input in = makeInput(h, 5);
    in.hasBestLap = true;
    in.bestLapNum = 2;  // index 0, inside

    Plan p = compute(in);
    CHECK(p.rows.size() == 5);
    CHECK(lapRows(p.rows).size() == 2);
    CHECK(countOf(p.rows, kPlaceholder) == 3);
}

// Documented edge, preserved from the original inline code rather than "fixed":
// at maxDisplayLaps==1 the two reserved rows both get emitted, so the plan is one
// row TALLER than configured. See the header. Changing this is a product decision.
TEST_CASE("lap log plan: maxDisplayLaps==1 with both reserved rows overflows by one") {
    std::vector<int> h = history(10);
    Input in = makeInput(h, 1);
    in.hasBestLap = true;
    in.bestLapNum = 1;  // outside the (zero-width) window
    in.showCurrentLap = true;

    Plan p = compute(in);
    CHECK(p.rows.size() == 2);  // one more than maxDisplayLaps
    CHECK(countOf(p.rows, kCurrentLap) == 1);
    CHECK(countOf(p.rows, kBestLap) == 1);
    CHECK(lapRows(p.rows).empty());
    CHECK(countOf(p.rows, kPlaceholder) == 0);  // budget went negative, no padding
    CHECK(p.dataRowCount == 1);
}

TEST_CASE("lap log plan: maxDisplayLaps==1 with only a live row stays at one") {
    std::vector<int> h = history(10);
    Input in = makeInput(h, 1);
    in.showCurrentLap = true;

    Plan p = compute(in);
    CHECK(p.rows.size() == 1);
    CHECK(p.rows[0] == kCurrentLap);
}

// The height contract, swept. Excludes only the documented maxDisplayLaps==1
// overflow above.
TEST_CASE("lap log plan: constantHeight — the plan always fills exactly the configured rows") {
    for (int maxDisplay = 2; maxDisplay <= 12; ++maxDisplay) {
        for (int histSize = 0; histSize <= 20; ++histSize) {
            std::vector<int> h = history(histSize);
            for (int liveBit = 0; liveBit < 2; ++liveBit) {
                for (int gapBit = 0; gapBit < 2; ++gapBit) {
                    for (int bestBit = 0; bestBit < 2; ++bestBit) {
                        for (int ord = 0; ord < 2; ++ord) {
                            Input in = makeInput(h, maxDisplay);
                            in.showCurrentLap = (liveBit != 0);
                            in.showGapRow = (gapBit != 0);
                            in.hasBestLap = (bestBit != 0) && histSize > 0;
                            in.bestLapNum = 1;  // oldest lap: out of window whenever it can be
                            in.order = (ord != 0) ? Order::OLDEST_FIRST : Order::NEWEST_FIRST;

                            Plan p = compute(in);
                            const int expected = maxDisplay + (in.showGapRow ? 1 : 0);
                            CAPTURE(maxDisplay);
                            CAPTURE(histSize);
                            CAPTURE(liveBit);
                            CAPTURE(gapBit);
                            CAPTURE(bestBit);
                            CHECK(static_cast<int>(p.rows.size()) == expected);
                            CHECK(p.dataRowCount == expected);

                            // No lap row may point past the history.
                            for (int r : lapRows(p.rows)) {
                                CHECK(r >= 0);
                                CHECK(r < histSize);
                            }
                        }
                    }
                }
            }
        }
    }
}

// The HUD calls the out-param overload with a member Plan so a rebuild reuses the
// row vector's capacity instead of allocating (480fps budget). The failure mode
// that buys: forgetting to clear, so rows ACCUMULATE across rebuilds and the
// table grows every frame. A returned-by-value Plan could never show this.
TEST_CASE("lap log plan: reusing a Plan replaces its rows rather than appending") {
    const std::vector<int> laps = history(5);
    Input in = makeInput(laps, 5);

    Plan reused;
    compute(in, reused);
    const size_t firstSize = reused.rows.size();
    CHECK(firstSize == 5);

    // Same input again: identical result, not a doubled one.
    compute(in, reused);
    CHECK(reused.rows.size() == firstSize);
    CHECK(reused.rows == compute(in).rows);

    // A plan that needs FEWER rows must shrink, and must not leave stale tail rows.
    Input smaller = makeInput(laps, 2);
    compute(smaller, reused);
    CHECK(reused.rows.size() == 2);
    CHECK(reused.rows == compute(smaller).rows);

    // Capacity survives the shrink — that is the whole point of the out-param.
    const size_t cap = reused.rows.capacity();
    compute(in, reused);
    CHECK(reused.rows.capacity() >= cap);
}

// THE SCROLLBACK (hud_scroll.h) moves only the recent window: `skip` laps from
// the newest end are passed over, the reserved rows stay where they are, and the
// best lap is "in the window" only when it is inside the window it moved to.
TEST_CASE("lap log plan: skip moves the recent window back, the reserved rows stay") {
    const std::vector<int> laps = history(12);   // {12, 11, ..., 1}
    Input in = makeInput(laps, 5);
    in.showCurrentLap = true;
    in.order = Order::NEWEST_FIRST;

    Plan at = compute(in);
    CHECK(at.recentRoom == 4);
    CHECK(lapRows(at.rows) == std::vector<int>{ 0, 1, 2, 3 });

    in.skip = 6;
    Plan back = compute(in);
    CHECK(back.rows.front() == kCurrentLap);
    CHECK(lapRows(back.rows) == std::vector<int>{ 6, 7, 8, 9 });
    CHECK(back.recentRoom == 4);

    // The best lap (lap 4 = index 8) inside the moved window: no row of its own.
    in.hasBestLap = true;
    in.bestLapNum = 4;
    CHECK(countOf(compute(in).rows, kBestLap) == 0);
    // ...outside it (back at the newest end): its own row, one lap less shown.
    in.skip = 0;
    Plan pinned = compute(in);
    CHECK(countOf(pinned.rows, kBestLap) == 1);
    CHECK(pinned.recentRoom == 3);

    // Near the oldest end the window shows what is left, padded.
    in.hasBestLap = false;
    in.skip = 10;
    Plan tail = compute(in);
    CHECK(lapRows(tail.rows) == std::vector<int>{ 10, 11 });
    CHECK(countOf(tail.rows, kPlaceholder) == 2);
}

// MID-GLIDE the Lap Log draws one lap past the window (HudScroll::drawRows),
// cut at the window's edge: it must take no slot, so the placeholders and the
// best lap's own row are what they are without it.
TEST_CASE("lap log plan: the glide's extra lap takes no slot") {
    const std::vector<int> laps = history(12);
    Input in = makeInput(laps, 5);
    in.order = Order::NEWEST_FIRST;
    in.skip = 3;
    Plan rest = compute(in);
    in.extra = 1;
    Plan glide = compute(in);
    CHECK(lapRows(glide.rows) == std::vector<int>{ 3, 4, 5, 6, 7, 8 });
    CHECK(glide.recentRoom == rest.recentRoom);
    CHECK(countOf(glide.rows, kPlaceholder) == 0);

    // Near the oldest end there is no lap to add, and the padding stays.
    in.skip = 9;
    in.extra = 0;
    const int pads = countOf(compute(in).rows, kPlaceholder);
    in.extra = 1;
    Plan tail = compute(in);
    CHECK(lapRows(tail.rows) == std::vector<int>{ 9, 10, 11 });
    CHECK(countOf(tail.rows, kPlaceholder) == pads);
}

// The two rules the Lap Log HUD and the Lap Log deck share, so neither keeps a
// copy that could drift: when the running lap gets its row, and what a lap
// with no time says in its time cell.
TEST_CASE("lap log plan: the running lap shows while timed or through the pits, never after the flag") {
    CHECK(LapLogPlan::runningLapShown(true, false, false));
    CHECK(LapLogPlan::runningLapShown(false, true, false));    // timer paused or dropped in the pits
    CHECK(LapLogPlan::runningLapShown(true, true, false));
    CHECK_FALSE(LapLogPlan::runningLapShown(false, false, false));
    CHECK_FALSE(LapLogPlan::runningLapShown(true, false, true));
    CHECK_FALSE(LapLogPlan::runningLapShown(false, true, true));
}

TEST_CASE("lap log plan: an untimed lap says why - through the pits before struck out") {
    using namespace PluginConstants::Placeholders;
    CHECK(std::string(untimedLap(true, true)) == PIT_LAP);
    CHECK(std::string(untimedLap(true, false)) == PIT_LAP);
    CHECK(std::string(untimedLap(false, false)) == INVALID_LAP);
    CHECK(std::string(untimedLap(false, true)) == GENERIC);
}
