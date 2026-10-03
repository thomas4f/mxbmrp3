// ============================================================================
// hud/split_crossing.h
// "The display rider just crossed a split or the line", detected once for
// every HUD that reacts to it: OfficialGapFreeze (Gap Bar, Lap Log gap row),
// the Timing panel and the Pitboard. Each used to keep its own split caches,
// and they drifted: the Pitboard never saw GP Bikes' third split, and keyed
// the line on the lap TIME changing, so two identical lap times in a row
// never showed the board.
//
// THE RULES, one place:
//   - A new session or display rider ADOPTS what is already there and reports
//     nothing: a split crossed before the switch is not news.
//   - The line is a new lastCompletedLapNum (lap identity, never the time),
//     reported with the lap log's verdict on the lap (valid, via the pits).
//   - The line is taken first and clears the split caches: the splits read
//     after it belong to the new lap (PluginData clears them at the line).
//   - At most one split per poll, the earliest new one.
// The caller decides what a crossing means (hold a gap, show the board) and
// owns its own hold (hold_timer.h).
//
// Pinned by pb_gap_test.cpp (the freeze cases), timing_reference_test.cpp and
// pitboard_splits_test.cpp; the pure helper by tests/unit/test_split_crossing.cpp.
// ============================================================================
#pragma once

#include "../game/game_config.h"

// A lap's time to a crossing: its accumulated sectors to split `splitIndex`
// (0-based), or its lap time for the line (-1). -1 when any part is missing.
inline int timeToCrossing(int lapTime, int sector1, int sector2, int sector3, int splitIndex) {
    if (splitIndex < 0) return lapTime > 0 ? lapTime : -1;
    const int sectors[3] = { sector1, sector2, sector3 };
    int sum = 0;
    for (int i = 0; i <= splitIndex && i < 3; ++i) {
        if (sectors[i] <= 0) return -1;
        sum += sectors[i];
    }
    return sum;
}

class SplitCrossingDetector {
public:
    static constexpr int NUM_SPLITS = GAME_SECTOR_COUNT - 1;

    struct Result {
        bool adopted = false;     // new session or display rider: caches re-read, nothing reported
        // The line, with the lap it completed
        bool line = false;
        int lapNum = -1;          // 0-based, the lap log's numbering
        int lapTime = -1;
        bool lapValid = true;     // the lap log's verdict (true when it has none)
        bool lapViaPits = false;  // an invalid lap that went through the pits
        // A split (0-based index), -1 = none
        int splitIndex = -1;
        int splitTime = -1;       // accumulated time to it
    };

    // Once per HUD update.
    Result poll();

    // Adopt on the next poll.
    void reset() { m_adopted = false; }

private:
    bool m_adopted = false;
    int m_sessionGeneration = 0;
    int m_displayRaceNum = 0;
    int m_cachedSplits[NUM_SPLITS] = {};
    int m_cachedLastCompletedLapNum = -1;
};
