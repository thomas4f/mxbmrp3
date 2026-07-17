// ============================================================================
// tests/integration/callback_fuzzer.cpp
// Crash-grade fuzzer for the DLL boundary. Loads the plugin, calls Startup, then
// hammers every data callback (Race*/Run*/Event*/Spectate*/Draw) with adversarial
// sizes, counts, element sizes and random bytes. A crash here would crash the
// HOST GAME in production, so the contract under test is: no input, however
// malformed, may fault or throw across the boundary — API_GUARD_CATCH plus the
// size/count clamps must absorb everything.
//
// Many iterations run in one process (no per-call Wine startup), so this goes
// deep. Survival = the process reaches the end and exits cleanly with no crash
// dump; the harness (run_fuzz_callbacks.sh) checks that.
//
//   x86_64-w64-mingw32-g++ callback_fuzzer.cpp -o callback_fuzzer.exe
//   wine callback_fuzzer.exe mxbmrp3_test.dlo [iterations]
// ============================================================================
#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <cstring>

typedef int  (*PFN_Startup)(char*);
typedef void (*PFN_Shutdown)();
typedef void (*PFN_DS)(void*, int);
typedef void (*PFN_Telem)(void*, int, float, float);
typedef void (*PFN_Class)(void*, int, void*, int);
typedef void (*PFN_TrackPos)(int, void*, int);
typedef void (*PFN_Draw)(int, int*, void**, int*, void**);
typedef int  (*PFN_Spectate)(int, void*, int, int*);
typedef void (*PFN_TrackCenter)(int, void*, void*);

// Deterministic PRNG (xorshift32) — fixed seed so a failure reproduces.
static uint32_t g_state = 0xC0FFEEu;
static uint32_t rnd() { uint32_t x = g_state; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return g_state = x; }

// Adversarial size/element-size values: negatives, zero, tiny, plausible, absurd.
// Used for byte-buffer sizes and element sizes, which the plugin memcpy-clamps or
// validates — so even the huge values are cheap (bounded read or early reject).
static int adversarialInt() {
    static const int pool[] = { -1000000, -8, -1, 0, 1, 3, 4, 7, 15, 16, 36, 40,
                                64, 100, 256, 1024, 4096, 65535, 100000, 1000000 };
    const int n = sizeof(pool) / sizeof(pool[0]);
    return (rnd() & 7) ? pool[rnd() % n] : (int)rnd();
}

// Array *counts* (segments / vehicles / cameras / entries). Deliberately bounded:
// over-cap values (100001, 1000000, negatives) verify the plugin REJECTS an
// implausible count without reading the array; small accepted values exercise the
// real path cheaply. We avoid huge-but-accepted counts because those make the
// plugin do genuine O(count) work per call (building a giant map) — real, but slow
// and not crash-relevant. An unbounded-read regression still faults: the read of
// 1000000 * struct blows past the buffer below.
static int arrayCount() {
    static const int pool[] = { -1000000, -8, -1, 0, 1, 3, 7, 50, 500, 2000,
                                100001, 1000000 };
    const int n = sizeof(pool) / sizeof(pool[0]);
    return pool[rnd() % n];
}

