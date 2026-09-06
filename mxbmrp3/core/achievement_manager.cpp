// ============================================================================
// core/achievement_manager.cpp
// See the header. The metric reads are the one place this file touches the
// rest of the plugin: every number comes from StatsManager's cached totals,
// converted here from the stored unit (m, ms) to the catalogue's (km, h, s).
// ============================================================================
#include "achievement_manager.h"
#include "exploration_stats.h"
#include "stats_manager.h"
#include "../game/game_config.h"
#include "../diagnostics/logger.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace Achievements;

AchievementManager& AchievementManager::getInstance() {
    static AchievementManager instance;
    return instance;
}

AchievementManager::AchievementManager() {
    // The rows this game can move. A crash-state entry on a game that never
    // reports one would sit at "0 / 1" forever (Frequent Flyer) or count every
    // race as clean (Rubber Side Down) -- neither is an achievement, so neither
    // is a row.
    for (int i = 0; i < COUNT; ++i) {
        if (kCatalogue[i].needsCrashState && !GAME_HAS_CRASH_STATE) continue;
        if (kCatalogue[i].needsFmx && !GAME_HAS_FMX) continue;
        if (isDisabledId(kCatalogue[i].id)) continue;   // switched off in the catalogue
        m_rows[m_rowCount++] = i;
    }
}

// ---- persistence seam -------------------------------------------------------

void AchievementManager::restore(const char* id, int tier, int halfway) {
    const Entry* entry = findById(id);
    if (!entry) return;   // a newer build's row: keep ignoring it, never rewrite it
    const int idx = static_cast<int>(entry - kCatalogue);
    // The ROW's tier count: a row cut down to a one-shot keeps a stored 4 from
    // over-counting Completionist, which evaluate() (raise-only) would never fix.
    m_state[idx].tier = std::clamp(tier, 0, entry->tierCount);
    m_state[idx].halfway = std::clamp(halfway, 0, entry->tierCount);
}

void AchievementManager::clearStates() {
    for (State& s : m_state) s = State();
    m_configReloads = 0;
    m_loading = true;
}

// ---- events -----------------------------------------------------------------

void AchievementManager::onStatsLoaded() {
    m_loading = false;
    // An earned one-shot whose number sits below its threshold (a flag of 1.0
    // stored before the row became a number) reads as earned, so it shows so.
    for (int r = 0; r < m_rowCount; ++r) {
        const Entry& e = kCatalogue[m_rows[r]];
        if (e.metric != Metric::Exploration || !isOneShot(e) || m_state[m_rows[r]].tier < 1) continue;
        if (metricValue(e) < e.thresholds[0]) {
            StatsManager::getInstance().exploration().restoreAtLeast(e.signal, e.thresholds[0]);
        }
    }
    const Granted granted = evaluate(/*toastEach=*/false);
    if (granted.rows <= 0) return;
    DEBUG_INFO_F("[Achievements] %d tier(s) across %d achievement(s) granted on load",
                 granted.tiers, granted.rows);
    // One row moved: its own card, as a live earn would show it (and its click
    // opens its page). Several: one summary, counting ACHIEVEMENTS, not tiers --
    // a veteran whose file lands Racer straight on Gold has unlocked one.
    if (granted.rows == 1 && granted.lastRow >= 0) {
        queueTierToast(kCatalogue[granted.lastRow], m_state[granted.lastRow].tier);
        return;
    }
    Toast t;
    snprintf(t.title, sizeof(t.title), "%d achievements unlocked", granted.rows);   // rows >= 2 here
    snprintf(t.detail, sizeof(t.detail), "See Settings > Achievements");
    snprintf(t.icon, sizeof(t.icon), "award");
    enqueue(t);
}

void AchievementManager::onStatsChanged() {
    if (m_loading) return;
    evaluate(/*toastEach=*/true);
}

void AchievementManager::onConfigReloaded() {
    ++m_configReloads;
    m_dirty = true;
    evaluate(/*toastEach=*/true);
    if (m_devToast) {
        // A toast that means nothing, on demand: the layout/test loop for the
        // widget. Hidden INI key, like [Recorder] enabled.
        ++m_devToastCount;
        Toast t;
        snprintf(t.title, sizeof(t.title), "Test toast #%d", m_devToastCount);
        snprintf(t.detail, sizeof(t.detail), "[Achievements] devToast=1");
        snprintf(t.icon, sizeof(t.icon), "wrench");
        enqueue(t);
    }
}

void AchievementManager::clearAll() {
    clearStates();
    m_toasts.clear();
    m_loading = false;   // no load follows this: feeds count again at once
    m_dirty = true;
}

// ---- evaluation ---------------------------------------------------------------

double AchievementManager::metricValue(const Entry& e) const {
    // Completion is a share of tiers, already the product of every other row.
    const double v = metricValueRaw(e);
    return e.metric == Metric::Completion ? v : v * m_devValueScale;
}

