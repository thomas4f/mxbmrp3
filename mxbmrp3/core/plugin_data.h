// ============================================================================
// core/plugin_data.h
// Central data store for all game state received from the game API
// ============================================================================
#pragma once

#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <map>
#include <vector>
#include <array>
#include <deque>
#include <cstring>
#include <chrono>

#include "../game/game_config.h"      // For SPluginQuad_t, SPluginString_t (via correct game API)
#include "../game/unified_types.h"    // For Unified::RaceClassificationEntry
#include "plugin_constants.h"  // For Placeholders namespace
#include "event_log_types.h"   // For EventLogEntry, EventLogType

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
    int connectionType;     // 0=Unknown, 1=Offline, 2=Host, 3=Client (see Memory::ConnectionType)
    char serverName[100];   // Server name (only set when connectionType is Host or Client)
    char serverPassword[64]; // Server password (only set when connectionType is Host or Client)
    int serverClientsCount; // Current number of players on server (including self)
    int serverMaxClients;   // Maximum players allowed on server

    // Bike setup data
    int shiftRPM;           // RPM threshold for shift warning (recommended shift point)
    int limiterRPM;         // RPM limiter threshold
    float steerLock;        // Maximum steering angle in degrees
    float engineOptTemperature;   // Optimal engine temperature in Celsius
    float engineTempAlarmLow;     // Engine temperature low alarm threshold in Celsius
    float engineTempAlarmHigh;    // Engine temperature high alarm threshold in Celsius

    // Session data
    int session;
    int sessionGeneration;  // Monotonic counter, incremented on every new session (RaceSession callback)
    int sessionState;
    int sessionLength;      // milliseconds
    int sessionNumLaps;
    int conditions;
    float airTemperature;
    float trackTemperature;     // Celsius (-1 = not available, e.g., MX Bikes)
    char setupFileName[100];

    // Overtime tracking for time+laps races
    bool overtimeStarted;   // True when sessionTime goes negative
    int finishLap;          // Lap number riders need to complete to finish (leaderLapAtOvertime + sessionNumLaps)
    int lastSessionTime;    // Previous sessionTime value for detecting overtime transition
    int leaderFinishTime;   // Leader's total race time in milliseconds (-1 if not finished)

    // Non-race session expiry tracking (practice/warmup/qualifying)
    bool sessionTimeExpired; // True when sessionTime goes negative in non-race sessions

    SessionData() : trackLength(0.0f), eventType(2), connectionType(0), serverClientsCount(0), serverMaxClients(0),
        shiftRPM(13500), limiterRPM(14000), steerLock(30.0f),
        engineOptTemperature(85.0f), engineTempAlarmLow(60.0f), engineTempAlarmHigh(110.0f),
        session(-1), sessionGeneration(0), sessionState(-1), sessionLength(-1), sessionNumLaps(-1),
        conditions(-1), airTemperature(-1.0f), trackTemperature(-1.0f), overtimeStarted(false), finishLap(-1), lastSessionTime(0), leaderFinishTime(-1),
        sessionTimeExpired(false) {
        riderName[0] = '\0';
        bikeName[0] = '\0';
        category[0] = '\0';
        trackId[0] = '\0';
        trackName[0] = '\0';
        serverName[0] = '\0';
        serverPassword[0] = '\0';
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
        connectionType = 0;  // Unknown
        serverName[0] = '\0';
        serverPassword[0] = '\0';
        serverClientsCount = 0;
        serverMaxClients = 0;
        shiftRPM = 13500;  // Default fallback value
        limiterRPM = 14000;  // Default fallback value
        steerLock = 30.0f;  // Default fallback value
        engineOptTemperature = 85.0f;  // Default fallback value
        engineTempAlarmLow = 60.0f;    // Default fallback value
        engineTempAlarmHigh = 110.0f;  // Default fallback value
        session = -1;
        // Also bumped by incrementSessionGeneration() in RaceSessionHandler — the double
        // bump is intentional: clear() catches event exits that bypass RaceSessionHandler
        ++sessionGeneration;
        sessionState = -1;
        sessionLength = -1;
        sessionNumLaps = -1;
        conditions = -1;
        airTemperature = -1.0f;
        trackTemperature = -1.0f;
        setupFileName[0] = '\0';
        overtimeStarted = false;
        finishLap = -1;
        lastSessionTime = 0;
        leaderFinishTime = -1;
        sessionTimeExpired = false;
    }

    // Race finish detection helpers
    // numLaps = completed laps (0 = on first lap, 5 = completed 5 laps)
    // numLapsAtLeaderFinish = rider's numLaps when leader finished (-1 if leader hasn't finished)
    // For timed+laps races: finishLap set during overtime (covers non-lapped riders)
    // For pure lap races: use sessionNumLaps directly (covers non-lapped riders)
    // For lapped riders in either type: use numLapsAtLeaderFinish (rider finishes on next line crossing after leader)
    bool isRiderFinished(int numLaps, int numLapsAtLeaderFinish = -1) const {
        // Lapped rider finish: leader has finished and rider crossed the line since
        if (numLapsAtLeaderFinish >= 0 && numLaps > numLapsAtLeaderFinish) {
            return true;
        }
        if (sessionLength > 0 && sessionNumLaps > 0) {
            // Timed+laps race
            return finishLap > 0 && numLaps > finishLap;
        }
        // Pure lap or pure time race
        return (finishLap > 0 && numLaps > finishLap) ||
               (sessionNumLaps > 0 && finishLap <= 0 && numLaps >= sessionNumLaps);
    }

    bool isRiderOnLastLap(int numLaps, int numLapsAtLeaderFinish = -1) const {
        // Lapped rider: on last lap once leader has finished (next line crossing = finish)
        if (numLapsAtLeaderFinish >= 0 && numLaps == numLapsAtLeaderFinish) {
            return true;
        }
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
    const char* brandName;       // Cached brand name (points to static string, e.g. "Honda")
    unsigned long bikeBrandColor; // Cached bike brand color
    char formattedRaceNum[8];    // Pre-formatted race number "#999"
    char truncatedName[4];       // Pre-truncated rider name (max 3 chars)

    RaceEntryData() : raceNum(-1), bikeAbbr(nullptr), brandName(""), bikeBrandColor(0) {
        name[0] = '\0';
        bikeName[0] = '\0';
        formattedRaceNum[0] = '\0';
        truncatedName[0] = '\0';
    }

    RaceEntryData(int num, const char* riderName, const char* bike, const char* abbr, const char* brand, unsigned long brandColor)
        : raceNum(num), bikeAbbr(abbr), brandName(brand), bikeBrandColor(brandColor) {
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
    int state;          // EntryState: 0=Racing, 1=DNS, 2=Unknown, 3=Retired, 4=DSQ
    int bestLap;        // milliseconds
    int bestLapNum;     // best lap index
    int numLaps;        // number of laps completed
    int gap;            // gap to leader in milliseconds (official from splits)
    int gapLaps;        // gap to leader in laps
    int realTimeGap;    // real-time estimated gap in milliseconds
    int penalty;        // penalty time in milliseconds
    int pit;            // 0 = on track, 1 = in pits
    int finishTime;     // total race time in milliseconds (-1 if not finished)
    int numLapsAtLeaderFinish;  // rider's numLaps when leader finished (-1 = leader hasn't finished)
    bool sessionFinished;       // true when rider crosses start/finish line after non-race session time expires

    StandingsData() : raceNum(-1), state(0), bestLap(-1), bestLapNum(-1),
        numLaps(0), gap(0), gapLaps(0), realTimeGap(0), penalty(0), pit(0), finishTime(-1), numLapsAtLeaderFinish(-1),
        sessionFinished(false) {
    }

    StandingsData(int num, int st, int bLap, int bLapNum, int nLaps,
        int g, int gLaps, int pen, int p)
        : raceNum(num), state(st), bestLap(bLap), bestLapNum(bLapNum),
        numLaps(nLaps), gap(g), gapLaps(gLaps), realTimeGap(0), penalty(pen), pit(p), finishTime(-1), numLapsAtLeaderFinish(-1),
        sessionFinished(false) {
    }
};

// Hazard type for riders who are stationary or going wrong way on track
enum class HazardType {
    None,        // No hazard
    Stationary,  // Rider is stationary on track
    WrongWay     // Rider is going the wrong way (higher priority than Stationary)
};

// Real-time track position data for gap calculation
struct TrackPositionData {
    float trackPos;       // 0.0 to 1.0 along centerline
    int numLaps;          // Current lap count for handling wraparound
    int sessionTime;      // Session time in milliseconds when this position was recorded
    bool crashed;

    // Wrong-way detection
    static constexpr float TELEPORT_THRESHOLD = 0.05f;  // Single-frame jump > 5% of track = teleport (reset/pit exit)
    float previousTrackPos;   // Previous frame's trackPos for direction detection
    bool wrongWay;            // True if rider is going backwards on track
    std::chrono::steady_clock::time_point wrongWaySince;  // When rider started going backward (epoch = inactive)

    // Hazard detection state
    float lastSignificantTrackPos;  // Track pos when last significant movement detected
    std::chrono::steady_clock::time_point stationarySince;  // When rider became stationary (epoch = inactive)
    std::chrono::steady_clock::time_point hazardClearedAt;  // When hazard conditions cleared (for cooldown, epoch = inactive)
    HazardType hazardType = HazardType::None;
    bool hazardConfirmed = false;  // True once duration threshold passed (survives type transitions)
    std::chrono::steady_clock::time_point pitExitGraceStart;  // Per-rider grace after leaving pits

    TrackPositionData()
        : trackPos(0.0f), numLaps(0), sessionTime(0), crashed(false)
        , previousTrackPos(0.0f), wrongWay(false)
        , lastSignificantTrackPos(0.0f) {
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

// Per-callback timing entry for benchmark profiling (developer mode only)
struct CallbackTimingEntry {
    char name[24];              // Callback name (e.g., "RunTelemetry", "Draw")
    long long totalTimeUs;      // Accumulated time this frame (microseconds)
    long long peakTimeUs;       // Peak single-call time over measurement window
    int callCount;              // Number of calls this frame

    CallbackTimingEntry() : totalTimeUs(0), peakTimeUs(0), callCount(0) {
        name[0] = '\0';
    }
};

// Per-HUD rebuild timing entry for benchmark profiling
struct HudTimingEntry {
    char name[24];              // HUD name (e.g., "Standings", "Map")
    long long lastRebuildTimeUs; // Duration of last rebuildRenderData() call
    int rebuildCount;           // Number of rebuilds over measurement window

    HudTimingEntry() : lastRebuildTimeUs(0), rebuildCount(0) {
        name[0] = '\0';
    }
};

// Benchmark metrics for detailed profiling (developer mode only)
// Collected by DrawHandler, consumed by BenchmarkWidget
struct BenchmarkMetrics {
    static constexpr int MAX_CALLBACKS = 32;
    static constexpr int MAX_HUDS = 32;

    // Per-callback timing (indexed by callback ID)
    std::array<CallbackTimingEntry, MAX_CALLBACKS> callbacks;
    int callbackCount = 0;

    // Per-HUD rebuild timing
    std::array<HudTimingEntry, MAX_HUDS> huds;
    int hudCount = 0;

    // Aggregate metrics
    long long collectRenderTimeUs = 0;  // Time spent in collectRenderData()
    int totalQuads = 0;                 // Total quads rendered this frame
    int totalStrings = 0;               // Total strings rendered this frame

    // Active flag - when false, timing macros skip per-callback recording
    bool active = false;

    void reset() {
        for (int i = 0; i < callbackCount; ++i) {
            callbacks[i].totalTimeUs = 0;
            callbacks[i].callCount = 0;
        }
        for (int i = 0; i < hudCount; ++i) {
            huds[i].lastRebuildTimeUs = 0;
        }
        collectRenderTimeUs = 0;
        totalQuads = 0;
        totalStrings = 0;
    }

    // Register a callback slot (returns index, -1 if full)
    int registerCallback(const char* callbackName) {
        if (callbackCount >= MAX_CALLBACKS) return -1;
        int idx = callbackCount++;
        strncpy_s(callbacks[idx].name, sizeof(callbacks[idx].name), callbackName, _TRUNCATE);
        return idx;
    }

    // Register a HUD slot (returns index, -1 if full)
    int registerHud(const char* hudName) {
        if (hudCount >= MAX_HUDS) return -1;
        int idx = hudCount++;
        strncpy_s(huds[idx].name, sizeof(huds[idx].name), hudName, _TRUNCATE);
        return idx;
    }

    // Record a callback timing
    void recordCallback(int index, long long timeUs) {
        if (index < 0 || index >= callbackCount) return;
        callbacks[index].totalTimeUs += timeUs;
        callbacks[index].callCount++;
        if (timeUs > callbacks[index].peakTimeUs) {
            callbacks[index].peakTimeUs = timeUs;
        }
    }

    // Record a HUD rebuild timing
    void recordHudRebuild(int index, long long timeUs) {
        if (index < 0 || index >= hudCount) return;
        huds[index].lastRebuildTimeUs = timeUs;
        huds[index].rebuildCount++;
    }
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
    float engineTemperature;    // Engine temperature in Celsius
    float waterTemperature;     // Water/coolant temperature in Celsius
    float treadTemperature[2][3];  // Tyre tread temps [wheel: 0=front,1=rear][section: 0=left,1=mid,2=right] (GP Bikes only)
    bool isValid;       // True if telemetry data is currently available

    BikeTelemetryData() : speedometer(0.0f), gear(0), numberOfGears(6), rpm(0), fuel(0.0f), maxFuel(0.0f),
                          frontSuspLength(0.0f), rearSuspLength(0.0f),
                          frontSuspMaxTravel(0.0f), rearSuspMaxTravel(0.0f),
                          roll(0.0f), engineTemperature(0.0f), waterTemperature(0.0f),
                          treadTemperature{}, isValid(false) {}
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
    int lastLapSector4;  // milliseconds - last completed lap sector 4 time (GP Bikes only)
    int bestSector1;     // milliseconds - best sector 1 time across all laps
    int bestSector2;     // milliseconds - best sector 2 time across all laps
    int bestSector3;     // milliseconds - best sector 3 time across all laps
    int bestSector4;     // milliseconds - best sector 4 time across all laps (GP Bikes only)

    // Previous PB data (for comparison when new PB is set)
    int previousBestLapTime;     // milliseconds - previous personal best lap time
    int previousBestSector1;     // milliseconds - previous PB sector 1 time
    int previousBestSector2;     // milliseconds - previous PB sector 2 time
    int previousBestSector3;     // milliseconds - previous PB sector 3 time
    int previousBestSector4;     // milliseconds - previous PB sector 4 time (GP Bikes only)

    // Previous ideal sector data (for comparison when new best sector is set)
    int previousIdealSector1;    // milliseconds - previous best sector 1 time
    int previousIdealSector2;    // milliseconds - previous best sector 2 time
    int previousIdealSector3;    // milliseconds - previous best sector 3 time
    int previousIdealSector4;    // milliseconds - previous best sector 4 time (GP Bikes only)

    IdealLapData() : lastCompletedLapNum(-1), lastLapTime(-1),
                        lastLapSector1(-1), lastLapSector2(-1), lastLapSector3(-1), lastLapSector4(-1),
                        bestSector1(-1), bestSector2(-1), bestSector3(-1), bestSector4(-1),
                        previousBestLapTime(-1), previousBestSector1(-1),
                        previousBestSector2(-1), previousBestSector3(-1), previousBestSector4(-1),
                        previousIdealSector1(-1), previousIdealSector2(-1),
                        previousIdealSector3(-1), previousIdealSector4(-1) {}

    void clear() {
        lastCompletedLapNum = -1;
        lastLapTime = -1;
        lastLapSector1 = -1;
        lastLapSector2 = -1;
        lastLapSector3 = -1;
        lastLapSector4 = -1;
        bestSector1 = -1;
        bestSector2 = -1;
        bestSector3 = -1;
        bestSector4 = -1;
        previousBestLapTime = -1;
        previousBestSector1 = -1;
        previousBestSector2 = -1;
        previousBestSector3 = -1;
        previousBestSector4 = -1;
        previousIdealSector1 = -1;
        previousIdealSector2 = -1;
        previousIdealSector3 = -1;
        previousIdealSector4 = -1;
    }

    // Get previous ideal lap time (sum of previous best sectors)
    // For 3-sector games: S1+S2+S3, for 4-sector games: S1+S2+S3+S4
    int getPreviousIdealLapTime() const {
        if (previousIdealSector1 > 0 && previousIdealSector2 > 0 && previousIdealSector3 > 0) {
            int total = previousIdealSector1 + previousIdealSector2 + previousIdealSector3;
            if (previousIdealSector4 > 0) total += previousIdealSector4;
            return total;
        }
        return -1;
    }

    // Get ideal lap time (sum of best sectors)
    // For 3-sector games: S1+S2+S3, for 4-sector games: S1+S2+S3+S4
    int getIdealLapTime() const {
        if (bestSector1 > 0 && bestSector2 > 0 && bestSector3 > 0) {
            int total = bestSector1 + bestSector2 + bestSector3;
            if (bestSector4 > 0) total += bestSector4;
            return total;
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
    int sector4;      // milliseconds - sector 4 time (GP Bikes only, -1 if N/A)
    int lapTime;      // milliseconds - total lap time
    bool isValid;     // false if lap was invalid
    bool isComplete;  // true if lap is completed, false if in progress

    LapLogEntry() : lapNum(-1), sector1(-1), sector2(-1), sector3(-1), sector4(-1),
                    lapTime(-1), isValid(true), isComplete(false) {}

    LapLogEntry(int lap, int s1, int s2, int s3, int s4, int total, bool valid, bool complete)
        : lapNum(lap), sector1(s1), sector2(s2), sector3(s3), sector4(s4),
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
    TrackedRiders,   // Tracked riders list or settings changed
    EventLog         // New event log entry added
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
    case DataChangeType::EventLog: return "EventLog";
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
    void setConnectionType(int connectionType);
    int getConnectionType() const { return m_sessionData.connectionType; }
    void setServerName(const char* serverName);
    const char* getServerName() const { return m_sessionData.serverName; }
    void setServerPassword(const char* serverPassword);
    const char* getServerPassword() const { return m_sessionData.serverPassword; }
    void setServerClientsCount(int count);
    int getServerClientsCount() const { return m_sessionData.serverClientsCount; }
    void setServerMaxClients(int max);
    int getServerMaxClients() const { return m_sessionData.serverMaxClients; }
    void setShiftRPM(int shiftRPM);
    void setLimiterRPM(int limiterRPM);
    void setSteerLock(float steerLock);
    void setEngineTemperatureThresholds(float optTemp, float alarmLow, float alarmHigh);
    void setMaxFuel(float maxFuel);
    void setNumberOfGears(int numberOfGears);
    void setSession(int session);
    void incrementSessionGeneration();  // Called on every new session (RaceSession callback)
    void setSessionState(int sessionState);
    void setSessionLength(int sessionLength);
    void setSessionNumLaps(int sessionNumLaps);
    void setConditions(int conditions);
    void setAirTemperature(float airTemperature);
    void setTrackTemperature(float trackTemperature);
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
    void updateIdealLap(int raceNum, int completedLapNum, int lapTime, int sector1, int sector2, int sector3, int sector4, bool isValid = true);
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
    void batchUpdateStandings(Unified::RaceClassificationEntry* entries, int numEntries);
    void clearStandings();
    const std::unordered_map<int, StandingsData>& getStandings() const { return m_standings; }  // Collection (never null)
    const StandingsData* getStanding(int raceNum) const;  // Per-rider (nullable)

    // Classification order (preserves the game's official race position order)
    void setClassificationOrder(const std::vector<int>& order);
    const std::vector<int>& getClassificationOrder() const { return m_classificationOrder; }

    // Position lookup - efficiently find a rider's position by race number (1-based, or -1 if not found)
    // Uses cached map that's only rebuilt when classification changes
    int getPositionForRaceNum(int raceNum) const;

    // Display classification: official order with optional DNS filtering
    const std::vector<int>& getDisplayClassificationOrder() const;
    int getDisplayPositionForRaceNum(int raceNum) const;

    // Live gaps: show real-time estimated gaps in race sessions (toggle in settings)
    void setLiveGapsEnabled(bool enabled);
    bool isLiveGapsEnabled() const { return m_liveGapsEnabled; }

    void setShortTimeFormat(bool enabled) { m_shortTimeFormat = enabled; }
    bool isShortTimeFormat() const { return m_shortTimeFormat; }

    // DNS rider filtering: hide Did Not Start riders from display
    // When enabled, getDisplayClassificationOrder() and getDisplayPositionForRaceNum()
    // exclude riders with state == DNS. Official accessors are unaffected.
    void setFilterDnsRiders(bool enabled);
    bool isFilterDnsRiders() const { return m_filterDnsRiders; }

    // Real-time track position management (for time-based gap calculation)
    // Notifies SessionData on whole-second boundaries (drives 1Hz HUD/SSE refresh).
    void setSessionTime(int sessionTime);
    int getSessionTime() const { return m_currentSessionTime; }
    void updateTrackPosition(int raceNum, float trackPos, int numLaps, bool crashed, int sessionTime);
    void updateActiveTrackPosRiders(int numVehicles, const Unified::TrackPositionData* positions);
    bool hasActiveTrackPos(int raceNum) const { return m_activeTrackPosRiders.count(raceNum) > 0; }
    void updateRealTimeGaps();  // Calculate gaps using time deltas
    void clearLiveGapTimingPoints();  // Clear timing points for new session

    // Wrong-way detection (based on track position changes)
    bool isPlayerGoingWrongWay() const;  // Check if display rider is going wrong way
    const TrackPositionData* getPlayerTrackPosition() const;  // Get display rider's track position data for debugging

    // Blue flag detection (riders 1+ laps ahead approaching from behind)
    bool isPlayerBlueFlagged() const;  // True if display rider should yield to a lapper
    bool isRiderBlueFlagged(int raceNum) const;  // True if rider is being lapped and lapper is nearby

    // Blue flag tuning (INI-only advanced setting)
    void setBlueFlagAwarenessDistance(float meters) { m_blueFlagAwarenessDistance = std::max(10.0f, std::min(meters, 500.0f)); }
    float getBlueFlagAwarenessDistance() const { return m_blueFlagAwarenessDistance; }

    // Shared exclusion check for hazard/blue flag detection
    bool isRiderExcludedFromDetection(const StandingsData& standing) const;

    // Hazard detection (stationary or wrong-way riders ahead on track)
    HazardType getRiderHazardType(int raceNum) const;
    bool isHazardAhead() const;
    const std::vector<int>& getHazardRaceNums() const;

    // Hazard tuning (INI-only advanced settings)
    void setHazardStationaryTolerance(float meters) { m_hazardStationaryToleranceMeters = std::max(1.0f, std::min(meters, 50.0f)); }
    float getHazardStationaryTolerance() const { return m_hazardStationaryToleranceMeters; }
    void setHazardStationaryDurationMs(int ms) { m_hazardStationaryDurationMs = std::max(1000, std::min(ms, 30000)); }
    int getHazardStationaryDurationMs() const { return m_hazardStationaryDurationMs; }
    void setHazardAwarenessDistance(float meters) { m_hazardAwarenessDistance = std::max(10.0f, std::min(meters, 500.0f)); }
    float getHazardAwarenessDistance() const { return m_hazardAwarenessDistance; }
    void setHazardWrongWayDurationMs(int ms) { m_hazardWrongWayDurationMs = std::max(100, std::min(ms, 10000)); }
    int getHazardWrongWayDurationMs() const { return m_hazardWrongWayDurationMs; }
    void setHazardCooldownMs(int ms) { m_hazardCooldownMs = std::max(0, std::min(ms, 30000)); }
    int getHazardCooldownMs() const { return m_hazardCooldownMs; }
    void setHazardGracePeriodMs(int ms) { m_hazardGracePeriodMs = std::max(0, std::min(ms, 60000)); }
    int getHazardGracePeriodMs() const { return m_hazardGracePeriodMs; }

    // Overtime tracking for time+laps races
    void setOvertimeStarted(bool started) { m_sessionData.overtimeStarted = started; }
    void setFinishLap(int lap) { m_sessionData.finishLap = lap; }
    void setLastSessionTime(int time) { m_sessionData.lastSessionTime = time; }
    void setLeaderFinishTime(int time) { m_sessionData.leaderFinishTime = time; }
    int getLeaderFinishTime() const { return m_sessionData.leaderFinishTime; }

    // Non-race session expiry tracking
    void setSessionTimeExpired(bool expired) { m_sessionData.sessionTimeExpired = expired; }
    void setRiderSessionFinished(int raceNum);
    void clearSessionFinished();

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
    BenchmarkMetrics& getBenchmarkMetrics() { return m_benchmarkMetrics; }
    const BenchmarkMetrics& getBenchmarkMetrics() const { return m_benchmarkMetrics; }
    const BikeTelemetryData& getBikeTelemetry() const { return m_bikeTelemetry; }
    const InputTelemetryData& getInputTelemetry() const { return m_inputTelemetry; }
    const HistoryBuffers& getHistoryBuffers() const { return m_historyBuffers; }
    void clearHistoryBuffers() { m_historyBuffers.clear(); }

    // Debug metrics update
    void updateDebugMetrics(float fps, float pluginTimeMs, float pluginPercent);

    // Bike telemetry update
    void updateSpeedometer(float speedometer, int gear, int rpm, float fuel);
    void updateRoll(float roll);
    void updateTemperatures(float engineTemp, float waterTemp);
    void updateTreadTemperatures(const float temps[2][3]);
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

    // ========================================================================
    // Timed Notice Flags (set by RaceLapHandler, consumed by NoticesHud)
    // ========================================================================
    void notifySessionPB()  { m_newSessionPB = true; m_sessionPBTime = std::chrono::steady_clock::now(); }
    void notifyFastestLap()  { m_newFastestLap = true; m_fastestLapTime = std::chrono::steady_clock::now(); }
    void notifyAllTimePB()  { m_newAllTimePB = true; m_allTimePBTime = std::chrono::steady_clock::now(); }

    bool hasNewSessionPB() const  { return m_newSessionPB; }
    bool hasNewFastestLap() const  { return m_newFastestLap; }
    bool hasNewAllTimePB() const  { return m_newAllTimePB; }

    std::chrono::steady_clock::time_point getSessionPBTime() const  { return m_sessionPBTime; }
    std::chrono::steady_clock::time_point getFastestLapTime() const  { return m_fastestLapTime; }
    std::chrono::steady_clock::time_point getAllTimePBTime() const   { return m_allTimePBTime; }

    void clearSessionPB()  { m_newSessionPB = false; }
    void clearFastestLap()  { m_newFastestLap = false; }
    void clearAllTimePB()  { m_newAllTimePB = false; }

    // Default setup warning (set by RunHandler when entering track with default setup)
    void notifyDefaultSetup()  { m_newDefaultSetup = true; m_defaultSetupTime = std::chrono::steady_clock::now(); }
    bool hasDefaultSetupNotice() const  { return m_newDefaultSetup; }
    std::chrono::steady_clock::time_point getDefaultSetupTime() const  { return m_defaultSetupTime; }
    void clearDefaultSetupNotice()  { m_newDefaultSetup = false; }

    // ========================================================================
    // Event Log (ring buffer of notable race events)
    // ========================================================================
    void addEventLogEntry(EventLogType type, const char* message, const char* detail = nullptr);
    const std::deque<EventLogEntry>& getEventLog() const { return m_eventLog; }

private:
    PluginData() : m_currentSessionTime(0), m_playerRaceNum(-1), m_bPlayerRaceNumValid(false),
                   m_bPlayerNotFoundWarned(false), m_bWaitingForPlayerEntry(false),
                   m_iPendingPlayerRaceNum(-1), m_bPlayerIsRunning(false), m_drawState(0),
                   m_spectatedRaceNum(-1), m_bPositionCacheDirty(true) {}
    ~PluginData() {}
    PluginData(const PluginData&) = delete;
    PluginData& operator=(const PluginData&) = delete;

    // Rebuild blue flag caches (player flag + per-rider set)
    void rebuildBlueFlagCaches() const;

    // Rebuild per-rider hazard type cache (lazy, called on first access when dirty)
    void rebuildHazardTypeCaches() const;

    // Compute hazard type for a single rider (uncached, used during cache rebuild)
    HazardType computeRiderHazardType(int raceNum, std::chrono::steady_clock::time_point now) const;

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
    BenchmarkMetrics m_benchmarkMetrics;
    BikeTelemetryData m_bikeTelemetry;
    InputTelemetryData m_inputTelemetry;
    HistoryBuffers m_historyBuffers;
    std::unordered_map<int, RaceEntryData> m_raceEntries;
    std::unordered_map<int, StandingsData> m_standings;
    std::unordered_map<int, int> m_lastValidOfficialGap;  // Cache of last valid official gap per rider (prevents flicker)
    std::vector<int> m_classificationOrder;  // Official race position order from game
    int m_lastLeaderRaceNum = -1;  // Previous race leader (for leader change detection, race sessions only)
    mutable std::unordered_map<int, int> m_positionCache;  // Cached position lookup (race number -> position), rebuilt when classification changes
    mutable bool m_bPositionCacheDirty;  // Flag to rebuild position cache

    // Display filters (global toggles saved in [General])
    bool m_liveGapsEnabled = false;              // Show real-time gaps in race sessions
    bool m_shortTimeFormat = false;              // Compact time format: drop leading 0:, tenths for gaps
    bool m_filterDnsRiders = false;              // Hide DNS riders from display

    // DNS-filtered cache (derived from official classification order, rebuilt when dirty)
    mutable std::vector<int> m_filteredClassificationOrder;
    mutable std::unordered_map<int, int> m_filteredPositionCache;
    mutable bool m_bFilteredOrderDirty = true;

    std::unordered_map<int, TrackPositionData> m_trackPositions;  // Real-time track positions
    std::unordered_set<int> m_activeTrackPosRiders;  // Riders in the most recent API track position batch
    mutable bool m_cachedPlayerBlueFlagged = false;        // Cached: is the display rider blue-flagged?
    mutable std::unordered_set<int> m_cachedBlueFlaggedSet;  // Cached per-rider blue flag lookup (recomputed when dirty)
    mutable bool m_blueFlagsDirty = true;                // Invalidated when track positions change
    float m_blueFlagAwarenessDistance = 100.0f;          // Blue flag detection range in meters

    // Hazard detection state and configuration
    mutable std::vector<int> m_cachedHazardRaceNums;     // Cached hazard result (recomputed when dirty)
    mutable std::unordered_map<int, HazardType> m_cachedHazardTypes;  // Cached per-rider hazard type (recomputed when dirty)
    mutable bool m_hazardsDirty = true;                  // Invalidated when track positions change
    mutable bool m_hazardTypesDirty = true;              // Invalidated alongside m_hazardsDirty
    float m_hazardStationaryToleranceMeters = 5.0f;      // Movement below this = "not moving"
    int m_hazardStationaryDurationMs = 2000;             // Time stationary before flagged
    int m_hazardWrongWayDurationMs = 1500;               // Time going backward before flagged
    float m_hazardAwarenessDistance = 100.0f;            // Meters ahead to check for hazards
    int m_hazardCooldownMs = 1000;                       // Hysteresis before clearing hazard state
    int m_hazardGracePeriodMs = 10000;                   // Grace period after race start
    std::chrono::steady_clock::time_point m_hazardGraceStart;  // When current session entered IN_PROGRESS (epoch = inactive)
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

    // Timed notice flags (set by RaceLapHandler, consumed by NoticesHud)
    // Uses steady_clock timestamps so NoticesHud can show timed notices.
    // Invariant: the bool flag and time_point are always set together in notify*().
    // The time_points default to epoch, but that's safe because the bool flag gates
    // access — isTimedNoticeActive() is only called when the flag is true.
    bool m_newSessionPB = false;
    bool m_newFastestLap = false;
    bool m_newAllTimePB = false;
    bool m_newDefaultSetup = false;
    std::chrono::steady_clock::time_point m_sessionPBTime;
    std::chrono::steady_clock::time_point m_fastestLapTime;
    std::chrono::steady_clock::time_point m_allTimePBTime;
    std::chrono::steady_clock::time_point m_defaultSetupTime;

    // Event log ring buffer
    std::deque<EventLogEntry> m_eventLog;
};
