// ============================================================================
// tests/unit/test_frame_time_stats.cpp
// FrameTimeStats (core/frame_time_stats.h): the Benchmark profiler's whole-stint
// frame-time record.
//
// WHY THIS FILE EXISTS. It replaced a ring of the last 16,384 raw frame times,
// which at 60 fps forgot everything older than ~4.5 minutes: a player capturing a
// stutter ten minutes into a race sent a report that no longer held it. The cases
// below pin the two promises the replacement makes instead: a spike from the very
// start of a long stint is still in the worst list (with its time and plugin cost)
// at the end, and the histogram's percentiles stay within its 1% bucket width of
// the exact sorted-array answer the ring used to give. Also pinned: menu gaps
// are left out but a plugin hang is not, hitches are counted over every frame
// (not only the worst 20), and a worst frame keeps its longest single callback.
// ============================================================================
#include "doctest.h"

#include "core/frame_time_stats.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {
void addF(FrameTimeStats& fs, double frameUs, double atSec, long long pluginUs, int riders,
          long long maxCallUs = 0, int maxCallIdx = -1) {
    FrameTimeStats::Spike f;
    f.frameUs = frameUs; f.atSec = atSec; f.pluginUs = pluginUs; f.riders = riders;
    f.maxCallUs = maxCallUs; f.maxCallIdx = maxCallIdx;
    fs.add(f);
}
}  // namespace

TEST_CASE("FrameTimeStats: empty reads as zero") {
    FrameTimeStats fs;
    CHECK(fs.count() == 0);
    CHECK(fs.percentileUs(0.99) == 0.0);
    CHECK(fs.meanUs() == 0.0);
    CHECK(fs.countAbove(50000.0) == 0);
    FrameTimeStats::Spike w[FrameTimeStats::WORST_COUNT];
    CHECK(fs.worst(w, FrameTimeStats::WORST_COUNT) == 0);
}

TEST_CASE("FrameTimeStats: INV_LOG_RATIO matches 1/ln(RATIO)") {
    CHECK(FrameTimeStats::INV_LOG_RATIO == doctest::Approx(1.0 / std::log(FrameTimeStats::RATIO)).epsilon(1e-9));
    // The top bucket reaches at least 10 s.
    CHECK(FrameTimeStats::MIN_US * std::pow(FrameTimeStats::RATIO, FrameTimeStats::BUCKET_COUNT) >= 1.0e7);
}

TEST_CASE("FrameTimeStats: rejects zero, negative, NaN and infinite intervals") {
    FrameTimeStats fs;
    addF(fs, 0.0, 0.0, 0, 0);
    addF(fs, -5.0, 0.0, 0, 0);
    addF(fs, std::nan(""), 0.0, 0, 0);
    addF(fs, HUGE_VAL, 0.0, 0, 0);
    CHECK(fs.count() == 0);
    CHECK(fs.gapCount() == 0);
}

TEST_CASE("FrameTimeStats: a menu gap is counted, never a frame") {
    // Practice, 40 s in the menus (no Draw), race: the 40 s must not become the
    // max frame, the FPS min, a hitch, or the top of the worst list.
    FrameTimeStats fs;
    for (int i = 0; i < 600; ++i) addF(fs, 16667.0, i / 60.0, 900, 20);
    addF(fs, 40.0e6, 50.0, 300, 20);
    addF(fs, 4.9e6, 51.0, 300, 20);   // under the gap threshold: a real (awful) frame
    for (int i = 0; i < 600; ++i) addF(fs, 16667.0, 52.0 + i / 60.0, 900, 20);

    CHECK(fs.gapCount() == 1);
    CHECK(fs.gapSec() == doctest::Approx(40.0));
    CHECK(fs.count() == 1201);
    CHECK(fs.maxUs() == 4.9e6);
    CHECK(fs.countAbove(100000.0) == 1);
    CHECK(fs.hitchCount() == 1);
    FrameTimeStats::Spike w[1];
    REQUIRE(fs.worst(w, 1) == 1);
    CHECK(w[0].frameUs == 4.9e6);
    fs.reset();
    CHECK(fs.gapCount() == 0);
}

