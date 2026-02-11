# MXBMRP3 Architecture Guide

This document explains how the MXBMRP3 plugin works, from the ground up. It's designed to help new contributors understand the codebase quickly.

## What Is This Project?

MXBMRP3 is a **HUD (Heads-Up Display) plugin** for Piboso racing simulators (MX Bikes, GP Bikes, WRS, KRP). The plugin displays real-time racing information on screen: lap times, standings, speedometer, track map, and more.

The plugin is a Windows DLL (with `.dlo` extension) that each game loads at startup. The game calls our exported functions to send us data and request rendering instructions. A **multi-game translation layer** allows the same core code to work across all supported games.

## Project Structure

```
mxbmrp3/
├── mxbmrp3/                    # Main plugin source code
│   ├── vendor/piboso/          # Game API definitions and exports
│   │   ├── mxb_api.h/.cpp      # MX Bikes API header and DLL exports
│   │   ├── gpb_api.h/.cpp      # GP Bikes API header and DLL exports
│   │   ├── wrs_api.h           # WRS API header (stubbed)
│   │   └── krp_api.h           # KRP API header (stubbed)
│   ├── game/                   # Multi-game abstraction layer
│   │   ├── unified_types.h     # Game-agnostic data structures
│   │   ├── game_config.h       # Compile-time game selection
│   │   └── adapters/           # Per-game type converters
│   │       ├── mxbikes_adapter.h
│   │       ├── gpbikes_adapter.h
│   │       └── ...
│   ├── core/                   # Core infrastructure
│   │   ├── plugin_manager.*    # Main coordinator, routes API callbacks
│   │   ├── plugin_data.*       # Central game state cache
│   │   ├── hud_manager.*       # Owns and updates all HUDs
│   │   ├── input_manager.*     # Keyboard and mouse input
│   │   ├── xinput_reader.*     # XInput controller state and rumble
│   │   ├── rumble_profile_manager.* # Per-bike rumble profiles (JSON)
│   │   ├── settings_manager.*  # Save/load configuration (INI file)
│   │   ├── asset_manager.*     # Dynamic asset discovery (fonts, textures, icons)
│   │   ├── font_config.*       # User-configurable font categories
│   │   ├── color_config.*      # User-configurable color palette
│   │   ├── fmx_manager.*       # FMX trick detection and scoring
│   │   ├── fmx_types.h         # FMX data structures and enums
│   │   ├── plugin_constants.h  # All named constants
│   │   └── plugin_utils.*      # Shared helper functions
│   ├── handlers/               # Event processors (one per API callback type)
│   │   ├── draw_handler.*      # Frame rendering and FPS tracking
│   │   ├── event_handler.*     # Event lifecycle (init/deinit)
│   │   ├── run_*_handler.*     # Player-only events
│   │   └── race_*_handler.*    # Multiplayer race events
│   ├── hud/                    # Display components
│   │   ├── base_hud.*          # Abstract base class for all HUDs
│   │   ├── *_hud.*             # Full HUDs (complex, configurable)
│   │   ├── *_widget.*          # Simple widgets (focused display)
│   │   └── settings/           # Settings UI components
│   │       ├── settings_hud.*      # Main settings menu
│   │       ├── settings_layout.*   # Layout helper context
│   │       └── settings_tab_*.cpp  # Individual tab renderers
│   └── diagnostics/            # Debugging tools
│       ├── logger.*            # Debug logging to file
│       └── timer.h             # Performance measurement
├── mxbmrp3_data/               # Runtime assets (discovered dynamically)
│   ├── fonts/                  # .fnt files (bitmap fonts)
│   ├── textures/               # .tga files (HUD backgrounds with variants)
│   ├── icons/                  # .tga files (rider icons for map/radar)
│   └── tooltips.json           # UI tooltip definitions
├── docs/                       # Documentation
├── replay_tool/                # Separate tool for replay analysis
└── mxbmrp3.sln                 # Visual Studio solution
```

## The Big Picture

Here's how data flows through the plugin:

