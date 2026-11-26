// ============================================================================
// core/leapfrog.h
// Leapfrog: in the air, over another rider. Measured on the track-position
// batch while the player is airborne (FmxManager's ground state, relayed by
// StatsManager), against every upright rider: close enough sideways to be
// underneath you, and far enough below that you cleared them rather than rode
// beside them. Each rider counts once per flight.
//
// The positions are the game's own reference points on each chassis, so the
// height test is bike over bike. A rider on the floor is not jumped OVER in
// the sense the row means (that is Slalom's business, and a crashed bike lies
// low enough to make the test trivial), so crashed riders are skipped.
// Pinned by tests/unit/test_achievement_feeds.cpp.
// ============================================================================
#pragma once

#include "flat_int_map.h"
#include "../game/unified_types.h"

namespace Leapfrog {

constexpr float MAX_SIDEWAYS_M = 2.5f;   // within reach underneath: about a bike length
constexpr float MIN_ABOVE_M = 2.0f;      // clear over them, not alongside

class Tracker {
public:
    // One position batch while airborne. Returns the riders newly cleared.
    int update(const Unified::TrackPositionData* positions, int count, int me) {
        int cleared = 0;
        for (int i = 0; i < count; ++i) {
            if (i == me || positions[i].crashed) continue;
            const float dy = positions[me].posY - positions[i].posY;
            if (dy < MIN_ABOVE_M) continue;
            const float dx = positions[i].posX - positions[me].posX;
            const float dz = positions[i].posZ - positions[me].posZ;
            if (dx * dx + dz * dz > MAX_SIDEWAYS_M * MAX_SIDEWAYS_M) continue;
            if (m_cleared.insert(positions[i].raceNum)) ++cleared;
        }
        return cleared;
    }
    // Touchdown: the next flight may clear the same riders again.
    void land() { m_cleared.clear(); }

private:
    FlatIntSet m_cleared;
};

}  // namespace Leapfrog
