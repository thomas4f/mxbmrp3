// ============================================================================
// core/achievement_manager.cpp
// See the header. The metric reads are the one place this file touches the
// rest of the plugin: every number comes from StatsManager's cached totals,
// converted here from the stored unit (m, ms) to the catalogue's (km, h, s).
// ============================================================================
#include "achievement_manager.h"
#include "achievement_text.h"
#include "completion_floor.h"
#include "exploration_stats.h"
#include "settings_manager.h"
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
    // NO ROWS AT ALL on a game without the feature (GAME_HAS_ACHIEVEMENTS). Every
    // path through this manager walks m_rows, so an empty set is the whole thing
    // off: evaluate() grants nothing, no toast is queued, the summary figures are
    // zero and the persistence seam has nothing to write. The widgets and the
    // settings tab are gated where they are registered.
    if (!GAME_HAS_ACHIEVEMENTS) return;
    // The rows this game can move. A crash-state entry on a game that never
    // reports one would sit at "0 / 1" forever (Skill Issue) or count every
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
    // NOTHING IS WRITTEN BACK HERE. An earned one-shot whose number sits below
    // its threshold used to have the number raised to meet it, so the row would
    // not read "Earned" beside "1 / 100". That was a rewrite of a LIFETIME
    // COUNTER on the strength of a display mismatch: every row whose threshold
    // was raised (Director's Cut 10 -> 500, Brick Breaker 100 -> 5,000, and the
    // rest) would have had a player's real tally inflated to the new bar on the
    // next load, permanently, and the usage survey reads those tallies. A tier
    // once earned is kept by restore() being raise-only; the count beside it is
    // what the player has actually done, and it catches up or it does not.
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

void AchievementManager::leaderFor(const Entry& e, char* out, size_t cap) const {
    if (!out || cap == 0) return;
    out[0] = '\0';
    const StatsManager& sm = StatsManager::getInstance();
    switch (e.metric) {
        // The bike's SHOWROOM name, not its abbreviation: the row has the whole
        // width between the title and the tier tag, and "FACTORY CRF450R" is
        // what the player picked in the game's own menu.
        case Metric::MaxBikeKm:
            snprintf(out, cap, "%s", sm.getMaxOdometerBikeName().c_str());
            return;
        case Metric::MaxTrackLaps:
            snprintf(out, cap, "%s", sm.getTrackDisplayName(sm.getMaxLapsTrackId()).c_str());
            return;
        default:
            return;
    }
}

bool AchievementManager::isPrestigeAvailable() const {
    // Developer mode is a second key on the same lock (see the header, and
    // PrestigeWidget::isUnlocked, which carries the same clause for the widget):
    // the trade and everything it unlocks have to be reachable for testing
    // without earning a hundred Platinums first.
    if (SettingsManager::getInstance().isDeveloperMode()) return true;
    for (int i = 0; i < Achievements::COUNT; ++i) {
        if (Achievements::completionMetal(Achievements::kCatalogue[i]) != Achievements::TIER_COUNT) continue;
        return m_state[i].tier > 0;
    }
    return false;
}

// ---- evaluation ---------------------------------------------------------------

double AchievementManager::metricValue(const Entry& e) const {
    // Completion is a share of tiers, already the product of every other row.
    const double v = metricValueRaw(e);
    // The completion rows are already percentages of the catalogue; scaling
    // them by the dev multiplier would push them past 100 and read as finished.
    return isCompletionRow(e) ? v : v * m_devValueScale;
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
        case Metric::FmxWheelieSec:  return sm.getFmxLifetime().longestWheelieSec;
        case Metric::FmxWheelieKm:   return sm.getFmxLifetime().wheelieDistanceM / 1000.0;
        case Metric::FmxChainScore:  return sm.getFmxLifetime().bestChainScore;
        case Metric::FmxKinds:       return static_cast<double>(sm.getFmxLifetime().kinds.size());
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
        case Metric::FmxEndoSec:     return sm.getFmxLifetime().longestEndoSec;
        case Metric::Exploration:    return sm.exploration().get(e.signal);
        case Metric::CompletionBronze:   return completionPercentAt(1);
        case Metric::CompletionSilver:   return completionPercentAt(2);
        case Metric::CompletionGold:     return completionPercentAt(3);
        case Metric::CompletionPlatinum: return completionPercentAt(TIER_COUNT);
        case Metric::COUNT:          break;
    }
    return 0.0;
}

// THE FOUR SWEEPS' NUMBER: the share of the counted rows standing at this metal
// or better, 0-100. One per metal, so each moves on its own and the page can say
// how far along each is - which the single "lowest metal held" row this replaced
// could not, because one un-earned row pinned the whole thing at zero.
//
// What is counted, and what is deliberately not, is countsTowardCompletion()
// (completion_floor.h holds the arithmetic). A one-shot's single tier counts as
// all four: it has no Silver to reach, so holding it must not hold a Sweep down.
//
double AchievementManager::completionPercentAt(int metal) const {
    return static_cast<double>(Achievements::completionPercentAt(
        m_rows, m_rowCount, metal, [this](int idx) { return m_state[idx].tier; }));
}

