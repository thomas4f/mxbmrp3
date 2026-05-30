// ============================================================================
// handlers/track_centerline_handler.cpp
// Processes track centerline data for map rendering
// ============================================================================
#include "track_centerline_handler.h"
#include "../core/handler_singleton.h"
#include "../core/hud_manager.h"
#include "../diagnostics/logger.h"

DEFINE_HANDLER_SINGLETON(TrackCenterlineHandler)

void TrackCenterlineHandler::handleTrackCenterline(int iNumSegments, Unified::TrackSegment* pasSegment, void* pRaceData) {
    // Safety: Validate track centerline data before processing
    // Null pointer or invalid segment count could cause crash in MapHud rendering
    if (!pasSegment || iNumSegments <= 0) {
        DEBUG_WARN_F("Invalid track centerline data (segments=%d, ptr=%p)", iNumSegments, static_cast<void*>(pasSegment));
        return;
    }

    // pRaceData is a float array of fixed length 4 for the supported games:
    //   [0] = start/finish line (meters along centerline)
    //   [1] = split 1
    //   [2] = split 2
    //   [3] = holeshot (MX Bikes only; other games may not populate)
    // Values <= 0 or > track length are treated as "not available".
    const float* raceData = static_cast<const float*>(pRaceData);

    // Forward track centerline data to HudManager for MapHud
    HudManager::getInstance().updateTrackCenterline(iNumSegments, pasSegment, raceData);
}
