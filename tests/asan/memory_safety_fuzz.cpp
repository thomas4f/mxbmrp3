// ============================================================================
// tests/asan/memory_safety_fuzz.cpp
// Native AddressSanitizer + UndefinedBehaviorSanitizer harness for the plugin's
// PORTABLE memory-handling surface — the fixed-size buffers and index math that
// a heap/stack overflow would live in.
//
// WHY THIS EXISTS
// Two shipped crashes (mxbmrp3.dlo+0x378d8, +0xeaab4) were access violations in
// innocent heap walks (a std::map teardown; a std::map iteration during save) —
// the classic signature of a heap corruption whose *culprit* write happened
// earlier and elsewhere. Analytics gives only the victim's fault address, never
// the writer. ASan closes that gap: it faults AT the corrupting write, with the
// stack. This harness drives the code most likely to contain such a write over
// adversarial input so any overflow is caught deterministically in CI — no game,
// no Windows, no flaky wild repro.
//
// SCOPE (native, this file): the portable pieces that compile without the Win32
// header graph — RaceEntryData's fixed name/number buffers, and the leader-timing
// position-index computation whose clamp guards writes into a fixed 100-element
// array (the exact array type in the +0x378d8 crash). The full live-callback path
// (updateRealTimeGaps / StatsManager::save over the real maps) is exercised under
// ASan by the MSVC build instead — see tests/asan/README.md — because those TUs
// pull in the whole HUD/HttpServer/httplib graph and can't be built natively.
//
//   ./tests/asan/run.sh          # build + run (see that script for flags)
// Survival = process exits 0 with no ASan/UBSan report.
// ============================================================================
#include "core/plugin_data.h"

#include <cstdio>
#include <cstdint>
#include <string>
#include <map>
#include <array>
#include <climits>
#include <cmath>

// Deterministic PRNG (xorshift32) — fixed seed so any failure reproduces byte-
// for-byte, matching the callback fuzzer's convention.
static uint32_t g_state = 0xA11CE5u;
static uint32_t rnd() { uint32_t x = g_state; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return g_state = x; }

// ----------------------------------------------------------------------------
// 1. RaceEntryData fixed-buffer safety (the real header constructor).
//    formattedRaceNum[8] and truncatedName[4] are the tightest fixed buffers in
//    PluginData; a rider name or race number that overran them would write into
//    an adjacent RaceEntryData/heap block — exactly the corruption class we're
//    ruling out. Feed hostile names and numbers; ASan validates every write.
// ----------------------------------------------------------------------------
static int fuzzRaceEntryData() {
    static const char* kNames[] = {
        "", "A", "AB", "ABC", "ABCD", "ABCDE",
        "ThisIsAVeryLongRiderNameWellPastEveryFixedBuffer",
        "\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9",           // 4 two-byte UTF-8 code points
        "\xF0\x9F\x8F\x8D",                            // 4-byte emoji (motorcycle)
        "\xE4\xB8\xAD\xE6\x96\x87\xE5\x90\x8D",       // multibyte, no ASCII
        "wwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwww",
    };
    static const int kNums[] = {
        0, 1, 7, 42, 99, 100, 999, 1000, 9999, 100000,
        INT_MAX, -1, -42, -999, INT_MIN + 1, INT_MIN,
    };
    const int nN = (int)(sizeof(kNames) / sizeof(kNames[0]));
    const int nM = (int)(sizeof(kNums) / sizeof(kNums[0]));

    long built = 0;
    // Full cross product, then random combinations to cover the constructor and
    // to keep values changing across a large iteration count.
    for (int a = 0; a < nN; ++a) {
        for (int b = 0; b < nM; ++b) {
            RaceEntryData e(kNums[b], kNames[a], kNames[(a + 3) % nN],
                            "abbr", "brand", 0xDEADBEEFu);
            // Read every fixed buffer back so ASan checks the terminator write too.
            volatile char sink = 0;
            sink ^= e.formattedRaceNum[0];
            sink ^= e.truncatedName[0];
            sink ^= e.name[sizeof(e.name) - 1];
            sink ^= e.bikeName[sizeof(e.bikeName) - 1];
            (void)sink;
            ++built;
        }
    }
    for (long i = 0; i < 200000; ++i) {
        const char* nm = kNames[rnd() % nN];
        int num = (rnd() & 1) ? kNums[rnd() % nM] : (int)rnd();
        RaceEntryData e(num, nm, kNames[rnd() % nN], "a", "b", rnd());
        volatile char sink = e.formattedRaceNum[0] ^ e.truncatedName[0];
        (void)sink;
        ++built;
    }
    return (int)(built & 0x7fffffff);
}