int main(int argc, char** argv) {
    const char* dll = (argc > 1) ? argv[1] : "mxbmrp3_test.dlo";
    long iters = (argc > 2) ? atol(argv[2]) : 20000;

    HMODULE h = LoadLibraryA(dll);
    if (!h) { printf("FAIL: LoadLibrary error %lu\n", GetLastError()); return 2; }
    auto sym = [&](const char* n) { return GetProcAddress(h, n); };

    auto Startup  = (PFN_Startup) sym("Startup");
    auto Shutdown = (PFN_Shutdown)sym("Shutdown");
    auto Draw     = (PFN_Draw)    sym("Draw");
    if (!Startup) { printf("FAIL: no Startup\n"); return 2; }

    // The void(void*,int) callbacks — the bulk of the boundary.
    const char* dsNames[] = {
        "EventInit", "RunInit", "RunLap", "RunSplit", "RaceEvent", "RaceAddEntry",
        "RaceRemoveEntry", "RaceSession", "RaceSessionState", "RaceLap", "RaceSplit",
        "RaceHoleshot", "RaceCommunication", "RaceVehicleData",
    };
    PFN_DS ds[sizeof(dsNames) / sizeof(dsNames[0])];
    int dsCount = 0;
    for (const char* n : dsNames) { if (auto p = sym(n)) ds[dsCount++] = (PFN_DS)p; }

    // Test-build-only hook: flips the EXPERIMENTAL plugin worker thread on. The
    // threaded path copies callback payloads onto a queue BEFORE the downstream
    // handlers' validation runs, so it has its own count-clamp obligations —
    // fuzz both modes (sync first half, threaded second half).
    auto PtEnable = (PFN_Shutdown)   sym("MXBMRP3_Test_PluginThreadEnable");

    auto Telem    = (PFN_Telem)      sym("RunTelemetry");
    auto Class    = (PFN_Class)      sym("RaceClassification");
    auto TrackPos = (PFN_TrackPos)   sym("RaceTrackPosition");
    auto SpecVeh  = (PFN_Spectate)   sym("SpectateVehicles");
    auto SpecCam  = (PFN_Spectate)   sym("SpectateCameras");
    auto TrackCen = (PFN_TrackCenter)sym("TrackCenterline");

    // savePath: optional argv[3] (default = the Wine harness's Z: mapping). The
    // MSVC/ASan job on real Windows passes a native temp dir instead, since Z:\
    // is a Wine-only drive. The plugin degrades gracefully on an unwritable path,
    // but a valid one keeps its file I/O out of the fuzz signal.
    char savePathDefault[] = "Z:\\tmp\\mxbfuzz\\";
    char* savePath = (argc > 3) ? argv[3] : savePathDefault;
    Startup(savePath);

    // A scratch buffer of random bytes, sized to comfortably hold every bounded
    // read a *correctly clamped* callback performs (accepted array counts are
    // capped at 2000 by arrayCount(); the largest bounded read is well under 1 MB).
    // A callback that fails to clamp — e.g. reading an over-cap count of 1,000,000
    // structs — walks past this 2 MB buffer and faults. That fault is the bug.
    const size_t BUFSZ = 2 * 1024 * 1024;
    unsigned char* buf = (unsigned char*)malloc(BUFSZ);
    for (size_t i = 0; i < BUFSZ; ++i) buf[i] = (unsigned char)rnd();
    // A zeroed buffer for TrackCenterline geometry (see case 16). Large enough for
    // the max accepted segment count (2000 * 32 bytes) plus the raceData floats.
    unsigned char* zbuf = (unsigned char*)calloc(1, 256 * 1024);

    for (long it = 0; it < iters; ++it) {
        // Halfway through, switch to plugin-thread mode: the pre-queue copy in
        // PluginManager must reject over-cap array counts (TrackCenterline) just
        // like the sync handlers do — an unbounded copy there faults right here.
        if (PtEnable && it == iters / 2) PtEnable();

        // Cheaply perturb the front of the buffer each iteration.
        *(uint32_t*)buf = rnd();
        buf[4] = (unsigned char)rnd();

        int pick = rnd() % 22;
        int sz = adversarialInt();
        switch (pick) {
        case 0: case 1: case 2: case 3: case 4: case 5: case 6:
            if (dsCount) ds[rnd() % dsCount](buf, sz);
            break;
        case 7: if (Telem) Telem(buf, sz, (float)(int)rnd(), (float)(int)rnd() / 1000.0f); break;
        case 8: case 9:
            // RaceClassification(header, headerSize, array, elemSize) with a random
            // count in the header + a random element size — exercises the elem-size
            // validation and the MAX_RACE_ENTRIES count clamp.
            if (Class) { *(int*)(buf + 12) = arrayCount(); Class(buf, sz, buf + 64, adversarialInt()); }
            break;
        case 10: case 11:
            if (TrackPos) TrackPos(arrayCount(), buf, adversarialInt());
            break;
        case 12: case 13: {
            int select = 0;
            if (SpecVeh) SpecVeh(arrayCount(), buf, adversarialInt(), &select);
            break;
        }
        case 14: case 15: {
            // SpectateCameras walks an opaque, unsized name blob (the SEH-guarded
            // path) — feed it unterminated random bytes and absurd counts.
            int select = 0;
            if (SpecCam) SpecCam(arrayCount(), buf, adversarialInt(), &select);
            break;
        }
        case 16:
            // TrackCenterline: fuzz the COUNT (over-cap values must be rejected;
            // this is the boundary-shape contract and the bug this fuzzer already
            // caught). Feed *zeroed* geometry, not random bytes: garbage float
            // geometry (NaN/Inf/degenerate arcs) stresses the map RENDERER's
            // numeric robustness, a separate concern out of this fuzzer's scope
            // (and a known limitation — see README). Zeroed segments exercise the
            // accepted path without that.
            if (TrackCen) TrackCen(arrayCount(), zbuf, zbuf);
            break;
        default:
            if (dsCount) ds[rnd() % dsCount](buf, sz);
            break;
        }

        if (Draw && (it % 97) == 0) { int nq, ns; void* q; void* s; Draw((int)(rnd() % 4), &nq, &q, &ns, &s); }
        if ((it % 1000) == 0) { fprintf(stderr, "[fuzz] it=%ld last_pick=%d\n", it, pick); fflush(stderr); }
    }

    free(buf);
    printf("FUZZ SURVIVED %ld iterations\n", iters);
    fflush(stdout);
    if (Shutdown) Shutdown();
    return 0;
}
