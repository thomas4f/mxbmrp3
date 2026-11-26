// ============================================================================
// core/lap_purity.h
// Grounded and Flat Out: a clean lap ridden a particular way. The two hidden
// rows ask for a lap with no flight in it, and a lap without touching either
// brake. Clean is the same for both: valid, not crashed in, and watched. That
// is the whole rule, which is what each row's sentence says.
//
// NO PACE AND NO "TRACK HAS JUMPS" GATE. Both once also asked for the lap to
// be within 10% of the PB, and Grounded for three flights this session first.
// Both were dropped (his call, 11 Nov 2026): any clean lap can do it, and a
// flat track earns Grounded.
//
// AND THE LAP WAS WATCHED: the odometer measured at least MIN_COVERED of the
// track length during it. Without that a lap the telemetry never saw (a
// stall, a replay, a lap timed while the plugin was not fed) has no flight
// and no brake in it simply because nothing was looking.
//
// THE FLAGS ONLY EVER OVER-COVER. StatsManager clears them at each lap line
// and at every run start, so the first lap after a pit exit carries whatever
// happened before its line too: that can only cost a qualifying lap, never
// grant one that did not qualify.
// Pinned by tests/unit/test_achievement_feeds.cpp.
// ============================================================================
#pragma once

namespace LapPurity {

constexpr float BRAKE_TOUCH = 0.05f;    // a lever brushed, not a finger resting on it
constexpr double MIN_COVERED = 0.8;     // of the track length, measured by the odometer

struct Lap {
    bool flew = false;      // a flight (FmxManager's, so chatter is not one) during it
    bool braked = false;    // either brake past BRAKE_TOUCH during it
};

inline bool watched(double lapDistanceM, double trackLengthM) {
    return trackLengthM > 0.0 && lapDistanceM >= trackLengthM * MIN_COVERED;
}

// `clean` = valid, not crashed in, and watched: what both rows ask first.
inline bool grounded(const Lap& lap, bool clean) { return clean && !lap.flew; }
inline bool flatOut(const Lap& lap, bool clean) { return clean && !lap.braked; }

}  // namespace LapPurity
