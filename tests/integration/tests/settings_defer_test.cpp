// ============================================================================
// tests/integration/tests/settings_defer_test.cpp
// Settings auto-save is DEFERRED: a HUD drag/toggle only marks settings dirty (markDirty),
// applying the change live but writing NOTHING to disk. Serializing settings costs a couple
// of milliseconds, which would be a visible frame spike if done while the player is on track,
// so the write waits for a moment where a hitch doesn't matter — leaving the track (pit/exit),
// handled by flushIfDirty() on the RunStop/RunDeinit transitions.
//
// This pins the contract: markDirty() writes nothing; flushIfDirty() then writes exactly once;
// and a flush with nothing dirty is a no-op (no needless disk churn on every session exit).
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"

#include <cstdio>       // std::remove
#include <fstream>
#include <sstream>
#include <string>

static std::string readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

TEST_CASE("settings save is deferred: markDirty writes nothing, flushIfDirty persists") {
    const char* saveWin = "Z:\\tmp\\mxbmrp3-tests\\settings_defer\\";
    const std::string iniPath =
        "Z:\\tmp\\mxbmrp3-tests\\settings_defer\\mxbmrp3\\mxbmrp3_settings.ini";

    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(host.hasMarkDirty());
    host.startup(saveWin);   // loads defaults into live state (creates the mxbmrp3\ dir)

    // Clear any file the load path created, so the ONLY writer below is our flush.
    std::remove(iniPath.c_str());
    REQUIRE(readFile(iniPath).empty());

    CHECK_FALSE(host.isDirty());        // clean after load

    // A settings edit marks dirty (drives the Save button) but must NOT touch disk.
    host.markDirty();
    host.markDirty();
    CHECK(host.isDirty());              // Save button would light up
    CHECK(readFile(iniPath).empty());  // still nothing on disk

    // Leaving the track flushes it (Auto-Save on by default) — one synchronous, atomic write.
    host.flushIfDirty();

    const std::string ini = readFile(iniPath);
    CHECK_FALSE(ini.empty());                                   // the deferred save landed
    CHECK(ini.find("[Settings]") != std::string::npos);        // a real settings file,
    CHECK(ini.find("[Profiles]") != std::string::npos);        // not truncated/partial
    CHECK_FALSE(host.isDirty());                               // cleared -> button greys to "Saved"

    // A second flush with nothing dirty is a no-op: delete the file and confirm the flush
    // doesn't rewrite it (so exiting a session with no edits never churns the file).
    std::remove(iniPath.c_str());
    host.flushIfDirty();
    CHECK(readFile(iniPath).empty());

    // Manual mode (Auto-Save off): leaving the track must NOT auto-flush — the user persists
    // explicitly. markDirty still tracks (so the Save button lights), flushIfDirty is a no-op,
    // and the manual Save button (host.save()) writes regardless of Auto-Save.
    host.setAutoSave(false);
    host.markDirty();
    CHECK(host.isDirty());
    host.flushIfDirty();
    CHECK(readFile(iniPath).empty());   // Auto-Save off -> leave-track wrote nothing
    CHECK(host.isDirty());              // still pending

    host.save();                        // manual Save button
    CHECK_FALSE(readFile(iniPath).empty());
    CHECK_FALSE(host.isDirty());        // manual save cleared it
}
