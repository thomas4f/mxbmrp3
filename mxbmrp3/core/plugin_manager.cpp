// ============================================================================
// core/plugin_manager.cpp
// Main entry point and coordinator for all plugin lifecycle events
// ============================================================================
#include "plugin_manager.h"
#include "plugin_constants.h"
#include "plugin_data.h"
#include "plugin_utils.h"
#include "../diagnostics/logger.h"
#include "../diagnostics/timer.h"
#include "hud_manager.h"
#include "input_manager.h"
#include "hotkey_manager.h"
#include "asset_manager.h"
#include "../handlers/draw_handler.h"
#include "../handlers/event_handler.h"
#include "../handlers/race_event_handler.h"
#include "../handlers/race_session_handler.h"
#include "../handlers/race_entry_handler.h"
#include "../handlers/race_lap_handler.h"
#include "../handlers/race_split_handler.h"
#include "../handlers/race_classification_handler.h"
#include "../handlers/race_track_position_handler.h"
#include "../handlers/race_communication_handler.h"
#include "../handlers/run_handler.h"
#include "../handlers/run_lap_handler.h"
#include "../handlers/run_split_handler.h"
#include "../handlers/run_telemetry_handler.h"
#include "../handlers/track_centerline_handler.h"
#include "../handlers/race_vehicle_data_handler.h"
#include "../handlers/spectate_handler.h"
#include "personal_best_manager.h"
#include "tracked_riders_manager.h"
#include <cstring>
#include <vector>
#include <windows.h>

using namespace PluginConstants;

// RAII helper macro to automatically measure and accumulate callback execution time
// Usage: Add ACCUMULATE_CALLBACK_TIME() at the start of any plugin callback
#define ACCUMULATE_CALLBACK_TIME() \
    struct _ScopedCallbackTimer { \
        long long _start; \
        _ScopedCallbackTimer() : _start(DrawHandler::getCurrentTimeUs()) {} \
        ~_ScopedCallbackTimer() { DrawHandler::accumulateCallbackTime(DrawHandler::getCurrentTimeUs() - _start); } \
    } _cbtimer

PluginManager::PluginManager() {
    m_savePath[0] = '\0';
}

PluginManager& PluginManager::getInstance() {
    static PluginManager instance;
    return instance;
}

void PluginManager::initialize(const char* savePath) {
    // Initialize logger first (so we can log everything else)
    Logger::getInstance().initialize(savePath);

    // Discover assets (syncs user overrides, then scans plugin data directory)
    // Must happen before HudManager::initialize() which sets up resources
    AssetManager::getInstance().discoverAssets(savePath);

    // Initialize components
    InputManager::getInstance().initialize();
    HotkeyManager::getInstance().initialize();
    HudManager::getInstance().initialize();

    DEBUG_INFO("PluginManager initialized");
}

void PluginManager::shutdown() {
    DEBUG_INFO("PluginManager shutdown");

    // Shutdown HUD manager
    HudManager::getInstance().shutdown();

    // Shutdown input manager
    InputManager::getInstance().shutdown();

    // Clear plugin data store
    PluginData::getInstance().clear();

    // Shutdown logger last (so we can log everything else)
    Logger::getInstance().shutdown();
}

int PluginManager::handleStartup(const char* savePath) {
    // Safety: Check for null pointer from API
    if (savePath != nullptr) {
        strncpy_s(m_savePath, sizeof(m_savePath), savePath, sizeof(m_savePath) - 1);
        m_savePath[sizeof(m_savePath) - 1] = '\0';
    } else {
        m_savePath[0] = '\0';
    }

    // Initialize with savePath (Logger is initialized first and will log startup info)
    initialize(m_savePath);

    // Load personal bests from disk
    PersonalBestManager::getInstance().load(m_savePath);

    // Load tracked riders from disk
    TrackedRidersManager::getInstance().load(m_savePath);

    if (savePath != nullptr) {
        DEBUG_INFO_F("Startup called with save path: %s", savePath);
    } else {
        DEBUG_WARN("Startup called with NULL save path");
    }

    // NOTE: API docs say -1 = disable is valid, but game rejects it and unloads plugin
    // CHANGE TELEMETRY RATE HERE: Options are TELEMETRY_RATE_10HZ, _20HZ, _50HZ, _100HZ
    return TELEMETRY_RATE_100HZ;
}

