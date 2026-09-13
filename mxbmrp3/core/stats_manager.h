// ============================================================================
// core/stats_manager.h
// Unified stats system — tracks per-track/bike stats, global race stats,
// personal bests, and odometer data in a single JSON file
// ============================================================================
#pragma once

#include "exploration_stats.h"
#include "roost_detect.h"

#include <set>
#include <string>
#include <unordered_map>
#include <ctime>
#include <chrono>
#include <cstdint>

// Per track+bike combination stats
struct TrackBikeStats {
    int validLaps = 0;                // Only laps with isValid && lapTime > 0
    int64_t totalLapTimeMs = 0;       // Sum of valid lap times (for avg calculation)
    int bestLapTimeMs = -1;           // Fastest valid lap time (-1 = none)
    int bestSector1Ms = -1;
    int bestSector2Ms = -1;
    int bestSector3Ms = -1;
    int bestSector4Ms = -1;           // GP Bikes only, -1 if N/A
    int64_t totalTimeOnTrackMs = 0;   // Accumulated riding time
    double totalDistanceM = 0.0;      // Accumulated distance in meters
    int crashCount = 0;
    int gearShiftCount = 0;           // Total gear shifts
    int penaltyCount = 0;             // Number of penalties received
    int64_t penaltyTimeMs = 0;         // Accumulated penalty time in ms
    float topSpeedMs = 0.0f;          // Highest speed in m/s
    std::time_t firstSessionTimestamp = 0;
    std::time_t lastSessionTimestamp = 0;
};

// Personal best data (nested within track+bike key)
struct StatsPersonalBestData {
    int lapTime = -1;                 // PB lap time (ms)
    int sector1 = -1;
    int sector2 = -1;
    int sector3 = -1;
    int sector4 = -1;                 // GP Bikes only, -1 if N/A
    std::string setupName;
    int conditions = -1;              // Weather conditions
    std::time_t timestamp = 0;

    bool isValid() const { return lapTime > 0; }
};

// Outcome of recording a lap into the all-time personal-best store.
//
// WHY TWO FLAGS. The store is ALWAYS keyed by track+bike (a PB belongs to the bike
// that set it), but what the user is SHOWN as their all-time reference depends on
// UiConfig's PBScope: under PBScope::CATEGORY the TimingHud "Alltime" row compares
// against the fastest lap across every bike in the class, not just the current bike.
// Those two questions have different answers, and a bare bool lets the caller
// silently use the storage answer to drive a user-facing notice: on a new bike in
// a class you already have a faster time in, the write succeeds (that bike has no
// PB yet) so the green "ALL-TIME PB" notice fires while the Alltime row shows red
// against the class best. Name both facts so a caller has to pick one.
//
// Invariant: beatsScopedBest implies stored. The category best is the minimum over
// every bike in the class INCLUDING this one, so it is never slower than this bike's
// own stored PB — beating it therefore always beats the bike's own.
struct PersonalBestUpdate {
    bool stored = false;           // the lap became this track+bike's stored PB (a write happened)
    bool beatsScopedBest = false;  // the lap beat the best in the ACTIVE PBScope (what the user is shown)
};

// Global stats aggregated across all tracks/bikes
struct GlobalStats {
    int raceCount = 0;
    int firstPositions = 0;
    int secondPositions = 0;
    int thirdPositions = 0;
    int fastestLapCount = 0;          // Times set overall fastest lap (bestFlag==2)
    int64_t penaltyTimeMs = 0;          // Accumulated penalty time in ms
    int breakoutHighScore = 0;          // Easter egg Breakout game high score
    // Achievement feeds that no per-track record carries: races finished with
    // zero session crashes (Rubber Side Down) and stored personal bests (Personal
    // Best). Counted where the event happens, persisted with the rest.
    int cleanRaceCount = 0;
    int pbCount = 0;
    int rainRaceCount = 0;      // finished with the session's weather reported Rainy
    int bigGridRaceCount = 0;   // finished on a grid of BIG_GRID_ENTRIES or more
    int maxSessionLaps = 0;     // most valid laps in one session
    int penaltyFreeStreak = 0;      // races finished in a row without a penalty (resets)
    int bestPenaltyFreeStreak = 0;  // the longest such run ever
    int pbLeaps = 0;                // personal bests beaten by a full second or more
    // THE CRASH TALLY the CrashWidget shows, and the only counter here the user
    // clears themselves.
    //
    // NOT a sum of TrackBikeStats::crashCount, though it counts the same edge: a
    // sum cannot be reset without destroying the per-track history it is summed
    // from, and reset is the whole point -- a streamer starts a session at zero
    // and their viewers watch it climb. So it runs alongside, global across every
    // track, bike and session, and survives restarts like everything else here.
    int crashTally = 0;
};