TEST_CASE("FrameTimeStats: an early spike survives a long stint") {
    // 10 minutes at 60 fps is 36,000 frames -- more than twice what the old ring
    // held. The spike at 3 s has to be in the export at the end.
    FrameTimeStats fs;
    const double frame = 16667.0;
    for (int i = 0; i < 36000; ++i) {
        const double at = i / 60.0;
        if (i == 180) addF(fs, 480000.0, at, 1200, 32);        // the freeze: 480 ms, plugin 1.2 ms
        else          addF(fs, frame + (i % 7), at, 900, 32);
    }
    CHECK(fs.count() == 36000);
    CHECK(fs.maxUs() == 480000.0);
    CHECK(fs.countAbove(50000.0) == 1);
    CHECK(fs.countAbove(100000.0) == 1);

    FrameTimeStats::Spike w[FrameTimeStats::WORST_COUNT];
    const int n = fs.worst(w, FrameTimeStats::WORST_COUNT);
    REQUIRE(n == FrameTimeStats::WORST_COUNT);
    CHECK(w[0].frameUs == 480000.0);
    CHECK(w[0].atSec == doctest::Approx(3.0));
    CHECK(w[0].pluginUs == 1200);
    CHECK(w[0].riders == 32);
    for (int i = 1; i < n; ++i) CHECK(w[i - 1].frameUs >= w[i].frameUs);   // longest first
}

TEST_CASE("FrameTimeStats: worst list keeps exactly the N longest") {
    FrameTimeStats fs;
    // Ascending then descending, so entries get displaced from both directions.
    for (int i = 1; i <= 100; ++i) addF(fs, 1000.0 * i, i, i, 0);
    for (int i = 200; i > 100; --i) addF(fs, 1000.0 * i, i, i, 0);
    FrameTimeStats::Spike w[FrameTimeStats::WORST_COUNT];
    const int n = fs.worst(w, FrameTimeStats::WORST_COUNT);
    REQUIRE(n == FrameTimeStats::WORST_COUNT);
    for (int i = 0; i < n; ++i) CHECK(w[i].frameUs == 1000.0 * (200 - i));
    // A smaller cap gets the top of the list.
    FrameTimeStats::Spike top[3];
    CHECK(fs.worst(top, 3) == 3);
    CHECK(top[2].frameUs == 198000.0);
}

TEST_CASE("FrameTimeStats: percentiles within a bucket of the exact answer") {
    FrameTimeStats fs;
    std::vector<double> all;
    // A skewed spread like a real stint: mostly ~2-4 ms with a long tail.
    unsigned seed = 12345;
    for (int i = 0; i < 50000; ++i) {
        seed = seed * 1103515245u + 12345u;
        const double u = ((seed >> 8) & 0xFFFF) / 65535.0;
        const double us = 2000.0 + 2000.0 * u + ((i % 97 == 0) ? 30000.0 * u : 0.0);
        addF(fs, us, 0.0, 0, 0);
        all.push_back(us);
    }
    std::sort(all.begin(), all.end());
    auto exact = [&](double p) { return all[static_cast<size_t>(p * (all.size() - 1))]; };
    for (double p : { 0.01, 0.5, 0.9, 0.99, 0.999 }) {
        CAPTURE(p);
        CHECK(fs.percentileUs(p) == doctest::Approx(exact(p)).epsilon(0.011));
    }
    CHECK(fs.minUs() == all.front());
    CHECK(fs.maxUs() == all.back());
    double sum = 0.0;
    for (double v : all) sum += v;
    CHECK(fs.meanUs() == doctest::Approx(sum / all.size()));
}

TEST_CASE("FrameTimeStats: reset starts a fresh stint") {
    FrameTimeStats fs;
    addF(fs, 90000.0, 1.0, 5, 3);
    fs.reset();
    CHECK(fs.count() == 0);
    CHECK(fs.countAbove(50000.0) == 0);
    FrameTimeStats::Spike w[FrameTimeStats::WORST_COUNT];
    CHECK(fs.worst(w, FrameTimeStats::WORST_COUNT) == 0);
    addF(fs, 2000.0, 0.5, 1, 1);
    CHECK(fs.maxUs() == 2000.0);
    CHECK(fs.minUs() == 2000.0);
}

