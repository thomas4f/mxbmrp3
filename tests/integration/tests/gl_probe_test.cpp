// ============================================================================
// tests/integration/tests/gl_probe_test.cpp
// The Phase 0 GL feasibility probe (core/gl_probe.h, [Advanced] glProbe).
//
// The probe's HEADLINE answer — is a GL context current on the game's Draw
// thread — cannot be reached from here; it needs the game. What CAN be pinned
// headlessly is everything that would waste the author's one game launch, or
// worse, harm a player who leaves the key on:
//
//  1) THE PROBE DISTURBS THE HANDOFF BY EXACTLY ONE KNOWN QUAD, AND ONLY AT
//     MODE 2. Mode 1 is read-only and changes nothing at all. Mode 2 appends a
//     single engine-drawn reference bar - the cyan half of the pair that makes
//     the coordinate mapping verifiable (see core/gl_probe.cpp). Pinning the
//     exact count is what would catch the probe leaking the HUD's own geometry
//     into the engine frame, or drawing its GL quad into it by mistake: "one
//     more than baseline" is a much stronger statement than "roughly the same".
//
//  2) NO CONTEXT => NO DRAW. Even at glProbe=2, the drawing path must not run
//     without a context current. This is the branch a non-GL game takes, and
//     the harness (which has no context of its own) takes it naturally.
//
//  3) With a real context — Wine gives one when a display exists — the probe's
//     OWN MACHINERY is
//     exercised against a real driver: the conservative save/restore, the
//     fingerprint diff, and the pixel readback. That is the part that would
//     otherwise be written blind and debugged through the author's screen.
//     Its central assertion is stateDiffs == 0: the probe drew inside a live
//     context and left it exactly as it found it.
//
//     To run that half locally:
//       Xvfb :99 & DISPLAY=:99 WINEPREFIX=/tmp/px ./run_tests.sh gl_probe
//     Without a display there is no GL and the test says so rather than
//     pretending; the headless branch still covers (1) and (2) in full.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"

#include <chrono>
#include <thread>

namespace {
// Status field indices, mirroring MXBMRP3_Test_GlProbeStatus.
constexpr int kRan = 0, kModuleResident = 1, kEntryPoints = 2, kContextCurrent = 3;
constexpr int kGlVersion = 4, kCompatProfile = 5, kDrew = 6, kReadbackMatched = 7;
constexpr int kStateDiffs = 8, kGlErrors = 9, kDrawGaps = 10, kLastGapMs = 11;
}  // namespace

TEST_CASE("gl probe: changes the engine handoff by exactly its reference bar") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\gl_probe\\");

    // Baseline with the probe off.
    host.draw();
    const int baseQuads = host.lastGameQuads();
    const int baseStrings = host.lastGameStrings();
    REQUIRE(baseQuads > 0);

    // Report-only. Read-only by construction: no draw, no state change, and
    // not one primitive added.
    host.glProbe(1);
    for (int i = 0; i < 5; ++i) host.draw();
    CHECK(host.glProbeStatus(kRan) == 1);
    CHECK(host.lastGameQuads() == baseQuads);
    CHECK(host.lastGameStrings() == baseStrings);
    CHECK(host.glProbeStatus(kDrew) == 0);   // mode 1 never draws, context or not

    // Report + draw: exactly one extra quad, the engine's reference bar. The
    // GL quad goes to the GL context and must never appear here as well.
    host.glProbe(2);
    for (int i = 0; i < 5; ++i) host.draw();
    CHECK(host.lastGameQuads() == baseQuads + 1);
    CHECK(host.lastGameStrings() == baseStrings);

    // Off again, and still unchanged.
    host.glProbe(0);
    host.draw();
    CHECK(host.lastGameQuads() == baseQuads);
    CHECK(host.lastGameStrings() == baseStrings);
}

TEST_CASE("gl probe: no context means no draw, whatever the mode") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\gl_probe_nogl\\");

    // No context has been made current on this thread, so this is the branch a
    // non-GL game takes — the probe must report honestly and do nothing.
    host.glProbe(2);
    for (int i = 0; i < 5; ++i) host.draw();

    CHECK(host.glProbeStatus(kRan) == 1);
    CHECK(host.glProbeStatus(kContextCurrent) == 0);
    CHECK(host.glProbeStatus(kDrew) == 0);
    // Nothing was measured, so the state diff must read "not measured" (-1)
    // rather than the 0 that would be indistinguishable from "measured clean".
    CHECK(host.glProbeStatus(kStateDiffs) == -1);
}

