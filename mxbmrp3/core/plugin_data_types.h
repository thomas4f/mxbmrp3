// ============================================================================
// core/plugin_data_types.h
// The shared game-state VALUE TYPES: SessionData, RaceEntryData, StandingsData,
// RiderTrackState, telemetry/input/history buffers, ideal-lap and lap-log
// records, the benchmark counters, and the DataChangeType notification enum.
//
// WHY THESE ARE NOT IN plugin_data.h. They used to be, and they were the bulk of
// it: 815 of that header's 1,605 lines were type definitions sitting in front of
// the 789-line PluginData class, so "plugin_data.h is enormous" was really two
// separate facts wearing one hat. They are also needed by far more code than the
// singleton is — handlers, adapters, every HUD that formats a row, and the JSON
// snapshot builder all traffic in these structs, and a good number of them never
// touch PluginData's API at all.
//
// So: include THIS header when you need the data, and plugin_data.h only when you
// need the store. plugin_data.h includes this one, so nothing that already worked
// stops working.
//
// These are plain aggregates on purpose. They are copied, cached and compared all
// over the plugin (change detection is largely memcmp-shaped POD comparison), and
// several are read on the render hot path, so they carry no virtuals, no
// ownership and no allocation beyond the containers they declare.
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
#include "segment_cumulative.h" // For SegmentCumulative (segment-timer aggregation)
#include "blue_flag_detect.h"  // For BlueFlag::Rider (blue-flag proximity core)
#include "proximity_tuning.h"  // For ProximityTuning (blue-flag + hazard INI knobs)

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
    // Raw server type from the MX Bikes API (m_iServerType).
    // -1 = unknown (pre-EventInit, or non-MX-Bikes game)
    //  0 = offline / testing
    //  1 = online race server
    //  2 = online practice-day server
    int serverType;
    char serverName[100];   // Server name (only set when online)

    bool isServerKnown() const { return serverType >= 0; }
    bool isOnline() const { return serverType > 0; }
    bool isOffline() const { return serverType == 0; }

    // Bike setup data
    int shiftRPM;           // RPM threshold for shift warning (recommended shift point)
    int limiterRPM;         // RPM limiter threshold
    float steerLock;        // Maximum steering angle in degrees
    float engineOptTemperature;   // Optimal engine temperature in Celsius
    float engineTempAlarmLow;     // Engine temperature low alarm threshold in Celsius
    float engineTempAlarmHigh;    // Engine temperature high alarm threshold in Celsius

    // Session data
    int session;
    int sessionSeries;      // KRP heat index within a session (0 on other games); distinguishes heats that share the same `session` id
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

    SessionData() : trackLength(0.0f), eventType(2), serverType(-1),
        shiftRPM(13500), limiterRPM(14000), steerLock(30.0f),
        engineOptTemperature(85.0f), engineTempAlarmLow(60.0f), engineTempAlarmHigh(110.0f),
        session(-1), sessionSeries(0), sessionGeneration(0), sessionState(-1), sessionLength(-1), sessionNumLaps(-1),
        conditions(-1), airTemperature(-1.0f), trackTemperature(-1.0f), overtimeStarted(false), finishLap(-1), lastSessionTime(0), leaderFinishTime(-1),
        sessionTimeExpired(false) {
        riderName[0] = '\0';
        bikeName[0] = '\0';
        category[0] = '\0';
        trackId[0] = '\0';
        trackName[0] = '\0';
        serverName[0] = '\0';
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
        serverType = -1;  // Unknown
        serverName[0] = '\0';
        shiftRPM = 13500;  // Default fallback value
        limiterRPM = 14000;  // Default fallback value
        steerLock = 30.0f;  // Default fallback value
        engineOptTemperature = 85.0f;  // Default fallback value
        engineTempAlarmLow = 60.0f;    // Default fallback value
        engineTempAlarmHigh = 110.0f;  // Default fallback value
        session = -1;
        sessionSeries = 0;
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

// Per-rider track state derived from the RaceTrackPosition callback: lap progress
// plus the wrong-way / hazard / crash-count state machines built on top of it.
//
// NAMED FOR WHAT IT IS, not for the callback it comes from. `Unified::
// TrackPositionData` is a DIFFERENT struct with almost disjoint contents
// (raceNum + posX/posY/posZ + yaw + trackPos): handleRaceTrackPosition() forwards
// five scalars into this one and drops the world coordinates entirely, so the two
// are not two copies of one thing. They shared the name until that cost two wrong
// readings of which HUDs duplicate PluginData state; see
// tests/integration/check_hud_raw_cache.sh, which leans on the distinction.
struct RiderTrackState {
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
    bool movedSincePitExit = true;  // False after pit 1→0 until rider moves beyond tolerance; suppresses Stationary hazard for motionless pit-exit riders

    // Session crash counter — rising-edge count of the crashed flag.
    // Mirrors StatsManager's player-only edge detection, but per-rider so
    // spectated riders can show a session crash count. Resets with the
    // m_trackPositions map on new-session transitions.
    int sessionCrashCount = 0;
    bool prevCrashedState = false;

    RiderTrackState()
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

// Debug metrics for performance monitoring.
//
// THESE TWO MEAN DIFFERENT THINGS in the two threading modes, and the difference
// is the point of [Advanced] pluginThread rather than a bug to reconcile:
//
//   sync (default) : pluginTimeMs is time spent ON THE GAME THREAD, published by
//                    DrawHandler every frame. pluginPercent > 100 means the game
//                    IS being stalled by us.
//   threaded       : Draw bypasses DrawHandler, so PluginThread publishes these
//                    from the worker (PluginThread::buildAndPublishFrame ->
//                    updateDebugMetrics). pluginTimeMs is then the WORKER'S build
//                    cost; game-thread cost is ~0 by design. pluginPercent > 100
//                    therefore reads "the off-thread build takes longer than a
//                    frame, so some frames reuse the previous build" — a capacity
//                    signal, NOT a stall. Don't 'fix' it to mean the same thing.
//
// currentFps is the real Draw cadence in both modes (an EMA taken on the game
// thread in requestFrame), so it stays the true frame rate even when the worker
// rebuilds less often than the game draws.
struct DebugMetrics {
    float currentFps;       // Current frames per second
    float pluginTimeMs;     // Plugin draw time in milliseconds (see above)
    float pluginPercent;    // Plugin time as percentage of frame budget (see above)

    DebugMetrics() : currentFps(0.0f), pluginTimeMs(0.0f), pluginPercent(0.0f) {}
};

// Per-callback timing entry for benchmark profiling (developer mode only)
struct CallbackTimingEntry {
    char name[24];              // Callback name (e.g., "RunTelemetry", "Draw")
    long long totalTimeUs;      // Accumulated time this frame (microseconds)
    long long peakTimeUs;       // Peak single-call time over measurement window
    // STINT totals: never cleared by BenchmarkWidget::takeSnapshot(), only when the
    // widget is shown. The per-interval fields above are a ~0.25 s keyhole (whatever the
    // last 30 frames happened to contain), which is useless for "what did this stint
    // cost?" — the exported report needs a whole-session view. See exportReport().
    long long stintTotalTimeUs;
    long long stintPeakTimeUs;
    int       stintCallCount;
    int callCount;              // Number of calls this frame

    CallbackTimingEntry() : totalTimeUs(0), peakTimeUs(0),
                            stintTotalTimeUs(0), stintPeakTimeUs(0), stintCallCount(0),
                            callCount(0) {
        name[0] = '\0';
    }
};

// Per-HUD rebuild timing entry for benchmark profiling
struct HudTimingEntry {
    char name[24];              // HUD name (e.g., "Standings", "Map")
    long long lastRebuildTimeUs; // Duration of last rebuildRenderData() call
    int rebuildCount;           // Number of rebuilds over measurement window
    // STINT totals - see the note on CallbackTimingEntry. `lastRebuildTimeUs` above is
    // literally the LAST rebuild and never decays, which reads on screen as a constant
    // per-frame cost when it is really one rebuild's duration; these answer how many
    // rebuilds actually happened and what they cost in aggregate.
    long long stintTotalTimeUs;
    long long stintPeakTimeUs;
    int       stintRebuildCount;
    int quadCount;              // Quads this HUD emitted into the game frame (0 if hidden)
    int stringCount;            // Strings this HUD emitted (base, pre-shadow; shadow ~doubles globally)

    HudTimingEntry() : lastRebuildTimeUs(0), rebuildCount(0),
                       stintTotalTimeUs(0), stintPeakTimeUs(0), stintRebuildCount(0),
                       quadCount(0), stringCount(0) {
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

    // NOTE: currently UNUSED, and not the profiler's live reset convention — the
    // per-interval zeroing lives in BenchmarkWidget::takeSnapshot() and the peak /
    // rebuild-count reset in BenchmarkWidget::setVisible(). This clears a different
    // field set than either (it leaves peakTimeUs and rebuildCount standing), so reach
    // for it only after checking it still matches what you want.
    void reset() {
        for (int i = 0; i < callbackCount; ++i) {
            callbacks[i].totalTimeUs = 0;
            callbacks[i].callCount = 0;
        }
        for (int i = 0; i < hudCount; ++i) {
            huds[i].lastRebuildTimeUs = 0;
            huds[i].quadCount = 0;
            huds[i].stringCount = 0;
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
        callbacks[index].stintTotalTimeUs += timeUs;
        callbacks[index].stintCallCount++;
        if (timeUs > callbacks[index].stintPeakTimeUs) {
            callbacks[index].stintPeakTimeUs = timeUs;
        }
    }

    // Record a HUD rebuild timing
    void recordHudRebuild(int index, long long timeUs) {
        if (index < 0 || index >= hudCount) return;
        huds[index].lastRebuildTimeUs = timeUs;
        huds[index].rebuildCount++;
        huds[index].stintTotalTimeUs += timeUs;
        huds[index].stintRebuildCount++;
        if (timeUs > huds[index].stintPeakTimeUs) {
            huds[index].stintPeakTimeUs = timeUs;
        }
    }

    // Record how many primitives a HUD emitted into the game frame this build.
    void recordHudPrimitives(int index, int quads, int strings) {
        if (index < 0 || index >= hudCount) return;
        huds[index].quadCount = quads;
        huds[index].stringCount = strings;
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
    float pitch;        // Pitch angle in degrees (negative = nose up / wheelie, positive = nose down / endo)
    float accelX;       // Lateral G-force (chassis-local; positive = right)
    float accelY;       // Vertical G-force (chassis-local; positive = up, ~1g at rest)
    float accelZ;       // Longitudinal G-force (chassis-local; positive = forward / throttle, negative = brake)
    float engineTemperature;    // Engine temperature in Celsius
    float waterTemperature;     // Water/coolant temperature in Celsius
    float treadTemperature[2][3];  // Tyre tread temps [wheel: 0=front,1=rear][section: 0=left,1=mid,2=right] (GP Bikes only)
    // ECU / electronic rider aids (GP Bikes only)
    int ecuMode;                // Page the rider is adjusting: 0=engine map, 1=TC, 2=engine brake
    char engineMapping[4];      // Engine mapping label (e.g. "1", "STD")
    int tractionControl;        // Traction control level
    int engineBraking;          // Engine braking level
    int antiWheeling;           // Anti-wheeling level
    int ecuState;               // Bitfield of active intervention: 1=TC, 2=EB, 4=AW
    bool isValid;       // True if telemetry data is currently available

    BikeTelemetryData() : speedometer(0.0f), gear(0), numberOfGears(6), rpm(0), fuel(0.0f), maxFuel(0.0f),
                          frontSuspLength(0.0f), rearSuspLength(0.0f),
                          frontSuspMaxTravel(0.0f), rearSuspMaxTravel(0.0f),
                          roll(0.0f), pitch(0.0f),
                          accelX(0.0f), accelY(0.0f), accelZ(0.0f),
                          engineTemperature(0.0f), waterTemperature(0.0f),
                          treadTemperature{},
                          ecuMode(0), engineMapping{}, tractionControl(0),
                          engineBraking(0), antiWheeling(0), ecuState(0),
                          isValid(false) {}
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

    // Grid (standing) start grace: the anchor was set at the green flag so the live time
    // spans the grid->S/F run and matches the official splits (accumulated from the start).
    // While set, an intermediate S/F crossing must NOT reset the anchor to 0 (that would drop
    // the grid->S/F time and make the timer "jump" when the first official split arrives).
    // Cleared when the first lap completes (resetLapTimerForNewLap) or on any reset.
    bool anchoredFromRaceStart;

    // Threshold for S/F line detection (position jump > 0.5 = S/F crossing)
    static constexpr float WRAP_THRESHOLD = 0.5f;

    LapTimer()
        : anchorAccumulatedTime(0), anchorValid(false), isPaused(false)
        , lastTrackPos(0.0f), lastLapNum(0), trackMonitorInitialized(false)
        , currentLapNum(0), currentSector(0)
        , lastSplit1Time(-1), lastSplit2Time(-1)
        , anchoredFromRaceStart(false) {}

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
        anchoredFromRaceStart = false;
    }

    void setAnchor(int accumulatedTime) {
        anchorTime = std::chrono::steady_clock::now();
        anchorAccumulatedTime = accumulatedTime;
        anchorValid = true;
        isPaused = false;  // Clear pause state when setting new anchor
    }

    // Drop the anchor without touching track monitoring, so getElapsedLapTime() returns the
    // placeholder (-1) until the next S/F crossing re-anchors it. Used on pit exit: the
    // in-progress lap is dead, so the live timer should read like a fresh track entry rather
    // than keep ticking. Keeping trackMonitorInitialized means the next S/F crossing is still
    // detected (updateLapTimerTrackPosition re-anchors on !anchorValid).
    void invalidateAnchor() {
        anchorValid = false;
        isPaused = false;
        // The grid-start anchor is abandoned once the lap is dropped (e.g. the rider pitted on
        // lap 1), so end the grace: the next S/F crossing must re-anchor normally rather than be
        // skipped (which would leave the timer stuck on the placeholder until the lap completes).
        anchoredFromRaceStart = false;
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
    // NOTE: this fires on real session transitions AND ~once per second during a
    // session — setSessionTime() emits it on every whole-second boundary (the SSE
    // overlay clock heartbeat). It is NOT a clean "new session" edge. A consumer that
    // must act only on a genuine session change should gate on
    // SessionData::sessionGeneration, not on this notification (see DirectorManager).
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