void PluginManager::handleShutdown() {
    SCOPED_TIMER_THRESHOLD("Plugin::handleShutdown", 1000);
    DEBUG_INFO("=== Shutdown ===");
    shutdown();
    m_savePath[0] = '\0';
}

void PluginManager::handleEventInit(Unified::VehicleEventData* psEventData) {
    ACCUMULATE_CALLBACK_TIME();
    SCOPED_TIMER_THRESHOLD("Plugin::handleEventInit", 100);
    DEBUG_INFO("=== Event Init ===");

    EventHandler::getInstance().handleEventInit(psEventData);
}

void PluginManager::handleEventDeinit() {
    ACCUMULATE_CALLBACK_TIME();
    SCOPED_TIMER_THRESHOLD("Plugin::handleEventDeinit", 100);
    DEBUG_INFO("=== Event Deinit ===");

    EventHandler::getInstance().handleEventDeinit();
}

void PluginManager::handleRunInit(Unified::SessionData* psSessionData) {
    ACCUMULATE_CALLBACK_TIME();
    SCOPED_TIMER_THRESHOLD("Plugin::handleRunInit", 100);
    DEBUG_INFO("=== Run Init ===");

    RunHandler::getInstance().handleRunInit(psSessionData);
}

void PluginManager::handleRunDeinit() {
    ACCUMULATE_CALLBACK_TIME();
    SCOPED_TIMER_THRESHOLD("Plugin::handleRunDeinit", 100);
    DEBUG_INFO("=== Run Deinit ===");

    RunHandler::getInstance().handleRunDeinit();
}

void PluginManager::handleRunStart() {
    ACCUMULATE_CALLBACK_TIME();
    SCOPED_TIMER_THRESHOLD("Plugin::handleRunStart", 100);
    DEBUG_INFO("=== Run Start ===");

    RunHandler::getInstance().handleRunStart();
}

void PluginManager::handleRunStop() {
    ACCUMULATE_CALLBACK_TIME();
    SCOPED_TIMER_THRESHOLD("Plugin::handleRunStop", 100);
    DEBUG_INFO("=== Run Stop ===");

    RunHandler::getInstance().handleRunStop();

    // Window refresh and HUD validation now happens automatically
    // when cursor is re-enabled (see InputManager::updateFrame)
}

void PluginManager::handleRunLap(Unified::PlayerLapData* psLapData) {
    ACCUMULATE_CALLBACK_TIME();
    SCOPED_TIMER_THRESHOLD("Plugin::handleRunLap", 500);
    DEBUG_INFO("=== Run Lap ===");

    RunLapHandler::getInstance().handleRunLap(psLapData);
}

void PluginManager::handleRunSplit(Unified::PlayerSplitData* psSplitData) {
    ACCUMULATE_CALLBACK_TIME();
    SCOPED_TIMER_THRESHOLD("Plugin::handleRunSplit", 500);
    DEBUG_INFO("=== Run Split ===");

    RunSplitHandler::getInstance().handleRunSplit(psSplitData);
}

void PluginManager::handleRunTelemetry(Unified::TelemetryData* psTelemetryData) {
    ACCUMULATE_CALLBACK_TIME();
    SCOPED_TIMER_THRESHOLD("Plugin::handleRunTelemetry", 100);
    // Skip logging (high-frequency event - runs at telemetry rate)

    // Delegate to handler
    RunTelemetryHandler::getInstance().handleRunTelemetry(psTelemetryData);
}

