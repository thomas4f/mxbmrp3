// ============================================================================
// game/game_config.h
// Compile-time game selection and adapter configuration
// ============================================================================
// This header selects the appropriate game adapter based on preprocessor defines.
// Build configurations should define exactly one of:
//   - GAME_MXBIKES   (MX Bikes - motocross)
//   - GAME_GPBIKES   (GP Bikes - road racing motorcycles)
//   - GAME_WRS       (World Racing Series - cars)
//   - GAME_KRP       (Kart Racing Pro - karts)
//
// Example Visual Studio project configuration:
//   <PreprocessorDefinitions>GAME_MXBIKES;%(PreprocessorDefinitions)</PreprocessorDefinitions>
// ============================================================================
#pragma once

// ============================================================================
// Game Selection Validation
// ============================================================================

#if defined(GAME_MXBIKES)
    #define GAME_SELECTED 1
    #define GAME_NAME "MX Bikes"
    #define GAME_SHORT_NAME "MXB"
    #define GAME_DLO_NAME "mxbmrp3.dlo"
#endif

#if defined(GAME_GPBIKES)
    #ifdef GAME_SELECTED
        #error "Multiple game targets defined! Only one of GAME_MXBIKES, GAME_GPBIKES, GAME_WRS, GAME_KRP may be defined."
    #endif
    #define GAME_SELECTED 1
    #define GAME_NAME "GP Bikes"
    #define GAME_SHORT_NAME "GPB"
    #define GAME_DLO_NAME "mxbmrp3_gpb.dlo"
#endif

#if defined(GAME_WRS)
    #ifdef GAME_SELECTED
        #error "Multiple game targets defined! Only one of GAME_MXBIKES, GAME_GPBIKES, GAME_WRS, GAME_KRP may be defined."
    #endif
    #define GAME_SELECTED 1
    #define GAME_NAME "World Racing Series"
    #define GAME_SHORT_NAME "WRS"
    #define GAME_DLO_NAME "mxbmrp3_wrs.dlo"
#endif

#if defined(GAME_KRP)
    #ifdef GAME_SELECTED
        #error "Multiple game targets defined! Only one of GAME_MXBIKES, GAME_GPBIKES, GAME_WRS, GAME_KRP may be defined."
    #endif
    #define GAME_SELECTED 1
    #define GAME_NAME "Kart Racing Pro"
    #define GAME_SHORT_NAME "KRP"
    #define GAME_DLO_NAME "mxbmrp3_krp.dlo"
#endif

// Default to MX Bikes if no game specified (for backward compatibility)
#ifndef GAME_SELECTED
    #define GAME_MXBIKES
    #define GAME_NAME "MX Bikes"
    #define GAME_SHORT_NAME "MXB"
    #define GAME_DLO_NAME "mxbmrp3.dlo"
    #pragma message("Warning: No game target defined. Defaulting to GAME_MXBIKES. Define GAME_MXBIKES, GAME_GPBIKES, GAME_WRS, or GAME_KRP for explicit selection.")
#endif

// ============================================================================
// Include Game-Specific Headers and Adapters
// ============================================================================

#if defined(GAME_MXBIKES)
    #include "../vendor/piboso/mxb_api.h"
    #include "adapters/mxbikes_adapter.h"
    namespace Game = Adapters::MXBikes;
#elif defined(GAME_GPBIKES)
    #include "../vendor/piboso/gpb_api.h"
    #include "adapters/gpbikes_adapter.h"
    namespace Game = Adapters::GPBikes;
#elif defined(GAME_WRS)
    #include "../vendor/piboso/wrs_api.h"
    #include "adapters/wrs_adapter.h"
    namespace Game = Adapters::WRS;
#elif defined(GAME_KRP)
    #include "../vendor/piboso/krp_api.h"
    #include "adapters/krp_adapter.h"
    namespace Game = Adapters::KRP;
#endif

// ============================================================================
// Convenience Aliases
// ============================================================================

