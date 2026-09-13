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
//
// A second case pins the Oppo (nose-up whip), which shipped near-impossible in
// v1.30.1: the world-pitch peak was only sampled once the booked yaw had passed
// TURN_YAW_THRESHOLD (67.5°), but the nose is up EARLY in a whip while the bike
// lays over, and the game's body-frame yaw rate books that phase slowly (only
// cos(pitch) of the swing lands in the yaw channel) — so in a 165-trick session
// with 21 nose-up whips the peak came at 5–38° of booked yaw every time and
// nothing ever classified. The tracker now samples from PARTIAL_ROTATION_MIN
// (30°) on; the case also checks the mirror: a nose-up BEFORE the whip starts
// (a steep launch, a bailed flip) still isn't an oppo.
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
static constexpr int TYPE_ENDO       = 3;
static constexpr int TYPE_STOPPIE    = 4;
static constexpr int TYPE_AIR        = 11;
static constexpr int TYPE_BACKFLIP   = 12;
static constexpr int TYPE_WHIP_RIGHT = 19;
static constexpr int TYPE_OPPO_RIGHT = 23;

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

TEST_CASE("fmx: a whip with the nose up mid-rotation is an Oppo; a nose-up before the whip is not") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\fmx_oppo\\");
    REQUIRE(host.hasFmx());

    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(PRACTICE, 0, 480000);
    host.addEntry(10, "Alice");
    host.runInit(PRACTICE);
    host.runStart();

    long long t = 1'000'000;
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
    // Airborne frames with a body-frame yaw rate and a world-space nose angle.
    // pitch is what the game reports (negative = nose up); pitchVel stays 0 so
    // no flip accumulates and only the Oppo's world-pitch path is exercised.
    auto airTicks = [&](int n, float yawVel, float pitch) {
        for (int i = 0; i < n; ++i) {
            TelemetryRow r;
            r.frontMaterial = 0; r.rearMaterial = 0;
            r.yawVel = yawVel;
            r.pitch = pitch;
            tick(r);
        }
    };
    // Land, sit out the 0.75s grace and the 2s chain window: back to IDLE.
    auto landAndBank = [&]() { groundTicks(80); groundTicks(210); };

    groundTicks(20);

    // --- 1. The oppo. 0.6s at 60°/s books 36° of yaw with the nose level: past
    // the 0.5s airborne debounce and the 0.3s commit, a Whip R. Then the bike
    // lays over: 0.3s at 100°/s with the nose 60° up takes yaw to 66° — still a
    // whip, TURN_YAW_THRESHOLD not yet reached — and that is the ONLY window in
    // which the nose is up. The rider then levels off for the landing while yaw
    // passes 67.5°. Under the old gate the nose peak was never sampled (yaw was
    // under 67.5° the whole time it was up) and this stayed a Whip R.
    airTicks(60, 60.0f, 0.0f);
    {
        auto s = host.fmxState();
        CHECK(s.activeState == STATE_ACTIVE);
        CHECK(s.activeType == TYPE_WHIP_RIGHT);
    }
    airTicks(30, 100.0f, -60.0f);
    {
        auto s = host.fmxState();
        CHECK(s.activeType == TYPE_WHIP_RIGHT);   // 66° of yaw: not yet an oppo
    }
    airTicks(20, 100.0f, -20.0f);                 // -> 86°, nose already coming down
    {
        auto s = host.fmxState();
        CHECK(s.activeState == STATE_ACTIVE);
        CHECK(s.activeType == TYPE_OPPO_RIGHT);
    }
    landAndBank();
    {
        auto s = host.fmxState();
        CHECK(s.activeState == STATE_IDLE);
        CHECK(s.lastTrickType == TYPE_OPPO_RIGHT);
        CHECK(s.tricksCompleted == 1);
        CHECK(s.tricksFailed == 0);
    }

    // --- 2. The mirror. The nose is 70° up with NO yaw for 0.6s (a steep
    // kicker, or a flip the rider pulled out of) — plain Air — and only then
    // does the whip start, nose level, through 90°. The nose-up happened
    // before the whip's 30° gate, so it must not be sampled: a Whip R.
    airTicks(60, 0.0f, -70.0f);
    {
        auto s = host.fmxState();
        CHECK(s.activeState == STATE_ACTIVE);
        CHECK(s.activeType == TYPE_AIR);
    }
    airTicks(60, 150.0f, 0.0f);
    {
        auto s = host.fmxState();
        CHECK(s.activeType == TYPE_WHIP_RIGHT);
    }
    landAndBank();
    {
        auto s = host.fmxState();
        CHECK(s.activeState == STATE_IDLE);
        CHECK(s.lastTrickType == TYPE_WHIP_RIGHT);
        CHECK(s.tricksCompleted == 2);
        CHECK(s.tricksFailed == 0);
    }

    host.fmxSetNowUs(-1);
    host.runDeinit();
    host.shutdown();
}

