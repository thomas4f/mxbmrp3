# AI Development Context for MXBMRP3

## Read This First

This is a **racing simulator HUD plugin** for Piboso racing games (MX Bikes, GP Bikes, WRS, KRP). It's a DLL plugin written in C++ using each game's proprietary API, with a shared core that works across all supported games.

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
```

**Key Singletons:**
- `PluginData` - Central game state cache, change detection
- `HudManager` - HUD lifecycle, owns all HUD instances
- `SettingsManager` - Save/load HUD configurations
- `InputManager` - Mouse and keyboard input
- `XInputReader` - Controller state and rumble effects
- `RumbleProfileManager` - Per-bike rumble profiles stored in JSON
- `OdometerManager` - Per-bike odometer/trip meter data stored in JSON
- `FmxManager` - FMX trick detection state machine, scoring, chain system
- `AssetManager` - Dynamic discovery of fonts, textures, icons from subdirectories
- `FontConfig` - User-configurable font categories (Title, Normal, Strong, Marker, Small)
- `ColorConfig` - User-configurable color palette

## Multi-Game Support

The plugin supports multiple Piboso games from a single codebase:

| Game | Config | Output | Status |
|------|--------|--------|--------|
| MX Bikes | `MXB-Release` | `mxbmrp3.dlo` | ✅ Full support |
| GP Bikes | `GPB-Release` | `mxbmrp3_gpb.dlo` | ✅ Core features |
| WRS | - | `wrsmrp3.dlo` | ⏳ Stubbed |
| KRP | - | `krpmrp3.dlo` | ⏳ Stubbed |

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
- **Platform**: x64 only (all Piboso games are 64-bit)
- **Configurations**:
  - `MXB-Debug` / `MXB-Release` → `build/MXB-Release/mxbmrp3.dlo`
  - `GPB-Debug` / `GPB-Release` → `build/GPB-Release/mxbmrp3_gpb.dlo`
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
Widgets (TimeWidget, PositionWidget, LapWidget, SpeedWidget, SpeedoWidget, TachoWidget, BarsWidget, LeanWidget, NoticesWidget, VersionWidget, SettingsButtonWidget) are simplified HUD components with:
- Single-purpose display (no configurable columns/rows)
- Minimal settings (just position, scale, opacity)
- Simpler rendering logic

Full HUDs (StandingsHud, LapLogHud, PitboardHud, TimingHud, etc.) have:
- Complex data visualization
- Extensive customization (column/row toggles, gap modes, etc.)
- More configuration options

**Handler-to-API Event Mapping**
Each handler corresponds to game API callback(s), but receives unified types:
- Run handlers (RunHandler, RunLapHandler, etc.) = player-only events
- Race handlers (RaceEventHandler, RaceLapHandler, etc.) = multiplayer/all riders
- See ARCHITECTURE.md for full mapping

**No unit tests**
Requires game engine to run. Manual testing in-game is current workflow.

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
6. Add save/load in `SettingsManager`

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

### Adding Support for a New Game Feature
1. Add field to appropriate `Unified::` struct in `game/unified_types.h`
2. Add conversion in each adapter (`game/adapters/*_adapter.h`)
3. Add feature flag to `game/game_config.h` if game-specific
4. Update handlers/HUDs to use the new field

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
- `mxbmrp3/core/odometer_manager.h/.cpp` - Per-bike odometer and trip meter tracking
- `mxbmrp3/core/fmx_manager.h/.cpp` - FMX trick detection and scoring
- `mxbmrp3/core/fmx_types.h` - FMX data structures (TrickType, TrickInstance, RotationTracker, etc.)

**Multi-Game Layer:**
- `mxbmrp3/game/unified_types.h` - Game-agnostic data structures
- `mxbmrp3/game/game_config.h` - Compile-time game selection
- `mxbmrp3/game/adapters/mxbikes_adapter.h` - MX Bikes type conversion
- `mxbmrp3/game/adapters/gpbikes_adapter.h` - GP Bikes type conversion
- `mxbmrp3/vendor/piboso/mxb_api.cpp` - MX Bikes DLL exports
- `mxbmrp3/vendor/piboso/gpb_api.cpp` - GP Bikes DLL exports

**HUD Base:**
- `mxbmrp3/hud/base_hud.h/.cpp` - Base class for all HUDs

**Example HUDs:**
- `mxbmrp3/hud/ideal_lap_hud.cpp` - Simple HUD (good starting point)
- `mxbmrp3/hud/standings_hud.cpp` - Complex HUD (dynamic table)
- `mxbmrp3/hud/map_hud.cpp` - Advanced (2D rendering, rotation)

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
- **No git tags** - Version is hardcoded in `mxbmrp3/core/plugin_constants.h`
- Update `PLUGIN_VERSION` constant when releasing

### Development Style
- **Iterative refinement:** Expect many small commits for UI tweaks, alignment fixes, etc.
- **Quick iterations:** Debug strings added/removed, grid alignment tweaks, constant adjustments
