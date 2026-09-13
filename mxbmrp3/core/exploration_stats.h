// ============================================================================
// core/exploration_stats.h
// The values behind Exploration::Signal (exploration_signals.h): one double
// per signal -- a flag is 0/1, a counter counts, a max-ever holds its peak --
// plus the name sets behind TabsVisited, HudsTried and Servers. Owned by
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
    void onStartup(const std::string& savePath);
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
    // The percent of the tabs the sidebar OFFERS that have ever been opened. The
    // caller passes the whole list, because the fraction has to be counted over
    // it in both halves -- the set persists names this build does not list (a
    // game-gated tab, or About before it stopped being recorded). See the .cpp.
    void onTabOpened(const char* tabName, const char* const* listedTabs, int listedCount);
    void onCompanionOpened();
    void onDirectorCut();
    void onSpotterCallout();
    void onProfileSwitched();   // by hand or by auto-switch
    void onSegmentCompleted();
    void onFactoryReset();
    void onToastsDisabled();
    void onRiderTracked();
    void onServerJoined(const char* serverName);   // Well Travelled: a name never seen before
    // Racing (StatsManager / race_lap_handler). Positions are 1-based. No lap
    // number: the starting position comes from the opening split now, so where
    // in the race a crossing happened no longer changes what is recorded.
    void onRaceLapPosition(int position, int lapNum);
    // THE SMALLEST GRID THAT IS A RACE. Alone in the lobby every line is yours
    // first and every lap is led, so Holeshot, Wire to Wire and Perfect Race -
    // the rows that read the field rather than the clock - want one other rider
    // entered. One is enough: the rows are not about grid size, only about not
    // being handed out for riding round an empty server. Survivor keeps its own
    // higher floor (a quarter of two is one rider).
    static constexpr int MIN_FIELD = 2;
    // The gate physically dropping (race_classification_handler), with the
    // number of STARTERS in the classification that dropped it (a DNS is not
    // one - the same count RaceFinish::starters reads): arms the holeshot when
    // that is a field (MIN_FIELD), which the first opening line claims. A
    // pit start never arms it, and neither does joining a race already under
    // way - both are correct, a holeshot is won off the gate.
    void onGateDrop(int starters);
    // A rider crossed an opening line (handlers/opening_line.h). Only the first
    // one after a gate drop is looked at; the rest of the race's lines cost one
    // bool test.
    void onFirstSplit(bool isPlayer);
    // Did THIS race's holeshot go to the player? Read by the spotter's callout
    // so it does not re-derive an answer this already has. Per race: onGateDrop
    // clears it.
    bool tookHoleshot() const { return m_gotHoleshot; }
    // The holeshot arm, read by handlers/opening_line.cpp to tell the crossing
    // that settled it from the ones after: raised by onGateDrop, cleared by the
    // first crossing of any rider.
    bool holeshotArmed() const { return m_holeshotArmed; }
    // The player's OWN first crossing of it, with where they sat at that moment.
    // This is where Charger starts counting: the run to the first corner is the
    // charge, and measuring from the end of lap one threw it away. Armed by the
    // gate drop, so a pit start or a mid-race join falls back to lap one.
    void onPlayerOpeningSplit(int position);
    // A lap the player completed, clean = no crash and no penalty on it. The
    // run is per-session scratch: leaving the track ends it rather than
    // silently carrying it across a reload.
    void onLapCompleted(bool clean);
    // What a finished race looked like, read off the classification the finish
    // was recorded with (StatsManager::tryRecordRaceFinish). Grouped because
    // every signal below wants a different slice of the same one classification
    // and the argument list had run out of room.
    struct RaceFinish {
        int position = 0;              // 1-based, final and classified
        // The EXACT margin over the runner-up (FinishMargin), or -1 when it
        // cannot be known: a lapped runner-up, a rolled-over lap log, or an
        // order that penalties decided rather than lap times. Photo Finish
        // takes only a positive margin, so each of those simply does not count.
        int gapToSecondMs = -1;
        // The mirror, for So Close: the player's margin BEHIND the winner, or
        // -1 when it cannot be known for the same reasons. Only meaningful when
        // position > 1, and a separate field rather than a signed reuse of the
        // one above -- "how far ahead" and "how far behind" are two questions,
        // and a caller that conflated them would read a win as a loss by the
        // same tenth.
        int gapToWinnerMs = -1;
        bool everyOtherRiderLapped = false;
        int ownGapLaps = 0;            // the player's own laps down
        int starters = 0;              // riders that took the start (a DNS did not)
        int retired = 0;               // of those, the ones that retired or were disqualified
        // The player's classified laps, which Last Gasp compares against the
        // lap positions it has actually seen: the classification can settle
        // before the final RaceLap arrives, and the pair is one lap behind
        // until it does. See the row in onRaceFinished().
        int lapsAtFinish = 0;
        // The fastest lap of the race was the player's. Read by two rows from
        // opposite ends: Perfect Race wants it beside a win, Consolation Prize
        // wants it beside a finish off the podium.
        bool hadFastestLap = false;
    };
    void onRaceFinished(const RaceFinish& race);
    // Photo Finish and So Close alone, for the retry when the lap logs were still
    // filling at the flag. Marks, so calling it twice costs nothing.
    bool onFinishMargin(int position, int gapToSecondMs, int gapToWinnerMs);
    // Last Gasp alone, for the same retry: the row is deferred, not skipped,
    // when the flag beat the player's final lap here. Consumes the deferral, so
    // a second call counts nothing.
    bool retryLastGasp();
    // Fuel (StatsManager::updateTelemetry). onFuelBurnt takes a BATCH of
    // litres, not a tick's worth: the caller accumulates and flushes on the
    // odometer's ~100m cadence, so the achievement evaluation this triggers
    // never lands on a 100Hz path. The other two are edges, so they cost two
    // float compares per tick and nothing else.
    void onFuelBurnt(double litres);
    void onRanDry();
    void onFinishedOnFumes();
    // Seconds spent in another rider's roost, BATCHED the way
    // onFuelBurnt is: the caller (StatsManager::recordProximity) accumulates
    // on the ~30Hz position path and flushes about once a second, so the
    // evaluation this triggers never lands on that path.
    void onProximityTime(double roostSeconds);
    void onRaceLeft();
    void onLapTime(int lapTimeMs);
    void onCrash(int sessionCrashes, int crashTally);
    // Where on the centreline (0-1) that crash happened, and how many in a row
    // have now happened there: Favorite Spot. The caller keeps the run because
    // it owns the crash edge; this only takes the best one seen.
    void onCrashSpotRun(int run);
    // Riders already down around the player while the player is down too
    // (Pile-Up). Fed from the ~30Hz position path and written to cost one
    // compare there: raise() evaluates nothing unless this is a new worst.
    void onRidersDown(int down);
    // The same count taken from outside the heap (Peace Out). Same shape, same
    // cadence, same one-compare cost.
    void onRodeThrough(int down);
    // Seconds of unbroken rear wheelspin standing still (Digging a Hole). Fed
    // at most once a SECOND - the caller crosses whole seconds - so the
    // evaluation never lands on the telemetry path it is measured from.
    void onDiggingTime(double seconds);
    void onSandbag();
    // FMX (StatsManager::recordFmxTrick / FmxManager::failTrick).
    void onTrickTime(bool shred, float seconds);
    // One completed flight, fed by FmxManager at the landing whether or not a
    // trick came off. Batched by nature - a jump ends a handful of times a lap,
    // not per frame - so it evaluates on the spot.
    // A flight, at TOUCHDOWN: the three sums (Frequent Flyer, Air Miles, To the
    // Moon). Crashing after it does not take these back - see FmxManager.
    void onFlight(float seconds, float heightM, float distanceM);
    // The same flight once it has been RIDDEN AWAY FROM: the three maxima
    // (Hang Time, Gap Jumper, Sent It). Same numbers, later moment.
    void onFlightLanded(float seconds, float heightM, float distanceM);
    // The hardest hit taken, in g. Called at TELEMETRY RATE, so it is written
    // to cost a float compare and nothing else: raise() returns false unless
    // the value is a new best, and only then is the catalogue evaluated - which
    // for a lifetime peak is a handful of times ever.
    void onGForce(float g);
    void onChainCrash();
    void onChainLost(int points);   // the chain's score at the moment it broke
    // Once a second from the draw path (DrawHandler): the time sums and the
    // frame-rate flag. framesThisSecond is the Draw count since the last tick.
    void tick(bool spectating, bool rumbleLive, bool onTrack, bool moving, int framesThisSecond,
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
    // "versions" was dropped with Version Hopper; an old file still carrying it
    // simply goes unread, which is what the key-named format is for.
    static constexpr const char* kNameSets[] = { "tabs", "huds", "servers" };
    static constexpr int NAME_SET_COUNT = 3;
    const std::set<std::string>& names(int which) const;
    void restoreName(int which, const std::string& name);
    const std::string& firstRunDate() const { return m_firstRunDate; }
    int lastDay() const { return m_lastDay; }
    int crashDumpsSeen() const { return m_crashDumpsSeen; }
    int dayStreak() const { return m_dayStreak; }
    // Iron Butt's running total. PERSISTED, unlike the other per-session
    // scratch: the whole point of counting by day rather than by session is
    // that a crash must not cost the hours already ridden, and a total held
    // only in memory would be lost with the process that crashed.
    int rideDay() const { return m_rideDay; }
    double todayRideSec() const { return m_todayRideSec; }
    void restoreScalars(const std::string& firstRunDate, int lastDay, int crashDumpsSeen, int dayStreak,
                        int rideDay, double todayRideSec);
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
    bool checkSettingsEdited();                  // the settings file's hash against its trailer
    // The two settings that ARE rows: rumble switched on, and the update
    // channel set to pre-release. Read at startup and at every reload, so
    // turning either on by hand lands without a restart.
    bool checkSwitches();
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
    std::set<std::string> m_huds;          // harness ids of HUDs ever seen on
    std::set<std::string> m_servers;       // server names ridden on
    std::string m_firstRunDate;        // "YYYY-MM-DD" of the first startup ever
    int m_lastDay = 0;                 // YYYYMMDD of the last counted day (a session across midnight counts the new one)
    int m_dayStreak = 0;               // days in a row up to m_lastDay; the row keeps the best run (DayStreak)
    int m_rideDay = 0;                 // the local day m_todayRideSec belongs to (0 = none yet)
    double m_todayRideSec = 0.0;       // seconds ridden within it, for Iron Butt's max
    int m_crashDumpsSeen = -1;         // .dmp files in crashes\ at the last startup; -1 = never counted
    bool m_dirty = false;

    // The final lap read both ways - a place taken (Last Gasp) and the lead
    // lost (Choke) - from the one pair, so the two can never disagree about
    // what the last lap was. Shared by onRaceFinished() and retryLastGasp().
    bool creditLastLap(int position);

    // Per-session scratch, never persisted.
    int m_firstLapPosition = 0;
    bool m_ledEveryLap = true;
    int m_prevLapPosition = 0;         // position after the lap BEFORE the last one (Last Gasp)
    int m_lastLapPosition = 0;
    int m_lastPositionLap = 0;         // the lap the pair above describes (Last Gasp's deferral)
    int m_pendingLastGaspPosition = 0; // a finish waiting on its final lap (retryLastGasp)
    int m_pendingLastGaspLaps = 0;     // ...and the lap count it is waiting for
    bool m_holeshotArmed = false;      // a gate dropped and no opening split has been seen yet
    bool m_gotHoleshot = false;        // ...and it was the player's (Perfect Race's first quarter)
    bool m_awaitingOpeningSplit = false;   // ...and the player has not reached it yet (Charger's start)
    int m_cleanLapRun = 0;             // consecutive clean laps so far (Spotless keeps the best)
    int m_lastLaps[5] = {};
    int m_lapCount = 0;                // laps in the current Metronome window; zeroed by a hit
    std::set<int> m_sessionLapTimes;
    double m_crashFreeMs = 0.0;
    uint32_t m_overlaySeen = 0;        // the overlay's per-process connection total already counted
};