TEST_CASE("fmx: a plain jump with no trick still counts as flight - time, height and distance") {
    // Air Miles used to be fed from LANDED TRICKS, so a normal MX jump - most
    // jumps - counted for nothing. FmxManager::updateFlight measures the flight
    // itself, which is also where the jump height and distance rows come from.
    // Nothing here attempts a trick: no rotation, no classification, no landing
    // bonus, and the numbers still move.
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\fmxflight\\");
    REQUIRE(host.hasFmx());
    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(PRACTICE, 0, 480000);
    host.addEntry(10, "Alice");
    host.runInit(PRACTICE);
    host.runStart();

    long long t = 1'000'000;
    auto tick = [&](TelemetryRow r) {
        t += 10'000;
        r.time = t / 1.0e6f;
        host.fmxSetNowUs(t);
        host.telemetryFrame(r);
    };
    // Rolling along the ground at the takeoff altitude.
    float x = 0.0f;
    auto ground = [&](int n) {
        for (int i = 0; i < n; ++i) {
            TelemetryRow r; r.speed = 20.0f; x += 0.2f; r.posX = x; r.posY = 100.0f;
            tick(r);
        }
    };
    ground(20);

    // A one-second flight: 20 m/s across the ground, rising 5 m and coming back
    // down. Position steps stay under the 2 m/frame teleport guard.
    for (int i = 0; i < 100; ++i) {
        TelemetryRow r;
        r.speed = 20.0f; r.frontMaterial = 0; r.rearMaterial = 0;
        x += 0.2f; r.posX = x;
        const float u = i / 99.0f;                 // 0..1 through the flight
        r.posY = 100.0f + 5.0f * 4.0f * u * (1.0f - u);   // a 5 m parabola
        tick(r);
    }
    // The landing closes the flight, but nothing is credited until the rider
    // has stayed upright for landingGracePeriod (0.75s = 75 frames) - see the
    // crash case below. 100 frames clears it with room to spare.
    ground(1);
    // WHAT THE HUD PRINTS, read while the trick is still up: for a jump its
    // distance and peak height are the FLIGHT's, so they are the same numbers
    // the jump rows are about to be granted. The distance used to be a path
    // summed from wherever the air trick classified - 0.3s of flight later -
    // so the HUD read short of the achievement it shared a jump with.
    const auto shown = host.fmxTrickStats();
    ground(99);

    CHECK(host.achievementValue("air_miles") == doctest::Approx(1.0).epsilon(0.05));
    CHECK(host.achievementValue("jump_height") == doctest::Approx(5.0).epsilon(0.05));
    const double across = 20.0;   // 100 frames x 0.2 m
    CHECK(host.achievementValue("jump_distance") == doctest::Approx(across).epsilon(0.05));
    CHECK(host.achievementValue("air_distance") == doctest::Approx(across / 1000.0).epsilon(0.05));
    // ...and the two agree, within the one frame between the last airborne
    // sample and the touchdown the flight is measured to.
    CHECK(shown.distance ==
          doctest::Approx(host.achievementValue("jump_distance")).epsilon(0.02));
    CHECK(shown.peakHeight ==
          doctest::Approx(host.achievementValue("jump_height")).epsilon(0.02));
    CHECK(shown.duration ==
          doctest::Approx(host.achievementValue("fmx_airtime")).epsilon(0.03));
    // No trick was attempted, so the trick counters stay put.
    CHECK(host.achievementValue("fmx_tricks") == doctest::Approx(0.0));

    // Wheel chatter over braking bumps is not a jump: both wheels leave the
    // ground for a few frames at a time, and counting it would turn a lap of
    // chop into a jump session.
    const double airBefore = host.achievementValue("air_miles");
    for (int b = 0; b < 20; ++b) {
        for (int i = 0; i < 8; ++i) {   // 80ms off the ground, under the floor
            TelemetryRow r; r.speed = 20.0f; r.frontMaterial = 0; r.rearMaterial = 0;
            x += 0.2f; r.posX = x; r.posY = 100.0f;
            tick(r);
        }
        ground(4);
    }
    CHECK(host.achievementValue("air_miles") == doctest::Approx(airBefore));

    host.fmxSetNowUs(-1);
    host.runStop();
    host.runDeinit();
    host.shutdown();
}