// Lifetime FMX totals, fed by FmxManager when a chain COMPLETES (the same rule
// its session totals follow: a chain ending in a crash earns nothing). Kept
// here rather than in FmxManager because this is the file they persist in.
struct FmxLifetimeStats {
    int tricksLanded = 0;
    int64_t totalScore = 0;
    int backflips = 0;
    int frontflips = 0;
    int whips = 0;
    int scrubs = 0;
    int oppos = 0;
    int turnDowns = 0;
    // Still counted though no achievement row reads it: the tally is already in
    // every player's stats file, it is the kind of number a later row or the
    // usage survey would want, and dropping it would throw that history away.
    int endos = 0;                 // endos and stoppies landed
    float longestWheelieSec = 0.0f;
    float longestEndoSec = 0.0f;   // ...and the longest one HELD, which is the row
    double wheelieDistanceM = 0.0;
    int bestChainScore = 0;
    // Trick KINDS landed at least once, by Fmx::getTrickIniKey name (a left and a
    // right whip are one kind). Names, never enum indices: the enum reorders.
    std::set<std::string> kinds;
};

// One landed trick, as FmxManager reports it: the kind name, and the shape the
// achievements read (a flip, airborne, how long, how far, on the back wheel).
struct FmxTrickSample {
    const char* kind = "";
    bool backflip = false;
    bool frontflip = false;
    bool whip = false;       // the named air tricks, left or right
    bool scrub = false;
    bool oppo = false;
    bool turnDown = false;
    bool endo = false;       // nose-down: an ENDO, and a STOPPIE, which is one held to a stop
    bool airborne = false;
    bool wheelie = false;
    bool shred = false;      // burnout, donut or drift: rear tyre time (Tyre Shredder)
    float durationSec = 0.0f;
    float distanceM = 0.0f;
};

class StatsManager {
public:
    static StatsManager& getInstance();

    // A "big grid", for the Crowd Surfer achievement: entries in the session at
    // the finish, the player included.
    static constexpr int BIG_GRID_ENTRIES = 20;

    // The exploration and hidden achievement signals, persisted in this file
    // under "exploration" (see exploration_stats.h).
    ExplorationStats& exploration() { return m_exploration; }
    const ExplorationStats& exploration() const { return m_exploration; }
    bool raceFinishRecorded() const { return m_raceFinishRecorded; }
    // RunDeinit is "bike leaves the track" (a pit stop too), so leaving a race
    // unfinished is ARMED there and counted (Rage Quit) only when the race is
    // gone for good: the session changes or the event ends. A finish disarms.
    void armRaceLeft() { m_raceLeftArmed = true; }
    static constexpr int PB_LEAP_MS = 1000;   // a PB beaten by this much is a Leap Forward

    // Lifecycle
    void load(const char* savePath);
    void save();

