# AI Development Context for MXBMRP3

## Read This First

This is a **racing simulator HUD plugin** for MX Bikes (motocross game). It's a DLL plugin written in C++ using the game's proprietary API.

**For deep technical details:** See [`ARCHITECTURE.md`](ARCHITECTURE.md) (comprehensive documentation with mermaid diagrams, component descriptions, dependency graphs). This file is a quick-start guide.

## Quick Architecture

```
MX Bikes Game Engine
    ↓ (callbacks via plugin API)
plugin_manager.cpp (entry point)
    ↓
PluginData (singleton - caches all game state)
    ↓ (notifies on data changes)
HudManager (singleton - owns all HUD instances)
    ↓
Individual HUDs (SessionBest, Standings, Map, etc.)
    ↓ (build render primitives)
Game Engine (renders quads/strings)
```

**Key Singletons:**
- `PluginData` - Central game state cache, change detection
- `HudManager` - HUD lifecycle, owns all HUD instances
- `SettingsManager` - Save/load HUD configurations
- `InputManager` - Mouse, keyboard, XInput controller

## Build & Test

**⚠️ IMPORTANT - Build Environment:**
- This is a **Windows-only** Visual Studio project
- **DO NOT attempt to build in Linux/WSL environments** - it will fail
- Claude Code often runs in Linux - you cannot build this project there
- Instead: read code, make edits, commit changes, and let Windows users build

**Build Instructions (Windows only):**
- **Build**: Open `mxbmrp3.sln` in Visual Studio 2022 (C++17, v143 toolset)
- **Platform**: x64 only (MX Bikes is 64-bit only)
- **Output**: Builds to `build/Release/mxbmrp3.dlo` (not .dll - MX Bikes uses .dlo extension)
- **Deploy**: Copy `mxbmrp3.dlo` to `MX Bikes/plugins/` folder
- **Debug**: Use Debug build configuration (enables DEBUG_INFO macros automatically)

## Important Patterns & Constraints

### DO:
- Use RAII (smart pointers, no raw `new`/`delete`)
- Use safe string functions (`strncpy_s`, `snprintf`)
- Add exception handling for file I/O
- Use `DEBUG_INFO_F()` for logging (not `printf`)
- Check for existing patterns before adding new code

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

**Widget vs HUD Distinction**
Widgets (TimeWidget, PositionWidget, LapWidget, SessionWidget, SpeedWidget, SpeedoWidget, TachoWidget, BarsWidget, TimingWidget, NoticesWidget, VersionWidget) are simplified HUD components with:
- Single-purpose display (no configurable columns/rows)
- Minimal settings (just position, scale, opacity)
- Simpler rendering logic

Full HUDs (StandingsHud, LapLogHud, PitboardHud, etc.) have:
- Complex data visualization
- Extensive customization (column/row toggles, gap modes, etc.)
- More configuration options

**Handler-to-API Event Mapping**
Each handler corresponds to specific MX Bikes API callback(s):
- Run handlers (RunHandler, RunLapHandler, etc.) = player-only events
- Race handlers (RaceEventHandler, RaceLapHandler, etc.) = multiplayer/all riders
- See ARCHITECTURE.md for full mapping

**No unit tests**
Requires game engine to run. Manual testing in-game is current workflow.

## Common Tasks

### Adding a New HUD
1. Create class inheriting from `BaseHud`
2. Implement `rebuildRenderData()` - builds vectors of quads/strings
3. Register in `HudManager` constructor
4. Add tab in `SettingsHud` for configuration
5. Add save/load in `SettingsManager`

### Debugging Rendering Issues
- Check if HUD is visible: `hud->isVisible()`
- Check if data is dirty: `hud->isDataDirty()` triggers rebuild
- Enable debug logging: Set breakpoint in `rebuildRenderData()`
- Verify coordinates: Game uses normalized coords (0.0-1.0)

