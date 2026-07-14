// ============================================================================
// tests/integration/tests/xinput_thread_test.cpp
// XInput controller I/O now runs on a dedicated thread so a slow/degraded driver
// can't stall whichever thread drives telemetry/hotkeys. The blocking XInputSetState
// moved off-thread, but the rumble SEND POLICY (idle-silence, first-send, transition
// -to-zero, disabled-guard) and the 8-bit quantization stay on the caller in
// setVibration() — CLAUDE.md warns that policy must not change. This asserts those
// branches are preserved by inspecting the command setVibration posts to the I/O
// thread (with the I/O thread stopped so it can't drain the post first).
//
// It cannot test rumble FEEL or the degraded-Bluetooth latency benefit (no controller
// under Wine) — those need a real-controller pass on Windows. What it DOES lock down:
// the policy logic and quantization survived the refactor, and the I/O thread's
// start/stop/join lifecycle is exercised by every test's startup/shutdown.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include "assertions.h"

TEST_CASE("xinput thread: rumble send policy + quantization preserved off-thread") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\xinput\\");

    // Stop the I/O thread so it doesn't consume the posted command before we read it.
    host.xinputStopIo();
    host.xinputSetIndex(0);   // select controller 0 (enables sends; resets policy)

    // First send always posts (m_hasSentVibration was reset), and the values quantize
    // to 8 bits: 0.5*255+0.5 = 128, 0.75*255+0.5 = 191.
    host.xinputVibrate(0.5f, 0.75f);
    {
        auto p = host.xinputConsumePending();
        REQUIRE(p.posted);
        CHECK(p.left8 == 128);
        CHECK(p.right8 == 191);
        CHECK(p.idx == 0);
    }

    // An immediate identical nonzero re-send is CAPPED (the rate cap prevents flooding
    // the Bluetooth stack): back-to-back calls fall inside the send-interval window, so
    // nothing new is posted. (The post-interval keepalive re-send is time-based and not
    // asserted here — it needs a real clock; the cap suppression IS deterministic.)
    host.xinputVibrate(0.5f, 0.75f);
    CHECK_FALSE(host.xinputConsumePending().posted);

    // Transition to (0,0) posts a zero immediately — it bypasses the rate cap so a stop
    // is never swallowed (a capped stop could leave the motors running if telemetry halts).
    host.xinputVibrate(0.0f, 0.0f);
    {
        auto p = host.xinputConsumePending();
        REQUIRE(p.posted);
        CHECK(p.left8 == 0);
        CHECK(p.right8 == 0);
    }

    // Idle: (0,0) after (0,0) posts NOTHING — an off pad generates no traffic.
    host.xinputVibrate(0.0f, 0.0f);
    CHECK_FALSE(host.xinputConsumePending().posted);

    // Disabled controller (-1): setVibration is a no-op, nothing posted.
    host.xinputSetIndex(-1);
    host.xinputVibrate(0.5f, 0.5f);
    CHECK_FALSE(host.xinputConsumePending().posted);

    host.shutdown();
}
