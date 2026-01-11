// ============================================================================
// core/plugin_data.h
// Central data store for all game state received from MX Bikes API
// ============================================================================
#pragma once

#include <cstdint>
#include <unordered_map>
#include <map>
#include <vector>
#include <array>
#include <deque>
#include <cstring>
#include <chrono>

#include "../vendor/piboso/mxb_api.h"  // For SPluginsRaceClassificationEntry_t
#include "plugin_constants.h"  // For Placeholders namespace

// Forward declarations
struct XInputData;
class XInputReader;

// Data structure for race session and event information
struct SessionData {
    // Event data
    char riderName[100];
    char bikeName[100];
    char category[100];
    char trackId[100];      // Short track identifier (e.g., "club")
    char trackName[100];    // Full track name (e.g., "Club MX")
    float trackLength;      // meters
    int eventType;

    // Bike setup data
    int shiftRPM;           // RPM threshold for shift warning (recommended shift point)
    int limiterRPM;         // RPM limiter threshold
    float steerLock;        // Maximum steering angle in degrees

    // Session data
    int session;
    int sessionState;
    int sessionLength;      // milliseconds
    int sessionNumLaps;
    int conditions;
    float airTemperature;
    char setupFileName[100];

    // Overtime tracking for time+laps races
    bool overtimeStarted;   // True when sessionTime goes negative
    int finishLap;          // Lap number riders need to complete to finish (leaderLapAtOvertime + sessionNumLaps)
    int lastSessionTime;    // Previous sessionTime value for detecting overtime transition
    int leaderFinishTime;   // Leader's total race time in milliseconds (-1 if not finished)

    SessionData() : trackLength(0.0f), eventType(2), shiftRPM(13500), limiterRPM(14000), steerLock(30.0f),
        session(-1), sessionState(-1), sessionLength(-1), sessionNumLaps(-1),
        conditions(-1), airTemperature(-1.0f), overtimeStarted(false), finishLap(-1), lastSessionTime(0), leaderFinishTime(-1) {
        riderName[0] = '\0';
        bikeName[0] = '\0';
        category[0] = '\0';
        trackId[0] = '\0';
        trackName[0] = '\0';
        setupFileName[0] = '\0';
    }

    void clear() {
        riderName[0] = '\0';
        bikeName[0] = '\0';
        category[0] = '\0';
        trackId[0] = '\0';
        trackName[0] = '\0';
        trackLength = 0.0f;
        eventType = 2;  // Default to Race (Testing events are offline-only)
        shiftRPM = 13500;  // Default fallback value
        limiterRPM = 14000;  // Default fallback value
        steerLock = 30.0f;  // Default fallback value
        session = -1;
        sessionState = -1;
        sessionLength = -1;
        sessionNumLaps = -1;
        conditions = -1;
        airTemperature = -1.0f;
        setupFileName[0] = '\0';
        overtimeStarted = false;
        finishLap = -1;
        lastSessionTime = 0;
        leaderFinishTime = -1;
    }

    // Race finish detection helpers
    // For timed+laps races: numLaps is current lap, finishLap set during overtime
    // For pure lap races: numLaps = completed laps, use sessionNumLaps directly
    bool isRiderFinished(int numLaps) const {
        if (sessionLength > 0 && sessionNumLaps > 0) {
            // Timed+laps race
            return finishLap > 0 && numLaps > finishLap;
        }
        // Pure lap or pure time race
        return (finishLap > 0 && numLaps > finishLap) ||
               (sessionNumLaps > 0 && finishLap <= 0 && numLaps >= sessionNumLaps);
    }

    bool isRiderOnLastLap(int numLaps) const {
        if (sessionLength > 0 && sessionNumLaps > 0) {
            // Timed+laps race
            return finishLap > 0 && numLaps == finishLap;
        }
        // Pure lap race: last lap when completed = total - 1
        return sessionNumLaps > 0 && numLaps == sessionNumLaps - 1;
    }
};

// Race entry data for tracking riders/vehicles in race events
struct RaceEntryData {
    int raceNum;
    char name[100];
    char bikeName[100];
    const char* bikeAbbr;        // Cached bike abbreviation (points to static string)
    unsigned long bikeBrandColor; // Cached bike brand color
    char formattedRaceNum[8];    // Pre-formatted race number "#999"
    char truncatedName[4];       // Pre-truncated rider name (max 3 chars)

    RaceEntryData() : raceNum(-1), bikeAbbr(nullptr), bikeBrandColor(0) {
        name[0] = '\0';
        bikeName[0] = '\0';
        formattedRaceNum[0] = '\0';
        truncatedName[0] = '\0';
    }

