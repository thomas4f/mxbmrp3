// ============================================================================
// hud/records_window.h
// Which slice of the records table is shown around the player's own PB — the
// context-window arithmetic, with no PluginData and no rendering.
//
// WHAT THIS IS. The records table pins the top N positions and then shows a
// window centred on where the player's personal best would slot in. Producing
// that window is pure integer arithmetic over four numbers, but it was inline in
// rebuildRenderData(), so the only way to exercise it was a DLL under Wine —
// with a live records fetch, which makes the interesting cases (player last,
// player just below the pinned block, fewer records than rows) awkward to stage.
//
// This is NOT the same window as hud/standings_context_window.h, and the two must
// not be merged. The standings window centres on a rider who is IN the field; here
// the player's PB is inserted BETWEEN fetched records and may sit past the end of
// them entirely (slower than every record fetched), which is its own branch. The
// row budget also differs: one row is always reserved for the player's own line.
//
// THE PART THAT IS EASY TO GET WRONG, and the reason this is worth a unit test:
// the window is clamped at both ends, and each clamp has to give its rows back to
// the other side or the table renders short. A player just below the pinned block
// cannot use the rows "above" them that the block already occupies, so those move
// down; a player near the last fetched record cannot use the rows "below", so
// those move up. Implementing only one of the two compensations looks correct for
// a mid-table player — which is where a developer's own PB usually sits.
//
// Pinned by tests/unit/test_records_window.cpp (~1s, no game, no network).
// ============================================================================
#pragma once

#include <algorithm>

namespace RecordsWindow {

// A contiguous slice of the fetched records, inclusive at both ends.
// end < start means "no rows".
struct Range {
    int start = 0;
    int end = -1;

    bool empty() const { return end < start; }
    int count() const { return empty() ? 0 : end - start + 1; }
};

// totalRecords   - how many records were fetched
// playerPosition - the 0-based index the player's PB would occupy among them.
//                  May equal totalRecords (slower than every record fetched).
// rowCount       - total data rows the table can draw
// topCount       - how many leading positions stay pinned above this window
//
// Precondition (the only case the caller reaches this on): playerPosition >=
// topCount. A player inside the pinned block needs no context window at all.
//
// One row of the budget is always reserved for the player's own line, so the
// window itself gets rowCount - topCount - 1 rows.
inline Range computeContext(int totalRecords, int playerPosition,
                            int rowCount, int topCount) {
    const int availableRows = rowCount - topCount - 1;

    Range r;
    if (playerPosition >= totalRecords) {
        // Player is slower than everything fetched: their row goes last, so show
        // the tail of the records immediately above it.
        r.end = totalRecords - 1;
        r.start = std::max(topCount, totalRecords - availableRows);
        return r;
    }

    // Player slots in among the records: centre the window on them, biased so the
    // player's own row sits just past the middle (contextAfter loses the row the
    // player line occupies).
    const int contextBefore = availableRows / 2;
    const int contextAfter = availableRows - contextBefore - 1;

    r.start = std::max(topCount, playerPosition - contextBefore);
    r.end = std::min(totalRecords - 1, playerPosition + contextAfter);

    // Both clamps hand their unusable rows to the opposite side. Exactly one can
    // apply: if the window is against both ends at once there are no rows to move.
    if (r.end == totalRecords - 1 && r.start > topCount) {
        r.start = std::max(topCount, r.end - availableRows + 1);
    } else if (r.start == topCount && r.end < totalRecords - 1) {
        r.end = std::min(totalRecords - 1, r.start + availableRows - 1);
    }
    return r;
}

// ---------------------------------------------------------------------------
// PANEL WIDTH, in characters. Here rather than inline in rebuildRenderData for the
// same reason the window above is: it is integer arithmetic over the column set, and
// the only bug it has had cannot be seen in the numbers -- only in a themed render.
//
// EVERY ENABLED COLUMN CONTRIBUTES ITS FULL WIDTH, gap included, the last one too.
// That trailing gap is the last column's right CLEARANCE inside the panel, not waste:
// without it the panel is exactly as wide as its content, so the lap time's final
// digit lands on the same pixel as the player row's highlight band. Measured at 1px
// of clearance before, 10 after. It only showed with a THEME installed back when the
// highlight spanned the card's interior (it spans the CONTENT COLUMN now, so the
// clearance is the column's either way) -- which is why it survived until a user
// reported it, and why this is worth a test rather than a comment. StandingsHud has
// always summed its widths this way.
//
// Pinned by tests/unit/test_records_window.cpp.
struct ColumnWidths {
    int pos = 0, rider = 0, bike = 0, sector = 0, laptime = 0, date = 0;
};
struct ColumnFlags {
    bool pos = false, rider = false, bike = false, sectors = false;
    bool laptime = false, date = false;
};

// minChars: the controls row (provider / category / Compare) sets a floor the columns
// may be narrower than.
inline int backgroundWidthChars(const ColumnWidths& w, const ColumnFlags& on,
                                int sectorCount, int minChars) {
    int chars = 0;
    if (on.pos)     chars += w.pos;
    if (on.rider)   chars += w.rider;
    if (on.bike)    chars += w.bike;
    if (on.sectors) chars += w.sector * sectorCount;
    if (on.laptime) chars += w.laptime;
    if (on.date)    chars += w.date;
    return std::max(chars, minChars);
}

}  // namespace RecordsWindow