    // ========================================================================
    // Recording (called from handlers)
    // ========================================================================
    void recordLap(int lapTime, int sector1, int sector2, int sector3, int sector4,
                   bool isValid, bool isFastestLap, bool isRace);
    void recordSessionStart(int sessionType);
    // The gate physically dropping: a race is STARTING. The only signal that
    // separates a restart from a pit stop, both of which re-enter the same
    // session type - so the per-race latches are cleared here rather than in
    // recordSessionStart(), which must keep them across a pit visit. Without it
    // a restarted race recorded NOTHING: the finish latch from the abandoned
    // attempt was still set, so no race count, no win, no podium, no clean race.
    void onRaceStart(int starters);   // riders that took the start, per the classification that dropped the gate
    void recordSessionEnd();
    void notifyPause();
    void notifyResume();
    // `final` = this is the last chance (RunDeinit / shutdown). The race itself is
    // latched by whichever call first sees a classified finish, normally the
    // classification path; what that path can miss is the finishing MARGIN (the
    // lap logs may still be filling) and the last-lap pass (the player's final
    // RaceLap may not have arrived). Those two are owed, remembered, and settled
    // by the `final` pass - the race is never held back for them.
    void tryRecordRaceFinish(const class PluginData& pd, bool final = false);
    // Photo Finish / So Close, retried once when the lap logs they are read from
    // were still filling as the classification settled. Called by the `final`
    // pass above, beside ExplorationStats::retryLastGasp(); see its definition.
    void retryFinishMargin(const class PluginData& pd);
    void clearPlayerFastestLap();   // Called when another rider sets a faster lap
    void recordPenalty(int penaltyTimeMs, bool isRace);
    // FMX: every trick of a completed chain, then the chain's banked score.
    void recordFmxTrick(const FmxTrickSample& trick);
    void recordFmxChainBanked(int chainScore);

    // Combined per-frame telemetry update — handles distance, top speed, crash,
    // gear shift and fuel-burn detection.
    // isCrashed uses edge detection (only counts transitions), and so does the
    // tank emptying; fuelLitres is the raw tank level, integrated the way the
    // odometer integrates speed.
    //
    // rearWheelSpeedMs is the DRIVEN wheel's surface speed WHILE IT IS ON THE
    // GROUND, or < 0 when it is in the air or this vehicle has no rear wheel the
    // plugin can name (the caller knows the layout and the contact, this does
    // not): Digging a Hole is the gap between it and the speed the bike is
    // actually making, and a wheel spinning in mid-air is not digging. trackPos is the player's place on the
    // centreline, 0-1, which Favorite Spot reads at the crash edge.
    void updateTelemetry(float speedMs, bool isCrashed, int currentGear, float fuelLitres,
                         float rearWheelSpeedMs, float trackPos);

    // The bike's tank capacity, from EventInit (event_handler) — what "almost
    // empty" is measured against. 0 while unknown, which reads as "cannot be
    // known" rather than "empty".
    void setTankCapacity(float litres);

    // Roost / Side by Side, one classified contact per position batch
    // (PluginData::updateProximity does the geometry). measuring=false ends the
    // interval instead of crediting it — off track, crashed, or nobody to race.
    // Seconds accrue against the same injectable clock the odometer integrates
    // on, and are flushed to the exploration sums about once a second so the
    // achievement evaluation never lands on the ~30Hz position path.
    void recordProximity(Roost::Contact contact, bool measuring);

    // Pile-Up: how many OTHER riders are down within a few metres while the
    // player is down too (PluginData::updateProximity counts them, on the one
    // pass it already walks). Only called while the player is crashed, so it
    // needs no edge of its own -- the row is a lifetime worst, and a worst can
    // only be set while you are on the floor.
    void recordRidersDown(int down);

    // Peace Out: the same count, taken while the player is UPRIGHT and riding
    // past it. Two guards this side, both stated at their constants: a REAL
    // riding speed (not the roost rows' "not parked" floor, which a rider
    // paddling through a heap clears), and a spell after the player's own crash
    // ends, so climbing out of your own pile-up is not riding through one.
    void recordRodeThrough(int down);

    // ========================================================================
    // Context (set once per event, avoids lookups at telemetry rate)
    // ========================================================================
    // trackName is the game's DISPLAY name for trackId (SPluginsBikeEvent_t's
    // m_szTrackName). The records are keyed by ID, which is what the game gives
    // the PB store, so the name is learned here and kept in its own table -- see
    // getTrackDisplayName().
    void setCurrentContext(const std::string& trackId, const std::string& bikeName,
                           const std::string& category = "",
                           const std::string& trackName = "");
    void clearCurrentContext();