```
┌─────────────────────────────────────────────────────────────────────────┐
│                    GAME ENGINE (MX Bikes / GP Bikes / etc.)             │
└─────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
                    ┌───────────────────────────────┐
                    │   mxb_api.cpp / gpb_api.cpp   │
                    │   (Per-Game DLL Exports)      │
                    │                               │
                    │  Startup(), Draw(), RunLap(), │
                    │  RaceEvent(), etc.            │
                    └───────────────────────────────┘
                                    │
                                    ▼
                    ┌───────────────────────────────┐
                    │      Game Adapters            │
                    │   (mxbikes_adapter.h, etc.)   │
                    │                               │
                    │  Convert game structs to      │
                    │  Unified:: types              │
                    └───────────────────────────────┘
                                    │
                                    ▼
                    ┌───────────────────────────────┐
                    │      PluginManager            │
                    │   (Main Coordinator)          │
                    │                               │
                    │  Receives Unified:: types,    │
                    │  routes to handlers           │
                    └───────────────────────────────┘
                                    │
              ┌─────────────────────┼─────────────────────┐
              ▼                     ▼                     ▼
     ┌─────────────────┐   ┌─────────────────┐   ┌─────────────────┐
     │    Handlers     │   │   DrawHandler   │   │  InputManager   │
     │                 │   │                 │   │                 │
     │ Process events, │   │ Triggers HUD    │   │ Tracks mouse,   │
     │ update data     │   │ render cycle    │   │ keyboard state  │
     └─────────────────┘   └─────────────────┘   └─────────────────┘
              │                     │
              ▼                     │
     ┌─────────────────┐            │
     │   PluginData    │◄───────────┘
     │  (State Cache)  │
     │                 │
     │ Stores all game │
     │ state, notifies │
     │ on changes      │
     └─────────────────┘
              │
              │ notifies
              ▼
     ┌─────────────────┐
     │   HudManager    │
     │                 │
     │ Owns all HUDs,  │
     │ marks dirty,    │
     │ collects output │
     └─────────────────┘
              │
              ▼
     ┌─────────────────┐
     │      HUDs       │
     │                 │
     │ Build quads &   │
     │ strings for     │
     │ rendering       │
     └─────────────────┘
              │
              │ returns render data
              ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                    GAME ENGINE (MX Bikes / GP Bikes / etc.)             │
│                          (Renders our output)                           │
└─────────────────────────────────────────────────────────────────────────┘
```

## Core Components

### 1. The Plugin API (`vendor/piboso/*_api.*`)

Each Piboso game defines a C API that plugins must implement. The APIs are nearly identical, with game-specific struct variations. Each game has its own API file:
- `mxb_api.h/.cpp` - MX Bikes
- `gpb_api.h/.cpp` - GP Bikes
- `wrs_api.h` / `krp_api.h` - WRS and KRP (headers only, stubs)

Key exported functions (same across all games):

| Function | When Called | Purpose |
|----------|-------------|---------|
| `Startup()` | Game starts | Initialize plugin, return telemetry rate |
| `Shutdown()` | Game closes | Clean up resources |
| `EventInit()` | Track loaded | Receive track/vehicle info |
| `RunInit()` | Player goes on track | Session begins |
| `RunTelemetry()` | Every physics tick | Receive vehicle telemetry (100Hz) |
| `RunLap()` | Lap completed | Receive lap time |
| `Draw()` | Every frame | Return quads/strings to render |
| `RaceEvent()` | Online race starts | Receive race info |
| `RaceClassification()` | Continuously | Receive standings updates |

The API uses C structs to pass data. Each game's structs have different field names and contents:
- MX Bikes: `SPluginsBikeData_t`, `SPluginsBikeEvent_t`
- GP Bikes: `SPluginsGPBBikeData_t`, `SPluginsGPBBikeEvent_t`

**The adapter layer** (`game/adapters/*.h`) converts these game-specific structs to unified types (`Unified::TelemetryData`, `Unified::VehicleEventData`, etc.) that the core plugin uses.

### 2. PluginManager (`core/plugin_manager.*`)

The central coordinator. It:
- Receives **unified types** from the per-game API files (after adapter conversion)
- Initializes core systems on startup
- Routes each callback to the appropriate handler
- Measures callback execution time for performance tracking

Note: PluginManager is **game-agnostic** - it never sees raw game API structs, only `Unified::*` types.

```cpp
// Example: mxb_api.cpp converts and forwards to PluginManager:
// In mxb_api.cpp:
void RunLap(void* _pData, int _iDataSize) {
    auto* gameData = static_cast<SPluginsBikeLap_t*>(_pData);
    auto unified = Adapter::toPlayerLap(gameData);  // Convert to unified type
    PluginManager::getInstance().handleRunLap(&unified);
}

// PluginManager receives unified type:
void PluginManager::handleRunLap(Unified::PlayerLapData* psLapData) {
    RunLapHandler::getInstance().handleRunLap(psLapData);
}
```

### 3. PluginData (`core/plugin_data.*`)

The **single source of truth** for all game state. This singleton:
- Caches all data received from the game (session info, standings, telemetry)
- Provides typed getters for HUDs to read from
- Detects changes and notifies HudManager when data updates
- Stores per-rider data (lap times, track positions, session bests)

Key data structures:
- `SessionData` - Track name, session type, weather, etc.
- `RaceEntryData` - Rider name, bike, race number
- `StandingsData` - Position, gap, best lap for each rider
- `BikeTelemetryData` - Speed, RPM, gear, fuel
- `IdealLapData` - Best sector/lap times per rider

```cpp
// Example: Handler stores data, HUD reads it
// In handler:
PluginData::getInstance().updateSpeedometer(speed, gear, rpm, fuel);

// In HUD:
const BikeTelemetryData& data = PluginData::getInstance().getBikeTelemetry();
int speedMph = data.speedometer * MS_TO_MPH;
```

### 4. HudManager (`core/hud_manager.*`)

Owns and orchestrates all HUD instances. It:
- Creates and registers all HUDs at startup
- Loads saved settings from disk
- Receives data change notifications from PluginData
- Marks relevant HUDs as "dirty" when data changes
- Calls `update()` on each HUD every frame
- Collects render output (quads/strings) from all visible HUDs
- Handles keyboard shortcuts (F1-F9 toggle HUDs)

