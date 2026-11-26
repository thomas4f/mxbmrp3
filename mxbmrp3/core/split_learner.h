// ============================================================================
// core/split_learner.h
// Where the splits are on a lap, learned from the watched rider's track
// position at each crossing: the fallback for a game that sends no marker data
// with its centerline (GP Bikes, whose splits then still get markers after one
// lap). Per track, so kept across spectate changes; PluginData drops it on a
// session change. Read by the Gap Bar's ticks, the Dash and the decks, through
// PluginData::collectSplitPositions(), which prefers the centerline's own.
// Pure, so it is unit-tested on its own (test_split_crossing.cpp).
// ============================================================================
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

class SplitLearner {
public:
    static constexpr int SLOTS = 3;   // the widest game's splits

    SplitLearner() { reset(); }

    void reset() {
        for (int i = 0; i < SLOTS; ++i) {
            m_pos[i] = -1.0f;
            m_seen[i] = -1;
        }
        m_raceNum = -1;
        ++m_revision;
    }

    // The watched rider, their split times so far this lap and where they are
    // now. A split's time changes exactly when it is crossed (and resets to -1
    // at the line), so a new positive value is the rider standing on that split.
    // A new rider's splits so far are adopted, not taken for fresh crossings.
    void observe(int raceNum, const int (&splits)[SLOTS], float trackPos) {
        const bool adopt = raceNum != m_raceNum;
        m_raceNum = raceNum;
        for (int i = 0; i < SLOTS; ++i) {
            if (!adopt && splits[i] > 0 && splits[i] != m_seen[i] &&
                trackPos > 0.001f && trackPos < 0.999f) {
                m_pos[i] = trackPos;
                ++m_revision;
            }
            m_seen[i] = splits[i];
        }
    }

    // Advances whenever a position is learned or dropped
    uint32_t revision() const { return m_revision; }

    // Split positions (0-1 along the lap, S/F excluded, ascending) into out[];
    // returns how many. The centerline's (`official`, entry 0 the S/F line)
    // when the game sent them, else the learned ones.
    int collect(const std::vector<float>& official, float (&out)[SLOTS]) const {
        int n = 0;
        for (size_t i = 1; i < official.size() && n < SLOTS; ++i) {
            if (official[i] > 0.001f && official[i] < 0.999f) out[n++] = official[i];
        }
        if (n == 0) {
            for (int i = 0; i < SLOTS; ++i) {
                if (m_pos[i] > 0.0f) out[n++] = m_pos[i];
            }
        }
        std::sort(out, out + n);
        return n;
    }

private:
    float m_pos[SLOTS];
    int m_seen[SLOTS];
    int m_raceNum = -1;
    uint32_t m_revision = 0;
};