    // ========================================================================
    // Query — current track+bike
    // ========================================================================
    const TrackBikeStats* getTrackBikeStats() const;
    int64_t getCurrentTotalTimeOnTrackMs() const;  // Persisted + live session time
    double getCurrentTotalDistanceM() const;       // Persisted + live session distance
    const StatsPersonalBestData* getPersonalBest(std::string* outBikeName = nullptr) const;
    PersonalBestUpdate updatePersonalBest(const StatsPersonalBestData& entry);

    // Query — by explicit track+bike
    const StatsPersonalBestData* getPersonalBest(const std::string& trackId, const std::string& bikeName,
                                                  std::string* outBikeName = nullptr) const;
    PersonalBestUpdate updatePersonalBest(const std::string& trackId, const std::string& bikeName,
                                          const StatsPersonalBestData& entry);

    // ========================================================================
    // Query — session (for HUD)
    // ========================================================================
    int getSessionLaps() const;
    int getSessionBestLapMs() const;
    int getSessionCrashes() const;
    float getSessionTopSpeedMs() const;
    int getSessionGearShifts() const;
    int getSessionPenaltyCount() const;
    int64_t getSessionPenaltyTimeMs() const;
    double getSessionTripDistance() const;
    int64_t getSessionDurationMs() const;

    // ========================================================================
    // Query — current lap in progress (live, accumulating)
    // ========================================================================
    int getCurrentLapCrashes() const;
    int getCurrentLapGearShifts() const;
    float getCurrentLapTopSpeedMs() const;
    int getCurrentLapPenaltyCount() const;
    int64_t getCurrentLapPenaltyTimeMs() const;
    double getCurrentLapDistance() const;
    int64_t getCurrentLapElapsedMs() const;

    // ========================================================================
    // Query — last completed lap (for HUD)
    // ========================================================================
    int getLastLapTimeMs() const;
    int getLastLapCrashes() const;
    int getLastLapGearShifts() const;
    float getLastLapTopSpeedMs() const;
    int getLastLapPenaltyCount() const;
    int64_t getLastLapPenaltyTimeMs() const;
    double getLastLapDistance() const;
    bool hasLastLapData() const;

    // ========================================================================
    // Query — global
    // ========================================================================
    GlobalStats getGlobalStats() const;
    void updateBreakoutHighScore(int score);
    double getOdometerForBike(const std::string& bikeName) const;
    double getOdometerForCurrentBike() const;
    double getTotalOdometer() const;
    int getGlobalTotalLaps() const;
    int64_t getGlobalTotalTimeMs() const;      // Includes live session time
    // True if the bike moved at any point since the last call, and clears the
    // flag. The exploration tick's gate for the rows that say RIDE: parked on
    // track is time on track, which Seat Time counts, but it is not riding.
    bool consumeMoved() { const bool m = m_movedSinceTick; m_movedSinceTick = false; return m; }
    int getGlobalTotalCrashes() const;
    // The resettable tally (see GlobalStats::crashTally) -- distinct from
    // getGlobalTotalCrashes(), which sums the per-track+bike history.
    int getCrashTally() const { return m_globalStats.crashTally; }
    void resetCrashTally();
    int getGlobalTotalGearShifts() const;
    int getGlobalTotalPenalties() const;
    int64_t getGlobalTotalPenaltyTimeMs() const;
    // The most laps at any one track (every bike counted) and the longest single
    // bike odometer, in metres: the Local Hero / Loyal achievements. On the
    // same cache as the totals, since a lap or a metre moves them.
    int getMaxLapsAtOneTrack() const;
    double getMaxOdometerOnOneBike() const;
    // Distinct tracks / bikes ever ridden (the Globetrotter / Collector
    // achievements). Cached; recomputed only when a context is set or an entry
    // is cleared, so the per-event achievement evaluation never walks the maps.
    int getDistinctTrackCount() const;
    int getDistinctBikeCount() const;
    int getDistinctBikeClassCount() const;   // categories the ridden bikes span (Class Act)

    // WHICH track and bike the "at one track" / "one bike" rows are currently
    // being carried by (Local Hero, Loyal): the row asks for a number, and the
    // number alone does not say which of forty tracks it came from. Empty when
    // nothing has been ridden. Recomputed with the maxima they belong to, so
    // reading them costs a cached lookup, not a walk.
    const std::string& getMaxLapsTrackId() const;
    const std::string& getMaxOdometerBikeName() const;

