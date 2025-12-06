// ============================================================================
// handlers/run_handler.cpp
// Processes run lifecycle data (run init/deinit/start/stop)
// ============================================================================
#include "run_handler.h"
#include "../core/handler_singleton.h"
#include "../core/plugin_data.h"
#include "../core/input_manager.h"
#include "../core/hud_manager.h"
#include "../hud/fuel_widget.h"
#include "../diagnostics/logger.h"

DEFINE_HANDLER_SINGLETON(RunHandler)

void RunHandler::handleRunInit(SPluginsBikeSession_t* psSessionData) {
    HANDLER_NULL_CHECK(psSessionData);

    // Event logging now handled by PluginManager

    // Update plugin data store
    PluginData::getInstance().setSession(psSessionData->m_iSession);
    PluginData::getInstance().setConditions(psSessionData->m_iConditions);
    PluginData::getInstance().setAirTemperature(psSessionData->m_fAirTemperature);
    PluginData::getInstance().setSetupFileName(psSessionData->m_szSetupFileName);
}

void RunHandler::handleRunStart() {
    // Event logging now handled by PluginManager

    // Set player running flag (cleared in RunStop/RunDeinit)
    PluginData::getInstance().setPlayerRunning(true);

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
}

void RunHandler::handleRunDeinit() {
    // Event logging now handled by PluginManager

    // Clear player running flag
    PluginData::getInstance().setPlayerRunning(false);
}
