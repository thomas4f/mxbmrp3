# Tests

Lightweight, **Linux/macOS-runnable** unit tests for the plugin's
platform-independent pure logic. No game engine, no Windows, no packages to
install - just a C++17 compiler.

## Why this exists

This is the **cheapest** layer: pure logic compiled straight from the production
headers with a plain `g++`, no cross-build, no Wine, ~1s to run. The whole plugin
*can* now be built and tested on Linux (the mingw cross-build in
`../integration/`, see the root `CLAUDE.md`), but that costs minutes; anything
expressible as a pure function belongs here instead, where the feedback is
immediate and the failure names the function rather than a snapshot diff.

It is also the layer where extracted decision logic lands. A growing set of small
headers beside the HUDs and managers (`hud/standings_gap_plan.h`,
`hud/peak_marker.h`, `handlers/camera_resolve.h`, `core/director_scoring.h`, …)
exists precisely so the interesting branches are reachable here rather than only
through the DLL under Wine. `../../TESTING.md` → *Layer 1* is the authoritative
catalogue of what each TU pins.

## Run

```bash
cmake -S . -B build/tests                            # configure once
ctest --test-dir build/tests -R '^unit'              # build + run all three flavours
```

Or drive the targets directly when you want a doctest filter:

```bash
cmake --build build/tests --target unit_tests
./build/tests/tests/unit/unit_tests -tc='*hex*'
```

Three flavours share one source list (`tests/unit/CMakeLists.txt`): `unit_tests`
(plain), `unit_tests_asan` (ASan + UBSan) and `unit_tests_cov` (gcov, driven by
`./tests/unit/coverage.sh`). CI runs the same targets - see
`.github/workflows/tests.yml`.

## What's covered

The authoritative list is `MXB_UNIT_SOURCES` in `tests/unit/CMakeLists.txt`, with
a per-TU description of what each one pins in
[`../../TESTING.md`](../../TESTING.md) → *Layer 1* (kept complete by
`tools/check_docs.py`, which fails if a `test_*.cpp` exists with no entry). As
the original example, `test_plugin_utils.cpp` covers the inline pure functions in
`plugin_utils.h`:

- **Color packing** - `makeColor` / `applyOpacity` (0xAABBGGRR layout, alpha
  replacement).
- **`isColorDark`** - the BT.601 luma decision, with its 128 boundary pinned.
  This function is **mirrored byte-for-byte in the web overlay's `overlay-util.js`**
  (`CLAUDE.md` flags the mirror as a maintenance trap); the boundary test is
  what a drift on either side would break.
- **`lightenColor` / `darkenColor`** - exact endpoints, alpha preserved.
- **`formatScore`** - thousands grouping, zero, negatives, buffer-size guard.
- **`formatColorHex` / `parseColorHex`** - round-trip and the documented
  "never throws, returns fallback on garbage" contract (including the
  `strtoul` leading-zero quirk).
- **`getRelativePositionColor`** - the ahead / behind / lapped branch matrix.

## What's *not* covered (and how to extend)

The formatting functions in `plugin_utils.**cpp**` (`formatLapTime`,
`formatSessionClock`, `formatDistance`, `fitText`, `sanitizeUntrusted`, the
gap formatters, the enum→string mappers) are **not** tested here. Compiling
that `.cpp` on Linux is currently impractical because it reaches into:

- `PluginData::getInstance().isShortTimeFormat()` / `.getSessionData().isOnline()`
  (the singleton), and
- the full `PluginConstants::DisplayStrings::*` tables,

which transitively pull in the game API headers. Stubbing all of that would be
a large, fragile shim that would rot - contrary to this project's ethos.

The clean way to unlock these (a genuinely worthwhile, small refactor):

1. Split the **pure** numeric formatters into a dependency-free
   `format_utils.h/.cpp` that takes the `compact` flag as a **parameter**
   instead of reading it from the `PluginData` singleton.
2. Have `PluginUtils` forward to them (call sites unchanged).
3. Add `format_utils.cpp` to `MXB_UNIT_SOURCES` in `CMakeLists.txt` and write
   `test_format_utils.cpp`.

That removes the singleton coupling from the hot formatting path (a small
design win in its own right) and makes the most bug-prone string logic
testable. The security-sensitive `sanitizeUntrusted` (handles
attacker-controlled Steam presence strings) is the highest-value target once
decoupled.

## Framework

These use [doctest](https://github.com/doctest/doctest) (single vendored header,
`tests/integration/harness/doctest.h`) - the same framework as the Wine integration
tests, so there's one assertion vocabulary across the project. `TEST_CASE` /
`SUBCASE` / `CHECK` / `REQUIRE`; run a subset with a filter
(`./build/tests/tests/unit/unit_tests -tc='*hex*'`).

## Adding a test

1. Add a `TEST_CASE("…") { … }` to `test_plugin_utils.cpp`, or create a new
   `tests/unit/test_<area>.cpp` (define the doctest main in exactly one TU with
   `DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN`; others just `#include "doctest.h"`).
2. If you added a file, list it in `MXB_UNIT_SOURCES` in `tests/unit/CMakeLists.txt`.

Only pure, dependency-free logic belongs here (see the extension note above);
anything touching `PluginData` or the game API is a `tests/integration/` integration
test instead - see [`../../TESTING.md`](../../TESTING.md).
