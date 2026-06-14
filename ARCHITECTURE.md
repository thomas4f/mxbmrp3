# MXBMRP3 Architecture Guide

This document explains how the MXBMRP3 plugin works, from the ground up. It's designed to help new contributors understand the codebase quickly.

## What Is This Project?

MXBMRP3 is a **HUD (Heads-Up Display) plugin** for PiBoSo racing simulators (MX Bikes, GP Bikes, WRS, KRP). The plugin displays real-time racing information on screen: lap times, standings, speedometer, track map, and more.

The plugin is a Windows DLL (with `.dlo` extension) that each game loads at startup. The game calls our exported functions to send us data and request rendering instructions. A **multi-game translation layer** allows the same core code to work across all supported games.

## Project Structure

```
mxbmrp3/
├── mxbmrp3/                    # Main plugin source code
│   ├── vendor/piboso/          # Game API definitions and exports
│   │   ├── mxb_api.h/.cpp      # MX Bikes API header and DLL exports
│   │   ├── gpb_api.h/.cpp      # GP Bikes API header and DLL exports
│   │   ├── krp_api.h/.cpp      # Kart Racing Pro API header and DLL exports
│   │   └── wrs_api.h           # WRS API header (stubbed)
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
│   │   ├── stats_manager.*     # Unified stats, personal bests, odometers (JSON)
│   │   ├── asset_manager.*     # Dynamic asset discovery (fonts, textures, icons)
│   │   ├── font_config.*       # User-configurable font categories
│   │   ├── color_config.*      # User-configurable color palette
│   │   ├── fmx_manager.*       # FMX trick detection and scoring
│   │   ├── fmx_types.h         # FMX data structures and enums
│   │   ├── http_server.*       # Embedded HTTP server with SSE streaming
│   │   ├── event_log_types.h   # Event log entry types and filter flags
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
│   └── web/                    # Web overlay (HTML/CSS/JS served by HttpServer)
│       └── logos/              # Logo slideshow PNGs (auto-detected by /api/logos)
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
              ├──────────────────────────┐
              ▼                          ▼
     ┌─────────────────┐       ┌─────────────────┐
     │   HudManager    │       │   HttpServer    │
     │                 │       │                 │
     │ Owns all HUDs,  │       │ Builds JSON on  │
     │ marks dirty,    │       │ game thread,    │
     │ collects output │       │ streams via SSE │
     └─────────────────┘       └─────────────────┘
              │                          │
              ▼                          ▼
     ┌─────────────────┐       ┌─────────────────┐
     │      HUDs       │       │  Web Overlay    │
     │                 │       │  (Browser/OBS)  │
     │ Build quads &   │       │                 │
     │ strings for     │       │ Standings tower │
     │ rendering       │       │ Event log       │
     └─────────────────┘       │ Focus card      │
              │                └─────────────────┘
              │ returns render data
              ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                    GAME ENGINE (MX Bikes / GP Bikes / etc.)             │
│                          (Renders our output)                           │
└─────────────────────────────────────────────────────────────────────────┘
```

## Core Components

### 1. The Plugin API (`vendor/piboso/*_api.*`)

Each PiBoSo game defines a C API that plugins must implement. The APIs are nearly identical, with game-specific struct variations. Each game has its own API file:
- `mxb_api.h/.cpp` - MX Bikes
- `gpb_api.h/.cpp` - GP Bikes
- `krp_api.h/.cpp` - Kart Racing Pro
- `wrs_api.h` - WRS (header only, stubbed)

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

**Exception barrier (`vendor/piboso/api_guard.h`):** Every DLL export wraps its body in `API_GUARD_CATCH("ExportName")`. The host game doesn't support C++ exceptions across the DLL boundary, so any uncaught throw from PluginManager downward would terminate the host process. The macro catches `std::exception` and `...` at the boundary, logs via `DEBUG_WARN_F`, and returns a sensible fallback value. When adding a new export, follow the same pattern.

