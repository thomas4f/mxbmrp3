# AI Development Context for MXBMRP3

## Read This First

This is a **racing simulator HUD plugin** for PiBoSo racing games (MX Bikes, GP Bikes, WRS, KRP). It's a DLL plugin written in C++ using each game's proprietary API, with a shared core that works across all supported games.

**For deep technical details:** See [`ARCHITECTURE.md`](ARCHITECTURE.md) (detailed documentation with mermaid diagrams, component descriptions, dependency graphs, multi-game architecture). This file is a quick-start guide.

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

**Key Singletons:**
- `PluginData` - Central game state cache, change detection
- `HudManager` - HUD lifecycle, owns all HUD instances
- `SettingsManager` - Save/load HUD configurations
- `InputManager` - Mouse and keyboard input
- `XInputReader` - Controller state and rumble effects
- `RumbleProfileManager` - Per-bike rumble profiles stored in JSON
- `StatsManager` - Unified stats, personal bests, odometers in a single JSON file
- `FmxManager` - FMX trick detection state machine, scoring, chain system
- `AssetManager` - Dynamic discovery of fonts, textures, icons from subdirectories
- `FontConfig` - User-configurable font categories (Title, Normal, Strong, Marker, Small)
- `ColorConfig` - User-configurable color palette
- `HttpServer` - Embedded HTTP server with SSE streaming for web overlays (OBS)
- `CompanionWindow` - Standalone OS window rendering the HUD on a second monitor via an in-process software renderer (`hud_sw_renderer`); each HUD can decouple its on/off + position there
- `DirectorManager` - Auto-director for spectate/replay: scores riders and cuts the camera to the most interesting subject (drives `DirectorWidget` and the web overlay's battle panel). See ARCHITECTURE.md §14.
- `EventRecorder` - Callback-tape recorder (MX Bikes only, dev tool). Taps the raw callbacks in `mxb_api.cpp` and writes a `.tape` for headless replay; dormant unless the hidden `[Recorder] enabled=1` INI key is set. Replaced the old standalone recorder plugin.

## Multi-Game Support

The plugin supports multiple PiBoSo games from a single codebase:

| Game | Config | Output | Status |
|------|--------|--------|--------|
| MX Bikes | `MXB-Release` | `mxbmrp3.dlo` | ✅ Full support |
| GP Bikes | `GPB-Release` | `mxbmrp3_gpb.dlo` | ✅ Core features |
| Kart Racing Pro | `KRP-Release` | `mxbmrp3_krp.dlo` | ✅ Core features (no FMX) |
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
  (Visual Studio 2022, `mxbmrp3.sln`, x64). You cannot produce the shippable
  artifact on Linux.
- **But you CAN now build and test on Linux** — use it to verify changes before
  committing rather than deferring everything to "a Windows user will build it":
  - `./tests/unit/run_tests.sh` — pure-logic unit tests (doctest, ~1s, only needs g++)
  - `./tests/integration/build.sh` — cross-compile the whole plugin to a Windows DLL (mingw-w64)
  - `./tests/integration/check_game_configs.sh` — syntax-check the shared sources under the GP Bikes / KRP defines (build.sh + Wine only build the MX Bikes config, so a compile error that surfaces when a `GAME_HAS_*` macro is OFF — like a gated include whose type is used unconditionally — would otherwise slip to a manual MSVC release build)
  - `./tests/integration/run_tests.sh` — doctest integration tests under Wine (smoke, race, sessions, director, version): drive real callbacks, assert `/api/state`
  - See [`TESTING.md`](TESTING.md) for the full layered guide + how to add a test
- **Preferred: run test suites and builds in the FOREGROUND with `tee` to a log**
  (e.g. `./tests/integration/run_tests.sh 2>&1 | tee /tmp/it.log`) so progress
  streams live to the user rather than being hidden in a background task (which the
  web UI shows as "output unavailable"). Don't wrap runs in backgrounded
  `until … sleep` wait-loops — those emit nothing and surface as empty tasks. Only
  background genuinely long-lived processes (a dev server), and prefer the Monitor
  tool when live progress must be surfaced while other work continues.
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

Tests are not optional scaffolding — this project has a real, CI-gated suite that
runs on Linux with no game (see [`DEVELOPMENT.md`](DEVELOPMENT.md)). **When you
change behavior, change a test:**

- **New feature or behavior change** → add or extend a test that exercises it.
- **Bug fix** → add a regression test that would have caught it (ideally one that
  fails before your fix, passes after). If it genuinely can't be reproduced in a
  headless test, say so and why.
- **You notice a missing/weak test while working** → add it, or call it out
  explicitly. Don't leave a known gap silent.

Where the test goes (pick the fastest one that can exercise the change):

| What you changed | Where the test goes |
|---|---|
| A pure helper (formatting, color, parsing, header-only math) | `tests/unit/` unit test (doctest) — compiles the real header, ~1s |
| Standings / gaps / penalties / session logic / anything in the JSON snapshot | add/extend a doctest in `tests/integration/tests/` using `PluginHost` + `checkStandings` (drives real callbacks under Wine, asserts `/api/state`) — see [`TESTING.md`](TESTING.md) |
| A settings / persistence change | `tests/integration/run_persist_test.sh` (load→save round-trip) |
| A new DLL-boundary callback or array-size/count handling | `tests/integration/callback_fuzzer.cpp` |
| Config parsing / a new INI or JSON field | `tests/integration/run_fuzz.sh` corpus |
| A hot-path change (Draw / telemetry / rebuild) | confirm `tests/integration/run_perf.sh` didn't regress |
| Installer / packaging (`packaging/mxbmrp3.nsi`) | `tests/integration/run_installer_test.sh` (makensis + Wine: asserts install/uninstall/registry/data-wipe outcomes) |
| Web overlay rendering (`mxbmrp3_data/web/` — js/overlay-*.js/style.css/index.html) | add/extend a Playwright test in `tests/web/tests/` driving `?demo` (asserts the rendered DOM) — see [`TESTING.md`](TESTING.md) |

These run headless — most via mingw + Wine, the web-overlay tests via Node +
Playwright. Manual in-game testing on Windows stays the final check for
rendering/input, but it does **not** excuse skipping an automated test when the
logic is testable headless.

## Important Patterns & Constraints

### Performance Target: 240fps
The plugin must run efficiently at **240fps** (4.17ms frame budget). Many competitive players use high refresh rate monitors. Avoid per-frame allocations, unnecessary string operations, and complex calculations in hot paths like `Draw()` and `RunTelemetry()`.

### DO:
- Use RAII (smart pointers, no raw `new`/`delete`)
- Use safe string functions (`strncpy_s`, `snprintf`)
- Add exception handling for file I/O
- Use `DEBUG_INFO_F()` for logging (not `printf`)
- Check for existing patterns before adding new code
- Wrap new DLL exports in `API_GUARD_CATCH("ExportName")` (see `vendor/piboso/api_guard.h`); uncaught exceptions across the boundary crash the host game
- Wrap new `std::thread` function bodies in a top-level try/catch; uncaught throws in threads call `std::terminate()`
- Exception-guard **every** INI parse site — hand-editing the INI (`auto_save` off + RELOAD_CONFIG) is a supported workflow, and one naked `std::stoul` aborts the whole settings load (the `parseColorHex` base-section bug)
- `isfinite`-guard persisted floats at **both** write and load — `+Inf` slips past `>`/`>=` checks that only reject NaN, and one bad sample permanently corrupts the saved value (e.g. the odometer); see `finiteOrZero()` in `stats_manager.cpp`
- Validate `_iElemSize` against the compiled struct size and `std::clamp` counts to `0..MAX_RACE_ENTRIES` in new array-style API callbacks — PiBoSo reshapes plugin structs between game versions (skew symptom: empty standings/map + one "element size N != expected M" log line)
- Use `addLabel()` for table headers and axis labels (STRONG font, Small size, row-centered); don't hand-roll `addString` at data-font size
- Gate HUD rebuilds on `isDataDirty()`/`isLayoutDirty()` (never unconditionally per frame unless the rebuild is trivially cheap); keep new global-input polling proportional to what's actually bound (see `HotkeyManager`)

### DON'T:
- Throw exceptions in core code (game engine doesn't support them)
- Use raw pointers for ownership
- Add features without understanding the data flow
- Bypass PluginData's change detection (use appropriate setter methods)
- Reintroduce per-tick XInput polling of empty/disconnected slots — `XInputGetState` on a dead slot is ms-class on degraded stacks (connection scan is throttled to 1s; the selected slot backs off to 500ms while unplugged)
- "Simplify" the rumble send policy in `setVibration()` back to value-dedup — controllers **decay** rumble without a continuous feed (a value sent once stops buzzing), so nonzero values are re-sent every `[Rumble] send_interval_ms` even when unchanged. The empirically-derived policy: nonzero re-sent on the cap, all-zero silenced, transitions to zero bypass the cap. The cap **defaults to 100Hz (10ms)** — that rate is fine on a healthy stack; the cap exists and is user-tunable only because some **buggy/degraded Bluetooth drivers** choke on sustained high-rate traffic (a driver issue, not inherent to 100Hz), and raising `send_interval_ms` is the escape hatch for them

## Maintenance Invariants (touch X → also update Y)

Regression traps where changing one thing silently rots another. The first five prevent *future* bugs, not just document past ones. ARCHITECTURE.md has the bug each one prevents.

- **Add a per-rider map to `PluginData`** (keyed by raceNum): erase it in `removeRaceEntry()` **and** reset it in `clear()`. Miss `removeRaceEntry` and a *reused* race number inherits the departed rider's stale standings/gap/position state. (Six maps were found missing.)
- **Add any input to MapHud's `renderTrack()` output** (new setting, color, transform): add it to `TrackRibbonKey` (`map_hud.h`) or the ribbon quad cache serves stale geometry. `TrackRibbonKey` guards the **screen-quad** cache — it hits in the default view (rider-only rebuilds) but by design is a pass-through in rotate-to-player/zoom (the view transform changes the key every frame there). Underneath it, `renderTrack` tessellates the ribbon once in **view-independent world space** and caches that (`WorldRibbonPoint` / `WorldRibbonKey`), then transforms the cached points to screen — this is what keeps rotate/zoom cheap (they'd otherwise re-tessellate the whole centerline every frame). The two keys guard **different** things: `TrackRibbonKey` covers everything that changes the emitted *screen* quads (view transform, clip, colors, width); `WorldRibbonKey` covers only what changes the emitted *world* points (track data via `m_worldRibbonValid` in `updateTrackData`, LOD/`detail`, and zoom-straight-subdivision). If you make the world geometry depend on a NEW input (e.g. a second LOD knob), add it to `WorldRibbonKey` or rotate/zoom serve stale ribbon geometry. `map_render_test.cpp` asserts the world cache is transparent (identical quads across LOD + rotate/zoom round-trips) and finite.
- **Add an `onDataChanged` consumer**: `DataChangeType::Standings` fires at RaceTrackPosition rate (many/sec on full grids — leader-timing quantization defeats the per-rider gap threshold, so `updateRealTimeGaps` time-coalesces to `gapNotifyIntervalMs`, default 100ms). A new consumer must be trivially cheap **or** gate on consumer activity *before* any string/alloc work (see `HttpServer::hasActiveClients()` and `SteamFriendsManager`'s POD `PresenceInputs` fingerprint).
- **HttpServer change-type gating is load-bearing**: frequent types (Standings/EventLog) gate on client activity; rare transition types (SessionData/RaceEntries/SpectateTarget) **always** rebuild — because the plugin receives **no callbacks while the player sits in menus**, so a snapshot skipped at the transition could never be rebuilt. Don't move rare types behind the gate. (This no-callbacks-in-menus constraint also bounds any "do it later on the game thread" design.)
- **Keep cross-thread flags atomic**: `setDataDirty()` writes **both** `m_bDataDirty` and `m_bLayoutDirty`; both — plus `m_bVisible` and `VersionWidget::m_showingUpdateNotification` — are `std::atomic<bool>` because background workers legitimately call `setDataDirty()`/`showUpdateNotification()` (RecordsHud fetch worker, update-checker/downloader callbacks). Any new flag written cross-thread stays atomic.
- **A mutex-guarded member is guarded at EVERY access site**, including private helpers called from already-locked-*looking* code. Preferred shape: copy under the lock, pass the snapshot into helpers — the crash-grade bug was `RecordsHud::findPlayerPositionInRecords()` iterating live `m_records` unlocked while the caller had carefully copied under lock.
- **Background workers never read live game-thread members**: snapshot at task start (RecordsHud `m_fetchProvider`/`m_fetchTrackName`), and never call cross-HUD methods that race shutdown — `HudManager::clear()` joins RecordsHud's fetch thread **before** nulling cached HUD pointers; a worker added to any HUD needs the same treatment.
- **Change the recorded callback-tape format** (a `SPlugins*` struct in `mxb_api.h`, or the record layout): `mxbmrp3/core/event_recorder.{h,cpp}` (the in-plugin recorder — MX Bikes only, `GAME_HAS_RECORDER`, tapping `mxb_api.cpp`) and `tests/integration/harness/tape.h` must stay **byte-identical** (magic, `FileHeader`/`EventHeader`, the `RaceClassification`/`RaceTrackPosition` packings), and the committed golden-master tapes (`tests/integration/tests/fixtures/*.tape.gz`) are coupled to the struct layout at record time — they need re-recording after an API-struct change. See TESTING.md.
- **New `MXBMRP3_Test_*` hook**: add it to `core/test_hooks.cpp` (the whole file is gated on `MXBMRP3_TEST_BUILD` and is **not** in `mxbmrp3.vcxproj`, so it never exists in a shipping DLL); expose it through `PluginHost`. Prefer a hook for internal state not in `/api/state` (the real-time gap is the model); don't route plugin-logic tests through the live HTTP server — read `PluginHost::snapshot()` (built directly) instead.
- **Update a vendored third-party lib** (`mxbmrp3/vendor/*`, or `tests/integration/harness/doctest.h`): bump its `version` in `mxbmrp3/vendor/vendored.json`. That file is the single source of truth for the release **SBOM** (`tools/gen_sbom.py`) and the weekly **freshness check** (`.github/workflows/vendored-deps.yml`) — leave it stale and the SBOM misreports what ships and the check nags for the wrong version. GitHub's dependency graph / Dependabot can't see vendored C++ source, which is the whole reason this manifest exists. The single-header libs (json, httplib, doctest) are drop-in from the upstream release's single file; **miniz** is a split amalgamation whose layout differs from its repo tree (needs the release zip). After any bump, the cross-build + Wine suite is the safety net (cpp-httplib especially — its API drifts across releases).
- **Add a per-surface HUD setting to the companion instance** (the "two settings menus" decoupling — a second on/off + position each HUD carries for the companion window; `base_hud.h` `m_bCompanion*`): wire it through **four** places or it half-works. (1) a `getCompanionX()` accessor that **falls back to the game value while `!m_bCompanionConfigured`** (mirror) and a `setCompanionX()` that calls `ensureCompanionConfigured()` first (snapshot-on-first-edit); (2) `SettingsManager::captureBaseHudSettings` writes it **only when the companion instance has DIVERGED from the game** (configured **and** any of companion visible/x/y differs from the game value), and `applyBaseHudSettings` **applies-or-clears authoritatively** (absent ⇒ `clearCompanionState()`, so an upgraded/renamed HUD doesn't inherit stale companion state). This "persist-only-moved" gate is load-bearing: the companion is configured for **every** HUD the first frame the window opens (`HudManager::collectRenderData` calls `snapshotCompanionFromGame()` — "decouple from the start", so the companion stops mirroring the game once opened), but a snapshot copies the game values **verbatim**, so a HUD the user never rearranged on the companion compares exactly equal and is **not** persisted — it re-snapshots from the game on the next open. So the sparse-save property holds for **any** user who doesn't actually move a HUD on the companion, and — critically — a HUD left untouched on the companion still picks up a **changed default position** on upgrade (gating on `isCompanionConfigured()` alone would have pinned every HUD's old position for anyone who ever opened the window). Only genuinely diverged HUDs persist their four `companion*` keys. Test both: a diverged HUD persists, a configured-but-equal HUD does **not** (`companion_decouple_test.cpp`); (3) `HudManager::collectSurface(companion)` reads the companion value on the companion pass, and `collectSurface(false)` stays **byte-identical** to the game frame *when the companion is inactive/disabled* (the mouse **pointer** and the open **settings menu** are the exception — they render only on the **active** surface, so the game frame drops them while the cursor is on the companion); (4) `settings_hud.cpp` + `base_hud` input **and `HudManager`'s drag pick/gate** route the toggle/drag by `InputManager::getActiveSurface()` — which follows the **window under the cursor** (`surfaceWindowUnderCursor`), not keyboard focus, and the drag hit-test uses that surface's offset (a companion HUD sits at its companion offset). **Any interactive HUD** (settings menu, records/standings/map click targets, the corner buttons, version-widget notifications) builds its click/hover regions at the **game** offset, but on the companion the render is translated — so it must shift the cursor into build space with `mapCursorToHudSpace()` before hit-testing its own regions (or `isPointInActiveBounds()` for a whole-widget button); miss this and a HUD moved on the companion has hit-boxes where it sits **in-game**. Reset is automatic via the factory snapshots. The base `[HudName]` INI section round-trips verbatim, so a persistence *string* check can't tell captured from passed-through — test the live HUD via the `MXBMRP3_Test_Standings*` hooks (`companion_decouple_test.cpp`).
- **A HUD that skips its rebuild/tick when hidden must gate on `isVisibleAnySurface()`** (visible in-game **or** on the companion when it's open), never `isVisible()`/`m_bVisible` alone — else a HUD enabled *only* on the companion renders **stale** (the reported "gap bar / map / telemetry don't update on the companion" bug). The gate has **several shapes**, so grep for all of them, not just one: the common `if (!isVisible()) return;` early-out; the **inverted** `if (isVisible()) setDataDirty()` / `if (isVisible()) rebuild` used by HUDs that must accumulate state every frame while hidden and gate only the *render* (`gap_bar_hud`, `fuel_widget`, `stats_hud` — their state feeds other HUDs/history, so they can't early-return); the compound `if (!isVisible() || …)`; the member-form `if (!m_bVisible) return;`; and `needsFrequentUpdates()` (gates the live-timing tick — `lap_log_hud`). The always-on state tracking above such a gate stays as-is; only the visibility test changes. `isVisibleAnySurface() == isVisible()` when the companion is disabled, so single-window behavior and cost are unchanged.

## Design Decisions (Don't "Fix" These)

**Singletons Everywhere**
Required by plugin API - we get one global entry point, everything branches from there.

**Lambdas in settings_hud.cpp rebuildRenderData()**
Intentional - they capture local layout state. Alternatives were worse (passing 8+ parameters).

**Public member variables on HUDs (e.g., `m_enabledRows`)**
These are configuration data, not encapsulated state. SettingsHud needs direct access.

**HUDs don't cache raw game data**
HUDs pull fresh from PluginData on rebuild - they only cache formatted render data (`m_displayEntries`, `m_quads`, `m_strings`).
This enforces PluginData as single source of truth and prevents synchronization issues.

**Settings reset reuses save/load serialization (don't add a third list)**
"Reset to defaults" replays a startup snapshot through the *same* applier `loadSettings()` uses, never a hand-maintained list of per-setting resets. Two snapshots back this, and they are intentionally separate:
- `m_globalDefaultsIni` — global sections (`writeGlobalSettings`/`applyGlobalLine`).
- `m_hudFactoryDefaults` — pristine per-HUD constructor defaults, captured *before* `loadSettings()` folds user base-section keys into `m_hudDefaults`.

`m_hudDefaults` (sparse-save baseline, with base-section edits folded in) is **not** a clean factory snapshot — don't "simplify" reset by pointing it at `m_hudDefaults` or by merging the two caches; that reintroduces stale-default-on-reset bugs (e.g. an upgraded HUD default not taking effect). A new setting gets reset coverage for free as long as it's wired into save/load. See ARCHITECTURE.md "Settings & Persistence".

**Widget vs HUD Distinction**
Widgets (TimeWidget, PositionWidget, LapWidget, SpeedWidget, GearWidget, ClockWidget, SpeedoWidget, TachoWidget, BarsWidget, FuelWidget, LeanWidget, GForceWidget, TyreTempWidget, EcuWidget, GamepadWidget, CompassWidget, VersionWidget, SettingsButtonWidget, DirectorWidget) are simplified HUD components with:
- Single-purpose display (no configurable columns/rows)
- Minimal settings (just position, scale, opacity)
- Simpler rendering logic

Full HUDs (StandingsHud, LapLogHud, PitboardHud, TimingHud, NoticesHud, StatsHud, etc.) have:
- Complex data visualization
- Extensive customization (column/row toggles, gap modes, etc.)
- More configuration options

**Helmet Overlay (HelmetOverlayHud)**
Full-screen immersion overlay — neither a widget nor a typical HUD:
- Renders textured quads (helmet upper/lower) over the entire viewport
- Telemetry-driven tilt (lean angle) and vibration (suspension deltas)
- Registered first in HudManager so it draws behind all other HUDs
- Global settings (not per-profile) — saved in its own `[HelmetOverlay]` INI section like `[Rumble]`
- Hidden during spectate/replay/crash; no title, no dragging, no scaling

**Companion Window (CompanionWindow + hud_sw_renderer)**
Standalone OS window that renders the HUD on a second monitor — *not* a network mirror and unrelated to the web overlay:
- Reads the plugin's live render primitives directly and draws them with an **in-process software renderer** (`hud_sw_renderer`): scanline quad fill, affine sprite blit, PiBoSo `.fnt` bitmap text. It reproduces the game's texture stage (texel × quad color) so per-quad **opacity** and white-icon **colorization** match — don't "simplify" the blit back to a plain copy or icons stop tinting / ignore opacity.
- **Own window thread** owns the Win32 loop and renders on its own cadence, so the window stays live **in menus** when the game issues no `Draw` (the same no-callbacks-in-menus constraint the HttpServer notes). The game thread only `submit()`s a POD frame copy under a mutex.
- **Never takes focus** (`WS_EX_NOACTIVATE` kept for the window's whole life — input is routed by the window under the cursor, so it never needs activating), persists geometry + maximized state, hides the OS cursor over its client area, and falls the display target back to In-game when the user closes it. Renders into the **full client** with a centered 16:9 *scale* viewport (no distortion), so HUD elements placed outside `[0,1]` use the whole window like in-game — not a letterbox.
- Each HUD can **decouple** its on/off + position here (see the Maintenance Invariant); everything else is shared. Runtime `[Display]` target only (In-game / Companion / Both); analytics `feat_companion`. Render cadence is INI-only (`[Display] companionRefreshHz`, default `0` = V-Sync via `DwmFlush`, `N` = fixed Hz cap). See ARCHITECTURE.md §13.

**Handler-to-API Event Mapping**
Each handler corresponds to game API callback(s), but receives unified types:
- Run handlers (RunHandler, RunLapHandler, etc.) = player-only events
- Race handlers (RaceEventHandler, RaceLapHandler, etc.) = multiplayer/all riders
- See ARCHITECTURE.md for full mapping

**Layered automated tests + manual in-game**
There is a real, CI-gated test suite, all runnable on Linux with no game engine.
**[`TESTING.md`](TESTING.md) is the guide** (layers, harness, philosophy, how to
add a test); the short version: pure-logic unit tests (`tests/unit/`, doctest); the
integration layer (`tests/integration/tests/`, doctest) that cross-compiles the whole
plugin, loads it under Wine, drives the **real callbacks** via `PluginHost`, and
asserts the plugin's computed state — read via `snapshot()` (built directly, no
HTTP server) or typed `MXBMRP3_Test_*` hooks for internal state, plus **real-data
golden masters** that replay actual in-game callback captures (the in-plugin
recorder `[Recorder] enabled=1` → tape → `replayTape`); Playwright web-overlay tests (`tests/web/`); and
specialized runners (persistence, fuzz, perf). They run in CI on every push.
Manual in-game testing on Windows remains the final check for rendering, input,
and game-specific behavior the headless build can't exercise — it complements the
automated tests, it isn't replaced by them.

**Logger has an internal mutex**
`Logger::log()` is called from the game thread and from at least five background threads (HttpServer, UpdateChecker, UpdateDownloader, DiscordManager, RecordsHud). The mutex serializes concurrent writes so log lines don't interleave. Don't remove it. The SEH crash filter deliberately doesn't call Logger to avoid deadlocking on this mutex.

## Common Tasks

### Adding a New HUD
1. Create class inheriting from `BaseHud` (`.h` and `.cpp` files in `mxbmrp3/hud/`)
2. **Add files to Visual Studio project:**
   - `mxbmrp3/mxbmrp3.vcxproj` - Add `<ClInclude>` for `.h` and `<ClCompile>` for `.cpp`
   - `mxbmrp3/mxbmrp3.vcxproj.filters` - Add filter entries to place files in `Header Files\hud` and `Source Files\hud`
   - **Without these entries, the build will fail with linker errors (LNK2019 unresolved externals)**
3. Implement `rebuildRenderData()` - builds vectors of quads/strings
4. Register in `HudManager` constructor (add pointer, getter, initialize in `initialize()`, null in `clear()`)
5. Add tab in `SettingsHud` for configuration
6. Add save/load via the **per-HUD serializer registry** (`settings_hud_registry.{h,cpp}`): write a `cap_<Name>` and `app_<Name>` (private static `SettingsManager` members — declared in `settings_hud_registry_decls.inc`, defined in `settings_hud_registry.cpp`) and add **one row** `{ "<Name>", &SettingsManager::cap_<Name>, &SettingsManager::app_<Name> }` to `hudSectionRegistry()`. That single row registers the HUD for capture, apply, **and** on-disk serialization at once — `captureToCache`, `applyProfile`, and `serializeSettings` all iterate the registry, so there is no longer a separate `hudOrder` list to forget (the FriendsHud "third hardcoded list" trap is gone by construction). Reset stays automatic via the factory snapshots. For a *global* single-value setting, use `writeGlobalSettings()`/`applyGlobalLine()` in `settings_manager_global.cpp` instead. Game-gated HUDs wrap their fn decls (in the `.inc`), their definitions, and their registry row in the same `#if GAME_HAS_*` — `settings_manager.h` includes `game_config.h` before the `.inc` so the guards resolve.
   - The functions are `SettingsManager` **members** so they inherit its `friend`-ship with the HUD classes (the bodies read/write private HUD members); `hudSectionRegistry()` is a `friend` so it can take their addresses.
   - `tests/integration/tests/settings_sections_test.cpp` remains a belt-and-suspenders CI check that every section `captureToCache()` produces is actually serialized (via `MXBMRP3_Test_CapturedSections`).

### Debugging Rendering Issues
- Check if HUD is visible: `hud->isVisible()`
- Check if data is dirty: `hud->isDataDirty()` triggers rebuild
- Enable debug logging: Set breakpoint in `rebuildRenderData()`
- Verify coordinates: Game uses normalized coords (0.0-1.0)

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
The embedded HTTP server (`core/http_server.cpp`) streams race data to browser-based overlays via Server-Sent Events (SSE):
- **JSON snapshot** built on the game thread in `buildJsonSnapshot()` (PluginData access is not thread-safe)
- **Cached string** protected by mutex, read by SSE threads
- **Data contract**: `session` (time, type, palette, fonts), `standings[]` (per-rider with all chips), `events[]` (all events, unfiltered), `overlayCmd` (broadcaster panel-force, see below), `laps[]` (per-rider oldest-first lap-time series `t[]` + optional per-lap validity `v[]` — the raw data the session-charts carousel derives all four charts from; `v` omitted when every lap is valid)
- **Broadcaster panel-force (`overlayCmd`)**: in-game hotkeys (`HotkeyAction::OVERLAY_FORCE_*`, gated on `GAME_HAS_HTTP_SERVER`) let a broadcaster force a bottom-slot panel in immediately. The plugin emits `overlayCmd:{panel,seq}` in every snapshot and `HttpServer::forceOverlayPanel()` bumps `seq` + pushes. The client is **edge-triggered on `seq`** (acts only when it changes; adopts the first seq seen so a mid-session connect doesn't replay) — so the panel name in `overlayPanelName()` (C++) must stay in lockstep with the `createSlotPanel` `name` values (`overlay-panels.js`). `forceSlot()` slides out any active panel, then `ctrl.force()` bypasses the slot's mutual-exclusion gate and any trigger (momentary). Manual force is **gated on neither `enabled()` nor `eligible()`** — disabling only stops the *automatic (event-driven) show*, and a broadcaster may force a panel outside its usual conditions (e.g. the lap boards during qualifying, or down-the-order which has no auto-trigger at all). `build()` is the backstop: a force before data exists returns 0, and **every forceable panel opts into `showEmptyWhenForced`** so the framework renders that panel's `renderEmpty()` — the **one shared `slotEmptyRow()` "No data" placeholder** (its title + a muted, flush-left row in the normal panel font), identical across the lap boards, best-sectors, down-the-order and session-charts, so a caster always gets consistent confirmation the hotkey fired. (Don't reintroduce a per-panel empty style: the old split had the lap boards showing "No data", session-charts showing "No lap data yet" in the display/marker font, and tail/sectors silently no-opping — the inconsistency users noticed.) Panels whose `autoHide` is 0 (tail/sectors/charts self-terminate via their sequence/paging) set their own finite self-hide inside `renderEmpty`, because the framework skips `onShow` for a forced-empty. The lap boards' and charts' `refresh` is placeholder-aware, so a forced-empty board/carousel populates live the moment data arrives. (The battle panel is not forceable — it's always synced to the director — so it never takes this path.)
- **Plugin sends raw data** — event/chip filtering, timestamps, and display settings are controlled client-side via the `CONFIG` block in `overlay-config.js`
- **Rider names are UTF-8-aware only on the web overlay** — the in-game `.fnt` renderer is a byte-indexed 256-glyph CP1252 table (see `fontgen.cfg`) and garbles multi-byte names regardless of any truncation logic, so UTF-8-safe truncation in-game is moot; the overlay handles names client-side
- **Web files** served from `plugins/mxbmrp3_data/web/` — users can customize CSS/HTML/JS freely (user overrides synced from Documents folder)
- **Lightweight style overrides**: users who only want to tweak the theme can create `Documents\PiBoSo\[Game]\mxbmrp3\web\custom.css` (synced into the served folder on game start) instead of forking `style.css`. `index.html` already links `custom.css`; the plugin doesn't ship a stub, so the link 404s harmlessly when no override exists. It's loaded after `style.css`, served with `Cache-Control: no-cache`, and excluded from the SW precache + fetch handler so edits show up on the next browser reload.
- **To add a new field**: add to `buildJsonSnapshot()` in the appropriate section, then consume in the overlay client (`overlay-*.js`)
- **Logo slideshow**: `GET /api/logos` scans `web/logos/` for PNGs and returns a sorted filename list. Users drop PNGs into the `logos/` folder (or the Documents user-override path `mxbmrp3/web/logos/`), no config editing needed. Bundled logos should be added to `PRECACHE_URLS` in `sw.js`.
- **Service worker / offline cache**: `mxbmrp3_data/web/sw.js` precaches the overlay shell so OBS can render it before the plugin's HTTP server is up. The `PRECACHE_URLS` list is hand-maintained — when adding new CSS/JS/font/icon assets under `mxbmrp3_data/web/`, also add them to `PRECACHE_URLS` in `sw.js`, otherwise they won't be available offline until first online load. Cache name is tied to `PLUGIN_VERSION` (substituted server-side in `http_server.cpp`), so plugin upgrades auto-invalidate the cache. When that happens the `activate` handler purges the old `mxbmrp3-overlay-<oldver>` cache and reports it — a `console.log` in the SW context **and** a `postMessage({type:"mxbmrp3-cache-updated"})` the overlay logs in the **page** console (overlay-config.js) so a caster sees the version-driven refresh where they're looking. (First install stays quiet — nothing to purge.)
- **Bottom-slot broadcast panels (`overlay-panels.js` + `overlay-slots.js`)**: the standings tower has one shared "bottom slot" used by several panels — fastest-last-lap, fastest-laps (session best), best-sectors (non-race), down-the-order (a vertical scroller of riders hidden below the Max Riders cutoff), session-charts (a carousel of race-progression SVG line charts), and battle (close-running groups, with detail sub-rows). They're registered through one `createSlotPanel({ panel, name, enabled, eligible, eventKey?/interval?/triggerOnEligible?, build, autoHide, refresh?, onShow?/onHide? })` controller that owns the bottom-up slide, the masking flag (held through the slide-out), **mutual exclusion** (`slotBusy()` — only one panel shows at a time; others skip that cycle), the show trigger + auto-hide, and reports covered rows via `slotCoveredRows()` so `renderStandings` hides the chips on the rows it overlays (chips hang outside the tower, so the panel can't cover them). **The lap/sector boards are event-driven, NOT on a cadence timer** (`eventKey()` returns a signature of the board's headline metric — session-best / fastest-recent-lap / best-sector — and the panel shows once when it changes, holds `autoHide`, then hides; `fastestOf()` builds the lap signatures, `sectorsEventKey()` the sector one). **Best-sectors is a horizontal page carousel**: the plugin emits per-sector ranked rider lists (`sectors:[{s, riders:[{num,ms}]}]`, non-race only), and it pages one sector at a time (`buildSectorsPages`/`sectorsStep`, `autoHide:0` self-terminating). **Session-charts is a sibling carousel** (`buildChartsPages`/`chartsStep`, `autoHide:0`): the plugin sends raw per-rider lap series (`laps[]`) and the client derives four race-progression charts — lap (position bump chart) / trace (cumulative vs reference pace, race-only) / gap / pace — a direct JS port of `hud/session_charts_math.h` (`fmtChartSecs` mirrors the C++ `formatSecs`; keep them in step), rendering each as an inline SVG page. Which charts appear is **overlay-configurable** (per-chart toggles), the rider-line count reuses `slotRows`, and axis/tag labels use the SMALL font (Tiny5) via a new `--gf-small`/`--font-small`. It's **race-only** and **priority 2** (above the timed boards) so it pre-empts the slot when it **auto-shows once the leader (P1) finishes** (`chartsLeaderFinishKey`, event-driven); the `OVERLAY_FORCE_CHARTS` hotkey forces it anytime. A forced-empty placeholder (`showEmptyWhenForced`) is **placeholder-aware like the lap boards** — a finite self-hide bounds it and a `refresh` upgrades it in place into the real carousel the moment laps arrive. **Both carousels build their title INTO each page** (the header is the page's first row, so it slides in with its board/chart — not a fixed title strip that swaps text in place; the viewport is `(pageSize+1)*rh` tall). **Down-the-order is a vertical scroller** (`buildTailList`/`runTailSequence`, `autoHide:0`): one list of all hidden riders that scrolls down then back up over Panel Time (pause / scroll down / pause / scroll up / pause), sized to `min(slotRows, tailLen)` rows. The battle panel is `triggerOnEligible` and **always mirrors the in-game director** (no toggle, no force hotkey): `build`/`eligible` use only `directorBattle()` (the group containing `director.subject`), so it shows/holds only the battle on camera and hides when the director leaves battles; a `refresh` re-syncs when the director moves to a different battle (`autoHide:0`). **A cut to a DIFFERENT battle is a full panel change like every other slot panel** (`ctrl.restart()`): the whole panel slides fully OUT (revealing the tower behind it), then a fresh one slides back IN with the new battle — so there is only ever ONE `.battle-track` and ONE `.battle-title`, never an in-place two-card reel (that old `slideBattleContent` reel is gone — it caused two headers, kept the tower covered, and half-slid). `refresh` picks the path via `isSameBattle()` (any shared rider ⇒ same battle): a front-swap / gap update ⇒ re-render in place; a rider joining/leaving the SAME battle ⇒ in place with a smooth **gap-free** height animation (`animateBattleTo`, hysteresis via `BATTLE_MEMBER_REST_MS` so an edge-hovering rider can't thrash it): a GROW builds the taller content first (new bottom row clipped below the short box, revealed as it opens), a SHRINK keeps the current taller content and collapses the box OVER it (overflow clips the departing bottom row away) and only swaps in the shorter content once fully collapsed — so a leaving row is never yanked mid-collapse (no black-box gap) and an arriving row is present before the box opens; a FULLY DISJOINT group ⇒ `restart()` — slide out, then in, using the SAME timed hand-off as an evict (`--anim-slide`) so the slide-out completes before the slide-in starts. `restart()` is a generic `createSlotPanel` capability (a clean out-then-in for any panel whose subject fundamentally changes), not battle-specific. The `?demo` race director cuts between battles (see `tickRace`) so this is exercised; `tests/web/overlay.spec.js` asserts one-card/one-header, no reel overlay, and a full shown→fully-hidden→shown cycle. The caster can't choose when it shows — the director frames battles automatically. **Down-the-order has neither an event nor a cadence** — it's coverage, not a story, so it only appears when the caster forces it via the Down-the-order hotkey. The `interval` cadence path still exists in `manage()` as a fallback but no shipped panel uses it. Panel rows are sized to the **measured pixel** row height (not CSS rem) so the panel is a whole number of rows tall and lands on a row boundary (no sliver). **Three shared knobs size/time every bottom-slot panel** instead of per-panel settings: `slotRows` ("Panel Rows") is the rider-row count; `slotDuration` ("Panel Time") is the seconds a panel holds the slot — the single-view boards use it directly (`autoHide: slotDuration*1000`), the best-sectors carousel splits it across its sector pages via `slotPageMs(pageCount)` (floored so a many-page carousel stays readable), and down-the-order splits it into five equal scroll phases (the battle panel ignores it — it holds as long as the camera is on the battle, `autoHide:0`); and `slotRest` ("Panel Rest", 0=off) is a **global** inter-panel gap — a module-level `slotRestUntil` armed by `hide(true)` when a panel ends its turn *naturally* (auto-hide or a triggered panel losing eligibility), which `manage()` gates every new showing on (`resting`). It is deliberately **not** armed by a pre-empt eviction or a battle-to-battle hop (those go through the plain `ctrl.hide()`, `armRest` falsy), and a manual force bypasses it. The event-driven gate is a **defer, not a drop** (the `!resting` check sits *before* consuming `lastEventKey`, so an event landing mid-rest still shows once the rest passes). Adding a panel is one `createSlotPanel` spec — don't reintroduce per-panel show/hide/timer/mask globals or per-panel row/duration settings.
- **CSS theming via `:root` tokens + `custom.css`**: the whole look is driven by the `:root` manifest in `style.css` — palette, fonts, font scale (`--fs-*`), spacing (`--sp-*`), layout widths (`--row-height`, `--col-pos-w`, `--col-gap-w`, `--battle-headline-w`, …) and animation timings (`--anim-*`, `--ease`). **Reuse tokens instead of hardcoding**; shared widths are tokens so the tower, boards, tail and battle stay aligned (e.g. battle cards size their leading column to `--col-pos-w + --posdelta-w` to align plates with the tower and track the ± column). `custom-sample.css` is a commented reference users copy to `custom.css`. Note: **colors and fonts are synced from the in-game palette at runtime as inline styles**, so a `custom.css` override of those needs `!important` (sizes/spacing/animations override directly). The few JS teardown timers that must match a CSS animation (panel slide, focus card, logo) read the duration back from the var via `cssTimeMs()`, so the `--anim-*` tokens stay the single source.
- **Per-renderer colour-role mapping**: the plugin ships palette *values* (`--gp-*` from the in-game colors), but *which slot an element uses* is decided independently in the in-game HUD (`ColorSlot::…`) and the overlay CSS (`--x: var(--gp-y)`). So a "which slot does X use" change (e.g. the number plate switching from PRIMARY to SECONDARY) must be made on **both** sides to stay consistent. Same for small helpers mirrored across the boundary — e.g. `PluginUtils::isColorDark` (C++) and the `isColorDark` in `overlay-util.js`.
- **Session clock in time+lap overtime**: when a time+lap race's clock expires it shows a leader-relative label (`N TO GO` / `FINAL LAP` / `CHECKERED`) instead of freezing at `00:00`. `PluginData::getLeaderLapsToGo()` (logic — same thresholds as `isRiderFinished`/the FinalLap event) and `PluginUtils::formatSessionClock()` (string) are the single source, used by the StandingsHud title and the JSON `time` field, so in-game and web read identically. (The TimeWidget deliberately shows *only* the plain `MM:SS` via `formatTimeMinutesSeconds()` — no overtime label — so it is intentionally *not* a `formatSessionClock()` consumer.)
- **Overlay live gaps (battle cards)**: the real-time gap (`StandingsData::realTimeGap`, computed by `updateRealTimeGaps` on every `RaceTrackPosition`) is **always** surfaced to the overlay as per-rider `liveGapMs` (leader-relative ms) + `liveGapValid` — the plugin sends the raw data and the on/off is a **purely client-side overlay setting** (`CONFIG.battleLiveGaps`, a checkbox in the overlay's own settings UI, **default off / opt-in**; **not** a plugin/INI setting and **not** the in-game live-gap HUD option — presentation lives in the web UI). `overlay-util.js`'s `battleInterval()` shows the live interval when `CONFIG.battleLiveGaps && both riders' liveGapValid` **and the computed live interval is positive**, else the official split (a jittery non-positive live value falls back rather than rendering an empty flash — matches in-game, which never blanks a battling rider). **`liveGapValid` is a DATA-validity flag, deliberately different from the in-game per-row display predicate** (`standings_hud.cpp` `canUseLiveForRider`): it is **true for the leader** (its 0 is a valid reference) and false for a rider that dropped out of the ~10-closest track-position batch (stale) or is lapped — the in-game predicate instead excludes the leader (it renders "Leader"). They answer different questions ("is this value a usable reference" vs "show a live gap in this row"), so **don't unify them**. The battle *grouping* stays on the official gap (`getBattleGroups`) for stability — only the displayed interval goes live. Currently battle-cards only; the tower still shows official gaps.
- **Demo mode (`?demo`)**: append `?demo` (or `#demo`) to the overlay URL to replay a synthetic 22-rider race instead of connecting to the plugin — it feeds the same JSON snapshots into `render()`. Use it to preview/iterate the overlay (panels, battles, theming) without launching the game; the live SSE path is untouched. It also briefly simulates time+lap overtime so the laps-to-go clock can be previewed. Add `&speed=N` to fast-forward past the warmup. **To eyeball your change headlessly** (no game, no display), screenshot `?demo` with Playwright — see `tests/web/README.md` → *Screenshot the overlay*. That is the **browser-overlay** path; the in-game/companion HUD is a separate renderer with its own harness (`tools/mxbmrp3_hud_window/`).
- **Mobile fill-width**: on phone-sized touch screens the overlay stops being a fixed corner widget and fills the viewport width. A `@media (pointer: coarse) and (max-width: 820px)` block in `style.css` puts `#overlay` in normal flow at the top-left (ignoring `towerX/towerY`) and lets the page scroll; `applyRootSizing()` in `overlay-shell.js` scales the root `font-size` so the tower's computed width equals `window.innerWidth`. Because every size in the tower is proportional to `font-size` and `offsetWidth` is linear in it, the scale is a stable fixed point (re-running it is a no-op). It's gated on `pointer: coarse`, so OBS/desktop browser sources (`pointer: fine`) are unaffected. Name truncation is unchanged — names clip to the name column via the `nameChars` substring + `text-overflow: ellipsis`, so a wide name setting shows "…" rather than overflowing. The same media block also handles elements that don't fit the fill-width model: the rider **focus card** (a fixed 20rem broadcast element outside `#overlay`) is force-hidden; the **status chips** (`.row-chips`, absolutely positioned at `left: 100%`, i.e. off-screen once the tower is full-width) are hidden; and the **settings gear** is forced visible because its normal `mousemove` reveal never fires on touch (otherwise settings would be unreachable on a phone).

### Adding Support for a New Game Feature
1. Add field to appropriate `Unified::` struct in `game/unified_types.h`
2. Add conversion in each adapter (`game/adapters/*_adapter.h`)
3. Add feature flag to `game/game_config.h` if game-specific
4. Update handlers/HUDs to use the new field

### Disabling a Feature Per-Game

When an entire feature (HUD, manager, integration) doesn't apply to one or more games — e.g. FMX freestyle tricks on karts, Discord Rich Presence on non-MXB, the records provider on non-MXB:

1. **Add a `GAME_HAS_X` flag** to `game/game_config.h`. Examples already in the file: `GAME_HAS_DISCORD`, `GAME_HAS_HTTP_SERVER`, `GAME_HAS_FMX`, `GAME_HAS_RECORDS_PROVIDER`. Pattern:
   ```cpp
   #if defined(GAME_MXBIKES) || defined(GAME_GPBIKES)
       #define GAME_HAS_FMX 1
   #else
       #define GAME_HAS_FMX 0
   #endif
   ```
2. **Gate the HUD registration** in `HudManager::initialize()`. Leave the member pointer as `nullptr`; existing null-checks downstream (`if (m_pFmxHud)`) will fall through silently.
3. **Gate the settings tab** in `SettingsHud` — prefer the runtime null-check pattern used for `TAB_RECORDS` (`if (i == TAB_FMX && !m_fmxHud) continue;`) over a `#if` block. Cleaner and reuses the nullptr you set up in step 2.
4. **Gate the hotkey row** in `settings_tab_hotkeys.cpp`. The hotkey *action* itself can stay in the enum (the handler in `HudManager::processHotkeys` is already null-safe), but the row should be hidden so users don't see a binding that does nothing.
5. **Gate handler entry points** that feed the disabled manager (`run_telemetry_handler.cpp`, `race_session_handler.cpp`, etc.). Skip the singleton calls entirely so the binary doesn't pull them in.
6. **Gate `SettingsManager` save/load** if the disabled HUD has its own profile section. Crucial when `HudManager::getXxxHud()` returns a `Hud&` with `assert(m_pXxxHud)` — calling it with a null member crashes in debug and null-derefs in release.
7. **Gate the installer (`packaging/mxbmrp3.nsi`)** if the feature has supporting data files (e.g. `web/` for HTTP server) so they don't ship to a build that can't use them.

If a `.cpp` file's `GAME_HAS_X` reference is in a file that doesn't transitively include `game_config.h`, add `#include "../../game/game_config.h"` (path from the file). The handlers' `plugin_data.h` already pulls it in; `hud_manager.h` pulls it in; isolated tab files like `settings_tab_hotkeys.cpp` may need the explicit include.

Reference implementations to copy from: FMX (commit `deba67f`), Discord (`GAME_HAS_DISCORD`), Records provider (`GAME_HAS_RECORDS_PROVIDER`).

## Files You'll Likely Need

**Core:**
- `mxbmrp3/core/plugin_manager.cpp` - Plugin coordinator (receives unified types)
- `mxbmrp3/core/plugin_data.h/.cpp` - Game state cache
- `mxbmrp3/core/hud_manager.h/.cpp` - HUD ownership
- `mxbmrp3/core/asset_manager.h/.cpp` - Dynamic asset discovery (with user override support)
- `mxbmrp3/core/font_config.h/.cpp` - Font category configuration
- `mxbmrp3/core/color_config.h/.cpp` - Color palette configuration
- `mxbmrp3/core/update_checker.h/.cpp` - GitHub update checker
- `mxbmrp3/core/update_downloader.h/.cpp` - Update download and installation
- `mxbmrp3/core/tooltip_manager.h` - UI tooltip management (header-only)
- `mxbmrp3/core/xinput_reader.h/.cpp` - Controller input and rumble effects
- `mxbmrp3/core/rumble_profile_manager.h/.cpp` - Per-bike rumble profiles
- `mxbmrp3/core/stats_manager.h/.cpp` - Unified stats, personal bests, odometers
- `mxbmrp3/core/fmx_manager.h/.cpp` - FMX trick detection and scoring
- `mxbmrp3/core/fmx_types.h` - FMX data structures (TrickType, TrickInstance, RotationTracker, etc.)
- `mxbmrp3/core/http_server.h/.cpp` - Embedded HTTP server with SSE for web overlays
- `mxbmrp3/core/companion_window.h/.cpp` - Standalone second-monitor HUD window (own Win32 thread; not a network mirror)
- `mxbmrp3/core/hud_sw_renderer.h/.cpp` - In-process software renderer for the companion window (quads, sprites, `.fnt` text, texture modulate)
- `mxbmrp3/core/event_log_types.h` - Event log entry types and filter flags
- `mxbmrp3/core/crash_handler.h/.cpp` - SEH minidump filter; writes `.dmp` to `<savePath>\mxbmrp3\crashes\` on unhandled hardware faults
- `tools/mdmp_analyze.py` - **General** standalone minidump triage (no Windows debugger needed, `capstone` used only if installed). It is a **reference tool** that surfaces facts + heuristic leads for a human/agent to reason over — **not** an auto-analyzer; trust its output as signposts, not conclusions. Prints the dump fingerprint (sha256 / capture time / PID / uptime-at-crash — catch re-sent duplicates before wasting effort), system + GPU info, exception code/faulting address, the **full register file**, the **faulting instruction bytes** (disassembled if `capstone` is present), a heuristic live-stack scan attributing the crash to the plugin vs game vs a GPU/system DLL, third-party/injected modules (overlays, anti-cheat, capture hooks), a breadth-first **crash classifier**, and a VERDICT that, when the fault is in a system/runtime **leaf** (CRT/`ntdll`/GPU driver), reports the **nearest non-system caller** as the likely culprit instead of blaming the leaf module. Run `python3 tools/mdmp_analyze.py <file.dmp>`, or `--compare <a.dmp> <b.dmp>` to diff two crashes' signatures (and flag identical re-sent files). Add `--record` to append a dump's provenance (filename/sha256/pid/capture time/fault) to the matching crash's `samples` list in `known_game_crashes.json` so you know which `.dmp`/`.log` files to keep for a given bug — it records **only** dumps that match a catalogued crash (idempotent by sha256; an unmatched dump is reported, not added, keeping the catalogue limited to understood crashes). `samples` is internal provenance and is **not** emitted into the player-facing `KNOWN_GAME_CRASHES.md`. Add `--note "<text>"` alongside `--record` to attach a free-text note (e.g. a video link or session context) to the recorded sample — never auto-filled; a `--note` on a dump already on file updates that sample's note. Optional disassembly: `python3 -m pip install -r tools/requirements.txt` (capstone).
  - **Don't tunnel-vision.** Vanilla MX Bikes has many *distinct* crashes; this tool triages whatever shows up, it does not confirm a favorite diagnosis. The classifier covers the common families — access violation (read/write/**execute**), debug-fill/heap-guard **sentinel** values (use-after-free / uninitialized: `0xFEEEFEEE`, `0xCDCDCDCD`, `0xCCCCCCCC`, …), 64-bit→32-bit **pointer truncation**, near-null deref, **stack overflow**, `/GS`/`__fastfail` stack-buffer-overrun, **heap corruption**, unhandled **C++ throw**, illegal/privileged instruction. When nothing matches it says so explicitly and tells you to inspect by hand — treat that as "investigate," not "boring." The live-stack output is a scan (not a real unwind), so it includes residue; weight the frames nearest RSP.
  - Symbol-less, so it gives module+offset, not function names — pair with the matching `.pdb`, and grab a **full** dump (`procdump -ma -e <exe>`) when you need the function/locals. Methodology: fingerprint first; always read the paired `.log` for session context; get a second repro — an identical signature across two dumps is strong evidence of a single root cause. One worked example (of many possible): the `mxbikes.exe+0x2a42f0` read-AV was a 64-bit pointer truncated to 32-bit (crashes only when ASLR places the stack above 4 GB), dodged via Exploit Protection → disable High-entropy ASLR for `mxbikes.exe`. Use it as a template for *method*, not as the expected answer.
- `tools/director_report.py` - **Broadcast analysis of the auto-director from a plugin log** (pure stdlib). The director logs one `Director cut: t=… #… shot=… cam=… partner=…` line per cut into the normal `mxbmrp3_log.txt` (release too, via `DEBUG_INFO_F`), so `python3 tools/director_report.py <log>` prints cut count, cut rate, shot-length spread, shot-type + camera mix (share of airtime), and per-rider screen time for any real in-game replay/spectate session. It's the standalone twin of `director_broadcast_test.cpp`'s `report()` (that one runs headless off tapes; this one runs off real logs) — keep the cut-log FORMAT in `director_manager.cpp` `cutTo()` in step with both. `--gap Ns` caps a single shot's counted airtime so a menu pause (no cuts flow while paused) doesn't dominate the shares; `--min/--mid/--max` set the shot-length buckets (default 8/15/25 = the director's min/max shot).

**Multi-Game Layer:**
- `mxbmrp3/game/unified_types.h` - Game-agnostic data structures
- `mxbmrp3/game/game_config.h` - Compile-time game selection
- `mxbmrp3/game/adapters/mxbikes_adapter.h` - MX Bikes type conversion
- `mxbmrp3/game/adapters/gpbikes_adapter.h` - GP Bikes type conversion
- `mxbmrp3/vendor/piboso/mxb_api.cpp` - MX Bikes DLL exports
- `mxbmrp3/vendor/piboso/gpb_api.cpp` - GP Bikes DLL exports
- `mxbmrp3/vendor/piboso/api_guard.h` - `API_GUARD_CATCH` macro that wraps every DLL export

**HUD Base:**
- `mxbmrp3/hud/base_hud.h/.cpp` - Base class for all HUDs

**Example HUDs:**
- `mxbmrp3/hud/ideal_lap_hud.cpp` - Simple HUD (good starting point)
- `mxbmrp3/hud/standings_hud.cpp` - Complex HUD (dynamic table)
- `mxbmrp3/hud/map_hud.cpp` - Advanced (2D rendering, rotation)
- `mxbmrp3/hud/event_log_hud.h/.cpp` - Event log with configurable event type filters
- `mxbmrp3/hud/helmet_overlay_hud.h/.cpp` - Full-screen overlay with telemetry-driven tilt/vibration

**Web Overlay** (`mxbmrp3_data/web/` — organized into subfolders: `js/` overlay scripts,
`fonts/` the `.ttf` web fonts, `icons/` the `.svg` chip/gear icons, `logos/` the slideshow
PNGs. `index.html`, `sw.js`, `style.css`, and `custom.css`/`custom-sample.css` stay at the
**root** — the service worker scope must be `/`, `index.html` is served at `/`, and
`custom.css` has a dedicated no-cache handler. When adding/renaming/moving a served asset,
update its path in `index.html`/`style.css` **and** `PRECACHE_URLS` in `sw.js`, the
installer's per-folder `File` blocks in `packaging/mxbmrp3.nsi` (three game sections), and
`AssetManager::syncUserAssets` if it's a new subfolder):
- `mxbmrp3_data/web/index.html` - Overlay HTML structure
- `mxbmrp3_data/web/style.css` - Overlay theme (CSS variables for full customization)
- `mxbmrp3_data/web/js/overlay-*.js` - SSE client, rendering, focus card logic. The former
  monolithic `app.js` was split into focused, **ordered classic scripts** that share one
  global scope: `overlay-config.js` (CONFIG, settings persistence, constants) →
  `overlay-shell.js` (DOM refs, responsive sizing, logos, tower position) →
  `overlay-connection.js` (SSE) → `overlay-util.js` (formatting/gap math, palette, fonts;
  `isColorDark`) → `overlay-render.js` (`render()`, header, standings tower, event log;
  `EVENT_TYPE_MAP`) → `overlay-focus.js` (focus card) → `overlay-slots.js`
  (`createSlotPanel` framework) → `overlay-panels.js` (lap boards, down-the-order, battle,
  sectors) → `overlay-charts.js` (session-charts math/SVG/carousel; `fmtChartSecs`) →
  `overlay-settings.js` (settings panel UI) → `overlay-demo.js` (`?demo` + init).
  **Invariant — adding/renaming an overlay script:** they share scope with no module
  boundary, so **load order is load-bearing** — update the `<script>` list in `index.html`
  **and** `PRECACHE_URLS` in `sw.js` in the **same** order (a top-level statement can't
  reference a `function`/`var` from a later-loading file at load time; runtime cross-calls
  are fine).

**Settings:**
- `mxbmrp3/hud/settings_hud.cpp` - Settings UI: menu construction (`rebuildRenderData`) + lifecycle
- `mxbmrp3/hud/settings_hud_input.cpp` - Settings UI: click hit-testing/dispatch, per-control handlers, reset ops
- `mxbmrp3/hud/settings/settings_tab_*.cpp` - Individual tab implementations
- `mxbmrp3/hud/settings/settings_layout.cpp` - Layout helper context
- `mxbmrp3/core/settings_manager.cpp` - Persistence: path resolution, serialize/build helpers, save/load orchestration
- `mxbmrp3/core/settings_manager_global.cpp` - Global (non-per-profile) sections: `writeGlobalSettings`/`applyGlobalLine`
- `mxbmrp3/core/settings_hud_profiles.cpp` - Per-profile orchestration: `captureToCache`/`applyProfile` (registry loops) + profile switch/copy/reset
- `mxbmrp3/core/settings_hud_registry.{h,cpp}` - The single ordered per-HUD serializer registry ({section, capture fn, apply fn}) driving capture, apply, and serialize; `settings_hud_registry_decls.inc` holds the member decls `#include`d into `SettingsManager`
- `mxbmrp3/core/settings_keys.h` - INI key-name constants (`Keys`) + INI-only descriptors (`IniOnly`), all in `namespace Settings`
- `mxbmrp3/core/settings_serde.h` - Free serde helpers (enum⇄string, bitmask save/load, base-HUD capture/apply/write, validators), inline in `namespace Settings`

**Testing** (see [`TESTING.md`](TESTING.md) — the canonical guide):
- `tests/unit/` - Layer 1: pure-logic unit tests (doctest)
- `tests/integration/tests/*.cpp` - Layer 2: doctest integration tests (drive real callbacks under Wine)
- `tests/integration/harness/` - `plugin_host.h` (PluginHost: load DLL, drive callbacks, `snapshot()`, `replayTape()`), `plugin_api.h`, `assertions.h`, `tape.h`, `doctest.h`
- `mxbmrp3/core/test_hooks.cpp` - test-only DLL exports (`MXBMRP3_Test_*`), gated on `MXBMRP3_TEST_BUILD`
- `tests/integration/run_tests.sh` / `run_persist_test.sh` / `run_fuzz*.sh` / `run_perf.sh` - runners
- `tests/integration/tests/fixtures/*.tape.gz` - real-data golden-master tapes; `tests/integration/tapes/` - master captures (git-ignored); `tests/integration/slim_tape.py` - derive fixtures
- `mxbmrp3/core/event_recorder.{h,cpp}` - in-plugin callback-tape recorder (MX Bikes only, `GAME_HAS_RECORDER`); `tests/web/` - Playwright overlay tests

**Callback-tape recorder** (in-plugin, replaces the old standalone `mxbmrp3_record.dlo`):
- Lives in `mxbmrp3/core/event_recorder.{h,cpp}`; taps the raw callbacks in `vendor/piboso/mxb_api.cpp`. Ships in the DLL but **dormant** — a developer opts in with the hidden `[Recorder] enabled=1` INI key (no HUD, no hotkey). Writes to `<save>/mxbmrp3/tapes/session_*.tape`. No second process, no console window (that's what caused the shutdown-teardown crash the standalone tool was retired over).

**Dev tools** (all namespaced `mxbmrp3_*`; the first two are projects in `mxbmrp3.sln`):
- `tools/mxbmrp3_replay/` - real-time `.tape` replay / overlay preview (MSVC)
- `tools/mxbmrp3_fontgen/` - portable PiBoSo `.fnt` bitmap-font generator. Builds in VS (`mxbmrp3.sln`) **or** cross-platform via `build.sh`; `test.sh` runs in CI (unit-tests job); `regen_shipped.sh` reproduces the shipped `mxbmrp3_data/fonts/*.fnt`. See its README for the config format + normalization.
- `tools/mxbmrp3_hud_window/` - companion-window demo/screenshot harness (headless Wine + Xvfb)

### Regenerating a shipped font
The shipped bitmap fonts are generated from the source `.ttf` (in `mxbmrp3_data/web/fonts/`) with `mxbmrp3_fontgen`, normalized so every font renders numbers at a consistent size/width/position (`normalize = 1`: cell 135, digit-advance 0.489, centered). The cell height is the atlas *resolution*, not the on-screen size (the renderer scales by `size × screenH / cellH`), so the 135px cell keeps text crisp when a HUD draws it larger than the cell (high-DPI, or scaled-up widgets like the speedo); the atlas auto-grows to 2048² to hold it. `RobotoMono-Regular.fnt` is the reference, regenerated at 135px via `test.sh`'s cfg. To rebuild them all: `tools/mxbmrp3_fontgen/regen_shipped.sh`. To add/replace one: drop a `.ttf` in `mxbmrp3_data/web/fonts/`, run `tools/mxbmrp3_fontgen/mxbmrp3_fontgen <font>.ttf mxbmrp3_data/fonts/<font>.fnt`, commit the `.fnt`.

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
- **Never hand-create or hand-edit git tags.** `mxbmrp3/resource.h` is the **single source of truth**: edit only `VER_MAJOR/MINOR/PATCH` there for a release. The `release` workflow *auto-creates* the `vX.Y.Z` tag from `resource.h` when you publish (there is no tag-push trigger, so the tag can never drift from the version) — see DEVELOPMENT.md → Releases. `VER_STRING` is composed from the macros, and `PluginConstants::PLUGIN_VERSION` is defined **from `VER_STRING` in `core/plugin_version.cpp`** (the *only* TU that includes `resource.h`) and declared as an `extern` in `plugin_constants.h` — so the runtime string and the DLL FILEVERSION can't drift, and there's nothing to keep in sync. **Don't move `#include "../resource.h"` back into `plugin_constants.h`** (or make `PLUGIN_VERSION` a `constexpr` there): `resource.h` pulls in the per-build `version_build.g.h`, so putting it in that universally-included header makes the automatic build-number bump recompile **every** TU on each commit. Keeping it in one `.cpp` means a bump recompiles only `plugin_version.cpp` + the `.rc`.
- **The 4th component (`VER_BUILD`) is stamped automatically at build time** from the git commit count (`git rev-list --count HEAD`) by the `StampVersion` pre-build target in `mxbmrp3.vcxproj`, which writes `mxbmrp3/version_build.g.h` (git-ignored, `#define VER_BUILD_AUTO`). **Don't hand-edit the 4th component** — it's monotonic across the repo (no per-patch reset) and climbs by 1 per commit. A git failure falls back to `0` so the build never breaks. `resource.h` guards the `#include` (`__INTELLISENSE__` / `__has_include`) so a fresh checkout doesn't hard-error before the first build; real cl/rc builds always include the stamped file (so the DLL FILEVERSION is never the `0` fallback).

### Peer Reviews
- **Update `main` first:** Before peer reviewing a branch, fetch and bring local `main` up to date with `origin/main`. Diff and review the branch against the current `main` so feedback reflects the latest base, not a stale one.

### Development Style
- **Iterative refinement:** Expect many small commits for UI tweaks, alignment fixes, etc.
- **Quick iterations:** Debug strings added/removed, grid alignment tweaks, constant adjustments
