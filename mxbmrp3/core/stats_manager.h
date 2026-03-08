// ============================================================================
// core/stats_manager.h
// Unified stats system — tracks per-track/bike stats, global race stats,
// personal bests, and odometer data in a single JSON file
// ============================================================================
#pragma once

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

// Global stats aggregated across all tracks/bikes
struct GlobalStats {
    int raceCount = 0;
    int firstPositions = 0;
    int secondPositions = 0;
    int thirdPositions = 0;
    int fastestLapCount = 0;          // Times set overall fastest lap (bestFlag==2)
    int64_t penaltyTimeMs = 0;          // Accumulated penalty time in ms
    int breakoutHighScore = 0;          // Easter egg Breakout game high score
};

class StatsManager {
public:
    static StatsManager& getInstance();

    // Lifecycle
    void load(const char* savePath);
    void save();

    // ========================================================================
    // Recording (called from handlers)
    // ========================================================================
    void recordLap(int lapTime, int sector1, int sector2, int sector3, int sector4,
                   bool isValid, bool isFastestLap, bool isRace);
    void recordSessionStart(int sessionType);
    void recordSessionEnd();
    void notifyPause();
    void notifyResume();
    void tryRecordRaceFinish(const class PluginData& pd);
    void clearPlayerFastestLap();   // Called when another rider sets a faster lap
    void recordPenalty();
    void updatePenaltyFromStandings(int64_t currentTotalPenaltyMs, bool isRace);

    // Combined per-frame telemetry update — handles distance, top speed, crash and gear shift detection.
    // isCrashed uses edge detection (only counts transitions).
    void updateTelemetry(float speedMs, bool isCrashed, int currentGear);

    // ========================================================================
    // Context (set once per event, avoids lookups at telemetry rate)
    // ========================================================================
    void setCurrentContext(const std::string& trackId, const std::string& bikeName);
    void clearCurrentContext();

    // ========================================================================
    // Query — current track+bike
    // ========================================================================
    const TrackBikeStats* getTrackBikeStats() const;
    int64_t getCurrentTotalTimeOnTrackMs() const;  // Persisted + live session time
    double getCurrentTotalDistanceM() const;       // Persisted + live session distance
    const StatsPersonalBestData* getPersonalBest() const;
    bool updatePersonalBest(const StatsPersonalBestData& entry);

    // Query — by explicit track+bike
    const StatsPersonalBestData* getPersonalBest(const std::string& trackId, const std::string& bikeName) const;
    bool updatePersonalBest(const std::string& trackId, const std::string& bikeName,
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
    int getGlobalTotalCrashes() const;
    int getGlobalTotalGearShifts() const;
    int getGlobalTotalPenalties() const;
    int64_t getGlobalTotalPenaltyTimeMs() const;

    // ========================================================================
    // Clear
    // ========================================================================
    bool clearEntry(const std::string& trackId, const std::string& bikeName);
    void clearAll();

    // Current context accessors (for settings tab display)
    std::string getCurrentTrackId() const;
    std::string getCurrentBikeName() const;

private:
    StatsManager() = default;
    ~StatsManager() = default;
    StatsManager(const StatsManager&) = delete;
    StatsManager& operator=(const StatsManager&) = delete;

    static std::string makeKey(const std::string& trackId, const std::string& bikeName);
    const std::string& getFilePath() const;
    void migrateOldFiles();

    // Cached file path (resolved once in load(), avoids repeated CreateDirectoryA calls)
    mutable std::string m_cachedFilePath;
    mutable bool m_directoryEnsured = false;

    // Data storage
    std::unordered_map<std::string, TrackBikeStats> m_trackBikeStats;
    std::unordered_map<std::string, StatsPersonalBestData> m_personalBests;
    std::unordered_map<std::string, double> m_bikeOdometers;
    GlobalStats m_globalStats;

    // Current context (cached for telemetry-rate calls)
    std::string m_currentTrackId;
    std::string m_currentBikeName;
    std::string m_currentKey;

    // Session-only transients (not persisted)
    int m_sessionLaps = 0;              // Valid laps only (matches TrackBikeStats::validLaps semantics)
    int m_sessionBestLapMs = -1;
    int m_sessionCrashes = 0;
    int m_sessionGearShifts = 0;
    float m_sessionTopSpeedMs = 0.0f;
    int m_sessionPenaltyCount = 0;
    int64_t m_sessionPenaltyTimeMs = 0;
    int64_t m_lastKnownStandingsPenaltyMs = 0;  // Tracks standings penalty total for delta detection
    double m_sessionTripDistance = 0.0;
    int64_t m_cachedSessionDurationMs = 0;  // Cached at session end for HUD display
    std::chrono::steady_clock::time_point m_sessionStartTime;
    std::chrono::steady_clock::time_point m_pauseStartTime;
    int64_t m_totalPausedMs = 0;           // Accumulated pause time within current session
    bool m_isPaused = false;
    int m_lastSessionType = -1;           // Track session type to avoid resetting on pit stops
    bool m_sessionActive = false;
    bool m_wasCrashed = false;
    int m_lastGear = -1;              // Previous gear for shift edge detection (-1 = uninitialized)
    bool m_raceFinishRecorded = false;
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

    // Cached global totals (updated incrementally, recomputed on load/clear)
    mutable int m_cachedTotalLaps = 0;
    mutable int64_t m_cachedTotalTimeMs = 0;
    mutable int m_cachedTotalCrashes = 0;
    mutable int m_cachedTotalGearShifts = 0;
    mutable int m_cachedTotalPenalties = 0;
    mutable int64_t m_cachedTotalPenaltyTimeMs = 0;
    mutable double m_cachedTotalOdometer = 0.0;
    mutable bool m_globalTotalsDirty = true;

    void recomputeGlobalTotals() const;

    // Persistence
    std::string m_savePath;
    bool m_dirty = false;

    static constexpr int FILE_VERSION = 1;
};