TEST_CASE("fmx: a rhythm section credits every jump, not just the last one") {
    // The settle window holds ONE flight. Rhythm-section jumps land a quarter
    // to three quarters of a second apart, which is inside it, so each landing
    // arrived while the previous one was still pending - and silently took its
    // slot. A new touchdown is proof the rider rode away from the one before,
    // so it banks it rather than overwriting it.
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\fmxrhythm\\");
    REQUIRE(host.hasFmx());
    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(PRACTICE, 0, 480000);
    host.addEntry(10, "Alice");
    host.runInit(PRACTICE);
    host.runStart();

    long long t = 1'000'000;
    float x = 0.0f;
    auto tick = [&](TelemetryRow r) {
        t += 10'000;
        r.time = t / 1.0e6f;
        host.fmxSetNowUs(t);
        host.telemetryFrame(r);
    };
    auto ground = [&](int n) {
        for (int i = 0; i < n; ++i) {
            TelemetryRow r; r.speed = 20.0f; x += 0.2f; r.posX = x; r.posY = 100.0f;
            tick(r);
        }
    };
    // Four jumps of half a second each, 200ms of ground between them - well
    // inside the 750ms window, so every landing lands on a pending slot.
    auto hop = [&]() {
        for (int i = 0; i < 50; ++i) {
            TelemetryRow r;
            r.speed = 20.0f; r.frontMaterial = 0; r.rearMaterial = 0;
            x += 0.2f; r.posX = x;
            const float u = i / 49.0f;
            r.posY = 100.0f + 3.0f * 4.0f * u * (1.0f - u);
            tick(r);
        }
        ground(20);
    };
    ground(20);
    for (int j = 0; j < 4; ++j) hop();
    ground(100);        // ride out the last one's window

    // Four half-second flights: two seconds, not the half second the last one
    // would have contributed on its own.
    CHECK(host.achievementValue("air_miles") == doctest::Approx(2.0).epsilon(0.05));
    // Each hop covers 10 m, so the cumulative air distance is four of them.
    CHECK(host.achievementValue("air_distance") == doctest::Approx(0.040).epsilon(0.05));
    // Four hops 3 m high: twelve metres of climb, in kilometres.
    CHECK(host.achievementValue("air_height") == doctest::Approx(0.012).epsilon(0.05));
    // The records are per-jump, so they read one hop, not the sum. Hang Time is
    // among them now: it reads the longest FLIGHT, so a hop nobody tricked on
    // still sets it - which is the whole reason it moved off the trick path.
    CHECK(host.achievementValue("fmx_airtime") == doctest::Approx(0.5).epsilon(0.05));
    CHECK(host.achievementValue("jump_height") == doctest::Approx(3.0).epsilon(0.05));
    CHECK(host.achievementValue("jump_distance") == doctest::Approx(10.0).epsilon(0.05));

    host.fmxSetNowUs(-1);
    host.runStop();
    host.runDeinit();
    host.shutdown();
}