int PluginManager::handleDrawInit(int* piNumSprites, char** pszSpriteName, int* piNumFonts, char** pszFontName) {
    SCOPED_TIMER_THRESHOLD("Plugin::handleDrawInit", 1000);
    DEBUG_INFO("=== Draw Init ===");

    // Safety: Check for null pointers from API
    if (piNumSprites == nullptr || pszSpriteName == nullptr ||
        piNumFonts == nullptr || pszFontName == nullptr) {
        DEBUG_WARN("handleDrawInit called with NULL pointer(s)");
        return 0;
    }

    // Delegate resource initialization to HudManager
    return HudManager::getInstance().initializeResources(piNumSprites, pszSpriteName, piNumFonts, pszFontName);
}

void PluginManager::handleDraw(int iState, int* piNumQuads, void** ppQuad, int* piNumString, void** ppString) {
    ACCUMULATE_CALLBACK_TIME();  // Measure this callback's execution time

    // Delegate to DrawHandler for performance tracking and rendering
    DrawHandler::getInstance().handleDraw(iState, piNumQuads, ppQuad, piNumString, ppString);
}

void PluginManager::handleTrackCenterline(int iNumSegments, Unified::TrackSegment* pasSegment, void* pRaceData) {
    ACCUMULATE_CALLBACK_TIME();
    SCOPED_TIMER_THRESHOLD("Plugin::handleTrackCenterline", 100);
    DEBUG_INFO("=== Track Centerline ===");

    TrackCenterlineHandler::getInstance().handleTrackCenterline(iNumSegments, pasSegment, pRaceData);
}

void PluginManager::handleRaceEvent(Unified::RaceEventData* psRaceEvent) {
    ACCUMULATE_CALLBACK_TIME();
    SCOPED_TIMER_THRESHOLD("Plugin::handleRaceEvent", 100);
    DEBUG_INFO("=== Race Event ===");

    RaceEventHandler::getInstance().handleRaceEvent(psRaceEvent);
}

void PluginManager::handleRaceDeinit() {
    ACCUMULATE_CALLBACK_TIME();
    SCOPED_TIMER_THRESHOLD("Plugin::handleRaceDeinit", 100);
    DEBUG_INFO("=== Race Deinit ===");

    // Delegate to handler
    RaceEventHandler::getInstance().handleRaceDeinit();
}

void PluginManager::handleRaceAddEntry(Unified::RaceEntryData* psRaceAddEntry) {
    ACCUMULATE_CALLBACK_TIME();
    SCOPED_TIMER_THRESHOLD("Plugin::handleRaceAddEntry", 500);
    DEBUG_INFO("=== Race Add Entry ===");

    RaceEntryHandler::getInstance().handleRaceAddEntry(psRaceAddEntry);
}

void PluginManager::handleRaceRemoveEntry(int raceNum) {
    ACCUMULATE_CALLBACK_TIME();
    SCOPED_TIMER_THRESHOLD("Plugin::handleRaceRemoveEntry", 100);
    DEBUG_INFO("=== Race Remove Entry ===");

    RaceEntryHandler::getInstance().handleRaceRemoveEntry(raceNum);
}

void PluginManager::handleRaceSession(Unified::RaceSessionData* psRaceSession) {
    ACCUMULATE_CALLBACK_TIME();
    SCOPED_TIMER_THRESHOLD("Plugin::handleRaceSession", 100);
    DEBUG_INFO("=== Race Session ===");

    RaceSessionHandler::getInstance().handleRaceSession(psRaceSession);
}

void PluginManager::handleRaceSessionState(Unified::RaceSessionStateData* psRaceSessionState) {
    ACCUMULATE_CALLBACK_TIME();
    SCOPED_TIMER_THRESHOLD("Plugin::handleRaceSessionState", 100);
    DEBUG_INFO("=== Race Session State ===");

    RaceSessionHandler::getInstance().handleRaceSessionState(psRaceSessionState);
}

void PluginManager::handleRaceLap(Unified::RaceLapData* psRaceLap) {
    ACCUMULATE_CALLBACK_TIME();
    SCOPED_TIMER_THRESHOLD("Plugin::handleRaceLap", 500);
    DEBUG_INFO("=== Race Lap ===");

    RaceLapHandler::getInstance().handleRaceLap(psRaceLap);
}

