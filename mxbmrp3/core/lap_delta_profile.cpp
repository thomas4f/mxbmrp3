// ============================================================================
// core/lap_delta_profile.cpp
// See lap_delta_profile.h.
// ============================================================================
#include "lap_delta_profile.h"

#include <algorithm>
#include <cstdlib>

#include "plugin_data.h"

bool LapDeltaProfile::sample(PbGapTracker::Ref ref) {
    const PluginData& pd = PluginData::getInstance();
    const PbGapTracker& t = pd.getPbGapTracker();
    // THE LINE WINDOW (PbGapTracker::atLine): between the wrap and the lap's
    // completion, in either order, the tracker holds one lap that has ended but
    // not been committed. Read now, the picture would flash the lap before it
    // whole and then snap to the right one; keep what is shown until it closes.
    if (t.atLine() && t.hasBestLap(ref)) return last >= 0 || anyPrev;
    live = pd.hasValidLiveGap(ref);
    last = -1;
    anyPrev = false;
    maxAbsMs = 0;
    std::fill(std::begin(has), std::end(has), false);
    std::fill(std::begin(prevHas), std::end(prevHas), false);
    // After a spectate switch the tracker's tables are still the previous
    // rider's until the new one's first position sample: show nothing of them
    // (the gap is not live either), rather than the old rider's last lap.
    if (!pd.lapTimerFollowsDisplayRider()) { prevFrom = 0; return false; }
    const int top = PbGapTracker::NUM_POINTS - 1;

    // Every slot the lap has sampled, not only those up to where the rider is
    // now: riding back (a crash, a missed jump) keeps what was already drawn
    // rather than erasing it. A slot ridden again is resampled with its new
    // time, as the Gap Bar reads it.
    if (live) {
        const PbGapTracker::Table& lap = t.currentTable();
        for (int p = 0; p < POINTS; ++p) {
            const int slot = std::min(top, p * STEP);
            if (!lap[slot].valid) continue;
            bool ok = false;
            const int gap = t.gapAt(static_cast<float>(slot) / PbGapTracker::NUM_POINTS, lap[slot].elapsedMs, &ok, ref);
            if (!ok) continue;
            gapMs[p] = static_cast<float>(gap);
            has[p] = true;
            last = p;
            maxAbsMs = std::max(maxAbsMs, std::abs(gap));
        }
    }

    prevFrom = live ? std::max(last, static_cast<int>(t.trackPos() * PbGapTracker::NUM_POINTS) / STEP) + 1 + OVERWRITE_GAP : 0;
    if (t.hasLastLapGaps(ref)) {
        const PbGapTracker::GapTable& prev = t.lastLapGaps(ref);
        for (int p = prevFrom; p < POINTS; ++p) {
            const PbGapTracker::GapPoint& g = prev[std::min(top, p * STEP)];
            if (!g.valid) continue;
            prevGapMs[p] = static_cast<float>(g.gapMs);
            prevHas[p] = true;
            anyPrev = true;
            maxAbsMs = std::max(maxAbsMs, std::abs(g.gapMs));
        }
    }
    return last >= 0 || anyPrev;
}
