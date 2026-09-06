// ============================================================================
// core/achievement_manager.h
// Lifetime achievements: which tier of each catalogue entry (core/achievements.h)
// the player has earned, the evaluation that awards a new one, and the queue of
// toasts the AchievementWidget shows for them.
//
// EVALUATION IS EVENT-DRIVEN, NEVER PER FRAME. StatsManager calls onStatsChanged()
// at the points where a lifetime number moves -- a lap, a race finish, a penalty,
// a crash edge, the ~100 m odometer mark -- and the hotkey path calls
// onConfigReloaded(). Each evaluation is ~32 threshold compares over numbers
// StatsManager already caches; nothing here allocates on the game thread after
// load except the toast text, which is formatted once per unlock.
//
// PERSISTENCE LIVES IN THE STATS FILE. An achievement is a fact about the same
// numbers mxbmrp3_stats.json holds, so it is stored beside them (one file to
// back up, one file to wipe) under an "achievements" block. StatsManager's
// persistence half reads and writes that block through stateOf()/restore(): this
// class never sees JSON, so its header pulls in nothing heavy.
//
// UPGRADE RULE (onStatsLoaded): everything the loaded numbers already satisfy is
// granted SILENTLY, then ONE summary toast ("12 achievements unlocked") is
// queued if anything was granted. A player with 5,000 km on the odometer would
// otherwise watch thirty toasts scroll past on the first ride after updating.
// The same pass makes a lowered threshold in a later release take effect on
// load rather than at the next lap.
//
// THE TOGGLE GATES DISPLAY ONLY. With toasts off, evaluation still runs and
// tiers are still recorded -- the settings tab is the same either way -- but
// nothing is queued, so nothing appears later when it is turned back on.
//
// TOAST TIMING: the widget starts a toast's clock when it TAKES it, not when it
// is queued. Load happens in the menus, where the game issues no Draw, and a
// clock started at queue time would expire unseen (the lesson notice_priority.h
// already pins for masked notices).
// ============================================================================
#pragma once

#include "achievements.h"

#include <cstdint>
#include <deque>

class AchievementManager {
public:
    static AchievementManager& getInstance();

    // Persisted per-entry state: the tier, and nothing else. No unlock date is
    // collected -- a timestamp per row is a record of when someone played, which
    // the stats file otherwise does not keep.
    struct State {
        int tier = 0;                // 0..Achievements::TIER_COUNT
        // The tier whose HALFWAY toast has been shown (0 = none): a milestone
        // card on the way to Gold and Platinum, the long steps, once each.
        int halfway = 0;
    };

    // One on-screen notification. Plain char arrays so the widget copies nothing
    // on its per-frame poll; `icon` names a marker sprite (see Entry::icon), or
    // is empty for the summary/test toasts, which use the generic award.
    struct Toast {
        char title[48] = {};
        char detail[64] = {};
        char icon[32] = {};
        int tier = 0;                // 0 = not a tier-up (summary / test toast)
        int catalogueIndex = -1;     // the row, for the click that opens its page (-1: none)
    };

    // ---- persistence seam (StatsManager's persistence half) ----------------
    // Replace every entry's state from the file. Unknown ids are ignored (a file
    // written by a newer build), and a tier outside the catalogue's range is
    // clamped. Called before onStatsLoaded().
    void restore(const char* id, int tier, int halfway = 0);
    void clearStates();
    const State& stateOf(int catalogueIndex) const { return m_state[catalogueIndex]; }
    int configReloads() const { return m_configReloads; }
    void setConfigReloads(int n) { m_configReloads = n < 0 ? 0 : n; }
    // True if anything changed since the stats file was last written; StatsManager
    // folds this into its own dirty flag on save.
    bool isDirty() const { return m_dirty; }
    void clearDirty() { m_dirty = false; }

    // ---- events -------------------------------------------------------------
    // After StatsManager finished loading: silent grants + one summary toast.
    void onStatsLoaded();
    // A lifetime number moved: award any tier newly reached, toasting each.
    void onStatsChanged();
    // The RELOAD_CONFIG hotkey (or its test hook): counts toward Tinkerer, and
    // with [Achievements] devToast=1 queues a numbered test toast every time.
    void onConfigReloaded();
    // "Reset all stats": every tier back to 0, counters to 0, queue emptied.
    void clearAll();

