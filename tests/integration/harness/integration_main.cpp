// ============================================================================
// tests/integration/harness/integration_main.cpp
// The doctest implementation + main() shared by every Wine integration test.
// run_tests.sh compiles this ONCE and links it into each test exe (see
// integration_main.h for why it is not compiled per test). main() accepts the
// plugin DLL path as a positional argument while leaving doctest's own flags
// (-tc, --success, ...) intact.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"

#include <cstdio>

int main(int argc, char** argv) {
    doctest::Context ctx;
    // First non-flag argv is the DLL path; neutralize it so doctest ignores it.
    for (int i = 1; i < argc; ++i)
        if (argv[i][0] != '-') { dllPath() = argv[i]; argv[i] = (char*)"--dummy=1"; }
    ctx.applyCommandLine(argc, argv);
    int rc = ctx.run();

    // Sentinel status file for the runner. Neither Wine channel is reliable on
    // its own: exit codes have been observed to arrive as 0 for a FAILING run,
    // and stdout capture has been observed to MISS output a passing run printed
    // (fresh-prefix console redirection race) — so the runner cross-checks this
    // file, written via ordinary file I/O, which suffers neither failure mode.
    // Written to the cwd (the build dir; the runner cds there) and only after
    // ctx.run() returned, so a crashed/killed run leaves the runner's
    // pre-deleted state ("no file") = failure.
    if (FILE* f = std::fopen("wine_test_status.txt", "w")) {
        std::fprintf(f, "%s\n", rc == 0 ? "PASS" : "FAIL");
        std::fclose(f);
    }
    return rc;
}
