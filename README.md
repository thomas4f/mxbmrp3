A customizable, [open-source](https://github.com/thomas4f/mxbmrp3) HUD plugin for MX Bikes displaying real-time race information and telemetry.

![MXBMRP3 HUD Screenshot](./mxbmrp3.png)
*Example HUD layout showing standings, map, telemetry, and widgets. All elements are fully customizable.*

### Features

- Live race standings, track map, and proximity radar with approach alerts
- Lap timing with splits, personal bests, gap-to-PB visualization, and online lap records
- Track specific riders with custom colors and icons across all HUDs
- Controller rumble feedback with customizable effects (bumps, slide, spin, lockup, wheelie, etc.)
- Telemetry visualization and compact info widgets
- Drag-and-drop positioning with color themes and customizable hotkeys
- Automatic profile switching for Practice, Qualify, Race, and Spectate sessions

### Get Started

Download and install the plugin to begin customizing your HUD. Most users should use the automatic installer. Use the ZIP for manual installation.

[![Download Installer](https://img.shields.io/badge/Download-Installer-green?style=for-the-badge)](https://github.com/thomas4f/mxbmrp3/releases/latest/download/mxbmrp3-Setup.exe)
[![Download ZIP](https://img.shields.io/badge/Download-ZIP-blue?style=for-the-badge)](https://github.com/thomas4f/mxbmrp3/releases/latest/download/mxbmrp3.zip)

See [Installation](#installation) for setup instructions.

## Contents

- [Installation](#installation)
  - [Automatic](#automatic-installation)
  - [Manual](#manual-installation)
- [Controls](#controls)
- [Configuration](#configuration)
  - [Profiles](#profiles)
  - [Appearance](#appearance)
  - [Hotkeys](#hotkeys)
  - [Tracked Riders](#tracked-riders)
  - [Controller Rumble](#controller-rumble)
- [HUDs](#huds)
  - [Standings](#standings-hud)
  - [Map](#map-hud)
  - [Radar](#radar-hud)
  - [Timing](#timing-hud)
  - [Gap Bar](#gap-bar-hud)
  - [Pitboard](#pitboard-hud)
  - [Lap Log](#lap-log-hud)
  - [Ideal Lap](#ideal-lap-hud)
  - [Records](#records-hud)
  - [Telemetry](#telemetry-hud)
  - [Performance](#performance-hud)
- [Widgets](#widgets)
- [Modding](#modding)
- [Troubleshooting](#troubleshooting)
- [Development](#development)

## Installation

**Requirements:**
- MX Bikes **Beta 20 or newer**
- [Microsoft Visual C++ Redistributable (x64)](https://aka.ms/vc14/vc_redist.x64.exe) (the automatic installer will check for this)

### Automatic Installation

1. Download the latest installer [`mxbmrp3-Setup.exe`](https://github.com/thomas4f/mxbmrp3/releases/latest/download/mxbmrp3-Setup.exe)
2. Run the installer - it will:
   - Auto-detect your MX Bikes installation (Steam or standalone)
   - Install to the correct plugins folder
   - Check for and offer to install Visual C++ Redistributable if needed
   - Handle upgrades automatically (preserves your settings)

### Manual Installation

1. Download the latest release archive [`mxbmrp3.zip`](https://github.com/thomas4f/mxbmrp3/releases/latest/download/mxbmrp3.zip)
2. Find your MX Bikes plugins folder:
   - **Steam**: Right-click MX Bikes in your library → **Manage** → **Browse local files** → open `plugins`
   - **Standalone**: Navigate to your MX Bikes installation folder (e.g., `C:\Games\MX Bikes\`) → open `plugins`
3. Extract the plugin files:
   - Copy `mxbmrp3.dlo` to `[MX Bikes]/plugins/`
   - Copy the `mxbmrp3_data/` folder to `[MX Bikes]/plugins/`

   **Do NOT delete the existing game files** (`proxy64.dlo`, `proxy_udp64.dlo`, `xinput64.dli`) - these are native MX Bikes files, not old plugin versions.

   Your directory should look like this after installation:
   ```
   MX Bikes/
   │   mxbikes.exe
   │   ...
   │
   └───plugins/
       ├── mxbmrp3_data/        ← Add this folder (from release)
       │   ├── fonts/           ← Font files (.fnt)
       │   ├── textures/        ← Texture files (.tga)
       │   └── icons/           ← Icon files (.tga)
       ├── mxbmrp3.dlo          ← Add this (from release)
       ├── proxy_udp64.dlo      ← Keep (native game file)
       ├── proxy64.dlo          ← Keep (native game file)
       └── xinput64.dli         ← Keep (native game file)
   ```

### After Installation

Launch MX Bikes - the plugin will load automatically. Some elements are enabled by default and can be repositioned or configured via the settings menu. If nothing appears, see [Troubleshooting](#troubleshooting).

## Controls

### Mouse
- **Move Mouse** - Show cursor and `[=]` settings button (auto-hides after inactivity)
- **Left Click** - Interact with settings menu and HUD elements
- **Right Click & Drag** - Reposition elements

### Keyboard Shortcuts

Keyboard and controller hotkeys can be customized in Settings > Hotkeys. By default, only the settings menu hotkey is configured:

- **Tilde** (below Esc) - Toggle settings menu

## Configuration

Use the settings menu (Tilde key or `[=]` settings button) to access tabs for each HUD:
- Toggle visibility
- Adjust scale, opacity, and background textures
- Configure individual HUD settings (columns, rows, display modes)
- Customize appearance (fonts and color theme)
- Set preferences (speed/fuel units, grid snapping)
- Customize keyboard and controller hotkeys
- Select controller index for Gamepad Widget and rumble feedback
- Check for updates (optional, checks GitHub releases on startup)
- Reset settings (per-tab, per-profile, or all settings)

All element positions, scales, and visibility settings are automatically saved between sessions.

### Profiles

The plugin supports four separate profiles, each storing a complete HUD layout configuration:

- **Practice** - Used during practice and warmup sessions
- **Qualify** - Used during pre-qualify, qualify practice, and qualify sessions
- **Race** - Used during race sessions (Race 1, Race 2, Straight Rhythm)
- **Spectate** - Used when spectating or viewing replays

**Auto-Switch** (disabled by default): Automatically switches profiles based on session type. You can manually select a profile or disable auto-switching from the General tab in settings.

**Copy**: Copy the current profile's layout to a specific profile or all other profiles.

### Appearance

Customize fonts and colors in **Settings > Appearance**:

**Fonts**: Assign fonts to five categories used across all HUDs:
- **Title** - HUD titles
- **Normal** - General text
- **Strong** - Emphasis/important text
- **Marker** - Pitboard text
- **Small** - Map/radar labels

**Colors**: Customize the color theme with nine configurable slots:
- **Primary** - Main text and highlights
- **Secondary** - Labels and secondary text
- **Muted** - Disabled/inactive elements
- **Positive** - Ahead/faster indicators (green)
- **Warning** - Behind/slower indicators (red)
- **Tertiary** - Neutral/other riders
- **Neutral** - Borders and dividers
- **Background** - HUD backgrounds
- **Highlight** - Selection highlights

### Hotkeys

Bind keyboard keys or controller buttons to toggle HUDs and widgets. Access the **Hotkeys** tab in settings to customize bindings.

- **Actions**: Toggle individual HUDs, all widgets, all elements, settings menu, or reload config
- **Reload Config**: Hotkey action to reload settings from the INI file without restarting the game
- **Keyboard**: Any key with optional modifiers (Ctrl, Shift, Alt)
- **Controller**: D-Pad, Start, Back, L3/R3, LB/RB, A/B/X/Y

Only the settings menu hotkey is configured by default (tilde key).

### Tracked Riders

Track specific riders to highlight them across all HUDs with custom colors and icons. Access the **Riders** tab in settings to manage tracked riders:

- **Server Players**: Shows all riders currently on the server. Click to add/remove from tracking.
- **Tracked Riders**: Your list of tracked riders with customizable appearance.
  - **Left-click**: Cycle through colors
  - **Right-click**: Cycle through 50 available icons
  - **Hover + click X**: Remove from tracking

Tracked riders appear with their custom colors and icons in:
- **Standings HUD**: Toggleable icon column showing tracked rider indicators
- **Map HUD**: Custom icons and colors on the track map
- **Radar HUD**: Custom icons and colors on the proximity radar

Tracked riders persist across sessions and servers - add a rider once and they'll be highlighted whenever you race together.

### Controller Rumble

Configure vibration feedback in the **Rumble** tab. Each effect can be assigned to heavy (left) or light (right) motors:

| Effect | Unit | Description |
|--------|------|-------------|
| **Bumps** | m/s | Feel suspension compression from bumps, jumps, and hard landings, driven by suspension compression velocity |
| **Slide** | deg | Feel when the bike is sliding laterally, based on lateral slip angle, useful for detecting and controlling drifts |
| **Spin** | x | Feedback when the rear wheel loses traction under acceleration, driven by rear wheel overrun multiplier (e.g., 5x = 500% slip) |
| **Lockup** | ratio | Warning vibration when front or rear wheel locks under heavy braking, based on wheel underrun ratio |
| **Wheelie** | deg | Feel the intensity of wheelies – vibration scales with pitch angle as the front wheel lifts |
| **Steer** | Nm | Handlebar resistance feedback – feel ruts, rocks, and terrain forces driven by handlebar torque |
| **RPM** | rpm | Continuous vibration that increases with engine RPM, giving a sense of engine load |
| **Surface** | m/s | Vibration when riding off-track on grass, dirt, or gravel, scaled by riding speed on rough terrain |

Each effect has:
- **Light/Heavy** - Motor strength (Off, 10-100%)
- **Min/Max** - Threshold values in the effect's unit where rumble scales from zero to full

#### Forces HUD

Real-time visualization of controller rumble effects:
- Graph showing force intensities over time
- Heavy motor (red) and light motor (blue) output levels
- Individual effect contributions when enabled (bumps, spin, lockup, etc.)
- Useful for tuning rumble settings and debugging
- Disabled by default (enable in Rumble tab)

## HUDs

Full-featured displays with extensive customization options:

### Standings HUD
Displays live race positions with detailed rider information:
- Position, race number, rider name
- Tracked rider indicators (custom icons for riders you're following)
- Bike brand (color-coded by manufacturer)
- Status column (L1, L2... for lap count, LL for last lap, FIN when finished, PIT, DNS/DSQ/RET)
- Gap times with configurable modes (Off, Player gaps only, All gaps)
- Gap indicator showing live/official time to adjacent riders
- **Inverted gaps** shown in warning color (when track positions don't match classification)
- Live gap shows N/A in muted color for non-race sessions
- Gap reference: Compare gaps to race leader or to yourself
- Configurable row count (8-30 rows)
- Pagination that focuses on player position
- Click rider names to follow them (spectating/replay mode)

### Map HUD
Top-down track map showing:
- Track layout with configurable rotation
- Rider positions in real-time
- **Click rider markers to spectate** them (in spectate/replay mode)
- Multiple rider shapes (arrow, chevron, circle, dot, pin, etc.)
- Color modes: Position (green/red by relative position), Brand (bike colors), or Uniform (same color, brightness varies by lap)
- Configurable labels, track width scale, and outline
- Adjustable marker scale for rider icons
- Range mode: Full track view or follow-player zoom with configurable distance

### Radar HUD
Proximity radar showing nearby riders:
- Adjustable range (10-100m)
- Proximity alerts with configurable alert distance
- **Proximity arrows**: Visual indicators for riders outside radar range (Edge or Circle mode)
  - Distance-based color gradient (red when close, yellow mid-range, green when far)
  - Configurable arrow shape and scale
- Multiple rider shapes (arrow, chevron, circle, dot, pin, etc.)
- Color modes: Brand (bike colors), Position (green/red by relative position), or Uniform (same color, brightness varies by lap)
- Tracked riders displayed with their custom colors and icons
- Adjustable marker scale for rider icons
- Toggle player arrow visibility
- Auto-fade when no riders nearby
- Multiple label modes (position, race number, name)

### Timing HUD
Real-time split and lap time display:
- Accumulated split times at each sector
- Gap comparison options: Personal Best (session), Ideal Lap, Session Best, or All-Time PB
- **Shows actual improvement** when beating your personal best (compares to previous PB)
- Multiple gap types can be stacked vertically
- Appears center-screen after crossing splits
- Configurable display duration

### Gap Bar HUD
Visual gap-to-personal-best display:
- Horizontal bar showing live gap (green=ahead, red=behind)
- Current position and ghost position markers
- Freezes to show official gap at splits/lap completion
- Configurable width, range, and freeze duration

### Pitboard HUD
Pitboard-style information display:
- Configurable rows: Rider ID, Position, Session, Time, Lap, Last Lap, Gap
- Position and lap status (L1, L2..., LL for last lap, FIN when finished)
- Accumulated split times at splits, lap time at finish
- Gap to leader
- Display modes: Always visible, Pit area only, or Splits triggered

### Lap Log HUD
Historical lap times and performance:
- Lap-by-lap time listing
- Personal best indicators
- Live gap coloring (green when ahead, red when behind)
- Race finish time display

### Ideal Lap HUD
Shows ideal (purple) sector times and gaps to theoretical best lap:
- S1, S2, S3: Best individual sector times with gap to current sector
- Last: Last lap time with gap to ideal
- Best: Personal best lap time with gap to ideal
- Ideal: Sum of best individual sectors (theoretical perfect lap)

### Records HUD
Lap records from online database:
- Track-specific lap records
- Rider name, bike, and lap time
- **Your personal best** displayed and highlighted (stored locally across sessions)
- Configurable number of records displayed
- Category filtering

### Telemetry HUD
Real-time bike data visualization:
- Throttle, brake, clutch inputs
- Suspension travel
- RPM, gear, and fuel
- Customizable graphs or numeric display

### Performance HUD
Plugin and game performance metrics:
- FPS counter
- Plugin CPU usage

## Widgets

Simple, focused display elements:

1. **Lap Widget** - Current lap number and total laps
2. **Position Widget** - Race position
3. **Time Widget** - Session time or countdown
4. **Session Widget** - Session type display
5. **Speed Widget** - Current speed with gear
6. **Speedo Widget** - Analog speedometer dial
7. **Tacho Widget** - Analog tachometer dial
8. **Bars Widget** - Visual telemetry bars (left to right):
   - **T** - Throttle (green)
   - **B** - Brakes (split: red front / dark red rear)
   - **C** - Clutch (blue)
   - **R** - RPM (gray)
   - **S** - Suspension (split: purple front / dark purple rear)
   - **F** - Fuel (yellow)
9. **Fuel Widget** - Fuel calculator with consumption tracking:
   - **Fue** - Current fuel level
   - **Use** - Total fuel used this session
   - **Avg** - Average consumption per lap
   - **Est** - Estimated laps remaining
10. **Notices Widget** - Race status notices (priority order: wrong way, blue flag, last lap, finished)
11. **Gamepad Widget** - Visual controller display with real-time button, stick, and trigger states. Multiple texture variants available.

## Modding

### Custom Textures

Customize the look of HUDs and widgets by adding TGA textures to `mxbmrp3_data/textures/`. Files use the naming convention `{name}_{number}.tga`:

| Element | Texture Name |
|-----------|--------------|
| Gap Bar | `gap_bar_hud` |
| Gamepad | `gamepad_widget` |
| Ideal Lap | `ideal_lap_hud` |
| Lap Log | `lap_log_hud` |
| Map | `map_hud` |
| Performance | `performance_hud` |
| Pitboard | `pitboard_hud` |
| Radar | `radar_hud` |
| Records | `records_hud` |
| Rumble | `rumble_hud` |
| Standings | `standings_hud` |
| Telemetry | `telemetry_hud` |
| Timing | `timing_hud` |
| Bars | `bars_widget` |
| Fuel | `fuel_widget` |
| Lap | `lap_widget` |
| Notices | `notices_widget` |
| Pointer | `pointer_widget` |
| Position | `position_widget` |
| Session | `session_widget` |
| Speed | `speed_widget` |
| Speedometer | `speedo_widget` |
| Tachometer | `tacho_widget` |
| Time | `time_widget` |

Textures are automatically discovered at startup. Cycle through them in each HUD's settings tab using the Texture control.

### Custom Fonts

Add custom fonts to `mxbmrp3_data/fonts/`. Fonts are automatically discovered and can be assigned to categories in **Settings > Appearance**:

| Category | Usage | Default |
|----------|-------|---------|
| **Title** | HUD titles | EnterSansman Italic |
| **Normal** | General text | Roboto Mono Regular |
| **Strong** | Emphasis/important | Roboto Mono Bold |
| **Marker** | Pitboard text | Fuzzy Bubbles Regular |
| **Small** | Map/radar labels | Tiny5 Regular |

To generate fonts, use the `fontgen` utility provided by PiBoSo. See [this forum post](https://forum.piboso.com/index.php?topic=1458.msg20183#msg20183) for details. An example configuration is provided in [`fontgen.cfg`](fontgen.cfg).

### Advanced INI Settings

Power users can manually edit `mxbmrp3_settings.ini` (located in the MX Bikes user data folder under `mxbmrp3/`) for settings not exposed in the UI.

Settings use `[HudName:N]` sections where N is the profile index: 0=Practice, 1=Qualify, 2=Race, 3=Spectate.

**Named Keys**: Settings use human-readable keys instead of numeric bitmasks. Toggle individual elements by setting their key to `1` (enabled) or `0` (disabled).

| Widget | Keys | Default |
|--------|------|---------|
| **TimeWidget** | `showSessionType` | Disabled |
| **SpeedWidget** | `row_speed`, `row_units`, `row_gear` | All enabled |
| **FuelWidget** | `row_fuel`, `row_used`, `row_avg`, `row_est` | All enabled |
| **BarsWidget** | `col_throttle`, `col_brake`, `col_clutch`, `col_rpm`, `col_suspension`, `col_fuel` | All enabled |
| **NoticesWidget** | `notice_wrong_way`, `notice_blue_flag`, `notice_last_lap`, `notice_finished` | All enabled |

**Enum Settings**: Mode settings use string values instead of integers for clarity.

| Setting | Values |
|---------|--------|
| `riderColorMode` | `UNIFORM`, `BRAND`, `RELATIVE_POS` |
| `labelMode` | `NONE`, `POSITION`, `RACE_NUM`, `BOTH` |
| `officialGapMode` / `liveGapMode` | `OFF`, `PLAYER`, `ALL` |
| `gapIndicatorMode` | `OFF`, `OFFICIAL`, `LIVE`, `BOTH` |
| `gapReferenceMode` | `LEADER`, `PLAYER` |

Examples:
```ini
[SpeedWidget:0]
row_speed=1
row_units=0
row_gear=1

[StandingsHud:2]
liveGapMode=ALL
gapReferenceMode=PLAYER
```

### Data Files

Plugin data is stored in the MX Bikes user data folder under `mxbmrp3/`:

| File | Description |
|------|-------------|
| `mxbmrp3_settings.ini` | All HUD settings (positions, visibility, options) |
| `mxbmrp3_personal_bests.json` | Your personal best lap times per track/bike/category |
| `mxbmrp3_tracked_riders.json` | Tracked riders with colors and icons |

Personal bests are stored locally and persist across sessions. They are displayed in the Records HUD alongside online records.

## Troubleshooting

**HUD Not Appearing**
- Check [Installation requirements](#installation) (MX Bikes Beta 20+, Visual C++ Redistributable)
- Verify `mxbmrp3.dlo` and `mxbmrp3_data/` are in the correct `plugins/` folder (see [Manual Installation](#manual-installation)). MX Bikes has two directories - the **game installation** (contains `mxbikes.exe`) and **user data** (`Documents\PiBoSo\MX Bikes\`). Plugins go in the game installation, not Documents.

**Black Screen on Game Startup**
- Ensure the [Visual C++ Redistributable (x64)](https://aka.ms/vc14/vc_redist.x64.exe) is installed
- Restart your computer after installing - the runtime may not load until after a reboot

**Elements Appearing Twice (Ghost/Duplicate)**
- Check for duplicate `mxbmrp3.dlo` files - only ONE should exist in your plugins folder

**Elements Overlapping**
- Drag elements to reposition them
- Use settings menu to adjust scale

**Controller Not Working**
- If you accidentally deleted `xinput64.dli` from the plugins folder, controller input may stop working
- To restore: verify game files integrity (Steam) or reinstall MX Bikes

For bug reports or feature requests, open an issue on [GitHub](https://github.com/thomas4f/mxbmrp3/issues).

## Development

Built with C++17, Visual Studio 2022, MX Bikes Plugin API, and Claude Code.

- [`CLAUDE.md`](CLAUDE.md) - Quick-start guide for developers and AI assistants
- [`ARCHITECTURE.md`](ARCHITECTURE.md) - Comprehensive technical documentation with diagrams

### Building from Source

**Requirements:** Visual Studio 2022+, Windows SDK 10.0, Platform Toolset v143

1. Clone the repository:
   ```bash
   git clone https://github.com/thomas4f/mxbmrp3.git
   cd mxbmrp3
   ```
2. Open `mxbmrp3.sln` in Visual Studio 2022
3. Select **Release** configuration (or Debug for development)
4. Build the solution (Ctrl+Shift+B)
5. Output: `build/Release/mxbmrp3.dlo`

### Roadmap

Ideas under consideration (no guarantees): extended telemetry (g-force, lean angle), event log, HTTP data export for OBS overlays.

---

Licensed under the [MIT License](LICENSE). Feedback and contributions are welcome.
