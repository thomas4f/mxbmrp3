// ============================================================================
// tests/integration/tests/engine_handoff_test.cpp
// The engine cull (core/engine_cull.h) at the handoff, through the real Draw
// export, in every render mode: HudManager::handOffToEngine runs exactly when
// the engine is going to draw the frame, and never when Direct GL drew it.
//
// Two gaps this pins, both on paths that are off by default. The cull used to
// sit in HudManager::draw(), ahead of Direct GL, so a frame GL drew still paid
// for a copy the engine was then handed empty. And the pluginThread path, which
// does not go through draw(), handed the engine the worker's frame UNCUT: every
// alpha-0 quad and empty string charged in full (~1 us a quad), on exactly the
// configuration meant to take load off the game thread.
//
// The GL cases need a display (Xvfb) and say so rather than passing:
//   Xvfb :99 & DISPLAY=:99 WINEPREFIX=/tmp/px ./run_tests.sh engine_handoff
// ============================================================================
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"

namespace {
constexpr const char* kDir = "Z:\\\\tmp\\\\mxbmrp3-tests\\\\engine_handoff\\\\";

// Draw on the worker path until it serves frames, the same way each time.
void drawThreaded(PluginHost& host, int frames) {
    for (int i = 0; i < frames; ++i) { host.draw(); host.pluginThreadFlush(); }
}
}  // namespace

TEST_CASE("engine handoff: the engine gets the frame without what draws nothing") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kDir);
    REQUIRE_MESSAGE(host.engineHandoffs() >= 0, "MXBMRP3_Test_EngineHandoffs missing");

    const int before = host.engineHandoffs();
    host.draw();
    CHECK(host.engineHandoffs() == before + 1);

    // A default frame carries invisible strings (a hidden title's placeholder
    // in nearly every HUD). Without any, the comparisons below would prove
    // nothing, so that is a failure, not a pass.
    REQUIRE_MESSAGE(host.gameFrameStrings() > host.lastGameStrings(),
                    "the default frame has nothing to cut - this test needs a new fixture");
    CHECK(host.gameFrameQuads() >= host.lastGameQuads());
    CHECK(host.lastGameQuads() > 0);
}

TEST_CASE("engine handoff: the pluginThread path cuts the same frame") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kDir);

    host.draw();
    const int syncQuads = host.lastGameQuads();
    const int syncStrings = host.lastGameStrings();
    REQUIRE(host.gameFrameStrings() > syncStrings);

    host.pluginThreadEnable();
    REQUIRE_MESSAGE(host.pluginThreadEnabled(), "worker did not start");
    drawThreaded(host, 12);

    CHECK_MESSAGE(host.lastGameStrings() < host.gameFrameStrings(),
                  "the threaded path handed the engine the uncut frame");
    CHECK(host.lastGameQuads() == syncQuads);
    CHECK(host.lastGameStrings() == syncStrings);

    host.pluginThreadStop();
}

TEST_CASE("engine handoff: a frame Direct GL draws skips the cull, in both modes") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kDir);
    if (!host.glMakeContext(true)) {
        MESSAGE("no GL context in this environment - cannot exercise the drawing path");
        return;
    }

    host.glInGame(true);
    for (int i = 0; i < 3; ++i) host.draw();
    if (!host.glDrewLastFrame()) {
        MESSAGE("GL backend declined to draw here; the decline case below covers that");
        return;
    }
    int before = host.engineHandoffs();
    for (int i = 0; i < 5; ++i) host.draw();
    CHECK(host.glDrewLastFrame());
    CHECK_MESSAGE(host.engineHandoffs() == before,
                  "the engine cull ran on frames Direct GL drew");
    CHECK(host.lastGameQuads() == 0);

    host.pluginThreadEnable();
    REQUIRE_MESSAGE(host.pluginThreadEnabled(), "worker did not start");
    drawThreaded(host, 12);
    REQUIRE(host.glDrewLastFrame());
    before = host.engineHandoffs();
    drawThreaded(host, 6);
    CHECK(host.glDrewLastFrame());
    CHECK_MESSAGE(host.engineHandoffs() == before,
                  "the engine cull ran on threaded frames Direct GL drew");
    host.pluginThreadStop();

    // Off again: the very next frame is the engine's, and cut.
    host.glInGame(false);
    before = host.engineHandoffs();
    host.draw();
    CHECK(host.engineHandoffs() == before + 1);
    CHECK(host.lastGameStrings() < host.gameFrameStrings());
}

TEST_CASE("engine handoff: when Direct GL cannot run, the engine still gets the cut frame") {
    // No context on the Draw thread: the backend declines, so the engine draws
    // and must get exactly the frame it gets with Direct GL off.
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kDir);

    host.draw();
    const int baseQuads = host.lastGameQuads();
    const int baseStrings = host.lastGameStrings();

    host.glInGame(true);
    const int before = host.engineHandoffs();
    for (int i = 0; i < 5; ++i) host.draw();
    CHECK(host.glDrewLastFrame() == false);
    CHECK(host.engineHandoffs() == before + 5);
    CHECK(host.lastGameQuads() == baseQuads);
    CHECK(host.lastGameStrings() == baseStrings);
    host.glInGame(false);
}
