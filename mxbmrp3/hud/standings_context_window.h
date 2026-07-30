// ============================================================================
// hud/standings_context_window.h
// Which slice(s) of the classification the standings table shows — the
// pagination arithmetic, with no PluginData and no rendering.
//
// WHAT THIS IS. The table has fewer rows than the field has riders, so it shows
// the top N plus a window centred on the display rider. Producing that window is
// pure integer arithmetic over four numbers (field size, the rider's index, the
// top-N count, the row count), but it was inline in rebuildRenderData() and so
// only reachable by building the DLL and driving real callbacks under Wine.
//
// THE PART THAT IS EASY TO GET WRONG, and the reason this is worth a unit test:
// the window is clamped at BOTH ends and each clamp has to give its rows back to
// the other side, or the table silently renders short. A rider in P4 with the
// top 3 pinned cannot use the two rows "above" them that the top-N block already
// occupies, so those rows move below; a rider in last place cannot use the rows
// "below" them, so those move above. Getting only one of the two compensations
// right looks correct in mid-pack testing and drops rows at the edges of the
// field — which is exactly where the interesting racing is.
//
// Pinned by tests/unit/test_standings_context_window.cpp (~1s, no game).
// ============================================================================
#pragma once

namespace StandingsWindow {

// A contiguous slice of the classification order, inclusive at both ends.
// endIndex < startIndex means "no rows".
//
// Neither slice can run past the field: the leaders-only path already takes
// min(fieldSize, rowCount), and on the context path centreOnRider implies
// topCount <= riderIndex <= fieldSize-1, which bounds both ends. So a caller
// needs no clamp of its own — noted because the earlier wording asked for one
// and invited a worry the arithmetic has already ruled out.
struct Range {
    int startIndex = 0;
    int endIndex = -1;

    bool empty() const { return endIndex < startIndex; }
    int count() const { return empty() ? 0 : endIndex - startIndex + 1; }
};

// Up to two slices: the pinned top-N block, then the window around the display
// rider. When the rider is already inside the top-N block (or there is no rider
// to centre on) a single slice covers everything and `context` is empty.
struct Window {
    Range top;
    Range context;

    int totalRows() const { return top.count() + context.count(); }
};

// fieldSize    - number of riders in the classification order
// riderIndex   - the display rider's 0-based index in it, or <0 when absent
// topCount     - how many leading positions stay pinned
// rowCount     - how many rows the table can draw
// showContext  - false (rider not on track and not spectating) falls back to
//                "just show the leaders", which is also the riderIndex<0 path
inline Window computeWindow(int fieldSize, int riderIndex, int topCount,
                            int rowCount, bool showContext) {
    Window w;

    const bool centreOnRider = showContext && riderIndex >= topCount;

    if (!centreOnRider) {
        // Leaders-only: the rider is inside the pinned block, is absent, or
        // context is switched off. One slice from the front.
        const int rows = fieldSize < rowCount ? fieldSize : rowCount;
        w.top.startIndex = 0;
        w.top.endIndex = rows - 1;
        return w;
    }

    // Pinned leaders.
    w.top.startIndex = 0;
    w.top.endIndex = topCount - 1;

    // Rows left for the rider's window, split around the rider's own row.
    const int available = rowCount - topCount;
    const int before = available / 2;
    const int after = available - before - 1;  // -1 for the rider's own row

    // Clamp the top of the window against the pinned block, and hand whatever
    // it cost back to the bottom.
    int startIndex = riderIndex - before;
    if (startIndex < topCount) startIndex = topCount;
    const int lostAbove = startIndex - (riderIndex - before);

    // Clamp the bottom against the end of the field, and hand what THAT cost
    // back to the top (never past the pinned block).
    const int desiredEnd = riderIndex + after + lostAbove;
    int endIndex = desiredEnd;
    if (endIndex > fieldSize - 1) endIndex = fieldSize - 1;
    const int lostBelow = desiredEnd - endIndex;
    if (lostBelow > 0) {
        startIndex -= lostBelow;
        if (startIndex < topCount) startIndex = topCount;
    }

    w.context.startIndex = startIndex;
    w.context.endIndex = endIndex;
    return w;
}

}  // namespace StandingsWindow
