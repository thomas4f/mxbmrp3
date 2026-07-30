// ============================================================================
// tests/integration/tests/version_test.cpp
// Update-checker version comparison (the pure, deterministic core of the auto-
// update feature — "is a newer version available") via the
// MXBMRP3_Test_CompareVersions hook. No game state, no HTTP — just loads the DLL
// and exercises the exported comparator. Self-contained doctest; see run_tests.sh.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include <windows.h>
#include <string>

typedef int (*PFN_Cmp)(const char*, const char*);

// sign() so we assert ordering, not the raw magnitude the comparator returns.
static int sgn(int v) { return (v < 0) ? -1 : (v > 0) ? 1 : 0; }

TEST_CASE("update-checker: numeric, per-component version ordering") {
    HMODULE h = LoadLibraryA(dllPath());
    REQUIRE(h != nullptr);
    auto cmp = (PFN_Cmp)GetProcAddress(h, "MXBMRP3_Test_CompareVersions");
    REQUIRE(cmp != nullptr);
    auto order = [&](const char* a, const char* b) { return sgn(cmp(a, b)); };

    // Numeric ordering, not lexicographic (the classic "1.10 < 1.9" string bug).
    CHECK(order("1.10.0.0", "1.9.0.0")   ==  1);
    CHECK(order("1.9.0.0",  "1.10.0.0")  == -1);
    CHECK(order("2.0.0.0",  "1.99.99.99")==  1);

    // Equality and per-component precedence.
    CHECK(order("1.25.3.0", "1.25.3.0") == 0);
    CHECK(order("1.25.3.1", "1.25.3.0") == 1);   // build component
    CHECK(order("1.26.0.0", "1.25.9.9") == 1);   // minor beats patch/build
    CHECK(order("1.0.0.0",  "1.0.0.0")  == 0);

    // Short forms normalize (missing components = 0).
    CHECK(order("1.25",  "1.25.0.0") == 0);
    CHECK(order("1.26",  "1.25.9")   == 1);

    FreeLibrary(h);
}

// ============================================================================
// The three constant exports the game reads BEFORE it will talk to the plugin
// at all. Trivial to implement, not trivial in consequence: the interface and
// mod-data versions are the handshake that decides whether the game loads this
// DLL and which struct layouts it will hand it, so a wrong number here is a
// plugin that silently doesn't load or one that gets fed the wrong shapes.
//
// mxb_api.cpp static_asserts the literals against the adapter's constants, so
// this adds the half the compiler can't see: that the EXPORTS exist under the
// exact names the game looks up, and answer what the adapter agreed to.
// (API_COVERAGE.md's last ⚪ row.)
// ============================================================================
TEST_CASE("api handshake: GetModID / GetModDataVersion / GetInterfaceVersion") {
    HMODULE h = LoadLibraryA(dllPath());
    REQUIRE(h != nullptr);

    auto modId = (char* (*)())GetProcAddress(h, "GetModID");
    auto modDataVersion = (int (*)())GetProcAddress(h, "GetModDataVersion");
    auto interfaceVersion = (int (*)())GetProcAddress(h, "GetInterfaceVersion");
    REQUIRE(modId != nullptr);
    REQUIRE(modDataVersion != nullptr);
    REQUIRE(interfaceVersion != nullptr);

    // This build is the MX Bikes target; the id is what the game matches on.
    REQUIRE(modId() != nullptr);
    CHECK(std::string(modId()) == "mxbikes");

    CHECK(modDataVersion() == 8);
    CHECK(interfaceVersion() == 9);

    // Constant across calls — the game may ask more than once, and the string
    // must stay valid (it's a static buffer, not a temporary).
    CHECK(std::string(modId()) == "mxbikes");
    CHECK(interfaceVersion() == 9);

    FreeLibrary(h);
}