void PluginManager::handleRaceSplit(Unified::RaceSplitData* psRaceSplit) {
    ACCUMULATE_CALLBACK_TIME();
    SCOPED_TIMER_THRESHOLD("Plugin::handleRaceSplit", 500);
    DEBUG_INFO("=== Race Split ===");
    RaceSplitHandler::getInstance().handleRaceSplit(psRaceSplit);
}

void PluginManager::handleRaceHoleshot(Unified::RaceHoleshotData* psRaceHoleshot) {
    ACCUMULATE_CALLBACK_TIME();
    SCOPED_TIMER_THRESHOLD("Plugin::handleRaceHoleshot", 100);
    DEBUG_INFO("=== Race Holeshot ===");
    // TODO: Implement holeshot handling if needed
}

void PluginManager::handleRaceSpeed(Unified::RaceSpeedData* psRaceSpeed) {
    ACCUMULATE_CALLBACK_TIME();
    SCOPED_TIMER_THRESHOLD("Plugin::handleRaceSpeed", 100);
    DEBUG_INFO("=== Race Speed ===");
    // TODO: Implement race speed handling if needed (GP Bikes, WRS, KRP only)
}

void PluginManager::handleRaceCommunication(Unified::RaceCommunicationData* psRaceCommunication) {
    ACCUMULATE_CALLBACK_TIME();
    SCOPED_TIMER_THRESHOLD("Plugin::handleRaceCommunication", 500);
    DEBUG_INFO("=== Race Communication ===");

    RaceCommunicationHandler::getInstance().handleRaceCommunication(psRaceCommunication);
}

void PluginManager::handleRaceClassification(Unified::RaceClassificationData* psRaceClassification, Unified::RaceClassificationEntry* pasRaceClassificationEntry, int iNumEntries) {
    ACCUMULATE_CALLBACK_TIME();
    SCOPED_TIMER_THRESHOLD("Plugin::handleRaceClassification", 100);
    // Skip logging (high-frequency event)

    RaceClassificationHandler::getInstance().handleRaceClassification(
        psRaceClassification,
        pasRaceClassificationEntry,
        iNumEntries
    );
}

void PluginManager::handleRaceTrackPosition(int iNumVehicles, Unified::TrackPositionData* pasRaceTrackPosition) {
    ACCUMULATE_CALLBACK_TIME();
    // SCOPED_TIMER_THRESHOLD("Plugin::handleRaceTrackPosition", 500);  // Commented out - too noisy for debugging
    // Skip logging (high-frequency event - runs at vehicle update rate)

    // Null check and bounds validation moved to handler
    RaceTrackPositionHandler::getInstance().handleRaceTrackPosition(iNumVehicles, pasRaceTrackPosition);
}

void PluginManager::handleRaceVehicleData(Unified::RaceVehicleData* psRaceVehicleData) {
    ACCUMULATE_CALLBACK_TIME();
    SCOPED_TIMER_THRESHOLD("Plugin::handleRaceVehicleData", 500);
    // Skip logging (high-frequency event)

    RaceVehicleDataHandler::getInstance().handleRaceVehicleData(psRaceVehicleData);
}

int PluginManager::handleSpectateVehicles(int iNumVehicles, Unified::SpectateVehicle* pasVehicleData, int iCurSelection, int* piSelect) {
    return SpectateHandler::getInstance().handleSpectateVehicles(iNumVehicles, pasVehicleData, iCurSelection, piSelect);
}

int PluginManager::handleSpectateCameras(int iNumCameras, void* pCameraData, int iCurSelection, int* piSelect) {
    return SpectateHandler::getInstance().handleSpectateCameras(iNumCameras, pCameraData, iCurSelection, piSelect);
}

void PluginManager::requestSpectateRider(int raceNum) {
    SpectateHandler::getInstance().requestSpectateRider(raceNum);
}