    RaceEntryData(int num, const char* riderName, const char* bike, const char* abbr, unsigned long brandColor)
        : raceNum(num), bikeAbbr(abbr), bikeBrandColor(brandColor) {
        // Copy name
        strncpy_s(name, sizeof(name), riderName, sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';

        // Copy bike name
        strncpy_s(bikeName, sizeof(bikeName), bike, sizeof(bikeName) - 1);
        bikeName[sizeof(bikeName) - 1] = '\0';

        // Pre-format race number
        snprintf(formattedRaceNum, sizeof(formattedRaceNum), "#%d", raceNum);

        // Pre-truncate name (max 3 chars)
        size_t nameLen = strlen(riderName);
        if (nameLen > 3) nameLen = 3;
        memcpy(truncatedName, riderName, nameLen);
        truncatedName[nameLen] = '\0';
    }
};

// Standings data for race classification (current race position)
struct StandingsData {
    int raceNum;
    int state;          // 1 = DNS, 2 = retired, 3 = DSQ
    int bestLap;        // milliseconds
    int bestLapNum;     // best lap index
    int numLaps;        // number of laps completed
    int gap;            // gap to leader in milliseconds (official from splits)
    int gapLaps;        // gap to leader in laps
    int realTimeGap;    // real-time estimated gap in milliseconds
    int penalty;        // penalty time in milliseconds
    int pit;            // 0 = on track, 1 = in pits
    int finishTime;     // total race time in milliseconds (-1 if not finished)

    StandingsData() : raceNum(-1), state(0), bestLap(-1), bestLapNum(-1),
        numLaps(0), gap(0), gapLaps(0), realTimeGap(0), penalty(0), pit(0), finishTime(-1) {
    }

    StandingsData(int num, int st, int bLap, int bLapNum, int nLaps,
        int g, int gLaps, int pen, int p)
        : raceNum(num), state(st), bestLap(bLap), bestLapNum(bLapNum),
        numLaps(nLaps), gap(g), gapLaps(gLaps), realTimeGap(0), penalty(pen), pit(p), finishTime(-1) {
    }
};

// Real-time track position data for gap calculation
struct TrackPositionData {
    float trackPos;       // 0.0 to 1.0 along centerline
    int numLaps;          // Current lap count for handling wraparound
    int sessionTime;      // Session time in milliseconds when this position was recorded
    bool crashed;

    // Rolling window for wrong-way detection
    static constexpr int POSITION_HISTORY_SIZE = 30;  // ~1.5 sec at 20Hz update rate
    static constexpr float WRAPAROUND_THRESHOLD = 0.5f;  // Position change > 0.5 indicates wrap through start/finish
    static constexpr float WRONG_WAY_THRESHOLD = -0.001f;  // Must move back 0.1% of track to trigger
    std::array<float, POSITION_HISTORY_SIZE> positionHistory;
    int historyIndex;     // Current write position in circular buffer
    int historyCount;     // How many positions we've stored (0 to POSITION_HISTORY_SIZE)
    bool wrongWay;        // True if rider is going backwards on track

    TrackPositionData()
        : trackPos(0.0f), numLaps(0), sessionTime(0), crashed(false)
        , historyIndex(0), historyCount(0), wrongWay(false) {
        positionHistory.fill(0.0f);
    }
};

// Leader timing point for time-based gap calculation
// Stores when leader crossed each 1% position on track
struct LeaderTimingPoint {
    int sessionTime;      // Session time in milliseconds when leader crossed this position
    int lapNum;           // Which lap this timing is from

    LeaderTimingPoint() : sessionTime(0), lapNum(-1) {}
    LeaderTimingPoint(int time, int lap) : sessionTime(time), lapNum(lap) {}
};

// Debug metrics for performance monitoring
struct DebugMetrics {
    float currentFps;       // Current frames per second
    float pluginTimeMs;     // Plugin draw time in milliseconds
    float pluginPercent;    // Plugin time as percentage of frame budget

    DebugMetrics() : currentFps(0.0f), pluginTimeMs(0.0f), pluginPercent(0.0f) {}
};

// Bike telemetry data from physics simulation
struct BikeTelemetryData {
    float speedometer;  // Ground speed in meters/second
    int gear;           // Current gear (0 = Neutral)
    int numberOfGears;  // Total number of gears (for normalization)
    int rpm;            // Engine RPM
    float fuel;         // Current fuel in liters
    float maxFuel;      // Fuel tank capacity in liters
    float frontSuspLength;      // Current front suspension length in meters
    float rearSuspLength;       // Current rear suspension length in meters
    float frontSuspMaxTravel;   // Front suspension maximum travel in meters
    float rearSuspMaxTravel;    // Rear suspension maximum travel in meters
    float roll;         // Lean angle in degrees (negative = left, positive = right)
    bool isValid;       // True if telemetry data is currently available

    BikeTelemetryData() : speedometer(0.0f), gear(0), numberOfGears(6), rpm(0), fuel(0.0f), maxFuel(0.0f),
                          frontSuspLength(0.0f), rearSuspLength(0.0f),
                          frontSuspMaxTravel(0.0f), rearSuspMaxTravel(0.0f),
                          roll(0.0f), isValid(false) {}
};

// Input telemetry data from controller/bike inputs
struct InputTelemetryData {
    // Telemetry data (processed bike inputs)
    float steer;        // Steering in degrees (negative = right)
    float throttle;     // 0 to 1
    float frontBrake;   // 0 to 1
    float rearBrake;    // 0 to 1
    float clutch;       // 0 to 1 (0 = fully engaged)