### 5. Handlers (`handlers/*`)

Each handler processes a specific category of game events. They're all singletons.

**Run Handlers** (player-only, single-player or your own bike):
- `EventHandler` - Track loaded/unloaded
- `RunHandler` - Session start/stop
- `RunLapHandler` - Player crossed finish line
- `RunSplitHandler` - Player crossed split timing point
- `RunTelemetryHandler` - Physics tick (100Hz telemetry)

**Race Handlers** (all riders in online races):
- `RaceEventHandler` - Online race initialized
- `RaceEntryHandler` - Rider joined/left
- `RaceSessionHandler` - Session state changes
- `RaceLapHandler` - Any rider completed a lap
- `RaceSplitHandler` - Any rider crossed split timing point
- `RaceClassificationHandler` - Standings update
- `RaceTrackPositionHandler` - Real-time positions of all riders
- `RaceCommunicationHandler` - Penalties, warnings, state changes
- `RaceVehicleDataHandler` - Telemetry for all riders (during replays)

**Other Handlers**:
- `DrawHandler` - Frame rendering, FPS calculation
- `TrackCenterlineHandler` - Track geometry for map display
- `SpectateHandler` - Camera/vehicle selection in spectator mode

### 6. ProfileManager (`core/profile_manager.*`)

Manages HUD layout profiles for different game contexts:
- **PRACTICE** - Used during practice and warmup sessions
- **QUALIFY** - Used during pre-qualify, qualify practice, and qualify sessions
- **RACE** - Used during Race 1, Race 2, Straight Rhythm sessions
- **SPECTATE** - Used when spectating or viewing replays

Features:
- Auto-switch between profiles based on game state (optional)
- Each profile stores complete HUD layout configuration
- Manual profile selection via settings menu
- Seamless transitions when session type changes

### 7. RumbleProfileManager (`core/rumble_profile_manager.*`)

Manages per-bike rumble profiles stored in a JSON file:
- Allows different rumble effect settings for different bikes
- Each profile stores complete `RumbleConfig` (effect strengths, input ranges)
- Automatically loads/saves from `{save_path}/mxbmrp3/rumble_profiles.json`

Features:
- Toggle between global settings (INI) and per-bike profiles (JSON)
- Profiles keyed by bike name string
- Auto-creates profile for current bike when enabled
- Integrates with XInputReader for runtime config access

### 8. OdometerManager (`core/odometer_manager.*`)

Manages per-bike odometer data stored in a JSON file:
- Tracks total distance traveled per bike (persistent across sessions)
- Tracks session trip distance (resets when session ends)
- Automatically loads/saves from `{save_path}/mxbmrp3/odometer.json`

Features:
- Per-bike odometer keyed by bike name string
- Uses `double` precision to maintain accuracy at high distances (100k+ km)
- Thread-safe with mutex protection
- Distance calculated from speed × delta time in telemetry handler
- Displayed in SpeedoWidget with odometer/tripmeter rows

### 9. FmxManager (`core/fmx_manager.*`)

Manages FMX (Freestyle Motocross) trick detection and scoring:
- State machine: `IDLE → ACTIVE → GRACE → CHAIN → COMPLETED/FAILED`
- Dynamic trick classification (re-evaluates type every frame using peaks to prevent downgrades)
- Committed L/R direction tracking (prevents flip-flopping between direction variants)
- Chain system with variety-based multiplier (unique tricks add full bonus, repeats diminished)
- Anti-exploit measures: teleport detection, stuck detection, ground trick debounce
- Pause compensation (shifts steady_clock time points on resume)

**Data types** are defined in `fmx_types.h`:
- `TrickType` enum (27 trick types across ground, air, and combination categories)
- `TrickInstance` - Active or completed trick with rotation, timing, and scoring data
- `RotationTracker` - Angular velocity integration for reliable rotation accumulation
- `GroundContactState` - Wheel contact, speed, and slip detection
- `FmxConfig` - Adjustable detection/scoring thresholds

**Display settings** are split between global and per-profile:
- Global (on FmxManager): enabled rows, chain display rows, debug logging
- Per-profile (on FmxHud): position, visibility, scale, opacity

## The HUD System

### BaseHud (`hud/base_hud.*`)

Abstract base class that all HUDs inherit from. Provides:

**Rendering Infrastructure**:
- `m_quads` - Vector of rectangles to draw (backgrounds, indicators)
- `m_strings` - Vector of text strings to display
- Helper methods: `addString()`, `addBackgroundQuad()`, `addLineSegment()`

**Dirty Flag System** (for performance):
- `m_bDataDirty` - True when underlying data changed, needs full rebuild
- `m_bLayoutDirty` - True when position changed, needs position update only
- `rebuildRenderData()` - Expensive: regenerate all quads/strings
- `rebuildLayout()` - Cheap: just update positions

