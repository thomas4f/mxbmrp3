// ============================================================================
// tests/integration/tests/benchmark_report_test.cpp
// The Benchmark profiler's report, end to end: it is written on leaving the
// track and at Shutdown(), not only when the profiler is turned off, and a
// freeze in the GAME's time reads as a long frame with little plugin time.
//
// WHY. A player chasing a stutter in a race quits the game right after the race.
// The report used to be written only on the profiler's falling edge, so quitting
// with it on threw the whole capture away -- the one capture that mattered.
//
// Every save rewrites ONE file per stint (named when the profiler was switched
// on), so leaving the track twice and quitting leaves one report holding all of
// it, not three partial copies. The menu time between outings is a gap, not a
// frame -- pinned in tests/unit/test_frame_time_stats.cpp, since a 5 s wait has
// no place in this suite.
//
// THE SUMMARY CASE drives the one thing a stutter report is for: the test thread
// sleeps 150 ms between two Draws -- a freeze outside every plugin callback, as a
// game hitch is -- and the report must list that frame as the longest with a
// plugin time far below it. The report states numbers, never a verdict (wall
// time includes Windows pauses), so the summary and PC lines are asserted present,
// not judged.
// ============================================================================
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"

#include <filesystem>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <thread>
#include <chrono>
#include <vector>

namespace fs = std::filesystem;

namespace {
std::vector<std::string> reports(const fs::path& dir) {
    std::vector<std::string> out;
    std::error_code ec;
    if (!fs::exists(dir, ec)) return out;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        const std::string n = e.path().filename().string();
        if (n.rfind("benchmark_", 0) == 0 && e.path().extension() == ".txt") {
            std::ifstream f(e.path());
            out.push_back(std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>()));
        }
    }
    return out;
}

void wipe(const fs::path& dir) {
    std::error_code ec;
    fs::remove_all(dir, ec);
}

// Drive one outing with the profiler on: RunInit, some frames, RunDeinit.
void outing(PluginHost& host, int frames) {
    host.runInit(/*session=*/1);
    host.runStart();
    for (int i = 0; i < frames; ++i) host.draw();
    host.runDeinit();
}
}  // namespace

TEST_CASE("benchmark report is saved on leaving the track and on quit") {
    const char* saveWin = "Z:\\tmp\\mxbmrp3-tests\\benchmark_report\\";
    const fs::path dir = "Z:\\tmp\\mxbmrp3-tests\\benchmark_report\\mxbmrp3\\benchmarks";
    wipe(dir);

    {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        REQUIRE(host.hasBenchmarkSurfaces());
        host.startup(saveWin);
        REQUIRE_MESSAGE(host.bmExists(), "BenchmarkWidget not built; this test would pass vacuously");
        host.session(/*session=*/1, /*numLaps=*/0);
        host.bmSetVisible(true);
        host.draw();   // the rising edge is applied on the next frame

        SUBCASE("leave the track, go back out, quit with the profiler still on") {
            outing(host, 30);
            host.runInit(1);
            host.runStart();
            for (int i = 0; i < 30; ++i) host.draw();
            host.shutdown();   // drains the disk writer

            const auto r = reports(dir);
            REQUIRE(r.size() == 1);   // one file, rewritten -- not a copy per save
            CHECK(r[0].find("=== FRAME RATE (whole stint) ===") != std::string::npos);
            // Both outings are in it: 60 frames drawn, minus the clock's first sample.
            long long frames = 0;
            const auto at = r[0].find("Duration: ");
            REQUIRE(at != std::string::npos);
            const auto paren = r[0].find('(', at);
            REQUIRE(paren != std::string::npos);
            frames = std::atoll(r[0].c_str() + paren + 1);
            CHECK(frames >= 59);
        }

        SUBCASE("leaving the track saves on its own, before any quit") {
            outing(host, 30);
            // The disk writer is a thread: give it up to 5 s, then look.
            std::vector<std::string> r;
            for (int i = 0; i < 100 && r.empty(); ++i) {
                r = reports(dir);
                if (r.empty()) std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            REQUIRE(r.size() == 1);
            CHECK(r[0].find("FPS: (no samples)") == std::string::npos);
        }
    }
    wipe(dir);
}

TEST_CASE("benchmark report shows a game freeze as a long frame with little plugin time") {
    const char* saveWin = "Z:\\tmp\\mxbmrp3-tests\\benchmark_report\\";
    const fs::path dir = "Z:\\tmp\\mxbmrp3-tests\\benchmark_report\\mxbmrp3\\benchmarks";
    wipe(dir);
    {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        REQUIRE(host.hasBenchmarkSurfaces());
        host.startup(saveWin);
        REQUIRE(host.bmExists());
        host.session(/*session=*/1, /*numLaps=*/0);
        host.bmSetVisible(true);
        host.draw();
        host.runInit(1);
        host.runStart();
        for (int i = 0; i < 20; ++i) host.draw();
        std::this_thread::sleep_for(std::chrono::milliseconds(150));   // the game froze
        for (int i = 0; i < 20; ++i) host.draw();
        host.shutdown();

        const auto r = reports(dir);
        REQUIRE(r.size() == 1);
        const std::string& text = r[0];
        // The summary comes first, before any table.
        const auto summary = text.find("=== SUMMARY ===");
        REQUIRE(summary != std::string::npos);
        CHECK(summary < text.find("=== CONTEXT ==="));
        CHECK(text.find("Captured: ", summary) != std::string::npos);
        CHECK(text.find("Plugin:   ", summary) != std::string::npos);
        // The machine it came from, for whoever reads the report.
        const auto context = text.find("=== CONTEXT ===");
        CHECK(text.find("CPU: ", context) != std::string::npos);
        CHECK(text.find("RAM: ", context) != std::string::npos);
        CHECK(text.find("Game memory: ", context) != std::string::npos);

        // The longest frame is the freeze, and the plugin's share of it is small.
        const auto table = text.find("=== WORST FRAMES");
        REQUIRE(table != std::string::npos);
        const auto row = text.find("\n", text.find("\n", text.find("\n", table) + 1) + 1) + 1;
        double frameMs = 0.0, pluginMs = 0.0;
        char at[16] = {};
        REQUIRE(std::sscanf(text.c_str() + row, "%15s %lf %lf", at, &frameMs, &pluginMs) == 3);
        CHECK(frameMs >= 150.0);
        CHECK(pluginMs < frameMs * 0.25);
        CHECK(text.find("Longest call", table) != std::string::npos);
    }
    wipe(dir);
}