// ----------------------------------------------------------------------------
// 2. Leader-timing position-index invariant.
//    plugin_data_standings.cpp computes, for a game-supplied float trackPos:
//        idx = (int)(trackPos * NUM_TIMING_POINTS);
//        idx = max(0, min(idx, NUM_TIMING_POINTS - 1));
//    then writes m_leaderTimingPoints[lap][idx]. The whole safety of that write
//    rests on the clamp holding for EVERY float the game can hand us — including
//    NaN/Inf/huge/negative/subnormal, where float->int conversion yields the
//    "integer indefinite" 0x80000000. We reproduce the exact formula and USE the
//    result to index a REAL std::array<LeaderTimingPoint, N> (the +0x378d8 type):
//    if the clamp ever fails to bound the index, ASan faults on the OOB write.
// ----------------------------------------------------------------------------
static constexpr int N = 100;   // mirrors PluginData::NUM_TIMING_POINTS (plugin_data.h)
static_assert(N == 100, "keep in step with NUM_TIMING_POINTS");

static int clampIndexLikeProduction(float trackPos) {
    int idx = static_cast<int>(trackPos * static_cast<float>(N));
    idx = idx < 0 ? 0 : idx;
    idx = idx > (N - 1) ? (N - 1) : idx;
    return idx;
}

static int fuzzLeaderTimingIndex() {
    std::array<LeaderTimingPoint, N> arr{};   // heap-adjacent when boxed below
    auto* boxed = new std::array<LeaderTimingPoint, N>();  // exercise the heap block too

    static const float kFloats[] = {
        0.0f, 0.5f, 1.0f, 0.999999f, 1.0000001f, -0.0f, -1.0f, -1e30f, 1e30f,
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::denorm_min(),
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::lowest(),
    };
    const int nF = (int)(sizeof(kFloats) / sizeof(kFloats[0]));

    long writes = 0;
    for (int i = 0; i < nF; ++i) {
        int idx = clampIndexLikeProduction(kFloats[i]);
        arr[idx]   = LeaderTimingPoint(12345, 3);   // ASan-checked indexed write
        (*boxed)[idx] = LeaderTimingPoint(idx, i);
        ++writes;
    }
    // Random float bit patterns — reinterpret raw uint32 as float to hit the
    // whole domain, including signaling NaNs and denormals the pool misses.
    for (long i = 0; i < 500000; ++i) {
        uint32_t bits = rnd();
        float f; std::memcpy(&f, &bits, sizeof(f));
        int idx = clampIndexLikeProduction(f);
        arr[idx] = LeaderTimingPoint((int)bits, idx);
        (*boxed)[idx].sessionTime = idx;
        ++writes;
    }
    delete boxed;
    return (int)(writes & 0x7fffffff);
}

// ----------------------------------------------------------------------------
// 3. Churn the exact crash-site container TYPES under ASan.
//    +0x378d8 crashed freeing std::map<int, std::array<LeaderTimingPoint,100>>;
//    +0xeaab4 crashed iterating std::map<std::string,double>. Build, mutate,
//    partially erase, and tear these down repeatedly. Not the plugin's own
//    mutation code (that's the MSVC path), but it confirms the node lifecycle
//    and our index writes into the boxed values are clean under the sanitizer.
// ----------------------------------------------------------------------------
static int churnCrashSiteContainers() {
    long ops = 0;
    for (int round = 0; round < 200; ++round) {
        std::map<int, std::array<LeaderTimingPoint, N>> timing;   // +0x378d8 type
        std::map<std::string, double> odo;                        // +0xeaab4 type

        const int entries = 1 + (int)(rnd() % 40);
        for (int e = 0; e < entries; ++e) {
            int lap = (int)(rnd() % 25) - 2;   // include negative/edge laps
            auto& a = timing[lap];
            int idx = clampIndexLikeProduction((float)(rnd() % 1000) / 999.0f);
            a[idx] = LeaderTimingPoint((int)rnd(), lap);

            char key[32];
            snprintf(key, sizeof(key), "bike_%u", rnd() % 7);
            odo[key] += (double)(rnd() % 100000) / 10.0;
            ++ops;
        }
        // Prune-then-clear, mirroring updateRealTimeGaps' erase pattern.
        for (auto it = timing.begin(); it != timing.end(); ) {
            if (it->first < 0) it = timing.erase(it); else ++it;
        }
        timing.clear();   // the operation that faulted in +0x378d8
        odo.clear();      // iterated in +0xeaab4
    }
    return (int)(ops & 0x7fffffff);
}

int main() {
    long r1 = fuzzRaceEntryData();
    long r2 = fuzzLeaderTimingIndex();
    long r3 = churnCrashSiteContainers();
    printf("ASAN MEMORY-SAFETY HARNESS PASSED  (raceEntry=%ld indexWrites=%ld churnOps=%ld)\n",
           r1, r2, r3);
    return 0;
}