**Positioning & Scaling**:
- `m_fOffsetX`, `m_fOffsetY` - Position offset (draggable)
- `m_fScale` - Size multiplier
- `validatePosition()` - Keep HUD within screen bounds
- Coordinates are normalized: (0,0) = top-left, (1,1) = bottom-right

**Visibility & Interaction**:
- `m_bVisible` - Show/hide toggle
- `m_bDraggable` - Can user drag this HUD?
- `handleMouseInput()` - Process drag operations

### Two Types of Display Components

**Full HUDs** (complex, highly configurable):
- `StandingsHud` - Race standings table with columns
- `LapLogHud` - History of lap times with sector breakdown
- `LapConsistencyHud` - Lap time consistency analysis with bars and trend lines
- `IdealLapHud` - Ideal (purple) sector times with gap comparison
- `MapHud` - 2D track map with rider positions and zoom/range mode
- `TelemetryHud` - Throttle/brake/suspension graphs
- `PerformanceHud` - FPS, CPU usage graphs
- `RadarHud` - Proximity radar with nearby rider alerts
- `PitboardHud` - Pitboard-style lap/split information
- `RecordsHud` - Track records from online databases (CBR or MXB-Ranked providers)
- `TimingHud` - Split time comparison popup (center display)
- `GapBarHud` - Live gap visualization bar with ghost position marker
- `SettingsHud` - Interactive settings menu UI
- `FmxHud` - FMX trick detection display with rotation arcs, chain stack, and scoring
- `SessionHud` - Session info (type, format, track, server, players, password)

**Widgets** (simple, focused):
- `SpeedWidget` - Speed and gear display
- `PositionWidget` - Current race position (P1, P2...)
- `LapWidget` - Current lap number
- `TimeWidget` - Session time remaining
- `SpeedoWidget` - Analog speedometer dial
- `TachoWidget` - Analog tachometer dial
- `BarsWidget` - Visual telemetry bars (throttle, brake, etc.)
- `LeanWidget` - Bike lean/roll angle display with arc gauge and steering bar
- `FuelWidget` - Fuel calculator with consumption tracking
- `NoticesWidget` - Race status notices (wrong way, blue flag, last lap, finished)
- `GamepadWidget` - Controller visualization with button/stick/trigger display
- `VersionWidget` - Plugin version display
- `SettingsButtonWidget` - Settings menu toggle button

### HUD Lifecycle

1. **Creation**: HudManager creates all HUDs in `initialize()`
2. **Configuration**: SettingsManager loads saved positions/settings
3. **Data Update**: PluginData changes -> HudManager notifies -> HUD marked dirty
4. **Render Cycle**: Every frame:
   - `update()` called -> if dirty, calls `rebuildRenderData()`
   - `getQuads()` and `getStrings()` return render data
5. **Shutdown**: Settings saved, HUDs destroyed

### Creating a New HUD

Here's the pattern for adding a new HUD:

```cpp
// 1. Create header: hud/my_hud.h
class MyHud : public BaseHud {
public:
    MyHud();
    void update() override;
    bool handlesDataType(DataChangeType type) const override;

private:
    void rebuildRenderData() override;
    void rebuildLayout() override;
};

// 2. Implement: hud/my_hud.cpp
MyHud::MyHud() {
    setDraggable(true);
    setPosition(0.1f, 0.1f);  // Top-left area
    m_quads.reserve(1);       // Background
    m_strings.reserve(5);     // Text lines
    rebuildRenderData();
}

bool MyHud::handlesDataType(DataChangeType type) const {
    return type == DataChangeType::SessionData;  // What triggers updates?
}

void MyHud::update() {
    if (isDataDirty()) {
        rebuildRenderData();
        clearDataDirty();
    } else if (isLayoutDirty()) {
        rebuildLayout();
        clearLayoutDirty();
    }
}

void MyHud::rebuildRenderData() {
    m_quads.clear();
    m_strings.clear();

    auto dim = getScaledDimensions();

    // Add background
    addBackgroundQuad(START_X, START_Y, width, height);

    // Add text
    addString("Hello", x, y, Justify::LEFT, Fonts::ROBOTO_MONO,
              ColorConfig::getInstance().getPrimary(), dim.fontSize);

    setBounds(START_X, START_Y, START_X + width, START_Y + height);
}

// 3. Register in HudManager::initialize()
auto myHudPtr = std::make_unique<MyHud>();
m_pMyHud = myHudPtr.get();
registerHud(std::move(myHudPtr));

// 4. Add settings tab in SettingsHud (optional)
// 5. Add save/load in SettingsManager (optional)
```

## Rendering System

The game engine handles actual rendering. We just provide instructions.

### Quads (`SPluginQuad_t`)

Rectangles with 4 corners. Used for:
- Solid color backgrounds
- Sprite/texture display
- Line segments (very thin quads)

```cpp
struct SPluginQuad_t {
    float m_aafPos[4][2];    // 4 corners, each with (x, y)
    int m_iSprite;           // 0 = solid color, 1+ = sprite index
    unsigned long m_ulColor; // ABGR format
};
```

### Strings (`SPluginString_t`)

Text to render:

```cpp
struct SPluginString_t {
    char m_szString[100];    // Text content
    float m_afPos[2];        // Position (x, y)
    int m_iFont;             // Font index (1-based)
    float m_fSize;           // Font size
    int m_iJustify;          // 0=left, 1=center, 2=right
    unsigned long m_ulColor; // ABGR format
};
```

### Coordinate System

- Normalized: `(0, 0)` = top-left, `(1, 1)` = bottom-right
- Based on 16:9 aspect ratio
- On ultrawide monitors, x extends beyond [0,1]
- Y is always 0-1 (vertical is reference)

### Color Format

Colors use ABGR (Alpha-Blue-Green-Red) format:
```cpp
// Helper in plugin_utils.h
constexpr unsigned long makeColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    return (static_cast<unsigned long>(a) << 24) |
           (static_cast<unsigned long>(b) << 16) |
           (static_cast<unsigned long>(g) << 8) |
           static_cast<unsigned long>(r);
}
```

## Settings & Persistence

### SettingsManager (`core/settings_manager.*`)

Saves/loads HUD configuration to INI file format:

```ini
[StandingsHud]
visible=1
showTitle=1
backgroundOpacity=0.8
scale=1.0
offsetX=0.05
offsetY=0.1
displayRowCount=20

[SpeedWidget]
visible=1
scale=1.0
offsetX=0.4125
offsetY=0.6882
```

Settings are saved:
- On plugin shutdown
- File location: `{game_save_path}/mxbmrp3/mxbmrp3_settings.ini`

### SettingsHud (`hud/settings_hud.*`)

In-game settings menu (toggle with `~` key). Allows users to:
- Show/hide individual HUDs
- Adjust scale and opacity
- Toggle specific columns/rows in data tables
- Configure display modes

#### Settings Layout System

The settings UI uses a helper class (`SettingsLayoutContext`) for consistent layout across all tabs:

```
mxbmrp3/hud/settings/
├── settings_hud.h/.cpp          # Main SettingsHud class
├── settings_layout.h/.cpp       # SettingsLayoutContext helper
├── settings_tab_general.cpp     # General preferences & profiles
├── settings_tab_appearance.cpp  # Fonts & colors
├── settings_tab_standings.cpp   # Standings HUD options
├── settings_tab_map.cpp         # Track map options
├── settings_tab_radar.cpp       # Radar options
├── settings_tab_*.cpp           # Other tab implementations
└── ...
```

**SettingsLayoutContext** provides standardized control rendering:

| Method | Purpose |
|--------|---------|
| `addSectionHeader(title)` | Section divider with label |
| `addToggleControl(label, value, ...)` | On/Off toggle with `< value >` arrows |
| `addCycleControl(label, value, ...)` | Multi-value cycle control |
| `addStandardHudControls(hud)` | Common controls (Visible, Title, Texture, Opacity, Scale) |
| `addWidgetRow(name, hud, ...)` | Table row for Widgets tab |
| `addSpacing(factor)` | Vertical spacing |

**Control Width Standardization**: All controls use `VALUE_WIDTH = 10` to ensure vertical alignment - users can toggle settings by moving the mouse vertically without horizontal adjustment.

#### Tooltip System

Tooltips provide contextual help when hovering over controls:

```
mxbmrp3_data/
└── tooltips.json    # Tooltip definitions
```

**tooltips.json structure**:
```json
{
  "version": 1,
  "tabs": {
    "standings": {
      "title": "Standings",
      "tooltip": "Live race standings showing position, gaps..."
    }
  },
  "controls": {
    "common.visible": "Show or hide this element during gameplay.",
    "standings.rows": "Maximum number of rider rows to display.",
    "map.range": "Zoom level. Full shows entire track..."
  }
}
```

**TooltipManager** (`core/tooltip_manager.h`) is a header-only singleton that:
- Loads `tooltips.json` at startup
- Provides `getTabTooltip(tabId)` and `getControlTooltip(controlId)` methods
- Returns empty string if tooltip not found (graceful fallback)

Tooltips are rendered when hovering over:
- Tab buttons (shows tab description)
- Control rows (shows setting description)

The row-wide tooltip regions are created by passing a `tooltipId` parameter to control helpers like `addToggleControl()` and `addCycleControl()`.

## Asset Management

The plugin uses a dynamic asset discovery system that scans subdirectories at startup.

### AssetManager (`core/asset_manager.*`)

Discovers and registers assets from `plugins/mxbmrp3_data/` subdirectories:

| Directory | File Type | Purpose |
|-----------|-----------|---------|
| `fonts/` | `.fnt` | Bitmap fonts (game engine format) |
| `textures/` | `.tga` | HUD background textures with variants (e.g., `standings_hud_1.tga`) |
| `icons/` | `.tga` | Rider icons for map/radar display |

**Texture Variants**: Textures can have numbered variants (e.g., `standings_hud_1.tga`, `standings_hud_2.tga`). Users can cycle through variants in settings.

**Icon Discovery**: Icons are discovered alphabetically. Use `AssetManager::getIconSpriteIndex(filename)` to get the sprite index for a specific icon by filename. Settings store icon filenames for persistence.