    // XInput data (raw controller inputs)
    float leftStickX;       // -1 to 1 (left stick horizontal)
    float leftStickY;       // -1 to 1 (left stick vertical)
    float rightStickX;      // -1 to 1 (rider lean left/right)
    float rightStickY;      // -1 to 1 (rider lean forward/back)
    float leftTrigger;      // 0 to 1 (left trigger)
    float rightTrigger;     // 0 to 1 (right trigger)
    bool xinputConnected;   // XInput controller connected

    InputTelemetryData() : steer(0.0f), throttle(0.0f), frontBrake(0.0f),
                           rearBrake(0.0f), clutch(0.0f),
                           leftStickX(0.0f), leftStickY(0.0f),
                           rightStickX(0.0f), rightStickY(0.0f),
                           leftTrigger(0.0f), rightTrigger(0.0f),
                           xinputConnected(false) {}
};

// History buffers for graphing telemetry and input data over time
struct HistoryBuffers {
    // Stick sample with X and Y position (used for both sticks)
    struct StickSample {
        float x;
        float y;

        StickSample() : x(0.0f), y(0.0f) {}
        StickSample(float _x, float _y) : x(_x), y(_y) {}
    };

    // History buffers (newest at back, oldest at front)
    std::deque<float> throttle;
    std::deque<float> frontBrake;
    std::deque<float> rearBrake;
    std::deque<float> clutch;
    std::deque<float> steer;
    std::deque<float> rpm;               // Engine RPM (normalized 0-1 range)
    std::deque<float> gear;              // Current gear (normalized 0-1 range, gear/numberOfGears)
    std::deque<float> frontSusp;         // Front suspension compression (normalized 0-1 range)
    std::deque<float> rearSusp;          // Rear suspension compression (normalized 0-1 range)
    std::deque<StickSample> leftStick;   // Left analog stick (steering/throttle)
    std::deque<StickSample> rightStick;  // Right analog stick (rider lean)

    // History configuration (time depends on telemetry rate set in plugin_manager.cpp)
    // At 100Hz physics rate: 200 samples = 2 seconds of data for telemetry graphs
    static constexpr size_t MAX_TELEMETRY_HISTORY = 200;
    // At 100Hz physics rate: 50 samples = 500ms of data for stick trails
    static constexpr size_t MAX_STICK_HISTORY = 50;

    // Add sample to history buffer
    void addSample(std::deque<float>& buffer, float value) {
        buffer.push_back(value);
        if (buffer.size() > MAX_TELEMETRY_HISTORY) {
            buffer.pop_front();
        }
    }

    void addStickSample(std::deque<StickSample>& buffer, float x, float y) {
        buffer.emplace_back(x, y);
        if (buffer.size() > MAX_STICK_HISTORY) {
            buffer.pop_front();
        }
    }

    void clear() {
        throttle.clear();
        frontBrake.clear();
        rearBrake.clear();
        clutch.clear();
        steer.clear();
        rpm.clear();
        gear.clear();
        frontSusp.clear();
        rearSusp.clear();
        leftStick.clear();
        rightStick.clear();
    }
};

// Current lap split data (accumulated times from race start for current lap, player-only)
struct CurrentLapData {
    int lapNum;
    int split1;     // milliseconds - accumulated time to split 1 (-1 if not crossed yet)
    int split2;     // milliseconds - accumulated time to split 2 (-1 if not crossed yet)
    int split3;     // milliseconds - accumulated time to split 3 (-1 if not crossed yet)

    CurrentLapData() : lapNum(-1), split1(-1), split2(-1), split3(-1) {}

    void clear() {
        lapNum = -1;
        split1 = -1;
        split2 = -1;
        split3 = -1;
    }
};

// Ideal lap data (best sector times and last lap time, per-rider)
struct IdealLapData {
    int lastCompletedLapNum;  // 0-indexed - last completed lap number (for detection)
    int lastLapTime;     // milliseconds - last completed lap time (0 if no timing data)
    int lastLapSector1;  // milliseconds - last completed lap sector 1 time
    int lastLapSector2;  // milliseconds - last completed lap sector 2 time
    int lastLapSector3;  // milliseconds - last completed lap sector 3 time
    int bestSector1;     // milliseconds - best sector 1 time across all laps
    int bestSector2;     // milliseconds - best sector 2 time across all laps
    int bestSector3;     // milliseconds - best sector 3 time across all laps

    // Previous PB data (for comparison when new PB is set)
    int previousBestLapTime;     // milliseconds - previous personal best lap time
    int previousBestSector1;     // milliseconds - previous PB sector 1 time
    int previousBestSector2;     // milliseconds - previous PB sector 2 time
    int previousBestSector3;     // milliseconds - previous PB sector 3 time

    // Previous ideal sector data (for comparison when new best sector is set)
    int previousIdealSector1;    // milliseconds - previous best sector 1 time
    int previousIdealSector2;    // milliseconds - previous best sector 2 time
    int previousIdealSector3;    // milliseconds - previous best sector 3 time

