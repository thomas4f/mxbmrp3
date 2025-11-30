A HUD plugin for MX Bikes that displays real-time race information, standings, and telemetry data.

![MXBMRP3 HUD Screenshot](./mxbmrp3.png)
*Example HUD layout showing standings, map, telemetry, and widgets. All elements are fully customizable.*

**Note:** First public release - stable but may still have undiscovered issues.

## Features

- Live race standings with gap times and rider info
- Interactive track map with real-time positions
- Fully customizable layout, scale, and visibility for all HUD elements
- Widgets for lap, position, speed, and more
- Telemetry visualization (inputs, suspension, RPM, gear)
- Controller input display
- Drag-and-drop positioning with auto-save
- Minimal performance impact

## Installation

### Manual Installation

1. Download the latest release archive from [Releases](https://github.com/thomas4f/mxbmrp3/releases)
2. Extract all files to your MX Bikes plugins folder. Your directory should look something like this:
   ```
   MX Bikes/
   │   mxbikes.exe
   │   ...
   │
   └───plugins/
       |   xinput64.dli
       │   mxbmrp3.dlo
       │
       └───mxbmrp3_data/
               *.tga
               *.fnt
   ```
3. Ensure you have [Microsoft Visual C++ Redistributable](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist) installed
4. Launch MX Bikes - the HUD will automatically appear during sessions

### Automatic Installation

1. Download the latest installer (`mxbmrp3-vX.X.X.X-Setup.exe`) from [Releases](https://github.com/thomas4f/mxbmrp3/releases)
2. Run the installer - it will:
   - Auto-detect your MX Bikes installation (Steam or standalone)
   - Install to the correct plugins folder
   - Check for and offer to install Visual C++ Redistributable if needed
   - Handle upgrades automatically (preserves your settings)
3. Launch MX Bikes - the HUD will automatically appear during sessions

## Controls

### Mouse
- **Move Mouse** - Show cursor and settings button (auto-hides after 2 seconds)
- **Right Click & Drag** - Reposition HUDs
- **Settings Button** - Click the `[=]` button in top-right to open/interact with settings menu

### Keyboard Shortcuts
- **F1** - Toggle Standings HUD
- **F2** - Toggle Map HUD
- **F3** - Toggle Pitboard HUD
- **F4** - Toggle Lap Log HUD
- **F5** - Toggle Session Best HUD
- **F6** - Toggle Telemetry HUD
- **F7** - Toggle Input HUD
- **F8** - Toggle Performance HUD
- **F9** - Toggle all Widgets
- **`** (backtick/tilde key - below ESC, left of 1) or **\\** - Toggle settings menu
- **Ctrl+`** or **Ctrl+\\** - Temporarily toggle ALL HUDs on/off

## HUDs

### Standings HUD
Displays live race positions with detailed rider information:
- Position, race number, rider name
- Bike brand (color-coded by manufacturer)
- Current status (lap count, finished, DNS/DNF/DSQ, pit, penalties)
- Gap times (official split-based and live timing)
- Pagination that focuses on player position
- Click rider names to follow them (spectating/replay mode)

### Map HUD
Top-down track map showing:
- Track layout with configurable rotation
- Rider positions in real-time
- Color-coded rider dots
- Configurable labels and track width

### Pitboard HUD
Pitboard-style information display:
- Rider ID (race number and name)
- Position and current lap
- Session time
- Accumulated split times at splits, lap time at finish
- Gap to leader
- Display modes: Always visible, Pit area only, or Splits triggered

### Lap Log HUD
Historical lap times and performance:
- Lap-by-lap time listing
- Personal best indicators

### Session Best HUD
Quick reference for session records:
- Best lap time
- Ideal lap time
- Best sector times (S1, S2, S3 - individual sector durations, not accumulated)
- Current session information

### Telemetry HUD
Real-time bike data visualization:
- Throttle, brake, clutch inputs
- Suspension travel
- RPM, gear, and fuel
- Customizable graphs or numeric display

### Input HUD
Visual representation of controller inputs:
- Real-time input display
- Useful for streaming or analysis

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
9. **Timing Widget** - Real-time accumulated split/lap times with gap comparison (appears center-screen for 3 seconds after crossing splits)
10. **Notices Widget** *(experimental)* - Displays "WRONG WAY" and blue flag warnings

## Configuration

All HUD positions, scales, and visibility settings are automatically saved between sessions.

Use the settings menu (backtick/tilde key or `[=]` button) to:
- Toggle HUD visibility
- Adjust scale and opacity
- Configure displayed data columns/rows
- Reset to defaults

## Troubleshooting

**HUD Not Appearing**
- Verify `mxbmrp3.dlo` and `mxbmrp3_data/` directory are in `[MX Bikes]/plugins/` folder (see [Manual Installation](#manual-installation) for expected structure)
- Requires MX Bikes Beta 20 or newer
- **Requires Microsoft Visual C++ Redistributable** (vc_redist) - Download from [Microsoft](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist)

**HUDs Appearing Twice (Ghost/Duplicate)**
- Check for duplicate `mxbmrp3.dlo` files - only ONE should exist in your plugins folder

**HUDs Overlapping**
- Drag HUDs to reposition them
- Use settings menu to adjust scale

## Support

- **Bug Reports**: [GitHub Issues](https://github.com/thomas4f/mxbmrp3/issues)

## Documentation

- [`CLAUDE.md`](CLAUDE.md) - Quick-start guide for developers and AI assistants
- [`ARCHITECTURE.md`](ARCHITECTURE.md) - Comprehensive technical documentation with diagrams

## Roadmap

Ideas under consideration (no guarantees):

- Save/load HUD layout presets
- Customizable colors and themes
- Configurable keyboard shortcuts
- Fuel usage tracking and estimates
- Persistent session history (personal bests, total distance)
- Extended telemetry (g-force, lean angle)
- Event log (fastest laps, penalties, DSQs, etc.)
- HTTP data export for OBS overlays
- Replay/broadcasting overlay mode

## Building from Source

### Requirements
- Visual Studio 2022 (or newer)
- Windows SDK 10.0
- Platform Toolset v143

### Build Steps

1. Clone the repository:
   ```bash
   git clone https://github.com/thomas4f/mxbmrp3.git
   cd mxbmrp3
   ```

2. Open `mxbmrp3.sln` in Visual Studio 2022

3. Select **Release** configuration (or Debug for development)

4. Build the solution (Ctrl+Shift+B)

5. Output: `build/Release/mxbmrp3.dlo`

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## Built With

- C++17
- Visual Studio 2022
- MX Bikes Plugin API
- Claude Code

---

Feedback and contributions are always welcome.
