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
│   ├── core/                   # Core infrastructure: the singletons (plugin_manager,
│   │                           #   plugin_data, hud_manager, settings_*, http_server,
│   │                           #   stats_manager, fmx_manager, companion_window,
│   │                           #   crash_handler, ...) plus plugin_constants.h and
│   │                           #   plugin_utils.*. Each file's header states its job.
│   ├── handlers/               # Event processors (one per API callback type)
│   │   ├── draw_handler.*      # Frame rendering and FPS tracking
│   │   ├── event_handler.*     # Event lifecycle (init/deinit)
│   │   ├── run_*_handler.*     # Player-only events
│   │   └── race_*_handler.*    # Multiplayer race events
│   ├── hud/                    # Display components
│   │   ├── base_hud.*          # Abstract base class for all HUDs
│   │   ├── *_hud.*             # Full HUDs (complex, configurable)
│   │   ├── *_widget.*          # Simple widgets (focused display)
│   │   ├── settings_hud.*      # Main SettingsHud (menu build / _input / _render) - in hud/, matches *_hud.*
│   │   └── settings/           # Settings UI helpers (NOT settings_hud.*)
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
├── tests/                      # All automated tests (Layers 1-6)
│   ├── unit/                   #   Layer 1: pure-logic unit tests (doctest, no game)
│   ├── integration/            #   Layers 2 & 3: mingw cross-build + Wine
│   │   ├── harness/            #     PluginHost, tape.h, assertions, doctest
│   │   ├── tests/              #     doctest integration tests (+ fixtures/ tapes)
│   │   └── tapes/              #     full master captures (git-ignored)
│   ├── web/                    #   Layer 4: Playwright overlay tests (?demo)
│   └── asan/                   #   Layer 5: ASan/UBSan memory-safety harness
├── tools/                      # Dev tools. One file = a script, a directory = a tool
│   ├── check_*.py gen_*.py     #   CI checks and fixture/report generators
│   ├── *_report.py             #   Analytics, benchmark, director and minidump analysis
│   ├── replay/                 #   Real-time tape replay / overlay preview (MSVC)
│   ├── fontgen/                #   Portable PiBoSo .fnt bitmap-font generator (MSVC + build.sh)
│   ├── hud_window/             #   Companion-window demo/screenshot harness (headless Wine)
│   ├── spottergen/ themeslice/ #   Spotter-reference and theme-slice generators
│   └── probetheme/ trnfix/     #   Theme cost probe; the trainer repair page
├── assets/                     # Source art (helmet .pdn, icon .svg)
├── crash_analysis/             # Crash catalogue (known_game_crashes.json + docs)
└── CMakeLists.txt              # Gates + the plugin definition (mxbmrp3/CMakeLists.txt);
                                #   generates build/msvc/mxbmrp3.sln for Visual Studio
```
See **[`TESTING.md`](TESTING.md)** for the test layers.

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
              ├──────────────────────────┐
              │                          ▼ (2nd frame, if enabled)
              │                 ┌─────────────────┐
              │                 │ CompanionWindow │
              │                 │ + sw renderer   │
              │                 │                 │
              │                 │ Draws the same  │
              │                 │ quads/strings   │
              │                 │ in its own OS   │
              │                 │ window (2nd mon)│
              │                 └─────────────────┘
              ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                    GAME ENGINE (MX Bikes / GP Bikes / etc.)             │
│                          (Renders our output)                           │
└─────────────────────────────────────────────────────────────────────────┘
```

> The **CompanionWindow** is an optional second render target: `HudManager` builds a second frame with `collectSurface(companion)` and submits it to a standalone OS window (drag it to a second monitor). It draws the *same* primitives with an in-process software renderer (`hud_sw_renderer`) instead of the game engine. See Core Components §13.

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

