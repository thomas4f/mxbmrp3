// ============================================================================
// core/lap_delta_profile.h
// The display rider's gap to a reference lap, sampled across the lap: the
// shared read behind the Delta Trace HUD (hud/delta_trace_hud.h) and the Map's
// lap delta colouring (MapHud::refreshDeltaRate). Both draw the same numbers,
// so they sample them one way.
//
// Read-only over PluginData's PbGapTracker. Two laps: the lap in progress, as
// far as it has been ridden (while the gap is live), and the lap last completed
// (PbGapTracker::lastLapGaps). The new lap draws over the old one like a sweep:
// ahead of the rider, past a short gap, the previous lap is still there to
// compare with; in the pits, or anywhere the gap is not live, all of it is.
// ============================================================================
#pragma once

#include "pb_gap_tracker.h"

#include <algorithm>
#include <cmath>

struct LapDeltaProfile {
    // Every STEP-th tracker slot: 201 points across the lap.
    static constexpr int STEP = 5;
    static constexpr int POINTS = PbGapTracker::NUM_POINTS / STEP + 1;

    // Kept clear ahead of the rider so the sweep reads as one: 3% of the lap.
    static constexpr int OVERWRITE_GAP = 6;

    // The lap in progress, every point it has sampled.
    float gapMs[POINTS] = {};
    bool has[POINTS] = {};
    int last = -1;           // highest point sampled, -1 = nothing
    // The lap last completed, shown from prevFrom on.
    float prevGapMs[POINTS] = {};
    bool prevHas[POINTS] = {};
    int prevFrom = 0;        // first point of it to show: past the furthest point and the gap
    bool anyPrev = false;
    int maxAbsMs = 0;        // largest |gap| sampled, either lap
    bool live = false;       // the gap is live (else only the previous lap shows)

    // Refill from the tracker against `ref`; returns whether anything is shown.
    bool sample(PbGapTracker::Ref ref);

    // Track position (0..1, S/F-relative) of a point.
    static float pointPos(int p) { return static_cast<float>(p * STEP) / PbGapTracker::NUM_POINTS; }
};

// How fast time is being gained or lost at each point of a LapDeltaProfile:
// what the Map's lap delta paints on the track. The gap's slope over a window
// of +/-HALF_WINDOW points (3% of the lap) rather than one step, scaled against
// the largest slope on the lap so the colours use their full range on any
// pace, and read back per track position with linear interpolation between
// points - so the colour fades along the track instead of stepping every 0.5%.
// Each of the profile's two laps is its own series with its own scale; the
// previous lap reads at PREVIOUS_STRENGTH while the new one is drawing over it.
// Pure (no plugin state): pinned by tests/unit/test_lap_delta_rate.cpp.
struct LapDeltaRate {
    static constexpr int HALF_WINDOW = 3;
    // Below this slope (ms per point, i.e. 20 ms per 1% of the lap) nothing
    // reads as a swing, so an even lap stays close to the plain fill.
    static constexpr float MIN_SWING_MS_PER_POINT = 10.0f;
    static constexpr float PREVIOUS_STRENGTH = 0.5f;
    // While a lap is live, the colour fades in over this many points (2% of
    // the lap) on both sides of the gap - behind the rider's furthest point and
    // past where the previous lap starts - so the sweep's edges grow in
    // instead of each new point popping in at full colour.
    static constexpr int EDGE_FADE_POINTS = 4;

    struct Series {
        float rate[LapDeltaProfile::POINTS] = {};   // -1 gaining fastest .. +1 losing fastest
        bool has[LapDeltaProfile::POINTS] = {};
        bool any = false;

        void build(const float* gapMs, const bool* sampled, int from, int to) {
            std::fill(std::begin(has), std::end(has), false);
            any = false;
            float slope[LapDeltaProfile::POINTS] = {};
            float swing = MIN_SWING_MS_PER_POINT;
            for (int p = from; p <= to; ++p) {
                if (!sampled[p]) continue;
                int lo = std::max(from, p - HALF_WINDOW);
                int hi = std::min(to, p + HALF_WINDOW);
                while (lo < p && !sampled[lo]) ++lo;
                while (hi > p && !sampled[hi]) --hi;
                if (hi <= lo) continue;
                slope[p] = (gapMs[hi] - gapMs[lo]) / static_cast<float>(hi - lo);
                has[p] = true;
                swing = std::max(swing, std::fabs(slope[p]));
            }
            for (int p = from; p <= to; ++p) {
                if (!has[p]) continue;
                rate[p] = std::clamp(slope[p] / swing, -1.0f, 1.0f);
                any = true;
            }
        }

        // Interpolated between the two points around `pos`; where only one of
        // them has data, the nearer one decides; false where neither does.
        bool at(float pos, float& out) const {
            const float f = std::clamp(pos, 0.0f, 1.0f) * static_cast<float>(LapDeltaProfile::POINTS - 1);
            const int i0 = std::min(LapDeltaProfile::POINTS - 1, static_cast<int>(f));
            const int i1 = std::min(LapDeltaProfile::POINTS - 1, i0 + 1);
            const float frac = f - static_cast<float>(i0);
            if (has[i0] && has[i1]) { out = rate[i0] + (rate[i1] - rate[i0]) * frac; return true; }
            if (has[i0] && frac < 0.5f) { out = rate[i0]; return true; }
            if (has[i1] && frac >= 0.5f) { out = rate[i1]; return true; }
            return false;
        }
    };

    Series current;
    Series previous;
    float previousFrom = 0.0f;   // track position the previous lap shows from
    float previousScale = 1.0f;  // PREVIOUS_STRENGTH while a lap is live, else 1
    float currentTo = 2.0f;      // the lap in progress's furthest point while live (no fade otherwise)
    bool fadeEdges = false;      // a lap is live: fade both edges of the gap
    bool any = false;

    void build(const LapDeltaProfile& d) {
        current.build(d.gapMs, d.has, 0, d.last);
        previous.build(d.prevGapMs, d.prevHas, d.prevFrom, LapDeltaProfile::POINTS - 1);
        previousFrom = LapDeltaProfile::pointPos(d.prevFrom);
        previousScale = d.live ? PREVIOUS_STRENGTH : 1.0f;
        fadeEdges = d.live;
        currentTo = d.live && d.last >= 0 ? LapDeltaProfile::pointPos(d.last) : 2.0f;
        any = current.any || previous.any;
    }

    // The rate at track position `pos` (0..1 from the S/F line): the lap in
    // progress where it has reached, else the previous lap past the gap.
    // Near the gap's edges the rate is scaled by the square of the edge fade,
    // so weight() - the tint's strength - rises linearly across the fade.
    bool at(float pos, float& out) const {
        if (current.at(pos, out)) {
            out *= edgeFade(currentTo - pos);
            return true;
        }
        if (pos >= previousFrom && previous.at(pos, out)) {
            out *= previousScale * edgeFade(pos - previousFrom);
            return true;
        }
        return false;
    }
    // 0 at the gap's edge, 1 from EDGE_FADE_POINTS inside it on; 1 everywhere
    // when no lap is live. `distance` is in track position (0..1).
    float edgeFade(float distance) const {
        if (!fadeEdges) return 1.0f;
        const float t = std::clamp(distance / LapDeltaProfile::pointPos(EDGE_FADE_POINTS), 0.0f, 1.0f);
        return t * t;
    }
    // How strongly a rate tints the track: 0 at an even pace, 1 at the lap's
    // largest swing. The square root lifts small swings so they stay visible.
    static float weight(float r) { return std::sqrt(std::min(1.0f, std::fabs(r))); }
};
