// ============================================================================
// core/finish_margin.h
// The exact margin between the winner and the runner-up at the flag.
//
// WHY THIS DOES NOT READ StandingsData::gap. That field is the official gap
// from splits, and PluginData deliberately CACHES it: the API clears gaps to 0
// as the leader crosses the line, and batchUpdateStandings substitutes the last
// nonzero value to stop the standings flickering. Correct for a live gap
// column, useless as a finishing margin - the value it holds at the flag is the
// gap from before the flag, i.e. from before the runner-up closed it. Reading
// it made Photo Finish essentially unearnable (1 unlock in 1728 installs).
//
// It also could not tell a tenth from a lap and a tenth: gap is the sub-lap
// remainder, so a rider a full lap down with gap == 50 read as a photo finish.
// Requiring an equal, complete lap count closes that in the same step.
//
// The lap log is the exact source instead: race_lap_handler records every
// rider's lap times in milliseconds straight from the RaceLap callback, so the
// difference of two riders' totals is the margin to the millisecond - no
// dependence on what the classification reports once the race is over.
//
// Pinned by tests/unit/test_finish_margin.cpp.
// ============================================================================
#pragma once

#include "plugin_data_types.h"

#include <deque>

namespace FinishMargin {

// Total race time in ms: the sum of a rider's complete laps. Returns -1 unless
// the log describes EXACTLY expectedLaps complete laps with a positive time,
// which is the honest answer whenever the rider joined late, the log rolled
// over (MAX_LAP_LOG_STORAGE), or a lap is still open. Invalid laps count: a cut
// track costs a penalty, not a lap, and the rider still rode the time.
inline int totalRaceTimeMs(const std::deque<LapLogEntry>& lapLog, int expectedLaps) {
    if (expectedLaps <= 0) return -1;
    int total = 0;
    int complete = 0;
    for (const LapLogEntry& lap : lapLog) {
        if (!lap.isComplete) continue;
        if (lap.lapTime <= 0) return -1;
        total += lap.lapTime;
        if (++complete > expectedLaps) return -1;
    }
    return complete == expectedLaps ? total : -1;
}

// The runner-up's margin over the winner in ms, or -1 when it cannot be known
// exactly. Both riders must have completed the same number of laps - a
// runner-up on fewer laps was lapped, which is not a close finish however small
// the remainder looks.
inline int marginMs(const std::deque<LapLogEntry>& winner, int winnerLaps,
                    const std::deque<LapLogEntry>& second, int secondLaps) {
    if (winnerLaps <= 0 || winnerLaps != secondLaps) return -1;
    const int winnerMs = totalRaceTimeMs(winner, winnerLaps);
    const int secondMs = totalRaceTimeMs(second, secondLaps);
    if (winnerMs < 0 || secondMs < 0) return -1;
    return secondMs - winnerMs;
}

}  // namespace FinishMargin
