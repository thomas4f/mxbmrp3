// ============================================================================
// game/adapters/mxbikes_adapter.h
// MX Bikes game adapter - converts MX Bikes API structs to unified types
// ============================================================================
#pragma once

#include "game_adapter_base.h"
#include "../../vendor/piboso/mxb_api.h"

namespace Adapters {
namespace MXBikes {

// ============================================================================
// Game Constants
// ============================================================================
struct Adapter {
    static constexpr const char* MOD_ID = "mxbikes";
    static constexpr int MOD_DATA_VERSION = 8;
    static constexpr int INTERFACE_VERSION = 9;
    static constexpr int SPLIT_COUNT = 2;
    static constexpr Unified::VehicleType VEHICLE_TYPE = Unified::VehicleType::Bike;

    // Feature flags
    static constexpr bool HAS_HOLESHOT = true;
    static constexpr bool HAS_RACE_SPEED = false;
    static constexpr bool HAS_TRACK_TEMP = false;
    static constexpr bool HAS_SESSION_SERIES = false;
    static constexpr bool HAS_ECU = false;
    static constexpr bool HAS_PENALTY_MANAGEMENT = false;
    static constexpr bool HAS_ROLLING_START = false;
    static constexpr bool HAS_CRASH_STATE = true;

    // ========================================================================
    // Plugin Identification
    // ========================================================================
    static constexpr const char* getModID() { return MOD_ID; }
    static constexpr int getModDataVersion() { return MOD_DATA_VERSION; }
    static constexpr int getInterfaceVersion() { return INTERFACE_VERSION; }

    // ========================================================================
    // Event Data Conversion
    // ========================================================================
    static Unified::VehicleEventData toVehicleEvent(const SPluginsBikeEvent_t* src) {
        Unified::VehicleEventData result;
        if (!src) return result;

        safeCopy(result.pilotName, src->m_szRiderName, Unified::NAME_BUFFER_SIZE);
        safeCopy(result.vehicleId, src->m_szBikeID, Unified::NAME_BUFFER_SIZE);
        safeCopy(result.vehicleName, src->m_szBikeName, Unified::NAME_BUFFER_SIZE);
        safeCopy(result.category, src->m_szCategory, Unified::NAME_BUFFER_SIZE);
        safeCopy(result.trackId, src->m_szTrackID, Unified::NAME_BUFFER_SIZE);
        safeCopy(result.trackName, src->m_szTrackName, Unified::NAME_BUFFER_SIZE);

        result.trackLength = src->m_fTrackLength;
        result.numberOfGears = src->m_iNumberOfGears;
        result.maxRPM = src->m_iMaxRPM;
        result.limiterRPM = src->m_iLimiter;
        result.shiftRPM = src->m_iShiftRPM;
        result.engineOptTemperature = src->m_fEngineOptTemperature;
        result.engineTempAlarmLow = src->m_afEngineTemperatureAlarm[0];
        result.engineTempAlarmHigh = src->m_afEngineTemperatureAlarm[1];
        result.maxFuel = src->m_fMaxFuel;
        result.vehicleType = Unified::VehicleType::Bike;

        // Convert event type
        switch (src->m_iType) {
            case 1: result.eventType = Unified::EventType::Testing; break;
            case 2: result.eventType = Unified::EventType::Race; break;
            case 4: result.eventType = Unified::EventType::Special; break;  // Straight Rhythm
            default: result.eventType = Unified::EventType::Unknown; break;
        }

        // Bike-specific
        result.suspMaxTravel[0] = src->m_afSuspMaxTravel[0];
        result.suspMaxTravel[1] = src->m_afSuspMaxTravel[1];
        result.steerLock = src->m_fSteerLock;

        return result;
    }

    // ========================================================================
    // Session Data Conversion
    // ========================================================================
    static Unified::SessionData toSessionData(const SPluginsBikeSession_t* src) {
        Unified::SessionData result;
        if (!src) return result;

        result.session = src->m_iSession;
        result.conditions = toWeatherCondition(src->m_iConditions);
        result.airTemperature = src->m_fAirTemperature;
        result.trackTemperature = -1.0f;  // Not available in MX Bikes
        safeCopy(result.setupFileName, src->m_szSetupFileName, Unified::NAME_BUFFER_SIZE);

        return result;
    }