    IdealLapData() : lastCompletedLapNum(-1), lastLapTime(-1),
                        lastLapSector1(-1), lastLapSector2(-1), lastLapSector3(-1),
                        bestSector1(-1), bestSector2(-1), bestSector3(-1),
                        previousBestLapTime(-1), previousBestSector1(-1),
                        previousBestSector2(-1), previousBestSector3(-1),
                        previousIdealSector1(-1), previousIdealSector2(-1),
                        previousIdealSector3(-1) {}

    void clear() {
        lastCompletedLapNum = -1;
        lastLapTime = -1;
        lastLapSector1 = -1;
        lastLapSector2 = -1;
        lastLapSector3 = -1;
        bestSector1 = -1;
        bestSector2 = -1;
        bestSector3 = -1;
        previousBestLapTime = -1;
        previousBestSector1 = -1;
        previousBestSector2 = -1;
        previousBestSector3 = -1;
        previousIdealSector1 = -1;
        previousIdealSector2 = -1;
        previousIdealSector3 = -1;
    }

    // Get previous ideal lap time (sum of previous best sectors)
    int getPreviousIdealLapTime() const {
        if (previousIdealSector1 > 0 && previousIdealSector2 > 0 && previousIdealSector3 > 0) {
            return previousIdealSector1 + previousIdealSector2 + previousIdealSector3;
        }
        return -1;
    }

    // Get ideal lap time (sum of best sectors)
    int getIdealLapTime() const {
        if (bestSector1 > 0 && bestSector2 > 0 && bestSector3 > 0) {
            return bestSector1 + bestSector2 + bestSector3;
        }
        return -1;
    }
};

// Historical lap data for lap log HUD
struct LapLogEntry {
    int lapNum;       // Lap number (1-based)
    int sector1;      // milliseconds - sector 1 time
    int sector2;      // milliseconds - sector 2 time
    int sector3;      // milliseconds - sector 3 time
    int lapTime;      // milliseconds - total lap time
    bool isValid;     // false if lap was invalid
    bool isComplete;  // true if lap is completed, false if in progress

    LapLogEntry() : lapNum(-1), sector1(-1), sector2(-1), sector3(-1),
                    lapTime(-1), isValid(true), isComplete(false) {}

    LapLogEntry(int lap, int s1, int s2, int s3, int total, bool valid, bool complete)
        : lapNum(lap), sector1(s1), sector2(s2), sector3(s3),
          lapTime(total), isValid(valid), isComplete(complete) {}
};

// ============================================================================
// Centralized Lap Timer for real-time elapsed time calculation
// Used by TimingHud, IdealLapHud, and other components that need live timing
// Uses wall clock time since session time can count UP (practice) or DOWN (races)
// ============================================================================
struct LapTimer {
    // Wall clock anchor for elapsed time calculation
    std::chrono::steady_clock::time_point anchorTime;  // Real time when anchor was set
    int anchorAccumulatedTime;    // Known accumulated lap time at anchor (ms)
    bool anchorValid;             // Do we have a usable anchor?

    // Pause support
    std::chrono::steady_clock::time_point pausedAt;  // When pause started
    bool isPaused;                // Is timer currently paused?

    // Track position monitoring for S/F line detection
    float lastTrackPos;           // Previous track position (0.0-1.0)
    int lastLapNum;               // Previous lap number
    bool trackMonitorInitialized; // Have we received first position?

    // Current state
    int currentLapNum;            // Current lap being timed
    int currentSector;            // Current sector (0=before S1, 1=before S2, 2=before S3)
    int lastSplit1Time;           // Accumulated time at S1 (for sector 2 calculation)
    int lastSplit2Time;           // Accumulated time at S2 (for sector 3 calculation)

    // Threshold for S/F line detection (position jump > 0.5 = S/F crossing)
    static constexpr float WRAP_THRESHOLD = 0.5f;

    LapTimer()
        : anchorAccumulatedTime(0), anchorValid(false), isPaused(false)
        , lastTrackPos(0.0f), lastLapNum(0), trackMonitorInitialized(false)
        , currentLapNum(0), currentSector(0)
        , lastSplit1Time(-1), lastSplit2Time(-1) {}

    void reset() {
        anchorAccumulatedTime = 0;
        anchorValid = false;
        isPaused = false;
        lastTrackPos = 0.0f;
        lastLapNum = 0;
        trackMonitorInitialized = false;
        currentLapNum = 0;
        currentSector = 0;
        lastSplit1Time = -1;
        lastSplit2Time = -1;
    }

    void setAnchor(int accumulatedTime) {
        anchorTime = std::chrono::steady_clock::now();
        anchorAccumulatedTime = accumulatedTime;
        anchorValid = true;
        isPaused = false;  // Clear pause state when setting new anchor
    }

    // Pause/resume support - adjusts anchor to exclude pause duration
    void pause() {
        if (!isPaused && anchorValid) {
            pausedAt = std::chrono::steady_clock::now();
            isPaused = true;
        }
    }