    // A track's display name, or the id itself when no name has been learned --
    // a record written before names were kept, or by a build that never saw the
    // track. Never empty for a track that has records.
    std::string getTrackDisplayName(const std::string& trackId) const;
    const FmxLifetimeStats& getFmxLifetime() const { return m_fmx; }

    // ========================================================================
    // Clear
    // ========================================================================
    bool clearEntry(const std::string& trackId, const std::string& bikeName);
    void clearAll();

    // ========================================================================
    // Prestige
    // ========================================================================
    // How many times the ladder has been traded in. Survives the trade (it is
    // what the trade produces) and survives clearAll(), which is a support
    // action -- "wipe my stats" is not "I never did this".
    int getPrestige() const { return m_prestige; }
    void setPrestige(int n) { m_prestige = n < 0 ? 0 : n; }

    // Trade every achievement and the lifetime counters they read for one
    // prestige level. PERSONAL BESTS SURVIVE: a lap time is a record of what
    // the player did on a track, not a rung on the ladder, and burning those
    // would make the trade cost more than it says. Everything an achievement
    // can read goes -- see the metric list in achievement_manager.cpp.
    //
    // Refuses (returns false, changes nothing) unless the Platinum Sweep is
    // earned: the button that calls this is only drawn then, and a second gate
    // here is what makes that a rule rather than a UI detail.
    bool prestige();

    // Current context accessors (for settings tab display)
    std::string getCurrentTrackId() const;
    std::string getCurrentBikeName() const;

#if defined(MXBMRP3_TEST_BUILD)
    // Test-only odometer seam. Distance is integrated over the WALL-CLOCK gap
    // between telemetry calls (odometerNow below), so a headless test firing
    // callbacks back-to-back would accumulate ~nothing — the injectable clock
    // makes each tick's dt exact and the expected distance deterministic
    // (mirrors DirectorManager::testSetNowMs). The white-box reads expose the
    // ~100m dirty-coalescing (m_dirty / m_unsavedDistance are never observable
    // through the file: a save only happens off-track). Never in a shipping DLL.
    static void testSetNowUs(long long us);   // µs on the steady_clock timeline; -1 = real clock
    bool testIsDirty() const { return m_dirty; }
    double testUnsavedDistance() const { return m_unsavedDistance; }
#endif

private:
    StatsManager() = default;
    ~StatsManager() = default;
    StatsManager(const StatsManager&) = delete;
    StatsManager& operator=(const StatsManager&) = delete;

    static std::string makeKey(const std::string& trackId, const std::string& bikeName);

    // Everything clearAll() and prestige() have in common: the records, the
    // counters, the achievements and every transient, with NO save. The caller
    // owns the write, so a partial wipe is never on disk. keepLapRecords spares
    // the personal bests, the track-name table and the bike->class map -- the
    // one difference between the two, and the reason this is a parameter
    // rather than two copies. Those three are FACTS (a lap time, a track's
    // name, which class a bike is in), not progress; the map is in that set
    // because the PB store is keyed per bike and the default scope needs it to
    // read a class best back out. See prestige().
    void wipe(bool keepLapRecords);
    const std::string& getFilePath() const;
    void migrateOldFiles();

    // The odometer's wall clock. Only the distance integration in
    // updateTelemetry() reads time through this (the session/pause timers keep
    // the plain steady_clock — they're wall-clock by contract and never
    // asserted to exact values); in a shipping build it IS steady_clock::now().
    static std::chrono::steady_clock::time_point odometerNow();

    // Category-scoped PB lookup (scans all bikes in the same category on a track)
    const StatsPersonalBestData* getPersonalBestForCategory(const std::string& trackId,
                                                             const std::string& category,
                                                             std::string* outBikeName = nullptr) const;

    // Cached file path (resolved once in load(), avoids repeated CreateDirectoryA calls)
    mutable std::string m_cachedFilePath;
    mutable bool m_directoryEnsured = false;

