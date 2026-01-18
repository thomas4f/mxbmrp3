// ============================================================================
// game/adapters/wrs_adapter.h
// World Racing Series game adapter - converts WRS API structs to unified types
// STATUS: STUB - awaiting wrs_api.h/cpp implementation. Adapter structure is
// prepared but conversion functions are not validated against actual WRS API.
// ============================================================================
#pragma once

#include "game_adapter_base.h"
#include "../../vendor/piboso/wrs_api.h"

namespace Adapters {
namespace WRS {

// ============================================================================
// Game Constants
// ============================================================================
struct Adapter {
    static constexpr const char* MOD_ID = "wrs";
    static constexpr int MOD_DATA_VERSION = 7;
    static constexpr int INTERFACE_VERSION = 9;
    static constexpr int SPLIT_COUNT = 2;
    static constexpr Unified::VehicleType VEHICLE_TYPE = Unified::VehicleType::Car;

    // Feature flags
    static constexpr bool HAS_HOLESHOT = false;
    static constexpr bool HAS_RACE_SPEED = true;
    static constexpr bool HAS_TRACK_TEMP = true;
    static constexpr bool HAS_SESSION_SERIES = false;
    static constexpr bool HAS_ECU = false;
    static constexpr bool HAS_PENALTY_MANAGEMENT = true;
    static constexpr bool HAS_ROLLING_START = true;
    static constexpr bool HAS_CRASH_STATE = false;

    // ========================================================================
    // Plugin Identification
    // ========================================================================
    static constexpr const char* getModID() { return MOD_ID; }
    static constexpr int getModDataVersion() { return MOD_DATA_VERSION; }
    static constexpr int getInterfaceVersion() { return INTERFACE_VERSION; }

    // ========================================================================
    // Event Data Conversion
    // ========================================================================
    static Unified::VehicleEventData toVehicleEvent(const SPluginsWRSCarEvent_t* src) {
        Unified::VehicleEventData result;
        if (!src) return result;

        safeCopy(result.pilotName, src->m_szDriverName, Unified::NAME_BUFFER_SIZE);
        safeCopy(result.vehicleId, src->m_szCarID, Unified::NAME_BUFFER_SIZE);
        safeCopy(result.vehicleName, src->m_szCarName, Unified::NAME_BUFFER_SIZE);
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
        result.vehicleType = Unified::VehicleType::Car;

        switch (src->m_iType) {
            case 1: result.eventType = Unified::EventType::Testing; break;
            case 2: result.eventType = Unified::EventType::Race; break;
            default: result.eventType = Unified::EventType::Unknown; break;
        }

        // Car-specific
        result.numberOfWheels = src->m_iNumberOfWheels;

        return result;
    }

    // ========================================================================
    // Session Data Conversion
    // ========================================================================
    static Unified::SessionData toSessionData(const SPluginsWRSCarSession_t* src) {
        Unified::SessionData result;
        if (!src) return result;

        result.session = src->m_iSession;
        result.conditions = toWeatherCondition(src->m_iConditions);
        result.airTemperature = src->m_fAirTemperature;
        result.trackTemperature = src->m_fTrackTemperature;
        safeCopy(result.setupFileName, src->m_szSetupFileName, Unified::NAME_BUFFER_SIZE);
        result.steerMaxRotation = src->m_fSteerMaxRot;

        return result;
    }

    // ========================================================================
    // Telemetry Conversion
    // ========================================================================
    static Unified::TelemetryData toTelemetry(const SPluginsWRSCarData_t* src, float time, float pos) {
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

        result.trackPos = pos;
        result.onTrackTime = time;
        result.crashed = 0;  // WRS doesn't have crash state

        result.steer = src->m_fSteer;
        result.brake = src->m_fBrake;

        result.engineTemperature = src->m_fEngineTemperature;
        result.waterTemperature = src->m_fWaterTemperature;

        // Up to 6 wheels for cars
        result.wheelCount = 4;  // Standard, but array supports 6
        for (int i = 0; i < 6; i++) {
            result.wheelSpeed[i] = src->m_afWheelSpeed[i];
            result.wheelMaterial[i] = src->m_aiWheelMaterial[i];
        }

        result.vehicleType = Unified::VehicleType::Car;

        // Car-specific
        result.car.handbrake = src->m_fHandbrake;
        result.car.turboPressure = src->m_fTurboPressure;
        result.car.oilPressure = src->m_fOilPressure;
        result.car.brakeBias = src->m_fBrakeBias;
        for (int i = 0; i < 6; i++) {
            result.car.suspNormLength[i] = src->m_afSuspNormLength[i];
        }
        result.car.steerTorque = src->m_fSteerTorque;
        result.car.pitLimiter = src->m_iPitLimiter;

        return result;
    }