    void resume() {
        if (isPaused && anchorValid) {
            // Adjust anchor forward by the pause duration so elapsed time is correct
            auto pauseDuration = std::chrono::steady_clock::now() - pausedAt;
            anchorTime += pauseDuration;
            isPaused = false;
        }
    }

    // Calculate elapsed lap time since anchor
    int getElapsedLapTime() const {
        if (!anchorValid) {
            return -1;  // No anchor - show placeholder
        }

        // Use pause time if paused, otherwise use now
        auto endTime = isPaused ? pausedAt : std::chrono::steady_clock::now();
        auto wallElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            endTime - anchorTime
        ).count();

        int elapsed = anchorAccumulatedTime + static_cast<int>(wallElapsed);

        // Sanity check - don't show negative time
        if (elapsed < 0) elapsed = 0;

        return elapsed;
    }

    // Calculate elapsed sector time
    // sectorIndex: 0=S1 (from lap start), 1=S2 (from S1), 2=S3 (from S2)
    int getElapsedSectorTime(int sectorIndex) const {
        int lapTime = getElapsedLapTime();
        if (lapTime < 0) {
            return -1;  // No valid elapsed time
        }

        switch (sectorIndex) {
            case 0:  // S1: time from lap start
                return lapTime;
            case 1:  // S2: time from S1
                if (lastSplit1Time > 0) {
                    return lapTime - lastSplit1Time;
                }
                return -1;  // S1 not crossed yet
            case 2:  // S3: time from S2
                if (lastSplit2Time > 0) {
                    return lapTime - lastSplit2Time;
                }
                return -1;  // S2 not crossed yet
            default:
                return -1;
        }
    }
};

// Data change notification types
enum class DataChangeType {
    SessionData,
    RaceEntries,
    Standings,
    DebugMetrics,
    InputTelemetry,
    IdealLap,
    LapLog,
    SpectateTarget,  // Spectate target changed (switch to different rider)
    TrackedRiders    // Tracked riders list or settings changed
};

// Helper function to convert DataChangeType to string for debugging
inline const char* dataChangeTypeToString(DataChangeType type) {
    switch (type) {
    case DataChangeType::SessionData: return "SessionData";
    case DataChangeType::RaceEntries: return "RaceEntries";
    case DataChangeType::Standings: return "Standings";
    case DataChangeType::DebugMetrics: return "DebugMetrics";
    case DataChangeType::InputTelemetry: return "InputTelemetry";
    case DataChangeType::IdealLap: return "IdealLap";
    case DataChangeType::LapLog: return "LapLog";
    case DataChangeType::SpectateTarget: return "SpectateTarget";
    case DataChangeType::TrackedRiders: return "TrackedRiders";
    default: return "Unknown";
    }
}

class PluginData {
public:
    static PluginData& getInstance();

    // SessionData field setters (called by event/session handlers)
    void setRiderName(const char* riderName);
    void setBikeName(const char* bikeName);
    void setCategory(const char* category);
    void setTrackId(const char* trackId);
    void setTrackName(const char* trackName);
    void setTrackLength(float trackLength);
    void setEventType(int eventType);
    void setShiftRPM(int shiftRPM);
    void setLimiterRPM(int limiterRPM);
    void setSteerLock(float steerLock);
    void setMaxFuel(float maxFuel);
    void setNumberOfGears(int numberOfGears);
    void setSession(int session);
    void setSessionState(int sessionState);
    void setSessionLength(int sessionLength);
    void setSessionNumLaps(int sessionNumLaps);
    void setConditions(int conditions);
    void setAirTemperature(float airTemperature);
    void setSetupFileName(const char* setupFileName);

    // Race entry management
    void addRaceEntry(int raceNum, const char* name, const char* bikeName);
    void removeRaceEntry(int raceNum);  // Also cleans up all per-rider data for this race number
    const std::unordered_map<int, RaceEntryData>& getRaceEntries() const { return m_raceEntries; }  // Collection (never null)
    const RaceEntryData* getRaceEntry(int raceNum) const;  // Per-rider (nullable)

    // Player race number with lazy evaluation (const-correct)
    int getPlayerRaceNum() const;
    void setPlayerRaceNum(int raceNum);  // Directly set player's race number (avoids name-based lookup)

    // Player entry detection (first RaceAddEntry with unactive=0 after EventInit is the player)
    void setWaitingForPlayerEntry(bool waiting) { m_bWaitingForPlayerEntry = waiting; }
    bool isWaitingForPlayerEntry() const { return m_bWaitingForPlayerEntry; }

    // Pending player entry (for spectate-first case where RaceAddEntry arrives before EventInit)
    void setPendingPlayerRaceNum(int raceNum) { m_iPendingPlayerRaceNum = raceNum; }
    int getPendingPlayerRaceNum() const { return m_iPendingPlayerRaceNum; }
    void clearPendingPlayerRaceNum() { m_iPendingPlayerRaceNum = -1; }

    // Spectate mode tracking
    void setDrawState(int state);  // Set current draw state (ON_TRACK/SPECTATE/REPLAY)
    void setSpectatedRaceNum(int raceNum);  // Set which rider is being spectated
    int getDrawState() const { return m_drawState; }  // Get current draw state
    int getDisplayRaceNum() const;  // Get race number to display (player when on track, spectated rider otherwise)