**User Asset Overrides**: Users can override bundled assets by placing custom files in the save directory:
- Location: `{save_path}/mxbmrp3/{fonts,textures,icons}/`
- On startup, AssetManager syncs user overrides to the plugin data directory
- User files override bundled files with the same name
- Allows customization without modifying the plugin installation

### FontConfig (`core/font_config.*`)

Maps semantic font categories to user-selected fonts:

| Category | Default Font | Usage |
|----------|--------------|-------|
| `TITLE` | EnterSansman-Italic | HUD titles |
| `NORMAL` | RobotoMono-Regular | Standard text |
| `STRONG` | RobotoMono-Bold | Emphasized text |
| `MARKER` | FuzzyBubbles-Regular | Handwritten style |
| `SMALL` | Tiny5-Regular | Map/radar labels |

Access via `PluginConstants::Fonts::getTitle()`, `getNormal()`, etc.

### ColorConfig (`core/color_config.*`)

User-configurable color palette with semantic slots:
- `PRIMARY`, `SECONDARY` - Main UI colors
- `POSITIVE`, `NEGATIVE`, `WARNING`, `NEUTRAL` - Status indicators
- `ACCENT` - Highlights

## Input Handling

### InputManager (`core/input_manager.*`)

Polls Windows for input state each frame:
- Mouse position (converted to normalized UI coordinates)
- Left/right mouse button state
- Function keys F1-F9 (HUD toggles)
- OEM keys for settings menu toggle

### Drag-and-Drop

HUDs can be dragged with right-click:
1. `handleMouseInput()` detects click within bounds
2. Saves initial position as drag origin
3. Updates offset while button held
4. `validatePosition()` keeps HUD on screen

## Auto-Update System

The plugin includes an optional auto-update system that checks for new versions on GitHub.

### UpdateChecker (`core/update_checker.*`)

Checks GitHub releases API for newer versions:
- Runs asynchronously in background thread
- Compares semantic version numbers (e.g., "1.6.6.0")
- Fetches release notes, download URL, and file size
- User-configurable: Off or Notify mode

### UpdateDownloader (`core/update_downloader.*`)

Downloads and installs plugin updates:
- Downloads ZIP file from GitHub release assets
- Verifies SHA256 checksum (if provided in release)
- Extracts using bundled miniz library
- Creates backup before installation (atomic update)
- Stages update for next game restart

**Update Flow**:
1. UpdateChecker detects new version → status = `UPDATE_AVAILABLE`
2. User clicks "Install" in settings → UpdateDownloader starts
3. Download → Verify → Backup existing → Extract → Install
4. Status = `READY` → Restart required

**Vendor Dependency**: Uses `vendor/miniz/` for ZIP extraction (public domain, single-file library).

## Key Design Patterns

### Singletons

Most core components are singletons:
```cpp
class PluginData {
public:
    static PluginData& getInstance() {
        static PluginData instance;
        return instance;
    }
private:
    PluginData() = default;
};
```

**Why?** The plugin API gives us one entry point. The game calls our exported functions - we don't create multiple instances.

### Dirty Flag Pattern

Instead of rebuilding every frame:
1. Data changes -> mark dirty
2. Next render -> check dirty flag
3. If dirty -> rebuild, clear flag
4. If clean -> reuse cached data

This is crucial for performance since `Draw()` is called every frame.

#### Standard Pattern (Most HUDs)

Use `processDirtyFlags()` for HUDs that rely on `DataChangeType` notifications:

```cpp
void MyHud::update() {
    processDirtyFlags();  // Handles isDataDirty/isLayoutDirty automatically
}
```

#### Self-Detection Pattern (Polling Widgets)

Some widgets display values that don't trigger `DataChangeType` notifications (e.g., session time updates continuously but doesn't fire `SessionData`). These widgets must poll PluginData and detect changes themselves:

```cpp
void TimeWidget::update() {
    // 1. Poll fresh data
    int currentTime = pluginData.getSessionTime();

    // 2. Compare to cached "last rendered" value
    if (currentSeconds != m_cachedSeconds) {
        setDataDirty();  // Self-mark dirty
    }

    // 3. Process dirty flags
    if (isDataDirty()) {
        rebuildRenderData();
        m_cachedSeconds = currentSeconds;  // Update cache AFTER rebuild
        clearDataDirty();
        clearLayoutDirty();
    }
    else if (isLayoutDirty()) {
        rebuildLayout();
        clearLayoutDirty();
    }
}
```

**Why can't these use `processDirtyFlags()`?** The cache update must happen after `rebuildRenderData()` using local variables calculated before the dirty check. The `onAfterDataRebuild()` hook exists for simpler cases, but these widgets use values computed at the top of `update()`.

#### Hybrid Pattern (Change Detection Before, Standard After)

Some HUDs do change detection but don't need post-rebuild caching:

```cpp
void NoticesWidget::update() {
    // Change detection - updates member state and marks dirty
    if (wrongWay != m_bIsWrongWay) {
        m_bIsWrongWay = wrongWay;  // State updated BEFORE dirty check
        setDataDirty();
    }

    processDirtyFlags();  // Can use standard helper
}
```

