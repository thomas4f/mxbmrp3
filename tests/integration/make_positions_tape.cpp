// ============================================================================
// tests/integration/make_positions_tape.cpp
// Generate a FULLER callback tape than the committed slim fixtures: one that
// carries TrackCenterline + RaceTrackPosition + RunTelemetry, so replaying it
// lights up the map, rider markers, live gaps and telemetry HUDs (the slim
// fixtures are standings-only). We can't record a REAL in-game session headless,
// so this drives a synthetic 22-rider race through PluginHost with the plugin's
// OWN recorder active — the tape is byte-identical in FORMAT to a real capture
// (the recorder taps the same raw callbacks), just with synthetic data.
//
//   wine make_positions_tape.exe mxbmrp3_test.dlo Z:\tmp\out.tape [frames] [riders]
// ============================================================================
#include "plugin_host.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

// Closed "stadium" loop (two straights + two semicircles) ~1400 m, so the map
// has real 2D geometry to tessellate.
static std::vector<TrackSegmentRow> stadium() {
    const double PI = 3.14159265358979323846, R = 130.0, L = 300.0;
    const int straightN = 8, curveN = 24;
    std::vector<TrackSegmentRow> segs;
    auto addStraight = [&]{ for (int i = 0; i < straightN; ++i) { TrackSegmentRow s; s.type = 0; s.length = float(L / straightN); segs.push_back(s); } };
    auto addSemicircle = [&]{ for (int i = 0; i < curveN; ++i) { TrackSegmentRow s; s.type = 1; s.radius = float(R); s.length = float(PI * R / curveN); segs.push_back(s); } };
    addStraight(); addSemicircle(); addStraight(); addSemicircle();
    segs[0].startX = 0; segs[0].startY = 0; segs[0].angle = 0;
    return segs;
}

int main(int argc, char** argv) {
    const char* dll   = argc > 1 ? argv[1] : "mxbmrp3_test.dlo";
    const char* out   = argc > 2 ? argv[2] : "Z:\\tmp\\positions.tape";
    const int   FRAMES = argc > 3 ? atoi(argv[3]) : 600;
    const int   RIDERS = argc > 4 ? atoi(argv[4]) : 22;

    PluginHost host(dll);
    if (!host.loaded()) { fprintf(stderr, "failed to load %s\n", dll); return 1; }

    host.startup("Z:\\tmp\\mxbperf\\");
    if (!host.startRecording(out)) { fprintf(stderr, "startRecording(%s) failed\n", out); return 2; }

    host.eventInit("Farm-Synth", "Player", 1417.0f, /*type=*/2);
    host.raceEvent("Farm-Synth", /*type=*/2);
    host.session(6, /*numLaps=*/8);            // session 6 = Race1
    host.runInit(6);
    for (int i = 0; i < RIDERS; ++i) {
        char name[32]; snprintf(name, sizeof(name), "Rider %02d", i + 1);
        host.addEntry(i + 1, name);
    }
    host.trackCenterline(stadium(), { 0.0f, 470.0f, 940.0f });   // centerline + s/f + 2 splits

    // Initial standings.
    std::vector<ClassRow> cls(RIDERS);
    for (int i = 0; i < RIDERS; ++i) { cls[i].num = i + 1; cls[i].best = 90000 + i * 350; cls[i].laps = 1; cls[i].gap = i * 900; }
    host.classify(6, 0, cls);

    const double TWO_PI = 6.28318530717958647692;
    for (int f = 0; f < FRAMES; ++f) {
        std::vector<TrackRow> rows(RIDERS);
        for (int i = 0; i < RIDERS; ++i) {
            // Each rider advances along the loop, spread out on the grid.
            double tp = std::fmod(0.02 * f + double(i) / RIDERS, 1.0);
            rows[i].num = i + 1;
            rows[i].trackPos = float(tp);
            rows[i].posX = float(std::cos(tp * TWO_PI) * 220.0);
            rows[i].posZ = float(std::sin(tp * TWO_PI) * 220.0);
            rows[i].yaw  = float(std::fmod(tp * 360.0 + 90.0, 360.0));
        }
        host.raceTrackPosition(rows);
        host.telemetry(/*speedMs=*/28.0f, /*gear=*/4, /*time=*/f * 0.033f, /*pos=*/rows[0].trackPos);
        if ((f % 30) == 0) {   // periodic classification (standings refresh)
            for (int i = 0; i < RIDERS; ++i) cls[i].gap = (i * 900 + f * 5) % 60000;
            host.classify(6, f * 33, cls);
        }
        host.draw();
    }

    host.stopRecording();
    fprintf(stderr, "recorded %d frames x %d riders -> %s\n", FRAMES, RIDERS, out);
    return 0;
}