    // Data storage
    std::unordered_map<std::string, TrackBikeStats> m_trackBikeStats;
    std::unordered_map<std::string, StatsPersonalBestData> m_personalBests;
    std::unordered_map<std::string, double> m_bikeOdometers;
    GlobalStats m_globalStats;
    FmxLifetimeStats m_fmx;

    // Current context (cached for telemetry-rate calls)
    std::string m_currentTrackId;
    std::string m_currentBikeName;
    std::string m_currentKey;
    std::string m_currentCategory;

    // Bike-to-category mapping (persisted for category-scoped PB lookups)
    std::unordered_map<std::string, std::string> m_bikeCategories;

    // trackId -> the game's display name for it. Learned at setCurrentContext
    // and kept for good: it is the only way a row can name a track the player
    // is not currently on. Survives prestige (it is not progress).
    std::unordered_map<std::string, std::string> m_trackNames;

    // Prestige levels taken. See prestige().
    int m_prestige = 0;

    // Cached category PB (needed because getPersonalBest returns a pointer to synthesized data)
    mutable StatsPersonalBestData m_cachedCategoryPB;

    // Session-only transients (not persisted)
    int m_sessionLaps = 0;              // Valid laps only (matches TrackBikeStats::validLaps semantics)
    int m_sessionBestLapMs = -1;
    int m_sessionCrashes = 0;
    int m_sessionGearShifts = 0;
    float m_sessionTopSpeedMs = 0.0f;
    int m_sessionPenaltyCount = 0;
    int64_t m_sessionPenaltyTimeMs = 0;
    double m_sessionTripDistance = 0.0;
    int64_t m_cachedSessionDurationMs = 0;  // Cached at session end for HUD display
    std::chrono::steady_clock::time_point m_sessionStartTime;
    std::chrono::steady_clock::time_point m_pauseStartTime;
    int64_t m_totalPausedMs = 0;           // Accumulated pause time within current session
    bool m_isPaused = false;
    int m_lastSessionType = -1;           // Track session type to avoid resetting on pit stops
    bool m_sessionActive = false;
    bool m_wasCrashed = false;
    bool m_movedSinceTick = false;   // see consumeMoved(): the RIDE rows' gate
    int m_lastGear = -1;              // Previous gear for shift edge detection (-1 = uninitialized)
    bool m_raceFinishRecorded = false;
    // 0, or the finishing position whose MARGIN could not be read at the flag --
    // see retryFinishMargin. Reset wherever m_raceFinishRecorded is.
    int m_pendingMarginPosition = 0;
    bool m_raceLeftArmed = false;
    void consumeRaceLeft();
    ExplorationStats m_exploration;
    bool m_playerHasFastestLapInRace = false;

    // Per-lap transients — accumulate during current lap, snapshot to "last" on lap completion
    std::chrono::steady_clock::time_point m_curLapStartTime;
    int64_t m_curLapPausedMs = 0;       // Accumulated pause time within current lap
    int m_curLapCrashes = 0;
    int m_curLapGearShifts = 0;
    float m_curLapTopSpeedMs = 0.0f;
    int m_curLapPenaltyCount = 0;
    int64_t m_curLapPenaltyTimeMs = 0;
    double m_curLapDistance = 0.0;

    // Last completed lap snapshot (displayed in HUD "Lap" column)
    int m_lastLapTimeMs = -1;
    int m_lastLapCrashes = 0;
    int m_lastLapGearShifts = 0;
    float m_lastLapTopSpeedMs = 0.0f;
    int m_lastLapPenaltyCount = 0;
    int64_t m_lastLapPenaltyTimeMs = 0;
    double m_lastLapDistance = 0.0;
    bool m_hasLastLapData = false;

    // Odometer time tracking
    std::chrono::steady_clock::time_point m_lastOdometerUpdateTime;
    bool m_hasLastOdometerUpdateTime = false;
    double m_unsavedDistance = 0.0;           // Accumulated distance since last dirty mark
    // Fuel. m_lastFuel is the previous tick's tank level, so the burn is its
    // fall; m_unflushedFuelL rides the odometer's ~100m coalescing rather than
    // feeding the exploration sum (and with it an evaluation) at 100Hz. The
    // reference is dropped at every track entry and every new bike, because the
    // level either side of a pit stop is two different tanks.
    // Proximity. The pending pair is what has been measured since the last
    // flush; m_proximitySinceFlushSec is the wall time it covers.
    double m_roostPendingSec = 0.0;
    double m_proximitySinceFlushSec = 0.0;
    std::chrono::steady_clock::time_point m_lastProximityTime{};
    bool m_hasLastProximityTime = false;

