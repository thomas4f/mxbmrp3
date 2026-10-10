// ============================================================================
// hud/official_gap_freeze.cpp
// The shared split/lap freeze of the Gap Bar, Lap Log and Timing -- see the header.
// ============================================================================
#include "official_gap_freeze.h"

#include "../core/plugin_data.h"
#include "../core/stats_manager.h"

namespace {

int timeTo(const LapLogEntry& lap, int splitIndex) {
    return timeToCrossing(lap.lapTime, lap.sector1, lap.sector2, lap.sector3, splitIndex);
}

bool displayRiderIsPlayer(const PluginData& data) {
    const int displayRaceNum = data.getDisplayRaceNum();
    return displayRaceNum < 0 || displayRaceNum == data.getPlayerRaceNum();
}

} // namespace

void OfficialGapFreeze::reset() {
    m_crossings.reset();
    m_hold.stop();
    m_frozenGap = 0;
}

void OfficialGapFreeze::cacheAllTimePB() {
    // The PB scope's PB for the current track and bike, the one PluginData plants
    // as the live all-time reference. Copied at once: under the class scope the
    // pointer is a scratch buffer.
    const StatsPersonalBestData* pb = StatsManager::getInstance().getPersonalBest();
    m_allTimeLap = (pb && pb->isValid()) ? pb->lapTime : -1;
    for (int i = 0; i < NUM_SPLITS; ++i) {
        m_allTimeSplits[i] = m_allTimeLap > 0 ? timeToCrossing(m_allTimeLap, pb->sector1, pb->sector2, pb->sector3, i) : -1;
    }
}

int OfficialGapFreeze::referenceTime(Ref ref, int splitIndex) const {
    const PluginData& data = PluginData::getInstance();
    switch (ref) {
        case Ref::SESSION_PB: {
            // On the crossing that SETS the PB this reads 0; update() then
            // compares against the previous PB.
            const LapLogEntry* pb = data.getBestLapEntry();
            return pb ? timeTo(*pb, splitIndex) : -1;
        }
        case Ref::LAST_LAP: {
            // The most recent valid lap before the one being measured: at the line
            // that lap is entry [0], so the scan starts at [1].
            const std::deque<LapLogEntry>* lapLog = data.getLapLog();
            if (!lapLog) return -1;
            for (size_t i = splitIndex < 0 ? 1 : 0; i < lapLog->size(); ++i) {
                const LapLogEntry& lap = (*lapLog)[i];
                if (!lap.isValid) continue;
                return timeTo(lap, splitIndex);   // its time, or none: never an older lap's
            }
            return -1;
        }
        case Ref::ALLTIME_PB:
            if (!displayRiderIsPlayer(data)) return -1;
            return splitIndex < 0 ? m_allTimeLap : m_allTimeSplits[splitIndex];
    }
    return -1;
}

bool OfficialGapFreeze::update(Ref ref, int durationMs) {
    const PluginData& data = PluginData::getInstance();
    bool changed = false;

    // New session or spectate target: drop the freeze, adopt what is there.
    const SplitCrossingDetector::Result crossing = m_crossings.poll();
    if (crossing.adopted) {
        changed = m_hold.active();
        m_hold.stop();
        m_liveSeenThisLap = false;
        cacheAllTimePB();
        return changed;
    }

    // A freeze taken against another reference is not this one's reading.
    if (m_hold.active() && m_frozenRef != ref) {
        m_hold.stop();
        changed = true;
    }

    // The freeze holds the official value of the gap the HUD shows, so a lap it
    // shows none on (the out-lap from the pits, a grid start's opening lap)
    // holds nothing either. A lap the timer dropped (pit exit) is not read live,
    // whatever was read on it before.
    const bool liveNow = data.hasValidLiveGap(ref);
    if (!data.isLapTimerValid()) m_liveSeenThisLap = false;
    const IdealLapData* idealLapData = data.getIdealLapData();

    // Hold the gap of a crossing (split index, or -1 = the line) at `time`.
    auto hold = [&](int time, int splitIndex) {
        if (durationMs <= 0 || time <= 0) return;
        int refTime = referenceTime(ref, splitIndex);
        const LapLogEntry* sessionPb = ref == Ref::SESSION_PB ? data.getBestLapEntry() : nullptr;
        if (splitIndex < 0 && sessionPb && idealLapData &&
            sessionPb->lapNum == idealLapData->lastCompletedLapNum) {
            // This line set the session PB (the best lap IS the lap just
            // completed), so its gap is against the previous one. Lap identity,
            // not time equality: a lap that TIES the PB to the millisecond is not
            // a new PB and reads +0.000 against it. A split is never the PB
            // lap's, so a split equal to the PB's reads 0 too.
            const int previous = timeToCrossing(idealLapData->previousBestLapTime,
                idealLapData->previousBestSector1, idealLapData->previousBestSector2,
                idealLapData->previousBestSector3, splitIndex);
            if (previous > 0) refTime = previous;
        }
        if (refTime <= 0) return;
        m_frozenGap = time - refTime;
        m_frozenRef = ref;
        m_hold.start();
        changed = true;
    };

    // The line ends the lap, so whatever split it still held goes. An invalid
    // lap (cut, or through the pits) holds nothing: the new lap starts over here.
    if (crossing.line) {
        if (m_hold.active()) {
            m_hold.stop();
            changed = true;
        }
        if (crossing.lapValid && m_liveSeenThisLap) hold(crossing.lapTime, -1);
        m_liveSeenThisLap = false;
        // The all-time PB the next lap is measured against: this lap may have
        // just become it, and was read against the one before.
        cacheAllTimePB();
    }

    // Official splits (accumulated time to S1, S2 and, in 4-sector games, S3)
    if (crossing.splitIndex >= 0) {
        // The PB can change between lines (a scope switch in Settings, a
        // stats reload) while the live reading already follows it; one
        // lookup per crossing keeps the held split on the same PB.
        if (ref == Ref::ALLTIME_PB) cacheAllTimePB();
        if (m_liveSeenThisLap || liveNow) hold(crossing.splitTime, crossing.splitIndex);
    }
    if (liveNow) m_liveSeenThisLap = true;

    if (m_hold.expire(durationMs)) changed = true;
    return changed;
}

bool OfficialGapFreeze::shownGap(Ref ref, int* gapMs) const {
    if (m_hold.active()) {
        *gapMs = m_frozenGap;
        return true;
    }
    const PluginData& data = PluginData::getInstance();
    if (data.hasValidLiveGap(ref)) {
        *gapMs = data.getLiveGap(ref);
        return true;
    }
    return false;
}