**Boundary validation (version skew):** PiBoSo has reshaped plugin structs between game versions before - the `EventInit`/`RaceCommunication` defensive copies exist for exactly that - so the array-style callbacks don't trust the game's framing. `RaceClassification`/`RaceTrackPosition`/`SpectateVehicles` reject a mismatch between the game-supplied `_iElemSize` and the compiled `sizeof` (warn-**once**, then return - the feature fails safe instead of indexing with the wrong stride, which misreads every entry past index 0 and runs off the real array), null-check `_pData`/`_pArray` when counts are positive, guard the defensive-copy `memcpy` against a null `_pData`, and `std::clamp` entry counts to `0..MAX_RACE_ENTRIES` (clamping negatives too, not just capping from above). Applied identically across MXB/GPB/KRP. The symptom of skew is empty standings/map plus a single "element size N != expected M" log line. New array-style callbacks must follow the same pattern.

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
    Handlers::handleRunLap(psLapData);
}
```

### 3. PluginData (`core/plugin_data.*`)

The **single source of truth** for all game state. This singleton:
- Caches all data received from the game (session info, standings, telemetry)
- Provides typed getters for HUDs to read from
- Detects changes and notifies HudManager when data updates
- Stores per-rider data (lap times, track positions, session bests)

**What came out of it, and what deliberately did not.** It is a large class and the
standing direction is to shrink it, but only where a piece is genuinely *separable*
rather than merely movable. The bar each of these cleared: its inputs are a handful of
numbers per rider, so it can be unit-tested with a plain g++ instead of only through the
DLL under Wine. Each header states its own rules and the trap it exists to avoid; this is
the index, not a second copy.

| Extracted | What it is | Unit test |
|---|---|---|
| `core/blue_flag_detect.h` | blue-flag/lapping pairwise proximity pass | `test_blue_flag_detect.cpp` |
| `core/proximity_tuning.h` | the seven INI-only blue-flag/hazard knobs, each clamp beside its field | - |
| `core/battle_groups.h` | battle-group partitioning, shared by the director and the overlay panel | `test_battle_groups.cpp` |
| `core/lap_timer.h` | the display rider's live lap-timer state machine | `test_lap_timer.cpp` |
| `core/live_gap_engine.h` | the live leader-relative gap core | `test_live_gap_engine.cpp` |

PluginData keeps the flattening, the caches, the eligibility filtering and the
notification coalescing around each of them. The hazard **state machine** stays put on
purpose: its per-rider state lives inside `RiderTrackState` and is updated inline in the
30 Hz position loop, so extracting it would add a hash lookup per rider per batch and
produce a component that reached back into PluginData for everything.

Key data structures:
- `SessionData` - Track name, session type, weather, etc.
- `RaceEntryData` - Rider name, bike, race number
- `StandingsData` - Position, gap, best lap for each rider
- `BikeTelemetryData` - Speed, RPM, gear, fuel
- `IdealLapData` - Best sector/lap times per rider
- `m_raceStartPositions` - Per-rider starting grid position (`raceNum → position`), snapshotted when a race goes green; drives the positions-gained/lost column. Cleared on each new session.

**Per-rider containers are declared `PerRider<>`**, which registers their erase-and-clear
so a reused race number cannot inherit a departed rider's state. It is enforced by
construction in `plugin_data.h` and pinned by `racenum_reuse_test.cpp`; the rule and the
one exception (derived caches, which are dirtied instead) are in CLAUDE.md's Maintenance
Invariants.

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

Each handler processes a specific category of game events.

Almost all of them are **stateless dispatch** - they read the unified event
struct, push it into `PluginData`, and return. Those are plain free functions in
`namespace Handlers` (one translation unit per callback category), because a
singleton around a class with no members buys nothing and costs a global:
`Handlers::handleRaceLap(psRaceLap)`, not
`RaceLapHandler::getInstance().handleRaceLap(...)`. The two handlers that
genuinely carry state across callbacks (`DrawHandler`, `SpectateHandler`) remain
singleton classes, and `DEFINE_HANDLER_SINGLETON` exists for exactly those two.

**Run handlers** (player-only, single-player or your own bike):
- `handleEventInit` / `handleEventDeinit` - Track loaded/unloaded
- `handleRunInit` / `handleRunStart` / `handleRunStop` / `handleRunDeinit` - Session start/stop
- `handleRunLap` - Player crossed finish line
- `handleRunSplit` - Player crossed split timing point
- `handleRunTelemetry` - Physics tick (100Hz telemetry)

**Race handlers** (all riders in online races):
- `handleRaceEvent` / `handleRaceDeinit` - Online race initialized/torn down
- `handleRaceAddEntry` / `handleRaceRemoveEntry` - Rider joined/left
- `handleRaceSession` / `handleRaceSessionState` - Session state changes
- `handleRaceLap` - Any rider completed a lap
- `handleRaceSplit` - Any rider crossed split timing point
- `handleRaceClassification` - Standings update
- `handleRaceTrackPosition` - Real-time positions of all riders
- `handleRaceCommunication` - Penalties, warnings, state changes
- `handleRaceVehicleData` - Telemetry for all riders (during replays)

**Other**:
- `handleTrackCenterline` - Track geometry for map display
- `DrawHandler` (singleton) - Frame rendering, FPS calculation
- `SpectateHandler` (singleton) - Camera/vehicle selection in spectator mode

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
- Transitions when session type changes

### 7. Rumble: RumbleProfileManager + XInputReader (`core/rumble_profile_manager.*`, `core/xinput_reader.*`)

`RumbleProfileManager` keeps per-bike rumble profiles in `{save_path}/mxbmrp3/mxbmrp3_rumble_profiles.json`, keyed by bike name, each holding a complete `RumbleConfig`. A toggle chooses between the global INI settings and per-bike profiles; enabling it auto-creates a profile for the current bike. `XInputReader` reads the active config at runtime.

**`XInputReader` runs its OS calls on a dedicated I/O thread**, and that split is the part worth knowing at this level because it is the same rule three subsystems follow. Every `XInputGetState`/`XInputSetState` happens on `ioThreadMain` - started in `PluginManager::initialize`, stopped in `shutdown` *after* the plugin worker is joined - so a degraded controller driver can never stall whichever thread drives telemetry and hotkeys (the game thread in legacy mode, the plugin worker in threaded mode). Same **state producer vs. blocking I/O** division as `HttpServer` and `DiscordManager`: the caller keeps all policy and effect math, because that feeds `RumbleHud`'s graph and must stay on the state thread; only the syscalls move. `update()` became a cheap copy of the snapshot the I/O thread published, `setVibration()` runs its unchanged policy and posts an 8-bit motor pair for the I/O thread to execute, and `setControllerIndex()` sets an atomic slot plus a poll-now flag.

**Two hardware constraints shape the tuning, and both are documented where they are enforced** - the empty-slot backoff inside `ioThreadMain()`, the send policy at `setVibration()`, with the reasoning inline at each. In outline: `XInputGetState` on a disconnected slot triggers device enumeration and can cost milliseconds, so scans are throttled; and controllers *decay* rumble without a continuous feed while some Bluetooth stacks choke on a sustained one, so nonzero values are re-sent on a user-tunable cap rather than deduped. The reasons are opposing and unobvious, which is exactly why they live next to the code that obeys them rather than in a second copy here. Pinned by `xinput_thread_test.cpp` and `rumble_effect_test.cpp`; CLAUDE.md carries the one-line "don't simplify this" warning.

### 8. StatsManager (`core/stats_manager.*`)

Unified stats system that tracks per-track/bike stats, global race stats, personal bests, and odometer data in a single JSON file (`{save_path}/mxbmrp3/mxbmrp3_stats.json`).

**Per track+bike stats** (`TrackBikeStats`):
- Total/valid lap counts, best lap/sector times
- Top speed, crash count, time on track
- First/last session timestamps

**Personal bests** (`StatsPersonalBestData`):
- Fastest lap time with sector breakdown per track+bike combo
- Metadata: setup name, weather conditions, timestamp
- Used by TimingHud, RecordsHud for all-time PB comparisons

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
- Migrates legacy data from the old `mxbmrp3_personal_bests.json` and `mxbmrp3_odometer_data.json` files
- Cached global totals (recomputed on load/clear, updated incrementally)
- Dirty flag with periodic save (not every telemetry tick)

**Non-finite hardening.** The persisted floats (per-bike odometer, `totalDistanceM`, `topSpeedMs`) are integrated from `speed × dt`, and the `>=`/`>` comparisons that gate them reject NaN but not `+Inf` - so one bad physics sample corrupts state that survives restarts. `updateTelemetry` sanitises at the sample, `finiteOrZero()` in `stats_manager_persistence.cpp` heals an already-corrupted file on load, and the reasoning sits at both. **Any new persisted float needs the same guard at both ends** - that is a Maintenance Invariant in CLAUDE.md, pinned by `stats_test.cpp` and `odometer_test.cpp`.

### 9. FmxManager (`core/fmx_manager.*`)

Manages FMX (Freestyle Motocross) trick detection and scoring:
- State machine: `IDLE → ACTIVE → GRACE → CHAIN → COMPLETED/FAILED`
- Dynamic trick classification (re-evaluates type every frame using peaks to prevent downgrades)
- Committed L/R direction tracking (prevents flip-flopping between direction variants)
- Chain system with variety-based multiplier (unique tricks add full bonus, repeats diminished)
- Anti-exploit measures: teleport detection, stuck detection, ground trick debounce
- Pause compensation (shifts steady_clock time points on resume)

**Data types** are defined in `fmx_types.h`:
- `TrickType` enum (trick types across ground, air, and combination categories; the exact count is pinned by `TrickType::COUNT` + a `static_assert`)
- `TrickInstance` - Active or completed trick with rotation, timing, and scoring data
- `RotationTracker` - Angular velocity integration for reliable rotation accumulation
- `GroundContactState` - Wheel contact, speed, and slip detection
- `FmxConfig` - Adjustable detection/scoring thresholds

**Display settings** are split between global and per-profile:
- Global (on FmxManager): enabled rows, chain display rows, debug logging
- Per-profile (on FmxHud): position, visibility, scale, opacity

### 10. HttpServer (`core/http_server.*`)

Embedded HTTP server streaming race data to browser overlays (an OBS browser source). It serves `mxbmrp3_data/web/` at `/`, pushes JSON snapshots over SSE at `/api/events`, answers `/api/state` for pollers, and scans `web/logos/` for the slideshow. Compile-time gated by `GAME_HAS_HTTP_SERVER`; runtime-gated by a settings toggle that starts and stops the server on demand.

**The snapshot is built on the game thread** and cached behind an annotated mutex for the SSE threads, because PluginData is not thread-safe. That, the connection limits, the throttle and the client-activity gating that keeps the build off the game thread when nobody is listening are all documented in `http_server.h` and at the code that enforces them; the gating asymmetry - frequent types gated, rare transition types never - is a Maintenance Invariant in CLAUDE.md and is pinned by `http_gating_test.cpp`.

**The snapshot is a contract between two languages**, which is the part no single file can state. Adding a field means extending `buildJsonSnapshot()`, consuming it in the client, and regenerating `tests/fixtures/overlay_snapshot.json` - a real captured snapshot read from *both* sides, so a rename cannot pass silently (`overlay_snapshot_test.cpp` checks the C++ side, `overlay_snapshot.spec.js` drives that same fixture through the real `render()`). CLAUDE.md's *Working with the Web Overlay* holds the boundary rules themselves: that the plugin ships raw data and the overlay decides presentation, which helpers are mirrored, why colour *roles* are chosen per renderer, and why `liveGapValid` and `canUseLiveForRider` answer different questions.

**The overlay's own structure** - the standings tower's cycling bottom slot (fastest-last-lap, fastest-laps, best-sectors, down-the-order, session-charts, battle), the `?demo` mode that replays a synthetic race with no plugin attached - lives in `mxbmrp3_data/web/js/overlay-*.js`, each file's header describing its own area.

### 11. Event Log System (`core/event_log_types.h`, `hud/event_log_hud.*`)

Timestamped feed of race events, used by both the in-game HUD and web overlay:

**Event types** (defined in `event_log_types.h`):
- Session events: started, state changes, overtime, final lap
- Rider events: finished, retired, DNS, DSQ, leader change
- Action events: penalty, penalty clear, pit entry, pit exit, fastest lap

**Filter flags:**
- Each event type has a bitmask flag (`EVENT_SESSION_STARTED = 1 << 0`, etc.)
- `EventLogHud::m_enabledEvents` holds the user's filter selection (a bitmask of the flags above)
- Used by in-game rendering to filter displayed events (web overlay filters client-side via `CONFIG.events` in `overlay-config.js`)

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

### 13. CompanionWindow & Software Renderer (`core/companion_window.*`, `core/hud_sw_renderer.*`)

A standalone, in-process OS window that renders the plugin's own HUD **outside** the game, so a player can drag it to a second monitor (telemetry on one screen, standings on another). It is **not** a network mirror and shares nothing with the web overlay - it reads the plugin's live render primitives directly from memory and draws them itself.

**How it renders (`hud_sw_renderer`):** the game normally hands our quads/strings to its own engine to draw. The companion has no engine, so `hud_sw_renderer` is a from-scratch software rasterizer for the exact same primitives: scanline convex-quad fill, affine (rotation-capable) sprite blit with bilinear atlas sampling, and text drawn from the game's own PiBoSo `.fnt` bitmap fonts (see `tools/fontgen`). Crucially it reproduces the game's **texture stage**: a texel is modulated by the quad's color (`out.rgb = tex.rgb × color.rgb`, `coverage = tex.a × color.a`) so per-quad **opacity** and the white-icon **colorization** the game does come out identical - a divergence here shows up as icons that ignore opacity or never tint. Presented via a plain Win32 window (`StretchDIBits`), natively on Windows and under Proton/Wine. Normalized HUD coords map into a **centered 16:9 viewport** (`Image::setViewport`) so the HUD keeps its aspect and never distorts in a non-16:9 window - but the renderer draws into the **full client**, so elements positioned outside `[0,1]` (negative / past 1, exactly as the in-game HUD allows) land in the surrounding area instead of being clipped to a letterbox. The window is freely resizable to any shape; only the *content scale* is 16:9, not the usable area.

**Threading:** the game thread calls `submit()` once per `Draw` with a cheap POD copy of the current frame (quads/strings + the font/sprite registration tables) under a mutex. A dedicated **window thread** owns the Win32 message loop and renders the latest snapshot on its own cadence - so the window stays live and interactive **in menus**, when the game issues no `Draw` calls. Enabled via the `[Display]` INI target; identified by its window class (`isCompanionHwnd()`) so input can tell the two surfaces apart.

**Window behavior:** persisted geometry + maximized state (window thread writes as the user moves/resizes; game thread reads at save time), **never takes focus** from the game (`WS_EX_NOACTIVATE` is kept for the window's whole life, not cleared after show - input is routed by the window under the cursor, so the companion never needs activating to interact with), hides the OS cursor over its client area (the plugin draws its own), and closing it (the X button) falls the display target back to In-game via a consumed `consumeUserClosed()` flag.

**Per-surface decoupling (the "two settings menus" model):** the companion is not a dumb clone - each HUD carries an *optional second instance* of its on/off + position (`base_hud.h`: `m_bCompanionConfigured` / `m_bCompanionVisible` / `m_fCompanionOffsetX/Y`). While a HUD is unconfigured its `getCompanion*()` accessors **fall back to the game values** (so both windows look identical, and a game-side change is reflected); the first companion-side edit **snapshots** the game state into the companion instance and thereafter the two are independent. `HudManager::collectSurface(companion)` builds the companion frame as a **second pass** (into `m_companionQuads`/`m_companionStrings`) gated on `CompanionWindow::isEnabled()` - `collectSurface(false)` stays byte-identical to the old single-frame game path. Which surface the settings menu / a drag edits is chosen by `InputManager::getActiveSurface()` (the focused window). Everything else - colors, fonts, sizes, columns - stays shared (one profile).

**Feature gating:** runtime only (the `[Display]` target: In-game / Companion / Both). Wired to analytics as `feat_companion`.

### 14. DirectorManager (`core/director_manager.*`)

An **auto-director** for spectating and replays: it scores an "interest" model over the field and cuts the broadcast camera to the most compelling story, with broadcast-style pacing. Global (broadcast) feature persisted in the `[Director]` INI section like HelmetOverlay/Rumble - **not** per-profile - passive except while spectating or replaying, and **off by default** so an upgrade never seizes a spectator's camera. The `DirectorWidget` status button is the discoverability path: one click enables, and the choice persists.

**What it can and cannot control.** The plugin can choose the *spectated rider* (`SpectateVehicles`) and a *named camera* (`SpectateCameras`) through `SpectateHandler`, but it **cannot** author camera angles beyond the game's named set. So its job is exactly two decisions: **subject selection**, plus a **name-based camera baseline** (`CameraRole`). `Auto` hands framing to the game's own trackside director; `Trackside` is the plugin-picked TV shot used for every story cut.

**Three drivers, one decision function.** Everything funnels into `evaluate()`, which coalesces to ~3×/sec and early-outs unless enabled and spectating/replaying:
- `onDataChanged(changeType)` - **Standings** drives race direction, **IdealLap** the non-race timing show, **SessionData** resets baselines (but only on a real `sessionGeneration` change: that type also carries the 1 Hz clock heartbeat, so resetting per notification would wipe director state once a second).
- `pollManualControl()` - per-frame, independent of timing data, so gamepad takeover and auto-resume work in quiet lulls, solo sessions and replays.
- `pollPacing()` - per-frame wall-clock pump, so the max-shot cap still fires when no data callbacks are flowing.

**The priority ladder.** After yielding to a hand-flown camera and honouring the rider lock, `evaluate()` runs a fixed ladder over the racing, on-track field - incident, fastest lap, fastest sectors, timing show, scored stories (leader / battle / overtake / lapper / drop), finish lock - each rung a `cutTo()`+return, so higher stories pre-empt lower ones. **The rungs, the scoring weights, the min/max-shot pacing and the onboard-variety rules are documented at the mechanism, in `director_manager.h`** - they are tuning, they move, and a second copy here would drift.

**Consumers.** Two, both reading the director's published status:
- `DirectorWidget` (`hud/director_widget.*`) - an on-screen status button (a camera icon tinted by state) that toggles the director on click; clipped unless spectating/replaying.
- The **web overlay** - `buildJsonSnapshot()` emits a `director` advisory (suppressed to `-1` while paused/manual/held so the overlay never highlights a stale rider) and a `battles` array from the **same** `getBattleGroups(battleGap, maxPos)`. That shared call is the "one brain, one config" property: the in-game director and the overlay's battle panel agree on what a battle is because both read the director's own settings.

**The cut log is a contract.** Every cut logs one parseable line - `Director cut: t=<ms> #<num> shot=<type> cam=<name> partner=<num> reason=<reason>` - so a whole broadcast can be reconstructed offline. `tools/director_report.py` parses it from a real log and `tests/integration/tests/director_broadcast_test.cpp` runs the same analysis headless off a recorded tape, so a one-sided change to `cutTo()` silently breaks both. `testSetNowMs()` injects a simulated wall-clock so a headless replay drives the real pacing from recorded timestamps.