double AchievementManager::metricValueRaw(const Entry& e) const {
    const StatsManager& sm = StatsManager::getInstance();
    switch (e.metric) {
        case Metric::DistanceKm:     return sm.getTotalOdometer() / 1000.0;
        case Metric::RideTimeHours:  return static_cast<double>(sm.getGlobalTotalTimeMs()) / 3600000.0;
        case Metric::Laps:           return sm.getGlobalTotalLaps();
        case Metric::Races:          return sm.getGlobalStats().raceCount;
        case Metric::Wins:           return sm.getGlobalStats().firstPositions;
        case Metric::Podiums: {
            const GlobalStats g = sm.getGlobalStats();
            return g.firstPositions + g.secondPositions + g.thirdPositions;
        }
        case Metric::FastestLaps:    return sm.getGlobalStats().fastestLapCount;
        case Metric::CleanRaces:     return sm.getGlobalStats().cleanRaceCount;
        case Metric::Crashes:        return sm.getGlobalTotalCrashes();
        case Metric::Penalties:      return sm.getGlobalTotalPenalties();
        case Metric::PenaltyTimeSec: return static_cast<double>(sm.getGlobalTotalPenaltyTimeMs()) / 1000.0;
        case Metric::GearShifts:     return sm.getGlobalTotalGearShifts();
        case Metric::Tracks:         return sm.getDistinctTrackCount();
        case Metric::Bikes:          return sm.getDistinctBikeCount();
        case Metric::PersonalBests:  return sm.getGlobalStats().pbCount;
        case Metric::Breakout:       return sm.getGlobalStats().breakoutHighScore;
        case Metric::ConfigReloads:  return m_configReloads;
        case Metric::RainRaces:      return sm.getGlobalStats().rainRaceCount;
        case Metric::BigGridRaces:   return sm.getGlobalStats().bigGridRaceCount;
        case Metric::MaxSessionLaps: return sm.getGlobalStats().maxSessionLaps;
        case Metric::FmxTricks:      return sm.getFmxLifetime().tricksLanded;
        case Metric::FmxScore:       return static_cast<double>(sm.getFmxLifetime().totalScore);
        case Metric::FmxBackflips:   return sm.getFmxLifetime().backflips;
        case Metric::FmxAirtimeSec:  return sm.getFmxLifetime().longestAirtimeSec;
        case Metric::FmxWheelieSec:  return sm.getFmxLifetime().longestWheelieSec;
        case Metric::FmxWheelieKm:   return sm.getFmxLifetime().wheelieDistanceM / 1000.0;
        case Metric::FmxChainScore:  return sm.getFmxLifetime().bestChainScore;
        case Metric::FmxKinds:       return static_cast<double>(sm.getFmxLifetime().kinds.size());
        case Metric::MaxSessionHours: return static_cast<double>(sm.getGlobalStats().maxSessionTimeMs) / 3600000.0;
        case Metric::PenaltyFreeStreak: return sm.getGlobalStats().bestPenaltyFreeStreak;
        case Metric::PbLeaps:        return sm.getGlobalStats().pbLeaps;
        case Metric::FmxFrontflips:  return sm.getFmxLifetime().frontflips;
        case Metric::MaxTrackLaps:   return sm.getMaxLapsAtOneTrack();
        case Metric::MaxBikeKm:      return sm.getMaxOdometerOnOneBike() / 1000.0;
        case Metric::BikeClasses:    return sm.getDistinctBikeClassCount();
        case Metric::FmxWhips:       return sm.getFmxLifetime().whips;
        case Metric::FmxScrubs:      return sm.getFmxLifetime().scrubs;
        case Metric::FmxOppos:     return sm.getFmxLifetime().oppos;
        case Metric::FmxTurnDowns:   return sm.getFmxLifetime().turnDowns;
        case Metric::Exploration:    return sm.exploration().get(e.signal);
        case Metric::Completion:     return completionPercent();
        case Metric::COUNT:          break;
    }
    return 0.0;
}

double AchievementManager::completionPercent() const {
    int earned = 0, total = 0;
    for (int r = 0; r < m_rowCount; ++r) {
        const int idx = m_rows[r];
        const Entry& e = kCatalogue[idx];
        if (e.metric == Metric::Completion) continue;
        earned += m_state[idx].tier;             // hidden ones count on top...
        if (!e.hidden) total += e.tierCount;      // ...of a total they are not in
    }
    return total > 0 ? 100.0 * earned / total : 0.0;
}

