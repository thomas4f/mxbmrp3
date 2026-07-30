// ============================================================================
// tests/integration/tape_bench_driver.cpp
// Run the developer benchmark against a REAL recorded session. Replays a .tape
// through PluginHost to reconstruct the actual game state (real riders, gaps,
// session), makes every HUD visible, then pumps Draw with the profiler active so
// the exported report's per-HUD RENDER FOOTPRINT reflects real data instead of a
// synthetic grid. The slim fixtures carry no Draw/TrackCenterline/positions, so
// the map/telemetry are empty here — this measures the STANDINGS-side footprint
// of a real field.
//   wine tape_bench_driver.exe mxbmrp3_test.dlo Z:\path\to.tape [max]
// ============================================================================
#include "plugin_host.h"
#include <cstdio>
#include <cstring>

int main(int argc, char** argv) {
    const char* dll  = argc > 1 ? argv[1] : "mxbmrp3_test.dlo";
    const char* tape = argc > 2 ? argv[2] : "Z:\\tmp\\mp.tape";
    bool maxSettings = (argc > 3 && strcmp(argv[3], "max") == 0);
    PluginHost host(dll);
    if (!host.loaded()) { fprintf(stderr, "failed to load %s\n", dll); return 1; }
    auto Bench   = host.sym<void(*)(int)>("MXBMRP3_Test_BenchmarkWidget");
    auto ShowAll = host.sym<void(*)(int)>("MXBMRP3_Test_ShowAllHuds");
    auto MaxSet  = host.sym<void(*)()>("MXBMRP3_Test_MaxHudSettings");
    if (!Bench) { fprintf(stderr, "missing MXBMRP3_Test_BenchmarkWidget (rebuild DLL)\n"); return 2; }

    host.startup("Z:\\tmp\\mxbperf\\");
    if (ShowAll) ShowAll(1);
    if (maxSettings && MaxSet) MaxSet();
    int n = host.replayTape(tape);
    fprintf(stderr, "replayed %d events from %s%s\n", n, tape, maxSettings ? " (max settings)" : "");
    if (n <= 0) { fprintf(stderr, "tape replay failed\n"); return 3; }

    Bench(1);                                   // activate profiler + reset
    for (int i = 0; i < 900; ++i) host.draw();  // pump frames so it samples the footprint
    Bench(0);                                   // deactivate -> writes the report
    fprintf(stderr, "done; report in <save>/mxbmrp3/benchmarks/\n");
    return 0;
}
