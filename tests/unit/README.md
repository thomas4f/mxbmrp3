# Tests

Lightweight, **Linux/macOS-runnable** unit tests for the plugin's
platform-independent pure logic. No game engine, no Windows, no packages to
install — just a C++17 compiler.

## Why this exists (and why it's small)

The plugin is a Windows-only Visual Studio project that links against each
game's proprietary DLL API; it **cannot be built on Linux/CI** (see the root
`CLAUDE.md`). The manual workflow is "build in VS, test in-game." That leaves
the game-agnostic helper logic — color math, string formatting, parsing —
with no automated guard, even though it's the code most amenable to testing
and most prone to silent regressions (a wrong bit-shift or luma threshold
corrupts every rendered color and never throws).

These tests cover exactly that layer. They compile the **real production
header** `core/plugin_utils.h` directly and assert its behavior.

## Run

```bash
./tests/unit/run_tests.sh
```

Exit code is non-zero if the build or any assertion fails. CI runs the same
script on every push/PR via `.github/workflows/ci.yml`.

## What's covered

`test_plugin_utils.cpp` covers the inline pure functions in `plugin_utils.h`:

- **Color packing** — `makeColor` / `applyOpacity` (0xAABBGGRR layout, alpha
  replacement).
- **`isColorDark`** — the BT.601 luma decision, with its 128 boundary pinned.
  This function is **mirrored byte-for-byte in the web overlay's `overlay-util.js`**
  (`CLAUDE.md` flags the mirror as a maintenance trap); the boundary test is
  what a drift on either side would break.
- **`lightenColor` / `darkenColor`** — exact endpoints, alpha preserved.
- **`formatScore`** — thousands grouping, zero, negatives, buffer-size guard.
- **`formatColorHex` / `parseColorHex`** — round-trip and the documented
  "never throws, returns fallback on garbage" contract (including the
  `strtoul` leading-zero quirk).
- **`getRelativePositionColor`** — the ahead / behind / lapped branch matrix.

## What's *not* covered (and how to extend)

The formatting functions in `plugin_utils.**cpp**` (`formatLapTime`,
`formatSessionClock`, `formatDistance`, `fitText`, `sanitizeUntrusted`, the
gap formatters, the enum→string mappers) are **not** tested here. Compiling
that `.cpp` on Linux is currently impractical because it reaches into:

- `PluginData::getInstance().isShortTimeFormat()` / `.getSessionData().isOnline()`
  (the singleton), and
- the full `PluginConstants::DisplayStrings::*` tables,

which transitively pull in the game API headers. Stubbing all of that would be
a large, fragile shim that would rot — contrary to this project's ethos.

The clean way to unlock these (a genuinely worthwhile, small refactor):

1. Split the **pure** numeric formatters into a dependency-free
   `format_utils.h/.cpp` that takes the `compact` flag as a **parameter**
   instead of reading it from the `PluginData` singleton.
2. Have `PluginUtils` forward to them (call sites unchanged).
3. Add `format_utils.cpp` to `SOURCES` in `run_tests.sh` and write
   `test_format_utils.cpp`.

That removes the singleton coupling from the hot formatting path (a small
design win in its own right) and makes the most bug-prone string logic
testable. The security-sensitive `sanitizeUntrusted` (handles
attacker-controlled Steam presence strings) is the highest-value target once
decoupled.

## Framework

These use [doctest](https://github.com/doctest/doctest) (single vendored header,
`tests/integration/harness/doctest.h`) — the same framework as the Wine integration
tests, so there's one assertion vocabulary across the project. `TEST_CASE` /
`SUBCASE` / `CHECK` / `REQUIRE`; run a subset with a filter
(`./tests/unit/run_tests.sh -tc='*hex*'`).

## Adding a test

1. Add a `TEST_CASE("…") { … }` to `test_plugin_utils.cpp`, or create a new
   `tests/unit/test_<area>.cpp` (define the doctest main in exactly one TU with
   `DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN`; others just `#include "doctest.h"`).
2. If you added a file, list it in the `SOURCES` array in `run_tests.sh`.

Only pure, dependency-free logic belongs here (see the extension note above);
anything touching `PluginData` or the game API is a `tests/integration/` integration
test instead — see [`../TESTING.md`](../TESTING.md).