**Threading.** All access (hotkeys, settings, UI dispatch, `onDataChanged`, the per-frame polls) is on the game thread, so the members are deliberately non-atomic; a background writer would have to make the touched fields atomic (see the cross-thread-flags invariant in CLAUDE.md).

### 15. PluginThread - game-thread isolation (`core/plugin_thread.*`) - EXPERIMENTAL, opt-in

An **opt-in** mode (`[Advanced] pluginThread=1`, **off by default**) that moves *all* per-frame and per-event work onto a **dedicated worker thread**, so a hiccup on our side can never stall the game's frame. `core/plugin_thread.h`'s header states the threading model, the two O(1) game-thread touchpoints, what is deliberately not routed through the worker, and why `reconcileEnabled()` must run on the game thread; `render_frame_buffer.h` covers the triple buffer. What belongs at *this* altitude is how it changes the shape of the system:

- **The single-owner property still holds.** Every handler runs verbatim on the worker, which has simply taken over the game thread's role as sole owner of PluginData/HudManager. So "PluginData is not thread-safe; it's touched on one thread" is unchanged - just not the game's. `onDataChanged` → `HttpServer::buildJsonSnapshot` therefore also runs on the worker; the SSE threads keep reading the mutex-guarded cached string as before.
- **Spectate is the one path still partly on the game thread**, because `SpectateVehicles`/`SpectateCameras` must answer *that frame*. `SpectateHandler`'s request/tracking fields are `std::atomic`, and the one call that cascades into real mutation (`setSpectatedRaceNum`) is routed onto the worker - so the game thread only reads atomics and the game's own arrays.
- **Performance metrics change meaning, deliberately** - in threaded mode "plugin time" is the worker's build cost, not game-thread cost. The full contract is at `DebugMetrics` in `core/plugin_data_types.h`.
- **One-frame latency** is inherent: `Draw` serves the previous finished frame, which is what makes it non-blocking.
- **Not a fix for game-side stalls** - it isolates *our* work, nothing about hitches inside the engine.

