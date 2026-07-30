// ============================================================================
// tests/integration/perf_scenario.h
// Shared "worst realistic case" scenario for the CPU perf drivers: a full
// 50-rider grid on a long, complex circuit. Both perf_driver.cpp and
// map_perf_driver.cpp build the SAME track through buildPerfTrack() so the
// 480fps budget is measured against one demanding, representative map.
//
// GROUNDED IN REAL DATA. The heaviest committed real-game capture is
// tests/integration/tests/fixtures/race_farm14_24riders.tape.gz — Farm14,
// 1669 m of centerline, 24 riders (its EventInit carries the length; the tape
// was slimmed to MIN so it no longer carries the raw centerline, hence we
// synthesize rather than replay it here). We deliberately synthesize a HEAVIER
// superset than any real MX Bikes track:
//   * ~2400 m of centerline (> Farm14's 1669 m, past the long end of real tracks)
//   * ~950 short segments (hairpins, sweepers, chicanes/esses, long straights)
//   * a full PERF_RIDERS = 50 grid (> Farm14's 24), the API max
// so passing the gate proves margin over the worst real track + grid, not just
// the average one.
//
// TRACK GEOMETRY MODEL (see MapHud::calculateTrackBounds / advanceAlongArc):
// heading advances ONLY through CURVE segments (type != 0); STRAIGHT segments
// (type 0) extend along the current heading. Radius sign picks the turn
// direction (>0 / <0). We subdivide every feature into short ~2.5 m pieces to
// mimic the game's fine export granularity and to exercise the world ribbon's
// short-segment merge and adaptive curve subdivision (tight radii => more steps).
//
// Requires SPluginsTrackSegment_t to be defined by the includer.
//
// NOTE: this header deliberately uses ONLY the C stdlib (calloc) — no <vector>
// or other libstdc++ facilities — so the perf drivers stay self-contained and
// don't pull a libstdc++-6.dll dependency the Wine runner can't resolve at
// startup (that manifests as an exit-53 with no output before main runs).
// ============================================================================
#pragma once
#include <cstdlib>

// API maximum grid the plugin supports (MAX_RACE_ENTRIES). A full field is the
// worst case for standings, gaps, map markers and the JSON snapshot.
static const int PERF_RIDERS = 50;

// One feature of the circuit: a straight or a constant-radius arc.
struct PerfFeature { int type; float length; float radius; }; // type 0=straight,1=curve; radius signed

// Build the long/complex circuit. Allocates a calloc'd segment array the caller
// frees; returns the segment count and writes the total centerline length.
static int buildPerfTrack(SPluginsTrackSegment_t** outSegs, float* outTotalLen) {
    // Right turns use +radius, left turns -radius. Arc length is baked in below
    // from radius * angle so the geometry is self-consistent. A weaving mix of
    // tight hairpins, fast sweepers, chicane/esse pairs and long straights.
    static const PerfFeature FEATURES[] = {
        {0, 240.f,   0.f},   // start straight
        {1,  86.4f,  55.f},  // R sweeper 90deg
        {0, 130.f,   0.f},
        {1,  39.1f, -14.f},  // L hairpin
        {0,  80.f,   0.f},
        {1,  20.9f,  20.f},  // chicane R
        {1,  20.9f, -20.f},  // chicane L
        {0,  60.f,   0.f},
        {1, 100.5f,  48.f},  // R sweeper 120deg
        {0, 280.f,   0.f},   // long straight
        {1,  97.7f, -70.f},  // L sweeper 80deg
        {0,  90.f,   0.f},
        {1,  37.7f,  12.f},  // R hairpin 180deg
        {0, 110.f,   0.f},
        {1,  53.1f, -32.f},  // esse L
        {1,  31.8f,  26.f},  // esse R
        {1,  31.8f, -26.f},  // esse L
        {0, 150.f,   0.f},
        {1, 115.2f,  60.f},  // R sweeper 110deg
        {0, 200.f,   0.f},
        {1,  44.0f, -18.f},  // L hairpin 140deg
        {0, 120.f,   0.f},
        {1,  69.8f,  40.f},  // R 100deg
        {0, 175.f,   0.f},   // straight to line
    };
    const int NUM_FEATURES = (int)(sizeof(FEATURES) / sizeof(FEATURES[0]));
    const float SUBSEG_M = 2.5f;   // export-like granularity

    // Pass 1: count the subdivided segments so we can size one calloc.
    int n = 0;
    for (int f = 0; f < NUM_FEATURES; ++f) {
        int k = (int)(FEATURES[f].length / SUBSEG_M);
        if (k < 1) k = 1;
        n += k;
    }

    // Pass 2: fill.
    SPluginsTrackSegment_t* arr =
        (SPluginsTrackSegment_t*)calloc(n, sizeof(SPluginsTrackSegment_t));
    float total = 0.f;
    int idx = 0;
    for (int f = 0; f < NUM_FEATURES; ++f) {
        const PerfFeature& ft = FEATURES[f];
        int k = (int)(ft.length / SUBSEG_M);
        if (k < 1) k = 1;
        float subLen = ft.length / (float)k;
        for (int j = 0; j < k; ++j) {
            arr[idx].m_iType   = ft.type;
            arr[idx].m_fLength = subLen;
            arr[idx].m_fRadius = ft.radius;   // ignored for straights
            arr[idx].m_fAngle  = 0.f;         // only segment[0].angle is read (start heading)
            ++idx;
            total += subLen;
        }
    }

    *outSegs = arr;
    if (outTotalLen) *outTotalLen = total;
    return n;
}