TEST_CASE("gl probe: against a real context, the restore is airtight") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\gl_probe_ctx\\");

    host.draw();
    const int baseQuads = host.lastGameQuads();
    REQUIRE(baseQuads > 0);

    if (!host.glMakeContext(true)) {
        // No display / no GL in this environment. Say so out loud: a silent pass
        // here would read as "the restore was verified" when nothing ran.
        MESSAGE("no GL context available in this environment (headless Wine) - "
                "the save/restore path is covered only by the in-game probe here");
        CHECK(host.glProbeStatus(kContextCurrent) == 0);
        return;
    }

    host.glProbe(2);
    // More frames than the probe's verify budget, so both the verifying frames
    // and the steady-state frames after it are exercised.
    for (int i = 0; i < 8; ++i) host.draw();

    CHECK(host.glProbeStatus(kModuleResident) == 1);
    CHECK(host.glProbeStatus(kEntryPoints) == 1);
    CHECK(host.glProbeStatus(kContextCurrent) == 1);
    CHECK(host.glProbeStatus(kGlVersion) > 0);

    // wglCreateContext always yields a compatibility context, so fixed function
    // is there and the probe should have drawn. If a driver ever answers
    // otherwise, that is worth seeing rather than skipping past.
    if (host.glProbeStatus(kCompatProfile) == 1) {
        CHECK(host.glProbeStatus(kDrew) == 1);
        // THE assertion this test exists for: the probe drew inside a live GL
        // context and left every sampled piece of state exactly as it found it.
        CHECK(host.glProbeStatus(kStateDiffs) == 0);
        CHECK(host.glProbeStatus(kGlErrors) == 0);
        // The readback proves the draw reached the framebuffer. Not asserted:
        // a 0x0 or otherwise unreadable surface is a property of this headless
        // environment, not of the probe, and the in-game run is what answers it.
        MESSAGE("probe readback matched: " << host.glProbeStatus(kReadbackMatched));
    } else {
        MESSAGE("core-profile context - the probe correctly declined to draw");
        CHECK(host.glProbeStatus(kDrew) == 0);
    }

    // Still exactly the one reference bar, now with a context present: having a
    // real GL context must not change what goes to the engine.
    CHECK(host.lastGameQuads() == baseQuads + 1);

    host.glProbe(0);
    // The context teardown is the host destructor's job (unconditionally, so
    // that an early return or a failed assertion above cannot skip it).
}

TEST_CASE("gl probe: the Draw heartbeat sees a real gap and ignores a normal frame") {
    // The claim under test is not the probe's - it is the REPO's: "the plugin
    // gets no callbacks in menus", documented in three places and load-bearing
    // for anything that has to know when the game stops drawing, but never
    // actually measured on a build. The
    // heartbeat is what turns it into evidence, and a heartbeat that miscounts
    // would produce confident wrong evidence. So both directions are pinned.
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\gl_probe\\");

    host.glProbe(1);
    for (int i = 0; i < 5; ++i) host.draw();   // back-to-back: no gap
    CHECK(host.glProbeStatus(kDrawGaps) == 0);

    // A pause longer than the 250ms threshold: exactly what leaving the track
    // for a menu looks like from inside Draw.
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    host.draw();
    CHECK(host.glProbeStatus(kDrawGaps) == 1);
    CHECK(host.glProbeStatus(kLastGapMs) >= 350);   // slack for scheduling
    CHECK(host.glProbeStatus(kLastGapMs) < 5000);

    // Normal frames after it must not keep incrementing.
    for (int i = 0; i < 5; ++i) host.draw();
    CHECK(host.glProbeStatus(kDrawGaps) == 1);
    host.glProbe(0);
}

