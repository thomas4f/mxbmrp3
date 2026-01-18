// ============================================================================
// game/unified_types.h
// Game-agnostic data structures for multi-game plugin support
// ============================================================================
// These unified types serve as an abstraction layer between game-specific APIs
// (MX Bikes, GP Bikes, WRS, KRP) and the core plugin logic (PluginData, HUDs).
//
// Data flow:
//   Game API structs -> GameAdapter::toXxx() -> Unified types -> PluginData
//
// This allows the same core plugin code to work with multiple games by
// providing adapters that translate game-specific structs to these unified types.
// ============================================================================
#pragma once

#include <vector>
#include <array>
#include <cstring>

namespace Unified {

// ============================================================================
// Constants
// ============================================================================

// Maximum number of splits any game supports (GP Bikes has 3, others have 2)
constexpr int MAX_SPLITS = 3;

// Maximum number of wheels any vehicle type supports (cars have up to 6)
constexpr int MAX_WHEELS = 6;

// String buffer sizes (matching game API limits)
constexpr size_t NAME_BUFFER_SIZE = 100;

// ============================================================================
// Enumerations
// ============================================================================

// Vehicle type determines which telemetry fields are available
enum class VehicleType {
    Bike,   // MX Bikes, GP Bikes (2 wheels, lean angles, suspension travel)
    Car,    // WRS (4-6 wheels, steering wheel, turbo, handbrake)
    Kart    // KRP (4 wheels, cylinder head temp, front brakes separate)
};

// Unified event types across all games
enum class EventType {
    Unknown = 0,
    Testing = 1,    // Solo testing / time attack / open practice
    Race = 2,       // Full race weekend with sessions
    Special = 4,    // Game-specific: Straight Rhythm (MXB), Challenge (KRP)
    Replay = -1     // Loaded replay
};

// Unified weather conditions (same across all Piboso games)
enum class WeatherCondition {
    Clear = 0,
    Cloudy = 1,
    Rainy = 2
};

// Rider/Driver state
enum class EntryState {
    Racing = 0,     // Normal racing
    DNS = 1,        // Did not start
    Unknown = 2,    // Unknown state (MX Bikes specific)
    Retired = 3,    // Retired from session (MX Bikes: 3, others: 2)
    DSQ = 4         // Disqualified (MX Bikes: 4, others: 3)
};

// Communication message types
enum class CommunicationType {
    Unknown = 0,
    StateChange = 1,
    Penalty = 2,
    PenaltyClear = 3,   // GP Bikes, WRS, KRP only
    PenaltyChange = 4   // GP Bikes, WRS, KRP only
};

// Penalty types
enum class PenaltyType {
    TimePenalty = 0,    // MX Bikes only has time penalties
    DriveThrough = 1,   // GP Bikes, WRS (ride-through/drive-through)
    PositionPenalty = 2 // WRS, KRP
};

// ============================================================================
// Core Data Structures
// ============================================================================

// Event initialization data (from EventInit callback)
struct VehicleEventData {
    char pilotName[NAME_BUFFER_SIZE];       // "RiderName" or "DriverName"
    char vehicleId[NAME_BUFFER_SIZE];       // "BikeID" or "CarID" or "KartID"
    char vehicleName[NAME_BUFFER_SIZE];
    char category[NAME_BUFFER_SIZE];
    char trackId[NAME_BUFFER_SIZE];
    char trackName[NAME_BUFFER_SIZE];
    float trackLength;                      // meters
    int numberOfGears;
    int maxRPM;
    int limiterRPM;
    int shiftRPM;
    float engineOptTemperature;             // Celsius
    float engineTempAlarmLow;               // Celsius
    float engineTempAlarmHigh;              // Celsius
    float maxFuel;                          // liters
    EventType eventType;
    VehicleType vehicleType;

    // Bike-specific (MX Bikes, GP Bikes)
    float suspMaxTravel[2];                 // front, rear (meters)
    float steerLock;                        // degrees

    // Car-specific (WRS)
    int numberOfWheels;                     // 4-6