### 16. SpotterManager (`core/spotter_manager.*`, `core/spotter_*.h`)

An audio **spotter**: short spoken callouts over the game audio, the way a crew chief talks on the radio. It is a global (per-install) feature in the `[Spotter]` INI section, and it is the largest subsystem added since the HTTP server - the hub class plus ten pure headers, each with its own tests.

**Where the pieces are.** `spotter_manager.*` is the hub and the only stateful part. Everything it decides is delegated to pure headers, which is why the logic is unit-testable without a game: `spotter_phrase.h` (event → words, and the cue **categories** that decide which settings switch mutes a cue), `spotter_cue_pack.h` (the pack format), `spotter_mix.h` (the chunk mixer), `spotter_hazard.h` (proximity / alongside / blue-flag edges), `spotter_milestones.h`, `spotter_pace.h` (gaps and trends), `spotter_vars.h` (the `{placeholder}` registry), `spotter_queue.h`, `spotter_stretch.h`, `spotter_tts_voice.h`. Read the hub's header first: it maps them.

**One emit path, one category gate.** Every cue goes through `emitCue(key, category, vars…)`. The category a cue is emitted *as* is the same one that mutes it, by construction - this used to be checked at each of twenty-odd emitters and three had drifted, leaving settings switches that did not silence what they named. Ambient variables (position, gaps, rider names) are filled in `emitCue` from live state rather than carried by each event, so a new variable reaches every template at once.

**Three output rungs**, all zero-dependency Windows built-ins, tried in order per cue: a pack's stitched **mix** (chunks assembled from memory), a pack's whole **wav**, or **SAPI TTS**. The bundled `default` pack is text only, so out of the box every cue lands on TTS. Worth knowing: **SAPI does not exist under Wine/Proton**, so on those systems a default install is silent and a recorded pack is what makes it audible.

**Packs are content, not configuration.** `mxbmrp3_data/spotters/<name>/<name>.ini` is a pack's whole vocabulary - one line per cue key, `_2`/`_3` suffixes for variants picked at random, `[optional groups]` that drop when their variables are empty. A pack is chosen by NAME, never by discovery index (the asset-pack invariant in CLAUDE.md). `docs/spotter.md` is the author's guide; `docs/spotter-reference.md` is generated from the registry by `tools/spottergen`, so the documented cue set cannot drift from the code.

**Threading.** All audio runs on one worker started lazily at the first cue, so the 480fps game thread never touches COM, disk or a speech engine; `say()`/`playWav()` enqueue under a mutex and notify. Speech is serialised (a spotter talking over itself is noise); wav playback is fire-and-forget through winmm, which gives **one channel, no per-sound volume and no ducking** - a backend limit, not a design choice, so the `[Spotter]` volume applies to TTS only. The worker is joined by the orchestrated `PluginManager::shutdown()`, never by the destructor, per the DLL-detach invariant.

**Tests.** Pure logic in `tests/unit/test_spotter_*.cpp`; end-to-end cue behaviour in `tests/integration/tests/spotter_test.cpp`, which drives real callbacks and reads the chosen cue through a test hook rather than listening to audio. `test_spotter_pack_census.cpp` asserts every key in the registry is actually emitted by something - the check that catches a cue wired up, documented, shipped, and firing never.

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
- `SessionChartsHud` - Session-progression charts (position / race trace / gap-to-leader / pace), one line per rider
- `IdealLapHud` - Ideal (purple) sector times with gap comparison
- `MapHud` - 2D track map with rider positions and zoom/range mode
- `TelemetryHud` - Throttle/brake/suspension graphs
- `PerformanceHud` - FPS, CPU usage graphs
- `RadarHud` - Proximity radar with nearby rider alerts
- `PitboardHud` - Pitboard-style lap/split information. Ships as a **pack** (`pitboards/<name>/` = art + `<name>.ini` of its proportions and row offsets), selected by name - what makes a custom board portable: its geometry used to live in the *user's* settings file, and its aspect was a compiled constant
- `RecordsHud` - Track records from online databases (CBR or MXB-Ranked providers)
- `TimingHud` - Split time comparison popup (center display)
- `GapBarHud` - Live gap visualization bar with ghost position marker
- `SettingsHud` - Interactive settings menu UI
- `FmxHud` - FMX trick detection display with rotation arcs, chain stack, and scoring
- `SessionHud` - Session info (server name as the headline, format & state, track, weather)
- `StatsHud` - Session stats display with configurable columns (last lap, session, all-time)
- `NoticesHud` - Race status notices (wrong way, blue flag, PB alerts, final lap, finished)
- `FriendsHud` - Steam friends in the same game, their server/track, and who has joined your session
- `EventLogHud` - Timestamped feed of race events with per-type filters (`m_enabledEvents` bitmask)
- `RumbleHud` - Real-time monitor of the controller rumble motor outputs and effect values (a window onto the rumble system)

**Overlays** (full-screen, telemetry-driven):
- `HelmetOverlayHud` - First-person helmet overlay with visor tint, tilt (lean angle) and vibration (suspension). Global settings in `[HelmetOverlay]` INI section. Registered first to draw behind all other HUDs.

