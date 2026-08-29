# AI Development Context for MXBMRP3

## Read This First

This is a **racing simulator HUD plugin** for PiBoSo racing games (MX Bikes, GP Bikes, WRS, KRP). It's a DLL plugin written in C++ using each game's proprietary API, with a shared core that works across all supported games.

**For deep technical details:** See [`ARCHITECTURE.md`](ARCHITECTURE.md) (detailed documentation with mermaid diagrams, component descriptions, dependency graphs, multi-game architecture). This file is a quick-start guide.

**How this file earns its length.** It is read in full before every task, so its cost is paid on every task - and a rule that has quietly gone stale is worse than no rule, because it is followed anyway. So the rule here is: state what the code *cannot* state about itself, and state it once.

- **Prefer a check to a paragraph.** If a rule can be enforced by a lint, a `static_assert`, a type, or a test, enforce it there and keep only the rule + the name of its enforcement here. `tools/check_docs.py` (CI) verifies that every path this file names exists, that every claimed enforcement exists, and that this file stays inside its size budget.
- **Before writing docs, run `python3 tools/docdup.py`.** It prints near-duplicate sentences across the tracked `.md`, catching a fact already stated elsewhere in different words - which an exact-match pass cannot, and did not. Its output is a judgement (which file owns the fact), not a fix list, so it is not a gate.
- **Mechanism detail belongs next to the mechanism.** How the map's two ribbon caches work is a comment in `map_hud.h`; why a panel slides the way it does is a comment in `overlay-slots.js`. Don't mirror it here - a copy drifts silently, and the reader is already in the file.
- **Bug lore belongs in the regression test.** "This crashed in v1.27.0.280 because…" goes in the header of the test that pins it, where it is re-read exactly when it matters. Here, keep the constraint, not the story.
- **Don't index the tree.** File and symbol locations are one search away and go stale on every rename; this file points at the handful of places that are genuinely non-obvious.

## Reach for standard tooling before writing your own

**Default to the off-the-shelf tool.** Before adding a script that lints, runs,
reports or gates something, check whether a standard tool already does it, and use
it even when the homemade version looks shorter today. This is the single largest
source of wasted effort this project has had: a hand-written test runner (deleted
in the CTest/CMake/gcovr migration) grew a stage table, tool checks, skip semantics and an
exit-code policy, then needed a lint to verify the skip semantics and a self-test
to verify the lint - five commits reimplementing what CTest already ships, down to
the `exit 3` = skip convention. `CMakeLists.txt`'s header has the full post-mortem.

The pattern to recognise: **you are writing tests for your tooling.** One test for
a gate is normal; a lint checking your lint means a maintained implementation
exists elsewhere.

**Bespoke is right when nothing off the shelf does the job**, and the bar is "I
looked and there isn't one". Real examples here: the callback recorder/replay path
(proprietary API), `hud_sw_renderer`, the `.fnt` generator, the minidump analyser,
and `check_docs.py`'s invariant-label and budget checks. When
you add one, say in its header what you evaluated and why it fell short - that
sentence is what lets the next person delete it when a standard tool catches up.

## Quick Architecture

```
Game Engine (MX Bikes / GP Bikes / WRS / KRP)
    ↓ (callbacks via plugin API)
mxb_api.cpp / gpb_api.cpp (per-game DLL exports)
    ↓ (converts to unified types via adapters)
PluginManager (receives unified types only)
    ↓
PluginData (singleton - caches all game state)
    ↓ (notifies on data changes)
HudManager (singleton - owns all HUD instances)
    ↓
Individual HUDs (IdealLap, Standings, Map, etc.)
    ↓ (build render primitives)
Game Engine (renders quads/strings)

PluginData ──(notifies on data changes)──→ HttpServer
    ↓ (builds JSON snapshot on game thread)
SSE stream → Web Overlay (browser/OBS)

HudManager ──(2nd frame via collectSurface, if enabled)──→ CompanionWindow
    ↓ (submit quads/strings; own window thread)
hud_sw_renderer → standalone OS window (2nd monitor)
```

