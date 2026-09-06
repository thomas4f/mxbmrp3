// ============================================================================
// core/exploration_stats.h
// The values behind Exploration::Signal (exploration_signals.h): one double
// per signal -- a flag is 0/1, a counter counts, a max-ever holds its peak --
// plus the name sets behind TabsVisited, VersionsRun and HudsTried. Owned by
// StatsManager and persisted beside the other numbers under an "exploration"
// block keyed by signal name, so a signal added or removed needs no parser
// change.
//
// FEED METHODS ARE THE ONLY WRITERS, named for the event, one call site each
// (grep the method to find it). Every feed marks the block dirty and asks the
// AchievementManager to evaluate, except the per-second tick() and the sums it
// carries, which evaluate once per second at most. All of it is game-thread:
// the one cross-thread source (the overlay's connection count) arrives as an
// atomic total read by the tick.
//
// THE CLOCK. Night Owl, Anniversary and Regular read the local wall clock. In
// a test build (MXBMRP3_TEST_BUILD) that clock is a fixed date unless a test
// sets it (MXBMRP3_Test_SetLocalTime), so no gate depends on the hour the CI
// runner happens to start at.
//
// FINGERPRINTS. "Edited outside the game" is a hash of the file as the plugin
// last wrote it against a hash of the file as it loaded. Each file carries its
// OWN fingerprint, computed over the file with the fingerprint itself left out
// (the stats file's `fingerprint` field; the settings file's trailing
// [Fingerprint] section), so there is no other file to fall out of step with.
// The comparison is skipped while a fingerprint is absent (an older file).
//
// STARTUP PULLS, NOT PUSHES, for what the settings already decided before the
// stats file loaded (developer mode, toasts off, the settings hash): a feed
// fired before load would be wiped by it.
// ============================================================================
#pragma once

#include "exploration_signals.h"

#include <cstdint>
#include <set>
#include <string>

class ExplorationStats {
public:
    ExplorationStats();

    // ---- reads (AchievementManager::metricValue) ---------------------------
    double get(Exploration::Signal s) const { return m_values[static_cast<int>(s)]; }

    // ---- feeds: one call site each -----------------------------------------
    // Startup, after the stats file loaded: days used, versions run, user packs,
    // custom.css, a crash dump from the previous run, the build's own nature,
    // and what the settings file (loaded first) already decided: developer
    // mode, toasts off, an update installed, a hand edit.
    void onStartup(const std::string& savePath, const char* version);
    // A stats session started (recordSessionStart): night owl, anniversary.
    void onSessionStart();   // a NEW session: the per-session scratch resets
    void onRunStart();       // any run start, a pit re-entry included: the clock only
    // RELOAD_CONFIG re-read the settings file: a hand edit shows here too, not
    // only at the next startup (the next save would re-fingerprint it away).
    void onSettingsReloaded();
    // What the setup says (which HUDs are on, how customised the look is, how
    // many hotkeys are bound, whether an experimental setting is on), read
    // live: at every settings edit (a panel click, a HUD hotkey) and at a
    // save, so nothing waits for the deferred write. A
    // HUD seen on joins the tried set for good: the percent is of the HUDs
    // this build has that were on at SOME point, not at once.
    void observeSettings(const class HudManager& hudManager);
    void onSettingsSaved(int hudsTriedPercent, int customisations, int hotkeysBound, bool experimental);
    void onTabOpened(const char* tabName);
    void onCompanionOpened();
    void onDirectorCut();
    void onSpotterCallout();
    void onProfileSwitched();   // by hand or by auto-switch
    void onSegmentCompleted();
    void onFactoryReset();
    void onToastsDisabled();
    void onRiderTracked();
    void onServerJoined(const char* serverName);   // Well Travelled: a name never seen before
    // Racing (StatsManager / race_lap_handler). Positions are 1-based.
    void onRaceLapPosition(int lapNum, int position);
    void onRaceFinished(int position, int gapToSecondMs, bool everyOtherRiderLapped, int ownGapLaps);
    void onRaceLeft();
    void onLapTime(int lapTimeMs);
    void onCrash(int sessionCrashes, int crashTally);
    void onSandbag();
    // FMX (StatsManager::recordFmxTrick / FmxManager::failTrick).
    void onTrickTime(bool airborne, bool shred, float seconds);
    void onChainCrash();
    void onChainLost(int points);   // the chain's score at the moment it broke
    // Once a second from the draw path (DrawHandler): the time sums and the
    // frame-rate flag. framesThisSecond is the Draw count since the last tick.
    void tick(bool spectating, bool rumbleLive, bool onTrack, int framesThisSecond,
              uint32_t overlayConnectionsTotal);

