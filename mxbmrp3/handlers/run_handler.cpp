// ============================================================================
// handlers/run_handler.cpp
// Processes run lifecycle data (run init/deinit/start/stop)
// ============================================================================
#include "run_handler.h"
#include <cstring>
#include "../core/handler_singleton.h"
#include "../core/plugin_data.h"
#include "../core/input_manager.h"
#include "../core/hud_manager.h"
#include "../core/stats_manager.h"
#include "../core/settings_manager.h"
#include "../hud/fuel_widget.h"
#include "../diagnostics/logger.h"

DEFINE_HANDLER_SINGLETON(RunHandler)

void RunHandler::handleRunInit(Unified::SessionData* psSessionData) {
    HANDLER_NULL_CHECK(psSessionData);

    // Event logging now handled by PluginManager

    // Update plugin data store
    PluginData::getInstance().setSession(psSessionData->session);
    PluginData::getInstance().setConditions(static_cast<int>(psSessionData->conditions));
    PluginData::getInstance().setAirTemperature(psSessionData->airTemperature);
    PluginData::getInstance().setTrackTemperature(psSessionData->trackTemperature);
    PluginData::getInstance().setSetupFileName(psSessionData->setupFileName);

    // Warn if using default setup (empty or "Default")
    if (psSessionData->setupFileName[0] == '\0' ||
        strcmp(psSessionData->setupFileName, "Default") == 0) {
        PluginData::getInstance().notifyDefaultSetup();
    }

    // Reset fuel tracking when entering track (rider may have refueled in pits)
    HudManager::getInstance().getFuelWidget().resetFuelTracking();

    // Start stats session tracking (pass session type so stats only reset on session change, not pit stops)
    StatsManager::getInstance().recordSessionStart(psSessionData->session);
}

void RunHandler::handleRunStart() {
    // Event logging now handled by PluginManager

    // Set player running flag (cleared in RunStop/RunDeinit)
    PluginData::getInstance().setPlayerRunning(true);

    // Resume stats session timer (paused in RunStop)
    StatsManager::getInstance().notifyResume();

    // Reset fuel tracking for new run
    HudManager::getInstance().getFuelWidget().resetFuelTracking();

    // Refresh window information at run start to detect any resolution changes
    // that might have happened while in menus
    DEBUG_INFO("Run started - refreshing window information");
    InputManager::getInstance().forceWindowRefresh();
}

void RunHandler::handleRunStop() {
    // Event logging now handled by PluginManager

    // Clear player running flag
    PluginData::getInstance().setPlayerRunning(false);

    // Pause stats session timer (resumed in RunStart)
    StatsManager::getInstance().notifyPause();

    // Leaving the track for the pits: persist deferred settings + stats now. This is where the
    // ~2ms settings serialize lands — a frame hitch here is invisible, and we NEVER write while
    // the player is actively riding. Both are no-ops if nothing changed. (RunDeinit repeats this
    // for a direct exit that skips the pit stop.)
    SettingsManager::getInstance().flushIfDirty(HudManager::getInstance());
    StatsManager::getInstance().save();
}

void RunHandler::handleRunDeinit() {
    // Event logging now handled by PluginManager

    // Clear player running flag
    PluginData::getInstance().setPlayerRunning(false);

    // Record race finish if player actually completed the race
    StatsManager::getInstance().tryRecordRaceFinish(PluginData::getInstance());

    // End stats session and save
    StatsManager::getInstance().recordSessionEnd();
    StatsManager::getInstance().save();

    // Exiting the run: flush any deferred settings changes (no-op if nothing changed).
    SettingsManager::getInstance().flushIfDirty(HudManager::getInstance());
}