TEST_CASE("gl probe: the measurement load draws clean on both submission paths") {
    // The batched path builds a client vertex array and calls glDrawArrays -
    // the newest and least-exercised GL code here. Running it first against a
    // real driver costs nothing; discovering a fault in it on Thomas's machine
    // costs a game launch, which is the scarce resource in this loop.
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\gl_probe\\");

    host.draw();
    const int baseQuads = host.lastGameQuads();
    REQUIRE(baseQuads > 0);

    if (!host.glMakeContext(true)) {
        MESSAGE("no GL context in this environment - measurement path unexercised here");
        return;
    }
    host.glProbe(2);

    for (int batch = 0; batch <= 1; ++batch) {
        host.glProbeLoad(256, batch);
        for (int i = 0; i < 6; ++i) host.draw();
        // The load must not disturb the context any more than the single probe
        // quad does: same clean restore, same absence of GL errors.
        CHECK(host.glProbeStatus(kStateDiffs) == 0);
        CHECK(host.glProbeStatus(kGlErrors) == 0);
        // And it is drawn IN-CONTEXT, so the engine's frame is untouched by it -
        // only the one reference bar mode 2 adds.
        CHECK(host.lastGameQuads() == baseQuads + 1);
    }

    host.glProbeLoad(0, 1);
    host.glProbe(0);
}

TEST_CASE("gl probe: the sweep refuses to report GL numbers it did not measure") {
    // The report's most dangerous failure is in the FLATTERING direction. GL rows
    // draw nothing unless glProbe=2 with a live context, and a sweep that measured
    // nothing produces rows costing zero - which reads as "in-context GL is free"
    // and would pass the plan's bar with a perfect score. render_probe_sweep.cpp's
    // header records that this exact class of silent non-experiment already
    // happened once here, producing five internally-perfect reports.
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\\\tmp\\\\mxbmrp3-tests\\\\gl_probe\\\\");

    // No context, and the probe has never drawn: the guard must fire, and no
    // verdict may be printed off those numbers.
    const std::string blind = host.probeSweepReport(0.0020, 0.0018, 0.0004, /*glUs=*/0.0);
    REQUIRE(!blind.empty());
    CHECK(blind.find("GL ROWS MEASURED NOTHING") != std::string::npos);
    CHECK(blind.find("=> PASS by the plan's bar") == std::string::npos);
}

TEST_CASE("gl probe: the sweep's verdict follows the plan's bar in both directions") {
    // A bar that only ever says PASS is not a bar. Both directions are driven
    // through the real report builder with injected per-quad costs.
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\\\tmp\\\\mxbmrp3-tests\\\\gl_probe\\\\");

    if (!host.glMakeContext(true)) {
        MESSAGE("no GL context here - the verdict branch needs one to pass the guard");
        return;
    }
    host.glProbe(2);
    for (int i = 0; i < 4; ++i) host.draw();
    REQUIRE(host.glProbeStatus(6) == 1);   // drew: the guard's other condition

    // Engine 0.0020 us/quad, GL a tenth of that: majority recovered, and
    // 6000 quads costs 0.0012 ms - far inside the 2.08 ms budget.
    const std::string good = host.probeSweepReport(0.0020, 0.0018, 0.0004, 0.0002);
    CHECK(good.find("GL ROWS MEASURED NOTHING") == std::string::npos);
    CHECK(good.find("=> PASS by the plan's bar") != std::string::npos);

    // GL barely cheaper than the engine: a marginal win, which the plan says
    // explicitly does not justify a second renderer.
    const std::string marginal = host.probeSweepReport(0.0020, 0.0018, 0.0004, 0.0019);
    CHECK(marginal.find("=> FAILS the plan's bar") != std::string::npos);

    // And the budget half of the bar, independent of the ratio: GL recovers 90%
    // of a very expensive engine, yet 0.5 us/quad x 6000 = 3.0 ms still blows the
    // 2.08 ms budget. Both halves of the bar have to be able to fail alone, or
    // one of them is decoration.
    const std::string overBudget = host.probeSweepReport(5.0, 0.0018, 0.0004, 0.5);
    CHECK(overBudget.find("=> FAILS the plan's bar") != std::string::npos);

    host.glProbe(0);
}