    // Kart-specific (KRP)
    int driveType;                          // 0=direct, 1=clutch, 2=shifter
    int engineCooling;                      // 0=air, 1=water
    char dashType[NAME_BUFFER_SIZE];

    VehicleEventData() {
        memset(this, 0, sizeof(*this));
        vehicleType = VehicleType::Bike;
        eventType = EventType::Unknown;
    }
};

// Session data (from RunInit callback)
struct SessionData {
    int session;                    // Game-specific session ID
    int sessionState;               // Bitflags for session state
    int sessionLength;              // milliseconds, 0 = no limit
    int sessionNumLaps;             // Number of laps, 0 = no limit
    WeatherCondition conditions;
    float airTemperature;           // Celsius
    float trackTemperature;         // Celsius (not available in MX Bikes)
    char setupFileName[NAME_BUFFER_SIZE];

    // Car-specific (WRS)
    float steerMaxRotation;         // degrees

    // Kart-specific (KRP)
    int sessionSeries;

    SessionData() {
        memset(this, 0, sizeof(*this));
        conditions = WeatherCondition::Clear;
        trackTemperature = -1.0f;   // -1 indicates not available
    }
};

// Real-time telemetry data (from RunTelemetry callback)
struct TelemetryData {
    // Common fields (all games)
    int rpm;
    int gear;                       // 0 = neutral, -1 = reverse (cars)
    float speedometer;              // m/s
    float fuel;                     // liters
    float throttle;                 // 0-1
    float clutch;                   // 0-1 (0 = engaged)

    // Position and orientation
    float posX, posY, posZ;         // world position (meters)
    float velocityX, velocityY, velocityZ;  // m/s
    float accelX, accelY, accelZ;   // G-forces
    float rotMatrix[3][3];
    float yaw, pitch, roll;         // degrees
    float yawVel, pitchVel, rollVel; // degrees/second

    // Track position
    float trackPos;                 // 0-1 along centerline
    float onTrackTime;              // seconds
    int crashed;                    // 1 = crashed/detached

    // Input state
    float steer;                    // degrees (bikes) or -1 to 1 (cars/karts)
    float brake;                    // 0-1 (combined for cars, front for bikes)

    // Temperatures
    float engineTemperature;        // Celsius
    float waterTemperature;         // Celsius

    // Wheel data (variable count by vehicle type)
    int wheelCount;
    float wheelSpeed[MAX_WHEELS];   // m/s
    int wheelMaterial[MAX_WHEELS];  // 0 = not in contact

    VehicleType vehicleType;

    // ---- Vehicle-type specific fields ----

    // Bike-specific (MX Bikes, GP Bikes)
    struct BikeData {
        float frontBrake;           // 0-1
        float rearBrake;            // 0-1
        float suspLength[2];        // front, rear (meters)
        float suspVelocity[2];      // front, rear (m/s)
        float brakePressure[2];     // front, rear (kPa)
        float steerTorque;          // Nm
        float pitchRel;             // degrees relative to ground
        float rollRel;              // degrees relative to ground
        float riderLRLean;          // -1 to 1 (GP Bikes only)
        int pitLimiter;             // GP Bikes only

        // GP Bikes ECU
        int ecuMode;                // 0=engine map, 1=TC, 2=engine brake
        char engineMapping[4];
        int tractionControl;
        int engineBraking;
        int antiWheeling;
        int ecuState;               // bitfield: 1=TC, 2=EB, 4=AW active

        // GP Bikes tread temps [wheel][section: left/mid/right]
        float treadTemperature[2][3];

        BikeData() { memset(this, 0, sizeof(*this)); }
    } bike;

    // Car-specific (WRS)
    struct CarData {
        float handbrake;            // 0-1
        float turboPressure;        // bar
        float oilPressure;          // bar
        float brakeBias;            // 0-1 (1 = fully front)
        float suspNormLength[MAX_WHEELS];  // normalized 0-1
        float steerTorque;          // Nm
        int pitLimiter;

        CarData() { memset(this, 0, sizeof(*this)); }
    } car;

