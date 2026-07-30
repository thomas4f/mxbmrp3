// ============================================================================
// tests/integration/tests/hazard_reach_test.cpp
// A wrong-way hazard is scanned for FURTHER ahead than a stationary one.
//
// WHY THE REACH DIFFERS. getHazardRaceNums() lists confirmed hazards within an
// awareness distance ahead of the display rider, and that distance was one number for
// both hazard kinds. 100 m is right for a bike lying on the track: you close it at your
// own speed, so it buys ~2s of warning. A rider coming AT you closes the same gap at
// roughly double that rate, and the wrong-way state must first survive
// hazardWrongWayDurationMs (1.5s) before it counts at all — so most of the warning was
// already spent and the notice landed as they went past, or never. Wrong-way now has its
// own hazardWrongWayAwarenessDistance (250 m default); everything else about the detector
// is unchanged.
//
// The assertion is a matched pair at ONE distance: three riders, two hazard kinds, same
// 208 m gap for two of them. That is what makes this a test of the REACH rather than of
// hazard detection in general — a single-rider test would pass just as well if the
// threshold had simply been raised for everybody.
//
// Real wall-clock sleep: the wrong-way confirmation is a steady_clock timer inside
// PluginData with no injection seam (unlike the odometer/FMX clocks). One ~1.6s wait is
// cheaper than adding a seam, and it errs safe — waiting LONGER than the threshold cannot
// un-confirm a rider who is still going backward.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"

#include <chrono>
#include <thread>

namespace {
constexpr int RACE1 = 6;

// Track is 1600 m (PluginHost::eventInit default), so track fractions are metres/1600:
//   stationary reach  100 m = 0.0625
//   wrong-way  reach  250 m = 0.15625
// FAR sits between them, NEAR inside both.
constexpr float PLAYER_POS = 0.500f;
constexpr float FAR_POS    = 0.630f;   // 208 m ahead — outside stationary, inside wrong-way
constexpr float NEAR_POS   = 0.530f;   //  48 m ahead — inside both
}  // namespace

TEST_CASE("hazards: a wrong-way rider is seen further ahead than a stationary one") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\hazard_reach\\");

    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    // Pit-start shape (session already in progress): never enters the grid-start grace
    // that suppresses hazards outright.
    host.session(RACE1, /*numLaps=*/0, /*lengthMs=*/300000);
    host.addEntry(10, "Alice");    // display rider
    host.addEntry(22, "Wrong");    // rides backward at FAR
    host.addEntry(33, "NearCrash");// crashed at NEAR
    host.addEntry(44, "FarCrash"); // crashed at FAR — same distance as #22
    host.draw();
    host.spectateVehicles({ { 10, "Alice" } }, /*curSelectionIndex=*/0);

    host.classify(RACE1, 1000, {
        { .num = 10, .laps = 1, .gap = 0 },
        { .num = 22, .laps = 1, .gap = 500 },
        { .num = 33, .laps = 1, .gap = 600 },
        { .num = 44, .laps = 1, .gap = 700 },
    });

    // Seed batch: the hazard state machine only evaluates riders already in
    // m_trackPositions — a first sighting just initializes the entry.
    host.raceTrackPosition({ { .num = 10, .trackPos = PLAYER_POS },
                             { .num = 22, .trackPos = FAR_POS + 0.010f },
                             { .num = 33, .trackPos = NEAR_POS },
                             { .num = 44, .trackPos = FAR_POS } });

    // #22 starts moving backward (8 m per sample — over the 5 m "not moving" tolerance, so
    // it reads as wrong-way rather than stationary, and well under the 5% teleport jump).
    // #33 and #44 are crashed, which confirms as a Stationary hazard immediately.
    host.raceTrackPosition({ { .num = 10, .trackPos = PLAYER_POS },
                             { .num = 22, .trackPos = FAR_POS + 0.005f },
                             { .num = 33, .trackPos = NEAR_POS, .crashed = 1 },
                             { .num = 44, .trackPos = FAR_POS, .crashed = 1 } });

    // Before the wrong-way timer expires only the NEAR crash is in reach: #44 is a
    // stationary hazard at 208 m, past the 100 m stationary reach.
    CHECK_MESSAGE(host.hazardRaceNumCount() == 1,
                  "only the near crash should be listed before wrong-way confirms");

    // Let the wrong-way confirmation timer (hazardWrongWayDurationMs, 1.5s) expire.
    std::this_thread::sleep_for(std::chrono::milliseconds(1600));

    host.raceTrackPosition({ { .num = 10, .trackPos = PLAYER_POS },
                             { .num = 22, .trackPos = FAR_POS },
                             { .num = 33, .trackPos = NEAR_POS, .crashed = 1 },
                             { .num = 44, .trackPos = FAR_POS, .crashed = 1 } });

    // The matched pair. #22 (wrong-way) and #44 (crashed) now sit at the SAME 208 m ahead:
    // the wrong-way rider is in reach, the crashed one still is not. Plus #33 near = 2.
    CHECK_MESSAGE(host.hazardRaceNumCount() == 2,
                  "the wrong-way rider at 208m should now be listed; the crash at 208m should not");

    host.shutdown();
}
