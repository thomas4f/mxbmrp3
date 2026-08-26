// ============================================================================
// hud/radar_fade.h
// The radar's auto-hide fade: how visible the radar should be given who is
// nearby. Pure math over rider positions — no PluginData, no rendering.
//
// TWO THINGS THAT ARE EASY TO GET WRONG, and the reason this is worth a test:
//
//   1. TRACK POSITION WRAPS. trackPos is normalised 0..1 around the lap, so two
//      riders either side of the start/finish line (0.99 and 0.01) are two
//      hundredths apart, not ninety-eight. Subtracting without folding at 0.5
//      makes the radar blink off exactly as a rider crosses the line — the most
//      visible moment on the lap.
//
//   2. RIDERS ARE GATED TWICE, on different distances. Straight-line distance
//      rejects someone physically far away; track distance then rejects someone
//      standing metres away across a barrier but half a lap apart on the racing
//      line. Dropping either gate makes the radar light up for riders the
//      player cannot interact with — on a tight infield that is most of them.
//
// THE trackLength == 0 FALLBACK is not a rounding detail: track length is
// unknown until the session data arrives, and without it there is no way to turn
// a normalised track separation into metres. The fallback fades over a fixed 5%
// of the lap instead, so the radar behaves sanely in the first seconds of a
// session rather than treating everyone as adjacent.
//
// PERFORMANCE CONTRACT: this runs per radar rebuild with up to a full field, so
// it reads the caller's OWN `Unified::TrackPositionData` array in place — no
// copy into a parallel sample array, nothing allocated here, and no map lookup
// per rider. Taking the game type costs this header one include of a
// dependency-free POD header; it is worth it to keep the hot path a flat walk.
//
// Pinned by tests/unit/test_radar_fade.cpp (~1s, no game).
// ============================================================================
#pragma once

#include "../game/unified_types.h"
#include "../core/plugin_utils.h"

#include <algorithm>
#include <cmath>

namespace RadarFade {

// Fade window used when track length is not yet known: 5% of a lap.
constexpr float kFallbackTrackFraction = 0.05f;

// Shortest separation between two normalised lap positions, accounting for the
// wrap at the start/finish line. Always in [0, 0.5].
// trackSeparation() is plugin_utils.h's, not a second copy here: this header
// used to define its own, identical down to the wrap boundary, and two of them
// is one that can drift. Included above, called unqualified.

// The radar's opacity multiplier: the largest contribution of any rider close
// enough to count, or 0 when nobody is.
//
// displayRaceNum - the rider being displayed for; skipped (never fades itself in)
// rangeMeters    - the configured radar range, used for BOTH gates
// trackLength    - metres per lap, or <= 0 when not yet known (fallback above)
inline float maxRiderOpacity(const Unified::TrackPositionData* riders, int count,
                             int displayRaceNum,
                             float playerX, float playerZ, float playerTrackPos,
                             float rangeMeters, float trackLength) {
    float maxOpacity = 0.0f;
    if (riders == nullptr) return maxOpacity;

    for (int i = 0; i < count; ++i) {
        const Unified::TrackPositionData& r = riders[i];
        if (r.raceNum == displayRaceNum) continue;

        // Gate 1: straight-line distance.
        const float relX = r.posX - playerX;
        const float relZ = r.posZ - playerZ;
        const float distance = std::sqrt(relX * relX + relZ * relZ);
        if (distance > rangeMeters) continue;

        // Gate 2: distance along the racing line, which is what actually decides
        // whether these two riders are racing each other.
        const float trackDist = trackSeparation(r.trackPos, playerTrackPos);

        float trackFadeOpacity;
        if (trackLength > 0.0f) {
            const float trackDistMeters = trackDist * trackLength;
            if (trackDistMeters >= rangeMeters) continue;
            trackFadeOpacity = 1.0f - (trackDistMeters / rangeMeters);
        } else {
            if (trackDist >= kFallbackTrackFraction) continue;
            trackFadeOpacity = 1.0f - (trackDist / kFallbackTrackFraction);
        }

        maxOpacity = std::max(maxOpacity, trackFadeOpacity);
    }
    return maxOpacity;
}

}  // namespace RadarFade