TEST_CASE("fmx: a jump you case out of pays the totals but sets no record") {
    // The three jump rows and Air Miles are records, so a crash landing that
    // banked one would be permanent: `Sent It` at Platinum for a fall down a
    // hillside. `crashed` arrives a few frames AFTER the impact, so crediting
    // on wheel contact banked exactly that - the flight is held for
    // landingGracePeriod first and dropped if the rider goes down inside it.
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\fmxcase\\");
    REQUIRE(host.hasFmx());
    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(PRACTICE, 0, 480000);
    host.addEntry(10, "Alice");
    host.runInit(PRACTICE);
    host.runStart();

    long long t = 1'000'000;
    auto tick = [&](TelemetryRow r) {
        t += 10'000;
        r.time = t / 1.0e6f;
        host.fmxSetNowUs(t);
        host.telemetryFrame(r);
    };
    float x = 0.0f;
    auto ground = [&](int n, int crashed) {
        for (int i = 0; i < n; ++i) {
            TelemetryRow r; r.speed = crashed ? 0.0f : 20.0f;
            x += crashed ? 0.0f : 0.2f; r.posX = x; r.posY = 100.0f;
            r.crashed = crashed;
            tick(r);
        }
    };
    ground(20, 0);

    // The same one-second, 5 m parabola the clean-landing case flies.
    for (int i = 0; i < 100; ++i) {
        TelemetryRow r;
        r.speed = 20.0f; r.frontMaterial = 0; r.rearMaterial = 0;
        x += 0.2f; r.posX = x;
        const float u = i / 99.0f;
        r.posY = 100.0f + 5.0f * 4.0f * u * (1.0f - u);
        tick(r);
    }
    // Wheels down, then the case-out three frames later - well inside the
    // settle window - and the rider stays down past the end of it.
    ground(3, 0);
    ground(120, 1);
    ground(120, 0);

    // THE SPLIT, which is what the Air group is built on. The flight touched
    // down, so the three SUMS bank it: you were in the air, and they measure
    // how much air. The rider then went down inside the settle window, so the
    // three MAXIMA take nothing - a personal best has to be ridden away from.
    //
    // This case used to assert zero across all six, back when a case-out threw
    // the whole measurement away. That rule made the totals lie about how much
    // flying a session contained, which is the opposite of what a lifetime
    // total is for.
    CHECK(host.achievementValue("air_miles") == doctest::Approx(1.0).epsilon(0.05));
    CHECK(host.achievementValue("air_distance") == doctest::Approx(0.020).epsilon(0.05));
    CHECK(host.achievementValue("air_height") == doctest::Approx(0.005).epsilon(0.05));
    CHECK(host.achievementValue("fmx_airtime") == doctest::Approx(0.0));
    CHECK(host.achievementValue("jump_height") == doctest::Approx(0.0));
    CHECK(host.achievementValue("jump_distance") == doctest::Approx(0.0));

    host.fmxSetNowUs(-1);
    host.runStop();
    host.runDeinit();
    host.shutdown();
}