AchievementManager::Granted AchievementManager::evaluate(bool toastEach) {
    Granted granted;
    for (int r = 0; r < m_rowCount; ++r) {
        const int idx = m_rows[r];
        const Entry& e = kCatalogue[idx];
        const double value = metricValue(e);
        const int reached = tierFor(e, value);
        State& s = m_state[idx];
        // Halfway to Gold or Platinum: the two long steps get one milestone
        // card at their midpoint (Steam's IndicateAchievementProgress, in
        // spirit). Recorded either way; toasted only for a live change, so a
        // load that finds a rider past a midpoint says nothing.
        if (reached >= 2 && reached < e.tierCount && s.halfway <= reached) {
            const double lo = e.thresholds[reached - 1];
            const double hi = e.thresholds[reached];
            if (value >= (lo + hi) * 0.5) {
                s.halfway = reached + 1;
                m_dirty = true;
                if (toastEach && reached == s.tier) queueHalfwayToast(e, reached + 1, value);
            }
        }
        if (reached <= s.tier) continue;
        // Several tiers at once (a big retro-grant, or a lowered threshold): the
        // toast is for the HIGHEST, the count is for all of them.
        granted.tiers += reached - s.tier;
        ++granted.rows;
        granted.lastRow = idx;
        s.tier = reached;
        m_dirty = true;
        if (toastEach) queueTierToast(e, reached);
        DEBUG_INFO_F("[Achievements] %s reached %s", e.id, tierName(reached));
    }
    return granted;
}

void AchievementManager::queueTierToast(const Entry& entry, int tier) {
    Toast t;
    // A one-shot has no metal: the title alone, in the plain colour.
    if (isOneShot(entry)) {
        snprintf(t.title, sizeof(t.title), "%s", entry.title);
        t.tier = 0;
    } else {
        snprintf(t.title, sizeof(t.title), "%s - %s", entry.title, tierName(tier));
        t.tier = tier;
    }
    formatDescription(entry, tier, t.detail, sizeof(t.detail));
    snprintf(t.icon, sizeof(t.icon), "%s", entry.icon ? entry.icon : "");
    t.catalogueIndex = static_cast<int>(&entry - kCatalogue);
    enqueue(t);
}

void AchievementManager::queueHalfwayToast(const Entry& entry, int targetTier, double value) {
    Toast t;
    snprintf(t.title, sizeof(t.title), "%s - halfway to %s", entry.title, tierName(targetTier));
    formatProgress(entry, value, t.detail, sizeof(t.detail));
    snprintf(t.icon, sizeof(t.icon), "%s", entry.icon ? entry.icon : "");
    t.tier = targetTier;
    t.catalogueIndex = static_cast<int>(&entry - kCatalogue);
    enqueue(t);
}

void AchievementManager::enqueue(const Toast& toast) {
    if (!m_toastsEnabled) return;   // display is off: nothing is held for later
    ++m_toastsQueued;
    m_lastToast = toast;
    if (m_toasts.size() >= MAX_QUEUED_TOASTS) m_toasts.pop_front();
    m_toasts.push_back(toast);
}

bool AchievementManager::takeToast(Toast& out) {
    if (m_toasts.empty()) return false;
    out = m_toasts.front();
    m_toasts.pop_front();
    return true;
}

// ---- queries ------------------------------------------------------------------

AchievementManager::Row AchievementManager::row(int i) const {
    Row r{};
    if (i < 0 || i >= m_rowCount) {
        r.entry = &kCatalogue[0];
        return r;
    }
    const int idx = m_rows[i];
    r.entry = &kCatalogue[idx];
    r.tier = m_state[idx].tier;
    r.value = metricValue(*r.entry);
    return r;
}

double AchievementManager::valueOf(int catalogueIndex) const {
    if (catalogueIndex < 0 || catalogueIndex >= COUNT) return 0.0;
    return metricValue(kCatalogue[catalogueIndex]);
}

int AchievementManager::earnedUnits() const {
    int earned = 0;
    for (int r = 0; r < m_rowCount; ++r) earned += m_state[m_rows[r]].tier;
    return earned;
}

int AchievementManager::earnedAchievements() const {
    int earned = 0;
    for (int r = 0; r < m_rowCount; ++r) earned += m_state[m_rows[r]].tier > 0 ? 1 : 0;
    return earned;
}

int AchievementManager::listedAchievements() const {
    int listed = 0;
    for (int r = 0; r < m_rowCount; ++r) listed += kCatalogue[m_rows[r]].hidden ? 0 : 1;
    return listed;
}

int AchievementManager::totalUnits() const {
    int total = 0;
    for (int r = 0; r < m_rowCount; ++r) {
        const Entry& e = kCatalogue[m_rows[r]];
        if (!e.hidden) total += e.tierCount;
    }
    return total;
}

// ---- settings -------------------------------------------------------------------

void AchievementManager::setToastsEnabled(bool on) {
    m_toastsEnabled = on;
    if (!on) {
        m_toasts.clear();
        StatsManager::getInstance().exploration().onToastsDisabled();   // Ungrateful
    }
}

void AchievementManager::setDevValueScale(double scale) {
    // Finite and within reason: a hand-typed 0 or NaN would freeze every row.
    m_devValueScale = (std::isfinite(scale) && scale >= 1.0 && scale <= 1000.0) ? scale : 1.0;
}

void AchievementManager::setToastDurationMs(int ms) {
    m_toastDurationMs = std::clamp(ms, MIN_TOAST_DURATION_MS, MAX_TOAST_DURATION_MS);
}