    // ---- queries for the settings tab -------------------------------------
    // Rows are the catalogue entries available on THIS game (crash-state gated
    // ones drop out on games that never report it), in catalogue order.
    struct Row {
        const Achievements::Entry* entry;
        int tier;
        double value;                // in the entry's unit
    };
    int rowCount() const { return m_rowCount; }
    Row row(int i) const;
    // A catalogue entry's current value, in its unit (the headless tests).
    double valueOf(int catalogueIndex) const;
    // Earned tiers over LISTED tiers, for the header bar and Completionist. A
    // hidden row is never in the total and its earned tiers count on top, so
    // the figure passes 100% (ten listed and one hidden: 110% at eleven). A
    // one-shot counts one.
    int earnedUnits() const;
    int totalUnits() const;
    // Achievements earned at any tier (a hidden one counts) over the listed
    // ones: the tab's summary, since a player counts achievements, not tiers.
    int earnedAchievements() const;
    int listedAchievements() const;

    // ---- toasts (AchievementWidget) ----------------------------------------
    bool hasPendingToast() const { return !m_toasts.empty(); }
    // Pops the oldest queued toast into `out`; false when the queue is empty.
    bool takeToast(Toast& out);
    // Every toast ever queued this run, whether or not one was shown. Cheap
    // observability for the headless tests.
    uint32_t toastsQueued() const { return m_toastsQueued; }
    const Toast& lastToast() const { return m_lastToast; }

    // ---- [Achievements] settings --------------------------------------------
    bool isToastsEnabled() const { return m_toastsEnabled; }
    void setToastsEnabled(bool on);
    int getToastDurationMs() const { return m_toastDurationMs; }
    void setToastDurationMs(int ms);
    // For the settings tab's data-driven stepper (SteppedControl::clampInt),
    // which writes the member directly within the same bounds setToastDurationMs
    // enforces.
    int* toastDurationMsPtr() { return &m_toastDurationMs; }
    bool isDevToastEnabled() const { return m_devToast; }
    void setDevToastEnabled(bool on) { m_devToast = on; }
    // Dev-only [Achievements] devScale: every row's number is multiplied by this
    // at read (never stored), so the whole catalogue -- tiers, toasts, halfway
    // cards, the rows' figures -- runs that many times faster for a test
    // session. 10 makes a 1,000 h row land at 100 h. 1 is off; kept in the INI
    // only while it is not 1, so it cannot linger unseen.
    double getDevValueScale() const { return m_devValueScale; }
    void setDevValueScale(double scale);

    static constexpr int MIN_TOAST_DURATION_MS = 1000;
    static constexpr int DEFAULT_TOAST_DURATION_MS = 5000;
    static constexpr int MAX_TOAST_DURATION_MS = 30000;
    static constexpr int TOAST_DURATION_STEP_MS = 1000;

private:
    AchievementManager();
    ~AchievementManager() = default;
    AchievementManager(const AchievementManager&) = delete;
    AchievementManager& operator=(const AchievementManager&) = delete;

    // The lifetime number behind a row, in the catalogue's display unit.
    double metricValue(const Achievements::Entry& e) const;   // the number, times the dev scale
    double metricValueRaw(const Achievements::Entry& e) const;
    // Completionist: tiers earned over tiers listed, every row but its own.
    double completionPercent() const;
    // Walk the available rows; award newly reached tiers. Every row, every time:
    // ~32 compares over numbers StatsManager already caches, cheaper than
    // keeping a per-call-site "which metrics moved" mask correct forever.
    struct Granted {
        int tiers = 0;               // tier steps awarded (a row can take several at once)
        int rows = 0;                // rows that moved at all: what the summary toast counts
        int lastRow = -1;            // the catalogue index of the last row moved (the card, when rows == 1)
    };
    Granted evaluate(bool toastEach);
    void enqueue(const Toast& toast);
    void queueTierToast(const Achievements::Entry& entry, int tier);
    void queueHalfwayToast(const Achievements::Entry& entry, int targetTier, double value);

    State m_state[Achievements::COUNT];
    // Catalogue indices available on this game, resolved once in the constructor.
    int m_rows[Achievements::COUNT] = {};
    int m_rowCount = 0;
    int m_configReloads = 0;
    int m_devToastCount = 0;
    double m_devValueScale = 1.0;
    bool m_dirty = false;
    // Until onStatsLoaded(): feeds fired before or while the stats file loads
    // evaluate nothing, so the silent grant + one summary toast stays the only
    // upgrade path. Closed from construction, not from clearStates(): the
    // settings load runs first and already feeds (a companion window opening
    // on its display target), and a toast queued then would outlive the load
    // that wipes the tier behind it.
    bool m_loading = true;

    std::deque<Toast> m_toasts;
    Toast m_lastToast;
    uint32_t m_toastsQueued = 0;
    static constexpr size_t MAX_QUEUED_TOASTS = 16;

    bool m_toastsEnabled = true;
    int m_toastDurationMs = DEFAULT_TOAST_DURATION_MS;
    bool m_devToast = false;
};