### Working with Game API Events
When implementing event handlers or debugging timing/lap data:
- **Check the API header**: `mxbmrp3/vendor/piboso/mxb_api.h`
- **Use cases:**
  - Understanding field indexing (0-based vs 1-based) - e.g., lap numbers, split indices
  - Clarifying field meanings in event structs (`SPluginsRaceLap_t`, `SPluginsRaceSplit_t`, etc.)
  - Determining data types, value ranges, and validation requirements
- **Example:** When displaying lap numbers, the API uses 0-based indexing internally (`m_iLapNum=0` for first lap) but UI typically shows 1-based (display as "L1")
- **Tip:** Many timing/position issues come from misunderstanding the API contract - always verify assumptions against the header

## Recent Refactors (2024)

- **Code duplication elimination** - Extracted helper methods to eliminate ~210 lines of duplication:
  - `addStatsRow()` in performance_hud.cpp (consolidates FPS/CPU stats rendering)
  - `addDisplayModeControl()` in settings_hud.cpp (consolidates display mode UI)
  - `addClickRegion()` in settings_hud.cpp (reduces ClickRegion boilerplate)
  - Added layout constants (LABEL_WIDTH, SMALL_GAP, etc.) to replace magic numbers
- **Maintainability quick wins** - Added reusable patterns to eliminate boilerplate:
  - `HANDLER_NULL_CHECK()` macro in handler_singleton.h (replaces 14 defensive checks)
  - `PluginUtils::applyOpacity()` helper (eliminates RGB extraction boilerplate)
  - `BaseHud::positionString()` helper (eliminates ~60 lines of widget string positioning)
  - Graph scaling constants (MAX_FPS_DISPLAY, GRID_LINE_*_PERCENT) for tunability
- **Magic number extraction** - Improved code clarity with named constants:
  - Penalty rounding constants (MS_TO_SEC_DIVISOR, MS_TO_SEC_ROUNDING_OFFSET)
  - Racing blue color constants (BLUE_FLAG_COLOR_R/G/B)
  - Map memory reservation constants (RESERVE_TRACK_SEGMENTS, etc.)
  - FPS calculation constants (MIN_FPS_CLAMP, MAX_FPS_CLAMP, DEFAULT_FRAME_BUDGET_MS)
- **Comment accuracy audit** - Fixed incorrect parameter count comment
- **Dead code removal** - Removed 4 unused functions
- **Memory safety** - Fixed dangling pointer bug in `HudManager::clear()`
- **Exception handling** - Added try-catch for settings file parsing
- **DRY refactor** - Created `saveBaseHudProperties()` helper (eliminated 93 lines)
- **Click handler refactor** - Extracted 12 handlers from 233-line switch statement

## Files You'll Likely Need

**Core:**
- `mxbmrp3/core/plugin_manager.cpp` - Plugin entry point
- `mxbmrp3/core/plugin_data.h/.cpp` - Game state cache
- `mxbmrp3/core/hud_manager.h/.cpp` - HUD ownership

**HUD Base:**
- `mxbmrp3/hud/base_hud.h/.cpp` - Base class for all HUDs

**Example HUDs:**
- `mxbmrp3/hud/session_best_hud.cpp` - Simple HUD (good starting point)
- `mxbmrp3/hud/standings_hud.cpp` - Complex HUD (dynamic table)
- `mxbmrp3/hud/map_hud.cpp` - Advanced (2D rendering, rotation)

**Settings:**
- `mxbmrp3/hud/settings_hud.cpp` - Settings UI (longest file, 1300+ lines)
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
- **No git tags** - Version is hardcoded in `mxbmrp3/core/plugin_constants.h`
- Update `PLUGIN_VERSION` constant when releasing
- Current version: 1.5.1.0

### Development Style
- **Iterative refinement:** Expect many small commits for UI tweaks, alignment fixes, etc.
- **PR workflow:** Heavy use of pull requests (36+ merged PRs)
- **AI-developed:** 95% of commits by Claude (695+ commits out of 730+)
- **Quick iterations:** Debug strings added/removed, grid alignment tweaks, constant adjustments

---

**Last Updated:** November 2024 (documentation accuracy review)