    // ========================================================================
    // Per-Rider Data Management (Ideal Lap, Lap Logs, Current Lap)
    // ========================================================================
    // API Design Pattern:
    //   - Per-rider getters return POINTERS (nullable, returns nullptr if no data for that rider)
    //   - Collection getters return REFERENCES (never null, but may be empty collections)
    //   - This allows callers to distinguish "no data" from "empty data"
    // ========================================================================

    // Current lap and ideal lap management (per-rider)
    void updateCurrentLapSplit(int raceNum, int lapNum, int splitIndex, int accumulatedTime);
    void setCurrentLapNumber(int raceNum, int lapNum);  // Initialize lap number for next lap
    void updateIdealLap(int raceNum, int completedLapNum, int lapTime, int sector1, int sector2, int sector3, bool isValid = true);
    void clearIdealLap(int raceNum);
    void clearAllIdealLap();  // Clear all riders' ideal lap data
    const CurrentLapData* getCurrentLapData(int raceNum) const;  // Returns nullptr if no data
    const IdealLapData* getIdealLapData(int raceNum) const;  // Returns nullptr if no data

    // Lap log management (per-rider, stores completed and in-progress laps)
    void updateLapLog(int raceNum, const LapLogEntry& entry);
    void clearLapLog(int raceNum);
    void clearAllLapLog();  // Clear all riders' lap log
    const std::deque<LapLogEntry>* getLapLog(int raceNum) const;  // Returns nullptr if no data

    // Best lap entry storage (per-rider, separate from lap log for easy access)
    void setBestLapEntry(int raceNum, const LapLogEntry& entry);
    const LapLogEntry* getBestLapEntry(int raceNum) const;  // Returns nullptr if no data

    // Overall best lap (fastest lap by any rider, with splits for gap comparison)
    void setOverallBestLap(const LapLogEntry& entry);
    const LapLogEntry* getOverallBestLap() const;
    const LapLogEntry* getPreviousOverallBestLap() const;
    void clearOverallBestLap() { m_overallBestLap.lapNum = -1; m_previousOverallBestLap.lapNum = -1; }

    // Convenience methods for display race number (uses getDisplayRaceNum internally)
    const CurrentLapData* getCurrentLapData() const { return getCurrentLapData(getDisplayRaceNum()); }
    const IdealLapData* getIdealLapData() const { return getIdealLapData(getDisplayRaceNum()); }
    const std::deque<LapLogEntry>* getLapLog() const { return getLapLog(getDisplayRaceNum()); }
    const LapLogEntry* getBestLapEntry() const { return getBestLapEntry(getDisplayRaceNum()); }

    // Check if display rider has finished the race (convenience helper)
    bool isDisplayRiderFinished() const;

    // ========================================================================
    // Centralized Lap Timer Management (display rider only)
    // Provides real-time elapsed lap and sector timing for HUDs
    // Tracks only the currently displayed rider (like GapBarHud pattern)
    // ========================================================================

    // Update lap timer with track position for S/F crossing detection
    // Returns true if S/F crossing was detected (anchor was set)
    bool updateLapTimerTrackPosition(int raceNum, float trackPos, int lapNum);

    // Set timer anchor when official split/lap event occurs
    // Called by handlers when splits are received
    void setLapTimerAnchor(int raceNum, int accumulatedTime, int lapNum, int sectorIndex);

    // Reset timer on new lap (called when lap completes)
    void resetLapTimerForNewLap(int raceNum, int lapNum);

    // Reset timer completely (for session change, spectate target change, pit entry)
    void resetLapTimer(int raceNum);
    void resetAllLapTimers();

    // Get elapsed times (returns -1 if no valid anchor or different rider)
    int getElapsedLapTime(int raceNum) const;
    int getElapsedSectorTime(int raceNum, int sectorIndex) const;  // sectorIndex: 0=S1, 1=S2, 2=S3

    // Check if timer has valid anchor
    bool isLapTimerValid(int raceNum) const;

    // Get current lap number being timed
    int getLapTimerCurrentLap(int raceNum) const;

    // Get current sector being timed (0=before S1, 1=before S2, 2=before S3)
    int getLapTimerCurrentSector(int raceNum) const;

    // Convenience methods for display race number
    int getElapsedLapTime() const { return getElapsedLapTime(getDisplayRaceNum()); }
    int getElapsedSectorTime(int sectorIndex) const { return getElapsedSectorTime(getDisplayRaceNum(), sectorIndex); }
    bool isLapTimerValid() const { return isLapTimerValid(getDisplayRaceNum()); }
    int getLapTimerCurrentLap() const { return getLapTimerCurrentLap(getDisplayRaceNum()); }
    int getLapTimerCurrentSector() const { return getLapTimerCurrentSector(getDisplayRaceNum()); }

