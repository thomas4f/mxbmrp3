// ============================================================================
// tests/integration/tests/settings_idempotency_test.cpp
// Apply-path coverage: serializing, loading, and re-serializing must be a fixed
// point. save (A) -> loadSettings -> save (B) must produce a byte-identical file.
//
// Why this matters (and what it guards that the boolean persist test does not):
// the round trip forces applyProfile() to read back EVERY serialized value — every
// enum (stringToX), float (validateX), int, and bitmask, at its real default — and
// re-capture it. If any apply branch drops a key, mis-parses an enum, or clamps a
// float differently on the way in, B diverges from A. The persist test only flips
// booleans, so this is the coverage that makes a capture/apply refactor safe: an
// apply block that silently fails to apply a non-boolean value is caught here.
//
// Two rounds (A->B and B->C) also prove the fixed point is reached immediately, not
// merely converging, so there is no "settles after N loads" drift hiding in apply.
//
// Self-contained doctest; see run_tests.sh / TESTING.md.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include "ini.h"

#include <string>

TEST_CASE("settings idempotency: save -> load -> save is byte-identical") {
    const char* saveWin = "Z:\\tmp\\mxbmrp3-tests\\settings_idempotency\\";
    const std::string iniPath =
        "Z:\\tmp\\mxbmrp3-tests\\settings_idempotency\\mxbmrp3\\mxbmrp3_settings.ini";

    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(saveWin);

    host.save();
    const std::string A = ini::readFile(iniPath);
    REQUIRE_MESSAGE(!A.empty(), "no settings.ini written at " << iniPath);

    // Load A back into live state (drives applyProfile over every section), then
    // re-serialize. A capture/apply round trip that preserves behavior is a no-op.
    host.loadSettings(saveWin);
    host.save();
    const std::string B = ini::readFile(iniPath);
    CHECK_MESSAGE(A == B,
                  "save->load->save changed the settings file: applyProfile did not "
                  "faithfully round-trip some value (enum/float/int/bitmask). See the "
                  "first differing region.");

    // Second round: prove it's an immediate fixed point, not slow convergence.
    host.loadSettings(saveWin);
    host.save();
    const std::string C = ini::readFile(iniPath);
    CHECK(B == C);

    host.shutdown();
}