#### When to Use Which Pattern

| Pattern | Use When | Examples |
|---------|----------|----------|
| `processDirtyFlags()` | HUD relies on DataChangeType notifications | StandingsHud, IdealLapHud, MapHud |
| Hybrid | Polls data but caches state BEFORE dirty check | NoticesWidget, GapBarHud |
| Self-Detection | Needs to cache "last rendered value" AFTER rebuild | TimeWidget, PositionWidget, LapWidget |

### Handler Singleton Macro

All handlers use this pattern:
```cpp
// In header
class MyHandler {
public:
    static MyHandler& getInstance();
    void handleSomething(Data* data);
};

// In .cpp
DEFINE_HANDLER_SINGLETON(MyHandler)

void MyHandler::handleSomething(Data* data) {
    HANDLER_NULL_CHECK(data);
    // Process data...
}
```

### Data Change Notifications

```cpp
// PluginData notifies HudManager directly (no observer pattern overhead)
void PluginData::notifyHudManager(DataChangeType changeType) {
    HudManager::getInstance().onDataChanged(changeType);
}

// HudManager marks relevant HUDs as dirty
void HudManager::onDataChanged(DataChangeType changeType) {
    for (auto& hud : m_huds) {
        if (hud->handlesDataType(changeType)) {
            hud->setDataDirty();
        }
    }
}
```

## Constants & Configuration

All magic numbers live in `plugin_constants.h`:

```cpp
namespace PluginConstants {
    namespace FontSizes {
        constexpr float NORMAL = 0.0200f;
        constexpr float LARGE = 0.0300f;
    }

    // Colors are configurable via ColorConfig singleton
    // ColorConfig::getInstance().getPrimary(), getSecondary(), etc.

    namespace Session {
        constexpr int RACE_1 = 6;
        constexpr int RACE_2 = 7;
    }
}
```

## Debugging

### Debug Logging

```cpp
DEBUG_INFO("Plugin initialized");
DEBUG_INFO_F("Received %d riders", count);
DEBUG_WARN("Something unexpected");
```

Logs go to `{save_path}/mxbmrp3/mxbmrp3.log`

### Performance Timing

```cpp
SCOPED_TIMER_THRESHOLD("MyFunction", 100);  // Logs if > 100us
```

### Build Configurations

- **Debug**: Enables all logging, assertions
- **Release**: Minimal logging, optimized

## Common Gotchas

1. **Don't cache game data in HUDs for rendering** - Always read fresh from PluginData when building render data. HUDs only cache formatted render data (`m_quads`, `m_strings`). **Exception:** Widgets that poll continuously-changing values (like session time) may cache "last rendered value" for change detection - see "Self-Detection Pattern" in Dirty Flag Pattern section.

2. **0-based vs 1-based indexing** - API uses 0-based lap numbers, UI shows 1-based. Check the API header comments.

3. **No exceptions in callbacks** - The game engine doesn't handle C++ exceptions. Use defensive checks.

4. **Thread safety** - The plugin runs single-threaded (all callbacks on main thread). No synchronization needed.

5. **Sprite indices are 1-based** - Index 0 means "solid color fill", not "first sprite".

6. **Font indices are 1-based** - Font index 0 is invalid.

7. **Icon ordering is alphabetical** - Icons in `mxbmrp3_data/icons/` are discovered alphabetically. Use filename-based lookups via `AssetManager` for persistence; icon additions/removals won't break saved settings.

## Multi-Game Support

The plugin supports multiple Piboso racing games from a single codebase using compile-time game selection.

### Supported Games

| Game | Mod ID | Vehicle Type | Splits | Unique Features |
|------|--------|--------------|--------|-----------------|
| MX Bikes | `mxbikes` | Bike (2 wheels) | 2 | Holeshot timing, Straight Rhythm |
| GP Bikes | `gpbikes` | Bike (2 wheels) | 3 | ECU/TC/AW, Tread temps |
| WRS | `wrs` | Car (4-6 wheels) | 2 | Rolling start, Turbo, Handbrake |
| KRP | `krp` | Kart (4 wheels) | 2 | Session series, Qualify heats |

### Build Configurations

Each game produces its own DLL:

| Configuration | Output | Install Location |
|---------------|--------|------------------|
| MXB-Release | `mxbmrp3.dlo` | MX Bikes `plugins/` |
| GPB-Release | `gpbmrp3.dlo` | GP Bikes `plugins/` |
| (future) | `wrsmrp3.dlo` | WRS `plugins/` |
| (future) | `krpmrp3.dlo` | KRP `plugins/` |

The Visual Studio project uses conditional compilation to include only the relevant API file:

```xml
<!-- MX Bikes API - excluded from GP Bikes builds -->
<ClCompile Include="vendor\piboso\mxb_api.cpp">
  <ExcludedFromBuild Condition="'$(Configuration)|$(Platform)'=='GPB-Debug|x64'">true</ExcludedFromBuild>
  <ExcludedFromBuild Condition="'$(Configuration)|$(Platform)'=='GPB-Release|x64'">true</ExcludedFromBuild>
</ClCompile>
```

