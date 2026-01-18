// ============================================================================
// handlers/spectate_handler.h
// Manages spectate mode vehicle and camera selection
// ============================================================================
#pragma once

#include "../game/unified_types.h"

class SpectateHandler {
public:
    static SpectateHandler& getInstance();

    // Handle spectate vehicle selection callback
    // Returns 1 if selection changed, 0 otherwise
    int handleSpectateVehicles(int iNumVehicles, Unified::SpectateVehicle* pasVehicleData, int iCurSelection, int* piSelect);

    // Handle spectate camera selection callback
    // Returns 1 if selection changed, 0 otherwise
    int handleSpectateCameras(int iNumCameras, void* pCameraData, int iCurSelection, int* piSelect);

    // Request to spectate a specific rider by race number
    void requestSpectateRider(int raceNum);

private:
    SpectateHandler() {}
    ~SpectateHandler() {}
    SpectateHandler(const SpectateHandler&) = delete;
    SpectateHandler& operator=(const SpectateHandler&) = delete;

    int m_requestedSpectateRaceNum = -1;  // Requested race number to spectate (-1 = none)
};
