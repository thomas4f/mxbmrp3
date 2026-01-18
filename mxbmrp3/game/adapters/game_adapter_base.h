// ============================================================================
// game/adapters/game_adapter_base.h
// Base interface for game-specific adapters
// ============================================================================
// Each game adapter provides static methods to convert raw API structs to
// unified types. Adapters also provide game-specific constants and metadata.
//
// Adapters are stateless - all methods are static constexpr or static inline.
// ============================================================================
#pragma once

#include "../unified_types.h"

namespace Adapters {

// ============================================================================
// Game Constants Interface
// ============================================================================
// Each adapter should provide these compile-time constants.
// Example usage: Game::Adapter::MOD_ID

// Required constants per adapter:
//   static constexpr const char* MOD_ID;           // "mxbikes", "gpbikes", etc.
//   static constexpr int MOD_DATA_VERSION;         // API data version
//   static constexpr int INTERFACE_VERSION;        // Always 9 for current APIs
//   static constexpr int SPLIT_COUNT;              // Number of splits (2 or 3)
//   static constexpr Unified::VehicleType VEHICLE_TYPE;
//
// Feature flags (compile-time booleans):
//   static constexpr bool HAS_HOLESHOT;
//   static constexpr bool HAS_RACE_SPEED;
//   static constexpr bool HAS_TRACK_TEMP;
//   static constexpr bool HAS_SESSION_SERIES;
//   static constexpr bool HAS_ECU;
//   static constexpr bool HAS_PENALTY_MANAGEMENT;
//   static constexpr bool HAS_ROLLING_START;
//   static constexpr bool HAS_CRASH_STATE;

// ============================================================================
// Session Type Mapping Interface
// ============================================================================
// Each game has different session ID meanings. Adapters provide mappings.

// Normalized session types for cross-game logic
enum class NormalizedSession {
    Waiting,
    Practice,
    PreQualify,     // MX Bikes only
    QualifyPractice,// MX Bikes only
    Qualify,
    Warmup,
    Race1,
    Race2,          // MX Bikes only

    // KRP-specific heat types
    QualifyHeat,
    SecondChanceHeat,
    Prefinal,
    Final,

    // Special modes
    StraightRhythmRound,    // MX Bikes
    StraightRhythmQuarter,
    StraightRhythmSemi,
    StraightRhythmFinal,
    Challenge,              // KRP

    Unknown
};

// Session state flags (bitwise) - common across all games
namespace SessionStateFlags {
    constexpr int IN_PROGRESS = 16;
    constexpr int COMPLETED = 32;
    constexpr int SIGHTING_LAP = 64;
    constexpr int WARMUP_LAP = 128;
    constexpr int PRE_START = 256;
    constexpr int RACE_OVER = 512;
    constexpr int SESSION_COMPLETED = 1024;
    constexpr int CANCELLED = 2048;         // MX Bikes only
    constexpr int ROLLING_START = 2048;     // WRS, KRP (same bit, different meaning)
    constexpr int SEMAPHORE = 32;           // KRP only (for race start)
}

// ============================================================================
// Adapter Method Signatures (Documentation)
// ============================================================================
// Each adapter must implement these static methods:

/*
// Plugin identification
static constexpr const char* getModID();
static constexpr int getModDataVersion();
static constexpr int getInterfaceVersion();

// Event data conversion
static Unified::VehicleEventData toVehicleEvent(const GameEventStruct* src);

// Session data conversion
static Unified::SessionData toSessionData(const GameSessionStruct* src);

// Telemetry conversion
static Unified::TelemetryData toTelemetry(const GameTelemetryStruct* src, float time, float pos);

// Player lap/split conversion
static Unified::PlayerLapData toPlayerLap(const GameLapStruct* src);
static Unified::PlayerSplitData toPlayerSplit(const GameSplitStruct* src);

// Race event conversion
static Unified::RaceEventData toRaceEvent(const GameRaceEventStruct* src);

// Race entry conversion
static Unified::RaceEntryData toRaceEntry(const GameRaceEntryStruct* src);

// Race session conversion
static Unified::RaceSessionData toRaceSession(const GameRaceSessionStruct* src);

// Race lap/split conversion
static Unified::RaceLapData toRaceLap(const GameRaceLapStruct* src);
static Unified::RaceSplitData toRaceSplit(const GameRaceSplitStruct* src);

// Race communication conversion
static Unified::RaceCommunicationData toRaceCommunication(const GameRaceCommStruct* src);

// Race classification conversion
static Unified::RaceClassificationData toRaceClassification(const GameClassStruct* src);
static Unified::RaceClassificationEntry toRaceClassificationEntry(const GameClassEntryStruct* src);

// Track position conversion
static Unified::TrackPositionData toTrackPosition(const GameTrackPosStruct* src);

// Vehicle data conversion
static Unified::RaceVehicleData toRaceVehicleData(const GameVehicleDataStruct* src);

// Track segment conversion
static Unified::TrackSegment toTrackSegment(const GameTrackSegmentStruct* src);

// Session type mapping
static NormalizedSession normalizeSession(int rawSession, int eventType);
static bool isRaceSession(int rawSession, int eventType);
static bool isQualifySession(int rawSession, int eventType);
static bool isPracticeSession(int rawSession, int eventType);
*/

// ============================================================================
// Utility Functions
// ============================================================================

// Safe string copy with null termination
inline void safeCopy(char* dest, const char* src, size_t destSize) {
    if (destSize == 0) return;
    size_t i = 0;
    while (i < destSize - 1 && src[i] != '\0') {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

// Convert game-specific entry state to unified state
inline Unified::EntryState toEntryState(int rawState, bool isMXBikes = false) {
    // MX Bikes has an extra "unknown" state (2), shifting retired/DSQ
    if (isMXBikes) {
        switch (rawState) {
            case 0: return Unified::EntryState::Racing;
            case 1: return Unified::EntryState::DNS;
            case 2: return Unified::EntryState::Unknown;
            case 3: return Unified::EntryState::Retired;
            case 4: return Unified::EntryState::DSQ;
            default: return Unified::EntryState::Racing;
        }
    } else {
        // GP Bikes, WRS, KRP
        switch (rawState) {
            case 0: return Unified::EntryState::Racing;
            case 1: return Unified::EntryState::DNS;
            case 2: return Unified::EntryState::Retired;
            case 3: return Unified::EntryState::DSQ;
            default: return Unified::EntryState::Racing;
        }
    }
}

// Convert weather condition
inline Unified::WeatherCondition toWeatherCondition(int rawCondition) {
    switch (rawCondition) {
        case 0: return Unified::WeatherCondition::Clear;
        case 1: return Unified::WeatherCondition::Cloudy;
        case 2: return Unified::WeatherCondition::Rainy;
        default: return Unified::WeatherCondition::Clear;
    }
}

} // namespace Adapters
