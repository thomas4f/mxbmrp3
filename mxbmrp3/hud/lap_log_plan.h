// ============================================================================
// hud/lap_log_plan.h
// Which rows the Lap Log draws, and in what order — the slot arithmetic, with
// no PluginData and no rendering.
//
// WHAT THIS IS. The Lap Log shows a fixed number of rows and has four things
// competing for them: recent laps, an optional live "current lap" row, the best
// lap when it has scrolled out of the recent window, and placeholders padding
// the rest. Deciding the row list is pure integer arithmetic over the history
// size, the best lap's number and four settings — but it was inline in
// rebuildRenderData(), so the only way to exercise it was to build the DLL and
// drive real callbacks under Wine.
//
// THE PART THAT IS EASY TO GET WRONG, and the reason this is worth a unit test:
// the two reserved slots are taken from the recent-lap budget in a specific
// ORDER, and the second reservation depends on a scan that used the budget the
// first one already reduced. The best lap counts as "in the recent window" only
// if it falls inside the budget left AFTER the current-lap row was reserved —
// so enabling live timing can push the best lap out of the window and force a
// separate row for it, which then costs a third slot. Reordering those steps
// looks harmless and quietly changes how many laps are visible.
//
// A DELIBERATELY PRESERVED EDGE. With maxDisplayLaps == 1, a live current-lap
// row and a separately-shown best lap, both reserved rows are emitted and the
// plan returns 2 rows — one MORE than the configured maximum. That is the
// behaviour this code has always had (the budget goes negative and the recent-lap
// loops simply don't run); it is pinned as-is by the unit test rather than
// silently "fixed", because the alternative — dropping one of the two — is a
// product decision, not a refactor. See the maxDisplayLaps==1 cases in
// tests/unit/test_lap_log_plan.cpp.
//
// Pinned by tests/unit/test_lap_log_plan.cpp (~1s, no game).
// ============================================================================
#pragma once

#include <vector>

namespace LapLogPlan {

// A row is either an index into the lap history (0 = most recent lap, counting
// backwards) or one of these synthetic rows. The values are the ones this HUD
// has always used; they are named here so the render loop stops comparing
// against bare negative literals.
constexpr int kPlaceholder = -2;  // empty padding row
constexpr int kBestLap     = -3;  // the best lap, shown out of sequence
constexpr int kCurrentLap  = -4;  // live in-progress lap
constexpr int kGap         = -5;  // gap-to-best row, pinned to an outer edge

enum class Order {
    OLDEST_FIRST,  // best lap on top, then padding, oldest->newest, live, gap
    NEWEST_FIRST,  // gap, live, newest->oldest, then padding, best lap last
};

struct Input {
    // Number of completed laps in the history.
    int lapLogSize = 0;
    // Rows the HUD is configured to show (excludes the gap row, which is extra).
    int maxDisplayLaps = 5;
    // Lap numbers of the history entries, index 0 = most recent. May be null,
    // in which case no lap can match the best lap and it is always shown
    // separately (when there is one). Only the first maxDisplayLaps entries are
    // ever read.
    const int* lapNums = nullptr;
    int lapNumCount = 0;
    // The best lap, when one exists. bestLapNum is its lap number, matched
    // against lapNums to decide whether it is already on screen.
    bool hasBestLap = false;
    int bestLapNum = -1;
    // Caller-resolved: live timing on AND the timer valid AND the rider has not
    // taken the flag. (Kept as a resolved bool so this header needs no session
    // state.)
    bool showCurrentLap = false;
    // Caller-resolved: the gap row is enabled AND live timing is on.
    bool showGapRow = false;
    Order order = Order::NEWEST_FIRST;
};

struct Plan {
    // Row descriptors in draw order (top to bottom).
    std::vector<int> rows;
    // How many data rows the background must be tall enough for. This is the
    // CONFIGURED height, not rows.size(): placeholders keep the box a constant
    // size as laps come in, and the gap row is extra on top of the configured
    // count.
    int dataRowCount = 0;
};

// Fills a CALLER-OWNED plan rather than returning one: the Lap Log rebuilds on a
// ticking timer, and a returned Plan would heap-allocate `rows` every rebuild.
// The caller keeps the Plan as a member, so in steady state this reuses its
// capacity and allocates nothing (480fps budget).
inline void compute(const Input& in, Plan& plan) {
    plan.rows.clear();

    // The recent-lap budget, reduced by each row we have to reserve. Order
    // matters here — see the header comment.
    int recentBudget = in.maxDisplayLaps;
    if (in.showCurrentLap) recentBudget--;

    // Is the best lap already inside the window of recent laps we are about to
    // draw? Scanned against the budget as it stands NOW (after the current-lap
    // reservation), because that is the window the rider will actually see.
    bool bestLapInRecent = false;
    if (in.hasBestLap && in.lapNums != nullptr) {
        for (int i = 0; i < recentBudget && i < in.lapNumCount; ++i) {
            if (in.lapNums[i] == in.bestLapNum) {
                bestLapInRecent = true;
                break;
            }
        }
    }

    const bool showBestSeparately = in.hasBestLap && !bestLapInRecent;
    if (showBestSeparately) recentBudget--;

    // recentBudget can be negative here (maxDisplayLaps 1 with both reservations);
    // the loops below then simply emit no lap rows. Preserved deliberately.
    const int numRecent = (recentBudget < in.lapLogSize) ? recentBudget : in.lapLogSize;

    const int filled = numRecent + (in.showCurrentLap ? 1 : 0) + (showBestSeparately ? 1 : 0);
    int placeholders = in.maxDisplayLaps - filled;
    if (placeholders < 0) placeholders = 0;

    if (in.order == Order::OLDEST_FIRST) {
        if (showBestSeparately) plan.rows.push_back(kBestLap);
        for (int i = 0; i < placeholders; ++i) plan.rows.push_back(kPlaceholder);
        for (int i = numRecent - 1; i >= 0; --i) plan.rows.push_back(i);
        if (in.showCurrentLap) plan.rows.push_back(kCurrentLap);
        if (in.showGapRow) plan.rows.push_back(kGap);
    } else {
        if (in.showGapRow) plan.rows.push_back(kGap);
        if (in.showCurrentLap) plan.rows.push_back(kCurrentLap);
        for (int i = 0; i < numRecent; ++i) plan.rows.push_back(i);
        for (int i = 0; i < placeholders; ++i) plan.rows.push_back(kPlaceholder);
        if (showBestSeparately) plan.rows.push_back(kBestLap);
    }

    plan.dataRowCount = in.maxDisplayLaps + (in.showGapRow ? 1 : 0);
}

// Convenience overload for tests and any caller not on the hot path.
inline Plan compute(const Input& in) {
    Plan plan;
    compute(in, plan);
    return plan;
}

}  // namespace LapLogPlan
