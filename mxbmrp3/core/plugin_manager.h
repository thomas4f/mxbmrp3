// ============================================================================
// core/plugin_manager.h
// Main entry point and coordinator for all plugin lifecycle events from MX Bikes API
// ============================================================================
#pragma once

#include "../vendor/piboso/mxb_api.h"

class PluginManager {
public:
    static PluginManager& getInstance();
    void initialize(const char* savePath);
    void shutdown();

    int handleStartup(const char* savePath);
    void handleShutdown();
    void handleEventInit(SPluginsBikeEvent_t* psEventData);
    void handleEventDeinit();
    void handleRunInit(SPluginsBikeSession_t* psSessionData);
    void handleRunDeinit();
    void handleRunStart();
    void handleRunStop();
    void handleRunLap(SPluginsBikeLap_t* psLapData);
    void handleRunSplit(SPluginsBikeSplit_t* psSplitData);
    void handleRunTelemetry(SPluginsBikeData_t* psBikeData, float fTime, float fPos);
    int handleDrawInit(int* piNumSprites, char** pszSpriteName, int* piNumFonts, char** pszFontName);
    void handleDraw(int iState, int* piNumQuads, void** ppQuad, int* piNumString, void** ppString);
    void handleTrackCenterline(int iNumSegments, SPluginsTrackSegment_t* pasSegment, void* pRaceData);
    void handleRaceEvent(SPluginsRaceEvent_t* psRaceEvent);
    void handleRaceDeinit();
    void handleRaceAddEntry(SPluginsRaceAddEntry_t* psRaceAddEntry);
    void handleRaceRemoveEntry(SPluginsRaceRemoveEntry_t* psRaceRemoveEntry);
    void handleRaceSession(SPluginsRaceSession_t* psRaceSession);
    void handleRaceSessionState(SPluginsRaceSessionState_t* psRaceSessionState);
    void handleRaceLap(SPluginsRaceLap_t* psRaceLap);
    void handleRaceSplit(SPluginsRaceSplit_t* psRaceSplit);
    void handleRaceHoleshot(SPluginsRaceHoleshot_t* psRaceHoleshot);
    void handleRaceCommunication(SPluginsRaceCommunication_t* psRaceCommunication, int dataSize);
    void handleRaceClassification(SPluginsRaceClassification_t* psRaceClassification, SPluginsRaceClassificationEntry_t* pasRaceClassificationEntry, int iNumEntries);
    void handleRaceTrackPosition(int iNumVehicles, SPluginsRaceTrackPosition_t* pasRaceTrackPosition);
    void handleRaceVehicleData(SPluginsRaceVehicleData_t* psRaceVehicleData);
    int handleSpectateVehicles(int iNumVehicles, SPluginsSpectateVehicle_t* pasVehicleData, int iCurSelection, int* piSelect);
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