**Widgets** (simple, focused):
- `SpeedWidget` - Speed and gear display
- `PositionWidget` - Current race position (P1, P2...)
- `LapWidget` - Current lap number
- `TimeWidget` - Session time remaining
- `ClockWidget` - Real-time clock
- `GearWidget` - Current gear indicator
- `CrashWidget` - A resettable crash tally for streaming. Deliberately NOT a view of StatsManager's per-track+bike `crashCount`: it counts across practice, races, server hops and restarts (`GlobalStats::crashTally`) and moves only on the widget's Reset button or the `CRASH_RESET` hotkey. Sized to share Speed's and Gear's content box so the three tile in a row
- `SpeedoWidget` - Analog speedometer dial
- `TachoWidget` - Analog tachometer dial
- `BarsWidget` - Visual telemetry bars (throttle, brake, etc.)
- `LeanWidget` - Bike lean/roll angle display with arc gauge and steering bar
- `GForceWidget` - Lateral/longitudinal G-force gauge with peak marker
- `FuelWidget` - Fuel calculator with consumption tracking
- `TyreTempWidget` - Front and rear tyre tread temperatures (GP Bikes only)
- `EcuWidget` - Electronic rider aids: engine map, traction control, engine braking, anti-wheeling (GP Bikes only)
- `GamepadWidget` - Controller visualization with button/stick/trigger display. Sizes its interior from its own FRAME, not the global grid (`hud/gamepad_geometry.h` says why). Its ~30 button offsets are per-pad DATA, so a pad ships as a **pack** - `gamepads/<name>/` = 17 `.tga` + `<name>.ini`, selected by *name* - and a new pad needs no build
- `CompassWidget` - Bike heading dial (classic north-up needle, or modern rotating card with numeric readout)
- `VersionWidget` - Plugin version display (includes hidden Breakout game easter egg; high score persisted via StatsManager)
- `SettingsButtonWidget` - Settings menu toggle button
- `DirectorWidget` - Auto-director status button (camera icon tinted by state; click to toggle). A window onto `DirectorManager`, clipped unless spectating/replaying.
- `PointerWidget` - Customizable mouse pointer rendered with quads (internal; the on-screen cursor)
- `BenchmarkWidget` - Developer-only per-callback/per-HUD timing breakdown (requires `developerMode=1` in INI)

### HUD Lifecycle

1. **Creation**: HudManager creates all HUDs in `initialize()`
2. **Configuration**: SettingsManager loads saved positions/settings
3. **Data Update**: PluginData changes -> HudManager notifies -> HUD marked dirty
4. **Render Cycle**: Every frame:
   - `update()` called -> if dirty, calls `rebuildRenderData()`
   - `getQuads()` and `getStrings()` return render data
5. **Shutdown**: Settings saved, HUDs destroyed

### Creating a New HUD

**[`CLAUDE.md`](CLAUDE.md) → *Common Tasks* → "Adding a New HUD" is the
authoritative step list** and is kept current with the registries it names (the
CMake source glob, `HudManager`, the `s_tabRegistry` tab row, the per-HUD
serializer registry row). It is deliberately not restated here - a second copy
drifts, and the reader can't tell which one is stale.

What this layer contributes is the `BaseHud` contract those steps assume:

- **Constructor** - defaults (`setDraggable`, `setPosition`), `reserve()` the
  primitive vectors, build once.
- **`handlesDataType()`** - which changes dirty this HUD.
- **`rebuildRenderData()`** - clear and rebuild `m_quads`/`m_strings` from
  `PluginData`, then `setBounds()`. Read fresh; cache only formatted output.
- **`rebuildLayout()`** - the drag fast path: reposition existing primitives
  rather than rebuilding. It must agree with `rebuildRenderData()` on placement
  (`standings_layout_test` pins that after the plate-nudge bug, and the placement
  itself beside it - two paths can agree on a wrong one).
- **`update()`** - service `isDataDirty()` then `isLayoutDirty()`. Gate any
  skip-when-hidden check on `isVisibleAnySurface()`, never `isVisible()`.

Pure decision logic - which rows to draw, which gap to show, where a glyph sits
- increasingly lives in a small header beside the HUD (`standings_gap_plan.h`,
`lap_log_plan.h`, `peak_marker.h`, …) so it is unit-testable with plain `g++`
instead of only through the DLL under Wine. Prefer that shape for anything with
interesting branches; TESTING.md's Layer 1 catalogue lists them.

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

**Font format & text encoding.** The game's `.fnt` bitmap fonts are a **byte-indexed 256-glyph table** built from CP1252 (`code_page = 1252`, glyphs 32–255). The renderer indexes by raw byte, so it cannot render UTF-8 - multi-byte rider names garble regardless of any truncation logic, which makes UTF-8-safe truncation *in-game* moot. The web overlay is the only UTF-8-aware renderer and handles names client-side. `m_szString` is `char[100]`, so in-game strings are also length-bounded by the struct.

**Header/label convention.** Table column headers and axis labels go through `BaseHud::addLabel()` - the STRONG font at the *Small* size, vertically centered in the row via `labelRowYOffset()` - rather than a hand-rolled `addString` at data-font size. FriendsHud (column headers) and FmxHud (rotation-arc Pitch/Yaw/Roll labels) both deviated and were brought in line; new HUDs should use the helper.

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

The settings layer is split across several TUs (all `SettingsManager`): `settings_manager.cpp`
(path resolution, serialize/build, save/load orchestration), `settings_manager_global.cpp`
(global-section `writeGlobalSettings`/`applyGlobalLine`), `settings_hud_profiles.cpp`
(per-profile capture/apply orchestration + profile switch/copy/reset), and
`settings_hud_registry.{cpp,h}` (the per-HUD serializer registry, below). Shared free helpers
live in `settings_keys.h` (INI key constants), `settings_serde.h` (the HUD-free half:
generic enum⇄string, bitmask primitives, base-HUD capture/apply, validators) and
`settings_serde_hud.h` (the HUD-typed half: per-HUD enum converters and column/row bitmask
save/loads - TUs that serialize concrete HUD types include this one), all in
`namespace Settings`.

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

**Parse robustness.** Hand-editing the INI is a supported workflow (`auto_save` off, then the RELOAD_CONFIG hotkey), so **every** value-parsing site in `loadSettings()` must be exception-guarded - a single naked `std::stoul`/`std::stof` on a typo'd value throws out of the loader and aborts the parse mid-file, leaving the plugin half-configured for the session. The one offender found was the v4 base-section color path calling `parseColorHex` (a bare `std::stoul`) without a `try/catch`. `parseColorHex` itself stays a thin wrapper, so the guard belongs at each call site, wrapping the whole section's branch.

#### Per-profile vs global sections

Settings fall into two kinds, persisted differently:

- **Per-profile** - each HUD/widget has a base `[HudName]` section (the defaults) plus
  optional `[HudName:Practice|Qualify|Race|Spectate]` override sections. Saving is **sparse**:
  a profile section only contains the keys that *differ* from the base. There are four
  profiles (`ProfileType`: Practice, Qualify, Race, Spectate) that auto-switch with the
  session type.
- **Global** (single value, not per-profile) - `[General]`, `[Updates]`, `[Advanced]`,
  `[Display]`, `[Colors]`, `[Fonts]`, `[Rumble]`, `[HelmetOverlay]`, `[Hotkeys]`. These are
  owned by singletons (UiConfig, ColorConfig, UpdateChecker, etc.), not by a profile.

#### Per-HUD serializer registry (one list for capture / apply / serialize)

The per-profile HUD sections are driven by a **single ordered table** -
`Settings::hudSectionRegistry()` in `settings_hud_registry.cpp` - where each row is
`{ section name, capture fn, apply fn }`. All three consumers iterate it:

- `captureToCache()` → `s.capture(...)` for every row (live HUD → profile cache),
- `applyProfile()` → `s.apply(...)` for every row (profile cache → live HUD),
- `serializeSettings()` → `buildHudSection(s.name)` in row order (cache → INI).

