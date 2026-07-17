// ============================================================================
// tests/integration/tests/rumble_effect_test.cpp
// Rumble EFFECT MATH — the telemetry→vibration computation users tune in the
// Rumble tab — through the real RunTelemetry path: the handler derives the
// inputs (suspension compression from m_afSuspVelocity, overrun/underrun ratios
// from wheel vs vehicle speed, airborne from wheel contact) and
// updateRumbleFromTelemetry() maps them through each RumbleEffect's
// min/max/strength ramp. The outputs are in-game-only (the rumble graph + the
// motor feed, never /api/state), so they're read via MXBMRP3_Test_RumbleChannels.
// The send POLICY (idle-silence, rate cap, quantization) is pinned separately
// by xinput_thread_test; rumble FEEL stays a manual real-controller check.
//
// Invariants pinned:
//  1. Zero telemetry (grounded, rolling, no slip) → every channel and both
//     motors are exactly 0 — no phantom buzz.
//  2. The default wheelspin/lockup ramps map the handler-derived slip ratios
//     to the documented values (min/max window + strength scaling).
//  3. A suspension spike drives the Bumps channel scaled by the per-bike
//     PROFILE's strength (JSON → effect), and doubling the JSON strength
//     doubles the output.
//  4. Airborne suppresses ground effects (bumps) but keeps RPM at half.
//  5. Profile JSON robustness: a malformed FILE doesn't crash and falls back
//     to the global config; a malformed single ENTRY is skipped without
//     discarding the sibling profiles (and `version` stays informational).
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"

#include <fstream>
#include <string>

static constexpr int PRACTICE = 1;

// The save dir run_tests.sh pre-creates for this test (Z:\ = the unix root).
static const char* SAVE = "Z:\\tmp\\mxbmrp3-tests\\rumble_effect\\";
static const char* PROFILE_PATH =
    "Z:\\tmp\\mxbmrp3-tests\\rumble_effect\\mxbmrp3\\mxbmrp3_rumble_profiles.json";

static void writeProfiles(const std::string& jsonText) {
    std::ofstream f(PROFILE_PATH, std::ios::trunc);
    REQUIRE(f.good());
    f << jsonText;
}

