// ============================================================================
// core/running_order.h
// The running order every reader shows: the Standings rows, the Position
// widget, the Dash's Standings page and the decks. The official
// classification, or with Live updates on Both (General) in a race in progress
// the live one (hud/standings_live_order.h, which holds the pair logic; who may
// move is decided here, from PluginData). Owned by PluginData, so it is the
// same answer whether or not the Standings HUD is shown.
//
// Re-read at most every POLL_MS, and whenever the official order moves; the
// first reader in a frame pays, the rest get the cached answer. When no reader
// has asked for IDLE_MS the live order starts over, as a pass held across a gap
// nobody watched proves nothing.
//
// Also the gap inputs those readers plan with (hud/standings_gap_plan.h), read
// from PluginData in one place.
//
// NOT PerRider (AGENTS.md's per-rider rule): the tracker's passes are keyed by
// race number, but they are a derived cache rebuilt from the official order on
// every read, dropped whenever the order is not live and after IDLE_MS unread,
// so a reused number cannot carry a pass past one of those resets.
// ============================================================================
#pragma once

#include <chrono>
#include <cstdint>
#include <vector>

#include "../hud/standings_gap_plan.h"
#include "../hud/standings_live_order.h"

class RunningOrder {
public:
    const std::vector<int>& get();
    // Whether get() can differ from the official order: Live updates on Both,
    // or the live order not yet handed back.
    bool watched() const;
    // The live order is what get() answers right now
    bool isLive() const { return m_active; }
    // Advances whenever get() answers differently, polling it first only while
    // watched: else two member reads, cheap enough for a reader to compare
    // every frame. Compare it rather than watched(), as the first reader to
    // poll after Both is left hands the official order back.
    uint32_t revision() {
        if (watched()) get();
        return m_revision;
    }

    // A rider's place in what get() answers, 1-based; -1 when not in it. The
    // one answer to "where is this rider": not watched, the official order's
    // position cache (PluginData); watched, a scan of get().
    int positionOf(int raceNum);

    // The gap table for `order` (the official classification) measured from
    // `displayRaceNum`, with the given reference and scope. playerRowIndex is
    // left for the caller: it is a row index in what that caller shows.
    static StandingsGap::Table gapTable(const std::vector<int>& order, int displayRaceNum,
                                        StandingsGap::Reference reference, StandingsGap::Scope scope);
    // A rider's row for planGap, the one way every reader fills it: `index` is
    // the row as the caller's table counts it (the running order, or the
    // Standings HUD's shown rows), `isLeader` the rider in first place.
    static StandingsGap::Row gapRow(int raceNum, int index, bool isPlayer, bool isLeader);

private:
    static constexpr int POLL_MS = 100;
    static constexpr int IDLE_MS = 1000;
    StandingsLiveOrder::Tracker m_tracker;
    std::vector<StandingsLiveOrder::Rider> m_riders;
    std::vector<int> m_rows;    // the live answer
    std::vector<int> m_next;    // scratch, swapped with m_rows
    std::vector<int> m_input;   // the official order it was read from
    bool m_active = false;      // get() answers m_rows
    uint32_t m_revision = 0;
    std::chrono::steady_clock::time_point m_time{};
};