    // The last speed telemetry reported. Roost reads it: the row says RIDE, and
    // nothing on the position path knows whether the bike is moving (the other
    // riders' speeds are not in a position batch at all - only the player's is
    // knowable, and it is the half that matters).
    float m_lastSpeedMs = 0.0f;

    // Digging a Hole: an UNBROKEN run of rear wheelspin standing still, and the
    // whole seconds of it already reported. The run is its own clock rather
    // than the odometer's, which only ticks while the bike is moving -- the
    // opposite of what this measures.
    std::chrono::steady_clock::time_point m_lastDigTime{};
    bool m_hasLastDigTime = false;
    double m_digSec = 0.0;
    double m_digReportedSec = 0.0;

    // Favorite Spot: where on the centreline the last crash happened, and how
    // many in a ROW have now happened there. Dropped at every session start,
    // because 0.5 of one track is not 0.5 of the next.
    float m_lastCrashTrackPos = -1.0f;
    int m_sameSpotCrashRun = 0;

    // Peace Out: when the player was last seen DOWN, stamped on the position
    // path (recordRidersDown) rather than off the telemetry crash edge, which
    // trails it by a batch. The row is about riding through someone else's
    // incident, and without this a remount inside your own credits it alongside
    // Pile-Up, which is the pair's whole distinction.
    std::chrono::steady_clock::time_point m_lastSeenDownTime{};
    bool m_hasSeenDownTime = false;

    float m_tankCapacityL = 0.0f;
    float m_lastFuel = 0.0f;
    bool m_hasLastFuel = false;
    double m_unflushedFuelL = 0.0;

    // Cached global totals. Rare mutations (a lap, a penalty, a session end,
    // load/clear) set the dirty flag and the next read recomputes over every
    // track+bike record; the three TELEMETRY-RATE mutations (crash edge, gear
    // shift, the odometer's per-tick distance) instead bump the cache in place
    // while it is clean, so a gear shift never costs the O(records) walk -- which
    // every achievement evaluation, not only a visible Stats HUD, would otherwise
    // trigger. While dirty they leave it alone: the pending recompute covers them.
    mutable int m_cachedTotalLaps = 0;
    mutable int64_t m_cachedTotalTimeMs = 0;
    mutable int m_cachedTotalCrashes = 0;
    mutable int m_cachedTotalGearShifts = 0;
    mutable int m_cachedTotalPenalties = 0;
    mutable int64_t m_cachedTotalPenaltyTimeMs = 0;
    mutable double m_cachedTotalOdometer = 0.0;
    mutable int m_cachedMaxTrackLaps = 0;
    mutable double m_cachedMaxBikeOdometer = 0.0;
    // Who holds those two maxima. Ties go to the first key the unordered walk
    // reaches, which is arbitrary but stable for a given map -- the number is
    // the same either way, and a row naming either holder is telling the truth.
    mutable std::string m_cachedMaxTrackId;
    mutable std::string m_cachedMaxBikeName;
    mutable bool m_globalTotalsDirty = true;

    void recomputeGlobalTotals() const;

    // Distinct track/bike counts. Separate flag from m_globalTotalsDirty: that one
    // flips on every gear shift, and these only change when a KEY appears or goes.
    mutable int m_cachedDistinctTracks = 0;
    mutable int m_cachedDistinctBikes = 0;
    mutable int m_cachedDistinctBikeClasses = 0;
    mutable bool m_distinctDirty = true;
    void recomputeDistinctCounts() const;

    // Persistence
    std::string m_savePath;
    bool m_dirty = false;

    static constexpr int FILE_VERSION = 1;  // Bump only for additive changes (new fields read with defaults); a non-additive change (rename/retype/repurpose) requires a deliberate migration step on load.
};