// A STOPPIE IS AN ENDO HELD UNTIL THE BIKE STOPS - the classifier splits the two
// on speed alone, and the Endo row counts both. That last clause lived only in a
// comment beside the sample flag: the trick type, the sample, the lifetime
// counter and the achievement row are four hops apart and nothing walked them.
TEST_CASE("fmx: a stoppie is an endo all the way to the row") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\fmxendo\\");
    REQUIRE(host.hasFmx());
    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(PRACTICE, 0, 480000);
    host.addEntry(10, "Alice");
    host.runInit(PRACTICE);
    host.runStart();

    long long t = 1'000'000;
    float x = 0.0f;
    auto tick = [&](TelemetryRow r) {
        t += 10'000;
        x += r.speed * 0.01f;
        r.posX = x;
        r.posY = 100.0f;
        r.time = t / 1.0e6f;
        host.fmxSetNowUs(t);
        host.telemetryFrame(r);
    };
    // Both wheels down, nose level: nothing to classify.
    auto rolling = [&](int n) {
        for (int i = 0; i < n; ++i) { TelemetryRow r; r.speed = 10.0f; tick(r); }
    };
    // On the front brake: rear wheel up and the nose past the 15 degree entry
    // gate. Speed is what the classifier reads to tell the two apart.
    auto noseDown = [&](int n, float speed) {
        for (int i = 0; i < n; ++i) {
            TelemetryRow r;
            r.speed = speed;
            r.pitch = 25.0f;
            r.frontMaterial = 1;
            r.rearMaterial = 0;
            tick(r);
        }
    };

    rolling(20);
    REQUIRE(host.achievementValue("fmx_endos") == doctest::Approx(0.0));

    // A TAP IS NOT THE TRICK. Two seconds nose-down and ridden away from: it
    // banks, and the row still does not move, because the row is the HOLD. The
    // value it keeps is the longest one held, so this is what it reads until a
    // longer one comes along.
    // (A trick opens about half a second into the nose-down - the ground-trick
    // entry gate - so the held time is always short of the ticks fed here. Both
    // cases are placed well clear of the 3s line rather than beside it.)
    noseDown(200, 8.0f);
    rolling(300);
    REQUIRE(host.fmxState().tricksCompleted == 1);
    CHECK(host.achievementValue("fmx_endos") >= 1.0);
    CHECK(host.achievementValue("fmx_endos") < 3.0);
    CHECK(host.achievementTier("fmx_endos") == 0);

    // IT HAS TO BEGIN AS AN ENDO. shouldStartTrick() will not open a trick on a
    // stationary bike (that is the stuck-on-the-fence guard), so a stoppie is
    // only ever reached by braking into one - which is what one is.
    noseDown(250, 8.0f);
    CHECK(host.fmxState().activeType == TYPE_ENDO);
    // ...and rolling to a stop over the 2.5 m/s line makes it a stoppie. The
    // two are one trick reclassified, not two, so the hold is the whole run.
    noseDown(250, 1.0f);
    CHECK(host.fmxState().activeType == TYPE_STOPPIE);

    // Wheel down and ride away, past the landing grace (0.75s) and the chain
    // window (2s), which is what banks it - and banking is what the row's
    // "land" means, here and on every other trick row.
    rolling(300);
    CHECK(host.fmxState().tricksCompleted == 2);
    CHECK(host.fmxState().lastTrickType == TYPE_STOPPIE);
    CHECK(host.achievementValue("fmx_endos") >= 3.0);
    CHECK(host.achievementTier("fmx_endos") == 1);

    host.runDeinit();
    host.eventDeinit();
    host.shutdown();
}