// TWO PASSES, AND THE ORDER IS THE POINT: a Sweep's value is read off the other
// rows' tiers (completionPercentAt), so every row it counts has to have been
// evaluated before it. Catalogue order nearly does that and not quite - four
// counted rows sit after the Completion block - which left the Sweeps one
// evaluation behind, and with them the Prestige gate: finish the ladder on one
// of those four and the Platinum Sweep waited for whatever moved a number next.
// A filter rather than a re-order, because an array order nobody may change is
// not a thing a reader can see.
AchievementManager::Granted AchievementManager::evaluate(bool toastEach) {
    Granted granted;
    for (int r = 0; r < m_rowCount; ++r) {
        if (isCompletionRow(kCatalogue[m_rows[r]])) continue;
        evaluateRow(m_rows[r], toastEach, granted);
    }
    for (int r = 0; r < m_rowCount; ++r) {
        if (!isCompletionRow(kCatalogue[m_rows[r]])) continue;
        evaluateRow(m_rows[r], toastEach, granted);
    }
    return granted;
}

void AchievementManager::evaluateRow(int idx, bool toastEach, Granted& granted) {
    const Entry& e = kCatalogue[idx];
    const double value = metricValue(e);
    const int reached = tierFor(e, value);
    State& s = m_state[idx];
    // Halfway to Gold or Platinum: the two long steps get one milestone card at
    // their midpoint (Steam's IndicateAchievementProgress, in spirit). Recorded
    // either way; toasted only for a live change, so a load that finds a rider
    // past a midpoint says nothing.
    if (reached >= 2 && reached < e.tierCount && s.halfway <= reached) {
        const double lo = e.thresholds[reached - 1];
        const double hi = e.thresholds[reached];
        if (value >= (lo + hi) * 0.5) {
            s.halfway = reached + 1;
            m_dirty = true;
            if (toastEach && reached == s.tier) queueHalfwayToast(e, reached + 1, value);
        }
    }
    if (reached <= s.tier) return;
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

// THE SUMMARY FIGURES, and the one rule behind all of them: an UNLISTED row
// (Entry::hidden, which both Hidden and Misfortune set) is in neither half of
// any of them. It is counted separately instead -- see bonusAchievements.
//
// This used to let a hidden row's tiers count in the earned half of a total
// they were not in, so the figure read past 100% ("50 / 45", 111%) and the
// overshoot was the reward for finding a secret. It also read as a counting
// fault to anyone who did not know the rule, and it left no room to say
// anything about the Misfortune rows, which should not flatter a completion
// figure at all. Both are answered by reporting the bonus as its own number.
int AchievementManager::earnedUnits() const {
    int earned = 0;
    for (int r = 0; r < m_rowCount; ++r) {
        if (!countsTowardCompletion(kCatalogue[m_rows[r]].group)) continue;
        earned += m_state[m_rows[r]].tier;
    }
    return earned;
}

int AchievementManager::earnedAchievements() const {
    int earned = 0;
    for (int r = 0; r < m_rowCount; ++r) {
        if (!countsTowardCompletion(kCatalogue[m_rows[r]].group)) continue;
        earned += m_state[m_rows[r]].tier > 0 ? 1 : 0;
    }
    return earned;
}

// The "(+n)": rows earned OUTSIDE the listed set, secrets and misfortunes
// alike. One number rather than two, because the tab's summary is one row and
// the question it answers is "what else have you got", not "of which kind".
int AchievementManager::bonusAchievements() const {
    int bonus = 0;
    for (int r = 0; r < m_rowCount; ++r) {
        const Entry& e = kCatalogue[m_rows[r]];
        // Everything outside the counted set EXCEPT the Sweeps: those measure the
        // set rather than adding to it, so one reading 100% would add itself to
        // the figure it is reporting.
        if (countsTowardCompletion(e.group) || e.group == Group::Completion) continue;
        bonus += m_state[m_rows[r]].tier > 0 ? 1 : 0;
    }
    return bonus;
}

int AchievementManager::listedAchievements() const {
    int listed = 0;
    for (int r = 0; r < m_rowCount; ++r) {
        listed += countsTowardCompletion(kCatalogue[m_rows[r]].group) ? 1 : 0;
    }
    return listed;
}

#if defined(MXBMRP3_TEST_BUILD)
void AchievementManager::testForceTiers(int tier, const char* exceptId) {
    for (int r = 0; r < m_rowCount; ++r) {
        const Entry& e = kCatalogue[m_rows[r]];
        if (!countsTowardCompletion(e.group)) continue;
        if (exceptId && std::strcmp(e.id, exceptId) == 0) continue;
        m_state[m_rows[r]].tier = std::min(tier, e.tierCount);
    }
    m_dirty = true;
}
#endif

int AchievementManager::totalUnits() const {
    int total = 0;
    for (int r = 0; r < m_rowCount; ++r) {
        const Entry& e = kCatalogue[m_rows[r]];
        if (countsTowardCompletion(e.group)) total += e.tierCount;
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