TEST_CASE("gl probe: the measurement load actually paints, with the game's bindings up") {
    // THE REGRESSION THIS PINS shipped a PASS. The first in-game sweep reported
    // in-context GL recovering 99.8% and then 101.0% of the engine's per-quad
    // cost - a negative per-quad cost - because the batched path used client
    // vertex arrays without unbinding the VBO and VAO the game leaves up across
    // the Draw callback. glVertexPointer then reads its pointer as an offset
    // into that VBO, the draw renders nothing, and "nothing" times any N is a
    // perfect score on a performance bar.
    //
    // Timing alone can never catch this: a load that draws nothing looks exactly
    // like a load that is free. Only a readback can tell them apart, which is
    // why loadPainted exists.
    constexpr int kLoadPainted = 12;

    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\\\tmp\\\\mxbmrp3-tests\\\\gl_probe\\\\");

    if (!host.glMakeContext(true)) {
        MESSAGE("no GL context here - the load-paints check needs one");
        return;
    }
    host.glProbe(2);

    // Clean state first: establishes that the load paints at all.
    for (int batch = 0; batch <= 1; ++batch) {
        host.glProbeLoad(256, batch);
        for (int i = 0; i < 6; ++i) host.draw();
        CHECK_MESSAGE(host.glProbeStatus(kLoadPainted) == 1,
                      "measurement load drew nothing on clean state, batch=" << batch);
    }

    // Now with a VBO and VAO bound, which is what the game actually does.
    if (!host.glBindGameLikeState()) {
        MESSAGE("this context has no VBO/VAO entry points - cannot simulate the game");
        host.glProbe(0);
        return;
    }
    for (int batch = 0; batch <= 1; ++batch) {
        host.glProbeLoad(256, batch);
        for (int i = 0; i < 6; ++i) host.draw();
        CHECK_MESSAGE(host.glProbeStatus(kLoadPainted) == 1,
                      "measurement load drew nothing with a VBO/VAO bound, batch=" << batch
                      << " - this is the bug that reported a 101% speedup");
        // And it must still leave the context as it found it.
        CHECK(host.glProbeStatus(8) == 0);   // stateDiffs
        CHECK(host.glProbeStatus(9) == 0);   // glErrors
    }

    host.glProbeLoad(0, 1);
    host.glProbe(0);
}

TEST_CASE("gl probe: a noise-level GL delta cannot produce a PASS") {
    // The exact shape of what shipped. Run 2 of the first in-game sweep measured
    // a NEGATIVE per-quad cost for in-context GL and reported "101.0% recovered,
    // PASS". Both numbers are impossible; the deltas were single-digit
    // microseconds against a ~1800 us frame, i.e. noise divided by N.
    //
    // Three things must hold now: no verdict from a load that drew nothing, no
    // per-quad figure manufactured out of sub-noise deltas, and no percentage
    // above 100 - which is not a better result, it is the tell.
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\\\tmp\\\\mxbmrp3-tests\\\\gl_probe\\\\");

    if (!host.glMakeContext(true)) {
        MESSAGE("no GL context here - the report guards need one to get past the context check");
        return;
    }
    host.glProbe(2);
    for (int i = 0; i < 4; ++i) host.draw();
    REQUIRE(host.glProbeStatus(6) == 1);

    // glUs = 0 models a load that drew nothing: zero cost is exactly what
    // "free" and "absent" look like to a timer.
    const std::string nothing = host.probeSweepReport(0.0020, 0.0018, 0.0004, 0.0);
    CHECK(nothing.find("GL ROWS DREW NOTHING") != std::string::npos);
    CHECK(nothing.find("=> PASS") == std::string::npos);

    // A NEGATIVE per-quad cost - physically impossible, and what the first
    // field run reported as "101.0% recovered". With paint separately
    // confirmed, a cost this far below the noise floor may legitimately pass
    // AS A BOUND; what must never happen again is a negative VALUE or a
    // percentage above 100 being presented as a measurement.
    const std::string negative = host.probeSweepReport(0.0020, 0.0018, 0.0004, -0.001);
    CHECK(negative.find("BELOW THIS INSTRUMENT'S RESOLUTION") != std::string::npos);
    CHECK(negative.find("us/quad\n  in-context, batched: -") == std::string::npos);
    CHECK(negative.find("-0.") == std::string::npos);   // no negative figure anywhere
    CHECK(negative.find("101.0%") == std::string::npos);
    // No percentage over 100 anywhere in the report.
    for (int pct = 101; pct <= 130; ++pct) {
        CHECK(negative.find(std::to_string(pct) + ".0%") == std::string::npos);
    }

    // A real, comfortably-above-noise win still passes, and says so plainly.
    const std::string real = host.probeSweepReport(0.0020, 0.0018, 0.0004, 0.0002);
    CHECK(real.find("=> PASS by the plan's bar") != std::string::npos);

    // Sub-resolution deltas that do NOT scale with N must never yield a
    // per-quad VALUE. This is the shape the real machine produced twice after
    // the no-op was fixed: deltas of -19..+31 us at every N, largest at N=6000
    // by less than the scatter. A synthetic per-quad cost so small that even
    // N=6000 stays inside the scatter models it.
    const std::string subRes = host.probeSweepReport(0.0020, 0.0018, 0.0004, 0.000002);
    CHECK(subRes.find("BELOW THIS INSTRUMENT'S RESOLUTION") != std::string::npos);
    CHECK(subRes.find("is defensible: <") != std::string::npos);

    // ...and the converse, which is what makes the scaling test a test rather
    // than a rubber stamp: a cost large enough to scale cleanly with N must be
    // reported as a VALUE. An earlier version compared the largest-N row against
    // a scatter that included itself, so this branch was unreachable and every
    // result, however large, came out as "below resolution".
    const std::string scaled = host.probeSweepReport(0.0020, 0.0018, 0.0004, 0.0002);
    CHECK(scaled.find("BELOW THIS INSTRUMENT'S RESOLUTION") == std::string::npos);
    CHECK(scaled.find("in-context, batched: 0.00020 us/quad") != std::string::npos);

    host.glProbe(0);
}

