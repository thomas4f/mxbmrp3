// ============================================================================
// core/race_finish_extras.h
// Four rows read off the final classification, beside the ones
// StatsManager::tryRecordRaceFinish already reads: Rush Hour (laps put on
// the others), Fall From Grace (last at the flag), Photocopy (a best lap
// equal to another rider's) and Nemesis (who finished either side of you).
// Pure, over a flat array in classification order, so the unit suite pins
// it without a game (tests/unit/test_achievement_feeds.cpp);
// race_finish_extras.cpp builds the array from PluginData.
// ============================================================================
#pragma once

namespace RaceFinishExtras {

// EntryState values (plugin_data_types.h): only these two matter here.
constexpr int STATE_RACING = 0;
constexpr int STATE_DNS = 1;

struct Rider {
    int raceNum = -1;
    int state = STATE_RACING;
    int gapLaps = 0;      // laps behind the leader
    int bestLapMs = -1;
};

struct Facts {
    int lapsPutOn = 0;            // laps every other classified finisher is down on the player
    bool lastOfFinishers = false; // nobody still classified as racing finished behind
    bool bestLapTwin = false;     // another starter's best lap equals the player's, exactly
    int aheadRaceNum = -1;        // the classified finisher directly ahead, or -1
    int behindRaceNum = -1;       // ... and directly behind
};

// `riders` in classification order; `me` the player's index in it.
inline Facts of(const Rider* riders, int count, int me) {
    Facts f;
    if (!riders || me < 0 || me >= count) return f;
    const Rider& self = riders[me];
    int racingOthers = 0;
    int racingBehind = 0;
    for (int i = 0; i < count; ++i) {
        if (i == me) continue;
        const Rider& r = riders[i];
        if (r.state != STATE_DNS && self.bestLapMs > 0 && r.bestLapMs == self.bestLapMs) {
            f.bestLapTwin = true;
        }
        if (r.state != STATE_RACING) continue;
        ++racingOthers;
        if (i > me) {
            ++racingBehind;
            if (f.behindRaceNum < 0) f.behindRaceNum = r.raceNum;
        } else {
            f.aheadRaceNum = r.raceNum;   // the last one before `me` wins: directly ahead
        }
        const int down = r.gapLaps - self.gapLaps;
        if (down > 0) f.lapsPutOn += down;
    }
    f.lastOfFinishers = racingOthers > 0 && racingBehind == 0;
    return f;
}

}  // namespace RaceFinishExtras