**Boundary validation (version skew):** PiBoSo has reshaped plugin structs between game versions before — the `EventInit`/`RaceCommunication` defensive copies exist for exactly that — so the array-style callbacks don't trust the game's framing. `RaceClassification`/`RaceTrackPosition`/`SpectateVehicles` reject a mismatch between the game-supplied `_iElemSize` and the compiled `sizeof` (warn-**once**, then return — the feature fails safe instead of indexing with the wrong stride, which misreads every entry past index 0 and runs off the real array), null-check `_pData`/`_pArray` when counts are positive, guard the defensive-copy `memcpy` against a null `_pData`, and `std::clamp` entry counts to `0..MAX_RACE_ENTRIES` (clamping negatives too, not just capping from above). Applied identically across MXB/GPB/KRP. The symptom of skew is empty standings/map plus a single "element size N != expected M" log line. New array-style callbacks must follow the same pattern.

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
- `m_raceStartPositions` - Per-rider starting grid position (`raceNum → position`), snapshotted when a race goes green; drives the positions-gained/lost column. Cleared on each new session.

**Per-rider map lifecycle.** PluginData holds many maps keyed by raceNum (`m_standings`, `m_riderLapLog`, `m_lastValidOfficialGap`, `m_raceStartPositions`, `m_lastSfPositions`, `m_lastSplitPositions`, `m_cachedHazardTypes`, …). Every such map must be **erased in `removeRaceEntry()`** (per-rider teardown) **and reset in `clear()`** (session/event teardown). The memory is trivial; the real hazard is **raceNum reuse** — the game can hand a departed rider's number to a new joiner mid-event, who would otherwise transiently inherit the old rider's standings entry, gap cache, and position-gain reference points until the next classification overwrote them. A batch of six maps was found reset in `clear()` but not erased in `removeRaceEntry()`, so adding a new per-rider map means wiring up *both* sites.

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

**XInputReader (`core/xinput_reader.*`) — send policy & connection cost.** XInputReader runs every telemetry tick (~100Hz) on the game thread, so its two Win32 call sites are tuned against real hardware behavior rather than the spec:

- *Empty-slot cost.* `XInputGetState` on a **disconnected** slot triggers device enumeration and can cost milliseconds on degraded driver/wrapper/Bluetooth stacks. The 4-slot connection scan (which only feeds the settings-UI controller list) is throttled to once per second, and the selected slot backs off to one poll per `DISCONNECTED_POLL_INTERVAL_MS` (500ms) while unplugged — so an idle, controller-less session isn't paying 4–5 slow syscalls per tick. Switching controller index resets the backoff so a new selection polls immediately. Don't reintroduce per-tick empty-slot polling.
- *Rumble send policy (`setVibration`).* Empirically derived from field reports; do **not** "simplify" back to value-dedup. Two opposing hardware facts shape it: (1) controllers **decay** rumble without a continuous feed — a constant nonzero value sent once stops buzzing after a moment, so the "XInput state persists until changed" assumption is false in practice; (2) Bluetooth pads **choke** on sustained 100Hz `XInputSetState` traffic — each call is a radio transaction, the queue backs up, and FPS progressively collapses over a few laps (recovering instantly on USB). So: nonzero values are re-sent every `send_interval_ms` (INI `[Rumble]`, default 16ms ≈ 60Hz) **even when unchanged** — the keepalive the motors need, at a rate the BT link sustains; an all-zero (idle) state is sent once then silenced — no traffic from a resting pad; and a transition to zero **bypasses** the rate cap so a final `stopVibration()` before telemetry halts can never be swallowed (which would leave the motors running). Values are quantized to 8 bits (the BT protocol's per-motor resolution) so jitter doesn't defeat the idle check.

### 8. StatsManager (`core/stats_manager.*`)

Unified stats system that tracks per-track/bike stats, global race stats, personal bests, and odometer data in a single JSON file (`{save_path}/mxbmrp3/mxbmrp3_stats.json`).

**Per track+bike stats** (`TrackBikeStats`):
- Total/valid lap counts, best lap/sector times
- Top speed, crash count, time on track
- First/last session timestamps

**Personal bests** (`StatsPersonalBestData`):
- Fastest lap time with sector breakdown per track+bike combo
- Metadata: setup name, weather conditions, timestamp
- Used by TimingHud, LapConsistencyHud, RecordsHud for all-time PB comparisons

**Global stats** (`GlobalStats`):
- Race count, podium finishes (P1/P2/P3), fastest lap awards, penalty count, Breakout high score

**Per-bike odometers**:
- Total distance traveled per bike (persistent across sessions)
- Uses `double` precision for accuracy at high distances (100k+ km)
- Distance calculated from speed × delta time in telemetry handler

**Session transients** (not persisted):
- Session lap count, best lap, crash count, top speed, trip distance, duration

Features:
- Context-based API: set current track+bike once, then telemetry-rate calls avoid lookups
- Migrates legacy data from old `mxbmrp3_personal_bests.json` and `odometer.json` files
- Cached global totals (recomputed on load/clear, updated incrementally)
- Dirty flag with periodic save (not every telemetry tick)

**Non-finite hardening.** The persisted floats (per-bike odometer, `totalDistanceM`, `topSpeedMs`) are integrated from `speed × dt` and gated by `>=`/`>` movement/record comparisons. Those comparisons reject NaN but **not `+Inf`**, so a single non-finite speed sample from a physics glitch would integrate into the odometer and top speed — *persisted* state that never recovers without hand-editing the JSON. `updateTelemetry` sanitizes the sample at the top (`!std::isfinite` → treated as `0`, so crash/gear edge detection still runs that tick), and the three floats are clamped through `finiteOrZero()` (a file-static helper in `stats_manager.cpp`) on load so an already-corrupted file **heals** instead of re-adopting the bad value. Any new persisted float needs the same guard at both write and load.

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

### 10. HttpServer (`core/http_server.*`)

Embedded HTTP server that streams race data to browser-based overlays (OBS browser source):

**Threading model:**
- JSON snapshot built on the **game thread** in `buildJsonSnapshot()` (PluginData is not thread-safe)
- Cached string protected by mutex, read by SSE server threads
- `onDataChanged()` called by PluginData's notification system (same path as HudManager)
- Per-client sequence tracking prevents multi-client wake races

**SSE streaming (`/api/events`):**
- Pushes JSON snapshots on data changes (standings, events, session, spectate target)
- Throttled per-connection (default 250ms) to avoid flooding
- Comment keepalives detect dead connections
- Max 3 concurrent SSE connections (prevents thread pool starvation)

**JSON data contract** (raw data, no filtering — web UI filters client-side):
- `session` - Type, state, palette colors, font names, track info, plus `time`: the MM:SS countdown, or — once a **time+lap** race's clock expires — a leader-relative overtime label (`N TO GO` / `FINAL LAP` / `CHECKERED`). The label is single-sourced by `PluginData::getLeaderLapsToGo()` (uses the same thresholds as `isRiderFinished`/the FinalLap event) + `PluginUtils::formatSessionClock()`, which also feed the in-game StandingsHud title and TimeWidget so all three read identically.
- `standings[]` - Per-rider: pos, num, name, gap, state, `lastLapMs`/`bestLapMs`, all chips, per-reference positions-gained/lost deltas (`posDeltaStart` vs race start, `posDeltaSf` vs last S/F, `posDeltaSplit` vs last split — each present only during races, once its reference exists), and `plateColor` (the rider's tracked-rider plate color, emitted only when tracked). The overlay chooses what to show client-side.
- `events[]` - All event log entries with clock/session timestamps and type enum

**Static file serving:**
- Mounts `plugins/mxbmrp3_data/web/` at `/` — users can freely customize the HTML/CSS/JS
- Web overlay syncs colors and fonts from in-game settings via CSS custom properties (`--gp-*`); the look is otherwise driven by `:root` tokens in `style.css` (palette/fonts/sizes/spacing/animation), overridable via `custom.css` (see `custom-sample.css`)
- `GET /api/logos` — scans `web/logos/` for PNGs, returns sorted JSON array for the logo slideshow
- The overlay's standings tower has a shared **bottom slot** cycling several broadcast panels (fastest-last-lap, fastest-laps, down-the-order, battle) via a `createSlotPanel` controller in `app.js` (mutually exclusive, client-side); append `?demo` to the overlay URL to replay a synthetic race with no plugin connection

**Zero-client gating (game-thread cost):** `onDataChanged()` builds the full JSON snapshot (tens of KB of string work) on the game thread. `Standings` changes fire from every `RaceTrackPosition` callback, so on a full grid with OBS closed that was many wasted builds per second. The build is gated on **client activity** — `hasActiveClients()`, i.e. a live SSE connection or an `/api/state` poll within the last 5s; while inactive the cache is just marked stale, and the first notification after a client appears rebuilds it (one telemetry tick in-session). The gate is **split by change-type frequency**, and the split is load-bearing: high-frequency types (`Standings`, `EventLog`) are gated, but the **rare transition types** (`SessionData`, `RaceEntries`, `SpectateTarget`) **always** rebuild, client or not. Why: the plugin receives **no callbacks at all while the player sits in menus** (the game stops calling it), so every quiet period is *entered* via a rare-type change — if that snapshot were skipped, a client connecting later would be served a stale in-session snapshot with no rebuild opportunity ever arriving. Don't move the rare types behind the gate, and keep this no-callbacks-in-menus constraint in mind for anything that tries to defer work "to the next game-thread tick."

**Feature gating:**
- Compile-time: `GAME_HAS_HTTP_SERVER` flag in `game_config.h`
- Runtime: user toggle in settings (starts/stops server on demand)

### 11. Event Log System (`core/event_log_types.h`, `hud/event_log_hud.*`)

Timestamped feed of race events, used by both the in-game HUD and web overlay:

**Event types** (defined in `event_log_types.h`):
- Session events: started, state changes, overtime, final lap
- Rider events: finished, retired, DNS, DSQ, leader change
- Action events: penalty, penalty clear, pit entry, pit exit, fastest lap

**Filter flags:**
- Each event type has a bitmask flag (`EVENT_SESSION_STARTED = 1 << 0`, etc.)
- `EventLogHud::getEnabledEvents()` returns the user's filter selection
- Used by in-game rendering to filter displayed events (web overlay filters client-side via `CONFIG.events` in `app.js`)

**Storage:**
- Ring buffer in PluginData (`MAX_EVENT_LOG_CAPACITY = 100`)
- Each entry: message, detail, type enum, session time, system clock time

### 12. CrashHandler (`core/crash_handler.*`)

Top-level Structured Exception Handling (SEH) filter for unhandled hardware faults: access violations, stack overflows, divide-by-zero, illegal instructions. These faults live below the C++ exception system: `catch (...)` doesn't intercept them, so they would otherwise crash the host without leaving any diagnostic context behind. The CrashHandler complements the C++ exception barrier at the DLL boundary by handling the failure modes the C++ machinery can't reach.

**What it does:**
- Installed via `SetUnhandledExceptionFilter` in `PluginManager::initialize()`, right after `Logger::initialize`
- On any unhandled SEH fault in the host process, writes a minidump to `<savePath>\mxbmrp3\crashes\mxbmrp3_crash_<date>_<time>_<pid>.dmp`
- Chains to the previously-installed filter (typically the host's own or the OS default), so MX Bikes' crash dialog / Windows Error Reporting still runs
- Uninstalled in `PluginManager::shutdown()` so the OS doesn't hold a function pointer into our DLL after unload

**Minidump contents:**
- `MiniDumpNormal | MiniDumpWithThreadInfo | MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithUnloadedModules`
- Includes exception record (code, address, context), thread stacks, module list, heap pages locals point into
- Deliberately excludes full memory (would produce multi-GB dumps)

**Design constraints inside the filter:**
- The heap may be corrupt at fault time, so the filter uses only stack-allocated buffers and Win32 calls. No `std::string`, no `new`, no `Logger`.
- `dbghelp.lib` is linked implicitly via `#pragma comment(lib, "dbghelp.lib")` so the DLL is mapped before any crash, not lazily loaded inside the filter.
- Re-entry guard via `InterlockedExchange(&s_dumping, 1)` prevents infinite recursion if `MiniDumpWriteDump` itself faults. The same guard also serializes concurrent SEH faults across threads.
- The filter explicitly does NOT call `Logger::warn()`. `Logger::log()` holds a mutex, and `MiniDumpWriteDump` suspends other threads to walk their stacks; if any thread held the log mutex at fault time, the filter would wedge.
- Transactional install/uninstall: `PluginManager::initialize()` wraps everything after `CrashHandler::install` in `try/catch(...)`. If init throws, the catch uninstalls the filter and rethrows. Otherwise the game would unload the DLL while the OS still held a function pointer into it.

**What it does NOT do:**
- Prevent crashes. It runs *after* a fault has fired and the process is already going down. It just leaves a `.dmp` behind for debugging.
- Catch C++ exceptions. That's the API guard's job (`vendor/piboso/api_guard.h`).

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
- `StandingsHud` - Race standings table with columns (incl. optional positions-gained/lost column, races only)
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
- `SessionHud` - Session info (server name as the headline, format & state, track, weather)
- `StatsHud` - Session stats display with configurable columns (last lap, session, all-time)
- `NoticesHud` - Race status notices (wrong way, blue flag, PB alerts, final lap, finished)

**Overlays** (full-screen, telemetry-driven):
- `HelmetOverlayHud` - First-person helmet overlay with visor tint, tilt (lean angle) and vibration (suspension). Global settings in `[HelmetOverlay]` INI section. Registered first to draw behind all other HUDs.

**Widgets** (simple, focused):
- `SpeedWidget` - Speed and gear display
- `PositionWidget` - Current race position (P1, P2...)
- `LapWidget` - Current lap number
- `TimeWidget` - Session time remaining
- `ClockWidget` - Real-time clock
- `GearWidget` - Current gear indicator
- `SpeedoWidget` - Analog speedometer dial
- `TachoWidget` - Analog tachometer dial
- `BarsWidget` - Visual telemetry bars (throttle, brake, etc.)
- `LeanWidget` - Bike lean/roll angle display with arc gauge and steering bar
- `GForceWidget` - Lateral/longitudinal G-force gauge with peak marker
- `FuelWidget` - Fuel calculator with consumption tracking
- `TyreTempWidget` - Front and rear tyre tread temperatures (GP Bikes only)
- `EcuWidget` - Electronic rider aids: engine map, traction control, engine braking, anti-wheeling (GP Bikes only)
- `GamepadWidget` - Controller visualization with button/stick/trigger display
- `VersionWidget` - Plugin version display (includes hidden Breakout game easter egg; high score persisted via StatsManager)
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

**Font format & text encoding.** The game's `.fnt` bitmap fonts are a **byte-indexed 256-glyph table** built from CP1252 (see `fontgen.cfg`: `code_page = 1252`, glyphs 32–255). The renderer indexes by raw byte, so it cannot render UTF-8 — multi-byte rider names garble regardless of any truncation logic, which makes UTF-8-safe truncation *in-game* moot. The web overlay is the only UTF-8-aware renderer and handles names client-side. `m_szString` is `char[100]`, so in-game strings are also length-bounded by the struct.

**Header/label convention.** Table column headers and axis labels go through `BaseHud::addLabel()` — the STRONG font at the *Small* size, vertically centered in the row via `labelRowYOffset()` — rather than a hand-rolled `addString` at data-font size. FriendsHud (column headers) and FmxHud (rotation-arc Pitch/Yaw/Roll labels) both deviated and were brought in line; new HUDs should use the helper.

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
- On plugin shutdown (and on change, when Auto-Save is enabled)
- File location: `{game_save_path}/mxbmrp3/mxbmrp3_settings.ini`

**Parse robustness.** Hand-editing the INI is a supported workflow (`auto_save` off, then the RELOAD_CONFIG hotkey), so **every** value-parsing site in `loadSettings()` must be exception-guarded — a single naked `std::stoul`/`std::stof` on a typo'd value throws out of the loader and aborts the parse mid-file, leaving the plugin half-configured for the session. The one offender found was the v4 base-section color path calling `parseColorHex` (a bare `std::stoul`) without a `try/catch`. `parseColorHex` itself stays a thin wrapper, so the guard belongs at each call site, wrapping the whole section's branch.

#### Per-profile vs global sections

Settings fall into two kinds, persisted differently:

- **Per-profile** — each HUD/widget has a base `[HudName]` section (the defaults) plus
  optional `[HudName:Practice|Qualify|Race|Spectate]` override sections. Saving is **sparse**:
  a profile section only contains the keys that *differ* from the base. There are four
  profiles (`ProfileType`: Practice, Qualify, Race, Spectate) that auto-switch with the
  session type.
- **Global** (single value, not per-profile) — `[General]`, `[Updates]`, `[Advanced]`,
  `[Display]`, `[Colors]`, `[Fonts]`, `[Rumble]`, `[HelmetOverlay]`, `[Hotkeys]`. These are
  owned by singletons (UiConfig, ColorConfig, UpdateChecker, etc.), not by a profile.

#### One serialization, three consumers (save / load / reset)

Both save and load route global sections through a **single pair** of functions, so they can't
drift as settings are added:

- `writeGlobalSettings(ostream&)` — the sole emitter for every global section. Used by
  `saveSettings()` *and* by `captureFactoryDefaults()` to snapshot defaults at startup.
- `applyGlobalLine(section, key, value)` — the sole applier. Used by `loadSettings()` *and*
  by the reset paths.

**Reset = replay the factory snapshot through the same applier.** At startup (before the
user's INI is parsed, while every singleton holds its constructor defaults),
`captureFactoryDefaults()` captures two snapshots:

- `m_globalDefaultsIni` — global sections as INI text. `resetGlobalsToFactoryDefaults()`
  (full reset) and `resetGlobalSectionsToFactoryDefaults({...})` (per-tab reset for tabs
  that map 1:1 to a section) replay it via `applyGlobalLine`.
- `m_hudFactoryDefaults` — pristine per-HUD constructor defaults. The per-HUD reset paths
  (`resetAllToFactoryDefaults`, `resetHudsToFactoryDefaults`, `resetActiveProfileToFactoryDefaults`)
  replay *this*, **not** `m_hudDefaults`.

> Why two HUD caches? `m_hudDefaults` is the sparse-save baseline and has the user's
> hand-edited base `[HudName]` keys *folded in* at load (so they round-trip). That makes it
> the wrong source for "reset to defaults" — it would restore the file's baseline (or, after a
> plugin upgrade, an *old* version's default) instead of this build's. `m_hudFactoryDefaults`
> is captured before any folding, so reset always means this build's defaults. Don't collapse
> the two. (Migration note: legacy keys are read from their old section as a fallback and
> migrate to the new section on next save — e.g. update keys `[General]`/`[Advanced]` →
> `[Updates]`, units `[General]` → `[Display]`.)

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

Tooltips provide contextual help when hovering over controls. Strings are
compiled into the plugin (no external file).

**TooltipManager** (`core/tooltip_manager.h`) is a header-only singleton that:
- Holds two static `unordered_map<string, const char*>` tables (tabs, controls)
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

**Gate on the flags, not the frame.** A HUD must rebuild only when `isDataDirty()`/`isLayoutDirty()` is set, never unconditionally per frame (unless the rebuild is trivially cheap). TelemetryHud was re-tessellating ~1600 line segments (each with a `sqrt`) every frame at 240fps for data that only changes at the 100Hz telemetry rate, so more than half the rebuilds produced identical output. (Becoming visible sets data-dirty via `BaseHud::setVisible`, so the first rebuild is unaffected.) The same proportionality applies to input polling: `HotkeyManager` refreshes only the *bound* keys each frame, doing the full 256-key `GetAsyncKeyState` sweep only while capturing a new binding.

**The visibility/dirty flags are atomic.** `m_bVisible`, `m_bDataDirty`, and `m_bLayoutDirty` are `std::atomic<bool>` — and `setDataDirty()` writes *both* dirty flags. Background workers legitimately mark HUDs dirty: the RecordsHud fetch thread flags itself and TimingHud on completion, and the update-checker/downloader callbacks reach `VersionWidget::showUpdateNotification` (`m_bVisible` + the atomic `m_showingUpdateNotification`) and `SettingsHud::setDataDirty`. The reads happen every frame on the game thread; plain bools made that a data race (benign on x86-64 but UB). Keep any flag written cross-thread atomic.

**Second-level render caches key on their inputs.** Where a rebuild is dominated by sub-geometry that *doesn't* change every rebuild, cache it keyed on everything that affects its output. MapHud's `renderTrack()` does this: every `RaceTrackPosition` marks the map dirty, but with rotation/zoom off the track ribbon is bit-identical between rebuilds, so its two tessellation passes are cached in `m_ribbonQuads` keyed by `TrackRibbonKey` (rotation, render bounds, scales, HUD offset, clip rect, LOD, zoom params, title row, the two colors — every input baked into the emitted quads). **Any new input to the ribbon output must be added to the key**, or the cache serves stale geometry. In rotate-to-player/zoom-follow modes the key changes every rebuild by design, so it's a transparent pass-through there.

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
void NoticesHud::update() {
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
| Hybrid | Polls data but caches state BEFORE dirty check | NoticesHud, GapBarHud |
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

**The `Standings` firehose.** `DataChangeType::Standings` is the highest-frequency notification: `updateRealTimeGaps()` runs on every `RaceTrackPosition` callback, and the per-rider `GAP_UPDATE_THRESHOLD_MS` (100ms) filter is structurally defeated on full grids — leader timing is quantized to 100 points per lap, so a gap steps by ~lapTime/100 (well above the threshold) whenever *any* rider crosses a quantization boundary, which on a 30+ grid is nearly every callback. Left unchecked, that rebuilt every table HUD (Standings/Timing/Pitboard/Friends) every frame during close racing. So the notification is **time-coalesced** to at most one per `gapNotifyIntervalMs` (default 100ms): a skipped notify is carried in `m_gapNotifyPending` and flushed by a later call, so the final change is never dropped. MapHud/RadarHud are unaffected — they rebuild from their own `updateRiderPositions` path.

**New consumers must respect the firehose.** Any new `onDataChanged` consumer beyond the HUDs sits on this hot path and must be trivially cheap *or* short-circuit before any string/alloc work, gated on whether its output is even consumed: `HttpServer` gates the snapshot build on `hasActiveClients()` (see HttpServer above), and `SteamFriendsManager::updateLocalPresence` fingerprints its raw inputs in a POD `PresenceInputs` compare and returns before building ~10 strings when nothing changed (session time bucketed per second, the finest granularity the self-row clock displays).

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

**INI-only tuning knobs.** A few power-user settings have no in-game control and are edited directly in the INI (documented inline, clamped on load, reset covered by the global-snapshot replay). Two were added for the performance work:

- `[Rumble] send_interval_ms` (4–200, default 16) — the continuous-rumble-feed cadence cap. Lower = more responsive; higher = less Bluetooth traffic on degraded stacks. Global (on XInputReader), never per-bike, since send cadence is a transport property, not an effect preference.
- `[Advanced] gapNotifyIntervalMs` (0–1000, default 100) — live-gap HUD refresh coalescing (see *Data Change Notifications*). `0` restores notify-on-every-change for anyone who prefers per-frame gap updates over frame budget.

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

3. **C++ exceptions must not cross the DLL boundary** - The host game terminates if a C++ exception escapes a DLL export. Every export in `vendor/piboso/*_api.cpp` wraps its body in `API_GUARD_CATCH` (see `vendor/piboso/api_guard.h`). When adding a new export, follow the same pattern. Similarly, every `std::thread` body (HttpServer, UpdateChecker, UpdateDownloader, DiscordManager, `RecordsHud::performFetch`) wraps itself in a top-level try/catch, since an uncaught throw in a `std::thread` calls `std::terminate()`. For hardware faults that don't go through the C++ exception system (null deref, OOB, divide-by-zero), the SEH filter in `core/crash_handler.*` writes a minidump for diagnosis but doesn't prevent the crash.

4. **Game thread vs background threads** - All PiBoSo API callbacks (`Draw`, `RunTelemetry`, etc.) run on the game thread. `PluginData`, `HudManager`, `SettingsManager`, and the various other managers are game-thread-only and not thread-safe. Background threads exist for I/O (HttpServer, DiscordManager, UpdateChecker, UpdateDownloader, RecordsHud's fetch thread) and must NOT touch those singletons directly. They consume snapshots built on the game thread instead (see `HttpServer::buildJsonSnapshot`, `DiscordManager::updateSnapshot`). The `Logger` has its own internal mutex and is safe to call from any thread. Two corollaries for any HUD that grows a worker thread: **(a) a mutex-guarded member is guarded at *every* access site, including private helpers** that look like they're already inside locked code — the crash-grade bug was `RecordsHud::findPlayerPositionInRecords()` iterating the live `m_records` vector unlocked while the fetch thread cleared and reallocated it under `m_recordsMutex`; the fix copies under the lock and passes the snapshot into the helper. **(b) Snapshot game-thread inputs at task start, and join before teardown** — the fetch worker branches on `m_fetchProvider`/`m_fetchTrackName` captured in `startFetch()` (not the live values the game thread mutates when cycling providers, which would parse the response with the wrong schema), and `HudManager::clear()` joins the fetch thread *before* nulling cached HUD pointers, because the worker calls `getTimingHud().setDataDirty()` on completion and would otherwise dereference a null `m_pTiming` on game exit mid-fetch.

5. **Sprite indices are 1-based** - Index 0 means "solid color fill", not "first sprite".

6. **Font indices are 1-based** - Font index 0 is invalid.

7. **Icon ordering is alphabetical** - Icons in `mxbmrp3_data/icons/` are discovered alphabetically. Use filename-based lookups via `AssetManager` for persistence; icon additions/removals won't break saved settings.

## Multi-Game Support

The plugin supports multiple PiBoSo racing games from a single codebase using compile-time game selection.

### Supported Games

| Game | Mod ID | Vehicle Type | Splits | Unique Features |
|------|--------|--------------|--------|-----------------|
| MX Bikes | `mxbikes` | Bike (2 wheels) | 2 | Straight Rhythm |
| GP Bikes | `gpbikes` | Bike (2 wheels) | 3 | ECU/TC/AW, Tread temps |
| WRS | `wrs` | Car (4-6 wheels) | 2 | Rolling start, Turbo, Handbrake |
| KRP | `krp` | Kart (4 wheels) | 2 | Session series, Qualify heats |

### Build Configurations

Each game produces its own DLL:

| Configuration | Output | Install Location |
|---------------|--------|------------------|
| MXB-Release | `mxbmrp3.dlo` | MX Bikes `plugins/` |
| GPB-Release | `mxbmrp3_gpb.dlo` | GP Bikes `plugins/` |
| KRP-Release | `mxbmrp3_krp.dlo` | Kart Racing Pro `plugins/` |
| (future) | `mxbmrp3_wrs.dlo` | WRS `plugins/` |

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
#if GAME_HAS_RACE_SPEED
void handleRaceSpeed(const Unified::RaceSpeedData* data);
#endif
```

**Runtime** (adapter constants):
```cpp
if constexpr (Game::Adapter::HAS_RACE_SPEED) {
    // Show speed trap data
}
```

Key feature flags:
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

When PiBoSo releases a new API version:

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
- Game-specific events (RaceSpeed)

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
| Stats manager | `core/stats_manager.cpp` |
| Rumble profiles manager | `core/rumble_profile_manager.cpp` |
| FMX trick detection | `core/fmx_manager.cpp` |
| FMX types | `core/fmx_types.h` |
| HTTP server | `core/http_server.cpp` |
| Event log types/flags | `core/event_log_types.h` |
| Event log HUD | `hud/event_log_hud.cpp` |
| Web overlay (HTML/CSS/JS) | `mxbmrp3_data/web/` |
| Settings UI | `hud/settings/settings_hud.cpp` |
| Settings layout helpers | `hud/settings/settings_layout.cpp` |
| Settings tabs | `hud/settings/settings_tab_*.cpp` |
| Tooltip definitions | `core/tooltip_manager.h` (embedded) |
| Tooltip manager | `core/tooltip_manager.h` |
| Settings file | `{save_path}/mxbmrp3/mxbmrp3_settings.ini` |
| Stats file | `{save_path}/mxbmrp3/mxbmrp3_stats.json` |
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
| Add new per-HUD setting | Add field + capture/apply in SettingsManager's per-HUD cache; reset is automatic (snapshot) |
| Add new global setting | Add to `writeGlobalSettings()` **and** `applyGlobalLine()` (one emit + one apply); reset is automatic |
| Add settings tab | Create `settings_tab_*.cpp`, add tab enum, register in SettingsHud |
| Add tooltip | Add entry to the maps in `core/tooltip_manager.h`, pass tooltipId to control helper |
| Add keyboard shortcut | Handle in HudManager::processKeyboardInput() |
| Add new handler | Create handler class, route from PluginManager |
| Add new font | Place `.fnt` file in `mxbmrp3_data/fonts/` (auto-discovered) |
| Add new texture | Place `.tga` file in `mxbmrp3_data/textures/` (auto-discovered) |
| Add new icon | Place `.tga` file in `mxbmrp3_data/icons/` (auto-discovered, alphabetical order) |
| Add new event log type | Add enum to `event_log_types.h`, add flag, update `eventLogTypeToFlag()`, add to handlers |
| Add field to web overlay | Add to `buildJsonSnapshot()` in `http_server.cpp`, consume in `app.js` |
| Add game-specific feature | Add to `unified_types.h`, update adapters, add feature flag to `game_config.h` |
| Support new game | Create adapter in `game/adapters/`, add API file in `vendor/piboso/`, update `game_config.h` |