TEST_CASE("gl probe: no verdict without confirmed paint, in all three states") {
    // THIS PINS A PRECONDITION, not a behaviour. The report allows a cost below
    // the noise floor to PASS as an upper bound. That is only sound because the
    // load is independently confirmed to have PAINTED - otherwise "too small to
    // measure" and "never drew" are the same reading, which is precisely the
    // failure that produced a 101%-recovered PASS earlier in this spike.
    //
    // So the relaxation and its precondition are now tested together. If a later
    // change removes, weakens or bypasses the paint check, this fails rather
    // than silently re-opening the hole the relaxation was allowed through.
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\\\tmp\\\\mxbmrp3-tests\\\\gl_probe\\\\");

    if (!host.glMakeContext(true)) {
        MESSAGE("no GL context here - these branches sit behind the context check");
        return;
    }
    host.glProbe(2);
    for (int i = 0; i < 4; ++i) host.draw();
    REQUIRE(host.glProbeStatus(6) == 1);

    // A cost far below the noise floor, with paint in each of its three states.
    // The COST IS IDENTICAL across all three - only the engagement evidence
    // differs, which is exactly the distinction timing alone cannot make.
    constexpr double kSubNoise = 0.000002;

    const std::string painted   = host.probeSweepReport(0.0020, 0.0018, 0.0004, kSubNoise, 1);
    const std::string drewNone  = host.probeSweepReport(0.0020, 0.0018, 0.0004, kSubNoise, 0);
    const std::string unchecked = host.probeSweepReport(0.0020, 0.0018, 0.0004, kSubNoise, -1);

    // Confirmed: a bound and a verdict are allowed.
    CHECK(painted.find("BELOW THIS INSTRUMENT'S RESOLUTION") != std::string::npos);
    CHECK(painted.find("=> PASS by the plan's bar") != std::string::npos);

    // Drew nothing: no verdict, and the reason said out loud.
    CHECK(drewNone.find("GL ROWS DREW NOTHING") != std::string::npos);
    CHECK(drewNone.find("=> PASS") == std::string::npos);
    CHECK(drewNone.find("=> FAILS") == std::string::npos);

    // Never checked is NOT "fine". Treating -1 as a pass is how a guard against
    // an unverified experiment quietly becomes decorative.
    CHECK(unchecked.find("ENGAGEMENT UNVERIFIED") != std::string::npos);
    CHECK(unchecked.find("=> PASS") == std::string::npos);
    CHECK(unchecked.find("=> FAILS") == std::string::npos);

    host.glProbe(0);
}