    // ---- fingerprints (see the header) ---------------------------------------
    // A file's hash as loaded against the fingerprint it carried (0 = none).
    void noteStatsLoadedHash(uint64_t loadedHash, uint64_t expected);
    static uint64_t fnv1a(const std::string& text);
    // The settings file's own fingerprint: a trailing [Fingerprint] section
    // over everything above it, appended last by the writer and cut off before
    // hashing by the reader (0 if unreadable).
    static std::string fingerprintTrailer(const std::string& fileText);
    static uint64_t hashFileAboveTrailer(const std::string& path);

    // ---- persistence surface (stats_manager_persistence.cpp) ----------------
    // Values by signal index, the name sets by their JSON key, and the
    // scalars, so the JSON block is written and read there like the rest of
    // the file.
    double rawValue(int index) const { return m_values[index]; }
    void restoreValue(int index, double v);
    // Lift a restored value to at least `v` (a one-shot's threshold when its row
    // is already earned): the five feats that were flags stored 1.0 in a file
    // from before they became numbers. No evaluation; the load's follows.
    void restoreAtLeast(Exploration::Signal s, double v);
    static constexpr const char* kNameSets[] = { "tabs", "versions", "huds", "servers" };
    static constexpr int NAME_SET_COUNT = 4;
    const std::set<std::string>& names(int which) const;
    void restoreName(int which, const std::string& name);
    const std::string& firstRunDate() const { return m_firstRunDate; }
    int lastDay() const { return m_lastDay; }
    int crashDumpsSeen() const { return m_crashDumpsSeen; }
    int dayStreak() const { return m_dayStreak; }
    void restoreScalars(const std::string& firstRunDate, int lastDay, int crashDumpsSeen, int dayStreak);
    void clear();
    bool isDirty() const { return m_dirty; }
    void clearDirty() { m_dirty = false; }

    // ---- test build ----------------------------------------------------------
    // The local clock the date-based signals read. Real in a shipping build.
    struct LocalTime { int year, month, day, hour; };
    static LocalTime localNow();
#ifdef MXBMRP3_TEST_BUILD
    static void setLocalTimeOverride(int year, int month, int day, int hour);
#endif

private:
    bool checkSettingsEdited();                        // the settings file's hash against its trailer
    bool mark(Exploration::Signal s);                 // 0 -> 1; true if it moved
    void add(Exploration::Signal s, double d);        // counter / sum
    bool raise(Exploration::Signal s, double v);      // max-ever; true if it moved
    void changed();                                    // dirty + evaluate

    // The clock signals (Regular; Night Owl and Anniversary while riding) and
    // the user-folder signals (the packs, the stylesheet), as one read each, so
    // the startup, a session start, a config reload and the once-a-minute tick
    // all ask the same question. True if a signal moved.
    bool checkClock(bool riding);
    bool scanUserFiles();

    double m_values[Exploration::SIGNAL_COUNT] = {};
    std::string m_savePath;            // the user tree, for the reload's rescan
    int m_clockTicks = 0;              // tick() re-reads the clock once a minute
    std::set<std::string> m_tabs;
    std::set<std::string> m_versions;
    std::set<std::string> m_huds;          // harness ids of HUDs ever seen on
    std::set<std::string> m_servers;       // server names ridden on
    std::string m_firstRunDate;        // "YYYY-MM-DD" of the first startup ever
    int m_lastDay = 0;                 // YYYYMMDD of the last counted day (a session across midnight counts the new one)
    int m_dayStreak = 0;               // days in a row up to m_lastDay; the row keeps the best run (DayStreak)
    int m_crashDumpsSeen = -1;         // .dmp files in crashes\ at the last startup; -1 = never counted
    bool m_dirty = false;

    // Per-session scratch, never persisted.
    int m_firstLapPosition = 0;
    bool m_ledEveryLap = true;
    int m_lastLaps[5] = {};
    int m_lapCount = 0;                // laps in the current Metronome window; zeroed by a hit
    std::set<int> m_sessionLapTimes;
    double m_crashFreeMs = 0.0;
    uint32_t m_overlaySeen = 0;        // the overlay's per-process connection total already counted
};