    // ========================================================================
    // Player Lap/Split Conversion
    // ========================================================================
    static Unified::PlayerLapData toPlayerLap(const SPluginsWRSCarLap_t* src) {
        Unified::PlayerLapData result;
        if (!src) return result;

        result.lapNum = src->m_iLapNum;
        result.lapTime = src->m_iLapTime;
        result.invalid = src->m_iInvalid != 0;
        result.isBest = src->m_iBest != 0;

        return result;
    }

    static Unified::PlayerSplitData toPlayerSplit(const SPluginsWRSCarSplit_t* src) {
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
    static Unified::RaceEventData toRaceEvent(const SPluginsWRSRaceEvent_t* src) {
        Unified::RaceEventData result;
        if (!src) return result;

        switch (src->m_iType) {
            case 1: result.eventType = Unified::EventType::Testing; break;
            case 2: result.eventType = Unified::EventType::Race; break;
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
    static Unified::RaceEntryData toRaceEntry(const SPluginsWRSRaceAddEntry_t* src) {
        Unified::RaceEntryData result;
        if (!src) return result;

        result.raceNum = src->m_iRaceNum;
        safeCopy(result.name, src->m_szName, Unified::NAME_BUFFER_SIZE);
        safeCopy(result.vehicleName, src->m_szCarName, Unified::NAME_BUFFER_SIZE);
        safeCopy(result.vehicleShortName, src->m_szCarShortName, Unified::NAME_BUFFER_SIZE);
        safeCopy(result.category, src->m_szCategory, Unified::NAME_BUFFER_SIZE);
        result.inactive = src->m_iUnactive != 0;
        result.numberOfGears = src->m_iNumberOfGears;
        result.maxRPM = src->m_iMaxRPM;

        return result;
    }

    // ========================================================================
    // Race Session Conversion
    // ========================================================================
    static Unified::RaceSessionData toRaceSession(const SPluginsWRSRaceSession_t* src) {
        Unified::RaceSessionData result;
        if (!src) return result;

        result.session = src->m_iSession;
        result.sessionState = src->m_iSessionState;
        result.sessionLength = src->m_iSessionLength;
        result.sessionNumLaps = src->m_iSessionNumLaps;
        result.conditions = toWeatherCondition(src->m_iConditions);
        result.airTemperature = src->m_fAirTemperature;
        result.trackTemperature = src->m_fTrackTemperature;

        return result;
    }

    static Unified::RaceSessionStateData toRaceSessionState(const SPluginsWRSRaceSessionState_t* src) {
        Unified::RaceSessionStateData result;
        if (!src) return result;

        result.session = src->m_iSession;
        result.sessionState = src->m_iSessionState;
        result.sessionLength = src->m_iSessionLength;

        return result;
    }

    // ========================================================================
    // Race Lap/Split/Speed Conversion
    // ========================================================================
    static Unified::RaceLapData toRaceLap(const SPluginsWRSRaceLap_t* src) {
        Unified::RaceLapData result;
        if (!src) return result;

        result.session = src->m_iSession;
        result.raceNum = src->m_iRaceNum;
        result.lapNum = src->m_iLapNum;
        result.lapTime = src->m_iLapTime;
        result.splitCount = SPLIT_COUNT;
        result.splits[0] = src->m_aiSplit[0];
        result.splits[1] = src->m_aiSplit[1];
        result.speed = src->m_fSpeed;
        result.bestFlag = src->m_iBest;
        result.invalid = src->m_iInvalid != 0;

        return result;
    }

    static Unified::RaceSplitData toRaceSplit(const SPluginsWRSRaceSplit_t* src) {
        Unified::RaceSplitData result;
        if (!src) return result;

        result.session = src->m_iSession;
        result.raceNum = src->m_iRaceNum;
        result.lapNum = src->m_iLapNum;
        result.splitIndex = src->m_iSplit;
        result.splitTime = src->m_iSplitTime;

        return result;
    }

    static Unified::RaceSpeedData toRaceSpeed(const SPluginsWRSRaceSpeed_t* src) {
        Unified::RaceSpeedData result;
        if (!src) return result;

        result.session = src->m_iSession;
        result.raceNum = src->m_iRaceNum;
        result.lapNum = src->m_iLapNum;
        result.speed = src->m_fSpeed;

        return result;
    }

    // ========================================================================
    // Race Communication Conversion
    // ========================================================================
    static Unified::RaceCommunicationData toRaceCommunication(const SPluginsWRSRaceCommunication_t* src) {
        Unified::RaceCommunicationData result;
        if (!src) return result;

        result.session = src->m_iSession;
        result.raceNum = src->m_iRaceNum;

        switch (src->m_iCommunication) {
            case 1: result.commType = Unified::CommunicationType::StateChange; break;
            case 2: result.commType = Unified::CommunicationType::Penalty; break;
            case 3: result.commType = Unified::CommunicationType::PenaltyClear; break;
            case 4: result.commType = Unified::CommunicationType::PenaltyChange; break;
            default: result.commType = Unified::CommunicationType::Unknown; break;
        }

        result.state = toEntryState(src->m_iState, false);
        result.reason = src->m_iReason;
        result.offence = src->m_iOffence;
        result.lap = src->m_iLap;
        result.penaltyIndex = src->m_iIndex;

        switch (src->m_iType) {
            case 0: result.penaltyType = Unified::PenaltyType::DriveThrough; break;
            case 1: result.penaltyType = Unified::PenaltyType::TimePenalty; break;
            case 2: result.penaltyType = Unified::PenaltyType::PositionPenalty; break;
            default: result.penaltyType = Unified::PenaltyType::TimePenalty; break;
        }
        result.penaltyTime = src->m_iTime;

        return result;
    }

    // ========================================================================
    // Race Classification Conversion
    // ========================================================================
    static Unified::RaceClassificationData toRaceClassification(const SPluginsWRSRaceClassification_t* src) {
        Unified::RaceClassificationData result;
        if (!src) return result;

        result.session = src->m_iSession;
        result.sessionState = src->m_iSessionState;
        result.sessionTime = src->m_iSessionTime;
        result.numEntries = src->m_iNumEntries;

        return result;
    }

    static Unified::RaceClassificationEntry toRaceClassificationEntry(const SPluginsWRSRaceClassificationEntry_t* src) {
        Unified::RaceClassificationEntry result;
        if (!src) return result;

        result.raceNum = src->m_iRaceNum;
        result.state = toEntryState(src->m_iState, false);
        result.bestLap = src->m_iBestLap;
        result.bestLapNum = src->m_iBestLapNum;
        result.numLaps = src->m_iNumLaps;
        result.gap = src->m_iGap;
        result.gapLaps = src->m_iGapLaps;
        result.penalty = src->m_iPenalty;
        result.inPit = src->m_iPit != 0;
        result.bestSpeed = src->m_fBestSpeed;

        return result;
    }

    // ========================================================================
    // Track Position Conversion
    // ========================================================================
    static Unified::TrackPositionData toTrackPosition(const SPluginsWRSRaceTrackPosition_t* src) {
        Unified::TrackPositionData result;
        if (!src) return result;

        result.raceNum = src->m_iRaceNum;
        result.posX = src->m_fPosX;
        result.posY = src->m_fPosY;
        result.posZ = src->m_fPosZ;
        result.yaw = src->m_fYaw;
        result.trackPos = src->m_fTrackPos;
        result.crashed = 0;  // WRS doesn't have crash state

        return result;
    }

    // ========================================================================
    // Race Vehicle Data Conversion
    // ========================================================================
    static Unified::RaceVehicleData toRaceVehicleData(const SPluginsWRSRaceVehicleData_t* src) {
        Unified::RaceVehicleData result;
        if (!src) return result;

        result.raceNum = src->m_iRaceNum;
        result.active = src->m_iActive != 0;
        result.rpm = src->m_iRPM;
        result.gear = src->m_iGear;
        result.speedometer = src->m_fSpeedometer;
        result.throttle = src->m_fThrottle;
        result.brake = src->m_fBrake;
        result.steer = src->m_fSteer;
        result.lean = 0;  // Cars don't lean

        return result;
    }

    // ========================================================================
    // Track Segment Conversion
    // ========================================================================
    static Unified::TrackSegment toTrackSegment(const SPluginsWRSTrackSegment_t* src) {
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
        // WRS: 0=waiting, 1=practice, 2=qualify, 3=warmup, 4=race
        switch (rawSession) {
            case 0: return NormalizedSession::Waiting;
            case 1: return NormalizedSession::Practice;
            case 2: return NormalizedSession::Qualify;
            case 3: return NormalizedSession::Warmup;
            case 4: return NormalizedSession::Race1;
            default: return NormalizedSession::Unknown;
        }
    }

    static bool isRaceSession(int rawSession, int eventType) {
        return rawSession == 4;
    }

    static bool isQualifySession(int rawSession, int eventType) {
        return rawSession == 2;
    }

    static bool isPracticeSession(int rawSession, int eventType) {
        return rawSession == 1 || rawSession == 3;
    }

    static bool isTimedSession(int rawSession, int eventType) {
        return !isRaceSession(rawSession, eventType);
    }
};

} // namespace WRS
} // namespace Adapters
