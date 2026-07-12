// ============================================================================
// tests/integration/harness/plugin_api.h
// The subset of the PiBoSo plugin ABI the headless tests drive. These structs
// mirror vendor/piboso/mxb_api.h *exactly* (field order, types, default
// alignment — do NOT pack): the plugin validates each classification element's
// size against its own compiled struct, so a layout mismatch here is caught at
// runtime as an "element size N != expected M" skew, not silently tolerated.
//
// Kept deliberately minimal — only the callbacks the integration tests use. Add
// a struct here when a new test needs to drive a new callback, and keep it byte-
// compatible with the real header. Shared by every driver in tests/integration/tests/.
// ============================================================================
#pragma once
#include <cstring>
#include <string>
#include <vector>

// --- game -> plugin callback payloads (default-aligned, matching the plugin) --
struct SPluginsBikeEvent_t {
    char m_szRiderName[100]; char m_szBikeID[100]; char m_szBikeName[100];
    int m_iNumberOfGears; int m_iMaxRPM; int m_iLimiter; int m_iShiftRPM;
    float m_fEngineOptTemperature; float m_afEngineTemperatureAlarm[2];
    float m_fMaxFuel; float m_afSuspMaxTravel[2]; float m_fSteerLock;
    char m_szCategory[100]; char m_szTrackID[100]; char m_szTrackName[100];
    float m_fTrackLength; int m_iType; char m_szServerName[64];
    int m_iServerType; char m_szGUID[100];
};
struct SPluginsRaceEvent_t {
    int m_iType; char m_szName[100]; char m_szTrackName[100]; float m_fTrackLength;
};
struct SPluginsRaceSession_t {
    int m_iSession; int m_iSessionState; int m_iSessionLength;
    int m_iSessionNumLaps; int m_iConditions; float m_fAirTemperature;
};
struct SPluginsRaceSessionState_t {
    int m_iSession; int m_iSessionState; int m_iSessionLength;
    // Session-state bits: 16=in progress, 32=completed, 64=sighting lap,
    // 256=pre-start, 512=race over, 2048=cancelled.
};
struct SPluginsRaceAddEntry_t {
    int m_iRaceNum; char m_szName[100]; char m_szBikeName[100];
    char m_szBikeShortName[100]; char m_szCategory[100];
    int m_iUnactive; int m_iNumberOfGears; int m_iMaxRPM;
};
struct SPluginsRaceRemoveEntry_t { int m_iRaceNum; };
struct SPluginsRaceClassification_t {
    int m_iSession; int m_iSessionState; int m_iSessionTime; int m_iNumEntries;
};
struct SPluginsRaceClassificationEntry_t {
    int m_iRaceNum; int m_iState; int m_iBestLap; int m_iBestLapNum;
    int m_iNumLaps; int m_iGap; int m_iGapLaps; int m_iPenalty; int m_iPit;
};
struct SPluginsRaceCommunication_t {
    int m_iSession; int m_iRaceNum; int m_iCommunication;  // 1=change state; 2=penalty
    int m_iState;   // 1=DNS; 3=retired; 4=DSQ
    int m_iReason; int m_iOffence; int m_iLap; int m_iStart; int m_iType; int m_iTime;
};
struct SPluginsRaceLap_t {
    int m_iSession; int m_iRaceNum; int m_iLapNum; int m_iInvalid;
    int m_iLapTime;          // milliseconds
    int m_aiSplit[2];        // milliseconds
    int m_iBest;             // 1 = personal best lap; 2 = overall best lap
};
struct SPluginsRaceTrackPosition_t {
    int m_iRaceNum;
    float m_fPosX, m_fPosY, m_fPosZ;  // meters
    float m_fYaw;                     // degrees from north
    float m_fTrackPos;                // position on the centerline, 0..1
    int m_iCrashed;
};
// Holeshot winner (first to the first corner) + time. The game doesn't currently
// fire this, but the recorder captures it and the replayer dispatches it — layout
// must match mxbmrp3/vendor/piboso/mxb_api.h's SPluginsRaceHoleshot_t.
struct SPluginsRaceHoleshot_t {
    int m_iSession; int m_iRaceNum; int m_iTime;
};
struct SPluginsSpectateVehicle_t {
    int m_iRaceNum;
    char m_szName[100];
};
struct SPluginsRaceSplit_t {
    int m_iSession;
    int m_iRaceNum;
    int m_iLapNum;
    int m_iSplit;            // split index (0..2)
    int m_iSplitTime;        // milliseconds
};
// RunTelemetry payload — the full per-frame bike telemetry. Copied verbatim from
// mxb_api.h (default alignment): RunTelemetry doesn't validate its data size, and
// the adapter reads many fields, so the WHOLE struct must line up. Stats use
// m_fSpeedometer (distance + top speed) and m_iGear (shift count).
struct SPluginsBikeData_t {
    int m_iRPM;
    float m_fEngineTemperature;
    float m_fWaterTemperature;
    int m_iGear;
    float m_fFuel;
    float m_fSpeedometer;                                 // meters/second
    float m_fPosX, m_fPosY, m_fPosZ;
    float m_fVelocityX, m_fVelocityY, m_fVelocityZ;
    float m_fAccelerationX, m_fAccelerationY, m_fAccelerationZ;
    float m_aafRot[3][3];
    float m_fYaw, m_fPitch, m_fRoll;
    float m_fYawVelocity, m_fPitchVelocity, m_fRollVelocity;
    float m_afSuspLength[2];
    float m_afSuspVelocity[2];
    int m_iCrashed;
    float m_fSteer;
    float m_fThrottle;
    float m_fFrontBrake;
    float m_fRearBrake;
    float m_fClutch;
    float m_afWheelSpeed[2];
    int m_aiWheelMaterial[2];
    float m_afBrakePressure[2];
    float m_fSteerTorque;
};
// RunInit payload — player session info (used by stats for session start).
struct SPluginsBikeSession_t {
    int m_iSession;
    int m_iConditions;
    float m_fAirTemperature;
    char m_szSetupFileName[100];
};

