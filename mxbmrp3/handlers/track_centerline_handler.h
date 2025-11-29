// ============================================================================
// handlers/track_centerline_handler.h
// Processes track centerline data for map rendering
// ============================================================================
#pragma once

#include "../vendor/piboso/mxb_api.h"

class TrackCenterlineHandler {
public:
    static TrackCenterlineHandler& getInstance();

    void handleTrackCenterline(int iNumSegments, SPluginsTrackSegment_t* pasSegment, void* pRaceData);

private:
    TrackCenterlineHandler() {}
    ~TrackCenterlineHandler() {}
    TrackCenterlineHandler(const TrackCenterlineHandler&) = delete;
    TrackCenterlineHandler& operator=(const TrackCenterlineHandler&) = delete;
};
