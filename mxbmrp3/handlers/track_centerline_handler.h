// ============================================================================
// handlers/track_centerline_handler.h
// Processes track centerline data for map rendering
// ============================================================================
#pragma once

#include "../game/unified_types.h"

namespace Handlers {

void handleTrackCenterline(int iNumSegments, Unified::TrackSegment* pasSegment, void* pRaceData);

}  // namespace Handlers