### Feature Flags

**Compile-Time** (`game/game_config.h`):
```cpp
#if GAME_HAS_HOLESHOT
void handleRaceHoleshot(const Unified::RaceHoleshotData* data);
#endif
```

**Runtime** (adapter constants):
```cpp
if constexpr (Game::Adapter::HAS_RACE_SPEED) {
    // Show speed trap data
}
```

Key feature flags:
- `GAME_HAS_HOLESHOT` - MX Bikes only
- `GAME_HAS_RACE_SPEED` - All except MX Bikes
- `GAME_HAS_ECU` - GP Bikes only
- `GAME_HAS_TRACK_TEMP` - All except MX Bikes
- `GAME_HAS_CRASH_STATE` - MX Bikes, GP Bikes

### Variable Split Count

Games have different numbers of timing splits. Unified types use a dynamic count:

```cpp
struct RaceLapData {
    int splits[MAX_SPLITS];  // MAX_SPLITS = 3
    int splitCount;          // Actual count (2 for MXB, 3 for GPB)
};
```

### Updating Vendor APIs

When Piboso releases a new API version:

1. **Update the vendor header** (`mxb_api.h`, `gpb_api.h`, etc.)
2. **Update the adapter** to handle new/changed fields
3. **Update the API cpp** if new callbacks are added
4. **Update unified types** if new data needs to be shared

The adapter layer isolates changes - core HUDs don't need modification for most API updates.

### API Differences

**Identical across all games:**
- Draw API (`SPluginQuad_t`, `SPluginString_t`)
- Track segment structure
- Callback function names
- Interface version (9)

**Per-game variations:**
- Vehicle telemetry fields (wheels, suspension, ECU)
- Session type meanings
- Entry state values (MX Bikes has extra "unknown" state)
- Split counts in lap data
- Game-specific events (Holeshot, RaceSpeed)

## Quick Reference: File Locations

| What | Where |
|------|-------|
| API entry points (MX Bikes) | `vendor/piboso/mxb_api.cpp` |
| API entry points (GP Bikes) | `vendor/piboso/gpb_api.cpp` |
| Game adapters | `game/adapters/*_adapter.h` |
| Unified types | `game/unified_types.h` |
| Game config | `game/game_config.h` |
| Central state | `core/plugin_data.cpp` |
| HUD base class | `hud/base_hud.cpp` |
| All constants | `core/plugin_constants.h` |
| Asset manager | `core/asset_manager.cpp` |
| Font configuration | `core/font_config.cpp` |
| Color configuration | `core/color_config.cpp` |
| Update checker | `core/update_checker.cpp` |
| Update downloader | `core/update_downloader.cpp` |
| XInput / Rumble | `core/xinput_reader.cpp` |
| Rumble profiles manager | `core/rumble_profile_manager.cpp` |
| FMX trick detection | `core/fmx_manager.cpp` |
| FMX types | `core/fmx_types.h` |
| Settings UI | `hud/settings/settings_hud.cpp` |
| Settings layout helpers | `hud/settings/settings_layout.cpp` |
| Settings tabs | `hud/settings/settings_tab_*.cpp` |
| Tooltip definitions | `mxbmrp3_data/tooltips.json` |
| Tooltip manager | `core/tooltip_manager.h` |
| Settings file | `{save_path}/mxbmrp3/mxbmrp3_settings.ini` |
| Rumble profiles file | `{save_path}/mxbmrp3/rumble_profiles.json` |
| Log file | `{save_path}/mxbmrp3/mxbmrp3.log` |
| Build output (MX Bikes) | `build/MXB-Release/mxbmrp3.dlo` |
| Build output (GP Bikes) | `build/GPB-Release/gpbmrp3.dlo` |
| Runtime assets | `{game_path}/plugins/mxbmrp3_data/{fonts,textures,icons}/` |
| User asset overrides | `{save_path}/mxbmrp3/{fonts,textures,icons}/` |

## Quick Reference: Adding Features

| Task | Steps |
|------|-------|
| Add new HUD | Create class, inherit BaseHud, register in HudManager |
| Add new data type | Add struct to PluginData, add DataChangeType enum |
| Add new setting | Add field to HUD, save/load in SettingsManager |
| Add settings tab | Create `settings_tab_*.cpp`, add tab enum, register in SettingsHud |
| Add tooltip | Add entry to `tooltips.json`, pass tooltipId to control helper |
| Add keyboard shortcut | Handle in HudManager::processKeyboardInput() |
| Add new handler | Create handler class, route from PluginManager |
| Add new font | Place `.fnt` file in `mxbmrp3_data/fonts/` (auto-discovered) |
| Add new texture | Place `.tga` file in `mxbmrp3_data/textures/` (auto-discovered) |
| Add new icon | Place `.tga` file in `mxbmrp3_data/icons/` (auto-discovered, alphabetical order) |
| Add game-specific feature | Add to `unified_types.h`, update adapters, add feature flag to `game_config.h` |
| Support new game | Create adapter in `game/adapters/`, add API file in `vendor/piboso/`, update `game_config.h` |