TEST_CASE("fmx: the trick strip keeps its units, and gives up the tenth of a second before it overruns") {
    // Four measurements with their unit letters, spaced so triple-digit values
    // do not touch. Four such values and the tenth of a second do not fit the
    // 27-character panel together, so the strip is measured with the tenth and
    // rebuilt without it only when it would overrun - see FmxHud::addTrickStatsRow.
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\fmxstrip\\");
    REQUIRE(host.hasFmx());
    REQUIRE(host.hasStringRows());
    REQUIRE(host.hasIconTheming());
    // The harness stages no icons, so the four glyphs would resolve to sprite 0
    // and the strip would degrade to bare values - narrower than the game ever
    // draws it. A theme override per glyph gives each a sprite, and with it the
    // width the row actually spends on icons. Installed before the first draw:
    // the HUD caches the four indices once.
    host.installTheme("strip", 3.0f, 1.0f, /*titleBand=*/1, /*card=*/1);
    REQUIRE(host.setThemeIcon("stopwatch",        900001, 7));
    REQUIRE(host.setThemeIcon("ruler-horizontal", 900002, 7));
    REQUIRE(host.setThemeIcon("ruler-vertical",   900003, 7));
    REQUIRE(host.setThemeIcon("border-top-left",  900004, 7));
    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(PRACTICE, 0, 480000);
    host.addEntry(10, "Alice");
    host.runInit(PRACTICE);
    host.runStart();
    host.setHudVisible("fmx_hud", true);

    long long t = 1'000'000;
    float x = 0.0f;
    auto tick = [&](TelemetryRow r) {
        t += 10'000;
        x += r.speed * 0.01f;
        r.posX = x;
        r.time = t / 1.0e6f;
        host.fmxSetNowUs(t);
        host.telemetryFrame(r);
    };
    // A backflip: `frames` of air at `speed`, rising `rise` metres, at `pitchVel`.
    // Two grounded frames after it land the trick into GRACE, where the strip
    // holds its final numbers.
    auto flip = [&](int frames, float speed, float rise, float pitchVel) {
        for (int i = 0; i < 20; ++i) { TelemetryRow r; r.speed = speed; r.posY = 100.0f; tick(r); }
        for (int i = 0; i < frames; ++i) {
            TelemetryRow r;
            r.speed = speed; r.frontMaterial = 0; r.rearMaterial = 0;
            r.pitchVel = pitchVel;
            const float u = i / static_cast<float>(frames - 1);
            r.posY = 100.0f + rise * 4.0f * u * (1.0f - u);
            tick(r);
        }
        for (int i = 0; i < 2; ++i) { TelemetryRow r; r.speed = speed; r.posY = 100.0f; tick(r); }
        host.draw();
    };
    // The strip's strings: the values on the row that carries the seconds.
    auto strip = [&]() {
        std::vector<std::string> out;
        double rowY = -1.0;
        for (const auto& r : host.hudStringRows("fmx_hud")) {
            if (r.text.size() >= 2 && r.text.back() == 's' && std::isdigit(static_cast<unsigned char>(r.text[0]))) {
                rowY = r.y;
                break;
            }
        }
        for (const auto& r : host.hudStringRows("fmx_hud")) {
            if (rowY >= 0.0 && std::abs(r.y - rowY) < 1e-6) out.push_back(r.text);
        }
        return out;
    };

    // AN ORDINARY BACKFLIP: 1.4 s, 25 m, 6 m up, ~420 degrees. Tenths kept,
    // every value with its letter, and the values start a glyph's width in
    // from the content edge, where the section heading starts.
    flip(140, 18.0f, 6.5f, -300.0f);
    auto s = strip();
    REQUIRE(s.size() == 4);
    CHECK(s[0] == "1.4s");
    CHECK(s[1] == "25m");
    CHECK(s[2] == "6m");
    CHECK(s[3].back() == 'd');
    {
        double headingX = -1.0, valueX = -1.0;
        for (const auto& r : host.hudStringRows("fmx_hud")) {
            if (r.text == "Trick Stack") headingX = r.x;
            if (r.text == "1.4s") valueX = r.x;
        }
        REQUIRE(headingX >= 0.0);
        CHECK(valueX > headingX);
    }

    // THE EXTREME: 12.3 s of air at 40 m/s, 120 m up, 1230 degrees - four values
    // of three or more digits. With the tenth the row would overrun the panel,
    // so the seconds go whole; nothing else changes.
    for (int i = 0; i < 200; ++i) { TelemetryRow r; r.speed = 40.0f; r.posY = 100.0f; tick(r); }   // grace out, chain settles
    flip(1230, 40.0f, 120.0f, -100.0f);
    s = strip();
    REQUIRE(s.size() == 4);
    CHECK(s[0] == "12s");
    CHECK(s[1] == "492m");
    CHECK(s[2] == "120m");
    CHECK(s[3].size() == 5);          // "12xxd": the integration lands within a degree or two
    CHECK(s[3].back() == 'd');

    host.shutdown();
}