    // Standings management
    void updateStandings(int raceNum, int state, int bestLap, int bestLapNum,
        int numLaps, int gap, int gapLaps, int penalty, int pit, bool notify);
    void batchUpdateStandings(SPluginsRaceClassificationEntry_t* entries, int numEntries);
    void clearStandings();
    const std::unordered_map<int, StandingsData>& getStandings() const { return m_standings; }  // Collection (never null)
    const StandingsData* getStanding(int raceNum) const;  // Per-rider (nullable)

    // Classification order (preserves the game's official race position order)
    void setClassificationOrder(const std::vector<int>& order);
    const std::vector<int>& getClassificationOrder() const { return m_classificationOrder; }

    // Position lookup - efficiently find a rider's position by race number (1-based, or -1 if not found)
    // Uses cached map that's only rebuilt when classification changes
    int getPositionForRaceNum(int raceNum) const;

    // Real-time track position management (for time-based gap calculation)
    void setSessionTime(int sessionTime) { m_currentSessionTime = sessionTime; }
    int getSessionTime() const { return m_currentSessionTime; }
    void updateTrackPosition(int raceNum, float trackPos, int numLaps, bool crashed, int sessionTime);
    void updateRealTimeGaps();  // Calculate gaps using time deltas
    void clearLiveGapTimingPoints();  // Clear timing points for new session

    // Wrong-way detection (based on track position changes)
    bool isPlayerGoingWrongWay() const;  // Check if display rider is going wrong way
    const TrackPositionData* getPlayerTrackPosition() const;  // Get display rider's track position data for debugging

    // Blue flag detection (riders 1+ laps ahead approaching from behind)
    std::vector<int> getBlueFlagRaceNums() const;  // Returns race numbers of riders to let past

    // Overtime tracking for time+laps races
    void setOvertimeStarted(bool started) { m_sessionData.overtimeStarted = started; }
    void setFinishLap(int lap) { m_sessionData.finishLap = lap; }
    void setLastSessionTime(int time) { m_sessionData.lastSessionTime = time; }
    void setLeaderFinishTime(int time) { m_sessionData.leaderFinishTime = time; }
    int getLeaderFinishTime() const { return m_sessionData.leaderFinishTime; }

    // Player running state (set by RunStart, cleared by RunStop/RunDeinit)
    void setPlayerRunning(bool running) {
        m_bPlayerIsRunning = running;
        // Pause/resume lap timer to account for game pause time
        if (running) {
            m_displayLapTimer.resume();
        } else {
            m_displayLapTimer.pause();
        }
    }
    bool isPlayerRunning() const { return m_bPlayerIsRunning; }

    // Session type checks
    bool isRaceSession() const;     // Returns true for RACE_1, RACE_2, SR sessions
    bool isQualifySession() const;  // Returns true for PRE_QUALIFY, QUALIFY_PRACTICE, QUALIFY

    // Data accessors for HUD components
    const SessionData& getSessionData() const { return m_sessionData; }
    const DebugMetrics& getDebugMetrics() const { return m_debugMetrics; }
    const BikeTelemetryData& getBikeTelemetry() const { return m_bikeTelemetry; }
    const InputTelemetryData& getInputTelemetry() const { return m_inputTelemetry; }
    const HistoryBuffers& getHistoryBuffers() const { return m_historyBuffers; }
    void clearHistoryBuffers() { m_historyBuffers.clear(); }

    // Debug metrics update
    void updateDebugMetrics(float fps, float pluginTimeMs, float pluginPercent);

    // Bike telemetry update
    void updateSpeedometer(float speedometer, int gear, int rpm, float fuel);
    void updateRoll(float roll);
    void invalidateSpeedometer();

    // Suspension update
    void updateSuspensionMaxTravel(float frontMaxTravel, float rearMaxTravel);
    void updateSuspensionLength(float frontLength, float rearLength);

    // Input telemetry update
    void updateInputTelemetry(float steer, float throttle, float frontBrake, float rearBrake, float clutch);
    void updateXInputData(const XInputData& xinputData);

    // Limited telemetry update for spectate/replay (only updates data available in SPluginsRaceVehicleData_t)
    void updateRaceVehicleTelemetry(float speedometer, int gear, int rpm, float throttle, float frontBrake, float lean);

    // Clear telemetry data (when spectate target becomes invalid)
    void clearTelemetryData();

    // Clear all data (useful for reset scenarios)
    void clear();

    // Direct notification to HudManager (no observer pattern overhead)
    // Made public for batch update optimization (call once after multiple updates)
    void notifyHudManager(DataChangeType changeType);

    // ========================================================================
    // XInputReader Access (provides single access point for controller data)
    // ========================================================================
    // Returns const reference to XInputReader singleton
    // HUDs should use this instead of accessing XInputReader::getInstance() directly
    const XInputReader& getXInputReader() const;

    // ========================================================================
    // TrackedRiders Notification
    // ========================================================================
    // Called by TrackedRidersManager when tracked riders list/settings change
    // Triggers DataChangeType::TrackedRiders notification to HUDs
    void notifyTrackedRidersChanged();

