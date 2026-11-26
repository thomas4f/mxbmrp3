// ============================================================================
// handlers/track_centerline_handler.cpp
// Processes track centerline data for map rendering
// ============================================================================
#include "track_centerline_handler.h"
#include "../core/hud_manager.h"
#include "../core/plugin_data.h"
#include "../diagnostics/logger.h"
#include <vector>
#include <cmath>

void Handlers::handleTrackCenterline(int iNumSegments, Unified::TrackSegment* pasSegment, void* pRaceData) {
    // Safety: Validate track centerline data before processing. A null pointer,
    // non-positive count, or an implausibly large count (garbage / game-plugin
    // struct skew) would drive an out-of-bounds read of the unsized segment array
    // below and in MapHud, crashing the host game. Reject rather than process --
    // an implausible count means the whole array is untrustworthy. Mirrors the
    // count clamp on the other array-style callbacks (see RaceTrackPosition).
    if (!pasSegment || iNumSegments <= 0 || iNumSegments > Unified::MAX_TRACK_SEGMENTS) {
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

    // Publish the official split positions to PluginData so the segment timer can
    // snap a new boundary onto a nearby split. raceData gives meters along the
    // centerline (measured from the centerline's data start), but the player's
    // trackPos is 0 at start/finish (raceData[0] meters in). Convert each split to
    // the same S/F-relative 0-1 frame the player's trackPos uses, so snapping and
    // crossing compare like for like. Index 0 is S/F, then the game's splits.
    std::vector<float> splitPositions;
    if (raceData) {
        float totalLength = 0.0f;
        for (int i = 0; i < iNumSegments; ++i) totalLength += pasSegment[i].length;
        // As the map reads them (MapHud): a start/finish not above 0 m sits at
        // the centerline's start, and each split above 0 m stands on its own.
        // Requiring the S/F as well dropped every split on such tracks, so the
        // Gap Bar and the Timing deck waited for a crossing to learn them.
        const float sfMeters = raceData[0] > 0.0f ? raceData[0] : 0.0f;
        if (totalLength > 0.0f) {
            splitPositions.push_back(0.0f);   // entry 0: the start/finish line itself
            // One split fewer than the game has sectors (GP Bikes 3, the rest 2).
            for (int i = 1; i < GAME_SECTOR_COUNT; ++i) {
                const float meters = raceData[i];
                if (meters > 0.0f && meters <= totalLength) {
                    float tp = (meters - sfMeters) / totalLength;
                    tp -= std::floor(tp);  // wrap into [0,1)
                    splitPositions.push_back(tp);
                }
            }
        }
    }
    PluginData::getInstance().setSplitPositions(splitPositions);
}