TEST_CASE("fmx: a lost chain freezes its multiplier in red beside its score, and holds while the rider is down") {
    // The Score and Chain values, and the trick stack, linger the ended chain
    // from FmxManager's ChainEndAnimation snapshot; the multiplier was recomputed
    // from the live chain, which the manager had just emptied, so it read 1.0 in
    // plain text the instant a chain broke - the one number a player wants to
    // see is the one they lost. And like the rotation arcs, the whole block
    // holds past the manager's linger for as long as the rider is crashed.
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\fmxmult\\");
    REQUIRE(host.hasFmx());
    REQUIRE(host.hasStringRows());
    REQUIRE(host.hasInkHooks());
    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(PRACTICE, 0, 480000);
    host.addEntry(10, "Alice");
    host.runInit(PRACTICE);
    host.runStart();
    host.setHudVisible("fmx_hud", true);

    long long t = 1'000'000;
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
            r.frontMaterial = 0; r.rearMaterial = 0;
            r.pitchVel = pitchVel;
            tick(r);
        }
    };
    // The multiplier is the one "d.d" string on the panel, with the "x" emitted
    // right after it - one block, one colour; the Chain value is the string
    // emitted right after its "Chain" label. Each with its colour.
    struct Ink { std::string text; unsigned long color = 0; bool found = false; };
    auto readInk = [&](Ink& mult, Ink& chain) {
        host.draw();
        mult = Ink{}; chain = Ink{};
        const auto rows = host.hudStringRows("fmx_hud");
        for (size_t i = 0; i < rows.size(); ++i) {
            const std::string& s = rows[i].text;
            if (s.size() == 3 && std::isdigit(static_cast<unsigned char>(s[0])) && s[1] == '.' &&
                std::isdigit(static_cast<unsigned char>(s[2]))) {
                mult = { s, host.stringColor("fmx_hud", static_cast<int>(i)), true };
                REQUIRE(i + 1 < rows.size());
                CHECK(rows[i + 1].text == "x");
                CHECK(host.stringColor("fmx_hud", static_cast<int>(i + 1)) == mult.color);
            }
            if (s == "Chain" && i + 1 < rows.size()) {
                chain = { rows[i + 1].text, host.stringColor("fmx_hud", static_cast<int>(i + 1)), true };
            }
        }
        REQUIRE(mult.found);
        REQUIRE(chain.found);
    };
    // The trick strip is on screen when a string reads "<digits>.<digit>s".
    auto hasStrip = [&]() {
        for (const auto& r : host.hudStringRows("fmx_hud")) {
            const std::string& s = r.text;
            if (s.size() >= 4 && std::isdigit(static_cast<unsigned char>(s[0])) && s.back() == 's' &&
                s[s.size() - 2] != 'm' && s[s.size() - 3] == '.') return true;
        }
        return false;
    };

    groundTicks(20);
    // One backflip banked into a chain, a second one in the air on top of it:
    // the multiplier on screen is the pair's, above a single trick's 1.0.
    airTicks(150, -300.0f);
    groundTicks(80);
    REQUIRE(host.fmxState().activeState == STATE_CHAIN);
    airTicks(150, -300.0f);
    REQUIRE(host.fmxState().chainCount == 1);
    Ink liveMult, liveChain;
    readInk(liveMult, liveChain);
    CHECK(liveMult.text != "1.0");
    CHECK(hasStrip());

    // A crash in the landing grace breaks the chain. For the end animation the
    // multiplier holds the value that was on screen, in the Chain value's red.
    // The rider is down on the track-position side too, which is what the HUD's
    // crash hold reads (as the arcs do).
    groundTicks(10);
    { TelemetryRow r; r.crashed = 1; tick(r); }
    host.raceTrackPosition({ { .num = 10, .trackPos = 0.5f, .crashed = 1 } });
    groundTicks(10);
    REQUIRE(host.fmxState().chainCount == 0);
    Ink lostMult, lostChain;
    readInk(lostMult, lostChain);
    CHECK(lostMult.text == liveMult.text);
    CHECK(lostMult.color == lostChain.color);
    CHECK(lostMult.color != liveMult.color);

    // The manager's linger runs out (chainPeriod, 2 s) with the rider still
    // down: the block holds, value and colour, until they are up again.
    groundTicks(210);
    Ink downMult, downChain;
    readInk(downMult, downChain);
    CHECK(downMult.text == liveMult.text);
    CHECK(downMult.color == lostMult.color);
    CHECK(downChain.text == lostChain.text);
    CHECK(downChain.color == lostChain.color);
    CHECK(hasStrip());                       // the trick's measurements hold too

    // Back on the bike: 1.0 in plain text again, the chain value back to zero.
    host.raceTrackPosition({ { .num = 10, .trackPos = 0.5f, .crashed = 0 } });
    groundTicks(2);
    Ink afterMult, afterChain;
    readInk(afterMult, afterChain);
    CHECK(afterMult.text == "1.0");
    CHECK(afterMult.color == liveMult.color);
    CHECK(afterChain.text == "0");
    CHECK(!hasStrip());

    host.fmxSetNowUs(-1);
    host.runDeinit();
    host.shutdown();
}