// The active game adapter type - use Game::Adapter in code
// Example: Unified::TelemetryData telem = Game::Adapter::toTelemetry(rawData);

// ============================================================================
// Feature Detection Macros
// ============================================================================
// These allow compile-time conditional code for game-specific features

// Holeshot timing (MX Bikes only)
#if defined(GAME_MXBIKES)
    #define GAME_HAS_HOLESHOT 1
#else
    #define GAME_HAS_HOLESHOT 0
#endif

// Speed trap in RaceLap (all except MX Bikes)
#if defined(GAME_GPBIKES) || defined(GAME_WRS) || defined(GAME_KRP)
    #define GAME_HAS_RACE_SPEED 1
#else
    #define GAME_HAS_RACE_SPEED 0
#endif

// Track temperature (all except MX Bikes)
#if defined(GAME_GPBIKES) || defined(GAME_WRS) || defined(GAME_KRP)
    #define GAME_HAS_TRACK_TEMP 1
#else
    #define GAME_HAS_TRACK_TEMP 0
#endif

// Session series (KRP only)
#if defined(GAME_KRP)
    #define GAME_HAS_SESSION_SERIES 1
#else
    #define GAME_HAS_SESSION_SERIES 0
#endif

// ECU/Traction Control (GP Bikes only)
#if defined(GAME_GPBIKES)
    #define GAME_HAS_ECU 1
#else
    #define GAME_HAS_ECU 0
#endif

// Tyre temperature data (GP Bikes only)
#if defined(GAME_GPBIKES)
    #define GAME_HAS_TYRE_TEMP 1
#else
    #define GAME_HAS_TYRE_TEMP 0
#endif

// Penalty clear/change (GP Bikes, WRS, KRP)
#if defined(GAME_GPBIKES) || defined(GAME_WRS) || defined(GAME_KRP)
    #define GAME_HAS_PENALTY_MANAGEMENT 1
#else
    #define GAME_HAS_PENALTY_MANAGEMENT 0
#endif

// Rolling start (WRS, KRP)
#if defined(GAME_WRS) || defined(GAME_KRP)
    #define GAME_HAS_ROLLING_START 1
#else
    #define GAME_HAS_ROLLING_START 0
#endif

// Crashed state in TrackPosition (MX Bikes, GP Bikes)
#if defined(GAME_MXBIKES) || defined(GAME_GPBIKES)
    #define GAME_HAS_CRASH_STATE 1
#else
    #define GAME_HAS_CRASH_STATE 0
#endif

// Vehicle lean angle in RaceVehicleData (bikes only)
#if defined(GAME_MXBIKES) || defined(GAME_GPBIKES)
    #define GAME_HAS_LEAN_ANGLE 1
#else
    #define GAME_HAS_LEAN_ANGLE 0
#endif

// Steering input in RaceVehicleData (cars/karts only)
#if defined(GAME_WRS) || defined(GAME_KRP)
    #define GAME_HAS_STEER_INPUT 1
#else
    #define GAME_HAS_STEER_INPUT 0
#endif

// 4-sector timing (GP Bikes has 3 splits = 4 sectors)
#if defined(GAME_GPBIKES)
    #define GAME_SECTOR_COUNT 4
#else
    #define GAME_SECTOR_COUNT 3
#endif

// External lap records providers (CBR, MXB-Ranked - MX Bikes only)
#if defined(GAME_MXBIKES)
    #define GAME_HAS_RECORDS_PROVIDER 1
#else
    #define GAME_HAS_RECORDS_PROVIDER 0
#endif

// Discord Rich Presence (MX Bikes only - requires separate Discord app per game)
#if defined(GAME_MXBIKES)
    #define GAME_HAS_DISCORD 1
#else
    #define GAME_HAS_DISCORD 0
#endif

// Server info (name, password, player count) via memory reading (MX Bikes only)
#if defined(GAME_MXBIKES)
    #define GAME_HAS_SERVER_INFO 1
#else
    #define GAME_HAS_SERVER_INFO 0
#endif
