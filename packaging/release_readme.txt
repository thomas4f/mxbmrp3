# MXBMRP3 v__VERSION__

A free, open-source plugin for MX Bikes, GP Bikes, and Kart Racing Pro
with customizable on-screen displays, immersion features, social tools, and
streaming/broadcasting overlays.

If you'd rather use the guided installer, download mxbmrp3-Setup.exe
from the GitHub releases page instead.

## Manual Installation

1. Download the latest release archive mxbmrp3.zip
2. Find your game's plugins folder:
   - Steam: Right-click the game in your library > Manage > Browse local files >
     open plugins
   - Standalone: Navigate to your game installation folder
     (e.g., C:\Program Files\[Game]\) > open plugins
3. Extract the plugin files:
   - Copy the DLO for your game to the plugins\ folder:
     - mxbmrp3.dlo for MX Bikes
     - mxbmrp3_gpb.dlo for GP Bikes
     - mxbmrp3_krp.dlo for Kart Racing Pro
   - Copy the mxbmrp3_data\ folder to the plugins\ folder
   - Do not delete the game's own files (proxy64.dlo, proxy_udp64.dlo,
     xinput64.dli, or telemetry64.dlo for GP Bikes) - they are native game
     files, not old plugin versions

   Your directory should look like this after installation (files vary slightly
   by game):
   [Game]/
   │   mxbikes.exe / gpbikes.exe / kart.exe
   │   ...
   │
   └───plugins/
       ├── mxbmrp3_data/        ← Add this folder (from release)
       ├── mxbmrp3.dlo          ← Add this (MX Bikes only)
       ├── mxbmrp3_gpb.dlo      ← Add this (GP Bikes only)
       ├── mxbmrp3_krp.dlo      ← Add this (Kart Racing Pro only)
       ├── proxy_udp64.dlo      ← Keep (native game file)
       ├── proxy64.dlo          ← Keep (native game file)
       ├── xinput64.dli         ← Keep (native game file)
       └── telemetry64.dlo      ← Keep (GP Bikes only)

## Links

- Documentation:         https://thomas4f.github.io/mxbmrp3
- Community discussion:  https://mxb-mods.com/mxbmrp3
- Source & issues:       https://github.com/thomas4f/mxbmrp3
- Say thanks:            https://ko-fi.com/thomas4f