    // Kart-specific (KRP)
    struct KartData {
        float cylinderHeadTemp;     // Celsius
        float frontBrakesInput;     // 0-1 (separate from rear)
        float inputSteer;           // degrees
        float inputThrottle;        // 0-1 (before processing)
        float inputBrake;           // 0-1 (before processing)
        float steerTorque;          // Nm

        KartData() { memset(this, 0, sizeof(*this)); }
    } kart;

    TelemetryData() {
        memset(this, 0, sizeof(*this));
        vehicleType = VehicleType::Bike;
        wheelCount = 2;
    }
};

// Player lap data (from RunLap callback)
struct PlayerLapData {
    int lapNum;                     // 1-based (lap just completed, first lap is 1)
    int lapTime;                    // milliseconds
    bool invalid;
    bool isBest;                    // Personal best

    PlayerLapData() : lapNum(0), lapTime(0), invalid(false), isBest(false) {}
};

// Player split data (from RunSplit callback)
struct PlayerSplitData {
    int splitIndex;                 // 0-based split index
    int splitTime;                  // milliseconds (cumulative from lap start)
    int bestDiff;                   // milliseconds difference from best lap

    PlayerSplitData() : splitIndex(0), splitTime(0), bestDiff(0) {}
};

// ============================================================================
// Race Data Structures (multiplayer / all riders)
// ============================================================================

// Race event data (from RaceEvent callback)
struct RaceEventData {
    EventType eventType;
    char eventName[NAME_BUFFER_SIZE];
    char trackName[NAME_BUFFER_SIZE];
    float trackLength;              // meters

    RaceEventData() {
        memset(this, 0, sizeof(*this));
        eventType = EventType::Unknown;
    }
};

// Race entry data (from RaceAddEntry callback)
struct RaceEntryData {
    int raceNum;                    // Unique identifier
    char name[NAME_BUFFER_SIZE];    // Rider/driver name
    char vehicleName[NAME_BUFFER_SIZE];
    char vehicleShortName[NAME_BUFFER_SIZE];
    char category[NAME_BUFFER_SIZE];
    bool inactive;                  // Left the event
    int numberOfGears;
    int maxRPM;

    RaceEntryData() {
        memset(this, 0, sizeof(*this));
        raceNum = -1;
    }
};

// Race session data (from RaceSession callback)
struct RaceSessionData {
    int session;                    // Game-specific session ID
    int sessionState;               // Bitflags
    int sessionLength;              // milliseconds
    int sessionNumLaps;
    WeatherCondition conditions;
    float airTemperature;           // Celsius
    float trackTemperature;         // Celsius

    // KRP-specific
    int sessionSeries;
    int numEntries;
    int entries[50];                // Race numbers
    int grid[50];                   // Grid positions
    int group1, group2;             // Qualify heat groups

    RaceSessionData() {
        memset(this, 0, sizeof(*this));
        conditions = WeatherCondition::Clear;
        trackTemperature = -1.0f;
    }
};

// Race session state update (from RaceSessionState callback)
struct RaceSessionStateData {
    int session;                    // Game-specific session ID
    int sessionSeries;              // KRP only
    int sessionState;               // Bitflags
    int sessionLength;              // milliseconds (may be updated during session)

    RaceSessionStateData() : session(0), sessionSeries(0), sessionState(0), sessionLength(0) {}
};

// Race lap data (from RaceLap callback)
struct RaceLapData {
    int session;
    int sessionSeries;              // KRP only
    int raceNum;
    int lapNum;                     // 1-based (lap just completed)
    int lapTime;                    // milliseconds
    int splits[MAX_SPLITS];         // milliseconds (cumulative)
    int splitCount;                 // Actual number of splits used
    float speed;                    // m/s (not available in MX Bikes)
    int bestFlag;                   // 1 = personal best, 2 = overall best
    bool invalid;