So a HUD is registered for capture, apply, **and** on-disk serialization in exactly one place.
This replaced three parallel hardcoded lists (the old `captureToCache`/`applyProfile` blocks
plus a `hudOrder[]` array): a HUD present in capture/apply but missing from `hudOrder` was
silently never written and reverted to default on restart (the FriendsHud bug). That drift is
now **structurally impossible**, not merely caught. The `cap_*`/`app_*` functions are private
static `SettingsManager` members (declared in `settings_hud_registry_decls.inc`) so they inherit
its `friend`-ship with the HUD classes; `hudSectionRegistry()` is a `friend` so it can take their
addresses. Guarded end-to-end by `settings_sections_test` (capture ⊆ serialized) and the two
apply-path tests (`settings_idempotency_test`, `settings_apply_values_test`).

#### One serialization, three consumers (save / load / reset)

Both save and load route global sections through a **single pair** of functions, so they can't
drift as settings are added:

- `writeGlobalSettings(ostream&)` - the sole emitter for every global section. Used by
  `saveSettings()` *and* by `captureFactoryDefaults()` to snapshot defaults at startup.
- `applyGlobalLine(section, key, value)` - the sole applier. Used by `loadSettings()` *and*
  by the reset paths.

**Reset = replay the factory snapshot through the same applier.** At startup (before the
user's INI is parsed, while every singleton holds its constructor defaults),
`captureFactoryDefaults()` captures two snapshots:

- `m_globalDefaultsIni` - global sections as INI text. `resetGlobalsToFactoryDefaults()`
  (full reset) and `resetGlobalSectionsToFactoryDefaults({...})` (per-tab reset for tabs
  that map 1:1 to a section) replay it via `applyGlobalLine`.
- `m_hudFactoryDefaults` - pristine per-HUD constructor defaults. The per-HUD reset paths
  (`resetAllToFactoryDefaults`, `resetHudsToFactoryDefaults`, `resetActiveProfileToFactoryDefaults`)
  replay *this*, **not** `m_hudDefaults`.

> Why two HUD caches? `m_hudDefaults` is the sparse-save baseline and has the user's
> hand-edited base `[HudName]` keys *folded in* at load (so they round-trip). That makes it
> the wrong source for "reset to defaults" - it would restore the file's baseline (or, after a
> plugin upgrade, an *old* version's default) instead of this build's. `m_hudFactoryDefaults`
> is captured before any folding, so reset always means this build's defaults. Don't collapse
> the two. (Migration note: legacy keys are read from their old section as a fallback and
> migrate to the new section on next save - e.g. update keys `[General]`/`[Advanced]` →
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
mxbmrp3/hud/settings_hud.h/.cpp  # Main SettingsHud class (in hud/, alongside settings_hud_input.cpp / settings_hud_render.cpp)
mxbmrp3/hud/settings/
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
| `addSectionHeading(title)` | Section divider with label |
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
| `DIGITS` | RobotoMono-Regular | Numeric displays |
| `MARKER` | FuzzyBubbles-Regular | Handwritten style |
| `SMALL` | Tiny5-Regular | Map/radar labels |

The authoritative category set and defaults live in the `FontCategory` enum + `FontConfig` defaults (`core/font_config.*`) - add a category there and this table is illustrative, not exhaustive. Access via `PluginConstants::Fonts::getTitle()`, `getNormal()`, etc.

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

**Gate on the flags, not the frame.** A HUD must rebuild only when `isDataDirty()`/`isLayoutDirty()` is set, never unconditionally per frame (unless the rebuild is trivially cheap). TelemetryHud was re-tessellating ~1600 line segments (each with a `sqrt`) every frame at 480fps for data that only changes at the 100Hz telemetry rate, so more than half the rebuilds produced identical output. (Becoming visible sets data-dirty via `BaseHud::setVisible`, so the first rebuild is unaffected.) The same proportionality applies to input polling: `HotkeyManager` refreshes only the *bound* keys each frame, doing the full 256-key `GetAsyncKeyState` sweep only while capturing a new binding.

**The visibility/dirty flags are atomic.** `m_bVisible`, `m_bDataDirty`, and `m_bLayoutDirty` are `std::atomic<bool>` - and `setDataDirty()` writes *both* dirty flags. Background workers legitimately mark HUDs dirty: the records fetch thread (`RecordsFetcher`) flags RecordsHud and TimingHud on completion, and the update-checker/downloader callbacks reach `VersionWidget::showUpdateNotification` (`m_bVisible` + the atomic `m_showingUpdateNotification`) and `SettingsHud::setDataDirty`. The reads happen every frame on the game thread; plain bools made that a data race (benign on x86-64 but UB). Keep any flag written cross-thread atomic.

**Second-level render caches key on their inputs.** Where a rebuild is dominated by sub-geometry that *doesn't* change every rebuild, cache it keyed on everything that affects its output. MapHud's `renderTrack()` does this: every `RaceTrackPosition` marks the map dirty, but with rotation/zoom off the track ribbon is bit-identical between rebuilds, so its two tessellation passes are cached in `m_ribbonQuads` keyed by `TrackRibbonKey` (rotation, render bounds, scales, HUD offset, clip rect, LOD, zoom params, title row, the two colors - every input baked into the emitted quads). **Any new input to the ribbon output must be added to the key**, or the cache serves stale geometry. In rotate-to-player/zoom-follow modes the key changes every rebuild by design, so it's a transparent pass-through there.

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

**The `Standings` firehose.** `DataChangeType::Standings` is the highest-frequency notification: `updateRealTimeGaps()` runs on every `RaceTrackPosition` callback, and the per-rider `GAP_UPDATE_THRESHOLD_MS` (100ms) filter is structurally defeated on full grids - leader timing is quantized to 100 points per lap, so a gap steps by ~lapTime/100 (well above the threshold) whenever *any* rider crosses a quantization boundary, which on a 30+ grid is nearly every callback. Left unchecked, that rebuilt every table HUD (Standings/Timing/Pitboard/Friends) every frame during close racing. So the notification is **time-coalesced** to at most one per `gapNotifyIntervalMs` (default 100ms): a skipped notify is carried in `m_gapNotifyPending` and flushed by a later call, so the final change is never dropped. MapHud/RadarHud are unaffected - they rebuild from their own `updateRiderPositions` path.

**New consumers must respect the firehose.** Any new `onDataChanged` consumer beyond the HUDs sits on this hot path and must be trivially cheap *or* short-circuit before any string/alloc work, gated on whether its output is even consumed: `HttpServer` gates the snapshot build on `hasActiveClients()` (see HttpServer above), and `SteamFriendsManager::updateLocalPresence` fingerprints its raw inputs in a POD `PresenceInputs` compare and returns before building ~10 strings when nothing changed (session time bucketed per second, the finest granularity the self-row clock displays).

## Constants & Configuration

Magic numbers live in `plugin_constants.h` - but the LAYOUT ones do not, and that
distinction is the point of the split:

```cpp
namespace PluginConstants {
    // Colors are configurable via ColorConfig singleton
    // ColorConfig::getInstance().getPrimary(), getSecondary(), etc.

    namespace Session {
        constexpr const char* RACE_1 = "Race 1";
        constexpr const char* RACE_2 = "Race 2";
    }
}
```

### The layout vocabulary is DATA (`core/layout_metrics.h`)

Font sizes, the snap grid, panel padding and the settings panel's block metrics
live in one struct (`core/layout_metrics.h`) rather than as `constexpr` blocks
spread across `plugin_constants.h` and a settings-panel header, where three copies
of the grid axis once drifted 5.4% apart.

**Two of them are settable**, from `[Advanced]` in the settings INI: `uiFontSize`
(base text size as a fraction of screen height) and `uiLineHeight` (row pitch as a
multiple of it). Both feed `derive()`, so every tier, row and grid cell follows.
Everything else is a compiled constant.

**It was all settable once**, from a defaults ini under the themes folder that
documented forty keys, each of which a theme could override. That went because the usage evidence was
unambiguous: across every theme a user would actually pick, the number of layout
keys set was **zero** - the themes of the day set slice values, colours and fonts,
and the only file exercising the tuning surface was the debug theme, whose job is to
demonstrate that the surface exists. (Every shipped theme states the full box now,
so the surface is exercised by all of them.) Two of the knobs were also measurably
dishonest: `[panel] padding-x` was inert below 6 cells because the frame clearance
dominated it, and `[panel] border-x/-y` existed *only* to work around a quantisation
dead zone that no longer exists.

**The grid IS the character box**, divided by `cellsPerChar` / `cellsPerRow` (1 and
2: one cell is one character wide and half a text row tall). Nothing else defines a
lattice - `snapX/snapY/ceilX/ceilY` hang off `LayoutMetrics`, and the grid overlay
draws those same numbers. The lattice is SHARED, which is why it is not per-theme:
HUDs align to each other by landing on the same grid lines.

### Panel themes (`ThemeAsset`, `hud/nine_slice.h`)

A theme is a folder of `.tga` under `mxbmrp3_data/themes/<name>/` plus that ini.
Three slice SETS, each with its own `border` in GRID CELLS: `[frame]` (the panel frame),
`[card]` (title bands, settings section cards, HUD body cards) and `[button]`. `[card]`
also takes `title border`, sizing the TITLE BAND's border alone on those same sprites;
absent it *follows* `border` (`ThemeAsset::titleBorderOverride`).

NINE files per set (center + four corners + four edges); `ThemeAsset` says why.

**Sprite order is an unchecked contract** for THEMES: `discoverThemes()` hands out
indices in `spriteFiles` order and `setupDefaultResources()` pushes them in it;
diverge and a theme draws another's sprites (hence the rewind on rejection). The
asset packs removed this class by walking one shared `kStems` table.

**Title bands and body cards** are new geometry rather than frame decoration - a band
behind the caption, a card behind the content, both spanning the panel's inner width
and placed from `[title]`. The band is universal; the body card is opt-in
(`m_bContentCard`), and widgets may take one - a lint once banned that, and its
post-mortem is at `core/panel_box.h`. `[card] widget-content` is the
per-theme choice that replaced the ban.

**Which themes ship is not stated here** - `mxbmrp3_data/themes/` is the list, and a
count in prose only goes stale. The `.tga` are edited directly, or cut from a single
master by `tools/themeslice` - which slices, never draws; the masters live in
`assets/themes/`, alongside `debug` (master + ini, slices deliberately NOT built:
it is a measuring instrument for a skinner, so it costs a checkout rather than
every install).

**They share one GEOMETRY and differ only in art, colour and typeface**, so a panel
measures the same whichever a player picks and a retune is one decision rather than
one per theme. Enforced by `tools/check_docs.py`, which compares the themes against
*each other* rather than against a canonical copy - a copy in the checker would be
the first place to go stale. `debug` is the one exemption, and the checker names it.

**Their semantic colours follow the PLUGIN's ramp, not their design language's**:
green positive, orange warning, yellow neutral, red negative, as `ColorConfig`'s
built-in defaults do. The hues are the language's own; only which slot each fills
is ours. A theme that reassigned them would make a delta mean something different
depending on the theme, which is the one thing a palette must not do.

`debug` is a measuring instrument rather than a look, and it ships because that is
what makes it useful to a skinner: every slice of every set is a different flat
colour, at three brightnesses for the three sets, with a brighter band on each
slice's outer side, so a band pointing inward says which *file* is authored wrong.
Its ini carries the legend, and it is the one theme that turns every `[card]` switch
on and exaggerates its box terms - the shared geometry turns the bands off, which
would leave it unable to show one. It answers "which margin did that knob just
move?", and the 9-slice bugs this project has shipped (corners culled by reversed
winding, a spine on the wrong axis after rotation) are visible in it and invisible
in a tinted monochrome theme.
Its colours are baked (`[frame] tint = 0`), so it deliberately ignores the HUD
background colour.

**INI-only tuning knobs.** Power-user settings with no in-game control, edited directly in the INI (documented inline, clamped on load, reset covered by the global-snapshot replay):

- `[Rumble] send_interval_ms` (4–200, default `DEFAULT_RUMBLE_SEND_INTERVAL_MS` = 10) - the continuous-rumble-feed cadence cap. Lower = more responsive; higher = less Bluetooth traffic on degraded stacks. Global (on XInputReader), never per-bike, since send cadence is a transport property, not an effect preference.
- `[Advanced] gapNotifyIntervalMs` (0–1000, default 100) - live-gap HUD refresh coalescing (see *Data Change Notifications*). `0` restores notify-on-every-change.

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

### Measuring the engine's render cost (render-load probe)

The plugin can time how long it takes to **build** the quads/strings (BenchmarkWidget
per-callback + `collectRenderTimeUs`), but the game engine's cost to **render** them
happens after `Draw()` returns and no in-plugin timer can see it. It is, however,
measurable **differentially**: emit a controlled number of extra quads and watch the
frame time rise - the slope (Δframe-time / Δquads) is the engine's per-primitive cost.

`[Advanced] renderProbeQuads=N` (INI-only, off by default; `HudManager::produceFrame`
appends N synthetic primitives to the game frame). `renderProbeType` picks WHICH
primitive, because the three the plugin emits have different engine costs: **0=solid-fill
quad** (cheapest - just fill; `renderProbeFullscreen=1` makes them full-screen), **1=sprite
quad** (textured - a texture fetch per pixel, and it cycles across every registered sprite
so it also exercises texture-switch / batch-break cost), **2=text string** (glyph-atlas
sampling). The count flows into `bm.totalQuads`/`totalStrings`, so the BenchmarkWidget
report already logs the `(primitives, frame-time p50/p99)` pairs. Sweep each type
separately - flat quads are the lower bound; sprites and text cost more. **In-game recipe:** uncap
FPS (no vsync/limiter - else adding load won't move frame time until you blow the
budget), sit at a **fixed** spot (a paused replay is ideal so the game's own frame cost
is constant), sweep `N` (0, 2k, 5k, 10k…) via RELOAD_CONFIG, and read the frame-time
from each report. Two modes separate the two GPU costs: **tiny** quads isolate
per-primitive submit/draw-call cost; **fullscreen** quads isolate fill-rate (what a
full-screen overlay like the helmet costs). This is inherently an **in-game** measurement
- headless (Wine) never renders the quads, so only the emission plumbing is testable
there (`MXBMRP3_Test_SetRenderProbe`). Off by default; ships in the DLL but dormant.

### Build Configurations

- **Debug**: Enables all logging, assertions
- **Release**: Minimal logging, optimized

## Testing

The shipping plugin is MSVC/Windows-only, but the game-independent logic is
covered by automated tests that run headless on Linux (and in CI)
with **no game engine**. **[`TESTING.md`](TESTING.md) is the canonical guide**
(layers, harness, how to add a test, philosophy); this is the architectural
summary. Six layers:

1. **Unit** (`tests/unit/`) - pure logic compiled from the real headers with a
   plain C++17 compiler (doctest).
2. **Integration** (`tests/integration/tests/`) - the heart of the suite. A
   mingw-w64 cross-build compiles the whole plugin to a Windows DLL; each doctest
   loads it under Wine and drives the **real PiBoSo callbacks**, exercising the
   full data flow: api exports → adapters → PluginData change detection →
   `buildJsonSnapshot`.
3. **Specialized** (`tests/integration/run_*.sh`) - persistence round-trip,
   config/callback fuzzing, CPU perf baseline, installer mechanics.
4. **Web overlay** (`tests/web/`) - Playwright asserts the rendered DOM, plus
   eslint over every `.js`. One spec closes the seam BETWEEN the layers: it
   renders a real captured `/api/state` snapshot (`tests/fixtures/`), so a field
   renamed in the plugin cannot pass a C++ suite reading the new name and a
   client suite driving its own synthetic demo.
5. **Memory safety** (`tests/asan/`) - the unit suite under ASan/UBSan, plus a
   targeted harness and an MSVC-DLL CI job over the real callback boundary.
6. **Visual** (`tools/hud_window/companion_demo.sh`) - screenshots the
   real HUD through the companion software renderer, so rendering is
   pixel-diffable. An instrument, not a gate.

Two seams are architectural rather than test detail. **Observation:** logic tests
read `PluginHost::snapshot()` - `buildJsonSnapshot()` called directly through a
test hook, with no server, socket or rebuild gating - so they depend on the
plugin's computation, not the serving layer; internal state that never reaches
the JSON is read through typed `MXBMRP3_Test_*` hooks (`core/test_hooks.cpp`,
gated on `MXBMRP3_TEST_BUILD`, excluded from every shipping target). **Fidelity:**
the in-plugin recorder (`core/event_recorder`, hidden `[Recorder] enabled=1`)
captures the real callback stream in-game; `PluginHost::replayTape()` replays it
headlessly, so committed real-race tapes anchor the synthetic scenarios.

What the headless build cannot reach - and so what manual in-game testing is
still for - is in **[`TESTING.md`](TESTING.md)**, along with how to run
everything and add tests; the cross-build's divergences from the shipping DLL
are in CLAUDE.md's *Build & Test*.

## Common Gotchas

1. **Don't cache game data in HUDs for rendering** - Always read fresh from PluginData when building render data. HUDs only cache formatted render data (`m_quads`, `m_strings`). **Exception:** Widgets that poll continuously-changing values (like session time) may cache "last rendered value" for change detection - see "Self-Detection Pattern" in Dirty Flag Pattern section.

2. **0-based vs 1-based indexing** - API uses 0-based lap numbers, UI shows 1-based. Check the API header comments.

3. **C++ exceptions must not cross the DLL boundary** - The host game terminates if a C++ exception escapes a DLL export. Every export in `vendor/piboso/*_api.cpp` wraps its body in `API_GUARD_CATCH` (see `vendor/piboso/api_guard.h`). When adding a new export, follow the same pattern. Similarly, every `std::thread` body (HttpServer, UpdateChecker, UpdateDownloader, DiscordManager, RecordsFetcher, CompanionWindow, SteamFriendsManager, AnalyticsManager, XInputReader, PluginThread) wraps itself in a top-level try/catch, since an uncaught throw in a `std::thread` calls `std::terminate()`. For hardware faults that don't go through the C++ exception system (null deref, OOB, divide-by-zero), the SEH filter in `core/crash_handler.*` writes a minidump for diagnosis but doesn't prevent the crash.

4. **Game thread vs background threads** - All PiBoSo API callbacks (`Draw`, `RunTelemetry`, etc.) run on the game thread. `PluginData`, `HudManager`, `SettingsManager`, and the various other managers are game-thread-only and not thread-safe. Background threads exist for I/O and off-thread work (HttpServer, DiscordManager, UpdateChecker, UpdateDownloader, RecordsFetcher, CompanionWindow, SteamFriendsManager, AnalyticsManager, XInputReader, PluginThread) and must NOT touch those singletons directly. They consume snapshots built on the game thread instead (see `HttpServer::buildJsonSnapshot`, `DiscordManager::updateSnapshot`). The `Logger` has its own internal mutex and is safe to call from any thread. Two corollaries for any worker serving a HUD, both stated with their enforcement in `CLAUDE.md` → *Maintenance Invariants*: **(a)** a mutex-guarded member is guarded at *every* access site, including private helpers that merely look like they sit inside locked code (copy under the lock, pass the snapshot in); **(b)** snapshot game-thread inputs at task start, and join before teardown. The records fetch thread is the worked example of both - it now lives in `core/records_fetcher.*` while `RecordsHud` owns the data it fills, snapshots provider/track in `startFetch()`, and is joined via `RecordsHud::joinFetchThread` from `HudManager::clear()` *before* cached HUD pointers are nulled, because the worker dirties TimingHud on completion.

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

### Build Targets

The **game is the target**, not the configuration - `Debug`/`Release` are plain
configurations, and each game its own CMake target:

| Target | Output | Install Location |
|---------------|--------|------------------|
| `mxbmrp3` | `mxbmrp3.dlo` | MX Bikes `plugins/` |
| `mxbmrp3_gpb` | `mxbmrp3_gpb.dlo` | GP Bikes `plugins/` |
| `mxbmrp3_krp` | `mxbmrp3_krp.dlo` | Kart Racing Pro `plugins/` |
| (future) | `mxbmrp3_wrs.dlo` | WRS `plugins/` |

One `mxb_add_plugin(NAME GAME_DEF API_TU)` call per game in
[`mxbmrp3/CMakeLists.txt`](mxbmrp3/CMakeLists.txt) builds the shared sources plus
exactly one export TU, so the API file is *selected* rather than excluded:

```cmake
mxb_add_plugin(mxbmrp3     GAME_MXBIKES mxb_api.cpp)
mxb_add_plugin(mxbmrp3_gpb GAME_GPBIKES gpb_api.cpp)
mxb_add_plugin(mxbmrp3_krp GAME_KRP     krp_api.cpp)
```

Output lands in `build/<GAME>-<Config>/`. [`DEVELOPMENT.md`](DEVELOPMENT.md) has
the presets and deploy variables.

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

Deliberately not a table of every file - the tree is the index, and a
hand-maintained mirror of it goes stale on the first rename. See *Project
Structure* above for the directory map and CLAUDE.md's *Where Things Live* for
the handful of placements that aren't guessable.

Runtime paths, which are the part you genuinely can't find by looking:

| What | Where |
|------|-------|
| Settings, stats, rumble profiles, log | `{save_path}/mxbmrp3/` |
| Crash dumps | `{save_path}/mxbmrp3/crashes/` |
| Callback tapes (recorder) | `{save_path}/mxbmrp3/tapes/` |
| Shipped assets | `{game_path}/plugins/mxbmrp3_data/{fonts,textures,icons,themes,gamepads,pitboards,spotters,web}/` |
| User asset overrides (synced on launch) | `{save_path}/mxbmrp3/` + the same subfolders |
| Build output | `build/<GAME>-<Config>/`, e.g. `build/MXB-Release/` |

## Quick Reference: Adding Features

**[`CLAUDE.md`](CLAUDE.md) → *Common Tasks* is the authoritative version** of this
list and is kept current with the registries it describes (adding a HUD, a
per-HUD or global setting, a settings tab, a web-overlay field, a game-specific
feature, a new game). It used to be duplicated here in a shorter, vaguer form -
two descriptions of one subject, where the copy drifts and the reader can't tell
which is stale. Only the entries CLAUDE.md doesn't cover live here:

| Task | Steps |
|------|-------|
| Add new data type | Add struct to PluginData, add DataChangeType enum |
| Add tooltip | Add entry to the maps in `core/tooltip_manager.h`, pass tooltipId to control helper |
| Add keyboard shortcut | Handle in HudManager::processKeyboardInput() |
| Add new handler | Create handler class, route from PluginManager |
| Add new font | Place `.fnt` file in `mxbmrp3_data/fonts/` (auto-discovered) |
| Add new texture | Place `.tga` file in `mxbmrp3_data/textures/` (auto-discovered) |
| Add new icon | Place `.tga` file in `mxbmrp3_data/icons/` (auto-discovered, alphabetical order) |
| Add new event log type | Add enum to `event_log_types.h`, add flag, update `eventLogTypeToFlag()`, add to handlers |
