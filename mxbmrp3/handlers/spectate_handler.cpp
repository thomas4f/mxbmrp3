// ============================================================================
// handlers/spectate_handler.cpp
// Manages spectate mode vehicle and camera selection
// ============================================================================
#include "spectate_handler.h"
#include "../core/handler_singleton.h"
#include "../core/plugin_data.h"
#include "../diagnostics/logger.h"

DEFINE_HANDLER_SINGLETON(SpectateHandler)

int SpectateHandler::handleSpectateVehicles(int iNumVehicles, Unified::SpectateVehicle* pasVehicleData, int iCurSelection, int* piSelect) {
    // Track the currently spectated rider
    if (iCurSelection >= 0 && iCurSelection < iNumVehicles && pasVehicleData != nullptr) {
        int spectatedRaceNum = pasVehicleData[iCurSelection].raceNum;
        PluginData::getInstance().setSpectatedRaceNum(spectatedRaceNum);
    }

    // Check if a spectate switch was requested
    if (m_requestedSpectateRaceNum >= 0 && pasVehicleData != nullptr && piSelect != nullptr) {
        // Find the rider with the requested race number
        for (int i = 0; i < iNumVehicles; ++i) {
            if (pasVehicleData[i].raceNum == m_requestedSpectateRaceNum) {
                // Found the rider - switch to them
                DEBUG_INFO_F("Spectating rider #%d (%s)", m_requestedSpectateRaceNum, pasVehicleData[i].name);
                *piSelect = i;
                m_requestedSpectateRaceNum = -1;  // Clear the request
                return 1;  // Selection changed
            }
        }
        // Rider not found - clear the invalid request
        m_requestedSpectateRaceNum = -1;
    }

    // No change requested or no match found
    return 0;
}

int SpectateHandler::handleSpectateCameras(int iNumCameras, void* pCameraData, int iCurSelection, int* piSelect) {
    // No camera switching logic implemented yet
    return 0;
}

void SpectateHandler::requestSpectateRider(int raceNum) {
    m_requestedSpectateRaceNum = raceNum;
}