TEST_CASE("FrameTimeStats: hitches are counted over every frame, with the plugin's peak") {
    // 30 long freezes overflow the worst list; the count and the plugin peak must
    // still cover all of them, including the one the list drops.
    FrameTimeStats fs;
    for (int i = 0; i < 1000; ++i) addF(fs, 16667.0, i / 60.0, 800, 20);
    for (int i = 0; i < 30; ++i) addF(fs, 300000.0 + i * 1000.0, i, 100, 0);
    addF(fs, 60000.0, 40.0, 30000, 0, 29000, 3);   // the plugin's frame, shortest hitch
    CHECK(fs.hitchCount() == 31);
    CHECK(fs.maxPluginUs() == 30000);
    FrameTimeStats::Spike w[FrameTimeStats::WORST_COUNT];
    const int n = fs.worst(w, FrameTimeStats::WORST_COUNT);
    for (int i = 0; i < n; ++i) CHECK(w[i].frameUs != 60000.0);   // indeed off the list
}

TEST_CASE("FrameTimeStats: a worst frame keeps its longest single call") {
    FrameTimeStats fs;
    addF(fs, 16667.0, 0.0, 800, 20);
    addF(fs, 120000.0, 1.0, 45000, 20, 44000, 2);   // one stuck callback
    addF(fs, 90000.0, 2.0, 30000, 20, 900, 0);      // many small ones adding up
    FrameTimeStats::Spike w[3];
    REQUIRE(fs.worst(w, 3) == 3);
    CHECK(w[0].maxCallUs == 44000);
    CHECK(w[0].maxCallIdx == 2);
    CHECK(w[1].maxCallUs == 900);
    CHECK(w[1].maxCallIdx == 0);
}

TEST_CASE("FrameTimeStats: a long interval the plugin spent is a hang, not a gap") {
    // Menus: no Draw AND no plugin callbacks, so a real gap has little plugin
    // time. A 6 s interval the plugin spent 5.5 s of is the worst frame there is.
    FrameTimeStats fs;
    addF(fs, 16667.0, 0.0, 800, 0);
    addF(fs, 6.0e6, 1.0, 5500000, 0);
    CHECK(fs.gapCount() == 0);
    CHECK(fs.count() == 2);
    CHECK(fs.maxUs() == 6.0e6);
}

TEST_CASE("FrameTimeStats: the session-change work around a menu visit keeps it a gap") {
    // 40 s in the menus; the leave-track save, the new session's callbacks and
    // the first Draw back (every HUD rebuilds) add up to 30 ms -- far under a
    // quarter of the interval: still a gap.
    FrameTimeStats fs;
    addF(fs, 16667.0, 0.0, 800, 0);
    addF(fs, 40.0e6, 1.0, 30000, 0);
    CHECK(fs.gapCount() == 1);
    CHECK(fs.count() == 1);
    CHECK(fs.hitchCount() == 0);
}

TEST_CASE("FrameTimeStats: a menu gap's plugin time is not booked onto the frame before it") {
    // The profiler books the interval after a frame onto that frame, because the
    // frame's own Draw ends there. Before bookNextInterval() skipped gaps, a pit
    // stop put the 30 ms of leave-track save, report and first Draw back onto the
    // last frame on track, and that ordinary frame became the report's "most ms
    // in any one frame".
    FrameTimeStats::Spike last;
    last.frameUs = 16667.0; last.pluginUs = 400; last.maxCallUs = 150; last.maxCallIdx = 1;
    FrameTimeStats::bookNextInterval(last, 40.0e6, 30000, 25000, 4);
    CHECK(last.pluginUs == 400);
    CHECK(last.maxCallUs == 150);
    CHECK(last.maxCallIdx == 1);

    FrameTimeStats fs;
    fs.add(last);
    CHECK(fs.maxPluginUs() == 400);

    // An ordinary next interval is still booked, longest call included.
    FrameTimeStats::Spike f;
    f.frameUs = 16667.0; f.pluginUs = 400; f.maxCallUs = 150; f.maxCallIdx = 1;
    FrameTimeStats::bookNextInterval(f, 16667.0, 500, 300, 2);
    CHECK(f.pluginUs == 900);
    CHECK(f.maxCallUs == 300);
    CHECK(f.maxCallIdx == 2);

    // A plugin HANG is not a gap, so it is booked too (an upper bound, as ever).
    FrameTimeStats::Spike h;
    h.frameUs = 16667.0; h.pluginUs = 400;
    FrameTimeStats::bookNextInterval(h, 6.0e6, 5.0e6, 5.0e6, 3);
    CHECK(h.pluginUs == 5000400);
}