// --- export signatures -------------------------------------------------------
typedef int  (*PFN_Startup)(char*);
typedef void (*PFN_Shutdown)();
typedef void (*PFN_Void_DS)(void*, int);                 // most (payload, size) callbacks
typedef void (*PFN_Class)(void*, int, void*, int);       // RaceClassification (header + array)
typedef void (*PFN_CountArray)(int, void*, int);         // RaceTrackPosition (count, array, elemSize)
typedef int  (*PFN_Spectate)(int, void*, int, int*);     // SpectateVehicles (count, array, curSel, out select)
typedef void (*PFN_Draw)(int, int*, void**, int*, void**);
typedef void (*PFN_Telemetry)(void*, int, float, float); // RunTelemetry (payload, size, time, centerlinePos)
typedef void (*PFN_TrackCenter)(int, void*, void*);      // TrackCenterline (numSegments, segments, raceData)

// Mirrors SPluginsTrackSegment_t (mxb_api.h) — the track centerline the game feeds
// once per session; the MapHud integrates it into the drawn track. Layout must match.
struct TrackSegmentRow {
    int   type = 0;        // 0 = straight, 1 = curve
    float length = 0.0f;   // meters
    float radius = 0.0f;   // meters, <0 = left curve, 0 = straight
    float angle = 0.0f;    // start heading in degrees, 0 = north
    float startX = 0.0f, startY = 0.0f;  // start position in meters (only segment 0 is the origin)
    float height = 0.0f;
};

// --- test-side convenience row ------------------------------------------------
// A classification row with race-typical defaults, so a test spells out only the
// fields it cares about: `{ .num = 10, .best = 90000, .gap = 1500 }`.
struct ClassRow {
    int num;
    int best       = 0;
    int laps       = 1;
    int gap        = 0;
    int gapLaps    = 0;
    int penalty    = 0;
    int state      = 0;
    int bestLapNum = 2;
    int pit        = 0;   // 0 = on track, 1 = in the pits
};

// A rider's position on the centerline for a RaceTrackPosition batch.
struct TrackRow {
    int   num;
    float trackPos;      // 0..1 along the centerline
    int   crashed = 0;
    float posX = 0.0f, posZ = 0.0f;  // world position (meters) — drives map zoom-follow
    float yaw = 0.0f;                 // heading in degrees — drives map rotate-to-player
};
