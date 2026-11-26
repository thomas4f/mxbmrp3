// ============================================================================
// core/stats_manager_feeds.cpp
// The feeds for the November achievement rows - Slalom, Leapfrog, Grounded,
// Flat Out, Rush Hour, Back From the Dead, Fall From Grace, Photocopy and
// Nemesis - kept in their own TU because stats_manager.cpp sits at its line
// budget. Each body is a thin adapter: the rule itself is a pure header
// (slalom.h, leapfrog.h, lap_purity.h, race_finish_extras.h), pinned by the
// unit suite, and the counting is ExplorationStats'.
// ============================================================================
#include "stats_manager.h"
#include "plugin_data.h"
#include "race_finish_extras.h"

#include <vector>

void StatsManager::recordSlalom(const Unified::TrackPositionData* positions, int count, int me) {
    const int passed = m_slalom.update(positions, count, me, ridingPastOthers());
    if (passed > 0) m_exploration.onRodePastDown(passed);
}

void StatsManager::recordLeapfrog(const Unified::TrackPositionData* positions, int count, int me) {
    const int cleared = m_leapfrog.update(positions, count, me);
    if (cleared > 0) m_exploration.onLeapfrog(cleared);
}

void StatsManager::setAirborne(bool airborne) {
    if (m_airborne && !airborne) m_leapfrog.land();
    m_airborne = airborne;
}

void StatsManager::noteFlight() {
    m_lapPurity.flew = true;
}

void StatsManager::checkPureLap(bool isValid) {
    // recordLap has snapshotted the closed lap into the m_lastLap* fields.
    const double trackLength = PluginData::getInstance().getSessionData().trackLength;
    const bool clean = isValid && m_lastLapCrashes == 0 &&
                       LapPurity::watched(m_lastLapDistance, trackLength);
    const bool grounded = LapPurity::grounded(m_lapPurity, clean);
    const bool flatOut = LapPurity::flatOut(m_lapPurity, clean);
    m_lapPurity = LapPurity::Lap();
    if (grounded || flatOut) m_exploration.onPureLap(grounded, flatOut);
}

void StatsManager::recordRaceFinishExtras(const PluginData& pd, int playerRaceNum, int position,
                                          int starters) {
    // Once per race, so the flat copy is not a hot path; the buffer is still
    // reused rather than rebuilt.
    static std::vector<RaceFinishExtras::Rider> s_riders;
    const auto& classOrder = pd.getClassificationOrder();
    s_riders.clear();
    s_riders.reserve(classOrder.size());
    int me = -1;
    for (int num : classOrder) {
        const StandingsData* so = pd.getStanding(num);
        if (!so) continue;
        if (num == playerRaceNum) me = static_cast<int>(s_riders.size());
        RaceFinishExtras::Rider r;
        r.raceNum = num;
        r.state = so->state;
        r.gapLaps = so->gapLaps;
        r.bestLapMs = so->bestLap;
        s_riders.push_back(r);
    }
    const RaceFinishExtras::Facts f =
        RaceFinishExtras::of(s_riders.data(), static_cast<int>(s_riders.size()), me);

    // Nemesis is told NAMES, which ExplorationStats keeps in memory for this
    // game launch only; a race number is reassigned between servers.
    const RaceEntryData* ahead = f.aheadRaceNum >= 0 ? pd.getRaceEntry(f.aheadRaceNum) : nullptr;
    const RaceEntryData* behind = f.behindRaceNum >= 0 ? pd.getRaceEntry(f.behindRaceNum) : nullptr;

    ExplorationStats::RaceExtras extras;
    extras.position = position;
    extras.starters = starters;
    extras.raceCrashes = m_sessionCrashes;
    extras.lapsPutOn = f.lapsPutOn;
    extras.lastOfFinishers = f.lastOfFinishers;
    extras.bestLapTwin = f.bestLapTwin;
    extras.aheadName = ahead ? ahead->name : nullptr;
    extras.behindName = behind ? behind->name : nullptr;
    m_exploration.onRaceExtras(extras);
}
