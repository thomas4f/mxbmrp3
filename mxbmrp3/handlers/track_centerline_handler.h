// ============================================================================
// handlers/track_centerline_handler.h
// Processes track centerline data for map rendering
// ============================================================================
#pragma once

#include "../game/unified_types.h"

class TrackCenterlineHandler {
public:
    static TrackCenterlineHandler& getInstance();

    void handleTrackCenterline(int iNumSegments, Unified::TrackSegment* pasSegment, void* pRaceData);

private:
    TrackCenterlineHandler() {}
    ~TrackCenterlineHandler() {}
    TrackCenterlineHandler(const TrackCenterlineHandler&) = delete;
    TrackCenterlineHandler& operator=(const TrackCenterlineHandler&) = delete;
};