    // ========================================================================
    // Telemetry Conversion
    // ========================================================================
    static Unified::TelemetryData toTelemetry(const SPluginsBikeData_t* src, float time, float pos) {
        Unified::TelemetryData result;
        if (!src) return result;

        // Common fields
        result.rpm = src->m_iRPM;
        result.gear = src->m_iGear;
        result.speedometer = src->m_fSpeedometer;
        result.fuel = src->m_fFuel;
        result.throttle = src->m_fThrottle;
        result.clutch = src->m_fClutch;

        // Position and orientation
        result.posX = src->m_fPosX;
        result.posY = src->m_fPosY;
        result.posZ = src->m_fPosZ;
        result.velocityX = src->m_fVelocityX;
        result.velocityY = src->m_fVelocityY;
        result.velocityZ = src->m_fVelocityZ;
        result.accelX = src->m_fAccelerationX;
        result.accelY = src->m_fAccelerationY;
        result.accelZ = src->m_fAccelerationZ;

        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 3; j++) {
                result.rotMatrix[i][j] = src->m_aafRot[i][j];
            }
        }

        result.yaw = src->m_fYaw;
        result.pitch = src->m_fPitch;
        result.roll = src->m_fRoll;
        result.yawVel = src->m_fYawVelocity;
        result.pitchVel = src->m_fPitchVelocity;
        result.rollVel = src->m_fRollVelocity;

        // Track position
        result.trackPos = pos;
        result.onTrackTime = time;
        result.crashed = src->m_iCrashed;

        // Input state
        result.steer = src->m_fSteer;
        result.brake = src->m_fFrontBrake;  // Use front brake as primary

        // Temperatures
        result.engineTemperature = src->m_fEngineTemperature;
        result.waterTemperature = src->m_fWaterTemperature;

        // Wheel data (2 wheels for bikes)
        result.wheelCount = 2;
        result.wheelSpeed[0] = src->m_afWheelSpeed[0];
        result.wheelSpeed[1] = src->m_afWheelSpeed[1];
        result.wheelMaterial[0] = src->m_aiWheelMaterial[0];
        result.wheelMaterial[1] = src->m_aiWheelMaterial[1];

        result.vehicleType = Unified::VehicleType::Bike;

        // Bike-specific
        result.bike.frontBrake = src->m_fFrontBrake;
        result.bike.rearBrake = src->m_fRearBrake;
        result.bike.suspLength[0] = src->m_afSuspLength[0];
        result.bike.suspLength[1] = src->m_afSuspLength[1];
        result.bike.suspVelocity[0] = src->m_afSuspVelocity[0];
        result.bike.suspVelocity[1] = src->m_afSuspVelocity[1];
        result.bike.brakePressure[0] = src->m_afBrakePressure[0];
        result.bike.brakePressure[1] = src->m_afBrakePressure[1];
        result.bike.steerTorque = src->m_fSteerTorque;

        return result;
    }

    // ========================================================================
    // Player Lap/Split Conversion
    // ========================================================================
    static Unified::PlayerLapData toPlayerLap(const SPluginsBikeLap_t* src) {
        Unified::PlayerLapData result;
        if (!src) return result;

        result.lapNum = src->m_iLapNum;
        result.lapTime = src->m_iLapTime;
        result.invalid = src->m_iInvalid != 0;
        result.isBest = src->m_iBest != 0;

        return result;
    }

    static Unified::PlayerSplitData toPlayerSplit(const SPluginsBikeSplit_t* src) {
        Unified::PlayerSplitData result;
        if (!src) return result;

        result.splitIndex = src->m_iSplit;
        result.splitTime = src->m_iSplitTime;
        result.bestDiff = src->m_iBestDiff;

        return result;
    }

    // ========================================================================
    // Race Event Conversion
    // ========================================================================
    static Unified::RaceEventData toRaceEvent(const SPluginsRaceEvent_t* src) {
        Unified::RaceEventData result;
        if (!src) return result;

        switch (src->m_iType) {
            case 1: result.eventType = Unified::EventType::Testing; break;
            case 2: result.eventType = Unified::EventType::Race; break;
            case 4: result.eventType = Unified::EventType::Special; break;
            case -1: result.eventType = Unified::EventType::Replay; break;
            default: result.eventType = Unified::EventType::Unknown; break;
        }

        safeCopy(result.eventName, src->m_szName, Unified::NAME_BUFFER_SIZE);
        safeCopy(result.trackName, src->m_szTrackName, Unified::NAME_BUFFER_SIZE);
        result.trackLength = src->m_fTrackLength;

        return result;
    }

    // ========================================================================
    // Race Entry Conversion
    // ========================================================================
    static Unified::RaceEntryData toRaceEntry(const SPluginsRaceAddEntry_t* src) {
        Unified::RaceEntryData result;
        if (!src) return result;

        result.raceNum = src->m_iRaceNum;
        safeCopy(result.name, src->m_szName, Unified::NAME_BUFFER_SIZE);
        safeCopy(result.vehicleName, src->m_szBikeName, Unified::NAME_BUFFER_SIZE);
        safeCopy(result.vehicleShortName, src->m_szBikeShortName, Unified::NAME_BUFFER_SIZE);
        safeCopy(result.category, src->m_szCategory, Unified::NAME_BUFFER_SIZE);
        result.inactive = src->m_iUnactive != 0;
        result.numberOfGears = src->m_iNumberOfGears;
        result.maxRPM = src->m_iMaxRPM;

        return result;
    }

