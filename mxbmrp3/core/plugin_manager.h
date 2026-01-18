// ============================================================================
// core/plugin_manager.h
// Main entry point and coordinator for all plugin lifecycle events
// ============================================================================
#pragma once

#include "../game/unified_types.h"

class PluginManager {
public:
    static PluginManager& getInstance();
    void initialize(const char* savePath);
    void shutdown();

    int handleStartup(const char* savePath);
    void handleShutdown();
    void handleEventInit(Unified::VehicleEventData* psEventData);
    void handleEventDeinit();
    void handleRunInit(Unified::SessionData* psSessionData);
    void handleRunDeinit();
    void handleRunStart();
    void handleRunStop();
    void handleRunLap(Unified::PlayerLapData* psLapData);
    void handleRunSplit(Unified::PlayerSplitData* psSplitData);
    void handleRunTelemetry(Unified::TelemetryData* psTelemetryData);
    int handleDrawInit(int* piNumSprites, char** pszSpriteName, int* piNumFonts, char** pszFontName);
    void handleDraw(int iState, int* piNumQuads, void** ppQuad, int* piNumString, void** ppString);
    void handleTrackCenterline(int iNumSegments, Unified::TrackSegment* pasSegment, void* pRaceData);
    void handleRaceEvent(Unified::RaceEventData* psRaceEvent);
    void handleRaceDeinit();
    void handleRaceAddEntry(Unified::RaceEntryData* psRaceAddEntry);
    void handleRaceRemoveEntry(int raceNum);
    void handleRaceSession(Unified::RaceSessionData* psRaceSession);
    void handleRaceSessionState(Unified::RaceSessionStateData* psRaceSessionState);
    void handleRaceLap(Unified::RaceLapData* psRaceLap);
    void handleRaceSplit(Unified::RaceSplitData* psRaceSplit);
    void handleRaceHoleshot(Unified::RaceHoleshotData* psRaceHoleshot);
    void handleRaceSpeed(Unified::RaceSpeedData* psRaceSpeed);
    void handleRaceCommunication(Unified::RaceCommunicationData* psRaceCommunication);
    void handleRaceClassification(Unified::RaceClassificationData* psRaceClassification, Unified::RaceClassificationEntry* pasRaceClassificationEntry, int iNumEntries);
    void handleRaceTrackPosition(int iNumVehicles, Unified::TrackPositionData* pasRaceTrackPosition);
    void handleRaceVehicleData(Unified::RaceVehicleData* psRaceVehicleData);
    int handleSpectateVehicles(int iNumVehicles, Unified::SpectateVehicle* pasVehicleData, int iCurSelection, int* piSelect);
    int handleSpectateCameras(int iNumCameras, void* pCameraData, int iCurSelection, int* piSelect);

    // Get save path provided by the game
    const char* getSavePath() const { return m_savePath; }

    // Request to spectate a specific rider by race number (delegates to SpectateHandler)
    void requestSpectateRider(int raceNum);

private:
    PluginManager();
    ~PluginManager() { shutdown(); }
    PluginManager(const PluginManager&) = delete;
    PluginManager& operator=(const PluginManager&) = delete;

    char m_savePath[260];  // MAX_PATH on Windows
};
