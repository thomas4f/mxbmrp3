# Tests

Unit tests for the plugin's platform-independent pure logic, compiled straight
from the production headers with a plain C++17 compiler - no game, no Windows,
no Wine. The layer overview, the per-TU catalogue and the rules for adding a test
are in [`../../TESTING.md`](../../TESTING.md) → *Layer 1*; this file covers how
to run the folder and what it cannot yet reach.

Small decision-logic headers beside the HUDs and managers
(`hud/standings_gap_plan.h`, `hud/peak_marker.h`, `handlers/camera_resolve.h`,
`core/director_scoring.h`, …) exist so the interesting branches are reachable
here rather than only through the DLL under Wine.

## Run

```bash
cmake -S . -B build/tests                            # configure once
ctest --test-dir build/tests -R '^unit'              # build + run all three flavours
cmake --build build/tests --target unit_tests        # or drive one target directly,
./build/tests/tests/unit/unit_tests -tc='*hex*'      # with a doctest filter
```

Three flavours share one source list (`MXB_UNIT_SOURCES` in
`tests/unit/CMakeLists.txt`): `unit_tests` (plain), `unit_tests_asan` (ASan +
UBSan) and `unit_tests_cov` (gcov, driven by `./tests/unit/coverage.sh`). The
framework is [doctest](https://github.com/doctest/doctest), vendored at
`tests/integration/harness/doctest.h` and shared with the integration tests.

## What's *not* covered (and how to extend)

The formatting functions in `plugin_utils.**cpp**` (`formatLapTime`,
`formatSessionClock`, `formatDistance`, `fitText` (its cut, `fitTextInPlace`, is
header-only and tested), `sanitizeUntrusted`, the gap formatters, the enum→string
mappers) are **not** tested here. Compiling that `.cpp` on Linux is currently
impractical because it reaches into:

- `PluginData::getInstance().isShortTimeFormat()` / `.getSessionData().isOnline()`
  (the singleton), and
- the full `PluginConstants::DisplayStrings::*` tables,

which transitively pull in the game API headers. Stubbing all of that would be
a large, fragile shim that would rot.

The clean way to unlock these (a small, worthwhile refactor):

1. Split the **pure** numeric formatters into a dependency-free
   `format_utils.h/.cpp` that takes the `compact` flag as a **parameter**
   instead of reading it from the `PluginData` singleton.
2. Have `PluginUtils` forward to them (call sites unchanged).
3. Add `format_utils.cpp` to `MXB_UNIT_SOURCES` in `CMakeLists.txt` and write
   `test_format_utils.cpp`.

That also removes the singleton coupling from the hot formatting path. The
security-sensitive `sanitizeUntrusted` (attacker-controlled Steam presence
strings) is the highest-value target once decoupled.