TEST_CASE("rumble effect math: default ramps, profile-JSON scaling, airborne, malformed-JSON fallback") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(SAVE);
    REQUIRE(host.hasRumbleMath());

    // eventInit's bike name ("Test 450") becomes the RumbleProfileManager's
    // current bike — the key the profile JSON below is stored under.
    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(PRACTICE, 0, 480000);
    host.addEntry(10, "Alice");
    host.runInit(PRACTICE);
    host.runStart();

    host.xinputSetIndex(0);       // a selected slot (a -1 index skips the engine entirely)
    host.rumbleSetEnabled(true);  // feed the motors (channels compute either way)

    // --- 1. Grounded, rolling, no slip: everything is exactly zero. ---------
    host.telemetryFrame(TelemetryRow{});
    {
        auto c = host.rumbleChannels();
        CHECK(c.susp == 0.0f);   CHECK(c.spin == 0.0f);  CHECK(c.lock == 0.0f);
        CHECK(c.wheelie == 0.0f); CHECK(c.rpm == 0.0f);  CHECK(c.slide == 0.0f);
        CHECK(c.surface == 0.0f); CHECK(c.steer == 0.0f);
        CHECK(c.heavy == 0.0f);  CHECK(c.light == 0.0f);
    }

    // --- 2a. Default wheelspin ramp (0..15, light 0.5): rear wheel at 25 m/s
    // over 10 m/s vehicle speed → overrun (25-10)/10 = 1.5 → norm 1.5/15 = 0.1
    // → channel 0.1*0.5 = 0.05 on the LIGHT motor only. -----------------------
    {
        TelemetryRow r;
        r.wheelSpeedRear = 25.0f;
        host.telemetryFrame(r);
        auto c = host.rumbleChannels();
        CHECK(c.spin == doctest::Approx(0.05f).epsilon(0.01));
        CHECK(c.light == doctest::Approx(0.05f).epsilon(0.01));
        CHECK(c.heavy == 0.0f);
        CHECK(c.lock == 0.0f);   // a FASTER wheel is spin, never lockup
    }

    // --- 2b. Default lockup ramp (0.2..1.0, light 0.5): front wheel at 2 m/s
    // under 10 m/s → underrun 0.8 → norm (0.8-0.2)/0.8 = 0.75 → channel 0.375. -
    {
        TelemetryRow r;
        r.wheelSpeedFront = 2.0f;
        host.telemetryFrame(r);
        auto c = host.rumbleChannels();
        CHECK(c.lock == doctest::Approx(0.375f).epsilon(0.01));
        CHECK(c.light == doctest::Approx(0.375f).epsilon(0.01));
        CHECK(c.spin == 0.0f);
    }

    // --- 3. Bumps via the per-bike profile JSON. Bumps is OFF by default, so a
    // response at all proves the JSON reached the effect; the value pins the
    // ramp: compression 5 m/s on a 0..10 ramp → norm 0.5 → ×0.8 heavy = 0.4. --
    writeProfiles(R"({"version":1,"profiles":{"Test 450":{"effects":{
        "suspension":{"minInput":0.0,"maxInput":10.0,"lightStrength":0.0,"heavyStrength":0.8},
        "rpm":{"minInput":0.0,"maxInput":10000.0,"lightStrength":1.0,"heavyStrength":0.0}
    }}}})");
    host.rumbleLoadProfiles(SAVE);
    host.rumbleSetPerBike(true);
    CHECK(host.rumbleHasProfile());

    TelemetryRow spike;
    spike.suspVelFront = -5.0f;   // game sign: negative = compressing
    host.telemetryFrame(spike);
    {
        auto c = host.rumbleChannels();
        CHECK(c.susp == doctest::Approx(0.4f).epsilon(0.01));
        CHECK(c.heavy == doctest::Approx(0.4f).epsilon(0.01));
        CHECK(c.light == 0.0f);
        CHECK(c.suspRear == 0.0f);   // not split: rear trace unused
    }

    // Halving the JSON strength halves the output — the profile strength is a
    // linear multiplier, not a threshold.
    writeProfiles(R"({"version":1,"profiles":{"Test 450":{"effects":{
        "suspension":{"minInput":0.0,"maxInput":10.0,"lightStrength":0.0,"heavyStrength":0.4}
    }}}})");
    host.rumbleLoadProfiles(SAVE);
    host.telemetryFrame(spike);
    CHECK(host.rumbleChannels().susp == doctest::Approx(0.2f).epsilon(0.01));

    // --- 4. Airborne (both wheels off): the same spike produces NO bumps, and
    // RPM drops to half strength (engine under less load), not to zero. -------
    writeProfiles(R"({"version":1,"profiles":{"Test 450":{"effects":{
        "suspension":{"minInput":0.0,"maxInput":10.0,"lightStrength":0.0,"heavyStrength":0.8},
        "rpm":{"minInput":0.0,"maxInput":10000.0,"lightStrength":1.0,"heavyStrength":0.0}
    }}}})");
    host.rumbleLoadProfiles(SAVE);
    {
        TelemetryRow r = spike;
        r.rpm = 5000;
        host.telemetryFrame(r);               // grounded reference: rpm norm 0.5
        auto g = host.rumbleChannels();
        CHECK(g.susp == doctest::Approx(0.4f).epsilon(0.01));
        CHECK(g.rpm == doctest::Approx(0.5f).epsilon(0.01));

        r.frontMaterial = 0; r.rearMaterial = 0;
        host.telemetryFrame(r);               // airborne: bumps gone, rpm halved
        auto a = host.rumbleChannels();
        CHECK(a.susp == 0.0f);
        CHECK(a.rpm == doctest::Approx(0.25f).epsilon(0.01));
        CHECK(a.light == doctest::Approx(0.25f).epsilon(0.01));
        CHECK(a.heavy == 0.0f);
    }

    // --- 5a. A malformed FILE: parse fails, no crash, profiles cleared — the
    // engine falls back to the global config (Bumps off), so the spike now
    // produces nothing even with per-bike mode still on. ----------------------
    writeProfiles("{ this is not json !!!");
    host.rumbleLoadProfiles(SAVE);
    CHECK_FALSE(host.rumbleHasProfile());
    host.telemetryFrame(spike);
    {
        auto c = host.rumbleChannels();
        CHECK(c.susp == 0.0f);
        CHECK(c.heavy == 0.0f);
    }

    // --- 5b. A malformed single ENTRY is isolated: the sibling profile still
    // loads (and a mismatched `version` is informational, not a discard). -----
    writeProfiles(R"({"version":99,"profiles":{
        "Broken": 42,
        "Test 450":{"effects":{
            "suspension":{"minInput":0.0,"maxInput":10.0,"lightStrength":0.0,"heavyStrength":0.8}
        }}
    }})");
    host.rumbleLoadProfiles(SAVE);
    CHECK(host.rumbleHasProfile());
    host.telemetryFrame(spike);
    CHECK(host.rumbleChannels().susp == doctest::Approx(0.4f).epsilon(0.01));

    host.runDeinit();
    host.shutdown();
}
