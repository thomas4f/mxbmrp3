// ============================================================================
// tests/integration/tests/fmx_test.cpp
// FMX trick detection + scoring (FmxManager), driven through the real
// RunTelemetry path. The whole state machine is WALL-CLOCK timed (dt
// integration of angular velocity, the 0.5s airborne debounce, the 0.75s
// landing grace, the 2s chain window), so back-to-back headless callbacks give
// dt≈0 and nothing ever advances — the injectable Fmx clock
// (MXBMRP3_Test_FmxSetNowUs, mirroring the director's sim clock) steps
// simulated time 10ms per frame (the game's ~100Hz telemetry rate) so the
// timing plays out deterministically. The score/chain state is in-game-only
// (never in /api/state), so it's read via MXBMRP3_Test_FmxState.
//
// Three invariants pinned, in one continuous session:
//  1. A sub-debounce hop (0.3s airborne, no rotation) banks NOTHING — the
//     airborne debounce + the unclassified-discard path (not a false trick).
//  2. A sustained airborne full-pitch rotation classifies as a Backflip,
//     survives the landing grace, and its score is banked into the session
//     total when the chain window expires — the real detection→score pipeline.
//  3. A crash during the landing grace kills the trick (grace exists exactly
//     for this): counted as failed, session score untouched.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"

static constexpr int PRACTICE = 1;

// Fmx::TrickState / Fmx::TrickType values (fmx_types.h) — the hook returns ints.
static constexpr int STATE_IDLE   = 0;
static constexpr int STATE_ACTIVE = 1;
static constexpr int STATE_CHAIN  = 3;
static constexpr int TYPE_BACKFLIP = 12;

TEST_CASE("fmx: airborne debounce rejects a hop; a backflip is detected, graced, and banked; a grace crash fails it") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\fmx\\");
    REQUIRE(host.hasFmx());

    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(PRACTICE, 0, 480000);   // RaceSession resets FmxManager — fire it before ticking
    host.addEntry(10, "Alice");
    host.runInit(PRACTICE);
    host.runStart();                     // FmxManager gates on isPlayerRunning

    // Simulated 100Hz telemetry: advance the Fmx clock 10ms, then feed the frame.
    // Steps stay well under the 200ms pause-compensation threshold. Position
    // advances with speed so the teleport guard and trick distance see a
    // physically consistent ride.
    long long t = 1'000'000;   // µs; arbitrary epoch on the simulated steady clock
    float x = 0.0f;
    auto tick = [&](TelemetryRow r) {
        t += 10'000;
        x += r.speed * 0.01f;
        r.posX = x;
        r.time = t / 1.0e6f;
        host.fmxSetNowUs(t);
        host.telemetryFrame(r);
    };
    auto groundTicks = [&](int n) { for (int i = 0; i < n; ++i) tick(TelemetryRow{}); };
    auto airTicks = [&](int n, float pitchVel) {
        for (int i = 0; i < n; ++i) {
            TelemetryRow r;
            r.frontMaterial = 0; r.rearMaterial = 0;   // both wheels off
            r.pitchVel = pitchVel;                     // deg/s; negative = backflip direction
            tick(r);
        }
    };

    groundTicks(20);   // settle: IDLE, dt baseline established

    // --- 1. A 0.3s rotation-free hop: under the 0.5s airborne debounce, so
    // hasBeenAirborne never latches, nothing classifies, and the landing
    // discards the attempt without scoring or counting a failure.
    airTicks(30, 0.0f);
    groundTicks(10);
    {
        auto s = host.fmxState();
        CHECK(s.activeState == STATE_IDLE);
        CHECK(s.sessionScore == 0);
        CHECK(s.tricksCompleted == 0);
        CHECK(s.tricksFailed == 0);
        CHECK(s.chainCount == 0);
    }

    // --- 2. The backflip: 1.5s airborne at -300°/s pitches through ~444° of
    // accumulated backward rotation — past the 270° full-rotation threshold —
    // so the trick classifies as BACKFLIP while still in the air.
    airTicks(150, -300.0f);
    {
        auto s = host.fmxState();
        CHECK(s.activeState == STATE_ACTIVE);
        CHECK(s.activeType == TYPE_BACKFLIP);
    }

    // Land (both wheels down after confirmed airtime ends the trick) and hold
    // through the 0.75s landing grace: the trick banks into the chain.
    groundTicks(80);
    int bankedChainScore = 0;
    {
        auto s = host.fmxState();
        CHECK(s.activeState == STATE_CHAIN);
        CHECK(s.chainCount == 1);
        CHECK(s.chainScore > 0);
        CHECK(s.lastTrickType == TYPE_BACKFLIP);
        CHECK(s.sessionScore == 0);   // chain not banked into the session yet
        bankedChainScore = s.chainScore;
    }

    // Ride out the 2s chain window with no follow-up trick: the chain completes
    // and its score (x1.0 multiplier for a single trick) lands in the session.
    groundTicks(210);
    int sessionAfterBank = 0;
    {
        auto s = host.fmxState();
        CHECK(s.activeState == STATE_IDLE);
        CHECK(s.sessionScore == bankedChainScore);
        CHECK(s.sessionScore > 0);
        CHECK(s.tricksCompleted == 1);
        CHECK(s.tricksFailed == 0);
        CHECK(s.chainCount == 0);
        sessionAfterBank = s.sessionScore;
    }

    // --- 3. Same backflip, but a crash 100ms into the landing grace: the grace
    // window exists to catch exactly this, so the trick fails (no score) and
    // the session total is untouched.
    airTicks(150, -300.0f);
    groundTicks(10);   // in GRACE, still upright
    {
        TelemetryRow r;
        r.crashed = 1;
        tick(r);
    }
    groundTicks(10);
    {
        auto s = host.fmxState();
        CHECK(s.activeState == STATE_IDLE);
        CHECK(s.sessionScore == sessionAfterBank);
        CHECK(s.tricksCompleted == 1);
        CHECK(s.tricksFailed == 1);
        CHECK(s.chainCount == 0);
    }

    host.fmxSetNowUs(-1);   // restore the real clock before teardown
    host.runDeinit();
    host.shutdown();
}
