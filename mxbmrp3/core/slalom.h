// ============================================================================
// core/slalom.h
// Slalom: riders down, ridden past. Peace Out's measurement (three down at
// once, a one-shot) turned into a lifetime count: every rider on the floor
// within the same ten metres the heap is measured over, ridden past upright
// at speed and away from your own crash, counts ONCE PER CRASH. The caller
// (PluginData::updateProximity) decides "upright, at speed, not your own
// crash" exactly as Peace Out does and passes it in as `eligible`.
//
// WHY A SET AND NOT AN EDGE. A rider lies in the landing for a second or
// more, across a hundred position batches, and the player is inside the
// radius for many of them: counting batches would score one fallen rider
// fifty times. So a rider is credited once and remembered until they are
// seen upright again (or leave the batch), which is also what keeps a reused
// race number from inheriting a departed rider's credit.
//
// PERFORMANCE: the caller only calls this while somebody is down or a credit
// is still held, so a race nobody falls in never walks the field for it. The
// two sets keep their capacity (FlatIntSet), so a steady state allocates
// nothing. Pinned by tests/unit/test_achievement_feeds.cpp.
// ============================================================================
#pragma once

#include "flat_int_map.h"
#include "../game/unified_types.h"

namespace Slalom {

// Peace Out's radius (plugin_data_trackpos.cpp, countRidersDownNear): close
// enough to be the same incident.
constexpr float RADIUS_M = 10.0f;

class Tracker {
public:
    // One position batch. `me` is the player's slot in `positions`. Returns
    // how many riders were newly credited in this batch.
    int update(const Unified::TrackPositionData* positions, int count, int me, bool eligible) {
        m_stillDown.clear();
        int credited = 0;
        for (int i = 0; i < count; ++i) {
            if (i == me || !positions[i].crashed) continue;
            const int num = positions[i].raceNum;
            const bool held = m_credited.count(num) != 0;
            if (held) { m_stillDown.insert(num); continue; }
            if (!eligible) continue;
            const float dx = positions[i].posX - positions[me].posX;
            const float dz = positions[i].posZ - positions[me].posZ;
            if (dx * dx + dz * dz > RADIUS_M * RADIUS_M) continue;
            m_stillDown.insert(num);
            ++credited;
        }
        // Keep only riders still on the floor: one back up, or gone, can be
        // credited again the next time they fall.
        m_credited.clear();
        for (int num : m_stillDown) m_credited.insert(num);
        return credited;
    }
    bool holding() const { return !m_credited.empty(); }
    void clear() { m_credited.clear(); m_stillDown.clear(); }

private:
    FlatIntSet m_credited;
    FlatIntSet m_stillDown;
};

}  // namespace Slalom
