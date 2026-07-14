// ============================================================================
// tests/integration/tests/plugin_thread_latency_test.cpp
// Demonstrates the WHOLE POINT of the plugin worker thread: a slow component can
// no longer delay the game's frame.
//
// We inject an artificial per-frame stall into the shared render build
// (HudManager::produceFrame, via MXBMRP3_Test_SetProduceDelayMs) — a stand-in for
// a genuinely heavy component like the Map HUD's ribbon re-tessellation on a big
// track. Then we measure how long the GAME'S Draw export takes to return:
//
//   * Synchronous mode  (worker off): produceFrame runs INLINE inside Draw, so the
//     stall is paid on the game thread — Draw blocks for ~the full stall.
//   * Plugin-thread mode (worker on): Draw only requests a frame and hands back the
//     latest triple-buffered one, so the SAME stall is paid on the worker and Draw
//     returns in well under it.
//
// The assertion is the demonstration: with a 60 ms injected stall, threaded Draw
// stays far below it while sync Draw pays essentially all of it.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include "assertions.h"

#include <chrono>
#include <thread>
#include <algorithm>

namespace {
// Wall-clock milliseconds for a single Draw export round-trip on THIS (the "game")
// thread.
double drawMs(PluginHost& host) {
    auto t0 = std::chrono::steady_clock::now();
    host.draw();
    auto t1 = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(t1 - t0).count();
}
}

TEST_CASE("plugin thread: a slow render build stalls Draw in sync mode, not threaded") {
    constexpr int kStallMs = 60;   // simulated slow-component cost per frame

    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\ptlatency\\");

    // A minimal scene so the render build does real work around the injected stall.
    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(/*session=*/6, /*numLaps=*/10, /*lengthMs=*/0);
    host.addEntry(10, "Alice");
    host.classify(6, 300000, { { .num = 10, .best = 90000, .laps = 5, .gap = 0 } });

    // --- Plugin-thread mode: the stall is paid on the worker --------------------
    host.pluginThreadEnable();
    REQUIRE(host.pluginThreadEnabled());
    host.setProduceDelayMs(kStallMs);

    // Let the worker complete a first (stalled) build so we're measuring steady state.
    host.draw();
    std::this_thread::sleep_for(std::chrono::milliseconds(kStallMs * 2));

    // Measure many Draws spread across several worker build cycles. Every one must
    // return fast even though the worker is continuously spending kStallMs per build.
    double threadedMax = 0.0, threadedSum = 0.0;
    constexpr int kSamples = 20;
    for (int i = 0; i < kSamples; ++i) {
        double ms = drawMs(host);
        threadedMax = std::max(threadedMax, ms);
        threadedSum += ms;
        std::this_thread::sleep_for(std::chrono::milliseconds(8));
    }
    double threadedAvg = threadedSum / kSamples;
    MESSAGE("threaded Draw: avg=" << threadedAvg << "ms  max=" << threadedMax
            << "ms  (injected build stall=" << kStallMs << "ms)");

    // The whole point: the game's Draw never pays the build stall. Generous ceiling
    // (half the stall) to stay robust against Wine scheduler jitter while still being
    // unambiguously below the stall.
    CHECK(threadedMax < kStallMs / 2.0);

    // --- Synchronous mode: the SAME stall is paid inside Draw -------------------
    host.pluginThreadStop();
    REQUIRE_FALSE(host.pluginThreadEnabled());
    // delay setting persists; produceFrame now runs inline on this thread inside Draw.

    double syncMin = 1e9, syncSum = 0.0;
    constexpr int kSyncSamples = 5;
    for (int i = 0; i < kSyncSamples; ++i) {
        double ms = drawMs(host);
        syncMin = std::min(syncMin, ms);
        syncSum += ms;
    }
    double syncAvg = syncSum / kSyncSamples;
    MESSAGE("sync Draw: avg=" << syncAvg << "ms  min=" << syncMin
            << "ms  (injected build stall=" << kStallMs << "ms)");

    // Sync Draw pays essentially the whole stall (allow a little slack under the
    // nominal value for timer granularity).
    CHECK(syncMin >= kStallMs * 0.75);

    // And the headline contrast: threaded Draw is dramatically faster than sync Draw
    // under the identical injected cost.
    CHECK(threadedMax < syncMin);

    host.setProduceDelayMs(0);
    host.shutdown();
}

// The PerformanceHud / BenchmarkWidget read PluginData debug metrics that, in sync
// mode, DrawHandler updates every frame. Threaded Draw bypasses DrawHandler, so the
// worker must publish those metrics itself — otherwise fps/plugin-time would freeze.
// This proves they stay LIVE off-thread, and that "plugin time" correctly reflects
// the worker's build cost (here dominated by the injected stall).
TEST_CASE("plugin thread: performance metrics stay live off-thread") {
    constexpr int kStallMs = 40;

    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\ptmetrics\\");

    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(6, 10, 0);
    host.addEntry(10, "Alice");
    host.classify(6, 300000, { { .num = 10, .best = 90000, .laps = 5, .gap = 0 } });

    host.pluginThreadEnable();
    REQUIRE(host.pluginThreadEnabled());
    host.setProduceDelayMs(kStallMs);

    // Pump frames at a realistic cadence so the game-thread FPS estimate and the
    // worker's per-build plugin-time both populate.
    for (int i = 0; i < 30; ++i) {
        host.draw();
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    host.pluginThreadFlush();

    auto m = host.debugMetrics();
    MESSAGE("threaded metrics: fps=" << m.fps << "  pluginMs=" << m.pluginMs
            << "  pct=" << m.pct);

    // FPS is measured and non-zero (roughly the ~1000/16 ≈ 60 cadence we drove, but
    // Wine timing is loose so just assert a sane live positive value).
    CHECK(m.fps > 1.0f);
    // Plugin time reflects the worker's build, which is dominated by the injected
    // 40ms stall — proving the metric tracks the real (off-thread) plugin cost.
    CHECK(m.pluginMs >= kStallMs * 0.5f);

    host.pluginThreadStop();
    host.shutdown();
}
