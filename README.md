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
- [Controls](#controls)
- [Configuration](#configuration)
- [HUDs & Widgets](#huds--widgets)
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

Use the settings menu (Tilde key or `[=]` settings button) to configure all HUDs and widgets. **Hover over any setting to see its description** - all controls have in-game tooltips explaining their function.

The settings menu provides:
- **General** - Profiles, preferences, grid snapping
- **Appearance** - Font categories and color theme customization
- **Hotkeys** - Keyboard and controller bindings
- **Riders** - Track specific riders with custom colors and icons
- **Rumble** - Controller vibration feedback effects
- **Updates** - Check for new versions and install updates in-game
- **Individual HUD tabs** - Per-element visibility, scale, opacity, and options

All settings are automatically saved between sessions.

### Profiles

Four separate profiles store complete HUD layout configurations:
- **Practice** - Practice and warmup sessions
- **Qualify** - Pre-qualify, qualify practice, and qualify sessions
- **Race** - Race 1, Race 2, Straight Rhythm sessions
- **Spectate** - Spectating or viewing replays

Auto-switch (disabled by default) automatically changes profiles based on session type.

## HUDs & Widgets

All HUD and widget settings are documented via **in-game tooltips** - hover over any control in the settings menu to see its description.

### HUDs

| HUD | Description |
|-----|-------------|
| **Standings** | Live race positions with gaps, status, and tracked rider indicators |
| **Map** | Top-down track map with rider positions (click to spectate) |
| **Radar** | Proximity radar with approach alerts and distance arrows |
| **Timing** | Split and lap times with gap comparisons |
| **Gap Bar** | Visual gap-to-PB bar with position markers |
| **Pitboard** | Pitboard-style lap information display |
| **Lap Log** | Historical lap times with PB indicators |
| **Ideal Lap** | Best sector times and theoretical ideal lap |
| **Records** | Online lap records (CBR or MXB-Ranked) with personal bests |
| **Telemetry** | Throttle, brake, suspension graphs |
| **Performance** | FPS and plugin CPU usage |
| **Rumble** | Controller rumble effect visualization |

### Widgets

| Widget | Description |
|--------|-------------|
| **Lap** | Current lap number |
| **Position** | Race position |
| **Time** | Session time/countdown |
| **Session** | Session type |
| **Speed** | Speed and gear |
| **Speedo** | Analog speedometer |
| **Tacho** | Analog tachometer |
| **Bars** | Telemetry bars (throttle, brake, clutch, RPM, suspension, fuel) |
| **Fuel** | Fuel calculator with consumption tracking |
| **Lean** | Bike lean angle with arc gauge |
| **Notices** | Race status notices (wrong way, blue flag, last lap, finished) |
| **Gamepad** | Controller visualization |

## Modding

### Custom Assets

Add custom fonts, textures, and icons by placing files in your MX Bikes **user data folder**:

```
Documents/PiBoSo/MX Bikes/mxbmrp3/
├── fonts/       ← Custom .fnt files
├── textures/    ← Custom .tga textures
└── icons/       ← Custom .tga icons
```

On game startup, the plugin syncs these files to the plugin's data directory (`plugins/mxbmrp3_data/`). User files with the same name as bundled assets will override them. This keeps your customizations separate from the plugin installation, so updates won't overwrite your files. **Restart the game after adding or modifying assets.**

**Textures** use the naming convention `{element_name}_{number}.tga` (e.g., `standings_hud_1.tga`). They're auto-discovered and selectable via the Texture control in each HUD's settings.

**Fonts** (`.fnt` files) are auto-discovered and assignable to categories (Title, Normal, Strong, Marker, Small) in Settings > Appearance. To generate fonts, use the `fontgen` utility provided by PiBoSo. See [this forum post](https://forum.piboso.com/index.php?topic=1458.msg20183#msg20183) for details. An example configuration is provided in [`fontgen.cfg`](fontgen.cfg).

**Icons** (`.tga` files) are discovered alphabetically and available for tracked rider customization.

### Data Files

Plugin data is stored in `Documents/PiBoSo/MX Bikes/mxbmrp3/`:

| File | Description |
|------|-------------|
| `mxbmrp3_settings.ini` | All HUD settings (positions, visibility, options) |
| `mxbmrp3_personal_bests.json` | Personal best lap times per track/bike/category |
| `mxbmrp3_tracked_riders.json` | Tracked riders with colors and icons |

## Troubleshooting

**HUD Not Appearing**
- Check [Installation requirements](#installation) (MX Bikes Beta 20+, Visual C++ Redistributable)
- Verify `mxbmrp3.dlo` and `mxbmrp3_data/` are in the correct `plugins/` folder. MX Bikes has two directories - the **game installation** (contains `mxbikes.exe`) and **user data** (`Documents\PiBoSo\MX Bikes\`). Plugins go in the game installation, not Documents.

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

Ideas under consideration (no guarantees): extended telemetry (g-force), event log, HTTP data export for OBS overlays.

---

Licensed under the [MIT License](LICENSE). See [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md) for bundled asset attributions.

Feedback and contributions are welcome.
