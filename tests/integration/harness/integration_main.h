// ============================================================================
// tests/integration/harness/integration_main.h
// The DLL path every Wine integration test loads. Each test is its own doctest
// binary (its own plugin lifecycle / port); its main() and the doctest
// implementation live in integration_main.cpp, compiled ONCE by run_tests.sh and
// linked into every test exe.
//
// WHY NOT IN EACH TEST: doctest's implementation is ~4s of -O1 codegen, and it
// used to be emitted by every one of the 126 test TUs (each defined
// DOCTEST_CONFIG_IMPLEMENT) - over half of the suite's compile time, for code
// that never changes. A test that still defines it now fails to LINK with a
// duplicate-symbol error, so the old shape cannot creep back silently.
//
// Usage in a test:
//   #include "doctest.h"
//   #include "integration_main.h"
//   ... TEST_CASEs, calling dllPath() for the DLL ...
// ============================================================================
#pragma once
#include "doctest.h"

// The DLL under test; default matches the build output so a bare
// `wine race_test.exe` still works when run from build/.
inline const char*& dllPath() { static const char* p = "mxbmrp3_test.dlo"; return p; }
