// ============================================================================
// tests/integration/harness/integration_main.h
// Shared main() for the Wine integration tests. Each test is its own doctest
// binary (its own plugin lifecycle / port), so it needs a main — this provides
// one that also accepts the plugin DLL path as a positional argument while
// leaving doctest's own flags (-tc, --success, …) intact.
//
// Usage — in exactly one TU per test binary:
//   #define DOCTEST_CONFIG_IMPLEMENT
//   #include "doctest.h"
//   #include "integration_main.h"
//   ... TEST_CASEs, calling dllPath() for the DLL ...
// ============================================================================
#pragma once
#include "doctest.h"

// The DLL under test; default matches the build output so a bare
// `wine race_test.exe` still works when run from build/.
inline const char*& dllPath() { static const char* p = "mxbmrp3_test.dlo"; return p; }

int main(int argc, char** argv) {
    doctest::Context ctx;
    // First non-flag argv is the DLL path; neutralize it so doctest ignores it.
    for (int i = 1; i < argc; ++i)
        if (argv[i][0] != '-') { dllPath() = argv[i]; argv[i] = (char*)"--dummy=1"; }
    ctx.applyCommandLine(argc, argv);
    return ctx.run();
}