**Key Singletons**, *not* the full set: `grep -l getInstance mxbmrp3/core/*.h` is,
and it cannot go stale. Listed are the ones whose ROLE the name does not give away.
**Enforced**: `check_docs.py` fails on a name here that no longer exists, so this
may go incomplete but never wrong.
- `PluginData` - Central game state cache, change detection
- `HudManager` - HUD lifecycle, owns all HUD instances
- `RumbleProfileManager` - Per-bike rumble profiles stored in JSON
- `StatsManager` - Unified stats, personal bests, odometers in a single JSON file
- `FmxManager` - FMX trick detection state machine, scoring, chain system
- `AssetManager` - Discovers fonts, textures and icons from subdirectories
- `FontConfig` - User-configurable font categories (`FontCategory`: Title, Normal, Strong, Digits, Marker, Small)
- `ColorConfig` - User-configurable color palette
- `HttpServer` - Embedded HTTP server, SSE-streaming to web overlays (OBS)
- `CompanionWindow` - Standalone OS window rendering the HUD on a second monitor via an in-process software renderer (`hud_sw_renderer`); each HUD can decouple its on/off + position there.
- `SpotterManager` - Spoken race callouts from the loaded cue pack (SAPI, or a pack's own `.wav`s), on its own thread. See ARCHITECTURE.md §16.
- `DirectorManager` - Auto-director for spectate/replay: cuts the camera to the most interesting subject (drives `DirectorWidget` + the overlay's battle panel). See ARCHITECTURE.md §14.
- `EventRecorder` - Callback-tape recorder (MX Bikes only, dev tool): taps the raw callbacks in `mxb_api.cpp` and writes a `.tape` for headless replay. Ships dormant; see *Non-obvious placements*.

## Multi-Game Support

The plugin supports multiple PiBoSo games from a single codebase. The game is the
**target**, not the configuration (`Debug`/`Release` are plain):

| Game | Target | Output | Status |
|------|--------|--------|--------|
| MX Bikes | `mxbmrp3` | `mxbmrp3.dlo` | ✅ Full support |
| GP Bikes | `mxbmrp3_gpb` | `mxbmrp3_gpb.dlo` | ✅ Core features |
| Kart Racing Pro | `mxbmrp3_krp` | `mxbmrp3_krp.dlo` | ✅ Core features (no FMX) |
| WRS | - | `wrsmrp3.dlo` | ⏳ Stubbed |

**Translation Layer:**
- `game/unified_types.h` - Game-agnostic data structures (`Unified::` namespace)
- `game/game_config.h` - Compile-time game selection, feature macros
- `game/adapters/*_adapter.h` - Convert game structs → unified types
- `vendor/piboso/*_api.cpp` - Per-game DLL exports

## Build & Test

Full build/test details, prerequisites, and both build tracks live in
**[`DEVELOPMENT.md`](DEVELOPMENT.md)**. The essentials for working here:

**⚠️ Build Environment:**
- The **shipping** `.dlo` (what users install) is built **only** with MSVC
  (Visual Studio 2022, x64), configured by CMake. You cannot produce the shippable
  artifact on Linux.
- **But you CAN build and test on Linux** - use it to verify changes before
  committing rather than deferring to "a Windows user will build it".
  `./tools/install_deps.sh [--list]` provisions the toolchain; **everything runs
  under CTest**, a standard runner:
  ```
  cmake -S . -B build/tests          # once; builds nothing, just registers gates
  ctest --test-dir build/tests --output-on-failure
  ctest --test-dir build/tests -L fast    # only what needs no mingw/wine
  ```
  A gate whose tools are missing exits 3 and reports **SKIPPED**, which is not a
  pass - install the tool and get a real answer. `-R <name>` runs one gate, `-j N`
  parallelises. The gate list, what each lint enforces, the per-layer scripts and
  the release/packaging flow are all in **[`DEVELOPMENT.md`](DEVELOPMENT.md)**;
  [`TESTING.md`](TESTING.md) is the layered guide + how to add a test.
- **The CodeQL gate is opt-in and everything above excludes it.** It costs ~15 min
  (clean rebuild under the extractor), so it skips unless `MXBMRP3_CODEQL=1` - a
  label wouldn't do it, since `-L` selects rather than excludes.
  `MXBMRP3_CODEQL=1 ctest --test-dir build/tests -R codeql`; TESTING.md → *CodeQL*
  has when to reach for it and the two false-green guards it carries.
- **NEVER append anything to a test/build command** - not `| tee`, not a trailing
  `echo "EXIT=$?"`. You get the LAST command's status, so both report **success
  while gates failed**, and that bogus 0 is what reaches the completion
  notification. Run it bare; `>log 2>&1` buys quiet at the cost of hiding all
  progress. To capture: `set -o pipefail; … | tee log`, NOTHING after it.
  **Trust the log's summary over any status that reached you second-hand.**
- **Read a long log with `grep -E "FAIL|Status: FAILURE"`, never `tail`.** `tail`
  answers "how did it end", not "did it pass": the integration gate ran red for
  several sessions reported as "all 73 pass, fault elsewhere" because its one
  `FAIL:` line sat 700 lines above the end.
- **Foreground for anything that fits the tool timeout** (unit suite, lints, warm
  cross-build). For anything longer - the full `ctest` run, the integration suite,
  `check_game_configs.sh` - run it **once** with `run_in_background: true`, then
  **stop and wait for the completion notification**. It is guaranteed; polling is
  never correct. Two ways polling silently deadlocks: `pgrep -f 'x'` matches the
  waiting shell's OWN command line (so `until ! pgrep -f x` never exits once two
  are running), and `grep` is case-sensitive. Never edit a script while it is
  executing - bash reads incrementally, so a mid-run edit shifts byte offsets and
  produces bogus syntax errors from a valid file.
- The cross-build is a **test** configuration (Discord/analytics compiled out,
  SEH is MSVC-only); every divergence is gated by `MXBMRP3_TEST_BUILD`/`_MSC_VER`,
  so the MSVC build is byte-for-byte unchanged. It is **not** a shippable build.
- **Gotcha**: a **Release (`NDEBUG`) MSVC build hard-fails with `#error`** unless
  the two secret env vars `APTABASE_KEY` / `GOATCOUNTER_TOKEN` are set (or
  `MXBMRP3_ALLOW_NO_ANALYTICS` is defined). The GoatCounter *code* is public and
  hardcoded in `plugin_constants.h`, so it's not required. Debug and the Linux
  cross-build are exempt. See DEVELOPMENT.md.

**⚠️ IMPORTANT - Shell Commands:**
- The user runs on **Windows**, not Linux
- When providing shell commands for the user to run, use Windows syntax:
  - Use `&` instead of `&&` for chaining commands (or provide separate commands)
  - Use backslashes `\` for paths, or forward slashes `/` (git accepts both)
  - Example: `git fetch origin & git reset --hard origin/branch-name`

## Testing Discipline

Tests are not optional scaffolding - this project has a real, CI-gated suite that
runs on Linux with no game (see [`DEVELOPMENT.md`](DEVELOPMENT.md)). **When you
change behavior, change a test:**

- **New feature or behavior change** → add or extend a test that exercises it.
- **Bug fix** → add a regression test that would have caught it (ideally one that
  fails before your fix, passes after). If it genuinely can't be reproduced in a
  headless test, say so and why.
- **New invariant or bug-guard discovered** → don't *document it away*: prose is
  the last resort, not the fix. Escalation order: make the mistake impossible
  **by construction** (a registry or type, like `PerRider<>` or the serializer
  registry); else a **compile-time contract or CI lint** (`static_assert`, the
  `check_*.sh` scripts); else **pin it with a behavioral test** and write the doc
  clause as *why + test name*; only a rule none of those can hold may stay as
  prose, explicitly labeled convention. The Maintenance Invariants section shows
  the target shape - every bullet either names its enforcement or says it's
  convention.
- **You notice a missing/weak test while working** → add it, or call it out
  explicitly. Don't leave a known gap silent.

Where the test goes (pick the fastest one that can exercise the change):

| What you changed | Where the test goes |
|---|---|
| A pure helper (formatting, color, parsing, header-only math) | `tests/unit/` unit test (doctest) - compiles the real header, ~1s |
| Standings / gaps / penalties / session logic / anything in the JSON snapshot | add/extend a doctest in `tests/integration/tests/` using `PluginHost` + `checkStandings` (drives real callbacks under Wine, asserts `/api/state`) - see [`TESTING.md`](TESTING.md) |
| A settings / persistence change | `tests/integration/run_persist_test.sh` (load→save round-trip) |
| A new DLL-boundary callback or array-size/count handling | `tests/integration/callback_fuzzer.cpp` |
| Config parsing / a new INI or JSON field | `tests/integration/run_fuzz.sh` corpus |
| A hot-path change (Draw / telemetry / rebuild) | confirm `tests/integration/run_perf.sh` didn't regress |
| Installer / packaging (`packaging/mxbmrp3.nsi`) | `tests/integration/run_installer_test.sh` (makensis + Wine: asserts install/uninstall/registry/data-wipe outcomes) |
| Web overlay rendering (`mxbmrp3_data/web/` - js/overlay-*.js/style.css/index.html) | add/extend a Playwright test in `tests/web/tests/` driving `?demo` (asserts the rendered DOM) - see [`TESTING.md`](TESTING.md). `./tests/web/lint.sh` (the `eslint` gate) runs in a second and catches the dead-code class |
| Anything that renames/moves a file the docs name, or adds a test | `python3 tools/check_docs.py` (paths resolve, invariants labelled, catalogue complete, CLAUDE.md within budget) |

These run headless - most via mingw + Wine, the web-overlay tests via Node +
Playwright. Manual in-game testing on Windows stays the final check for
rendering/input, but it does **not** excuse skipping an automated test when the
logic is testable headless.

## Important Patterns & Constraints

### Performance Target: 480fps
The plugin must run efficiently at **480fps** (2.08ms frame budget). Many competitive players use high refresh rate monitors. Avoid per-frame allocations, unnecessary string operations, and complex calculations in hot paths like `Draw()` and `RunTelemetry()`. `run_perf.sh` gates the average and both p99s against that budget; BenchmarkWidget's in-game warning colours are tied to it too, so changing the target changes what players are shown.

### DO:
- Use RAII (smart pointers, no raw `new`/`delete`)
- Use safe string functions (`strncpy_s`, `snprintf`)
- Add exception handling for file I/O
- Use `DEBUG_INFO_F()` for logging (not `printf`)
- Check for existing patterns before adding new code
- Wrap new DLL exports in `API_GUARD_CATCH("ExportName")` (see `vendor/piboso/api_guard.h`); uncaught exceptions across the boundary crash the host game. **Enforced**: `check_api_guards.sh` (CI; `// api-guard-exempt: <reason>` for trivially-nonthrowing constant getters)
- Wrap new `std::thread` function bodies in a top-level try/catch; uncaught throws in threads call `std::terminate()`
- Exception-guard **every** INI parse site - hand-editing the INI (`auto_save` off + RELOAD_CONFIG) is a supported workflow, and one naked `std::stoul` aborts the whole settings load (the `parseColorHex` base-section bug)
- `isfinite`-guard persisted floats at **both** write and load - `+Inf` slips past `>`/`>=` checks that only reject NaN, and one bad sample permanently corrupts the saved value (e.g. the odometer); see `finiteOrZero()` in `stats_manager_persistence.cpp` - pinned by the +Inf/NaN cases in `stats_test.cpp` / `odometer_test.cpp`
- Validate `_iElemSize` against the compiled struct size and `std::clamp` counts to `0..MAX_RACE_ENTRIES` in new array-style API callbacks - PiBoSo reshapes plugin structs between game versions (skew symptom: empty standings/map + one "element size N != expected M" log line)
- Use `addLabel()` for table headers and axis labels (STRONG font, Small size, row-centered); don't hand-roll `addString` at data-font size
- Gate HUD rebuilds on `isDataDirty()`/`isLayoutDirty()` (never unconditionally per frame unless the rebuild is trivially cheap); keep new global-input polling proportional to what's actually bound (see `HotkeyManager`)

### DON'T:
- Throw exceptions in core code (game engine doesn't support them)
- Use raw pointers for ownership
- Add features without understanding the data flow
- Bypass PluginData's change detection (use appropriate setter methods)
- Reintroduce per-tick XInput polling of empty/disconnected slots - `XInputGetState` on a dead slot is ms-class on degraded stacks (connection scan is throttled to 1s; the selected slot backs off to 500ms while unplugged)
- "Simplify" the rumble send policy in `setVibration()` back to value-dedup: controllers **decay** rumble without a continuous feed, so a value sent once stops buzzing. The policy and why its rate cap is user-tunable are documented at `setVibration()`; pinned by `xinput_thread_test.cpp` and `rumble_effect_test.cpp`.

## Maintenance Invariants (touch X → also update Y)

Regression traps where changing one thing silently rots another. Each is the *rule*; the mechanism's own detail lives next to the mechanism, and the bug it prevents lives in the test that pins it. **Enforced** = a check fails if you get it wrong, so read the failure rather than memorizing the rule. **Pinned** = a test covers it. **Convention** = nothing catches it but review.

- **Per-rider container in `PluginData`** (keyed by raceNum): declare it `PerRider<container>` - the declaration *is* the eviction registration (erase in `removeRaceEntry()`, reset in `clear()`). A plain member is correct only for a derived cache rebuilt wholesale from a dirty flag. Miss it and a *reused* race number inherits the departed rider's state. **Enforced** by construction (`plugin_data.h`); **pinned** by `racenum_reuse_test.cpp`.
- **New input to MapHud's `renderTrack()` output**: add it to `TrackRibbonKey` (screen quads) or `WorldRibbonKey` (world points) - the wrong one, or neither, serves stale geometry. Which key owns what, and why the world builder merges short segments and dedupes joint samples, is documented at the structs in `map_hud.h` and at `ensureWorldRibbon()` in `map_hud_track.cpp`. **Pinned** by `map_render_test.cpp`.
- **New `onDataChanged` consumer**: `DataChangeType::Standings` fires many times/sec on full grids. A consumer must be trivially cheap **or** gate on consumer activity *before* any string/alloc work (models: `HttpServer::hasActiveClients()`, `SteamFriendsManager`'s POD fingerprint). **Enforced** by `check_change_consumers.sh` (CI): every `onDataChanged` definition states its `// change-gate:` out loud; the *model* is pinned (`http_gating_test.cpp`), the stated reason stays review's.
- **A changed `/api/state` shape**: regenerate `tests/fixtures/overlay_snapshot.json`. It is a real captured snapshot, read from BOTH sides - a rename passes every other test, because the C++ tests read the fields they expect and the Playwright suite drives `?demo`, whose snapshot the overlay writes itself. **Pinned** by `overlay_snapshot_test.cpp` (paths+types vs the fixture) and `overlay_snapshot.spec.js` (that fixture through the real `render()`).
- **HttpServer change-type gating**: frequent types (Standings/EventLog) gate on client activity; rare transition types (SessionData/RaceEntries/SpectateTarget) **always** rebuild. The plugin receives **no callbacks while the player sits in menus**, so a snapshot skipped at a transition could never be rebuilt. Don't move rare types behind the gate, nor behind the build-coalescing window they bypass. **Pinned** by `http_gating_test.cpp` (both directions, via the rebuild-count hook - a gated change leaves the previous snapshot in place, so `/api/state` alone can't see the difference).
- **Cross-thread flags stay atomic**: background workers legitimately call `setDataDirty()` / `showUpdateNotification()`, so those flags - and any new one written off the game thread - are `std::atomic<bool>`. A plain `bool` is invisible to `-Wthread-safety` (that only checks *annotated* members), so in a class owning a `std::thread` it must instead be `MXB_GUARDED_BY` or carry `// mt-plain: <which thread owns it>`. **Enforced** by `check_mt_flags.sh` (CI).
- **Singleton destructors are self-contained**: a Meyers singleton's destructor runs during static teardown, when anything constructed after it is already destructed - so reaching another `getInstance()` from one is use-after-destruction. Each destructor handles only its own threads and files; cross-singleton orchestration belongs solely to the game-driven `Shutdown()` export. (Logger is the near-exception: it outlives every singleton but `PluginManager`, so destructors may log - `~PluginManager` may not.) Cost two shipped crashes; both stories are in `teardown_test.cpp`. **Pinned** by its unload-without-`Shutdown()` case.
- **A worker thread must not outlive the DLL**: a destructor that `join()`s at DLL detach deadlocks - `FreeLibrary` holds the loader lock the worker needs to exit, so the game HANGS rather than crashes. The game does unload without calling `Shutdown()` (the path the two crashes above came from), so a new background thread must be joined by the orchestrated `Shutdown()`, never left to its singleton's destructor. **Enforced** by `check_thread_join.sh` (`joined-by:` annotations, CI); repro in `teardown_test.cpp`.
- **A mutex-guarded member is guarded at EVERY access site**, including private helpers called from already-locked-*looking* code. New mutexes use the annotated `Mutex`/`MutexLock`/`CvLock` wrappers (a raw `std::mutex` is invisible to the analysis); deliberate exceptions carry `MXB_NO_TSA` with a reason. Preferred shape: copy under the lock, pass the snapshot into helpers. **Enforced** by `check_thread_safety.sh` (clang `-Wthread-safety`, CI).
- **Background workers never read live game-thread members**: snapshot at task start, and never call cross-HUD methods that race shutdown (`HudManager::clear()` joins the records fetch thread - `RecordsFetcher`, via `RecordsHud::joinFetchThread` - *before* nulling HUD pointers; a worker serving any other HUD needs the same). **Convention.**
- **Callback-tape format change** (a `SPlugins*` struct, or the record layout): `core/event_recorder.{h,cpp}` and `tests/integration/harness/tape.h` `static_assert` the same literal sizes/offsets, so a one-sided edit fails to compile. A deliberate change updates both, bumps `FileHeader::version`, and re-records the golden tapes. **Enforced** at compile time; see TESTING.md.
- **New `MXBMRP3_Test_*` hook**: add it to `core/test_hooks.cpp` (gated on `MXBMRP3_TEST_BUILD`, which `mxbmrp3/CMakeLists.txt` removes from the source list for every shipping target, so it cannot reach a shipping DLL) and expose it via `PluginHost`. Prefer a hook over routing plugin-logic tests through the live HTTP server - read `PluginHost::snapshot()` instead. **Enforced** by `check_test_hook_placement.sh` (CI; `friend` declarations and `// test-hook-exempt:` are the escape hatches).
- **Vendored lib update** (`mxbmrp3/vendor/*`, `harness/doctest.h`): bump its `version` in `vendored.json`, the single source for the release SBOM and the weekly freshness check. A newly vendored lib needs an extractor registered in the checker. **Enforced** by `tools/check_vendored_manifest.py` (CI).
- **Per-surface HUD setting on the companion instance**: wire it through four places or it half-works - (1) a `getCompanionX()` that falls back to the game value while unconfigured plus a `setCompanionX()` that configures first; (2) capture that persists **only when the companion has diverged from the game**, and apply that clears authoritatively when absent; (3) `collectSurface()` reading the companion value on the companion pass only; (4) input routed by `getActiveSurface()`, with interactive HUDs mapping the cursor into build space before hit-testing. Why the persist-only-diverged gate is load-bearing (it is what lets an untouched HUD pick up a changed default position on upgrade) is documented at `captureBaseHudSettings`. **Pinned** by `companion_decouple_test.cpp`; the (4) cursor map is the one `ui_viewport.h` function paint also uses, **pinned** by `viewport_test.cpp`.
- **A HUD that skips work when hidden gates on `isVisibleAnySurface()`**, never `isVisible()`/`m_bVisible` - else a HUD enabled *only* on the companion renders stale. Legitimate game-surface reads carry a `// vis-gate: <reason>` annotation. **Enforced** by `check_visibility_gates.sh` (CI).
- **Theme sprite order**: `discoverThemes()` assigns indices in `spriteFiles` order and `setupDefaultResources()` pushes in it; diverge and a theme draws another's sprites. **Enforced** at startup by `HudManager::verifySpriteRegistrationOrder()` (re-derives every recorded index for every asset type, logs each skew); **pinned** by `sprite_order_test.cpp`, must-catch half included.
- **A user-selectable asset is stored by NAME, not by discovery index** (themes, icons, packs): an index reassigns everyone's choice when a pack is added or renamed. An unknown name degrades to the shipped default *without* rewriting the stored value, so restoring the folder restores the choice. **Pinned** by `asset_pack_test.cpp`.
- **An asset pack's sprites come from one table**: each type's `kStems` is walked by both its `discoverX()` and `setupDefaultResources()`, and a pack whose RESOLVED set (own file, else its `base` pack's) misses any stem is skipped, not half-registered. **Enforced** at compile time by `asset_manager.h`'s `static_assert`s; the base resolution is **pinned** by `pack_skin_test.cpp`.
- **Numbers that describe one piece of pack art live in that pack**, never compiled in: a board's row offsets, a pad's button positions, a dial's range and sweep. Compiled, they silently mis-draw every other picture - the tacho's `MAX_RPM` placed the needle while the figures were painted in the `.tga`. A HUD that gains pack art also gains the trap in `BaseHud::setTextureVariant` (a stale `textureVariant` in an upgraded INI turns the art off) and orphans anything users kept in `textures/`, so it needs a migration. **Pinned** by `pack_texture_variant_test.cpp` and `gauges_migration_test.cpp`.
- **Benchmark report format**: the `BENCH key=value` line and the HUD-footprint table columns emitted by `benchmark_widget.cpp` `exportReport()` are parsed by `tools/benchmark_report.py`; a one-sided rename makes the analyzer silently report nothing. **Enforced** by `run_perf.sh`, which feeds a headless `bench_driver` report through the analyzer and requires a clean parse.
- **Content anchors to its own box, never the panel**: rows via `contentX()`/`contentRight()`, centred values via `sectionBoxCenterX()` (its comment has the why). **Pinned** by `card_anchor_sweep_test.cpp`; its coverage is **enforced** by `check_card_anchor_coverage.sh`.

## Design Decisions (Don't "Fix" These)

**Singletons Everywhere**
Required by plugin API - we get one global entry point, everything branches from there.

**Settings panel helpers are members, not lambdas**
This entry said the opposite until the "8+ parameters" behind it was measured: 2. Measure before inheriting a claim.

**HUD config is open to SettingsHud - `friend class SettingsHud` or public members**
Configuration data, not encapsulated state. Both shapes are in the tree (some HUDs use both); `friend` is the majority - prefer it for new HUDs. Counts are deliberately not quoted here: they moved every time a HUD landed.

**HUDs pull from PluginData - except the track-position push**
HUDs cache formatted render data (`m_quads`, `m_strings`), not raw game state, so PluginData stays authoritative. Exception: `HudManager::updateRiderPositions` pushes raw `Unified::TrackPositionData` into Map/Radar (world coords PluginData drops) + GapBar (convenience). **Enforced**: `check_hud_raw_cache.sh` - a new `Unified::` member in a HUD header needs `// raw-cache:`.

**Settings reset reuses save/load serialization (don't add a third list)**
"Reset to defaults" replays a startup snapshot through the *same* applier `loadSettings()` uses, never a hand-maintained list of per-setting resets. Two snapshots back this, and they are intentionally separate:
- `m_globalDefaultsIni` - global sections (`writeGlobalSettings`/`applyGlobalLine`).
- `m_hudFactoryDefaults` - pristine per-HUD constructor defaults, captured *before* `loadSettings()` folds user base-section keys into `m_hudDefaults`.

`m_hudDefaults` (sparse-save baseline, with base-section edits folded in) is **not** a clean factory snapshot - don't "simplify" reset by pointing it at `m_hudDefaults` or by merging the two caches; that reintroduces stale-default-on-reset bugs (e.g. an upgraded default not taking effect). A new setting gets reset coverage for free once wired into save/load. **Pinned** by `reset_test.cpp`; see ARCHITECTURE.md.

**Widget vs HUD Distinction**
Widgets (grep `_widget.h` for the set) are simplified HUD components with:
- Single-purpose display (no data tables); some do have row toggles (LeanWidget's `m_enabledRows`)
- Fewer settings, but not only position/scale/opacity
- Simpler rendering logic

Full HUDs (StandingsHud, LapLogHud, PitboardHud, TimingHud, NoticesHud, StatsHud, etc.) have:
- Complex data visualization
- Extensive customization (column/row toggles, gap modes, etc.)
- More configuration options

**Helmet Overlay (HelmetOverlayHud)**
Full-screen immersion overlay - neither a widget nor a typical HUD:
- Renders textured quads (helmet upper/lower) over the entire viewport
- Telemetry-driven tilt (lean angle) and vibration (suspension deltas)
- Registered first in HudManager so it draws behind all other HUDs
- Global settings (not per-profile) - saved in its own `[HelmetOverlay]` INI section like `[Rumble]`
- Hidden during spectate/replay/crash; no title, no dragging, no scaling

**Companion Window (CompanionWindow + hud_sw_renderer)**
Standalone OS window that renders the HUD on a second monitor - *not* a network mirror and unrelated to the web overlay:
- Draws the plugin's live render primitives with an **in-process software renderer** (`hud_sw_renderer` - see its header). It reproduces the game's texture stage, so per-quad **opacity** and white-icon **colorization** match; don't "simplify" the blit to a plain copy. **Pinned** by `test_hud_sw_renderer.cpp`.
- **Own window thread** owns the Win32 loop and renders on its own cadence, so the window stays live **in menus** when the game issues no `Draw` (the same no-callbacks-in-menus constraint the HttpServer notes). The game thread only `submit()`s a POD frame copy under a mutex.
- **Never takes focus**, and renders into the **full client** with a centered 16:9 *scale* viewport - not a letterbox, so HUD elements outside `[0,1]` use the whole window like in-game. Both, plus the geometry/maximized persistence and the cursor handling, are documented where they happen in `companion_window.{h,cpp}`.
- Each HUD can **decouple** its on/off + position here (see the Maintenance Invariant); everything else is shared. Runtime `[Display]` target only (In-game / Companion / Both); analytics `feat_companion`. Render cadence is INI-only (`[Display] companionRefreshHz`, default `0` = V-Sync via `DwmFlush`, `N` = fixed Hz cap). See ARCHITECTURE.md §13.

**Handler-to-API Event Mapping**
Each handler corresponds to game API callback(s), but receives unified types:
- Run handlers (`Handlers::handleRunInit`, `handleRunLap`, …) = player-only events
- Race handlers (`Handlers::handleRaceEvent`, `handleRaceLap`, …) = multiplayer/all riders
- Stateless handlers are free functions in `namespace Handlers`; only `DrawHandler`/`SpectateHandler` carry state and stay singletons
- See ARCHITECTURE.md for full mapping

**Layered automated tests + manual in-game**
There is a real, CI-gated test suite, all runnable on Linux with no game engine.
**[`TESTING.md`](TESTING.md) is the guide** (layers, harness, philosophy, how to
add a test); the short version: pure-logic unit tests (`tests/unit/`, doctest); the
integration layer (`tests/integration/tests/`, doctest) that cross-compiles the whole
plugin, loads it under Wine, drives the **real callbacks** via `PluginHost`, and
asserts the plugin's computed state - read via `snapshot()` (built directly, no
HTTP server) or typed `MXBMRP3_Test_*` hooks for internal state, plus **real-data
golden masters** that replay actual in-game callback captures (the in-plugin
recorder `[Recorder] enabled=1` → tape → `replayTape`); Playwright web-overlay tests (`tests/web/`); and
specialized runners (persistence, fuzz, perf). They run in CI on demand and as the
release gate (no push trigger - see `.github/workflows/tests.yml`), plus
automatically on pull requests in the free public mirror.
Rendering is not a headless blind spot: `companion_demo.sh` screenshots the real
HUD via the software renderer, so visual changes are pixel-diffable (TESTING.md).
In-game testing stays the final check for input and game-specific behavior.

**Logger has an internal mutex**
`Logger::log()` is called from the game thread and ~10 background threads (HttpServer, UpdateChecker, UpdateDownloader, DiscordManager, RecordsFetcher, CompanionWindow, SteamFriends, Analytics, XInput). The mutex serializes concurrent writes so log lines don't interleave. Don't remove it. The SEH crash filter deliberately doesn't call Logger to avoid deadlocking on this mutex.

## Common Tasks

### Adding a New HUD
1. Create class inheriting from `BaseHud` (`.h` and `.cpp` files in `mxbmrp3/hud/`)
2. **Nothing to register.** `mxbmrp3/CMakeLists.txt` globs the four product
   directories with `CONFIGURE_DEPENDS`, so a new file is picked up by every
   toolchain on the next build. (This used to mean hand-editing the vcxproj and
   its `.filters`, with a checker guarding the drift; one definition removed the
   whole class.)
3. Implement `rebuildRenderData()` - builds vectors of quads/strings
4. Register in `HudManager`: add the member pointer + getter, and one `createHud(m_pX, "harness_id")` line in `initialize()` (registration order = draw order). Nulling in `clear()` is automatic - `createHud`/`bindHudSlot` enroll the pointer for it, so a forgotten-null dangling pointer can't happen.
5. Add tab in `SettingsHud` for configuration: a `Tab` enum value (settings_hud.h), a `renderTab<Name>`/optional `handleClickTab<Name>` in a new `settings/settings_tab_*.cpp`, and **one row** in the per-tab descriptor registry `s_tabRegistry` (settings_hud_render.cpp) - the row drives display order, name, tooltip id, the tab-list checkbox, game gating, render/click routing, and the per-tab reset (no switches to edit). Plain numeric steppers ("value = applyAccelerated*; mark dirty") should use `ctx.addSteppedControl` + a `SteppedControl` descriptor instead of new `ClickRegion::Type` enum pairs.
6. Add save/load via the **per-HUD serializer registry** (`settings_hud_registry.{h,cpp}`, whose header explains why one table drives capture + apply + serialize): write a `cap_<Name>`/`app_<Name>` (declared in `settings_hud_registry_decls.inc`, defined in the `.cpp`) and add **one row** to `hudSectionRegistry()`. Reset stays automatic via the factory snapshots. For a *global* single-value setting, use `writeGlobalSettings()`/`applyGlobalLine()` in `settings_manager_global.cpp` instead. Game-gated HUDs wrap decls, definitions and registry row in the same `#if GAME_HAS_*`. **Pinned** by `settings_sections_test.cpp` (every captured section is actually serialized).

### Working with Game API Events
When implementing event handlers or debugging timing/lap data:
- **Check the API headers**: `mxbmrp3/vendor/piboso/mxb_api.h`, `gpb_api.h`, etc.
- **Unified types**: Handlers receive `Unified::*` types, not raw game structs
- **Use cases:**
  - Understanding field indexing (0-based vs 1-based) - e.g., lap numbers, split indices
  - Clarifying field meanings in event structs
  - Determining data types, value ranges, and validation requirements
- **Example:** When displaying lap numbers, the API uses 0-based indexing internally (`m_iLapNum=0` for first lap) but UI typically shows 1-based (display as "L1")
- **Tip:** Many timing/position issues come from misunderstanding the API contract - always verify assumptions against the header

### Working with the Web Overlay
The embedded HTTP server (`core/http_server.cpp`) streams race data to browser overlays over SSE. The client is in `mxbmrp3_data/web/js/overlay-*.js`; each file's header describes its own area, so read there for mechanism.

The rules that span the C++/JS boundary, which no single file can state:

- **The plugin sends raw data; the overlay decides presentation.** Event/chip filtering, timestamps and display options live client-side in `overlay-config.js`'s `CONFIG`. Resist adding a plugin-side setting for something the browser can decide (`CONFIG.battleLiveGaps` is the model: the plugin always ships `liveGapMs`/`liveGapValid`, the overlay chooses whether to show it).
- **Adding a field**: extend `buildJsonSnapshot()` (built on the game thread - PluginData is not thread-safe), then consume it in the client, then **regenerate `tests/fixtures/overlay_snapshot.json`** (`overlay_snapshot_test.cpp` fails with the copy command). The snapshot string is cached under a mutex and read by SSE threads.
- **Mirrored helpers must stay in step**: `isColorDark` (C++ ↔ `overlay-util.js`), `formatSecs` and the sector-resolution helpers (`session_charts_math.h` ↔ `overlay-charts.js`), and the panel names in `overlayPanelName()` ↔ `createSlotPanel`. **Enforced**: `tests/fixtures/cpp_js_parity.json` is asserted by both `test_cpp_js_parity.cpp` and `parity.spec.js`, so the two sides can only pass together.
- **Colour *roles* are chosen per renderer.** The plugin ships palette values; which slot an element uses is decided independently in `ColorSlot::…` and in the overlay CSS. A "which colour does X use" change must be made on both sides.
- **`liveGapValid` (data validity) and `canUseLiveForRider` (in-game row display) answer different questions** - the former is true for the leader, the latter isn't. Don't unify them.
- **Session clock**: `getLeaderLapsToGo()` + `formatSessionClock()` are the single source for time+lap overtime labels, so in-game and web read identically. (TimeWidget deliberately shows plain `MM:SS` and is not a consumer.)
- **Rider names are UTF-8-aware on the overlay only.** The in-game `.fnt` renderer is a byte-indexed 256-glyph CP1252 table, so multi-byte names garble there regardless of truncation logic - don't "fix" in-game truncation for UTF-8.
- **Adding or renaming a served asset**: the load order of the classic scripts is load-bearing (one shared global scope), and every asset must be listed in `index.html`/`style.css`, in `sw.js`'s `PRECACHE_URLS` in the same order, and - for a new *subfolder* - in `packaging/mxbmrp3.nsi`. **Enforced** by `tests/web/tests/assets.spec.js`.
- **Theming** is driven by the `:root` token manifest in `style.css`; reuse tokens rather than hardcoding, and note that colours/fonts arrive as runtime inline styles, so a `custom.css` override of those needs `!important`. Users theme via `custom.css` (synced from Documents, no-cache, deliberately not precached) rather than forking `style.css`.
- **`?demo`** replays a synthetic 22-rider race through the same `render()` path - the way to iterate on the overlay without the game. Screenshot it headlessly with Playwright (`tests/web/README.md`).

### Adding Support for a New Game Feature
1. Add field to appropriate `Unified::` struct in `game/unified_types.h`
2. Add conversion in each adapter (`game/adapters/*_adapter.h`)
3. Add feature flag to `game/game_config.h` if game-specific
4. Update handlers/HUDs to use the new field

### Disabling a Feature Per-Game

When an entire feature (HUD, manager, integration) doesn't apply to one or more games - e.g. FMX freestyle tricks on karts, Discord Rich Presence on non-MXB, the records provider on non-MXB:

1. **Add a `GAME_HAS_X` flag** to `game/game_config.h`. Examples already in the file: `GAME_HAS_DISCORD`, `GAME_HAS_HTTP_SERVER`, `GAME_HAS_FMX`, `GAME_HAS_RECORDS_PROVIDER`. Pattern:
   ```cpp
   #if defined(GAME_MXBIKES) || defined(GAME_GPBIKES)
       #define GAME_HAS_FMX 1
   #else
       #define GAME_HAS_FMX 0
   #endif
   ```
2. **Gate the HUD registration** in `HudManager::initialize()`. Leave the member pointer as `nullptr`; existing null-checks downstream (`if (m_pFmxHud)`) will fall through silently.
3. **Gate the settings tab** in `SettingsHud` - set `gameGated = true` on the tab's row in `s_tabRegistry` (settings_hud_render.cpp). `isTabAvailable()` then skips the tab whenever its `hud` getter returns the nullptr you set up in step 2 - no `#if` block needed (runtime null-check pattern, like `TAB_RECORDS`/`TAB_FMX`/`TAB_FRIENDS`).
4. **Gate the hotkey row** in `settings_tab_hotkeys.cpp`. The hotkey *action* itself can stay in the enum (the handler in `HudManager::processHotkeys` is already null-safe), but the row should be hidden so users don't see a binding that does nothing.
5. **Gate handler entry points** that feed the disabled manager (`run_telemetry_handler.cpp`, `race_session_handler.cpp`, etc.). Skip the singleton calls entirely so the binary doesn't pull them in.
6. **Gate `SettingsManager` save/load** if the disabled HUD has its own profile section. Crucial when `HudManager::getXxxHud()` returns a `Hud&` with `assert(m_pXxxHud)` - calling it with a null member crashes in debug and null-derefs in release.
7. **Gate the installer (`packaging/mxbmrp3.nsi`)** if the feature has supporting data files (e.g. `web/` for HTTP server) so they don't ship to a build that can't use them.

If a `.cpp` file's `GAME_HAS_X` reference is in a file that doesn't transitively include `game_config.h`, add `#include "../../game/game_config.h"` (path from the file). The handlers' `plugin_data.h` already pulls it in; `hud_manager.h` pulls it in; isolated tab files like `settings_tab_hotkeys.cpp` may need the explicit include.

Reference implementations to copy from: FMX (commit `deba67f`), Discord (`GAME_HAS_DISCORD`), Records provider (`GAME_HAS_RECORDS_PROVIDER`).

## Where Things Live

The tree is the index - this is the map plus the parts that aren't guessable from a filename.

| Path | What's there |
|---|---|
| `mxbmrp3/core/` | Singletons and services: `plugin_data` (state cache, split into `_standings`/`_trackpos`/`_livegaps`; pure pieces pulled out to `blue_flag_detect.h` + `proximity_tuning.h` and friends - ARCHITECTURE.md lists them and what is deliberately left in), `hud_manager`, `settings_*`, `http_server`, `companion_window` + `hud_sw_renderer`, `stats_manager`, `fmx_manager`, `crash_handler`, `analytics_manager`, `event_recorder`, `spotter_*` (map: `tools/spottergen/README.md`) |
| `mxbmrp3/hud/` | Every HUD and widget, all deriving from `base_hud`. Settings UI is `settings_hud*.cpp` + `hud/settings/settings_tab_*.cpp` |
| `mxbmrp3/handlers/` | Callback handlers; run-prefixed = player-only, race-prefixed = all riders |
| `mxbmrp3/game/` | `unified_types.h`, `game_config.h` (compile-time game + `GAME_HAS_*`), `adapters/` |
| `mxbmrp3/vendor/piboso/` | Per-game DLL exports (`*_api.cpp`) and `api_guard.h` |
| `mxbmrp3_data/gamepads/`, `pitboards/`, `gauges/` | Asset packs: `<name>/` = art + a fixed `<type>.ini` placing content on it (a pad's 17 buttons, a board's rows, a dial's range and sweep). Same nested shape as `themes/`, same sync code |
| `mxbmrp3_data/web/` | Web overlay. Root holds `index.html`/`sw.js`/`style.css`/`custom.css`; assets live in `js/ fonts/ icons/ logos/` |
| `tests/` | `unit/` (pure logic), `integration/` (real DLL under Wine + the `check_*.sh` invariant lints), `web/` (Playwright), `asan/` |
| `tools/` | Standalone dev tools, each documented in its own header/README |

**Reading order for a new area:** the type's header comment first (mechanism), then the test that pins it (behavior + the bug it prevents). Between them they are more current than any prose here.

**Non-obvious placements:**
- **Settings** are spread deliberately: `settings_keys.h` (INI key constants), `settings_serde.h` (HUD-free serde helpers) + `settings_serde_hud.h` (the HUD-typed converters and per-HUD bitmask save/loads; include this one when serializing concrete HUD types), `settings_hud_registry.{h,cpp}` (the one ordered registry driving capture + apply + serialize), `settings_manager_global.cpp` (non-per-profile sections), `settings_hud_profiles.cpp` (profile orchestration).
- **Test-only DLL exports** live in `core/test_hooks.cpp`, which `mxbmrp3/CMakeLists.txt` excludes from every shipping target so it cannot reach a shipping DLL.
- **The callback-tape recorder** is in-plugin (`core/event_recorder.*`, MX Bikes only) and ships **dormant** - a developer opts in with the hidden `[Recorder] enabled=1` INI key. It replaced a standalone recorder plugin whose extra process caused a shutdown-teardown crash; don't reintroduce one.
- **Dev tools** don't all build the same way. `tools/replay` and `mxbmrp3_fontgen` build from `tools/CMakeLists.txt` (Windows/MSVC); `fontgen` also builds cross-platform and its `test.sh` runs in CI. `tools/hud_window` is built by its own `companion_demo.sh` (mingw + Wine).
- **Theme art is cut from a master, never generated.** `tools/themeslice` slices one image into a theme's 27 `.tga`; it draws nothing.

### Regenerating a shipped font
The shipped `.fnt` files are GENERATED from the `.ttf` in `mxbmrp3_data/web/fonts/`, normalized so every font renders numbers identically - so a font swap cannot reflow a HUD. Rebuild all: `tools/fontgen/regen_shipped.sh`, whose header documents the normalisation, the atlas-resolution rule and how to add just one.

---

## Git & Development Workflow

### Commit Message Conventions
- **Use imperative verbs:** Fix, Add, Update, Remove, Refactor, Merge
- **Be specific:** "Fix dangling pointer in HudManager::clear()" not "Fix bug"
- **Examples from history:**
  - `Add podium colors for P1/P2/P3 in standings`
  - `Fix position cache not being marked dirty when standings update`
  - `Refactor SettingsHud click handlers to reduce complexity`

### Branch Naming
- **Pattern:** `claude/descriptive-name-sessionID`
- **Examples:** `claude/analyze-comments-correctness-01EqgeCF2tcaLHWDT9xpeK1W`
- **Critical:** Branch must start with `claude/` and end with matching session ID, otherwise push will fail with 403

### Version Management
- **Never hand-create or hand-edit git tags, and never hand-edit the 4th version component.** `mxbmrp3/resource.h` is the **single source of truth** - edit only `VER_MAJOR/MINOR/PATCH` there for a release. Both release workflows auto-create the `vX.Y.Z` tag from it, with no tag-push trigger either side, so a tag cannot drift from the version (DEVELOPMENT.md → Releases); `cmake/stamp_version.cmake` stamps `VER_BUILD` from the git commit count before every build, monotonic across the repo. Those three files each document their own half. The rule that spans them is at `PLUGIN_VERSION` in `plugin_constants.h`: **don't move `#include "../resource.h"` into that header** (or make `PLUGIN_VERSION` a `constexpr` there) - it would make every build-number bump recompile **every** TU.

### Peer Reviews
- **Update `main` first:** Before peer reviewing a branch, fetch and bring local `main` up to date with `origin/main`. Diff and review the branch against the current `main` so feedback reflects the latest base, not a stale one.

### Development Style
- **Iterative refinement:** Expect many small commits for UI tweaks, alignment fixes, etc.
- **Quick iterations:** Debug strings added/removed, grid alignment tweaks, constant adjustments