    RaceLapData() {
        memset(this, 0, sizeof(*this));
        splitCount = 2;             // Default for most games
        speed = -1.0f;              // -1 indicates not available
    }
};

// Race split data (from RaceSplit callback)
struct RaceSplitData {
    int session;
    int sessionSeries;              // KRP only
    int raceNum;
    int lapNum;
    int splitIndex;                 // 0-based
    int splitTime;                  // milliseconds

    RaceSplitData() { memset(this, 0, sizeof(*this)); }
};

// Race speed data (from RaceSpeed callback - GP Bikes, WRS, KRP only)
struct RaceSpeedData {
    int session;
    int sessionSeries;              // KRP only
    int raceNum;
    int lapNum;
    float speed;                    // m/s

    RaceSpeedData() { memset(this, 0, sizeof(*this)); }
};

// Holeshot data (from RaceHoleshot callback - MX Bikes only)
struct RaceHoleshotData {
    int session;
    int raceNum;
    int time;                       // milliseconds

    RaceHoleshotData() { memset(this, 0, sizeof(*this)); }
};

// Race communication data (from RaceCommunication callback)
struct RaceCommunicationData {
    int session;
    int sessionSeries;              // KRP only
    int raceNum;
    CommunicationType commType;
    EntryState state;
    int reason;                     // Game-specific DSQ reason
    int offence;                    // Game-specific offence type
    int lap;                        // Lap index
    PenaltyType penaltyType;
    int penaltyTime;                // milliseconds
    int penaltyIndex;               // For clear/change (GP Bikes, WRS, KRP)

    RaceCommunicationData() {
        memset(this, 0, sizeof(*this));
        commType = CommunicationType::Unknown;
        state = EntryState::Racing;
        penaltyType = PenaltyType::TimePenalty;
    }
};

// Race classification header (from RaceClassification callback)
struct RaceClassificationData {
    int session;
    int sessionSeries;              // KRP only
    int sessionState;
    int sessionTime;                // milliseconds
    int numEntries;

    RaceClassificationData() { memset(this, 0, sizeof(*this)); }
};

// Race classification entry
struct RaceClassificationEntry {
    int raceNum;
    EntryState state;
    int bestLap;                    // milliseconds
    int bestLapNum;                 // 1-based (which lap was best)
    int numLaps;
    int gap;                        // milliseconds
    int gapLaps;
    int penalty;                    // milliseconds
    bool inPit;
    float bestSpeed;                // m/s (not in MX Bikes)

    RaceClassificationEntry() {
        memset(this, 0, sizeof(*this));
        state = EntryState::Racing;
        bestSpeed = -1.0f;
    }
};

// Track position data (from RaceTrackPosition callback)
struct TrackPositionData {
    int raceNum;
    float posX, posY, posZ;         // meters
    float yaw;                      // degrees from north
    float trackPos;                 // 0-1 along centerline
    int crashed;                    // MX Bikes, GP Bikes only

    TrackPositionData() { memset(this, 0, sizeof(*this)); }
};

// Vehicle data for other riders (from RaceVehicleData callback)
struct RaceVehicleData {
    int raceNum;
    bool active;
    int rpm;
    int gear;
    float speedometer;              // m/s
    float throttle;                 // 0-1
    float brake;                    // 0-1 (front brake for bikes)
    float lean;                     // degrees (bikes only)
    float steer;                    // -1 to 1 (cars/karts only)

    RaceVehicleData() { memset(this, 0, sizeof(*this)); }
};

// ============================================================================
// Track Data
// ============================================================================

// Track segment (identical across all games)
struct TrackSegment {
    int type;                       // 0 = straight, 1 = curve
    float length;                   // meters
    float radius;                   // meters, <0 for left curves, 0 for straights
    float angle;                    // start angle in degrees, 0 = north
    float startX, startY;           // start position in meters
    float height;                   // start height in meters

    TrackSegment() { memset(this, 0, sizeof(*this)); }
};

// ============================================================================
// Spectator Control
// ============================================================================

struct SpectateVehicle {
    int raceNum;
    char name[NAME_BUFFER_SIZE];

    SpectateVehicle() { memset(this, 0, sizeof(*this)); }
};

} // namespace Unified
