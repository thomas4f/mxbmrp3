// ============================================================================
// tests/integration/tests/settings_heavy_steps_test.cpp
// HEAVY WORK IN THE SETTINGS MENU HAPPENS ONCE, NOT PER REPEAT OR PER REBUILD.
//
// 1. Holding an arrow repeats only where a step is cheap. A press on a settings arrow that repeats fires at once and again while held;
// every other press fires on release (and can slide off to abort). 1.32 moved
// controls that used to be release-fired buttons onto the shared cycle
// control, whose arrows all repeated: holding "HUD display" opened and closed
// the companion window (a thread start and join on the game thread) every
// repeat, and holding Voice pack or TTS voice played a preview every repeat
// (1.32 review). Those steps carry CycleControl::repeat = false.
//
//    Pinned: the heavy rows the test install draws (HUD display, Voice pack,
//    Update channel) fire once; an ordinary tab's cycles still repeat.
// 2. The Spotter tab reads its pack folders and SAPI voices (a directory scan
//    and a COM enumeration) once per menu opening. 1.32 moved both from its
//    click handlers into the tab's render, which runs twice per rebuild, and
//    the panel rebuilds on every hover change (1.32 review). Under Wine the
//    voice list is empty, so this counts the reads rather than timing them.
//
//    Pinned too: opening the menu on ANOTHER tab reads nothing. Every opening
//    lays out every tab to measure the tallest, Spotter included, and the
//    lists were read in that pass, so SAPI's cold first enumeration (it loads
//    the speech runtime) hitched the first menu opening of a session for
//    players who never open this tab (1.32, before release).
// 3. The SAPI voices are read once per SESSION, at plugin load, never by the
//    menu: the enumeration is a COM round trip worth tens of ms on Windows,
//    so even a once-per-opening read hitched the frame the Spotter tab drew.
//    A voice installed mid-session shows after a game restart.
// ============================================================================
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"

#include <cstdio>

TEST_CASE("settings: heavy cycle steps fire once; ordinary cycles still repeat") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\settings_heavy_steps\\");
    auto Once = host.sym<int (*)()>("MXBMRP3_Test_SettingsOnceCycleCount");
    REQUIRE_MESSAGE(Once, "MXBMRP3_Test_SettingsOnceCycleCount not exported (test build?)");

    host.showSettings(true);
    auto onceOn = [&](const char* tab) {
        host.setActiveTab(tab);
        host.draw();
        const int n = Once();
        std::printf("%s: %d once, %d cycles\n", tab, n, host.cycleCount(true));
        return n;
    };
    // The test install has no themes, packs or SAPI voices, so the rows with
    // nothing to pick are greyed and draw no arrows: what is left here is HUD
    // display on Appearance and Voice pack on Spotter (the shipped default).
    CHECK(onceOn("Appearance") >= 1);
    CHECK(onceOn("Spotter") >= 1);
    host.setDeveloperMode(true);     // Update channel is a developer row
    CHECK(onceOn("Updates") >= 1);   // each step starts an update check
    host.setDeveloperMode(false);
    host.setActiveTab("Standings");
    host.draw();
    REQUIRE(host.cycleCount(true) > 0);
    CHECK(Once() == 0);
    host.showSettings(false);
    host.shutdown();
}

TEST_CASE("settings: the Spotter tab reads its packs and voices once per opening") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\settings_heavy_steps\\");
    auto Reads = host.sym<int (*)()>("MXBMRP3_Test_SpotterListReads");
    REQUIRE_MESSAGE(Reads, "MXBMRP3_Test_SpotterListReads not exported (test build?)");

    host.showSettings(true);
    host.setActiveTab("Appearance");
    host.draw();
    CHECK_MESSAGE(Reads() == 0, "opening the menu read the Spotter lists without showing the tab");
    host.setActiveTab("Spotter");
    host.draw();
    const int first = Reads();
    CHECK(first >= 1);
    // Rebuilds with the menu open: tab switches, back and forth.
    for (int i = 0; i < 5; ++i) {
        host.setActiveTab("Appearance");
        host.draw();
        host.setActiveTab("Spotter");
        host.draw();
    }
    CHECK(Reads() == first);
    // A new opening reads them again (a pack installed meanwhile shows).
    host.showSettings(false);
    host.draw();
    host.showSettings(true);
    host.setActiveTab("Spotter");
    host.draw();
    CHECK(Reads() == first + 1);
    host.showSettings(false);
    host.shutdown();
}

TEST_CASE("settings: the SAPI voices are read once, at plugin load") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\settings_heavy_steps\\");
    auto Voices = host.sym<int (*)()>("MXBMRP3_Test_TtsVoiceReads");
    REQUIRE_MESSAGE(Voices, "MXBMRP3_Test_TtsVoiceReads not exported (test build?)");
    const int atLoad = Voices();
    CHECK(atLoad == 1);
    for (int i = 0; i < 3; ++i) {
        host.showSettings(true);
        host.setActiveTab("Spotter");
        host.draw();
        host.showSettings(false);
        host.draw();
    }
    CHECK(Voices() == atLoad);
    host.shutdown();
}
