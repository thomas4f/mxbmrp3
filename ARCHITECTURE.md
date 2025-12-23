# MXBMRP3 Architecture Guide

This document explains how the MXBMRP3 plugin works, from the ground up. It's designed to help new contributors understand the codebase quickly.

## What Is This Project?

MXBMRP3 is a **HUD (Heads-Up Display) plugin** for MX Bikes, a motocross racing simulator. The plugin displays real-time racing information on screen: lap times, standings, speedometer, track map, and more.

The plugin is a Windows DLL (with `.dlo` extension) that the game loads at startup. The game calls our exported functions to send us data and request rendering instructions.

## Project Structure

```
mxbmrp3/
├── mxbmrp3/                    # Main plugin source code
│   ├── vendor/piboso/          # Game API definitions (read-only)
│   │   └── mxb_api.h/.cpp      # Plugin interface exported to game
│   ├── core/                   # Core infrastructure
│   │   ├── plugin_manager.*    # Main coordinator, routes API callbacks
│   │   ├── plugin_data.*       # Central game state cache
│   │   ├── hud_manager.*       # Owns and updates all HUDs
│   │   ├── input_manager.*     # Keyboard and mouse input
│   │   ├── xinput_reader.*     # XInput controller state and rumble
│   │   ├── settings_manager.*  # Save/load configuration (INI file)
│   │   ├── asset_manager.*     # Dynamic asset discovery (fonts, textures, icons)
│   │   ├── font_config.*       # User-configurable font categories
│   │   ├── color_config.*      # User-configurable color palette
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
│   │   └── *_widget.*          # Simple widgets (focused display)
│   └── diagnostics/            # Debugging tools
│       ├── logger.*            # Debug logging to file
│       └── timer.h             # Performance measurement
├── mxbmrp3_data/               # Runtime assets (discovered dynamically)
│   ├── fonts/                  # .fnt files (bitmap fonts)
│   ├── textures/               # .tga files (HUD backgrounds with variants)
│   └── icons/                  # .tga files (rider icons for map/radar)
├── docs/                       # Documentation
├── replay_tool/                # Separate tool for replay analysis
└── mxbmrp3.sln                 # Visual Studio solution
```

## The Big Picture

Here's how data flows through the plugin:

```
┌─────────────────────────────────────────────────────────────────────────┐
│                           MX BIKES GAME ENGINE                          │
└─────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
                    ┌───────────────────────────────┐
                    │      mxb_api.cpp              │
                    │   (Exported C Functions)      │
                    │                               │
                    │  Startup(), Draw(), RunLap(), │
                    │  RaceEvent(), etc.            │
                    └───────────────────────────────┘
                                    │
                                    ▼
                    ┌───────────────────────────────┐
                    │      PluginManager            │
                    │   (Main Coordinator)          │
                    │                               │
                    │  Routes callbacks to handlers │
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
│                           MX BIKES GAME ENGINE                          │
│                          (Renders our output)                           │
└─────────────────────────────────────────────────────────────────────────┘
```

## Core Components

### 1. The Plugin API (`vendor/piboso/mxb_api.*`)

The game defines a C API that plugins must implement. Key exported functions:

| Function | When Called | Purpose |
|----------|-------------|---------|
| `Startup()` | Game starts | Initialize plugin, return telemetry rate |
| `Shutdown()` | Game closes | Clean up resources |
| `EventInit()` | Track loaded | Receive track/bike info |
| `RunInit()` | Player goes on track | Session begins |
| `RunTelemetry()` | Every physics tick | Receive bike telemetry (100Hz) |
| `RunLap()` | Lap completed | Receive lap time |
| `Draw()` | Every frame | Return quads/strings to render |
| `RaceEvent()` | Online race starts | Receive race info |
| `RaceClassification()` | Continuously | Receive standings updates |

The API uses C structs (e.g., `SPluginsBikeData_t`, `SPluginsRaceLap_t`) to pass data. These are defined in `mxb_api.h`.

### 2. PluginManager (`core/plugin_manager.*`)