    // ========================================================================
    // Race Session Conversion
    // ========================================================================
    static Unified::RaceSessionData toRaceSession(const SPluginsRaceSession_t* src) {
        Unified::RaceSessionData result;
        if (!src) return result;

        result.session = src->m_iSession;
        result.sessionState = src->m_iSessionState;
        result.sessionLength = src->m_iSessionLength;
        result.sessionNumLaps = src->m_iSessionNumLaps;
        result.conditions = toWeatherCondition(src->m_iConditions);
        result.airTemperature = src->m_fAirTemperature;
        result.trackTemperature = -1.0f;  // Not available in MX Bikes

        return result;
    }

    static Unified::RaceSessionStateData toRaceSessionState(const SPluginsRaceSessionState_t* src) {
        Unified::RaceSessionStateData result;
        if (!src) return result;

        result.session = src->m_iSession;
        result.sessionState = src->m_iSessionState;
        result.sessionLength = src->m_iSessionLength;

        return result;
    }

    // ========================================================================
    // Race Lap/Split Conversion
    // ========================================================================
    static Unified::RaceLapData toRaceLap(const SPluginsRaceLap_t* src) {
        Unified::RaceLapData result;
        if (!src) return result;

        result.session = src->m_iSession;
        result.raceNum = src->m_iRaceNum;
        result.lapNum = src->m_iLapNum;
        result.lapTime = src->m_iLapTime;
        result.splitCount = SPLIT_COUNT;
        result.splits[0] = src->m_aiSplit[0];
        result.splits[1] = src->m_aiSplit[1];
        result.speed = -1.0f;  // Not available in MX Bikes RaceLap
        result.bestFlag = src->m_iBest;
        result.invalid = src->m_iInvalid != 0;

        return result;
    }

    static Unified::RaceSplitData toRaceSplit(const SPluginsRaceSplit_t* src) {
        Unified::RaceSplitData result;
        if (!src) return result;

        result.session = src->m_iSession;
        result.raceNum = src->m_iRaceNum;
        result.lapNum = src->m_iLapNum;
        result.splitIndex = src->m_iSplit;
        result.splitTime = src->m_iSplitTime;

        return result;
    }

    // ========================================================================
    // Holeshot Conversion (MX Bikes specific)
    // ========================================================================
    static Unified::RaceHoleshotData toRaceHoleshot(const SPluginsRaceHoleshot_t* src) {
        Unified::RaceHoleshotData result;
        if (!src) return result;

        result.session = src->m_iSession;
        result.raceNum = src->m_iRaceNum;
        result.time = src->m_iTime;

        return result;
    }

    // ========================================================================
    // Race Communication Conversion
    // ========================================================================
    static Unified::RaceCommunicationData toRaceCommunication(const SPluginsRaceCommunication_t* src) {
        Unified::RaceCommunicationData result;
        if (!src) return result;

        result.session = src->m_iSession;
        result.raceNum = src->m_iRaceNum;

        switch (src->m_iCommunication) {
            case 1: result.commType = Unified::CommunicationType::StateChange; break;
            case 2: result.commType = Unified::CommunicationType::Penalty; break;
            default: result.commType = Unified::CommunicationType::Unknown; break;
        }

        result.state = toEntryState(src->m_iState, true);  // MX Bikes has extra state
        result.reason = src->m_iReason;
        result.offence = src->m_iOffence;
        result.lap = src->m_iLap;
        result.penaltyType = Unified::PenaltyType::TimePenalty;  // MX Bikes only has time penalties
        result.penaltyTime = src->m_iTime;

        return result;
    }

    // ========================================================================
    // Race Classification Conversion
    // ========================================================================
    static Unified::RaceClassificationData toRaceClassification(const SPluginsRaceClassification_t* src) {
        Unified::RaceClassificationData result;
        if (!src) return result;

        result.session = src->m_iSession;
        result.sessionState = src->m_iSessionState;
        result.sessionTime = src->m_iSessionTime;
        result.numEntries = src->m_iNumEntries;

        return result;
    }

