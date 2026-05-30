# AI Development Context for MXBMRP3

## Read This First

This is a **racing simulator HUD plugin** for PiBoSo racing games (MX Bikes, GP Bikes, WRS, KRP). It's a DLL plugin written in C++ using each game's proprietary API, with a shared core that works across all supported games.

**For deep technical details:** See [`ARCHITECTURE.md`](ARCHITECTURE.md) (comprehensive documentation with mermaid diagrams, component descriptions, dependency graphs, multi-game architecture). This file is a quick-start guide.

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

**⚠️ IMPORTANT - Build Environment:**
- This is a **Windows-only** Visual Studio project
- **DO NOT attempt to build in Linux/WSL environments** - it will fail
- Claude Code often runs in Linux - you cannot build this project there
- Instead: read code, make edits, commit changes, and let Windows users build

**⚠️ IMPORTANT - Shell Commands:**
- The user runs on **Windows**, not Linux
- When providing shell commands for the user to run, use Windows syntax:
  - Use `&` instead of `&&` for chaining commands (or provide separate commands)
  - Use backslashes `\` for paths, or forward slashes `/` (git accepts both)
  - Example: `git fetch origin & git reset --hard origin/branch-name`

**Build Instructions (Windows only):**
- **Build**: Open `mxbmrp3.sln` in Visual Studio 2022 (C++17, v143 toolset)
- **Platform**: x64 only (all PiBoSo games are 64-bit)
- **Configurations**:
  - `All-Release` / `All-Debug` → builds MXB + GPB + KRP sequentially via the `build_all` meta-project (default in the dropdown)
  - `MXB-Debug` / `MXB-Release` → `build/MXB-Release/mxbmrp3.dlo`
  - `GPB-Debug` / `GPB-Release` → `build/GPB-Release/mxbmrp3_gpb.dlo`
  - `KRP-Debug` / `KRP-Release` → `build/KRP-Release/mxbmrp3_krp.dlo`
- **Deploy**: Copy `.dlo` to game's `plugins/` folder
- **Debug**: Use Debug configuration (enables DEBUG_INFO macros automatically)

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

### DON'T:
- Throw exceptions in core code (game engine doesn't support them)
- Use raw pointers for ownership
- Add features without understanding the data flow
- Bypass PluginData's change detection (use appropriate setter methods)

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
Widgets (TimeWidget, PositionWidget, LapWidget, SpeedWidget, GearWidget, ClockWidget, SpeedoWidget, TachoWidget, BarsWidget, FuelWidget, LeanWidget, GForceWidget, TyreTempWidget, EcuWidget, GamepadWidget, VersionWidget, SettingsButtonWidget) are simplified HUD components with:
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

**Handler-to-API Event Mapping**
Each handler corresponds to game API callback(s), but receives unified types:
- Run handlers (RunHandler, RunLapHandler, etc.) = player-only events
- Race handlers (RaceEventHandler, RaceLapHandler, etc.) = multiplayer/all riders
- See ARCHITECTURE.md for full mapping

**No unit tests**
Requires game engine to run. Manual testing in-game is current workflow.

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
6. Add save/load in `SettingsManager` (per-HUD capture/apply, or — for a global single-value setting — one line in `writeGlobalSettings()` and one branch in `applyGlobalLine()`). Reset is then automatic via the factory snapshots; no separate reset code needed.

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
- **Data contract**: `session` (time, type, palette, fonts), `standings[]` (per-rider with all chips), `events[]` (all events, unfiltered)
- **Plugin sends raw data** — event/chip filtering, timestamps, and display settings are controlled client-side via the `CONFIG` block in `app.js`
- **Web files** served from `plugins/mxbmrp3_data/web/` — users can customize CSS/HTML/JS freely (user overrides synced from Documents folder)
- **Lightweight style overrides**: users who only want to tweak the theme can create `Documents\PiBoSo\[Game]\mxbmrp3\web\custom.css` (synced into the served folder on game start) instead of forking `style.css`. `index.html` already links `custom.css`; the plugin doesn't ship a stub, so the link 404s harmlessly when no override exists. It's loaded after `style.css`, served with `Cache-Control: no-cache`, and excluded from the SW precache + fetch handler so edits show up on the next browser reload.
- **To add a new field**: add to `buildJsonSnapshot()` in the appropriate section, then consume in `app.js`
- **Logo slideshow**: `GET /api/logos` scans `web/logos/` for PNGs and returns a sorted filename list. Users drop PNGs into the `logos/` folder (or the Documents user-override path `mxbmrp3/web/logos/`), no config editing needed. Bundled logos should be added to `PRECACHE_URLS` in `sw.js`.
- **Service worker / offline cache**: `mxbmrp3_data/web/sw.js` precaches the overlay shell so OBS can render it before the plugin's HTTP server is up. The `PRECACHE_URLS` list is hand-maintained — when adding new CSS/JS/font/icon assets under `mxbmrp3_data/web/`, also add them to `PRECACHE_URLS` in `sw.js`, otherwise they won't be available offline until first online load. Cache name is tied to `PLUGIN_VERSION` (substituted server-side in `http_server.cpp`), so plugin upgrades auto-invalidate the cache.

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
7. **Gate the installer (`mxbmrp3.nsi`)** if the feature has supporting data files (e.g. `web/` for HTTP server) so they don't ship to a build that can't use them.

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
- `mxbmrp3/core/event_log_types.h` - Event log entry types and filter flags
- `mxbmrp3/core/crash_handler.h/.cpp` - SEH minidump filter; writes `.dmp` to `<savePath>\mxbmrp3\crashes\` on unhandled hardware faults
- `tools/mdmp_analyze.py` - Standalone minidump triage (no Windows debugger needed): prints the exception code/faulting address, maps the faulting RIP to its module, and does a heuristic live-stack scan to attribute a crash to the plugin vs the game vs a GPU/system DLL. Run `python3 tools/mdmp_analyze.py <file.dmp>`. Symbol-less, so it gives module+offset, not function names — pair with the matching `.pdb` for deeper analysis.

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

**Web Overlay:**
- `mxbmrp3_data/web/index.html` - Overlay HTML structure
- `mxbmrp3_data/web/style.css` - Overlay theme (CSS variables for full customization)
- `mxbmrp3_data/web/app.js` - SSE client, rendering, focus card logic

**Settings:**
- `mxbmrp3/hud/settings_hud.cpp` - Main settings UI class
- `mxbmrp3/hud/settings/settings_tab_*.cpp` - Individual tab implementations
- `mxbmrp3/hud/settings/settings_layout.cpp` - Layout helper context
- `mxbmrp3/core/settings_manager.cpp` - Persistence layer

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
- **No git tags** - Version is hardcoded in two places that must stay in sync:
  - `mxbmrp3/core/plugin_constants.h` (the `PLUGIN_VERSION` string used at runtime)
  - `mxbmrp3/resource.h` (the `VER_MAJOR/MINOR/PATCH/BUILD` macros and `VER_STRING` consumed by the Windows DLL version info via `mxbmrp3.rc`)
- Update both when releasing

### Peer Reviews
- **Update `main` first:** Before peer reviewing a branch, fetch and bring local `main` up to date with `origin/main`. Diff and review the branch against the current `main` so feedback reflects the latest base, not a stale one.

### Development Style
- **Iterative refinement:** Expect many small commits for UI tweaks, alignment fixes, etc.
- **Quick iterations:** Debug strings added/removed, grid alignment tweaks, constant adjustments