The central coordinator. It:
- Receives all API callbacks from the game
- Initializes core systems on startup
- Routes each callback to the appropriate handler
- Measures callback execution time for performance tracking

```cpp
// Example: When the game calls RunLap(), PluginManager routes it:
void PluginManager::handleRunLap(SPluginsBikeLap_t* psLapData) {
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
- `IdealLapHud` - Ideal (purple) sector times with gap comparison
- `MapHud` - 2D track map with rider positions and zoom/range mode
- `TelemetryHud` - Throttle/brake/suspension graphs
- `InputHud` - Controller stick visualization
- `PerformanceHud` - FPS, CPU usage graphs
- `RadarHud` - Proximity radar with nearby rider alerts
- `PitboardHud` - Pitboard-style lap/split information
- `RecordsHud` - Track records from online database
- `TimingHud` - Split time comparison popup (center display)
- `GapBarHud` - Live gap visualization bar with ghost position marker
- `SettingsHud` - Interactive settings menu UI

**Widgets** (simple, focused):
- `SpeedWidget` - Speed and gear display
- `PositionWidget` - Current race position (P1, P2...)
- `LapWidget` - Current lap number
- `TimeWidget` - Session time remaining
- `SessionWidget` - Session type display
- `SpeedoWidget` - Analog speedometer dial
- `TachoWidget` - Analog tachometer dial
- `BarsWidget` - Visual telemetry bars (throttle, brake, etc.)
- `FuelWidget` - Fuel calculator with consumption tracking
- `NoticesWidget` - Race status notices (wrong way, blue flag, last lap, finished)
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

**Icon Discovery**: Icons are discovered alphabetically. The `SHAPE_*` constants in `tracked_riders_manager.h` map to this alphabetical order.

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

1. **Don't cache game data in HUDs** - Always read from PluginData. HUDs only cache formatted render data.

2. **0-based vs 1-based indexing** - API uses 0-based lap numbers, UI shows 1-based. Check the API header comments.

3. **No exceptions in callbacks** - The game engine doesn't handle C++ exceptions. Use defensive checks.

4. **Thread safety** - The plugin runs single-threaded (all callbacks on main thread). No synchronization needed.

5. **Sprite indices are 1-based** - Index 0 means "solid color fill", not "first sprite".

6. **Font indices are 1-based** - Font index 0 is invalid.

7. **Icon ordering is alphabetical** - Icons in `mxbmrp3_data/icons/` are discovered alphabetically. The `SHAPE_*` constants in `tracked_riders_manager.h` depend on this order. Renaming icon files will break the constant mappings.

## Quick Reference: File Locations

| What | Where |
|------|-------|
| API entry points | `vendor/piboso/mxb_api.cpp` |
| Central state | `core/plugin_data.cpp` |
| HUD base class | `hud/base_hud.cpp` |
| All constants | `core/plugin_constants.h` |
| Asset manager | `core/asset_manager.cpp` |
| Font configuration | `core/font_config.cpp` |
| Color configuration | `core/color_config.cpp` |
| Settings file | `{save_path}/mxbmrp3/mxbmrp3_settings.ini` |
| Log file | `{save_path}/mxbmrp3/mxbmrp3.log` |
| Build output | `build/Release/mxbmrp3.dlo` |
| Runtime assets | `{game_path}/plugins/mxbmrp3_data/{fonts,textures,icons}/` |

## Quick Reference: Adding Features

| Task | Steps |
|------|-------|
| Add new HUD | Create class, inherit BaseHud, register in HudManager |
| Add new data type | Add struct to PluginData, add DataChangeType enum |
| Add new setting | Add field to HUD, save/load in SettingsManager |
| Add keyboard shortcut | Handle in HudManager::processKeyboardInput() |
| Add new handler | Create handler class, route from PluginManager |
| Add new font | Place `.fnt` file in `mxbmrp3_data/fonts/` (auto-discovered) |
| Add new texture | Place `.tga` file in `mxbmrp3_data/textures/` (auto-discovered) |
| Add new icon | Place `.tga` file in `mxbmrp3_data/icons/` (auto-discovered, alphabetical order) |
