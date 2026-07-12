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
