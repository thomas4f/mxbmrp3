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

    // Forward track centerline data to HudManager for MapHud
    HudManager::getInstance().updateTrackCenterline(iNumSegments, pasSegment);
}