    static Unified::RaceClassificationEntry toRaceClassificationEntry(const SPluginsRaceClassificationEntry_t* src) {
        Unified::RaceClassificationEntry result;
        if (!src) return result;

        result.raceNum = src->m_iRaceNum;
        result.state = toEntryState(src->m_iState, true);
        result.bestLap = src->m_iBestLap;
        result.bestLapNum = src->m_iBestLapNum;
        result.numLaps = src->m_iNumLaps;
        result.gap = src->m_iGap;
        result.gapLaps = src->m_iGapLaps;
        result.penalty = src->m_iPenalty;
        result.inPit = src->m_iPit != 0;
        result.bestSpeed = -1.0f;  // Not available in MX Bikes

        return result;
    }

    // ========================================================================
    // Track Position Conversion
    // ========================================================================
    static Unified::TrackPositionData toTrackPosition(const SPluginsRaceTrackPosition_t* src) {
        Unified::TrackPositionData result;
        if (!src) return result;

        result.raceNum = src->m_iRaceNum;
        result.posX = src->m_fPosX;
        result.posY = src->m_fPosY;
        result.posZ = src->m_fPosZ;
        result.yaw = src->m_fYaw;
        result.trackPos = src->m_fTrackPos;
        result.crashed = src->m_iCrashed;

        return result;
    }

    // ========================================================================
    // Race Vehicle Data Conversion
    // ========================================================================
    static Unified::RaceVehicleData toRaceVehicleData(const SPluginsRaceVehicleData_t* src) {
        Unified::RaceVehicleData result;
        if (!src) return result;

        result.raceNum = src->m_iRaceNum;
        result.active = src->m_iActive != 0;
        result.rpm = src->m_iRPM;
        result.gear = src->m_iGear;
        result.speedometer = src->m_fSpeedometer;
        result.throttle = src->m_fThrottle;
        result.brake = src->m_fFrontBrake;
        result.lean = src->m_fLean;

        return result;
    }

    // ========================================================================
    // Track Segment Conversion
    // ========================================================================
    static Unified::TrackSegment toTrackSegment(const SPluginsTrackSegment_t* src) {
        Unified::TrackSegment result;
        if (!src) return result;

        result.type = src->m_iType;
        result.length = src->m_fLength;
        result.radius = src->m_fRadius;
        result.angle = src->m_fAngle;
        result.startX = src->m_afStart[0];
        result.startY = src->m_afStart[1];
        result.height = src->m_fHeight;

        return result;
    }

    // ========================================================================
    // Session Type Mapping
    // ========================================================================
    static NormalizedSession normalizeSession(int rawSession, int eventType) {
        if (eventType == 4) {  // Straight Rhythm
            switch (rawSession) {
                case 0: return NormalizedSession::Waiting;
                case 1: return NormalizedSession::Practice;
                case 2: return NormalizedSession::StraightRhythmRound;
                case 3: return NormalizedSession::StraightRhythmQuarter;
                case 4: return NormalizedSession::StraightRhythmSemi;
                case 5: return NormalizedSession::StraightRhythmFinal;
                default: return NormalizedSession::Unknown;
            }
        }

        // Testing or Race
        switch (rawSession) {
            case 0: return NormalizedSession::Waiting;
            case 1: return NormalizedSession::Practice;
            case 2: return NormalizedSession::PreQualify;
            case 3: return NormalizedSession::QualifyPractice;
            case 4: return NormalizedSession::Qualify;
            case 5: return NormalizedSession::Warmup;
            case 6: return NormalizedSession::Race1;
            case 7: return NormalizedSession::Race2;
            default: return NormalizedSession::Unknown;
        }
    }

    static bool isRaceSession(int rawSession, int eventType) {
        if (eventType == 4) {  // Straight Rhythm - rounds are "races"
            return rawSession >= 2 && rawSession <= 5;
        }
        return rawSession == 6 || rawSession == 7;  // Race1 or Race2
    }

    static bool isQualifySession(int rawSession, int eventType) {
        if (eventType == 4) return false;  // Straight Rhythm has no qualify
        return rawSession == 4;  // Qualify
    }

    static bool isPracticeSession(int rawSession, int eventType) {
        if (eventType == 4) {
            return rawSession == 1;  // Practice
        }
        return rawSession == 1 || rawSession == 2 || rawSession == 3 || rawSession == 5;
    }

    static bool isTimedSession(int rawSession, int eventType) {
        // Sessions that count time down (have a session length)
        return !isRaceSession(rawSession, eventType);
    }

    // ========================================================================
    // Spectate Vehicle Conversion
    // ========================================================================
    static Unified::SpectateVehicle toSpectateVehicle(const SPluginsSpectateVehicle_t* src) {
        Unified::SpectateVehicle result;
        if (!src) return result;

        result.raceNum = src->m_iRaceNum;
        safeCopy(result.name, src->m_szName, Unified::NAME_BUFFER_SIZE);

        return result;
    }
};

} // namespace MXBikes
} // namespace Adapters