    // ========================================================================
    // Live Gap (published by GapBarHud for use by LapLogHud and other HUDs)
    // ========================================================================
    // Positive = behind PB, Negative = ahead of PB
    void setLiveGap(int gapMs, bool valid) { m_liveGapMs = gapMs; m_liveGapValid = valid; }
    int getLiveGap() const { return m_liveGapMs; }
    bool hasValidLiveGap() const { return m_liveGapValid; }

private:
    PluginData() : m_currentSessionTime(0), m_playerRaceNum(-1), m_bPlayerRaceNumValid(false),
                   m_bPlayerNotFoundWarned(false), m_bWaitingForPlayerEntry(false),
                   m_iPendingPlayerRaceNum(-1), m_bPlayerIsRunning(false), m_drawState(0),
                   m_spectatedRaceNum(-1), m_bPositionCacheDirty(true) {}
    ~PluginData() {}
    PluginData(const PluginData&) = delete;
    PluginData& operator=(const PluginData&) = delete;

    // Update cached player race number by searching race entries
    void updatePlayerRaceNum() const;

    // Template helper for setting char array values with change detection
    bool setStringValue(char* field, size_t fieldSize, const char* newValue);

    // Template helper for setting numeric values with change detection
    template<typename T>
    bool setValue(T& field, const T& newValue) {
        if (field != newValue) {
            field = newValue;
            return true;
        }
        return false;
    }

    SessionData m_sessionData;
    DebugMetrics m_debugMetrics;
    BikeTelemetryData m_bikeTelemetry;
    InputTelemetryData m_inputTelemetry;
    HistoryBuffers m_historyBuffers;
    std::unordered_map<int, RaceEntryData> m_raceEntries;
    std::unordered_map<int, StandingsData> m_standings;
    std::unordered_map<int, int> m_lastValidOfficialGap;  // Cache of last valid official gap per rider (prevents flicker)
    std::vector<int> m_classificationOrder;  // Official race position order from game
    mutable std::unordered_map<int, int> m_positionCache;  // Cached position lookup (race number -> position), rebuilt when classification changes
    mutable bool m_bPositionCacheDirty;  // Flag to rebuild position cache
    std::unordered_map<int, TrackPositionData> m_trackPositions;  // Real-time track positions
    std::unordered_map<int, CurrentLapData> m_riderCurrentLap;  // Current lap split data per rider
    std::unordered_map<int, IdealLapData> m_riderIdealLap;  // Ideal lap sectors per rider
    std::unordered_map<int, std::deque<LapLogEntry>> m_riderLapLog;  // Lap log per rider (newest first, deque for O(1) front insert)
    std::unordered_map<int, LapLogEntry> m_riderBestLap;  // Best lap entry per rider (for easy access)
    LapLogEntry m_overallBestLap;          // Overall best lap (any rider) with splits for gap comparison
    LapLogEntry m_previousOverallBestLap;  // Previous overall best (for showing improvement)

    // Single centralized lap timer for display rider only (follows GapBarHud pattern)
    // Resets when spectate target changes - no need to track all riders
    LapTimer m_displayLapTimer;
    int m_displayLapTimerRaceNum = -1;  // Which rider the timer is currently tracking

    // Leader timing points for time-based gap calculation
    // Stores when leader crossed each 1% position, indexed by lap number
    // Map key = lap number, Value = array of 100 timing points (1% resolution)
    static constexpr size_t NUM_TIMING_POINTS = 100;
    static constexpr size_t MAX_LAPS_TO_KEEP = 20;  // Keep up to 20 laps of timing data
    static constexpr int GAP_UPDATE_THRESHOLD_MS = 100;  // Minimum gap change (in ms) to trigger cache update (prevents flicker from small oscillations)
    std::map<int, std::array<LeaderTimingPoint, NUM_TIMING_POINTS>> m_leaderTimingPoints;
    int m_currentSessionTime;  // Most recent session time in milliseconds

    // Thread safety: These mutable cache members are NOT thread-safe
    // The plugin runs single-threaded - all API callbacks occur on the main game thread
    // If multi-threading is added in the future, these will need synchronization
    mutable int m_playerRaceNum;           // Cached player race number for performance
    mutable bool m_bPlayerRaceNumValid;     // Is the cached player race number still valid?
    mutable bool m_bPlayerNotFoundWarned;   // Have we already warned about player not found?
    mutable bool m_bWaitingForPlayerEntry;  // True after EventInit, cleared when player entry is identified
    int m_iPendingPlayerRaceNum;            // Stores raceNum from RaceAddEntry before EventInit (spectate-first case)

    bool m_bPlayerIsRunning;                // Set by RunStart, cleared by RunStop/RunDeinit

    // Spectate mode tracking
    int m_drawState;                       // Current draw state (ON_TRACK=0, SPECTATE=1, REPLAY=2)
    int m_spectatedRaceNum;                // Race number of rider being spectated (-1 if none)

    // Live gap tracking (published by GapBarHud)
    int m_liveGapMs = 0;                   // Current gap in milliseconds (positive = behind PB, negative = ahead)
    bool m_liveGapValid = false;           // Is the live gap valid?
};
