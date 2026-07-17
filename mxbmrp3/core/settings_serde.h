// ============================================================================
// core/settings_serde.h
// Free helper functions for the settings layer: enum<->string converters,
// bitmask (named-key) save/load, base-HUD capture/apply/write, value
// validators, and section-name parsing. Extracted from settings_manager.cpp so
// the SettingsManager translation units and the per-HUD registry
// (settings_hud_registry) share ONE definition.
//
// These live in `namespace Settings` alongside the key constants (settings_keys.h)
// so their function-local `using namespace Keys::X;` directives resolve into
// `Settings` (nested below global) — this is load-bearing: at global scope those
// directives would leak names like RecordsCols::DATE into the global namespace and
// collide with <windows.h> (typedef double DATE). Keep helpers and Keys co-located.
//
// Definitions are `inline` (header-defined) so multiple TUs may include this
// without ODR violations; the linker folds the COMDAT copies.
// ============================================================================
#pragma once

#include "settings_manager.h"
#include "settings_keys.h"
#include "atomic_file_writer.h"
#include "hud_manager.h"
#include "profile_manager.h"
#include "../diagnostics/logger.h"
#include "../hud/ideal_lap_hud.h"
#include "../hud/lap_log_hud.h"
#include "../hud/friends_hud.h"
#include "../hud/session_charts_hud.h"
#include "../hud/standings_hud.h"
#include "../hud/performance_hud.h"
#include "../hud/telemetry_hud.h"
#include "../hud/time_widget.h"
#include "../hud/clock_widget.h"
#include "../hud/position_widget.h"
#include "../hud/lap_widget.h"
#include "../hud/session_hud.h"
#include "../hud/speed_widget.h"
#include "../hud/gear_widget.h"
#include "../hud/speedo_widget.h"
#include "../hud/tacho_widget.h"
#include "../hud/timing_hud.h"
#include "../hud/gap_bar_hud.h"
#include "../hud/bars_widget.h"
#include "../hud/version_widget.h"
#include "../hud/notices_hud.h"
#include "../hud/fuel_widget.h"
#include "../hud/settings_button_widget.h"
#include "../hud/pointer_widget.h"
#include "../hud/map_hud.h"
#include "../hud/radar_hud.h"
#include "../hud/pitboard_hud.h"
// settings_hud.h is core (every game has the settings menu, and getSettingsHud() is
// used unconditionally below), and it pulls records_hud.h itself; both .cpp files are
// compiled on every game, so neither include may be gated on GAME_HAS_RECORDS_PROVIDER
// — gating it broke the GPB/KRP builds (SettingsHud left incomplete -> C2027). The
// *provider* feature stays runtime/registration-gated; only these includes are always on.
#include "../hud/records_hud.h"
#include "../hud/settings_hud.h"
#include "../hud/rumble_hud.h"
#include "../hud/helmet_overlay_hud.h"
#include "../hud/benchmark_widget.h"
#include "../hud/gamepad_widget.h"
#include "../hud/lean_widget.h"
#include "../hud/gforce_widget.h"
#include "../hud/compass_widget.h"
#if GAME_HAS_TYRE_TEMP
#include "../hud/tyre_temp_widget.h"
#endif
#if GAME_HAS_ECU
#include "../hud/ecu_widget.h"
#endif
#include "../hud/fmx_hud.h"
#include "../hud/stats_hud.h"
#include "../hud/event_log_hud.h"
#include "fmx_manager.h"
#include "color_config.h"
#include "font_config.h"
#include "ui_config.h"
#include "update_checker.h"
#include "update_downloader.h"
#if GAME_HAS_DISCORD
#include "discord_manager.h"
#endif
#if GAME_HAS_STEAM_FRIENDS
#include "steam_friends_manager.h"
#endif
#if GAME_HAS_HTTP_SERVER
#include "http_server.h"
#endif
#if GAME_HAS_RECORDER
#include "event_recorder.h"
#endif
#if GAME_HAS_ANALYTICS
#include "analytics_manager.h"
#endif
#include "xinput_reader.h"
#include "hotkey_manager.h"
#include "director_manager.h"
#include "companion_window.h"
#include "../hud/director_widget.h"
#include "tracked_riders_manager.h"
#include "asset_manager.h"
#include "../game/game_config.h"
#include <fstream>
#include <sstream>
#include <array>
#include <vector>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <ostream>
#include <string>

namespace Settings {

    // Enum string conversion helpers
    // These convert enums to/from stable string representations
    // ========================================================================

    // ColumnMode (TimingHud)
    inline const char* columnModeToString(ColumnMode mode) {
        switch (mode) {
            case ColumnMode::OFF: return "OFF";
            case ColumnMode::SPLITS: return "SPLITS";
            case ColumnMode::ALWAYS: return "ALWAYS";
            default: return "OFF";
        }
    }

    inline ColumnMode stringToColumnMode(const std::string& str, ColumnMode defaultVal = ColumnMode::OFF) {
        if (str == "OFF") return ColumnMode::OFF;
        if (str == "SPLITS") return ColumnMode::SPLITS;
        if (str == "ALWAYS") return ColumnMode::ALWAYS;
        DEBUG_WARN_F("Unknown ColumnMode '%s', using default", str.c_str());
        return defaultVal;
    }

    // StandingsHud::GapMode
    inline const char* gapModeToString(StandingsHud::GapMode mode) {
        switch (mode) {
            case StandingsHud::GapMode::OFF: return "OFF";
            case StandingsHud::GapMode::PLAYER: return "PLAYER";
            case StandingsHud::GapMode::ADJACENT: return "ADJACENT";
            case StandingsHud::GapMode::ALL: return "ALL";
            default: return "ALL";
        }
    }

    inline StandingsHud::GapMode stringToGapMode(const std::string& str, StandingsHud::GapMode defaultVal = StandingsHud::GapMode::ALL) {
        if (str == "OFF") return StandingsHud::GapMode::OFF;
        if (str == "PLAYER") return StandingsHud::GapMode::PLAYER;
        if (str == "ADJACENT") return StandingsHud::GapMode::ADJACENT;
        if (str == "ALL") return StandingsHud::GapMode::ALL;
        DEBUG_WARN_F("Unknown GapMode '%s', using default", str.c_str());
        return defaultVal;
    }

    // StandingsHud::PosGainMode
    inline const char* posGainModeToString(StandingsHud::PosGainMode mode) {
        switch (mode) {
            case StandingsHud::PosGainMode::OFF: return "OFF";
            case StandingsHud::PosGainMode::RACE_START: return "RACE_START";
            case StandingsHud::PosGainMode::LAST_SF: return "LAST_SF";
            case StandingsHud::PosGainMode::LAST_SPLIT: return "LAST_SPLIT";
            default: return "OFF";
        }
    }

    inline StandingsHud::PosGainMode stringToPosGainMode(const std::string& str, StandingsHud::PosGainMode defaultVal = StandingsHud::PosGainMode::OFF) {
        if (str == "OFF") return StandingsHud::PosGainMode::OFF;
        if (str == "RACE_START") return StandingsHud::PosGainMode::RACE_START;
        if (str == "LAST_SF") return StandingsHud::PosGainMode::LAST_SF;
        if (str == "LAST_SPLIT") return StandingsHud::PosGainMode::LAST_SPLIT;
        DEBUG_WARN_F("Unknown PosGainMode '%s', using default", str.c_str());
        return defaultVal;
    }

    // StandingsHud::GapReferenceMode
    inline const char* gapReferenceModeToString(StandingsHud::GapReferenceMode mode) {
        switch (mode) {
            case StandingsHud::GapReferenceMode::LEADER:      return "LEADER";
            case StandingsHud::GapReferenceMode::PLAYER:      return "PLAYER";
            case StandingsHud::GapReferenceMode::ALTERNATING: return "ALTERNATING";
            default: return "LEADER";
        }
    }

    inline StandingsHud::GapReferenceMode stringToGapReferenceMode(const std::string& str, StandingsHud::GapReferenceMode defaultVal = StandingsHud::GapReferenceMode::LEADER) {
        if (str == "LEADER") return StandingsHud::GapReferenceMode::LEADER;
        if (str == "PLAYER") return StandingsHud::GapReferenceMode::PLAYER;
        if (str == "ALTERNATING") return StandingsHud::GapReferenceMode::ALTERNATING;
        DEBUG_WARN_F("Unknown GapReferenceMode '%s', using default", str.c_str());
        return defaultVal;
    }

    // StandingsHud::AnimationMode
    inline const char* animationModeToString(StandingsHud::AnimationMode mode) {
        switch (mode) {
            case StandingsHud::AnimationMode::OFF:     return "OFF";
            case StandingsHud::AnimationMode::BASIC:   return "BASIC";
            case StandingsHud::AnimationMode::COLORED: return "COLORED";
            default: return "BASIC";
        }
    }

    inline StandingsHud::AnimationMode stringToAnimationMode(const std::string& str, StandingsHud::AnimationMode defaultVal = StandingsHud::AnimationMode::BASIC) {
        if (str == "OFF") return StandingsHud::AnimationMode::OFF;
        if (str == "BASIC") return StandingsHud::AnimationMode::BASIC;
        if (str == "COLORED") return StandingsHud::AnimationMode::COLORED;
        DEBUG_WARN_F("Unknown AnimationMode '%s', using default", str.c_str());
        return defaultVal;
    }

    // MapHud::RiderColorMode (also used by RadarHud)
    inline const char* riderColorModeToString(MapHud::RiderColorMode mode) {
        switch (mode) {
            case MapHud::RiderColorMode::UNIFORM: return "UNIFORM";
            case MapHud::RiderColorMode::BRAND: return "BRAND";
            case MapHud::RiderColorMode::RELATIVE_POS: return "RELATIVE_POS";
            default: return "UNIFORM";
        }
    }

    inline MapHud::RiderColorMode stringToRiderColorMode(const std::string& str, MapHud::RiderColorMode defaultVal = MapHud::RiderColorMode::UNIFORM) {
        if (str == "UNIFORM") return MapHud::RiderColorMode::UNIFORM;
        if (str == "BRAND") return MapHud::RiderColorMode::BRAND;
        if (str == "RELATIVE_POS") return MapHud::RiderColorMode::RELATIVE_POS;
        DEBUG_WARN_F("Unknown RiderColorMode '%s', using default", str.c_str());
        return defaultVal;
    }

    // MapHud::LabelMode (also used by RadarHud)
    inline const char* labelModeToString(MapHud::LabelMode mode) {
        switch (mode) {
            case MapHud::LabelMode::NONE: return "NONE";
            case MapHud::LabelMode::POSITION: return "POSITION";
            case MapHud::LabelMode::RACE_NUM: return "RACE_NUM";
            case MapHud::LabelMode::BOTH: return "BOTH";
            default: return "NONE";
        }
    }

    inline MapHud::LabelMode stringToLabelMode(const std::string& str, MapHud::LabelMode defaultVal = MapHud::LabelMode::NONE) {
        if (str == "NONE") return MapHud::LabelMode::NONE;
        if (str == "POSITION") return MapHud::LabelMode::POSITION;
        if (str == "RACE_NUM") return MapHud::LabelMode::RACE_NUM;
        if (str == "BOTH") return MapHud::LabelMode::BOTH;
        DEBUG_WARN_F("Unknown LabelMode '%s', using default", str.c_str());
        return defaultVal;
    }

    // MapHud::LabelAnchor (label position relative to the rider icon)
    inline const char* labelAnchorToString(MapHud::LabelAnchor anchor) {
        switch (anchor) {
            case MapHud::LabelAnchor::BELOW: return "BELOW";
            case MapHud::LabelAnchor::ABOVE: return "ABOVE";
            case MapHud::LabelAnchor::LEFT:  return "LEFT";
            case MapHud::LabelAnchor::RIGHT: return "RIGHT";
            default: return "BELOW";
        }
    }

    inline MapHud::LabelAnchor stringToLabelAnchor(const std::string& str, MapHud::LabelAnchor defaultVal = MapHud::LabelAnchor::BELOW) {
        if (str == "BELOW") return MapHud::LabelAnchor::BELOW;
        if (str == "ABOVE") return MapHud::LabelAnchor::ABOVE;
        if (str == "LEFT")  return MapHud::LabelAnchor::LEFT;
        if (str == "RIGHT") return MapHud::LabelAnchor::RIGHT;
        DEBUG_WARN_F("Unknown LabelAnchor '%s', using default", str.c_str());
        return defaultVal;
    }

    // Legacy map-detail preset -> scale/adaptive migration. Pre-1.27.6 INIs carry
    // `detail=AUTO|HIGH|LOW`; newer files carry detailScale/detailAdaptive
    // instead (see app_MapHud). AUTO was adaptive at what is now 100%; HIGH was
    // fixed 1.0m (= fixed 200%); LOW was fixed 4.0m (closest new point: fixed
    // 60% ≈ 3.3m).
    inline void applyLegacyMapDetail(MapHud& hud, const std::string& str) {
        // AUTO maps to a literal 100% — the old AUTO's exact density — NOT the
        // (leaner) new default, so upgraders keep the look they had.
        if (str == "AUTO")      { hud.setAdaptiveDetail(true);  hud.setDetailScale(1.0f); }
        else if (str == "HIGH") { hud.setAdaptiveDetail(false); hud.setDetailScale(2.0f); }
        else if (str == "LOW")  { hud.setAdaptiveDetail(false); hud.setDetailScale(0.6f); }
        else DEBUG_WARN_F("Unknown legacy Detail '%s', keeping current", str.c_str());
    }

    // MapHud::AnchorPoint
    inline const char* anchorPointToString(MapHud::AnchorPoint point) {
        switch (point) {
            case MapHud::AnchorPoint::TOP_LEFT: return "TOP_LEFT";
            case MapHud::AnchorPoint::TOP_RIGHT: return "TOP_RIGHT";
            case MapHud::AnchorPoint::BOTTOM_LEFT: return "BOTTOM_LEFT";
            case MapHud::AnchorPoint::BOTTOM_RIGHT: return "BOTTOM_RIGHT";
            default: return "TOP_LEFT";
        }
    }

    inline MapHud::AnchorPoint stringToAnchorPoint(const std::string& str, MapHud::AnchorPoint defaultVal = MapHud::AnchorPoint::TOP_LEFT) {
        if (str == "TOP_LEFT") return MapHud::AnchorPoint::TOP_LEFT;
        if (str == "TOP_RIGHT") return MapHud::AnchorPoint::TOP_RIGHT;
        if (str == "BOTTOM_LEFT") return MapHud::AnchorPoint::BOTTOM_LEFT;
        if (str == "BOTTOM_RIGHT") return MapHud::AnchorPoint::BOTTOM_RIGHT;
        DEBUG_WARN_F("Unknown AnchorPoint '%s', using default", str.c_str());
        return defaultVal;
    }

    // RadarHud::RiderColorMode (overload for RadarHud's type)
    inline const char* radarRiderColorModeToString(RadarHud::RiderColorMode mode) {
        switch (mode) {
            case RadarHud::RiderColorMode::UNIFORM: return "UNIFORM";
            case RadarHud::RiderColorMode::BRAND: return "BRAND";
            case RadarHud::RiderColorMode::RELATIVE_POS: return "RELATIVE_POS";
            default: return "UNIFORM";
        }
    }

    inline RadarHud::RiderColorMode stringToRadarRiderColorMode(const std::string& str, RadarHud::RiderColorMode defaultVal = RadarHud::RiderColorMode::UNIFORM) {
        if (str == "UNIFORM") return RadarHud::RiderColorMode::UNIFORM;
        if (str == "BRAND") return RadarHud::RiderColorMode::BRAND;
        if (str == "RELATIVE_POS") return RadarHud::RiderColorMode::RELATIVE_POS;
        DEBUG_WARN_F("Unknown RadarRiderColorMode '%s', using default", str.c_str());
        return defaultVal;
    }

    // RadarHud::LabelMode (overload for RadarHud's type)
    inline const char* radarLabelModeToString(RadarHud::LabelMode mode) {
        switch (mode) {
            case RadarHud::LabelMode::NONE: return "NONE";
            case RadarHud::LabelMode::POSITION: return "POSITION";
            case RadarHud::LabelMode::RACE_NUM: return "RACE_NUM";
            case RadarHud::LabelMode::BOTH: return "BOTH";
            default: return "NONE";
        }
    }

    inline RadarHud::LabelMode stringToRadarLabelMode(const std::string& str, RadarHud::LabelMode defaultVal = RadarHud::LabelMode::NONE) {
        if (str == "NONE") return RadarHud::LabelMode::NONE;
        if (str == "POSITION") return RadarHud::LabelMode::POSITION;
        if (str == "RACE_NUM") return RadarHud::LabelMode::RACE_NUM;
        if (str == "BOTH") return RadarHud::LabelMode::BOTH;
        DEBUG_WARN_F("Unknown RadarLabelMode '%s', using default", str.c_str());
        return defaultVal;
    }

    // GapBarHud::RiderColorMode
    inline const char* gapBarRiderColorModeToString(GapBarHud::RiderColorMode mode) {
        switch (mode) {
            case GapBarHud::RiderColorMode::UNIFORM: return "UNIFORM";
            case GapBarHud::RiderColorMode::BRAND: return "BRAND";
            case GapBarHud::RiderColorMode::RELATIVE_POS: return "RELATIVE_POS";
            default: return "RELATIVE_POS";
        }
    }

    inline GapBarHud::RiderColorMode stringToGapBarRiderColorMode(const std::string& str, GapBarHud::RiderColorMode defaultVal = GapBarHud::RiderColorMode::RELATIVE_POS) {
        if (str == "UNIFORM") return GapBarHud::RiderColorMode::UNIFORM;
        if (str == "BRAND") return GapBarHud::RiderColorMode::BRAND;
        if (str == "RELATIVE_POS") return GapBarHud::RiderColorMode::RELATIVE_POS;
        DEBUG_WARN_F("Unknown GapBarRiderColorMode '%s', using default", str.c_str());
        return defaultVal;
    }

    // RadarHud::ProximityArrowMode
    inline const char* proximityArrowModeToString(RadarHud::ProximityArrowMode mode) {
        switch (mode) {
            case RadarHud::ProximityArrowMode::OFF: return "OFF";
            case RadarHud::ProximityArrowMode::EDGE: return "EDGE";
            case RadarHud::ProximityArrowMode::CIRCLE: return "CIRCLE";
            default: return "OFF";
        }
    }

    inline RadarHud::ProximityArrowMode stringToProximityArrowMode(const std::string& str, RadarHud::ProximityArrowMode defaultVal = RadarHud::ProximityArrowMode::OFF) {
        if (str == "OFF") return RadarHud::ProximityArrowMode::OFF;
        if (str == "EDGE") return RadarHud::ProximityArrowMode::EDGE;
        if (str == "CIRCLE") return RadarHud::ProximityArrowMode::CIRCLE;
        DEBUG_WARN_F("Unknown ProximityArrowMode '%s', using default", str.c_str());
        return defaultVal;
    }

    // RadarHud::ProximityArrowColorMode
    inline const char* proximityArrowColorModeToString(RadarHud::ProximityArrowColorMode mode) {
        switch (mode) {
            case RadarHud::ProximityArrowColorMode::DISTANCE: return "DISTANCE";
            case RadarHud::ProximityArrowColorMode::POSITION: return "POSITION";
            default: return "DISTANCE";
        }
    }

    inline RadarHud::ProximityArrowColorMode stringToProximityArrowColorMode(const std::string& str, RadarHud::ProximityArrowColorMode defaultVal = RadarHud::ProximityArrowColorMode::DISTANCE) {
        if (str == "DISTANCE") return RadarHud::ProximityArrowColorMode::DISTANCE;
        if (str == "POSITION") return RadarHud::ProximityArrowColorMode::POSITION;
        DEBUG_WARN_F("Unknown ProximityArrowColorMode '%s', using default", str.c_str());
        return defaultVal;
    }

    // RadarHud::RadarMode
    inline const char* radarModeToString(RadarHud::RadarMode mode) {
        switch (mode) {
            case RadarHud::RadarMode::OFF: return "OFF";
            case RadarHud::RadarMode::ON: return "ON";
            case RadarHud::RadarMode::AUTO_HIDE: return "AUTO_HIDE";
            default: return "ON";
        }
    }

    inline RadarHud::RadarMode stringToRadarMode(const std::string& str, RadarHud::RadarMode defaultVal = RadarHud::RadarMode::ON) {
        if (str == "OFF") return RadarHud::RadarMode::OFF;
        if (str == "ON") return RadarHud::RadarMode::ON;
        if (str == "AUTO_HIDE") return RadarHud::RadarMode::AUTO_HIDE;
        DEBUG_WARN_F("Unknown RadarMode '%s', using default", str.c_str());
        return defaultVal;
    }

    // PitboardHud::DisplayMode
    inline const char* pitboardDisplayModeToString(uint8_t mode) {
        switch (mode) {
            case PitboardHud::MODE_ALWAYS: return "ALWAYS";
            case PitboardHud::MODE_PIT: return "PIT";
            case PitboardHud::MODE_SPLITS: return "SPLITS";
            default: return "ALWAYS";
        }
    }

    inline uint8_t stringToPitboardDisplayMode(const std::string& str, uint8_t defaultVal = PitboardHud::MODE_ALWAYS) {
        if (str == "ALWAYS") return PitboardHud::MODE_ALWAYS;
        if (str == "PIT") return PitboardHud::MODE_PIT;
        if (str == "SPLITS") return PitboardHud::MODE_SPLITS;
        DEBUG_WARN_F("Unknown PitboardDisplayMode '%s', using default", str.c_str());
        return defaultVal;
    }

    inline const char* pitboardGapCompareModeToString(uint8_t mode) {
        switch (mode) {
            case PitboardHud::GAP_AUTO:       return "AUTO";
            case PitboardHud::GAP_LEADER:     return "LEADER";
            case PitboardHud::GAP_SESSION_PB: return "SESSION_PB";
            case PitboardHud::GAP_IDEAL:      return "IDEAL";
            case PitboardHud::GAP_ALLTIME_PB: return "ALLTIME_PB";
            case PitboardHud::GAP_OVERALL:    return "OVERALL";
            case PitboardHud::GAP_RECORD:     return "RECORD";
            default: return "AUTO";
        }
    }

    inline uint8_t stringToPitboardGapCompareMode(const std::string& str, uint8_t defaultVal = PitboardHud::GAP_AUTO) {
        if (str == "AUTO") return PitboardHud::GAP_AUTO;
        if (str == "LEADER") return PitboardHud::GAP_LEADER;
        if (str == "SESSION_PB") return PitboardHud::GAP_SESSION_PB;
        if (str == "IDEAL") return PitboardHud::GAP_IDEAL;
        if (str == "ALLTIME_PB") return PitboardHud::GAP_ALLTIME_PB;
        if (str == "OVERALL") return PitboardHud::GAP_OVERALL;
        if (str == "RECORD") return PitboardHud::GAP_RECORD;
        DEBUG_WARN_F("Unknown PitboardGapCompareMode '%s', using default", str.c_str());
        return defaultVal;
    }

    // TelemetryHud::DisplayMode / PerformanceHud::DisplayMode (same values)
    inline const char* displayModeToString(uint8_t mode) {
        switch (mode) {
            case TelemetryHud::DISPLAY_GRAPHS: return "GRAPHS";
            case TelemetryHud::DISPLAY_VALUES: return "VALUES";
            case TelemetryHud::DISPLAY_BOTH: return "BOTH";
            default: return "BOTH";
        }
    }

    inline uint8_t stringToDisplayMode(const std::string& str, uint8_t defaultVal = TelemetryHud::DISPLAY_BOTH) {
        if (str == "GRAPHS") return TelemetryHud::DISPLAY_GRAPHS;
        if (str == "VALUES") return TelemetryHud::DISPLAY_VALUES;
        if (str == "BOTH") return TelemetryHud::DISPLAY_BOTH;
        DEBUG_WARN_F("Unknown DisplayMode '%s', using default", str.c_str());
        return defaultVal;
    }

#if GAME_HAS_RECORDS_PROVIDER
    // RecordsHud::DataProvider
    inline const char* dataProviderToString(RecordsHud::DataProvider provider) {
        switch (provider) {
            case RecordsHud::DataProvider::CBR: return "CBR";
            case RecordsHud::DataProvider::MXB_RANKED: return "MXB_RANKED";
            default: return "CBR";
        }
    }

    inline RecordsHud::DataProvider stringToDataProvider(const std::string& str, RecordsHud::DataProvider defaultVal = RecordsHud::DataProvider::CBR) {
        if (str == "CBR") return RecordsHud::DataProvider::CBR;
        if (str == "MXB_RANKED") return RecordsHud::DataProvider::MXB_RANKED;
        DEBUG_WARN_F("Unknown DataProvider '%s', using default", str.c_str());
        return defaultVal;
    }
#endif

    // SpeedWidget::SpeedUnit
    inline const char* speedUnitToString(SpeedWidget::SpeedUnit unit) {
        switch (unit) {
            case SpeedWidget::SpeedUnit::MPH: return "MPH";
            case SpeedWidget::SpeedUnit::KMH: return "KMH";
            default: return "MPH";
        }
    }

    inline SpeedWidget::SpeedUnit stringToSpeedUnit(const std::string& str, SpeedWidget::SpeedUnit defaultVal = SpeedWidget::SpeedUnit::MPH) {
        if (str == "MPH") return SpeedWidget::SpeedUnit::MPH;
        if (str == "KMH") return SpeedWidget::SpeedUnit::KMH;
        DEBUG_WARN_F("Unknown SpeedUnit '%s', using default", str.c_str());
        return defaultVal;
    }

    // CompassWidget::Style
    inline const char* compassStyleToString(CompassWidget::Style style) {
        switch (style) {
            case CompassWidget::Style::Classic: return "classic";
            case CompassWidget::Style::Modern:  return "modern";
            default: return "classic";
        }
    }

    inline CompassWidget::Style stringToCompassStyle(const std::string& str, CompassWidget::Style defaultVal = CompassWidget::Style::Classic) {
        if (str == "classic") return CompassWidget::Style::Classic;
        if (str == "modern") return CompassWidget::Style::Modern;
        DEBUG_WARN_F("Unknown Compass style '%s', using default", str.c_str());
        return defaultVal;
    }

    // FuelWidget::FuelUnit
    inline const char* fuelUnitToString(FuelWidget::FuelUnit unit) {
        switch (unit) {
            case FuelWidget::FuelUnit::LITERS: return "LITERS";
            case FuelWidget::FuelUnit::GALLONS: return "GALLONS";
            default: return "LITERS";
        }
    }

    inline FuelWidget::FuelUnit stringToFuelUnit(const std::string& str, FuelWidget::FuelUnit defaultVal = FuelWidget::FuelUnit::LITERS) {
        if (str == "LITERS") return FuelWidget::FuelUnit::LITERS;
        if (str == "GALLONS") return FuelWidget::FuelUnit::GALLONS;
        DEBUG_WARN_F("Unknown FuelUnit '%s', using default", str.c_str());
        return defaultVal;
    }

    // TemperatureUnit
    inline const char* tempUnitToString(TemperatureUnit unit) {
        switch (unit) {
            case TemperatureUnit::CELSIUS: return "CELSIUS";
            case TemperatureUnit::FAHRENHEIT: return "FAHRENHEIT";
            default: return "CELSIUS";
        }
    }

    inline TemperatureUnit stringToTempUnit(const std::string& str, TemperatureUnit defaultVal = TemperatureUnit::CELSIUS) {
        if (str == "CELSIUS") return TemperatureUnit::CELSIUS;
        if (str == "FAHRENHEIT") return TemperatureUnit::FAHRENHEIT;
        DEBUG_WARN_F("Unknown TemperatureUnit '%s', using default", str.c_str());
        return defaultVal;
    }

    // PBScope
    inline const char* pbScopeToString(PBScope scope) {
        switch (scope) {
            case PBScope::BIKE: return "BIKE";
            case PBScope::CATEGORY: return "CATEGORY";
            default: return "BIKE";
        }
    }

    inline PBScope stringToPBScope(const std::string& str, PBScope defaultVal = PBScope::CATEGORY) {
        if (str == "BIKE") return PBScope::BIKE;
        if (str == "CATEGORY") return PBScope::CATEGORY;
        DEBUG_WARN_F("Unknown PBScope '%s', using default", str.c_str());
        return defaultVal;
    }

    // DisplayTarget (where the HUD is drawn: in-game / companion window / both)
    inline const char* displayTargetToString(DisplayTarget t) {
        switch (t) {
            case DisplayTarget::COMPANION: return "COMPANION";
            case DisplayTarget::BOTH:      return "BOTH";
            case DisplayTarget::IN_GAME:   return "IN_GAME";
            default: return "IN_GAME";
        }
    }

    inline DisplayTarget stringToDisplayTarget(const std::string& str, DisplayTarget defaultVal = DisplayTarget::IN_GAME) {
        if (str == "IN_GAME")   return DisplayTarget::IN_GAME;
        if (str == "COMPANION") return DisplayTarget::COMPANION;
        if (str == "BOTH")      return DisplayTarget::BOTH;
        DEBUG_WARN_F("Unknown DisplayTarget '%s', using default", str.c_str());
        return defaultVal;
    }

    // ========================================================================

    // Parse a float from an INI string, rejecting non-finite results. A malformed
    // string still throws via std::stof (handled by the caller's existing catch), but
    // "nan"/"inf" parse successfully and would otherwise slip past every "< MIN || > MAX"
    // range check (NaN compares false both ways) and poison live quad/rumble math — and
    // auto-save would re-persist the bad value. Returns `fallback` for non-finite input.
    inline float parseFiniteFloat(const std::string& value, float fallback = 0.0f) {
        float parsed = std::stof(value, nullptr);
        return std::isfinite(parsed) ? parsed : fallback;
    }

    // Validation helper functions
    inline float validateScale(float value) {
        using namespace PluginConstants::SettingsLimits;
        if (!std::isfinite(value)) {
            DEBUG_WARN_F("Non-finite scale value, using 1.0");
            return 1.0f;
        }
        if (value < MIN_SCALE || value > MAX_SCALE) {
            DEBUG_WARN_F("Invalid scale value %.2f, clamping to [%.2f, %.2f]",
                        value, MIN_SCALE, MAX_SCALE);
            return (value < MIN_SCALE) ? MIN_SCALE : MAX_SCALE;
        }
        return value;
    }

    inline uint8_t validateDisplayMode(int value) {
        if (value < 0 || value > 255) {
            DEBUG_WARN_F("Invalid display mode value %d (must be 0-255), using default 0", value);
            return 0;
        }
        return static_cast<uint8_t>(value);
    }

    inline float validateOpacity(float value) {
        using namespace PluginConstants::SettingsLimits;
        if (!std::isfinite(value)) {
            DEBUG_WARN_F("Non-finite opacity value, using %.2f", MAX_OPACITY);
            return MAX_OPACITY;
        }
        if (value < MIN_OPACITY || value > MAX_OPACITY) {
            DEBUG_WARN_F("Invalid opacity value %.2f, clamping to [%.2f, %.2f]",
                        value, MIN_OPACITY, MAX_OPACITY);
            return (value < MIN_OPACITY) ? MIN_OPACITY : MAX_OPACITY;
        }
        return value;
    }

    inline float validateOffset(float value) {
        using namespace PluginConstants::SettingsLimits;
        if (!std::isfinite(value)) {
            DEBUG_WARN_F("Non-finite offset value, using 0.0");
            return 0.0f;
        }
        if (value < MIN_OFFSET || value > MAX_OFFSET) {
            DEBUG_WARN_F("Invalid offset value %.2f, clamping to [%.2f, %.2f]",
                        value, MIN_OFFSET, MAX_OFFSET);
            return (value < MIN_OFFSET) ? MIN_OFFSET : MAX_OFFSET;
        }
        return value;
    }

    inline int validateDisplayRows(int value) {
        using namespace PluginConstants::SettingsLimits;
        if (value < MIN_DISPLAY_ROWS || value > MAX_DISPLAY_ROWS) {
            DEBUG_WARN_F("Invalid display row count %d, clamping to [%d, %d]",
                        value, MIN_DISPLAY_ROWS, MAX_DISPLAY_ROWS);
            return (value < MIN_DISPLAY_ROWS) ? MIN_DISPLAY_ROWS : MAX_DISPLAY_ROWS;
        }
        return value;
    }

    inline int validateDisplayLaps(int value) {
        using namespace PluginConstants::SettingsLimits;
        if (value < MIN_DISPLAY_LAPS || value > MAX_DISPLAY_LAPS) {
            DEBUG_WARN_F("Invalid display lap count %d, clamping to [%d, %d]",
                        value, MIN_DISPLAY_LAPS, MAX_DISPLAY_LAPS);
            return (value < MIN_DISPLAY_LAPS) ? MIN_DISPLAY_LAPS : MAX_DISPLAY_LAPS;
        }
        return value;
    }

    inline float validateTrackWidthScale(float value) {
        if (!std::isfinite(value)) {
            DEBUG_WARN_F("Non-finite track width scale, using default");
            return MapHud::DEFAULT_TRACK_WIDTH_SCALE;
        }
        if (value < MapHud::MIN_TRACK_WIDTH_SCALE || value > MapHud::MAX_TRACK_WIDTH_SCALE) {
            DEBUG_WARN_F("Invalid track width scale %.2f, clamping to [%.2f, %.2f]",
                        value, MapHud::MIN_TRACK_WIDTH_SCALE, MapHud::MAX_TRACK_WIDTH_SCALE);
            return (value < MapHud::MIN_TRACK_WIDTH_SCALE) ? MapHud::MIN_TRACK_WIDTH_SCALE : MapHud::MAX_TRACK_WIDTH_SCALE;
        }
        return value;
    }

    inline float validateZoomDistance(float value) {
        if (!std::isfinite(value)) {
            DEBUG_WARN_F("Non-finite zoom distance, using default");
            return MapHud::DEFAULT_ZOOM_DISTANCE;
        }
        if (value < MapHud::MIN_ZOOM_DISTANCE || value > MapHud::MAX_ZOOM_DISTANCE) {
            DEBUG_WARN_F("Invalid zoom distance %.2f, clamping to [%.2f, %.2f]",
                        value, MapHud::MIN_ZOOM_DISTANCE, MapHud::MAX_ZOOM_DISTANCE);
            return (value < MapHud::MIN_ZOOM_DISTANCE) ? MapHud::MIN_ZOOM_DISTANCE : MapHud::MAX_ZOOM_DISTANCE;
        }
        return value;
    }

    // Icon shape helpers - convert between shape index and filename
    // Shape index is 1-based offset into icon list (0 = off/none)
    inline std::string shapeIndexToFilename(int shapeIndex) {
        if (shapeIndex <= 0) return "Off";
        const auto& assetMgr = AssetManager::getInstance();
        int spriteIndex = assetMgr.getFirstIconSpriteIndex() + shapeIndex - 1;
        std::string filename = assetMgr.getIconFilename(spriteIndex);
        return filename.empty() ? "Off" : filename;
    }

    inline int filenameToShapeIndex(const std::string& filename, int defaultShape) {
        if (filename.empty() || filename == "Off") return 0;
        const auto& assetMgr = AssetManager::getInstance();
        int spriteIndex = assetMgr.getIconSpriteIndex(filename);
        if (spriteIndex <= 0) return defaultShape;
        return spriteIndex - assetMgr.getFirstIconSpriteIndex() + 1;
    }

    // Helper to format a section name with profile index
    inline std::string formatSectionName(const char* hudName, ProfileType profile) {
        return std::string(hudName) + ":" + std::to_string(static_cast<int>(profile));
    }

    // Parse section name to extract HUD name and profile index
    // Returns true if successfully parsed, false if no profile index (global section)
    // Convert profile name to ProfileType (returns -1 if not found)
    inline int profileNameToIndex(const std::string& name) {
        if (name == "Practice") return static_cast<int>(ProfileType::PRACTICE);
        if (name == "Qualify")  return static_cast<int>(ProfileType::QUALIFY);
        if (name == "Race")     return static_cast<int>(ProfileType::RACE);
        if (name == "Spectate") return static_cast<int>(ProfileType::SPECTATE);
        return -1;
    }

    // Parse section name into HUD name and profile index
    // Supports both old format [HudName:0] and new format [HudName:Practice]
    // Returns true if this is a profile-specific section, false if base/global section
    inline bool parseSectionName(const std::string& section, std::string& hudName, int& profileIndex) {
        size_t colonPos = section.find(':');
        if (colonPos == std::string::npos) {
            hudName = section;
            profileIndex = -1;  // Base/global section (no profile suffix)
            return false;
        }
        hudName = section.substr(0, colonPos);
        std::string suffix = section.substr(colonPos + 1);

        // Try parsing as profile name first (new format: "Practice", "Qualify", etc.)
        profileIndex = profileNameToIndex(suffix);
        if (profileIndex >= 0) {
            return true;
        }

        // Fall back to numeric index (old format: "0", "1", etc.) - for migration
        try {
            profileIndex = std::stoi(suffix);
            return true;
        } catch (...) {
            hudName = section;
            profileIndex = -1;
            return false;
        }
    }

    // Helper to capture base HUD properties to a settings map
    // Set includePosition=false for HUDs that use anchor-based positioning (e.g., MapHud)
    // Color slot key names for per-HUD INI overrides (color_primary, color_secondary, etc.)
    //
    // When ColorSlot or FontCategory grows, both the toKey() and parseKey() lookups below
    // must be updated in lock-step. These static_asserts catch the easy mistake of adding
    // a new enum value without updating the INI key mappings.
    static_assert(static_cast<int>(ColorSlot::COUNT) == 10,
                  "ColorSlot enum changed — update colorSlotToKey() and parseColorKey() below.");
    static_assert(static_cast<int>(FontCategory::COUNT) == 6,
                  "FontCategory enum changed — update fontCategoryToKey() and parseFontKey() below.");

    inline const char* colorSlotToKey(ColorSlot slot) {
        switch (slot) {
            case ColorSlot::PRIMARY:    return "color_primary";
            case ColorSlot::SECONDARY:  return "color_secondary";
            case ColorSlot::TERTIARY:   return "color_tertiary";
            case ColorSlot::MUTED:      return "color_muted";
            case ColorSlot::BACKGROUND: return "color_background";
            case ColorSlot::POSITIVE:   return "color_positive";
            case ColorSlot::WARNING:    return "color_warning";
            case ColorSlot::NEUTRAL:    return "color_neutral";
            case ColorSlot::NEGATIVE:   return "color_negative";
            case ColorSlot::ACCENT:     return "color_accent";
            default:                    return nullptr;
        }
    }

    // Font category key names for per-HUD INI overrides (font_title, font_normal, etc.)
    inline const char* fontCategoryToKey(FontCategory category) {
        switch (category) {
            case FontCategory::TITLE:   return "font_title";
            case FontCategory::NORMAL:  return "font_normal";
            case FontCategory::STRONG:  return "font_strong";
            case FontCategory::DIGITS:  return "font_digits";
            case FontCategory::MARKER:  return "font_marker";
            case FontCategory::SMALL:   return "font_small";
            default:                    return nullptr;
        }
    }

    // Parse color slot from INI key name (returns ColorSlot::COUNT if not a color key)
    inline ColorSlot parseColorKey(const std::string& key) {
        if (key == "color_primary")    return ColorSlot::PRIMARY;
        if (key == "color_secondary")  return ColorSlot::SECONDARY;
        if (key == "color_tertiary")   return ColorSlot::TERTIARY;
        if (key == "color_muted")      return ColorSlot::MUTED;
        if (key == "color_background") return ColorSlot::BACKGROUND;
        if (key == "color_positive")   return ColorSlot::POSITIVE;
        if (key == "color_warning")    return ColorSlot::WARNING;
        if (key == "color_neutral")    return ColorSlot::NEUTRAL;
        if (key == "color_negative")   return ColorSlot::NEGATIVE;
        if (key == "color_accent")     return ColorSlot::ACCENT;
        return ColorSlot::COUNT;
    }

    // Parse font category from INI key name (returns FontCategory::COUNT if not a font key)
    inline FontCategory parseFontKey(const std::string& key) {
        if (key == "font_title")   return FontCategory::TITLE;
        if (key == "font_normal")  return FontCategory::NORMAL;
        if (key == "font_strong")  return FontCategory::STRONG;
        if (key == "font_digits")  return FontCategory::DIGITS;
        if (key == "font_marker")  return FontCategory::MARKER;
        if (key == "font_small")   return FontCategory::SMALL;
        return FontCategory::COUNT;
    }

    inline void captureBaseHudSettings(SettingsManager::HudSettings& settings, const BaseHud& hud, bool includePosition = true) {
        using namespace Keys::Base;
        settings[VISIBLE] = std::to_string(hud.isVisible() ? 1 : 0);
        settings[SHOW_TITLE] = std::to_string(hud.getShowTitle() ? 1 : 0);
        settings[SHOW_BG_TEXTURE] = std::to_string(hud.getShowBackgroundTexture() ? 1 : 0);
        settings[TEXTURE_VARIANT] = std::to_string(hud.getTextureVariant());
        settings[BG_OPACITY] = std::to_string(hud.getBackgroundOpacity());
        settings[SCALE] = std::to_string(hud.getScale());
        if (includePosition) {
            settings[OFFSET_X] = std::to_string(hud.getOffsetX());
            settings[OFFSET_Y] = std::to_string(hud.getOffsetY());
        }
        // Companion instance — persist only when it has actually DIVERGED from the game,
        // not merely when configured. Opening the companion window snapshots the game
        // layout into EVERY HUD (decouple-from-the-start), so gating on isCompanionConfigured()
        // alone would write companion keys for HUDs the user never touched on the companion —
        // bloating the INI and, worse, pinning those HUDs to their old position so a changed
        // default in a later version wouldn't take effect on the companion. A snapshot copies
        // the game values verbatim, so an untouched companion compares exactly equal here and
        // is left unpersisted — it simply re-snapshots from the game on the next open, keeping
        // the save sparse and upgrade-safe. A HUD moved/toggled on the companion (or one whose
        // game surface moved after the companion opened, so the two genuinely differ) persists.
        if (hud.isCompanionConfigured() &&
            (hud.getCompanionVisible() != hud.isVisible() ||
             hud.getCompanionOffsetX() != hud.getOffsetX() ||
             hud.getCompanionOffsetY() != hud.getOffsetY())) {
            settings[COMPANION_CONFIGURED] = "1";
            settings[COMPANION_VISIBLE] = std::to_string(hud.getCompanionVisible() ? 1 : 0);
            settings[COMPANION_X] = std::to_string(hud.getCompanionOffsetX());
            settings[COMPANION_Y] = std::to_string(hud.getCompanionOffsetY());
        }

        // Capture per-HUD color overrides (only if set)
        for (int i = 0; i < static_cast<int>(ColorSlot::COUNT); ++i) {
            ColorSlot slot = static_cast<ColorSlot>(i);
            if (hud.hasColorOverride(slot)) {
                const char* key = colorSlotToKey(slot);
                if (key) {
                    settings[key] = PluginUtils::formatColorHex(hud.getColorOverrideValue(slot));
                }
            }
        }

        // Capture per-HUD font overrides (only if set)
        for (int i = 0; i < static_cast<int>(FontCategory::COUNT); ++i) {
            FontCategory category = static_cast<FontCategory>(i);
            if (hud.hasFontOverride(category)) {
                const char* key = fontCategoryToKey(category);
                if (key) {
                    settings[key] = hud.getFontOverrideName(category);
                }
            }
        }

        // Capture per-HUD drop-shadow override (only if set)
        if (hud.hasDropShadowOverride()) {
            settings["dropShadow"] = hud.getDropShadowOverrideValue() ? "1" : "0";
        }
    }

    // Helper to write base HUD properties to file
    inline void writeBaseHudSettings(std::ostream& file, const SettingsManager::HudSettings& settings) {
        using namespace Keys::Base;
        static const std::array<const char*, 8> baseKeys = {
            VISIBLE, SHOW_TITLE, SHOW_BG_TEXTURE, TEXTURE_VARIANT,
            BG_OPACITY, SCALE, OFFSET_X, OFFSET_Y
        };
        for (const auto& key : baseKeys) {
            auto it = settings.find(key);
            if (it != settings.end()) {
                file << key << "=" << it->second << "\n";
            }
        }
    }

    // Helper to check if a key is a base HUD property
    inline bool isBaseKey(const std::string& key) {
        using namespace Keys::Base;
        return key == VISIBLE || key == SHOW_TITLE || key == SHOW_BG_TEXTURE ||
               key == TEXTURE_VARIANT || key == BG_OPACITY || key == SCALE ||
               key == OFFSET_X || key == OFFSET_Y;
    }

    // Helper to get IniOnly setting description by HUD name and key
    // Returns nullptr if not an IniOnly setting (no description available)
    inline const char* getIniOnlyDescription(const std::string& hudName, const std::string& key) {
        using namespace IniOnly;

        if (hudName == "SpeedoWidget") {
            if (key == Speedo::NEEDLE_COLOR.key) return Speedo::NEEDLE_COLOR.description;
            if (key == Speedo::SHOW_ODOMETER.key) return Speedo::SHOW_ODOMETER.description;
            if (key == Speedo::SHOW_TRIPMETER.key) return Speedo::SHOW_TRIPMETER.description;
        } else if (hudName == "TachoWidget") {
            if (key == Tacho::NEEDLE_COLOR.key) return Tacho::NEEDLE_COLOR.description;
        } else if (hudName == "SpeedWidget") {
            if (key == Speed::ROW_UNITS.key) return Speed::ROW_UNITS.description;
        } else if (hudName == "GearWidget") {
            if (key == Gear::SHOW_SHIFT_COLOR.key) return Gear::SHOW_SHIFT_COLOR.description;
            if (key == Gear::SHOW_LIMITER_CIRCLE.key) return Gear::SHOW_LIMITER_CIRCLE.description;
        } else if (hudName == "ClockWidget") {
            if (key == Clock::SHOW_UTC.key) return Clock::SHOW_UTC.description;
            if (key == Clock::UTC_ON_TOP.key) return Clock::UTC_ON_TOP.description;
        } else if (hudName == "RumbleHud") {
            if (key == Rumble::SHOW_MAX_MARKERS.key) return Rumble::SHOW_MAX_MARKERS.description;
            if (key == Rumble::MAX_MARKER_LINGER_FRAMES.key) return Rumble::MAX_MARKER_LINGER_FRAMES.description;
        } else if (hudName == "LeanWidget") {
            if (key == Lean::ARC_FILL_COLOR.key) return Lean::ARC_FILL_COLOR.description;
            if (key == Lean::ROW_ARC.key) return Lean::ROW_ARC.description;
            if (key == Lean::ROW_LEAN_VALUE.key) return Lean::ROW_LEAN_VALUE.description;
            if (key == Lean::ROW_STEER_BAR.key) return Lean::ROW_STEER_BAR.description;
            if (key == Lean::ROW_STEER_VALUE.key) return Lean::ROW_STEER_VALUE.description;
            if (key == Lean::SHOW_MAX_MARKERS.key) return Lean::SHOW_MAX_MARKERS.description;
            if (key == Lean::MAX_MARKER_LINGER_FRAMES.key) return Lean::MAX_MARKER_LINGER_FRAMES.description;
        } else if (hudName == "GForceWidget") {
            if (key == GForce::MAX_SCALE.key) return GForce::MAX_SCALE.description;
            if (key == GForce::SHOW_MAX_TEXT.key) return GForce::SHOW_MAX_TEXT.description;
            if (key == GForce::SHOW_MAX_MARKER.key) return GForce::SHOW_MAX_MARKER.description;
            if (key == GForce::MAX_MARKER_LINGER_FRAMES.key) return GForce::MAX_MARKER_LINGER_FRAMES.description;
        } else if (hudName == "CompassWidget") {
            if (key == Compass::STYLE.key) return Compass::STYLE.description;
        } else if (hudName == "BarsWidget") {
            if (key == Bars::COL_THROTTLE.key) return Bars::COL_THROTTLE.description;
            if (key == Bars::COL_BRAKE.key) return Bars::COL_BRAKE.description;
            if (key == Bars::COL_CLUTCH.key) return Bars::COL_CLUTCH.description;
            if (key == Bars::COL_RPM.key) return Bars::COL_RPM.description;
            if (key == Bars::COL_SUSPENSION.key) return Bars::COL_SUSPENSION.description;
            if (key == Bars::COL_FUEL.key) return Bars::COL_FUEL.description;
            if (key == Bars::COL_ENGINE_TEMP.key) return Bars::COL_ENGINE_TEMP.description;
            if (key == Bars::COL_WATER_TEMP.key) return Bars::COL_WATER_TEMP.description;
            if (key == Bars::SHOW_LABELS.key) return Bars::SHOW_LABELS.description;
            if (key == Bars::SHOW_MAX_MARKERS.key) return Bars::SHOW_MAX_MARKERS.description;
            if (key == Bars::MAX_MARKER_LINGER_FRAMES.key) return Bars::MAX_MARKER_LINGER_FRAMES.description;
        }
#if GAME_HAS_TYRE_TEMP
        else if (hudName == "TyreTempWidget") {
            if (key == TyreTemp::COLD_THRESHOLD.key) return TyreTemp::COLD_THRESHOLD.description;
            if (key == TyreTemp::HOT_THRESHOLD.key) return TyreTemp::HOT_THRESHOLD.description;
            if (key == TyreTemp::ROW_BARS.key) return TyreTemp::ROW_BARS.description;
            if (key == TyreTemp::ROW_VALUES.key) return TyreTemp::ROW_VALUES.description;
            if (key == TyreTemp::SHOW_LABELS.key) return TyreTemp::SHOW_LABELS.description;
        }
#endif
#if GAME_HAS_ECU
        else if (hudName == "EcuWidget") {
            if (key == Ecu::ROW_MAP.key) return Ecu::ROW_MAP.description;
            if (key == Ecu::ROW_TC.key) return Ecu::ROW_TC.description;
            if (key == Ecu::ROW_EB.key) return Ecu::ROW_EB.description;
            if (key == Ecu::ROW_AW.key) return Ecu::ROW_AW.description;
            if (key == Ecu::SHOW_LABELS.key) return Ecu::SHOW_LABELS.description;
        }
#endif
        else if (hudName == "FuelWidget") {
            if (key == Fuel::ROW_FUEL.key) return Fuel::ROW_FUEL.description;
            if (key == Fuel::ROW_USED.key) return Fuel::ROW_USED.description;
            if (key == Fuel::ROW_AVG.key) return Fuel::ROW_AVG.description;
            if (key == Fuel::ROW_EST.key) return Fuel::ROW_EST.description;
        } else if (hudName == "NoticesHud") {
            if (key == Notices::WRONG_WAY.key) return Notices::WRONG_WAY.description;
            if (key == Notices::BLUE_FLAG.key) return Notices::BLUE_FLAG.description;
            if (key == Notices::LAST_LAP.key) return Notices::LAST_LAP.description;
            if (key == Notices::FINISHED.key) return Notices::FINISHED.description;
            if (key == Notices::ALLTIME_PB.key) return Notices::ALLTIME_PB.description;
            if (key == Notices::FASTEST_LAP.key) return Notices::FASTEST_LAP.description;
            if (key == Notices::SESSION_PB.key) return Notices::SESSION_PB.description;
            if (key == Notices::DEFAULT_SETUP.key) return Notices::DEFAULT_SETUP.description;
            if (key == Notices::PB_DURATION.key) return Notices::PB_DURATION.description;
        } else if (hudName == "StandingsHud") {
            if (key == Standings::TOP_POSITIONS.key) return Standings::TOP_POSITIONS.description;
            if (key == Standings::PLAYER_ROW_HIGHLIGHT.key) return Standings::PLAYER_ROW_HIGHLIGHT.description;
            if (key == Standings::PLAYER_ROW_HIGHLIGHT_BRAND.key) return Standings::PLAYER_ROW_HIGHLIGHT_BRAND.description;
            if (key == Standings::ANIMATION_DURATION_MS.key) return Standings::ANIMATION_DURATION_MS.description;
            if (key == Standings::CLASSIC_LAYOUT.key) return Standings::CLASSIC_LAYOUT.description;
            if (key == Standings::NAME_MODE.key) return Standings::NAME_MODE.description;
            if (key == Standings::SHORT_NAME_CHARS.key) return Standings::SHORT_NAME_CHARS.description;
        }
#if GAME_HAS_RECORDS_PROVIDER
        else if (hudName == "RecordsHud") {
            if (key == Records::SHOW_FOOTER.key) return Records::SHOW_FOOTER.description;
        }
#endif
        else if (hudName == "GamepadWidget") {
            if (key == Gamepad::TRIGGER_FILL_MODE.key) return Gamepad::TRIGGER_FILL_MODE.description;
        }
        else if (hudName == "MapHud") {
            if (key == Map::LABEL_ANCHOR.key) return Map::LABEL_ANCHOR.description;
        }
#if GAME_HAS_FMX
        else if (hudName == "FmxHud") {
            // Per-trick disable flags share a common prefix; one description covers them all.
            if (key.rfind(Keys::Fmx::TRICK_ENABLED_PREFIX, 0) == 0) {
                return "Track this trick (1=enabled, 0=ignore)";
            }
        }
#endif
        else if (hudName == "SessionChartsHud") {
            if (key == SessionCharts::OUTLIER_FACTOR.key) return SessionCharts::OUTLIER_FACTOR.description;
        }

        // Per-HUD color/font overrides apply to all HUDs (checked last so HUD-specific
        // keys starting with "color_" or "font_" can be matched first if ever added)
        if (key.length() > 6 && key.substr(0, 6) == "color_") return "Per-HUD color override (hex ABGR, e.g. 0xff00ff00)";
        if (key.length() > 5 && key.substr(0, 5) == "font_") return "Per-HUD font override (font filename without extension)";
        if (key == "dropShadow") return "Per-HUD drop shadow override (0=off, 1=on; absent=inherit global)";

        return nullptr;
    }

    // Helper to write a setting with optional inline comment for IniOnly settings
    inline void writeSettingWithComment(std::ostream& file, const std::string& hudName,
                                 const std::string& key, const std::string& value) {
        const char* description = getIniOnlyDescription(hudName, key);
        if (description) {
            file << key << "=" << value << " ; " << description << "\n";
        } else {
            file << key << "=" << value << "\n";
        }
    }

    // ========================================================================
    // Named key helpers for bitmask fields
    // ========================================================================

    // Helper to save a single bit as a named key
    inline void saveBitAsKey(SettingsManager::HudSettings& settings, const char* key, uint32_t bitmask, uint32_t bit) {
        settings[key] = (bitmask & bit) ? "1" : "0";
    }

    // Helper to load a single bit from a named key
    inline void loadBitFromKey(const SettingsManager::HudSettings& settings, const char* key, uint32_t& bitmask, uint32_t bit) {
        auto it = settings.find(key);
        if (it != settings.end()) {
            if (it->second == "1") {
                bitmask |= bit;
            } else {
                bitmask &= ~bit;
            }
        }
        // If key is missing, leave bitmask unchanged (uses default)
    }

    // StandingsHud: save columns as named keys
    inline void saveStandingsColumns(SettingsManager::HudSettings& settings, uint32_t cols) {
        using namespace Keys::StandingsCols;
        saveBitAsKey(settings, TRACKED, cols, StandingsHud::COL_TRACKED);
        saveBitAsKey(settings, POS, cols, StandingsHud::COL_POS);
        // COL_POSGAIN visibility is driven entirely by posGainMode now; the bit is never
        // user-toggled, so we don't write col_posgain (avoids an inconsistent INI). It's
        // still read in loadStandingsColumns purely to migrate pre-mode configs.
        saveBitAsKey(settings, RACENUM, cols, StandingsHud::COL_RACENUM);
        saveBitAsKey(settings, NAME, cols, StandingsHud::COL_NAME);
        saveBitAsKey(settings, BIKE, cols, StandingsHud::COL_BIKE);
        saveBitAsKey(settings, PENALTY, cols, StandingsHud::COL_PENALTY);
        saveBitAsKey(settings, BEST_LAP, cols, StandingsHud::COL_BEST_LAP);
        saveBitAsKey(settings, LAST_LAP, cols, StandingsHud::COL_LAST_LAP);
        saveBitAsKey(settings, GAP, cols, StandingsHud::COL_GAP);
    }

    // StandingsHud: load columns from named keys
    inline void loadStandingsColumns(const SettingsManager::HudSettings& settings, uint32_t& cols) {
        using namespace Keys::StandingsCols;
        loadBitFromKey(settings, TRACKED, cols, StandingsHud::COL_TRACKED);
        loadBitFromKey(settings, POS, cols, StandingsHud::COL_POS);
        loadBitFromKey(settings, POSGAIN, cols, StandingsHud::COL_POSGAIN);  // migration-only (see saveStandingsColumns); posGainMode is the source of truth
        loadBitFromKey(settings, RACENUM, cols, StandingsHud::COL_RACENUM);
        loadBitFromKey(settings, NAME, cols, StandingsHud::COL_NAME);
        loadBitFromKey(settings, BIKE, cols, StandingsHud::COL_BIKE);
        loadBitFromKey(settings, PENALTY, cols, StandingsHud::COL_PENALTY);
        loadBitFromKey(settings, BEST_LAP, cols, StandingsHud::COL_BEST_LAP);
        loadBitFromKey(settings, LAST_LAP, cols, StandingsHud::COL_LAST_LAP);
        loadBitFromKey(settings, GAP, cols, StandingsHud::COL_GAP);
    }

#if GAME_HAS_RECORDS_PROVIDER
    // RecordsHud: save columns as named keys (only optional columns)
    inline void saveRecordsColumns(SettingsManager::HudSettings& settings, uint32_t cols) {
        using namespace Keys::RecordsCols;
        // Core columns (POS, RIDER, BIKE, LAPTIME) are always on - don't save
        // Only save optional columns
        saveBitAsKey(settings, SECTORS, cols, RecordsHud::COL_SECTORS);
        saveBitAsKey(settings, DATE, cols, RecordsHud::COL_DATE);
    }

    // RecordsHud: load columns from named keys (only optional columns)
    inline void loadRecordsColumns(const SettingsManager::HudSettings& settings, uint32_t& cols) {
        using namespace Keys::RecordsCols;
        // Core columns are always on - ensure they're set
        cols |= RecordsHud::COL_CORE;
        // Load optional columns
        loadBitFromKey(settings, SECTORS, cols, RecordsHud::COL_SECTORS);
        loadBitFromKey(settings, DATE, cols, RecordsHud::COL_DATE);
    }
#endif

    // LapLogHud: save columns (only sectors are configurable)
    inline void saveLapLogColumns(SettingsManager::HudSettings& settings, uint32_t cols) {
        using namespace Keys::LapLogCols;
        saveBitAsKey(settings, SECTORS, cols, LapLogHud::COL_SECTORS);
    }

    // LapLogHud: load columns (only sectors are configurable)
    inline void loadLapLogColumns(const SettingsManager::HudSettings& settings, uint32_t& cols) {
        using namespace Keys::LapLogCols;
        loadBitFromKey(settings, SECTORS, cols, LapLogHud::COL_SECTORS);
    }

    // IdealLapHud: save rows (sectors and laps toggled as groups)
    inline void saveIdealLapRows(SettingsManager::HudSettings& settings, uint32_t rows) {
        using namespace Keys::IdealLapRows;
        saveBitAsKey(settings, SECTORS, rows, IdealLapHud::ROW_SECTORS);
        saveBitAsKey(settings, LAPS, rows, IdealLapHud::ROW_LAPS);
    }

    // IdealLapHud: load rows (sectors and laps toggled as groups)
    inline void loadIdealLapRows(const SettingsManager::HudSettings& settings, uint32_t& rows) {
        using namespace Keys::IdealLapRows;
        loadBitFromKey(settings, SECTORS, rows, IdealLapHud::ROW_SECTORS);
        loadBitFromKey(settings, LAPS, rows, IdealLapHud::ROW_LAPS);
    }

    // PitboardHud: save rows as named keys
    inline void savePitboardRows(SettingsManager::HudSettings& settings, uint32_t rows) {
        using namespace Keys::PitboardRows;
        saveBitAsKey(settings, RIDER_ID, rows, PitboardHud::ROW_RIDER_ID);
        saveBitAsKey(settings, SESSION, rows, PitboardHud::ROW_SESSION);
        saveBitAsKey(settings, POSITION, rows, PitboardHud::ROW_POSITION);
        saveBitAsKey(settings, TIME, rows, PitboardHud::ROW_TIME);
        saveBitAsKey(settings, LAP, rows, PitboardHud::ROW_LAP);
        saveBitAsKey(settings, LAST_LAP, rows, PitboardHud::ROW_LAST_LAP);
        saveBitAsKey(settings, GAP, rows, PitboardHud::ROW_GAP);
    }

    // PitboardHud: load rows from named keys
    inline void loadPitboardRows(const SettingsManager::HudSettings& settings, uint32_t& rows) {
        using namespace Keys::PitboardRows;
        loadBitFromKey(settings, RIDER_ID, rows, PitboardHud::ROW_RIDER_ID);
        loadBitFromKey(settings, SESSION, rows, PitboardHud::ROW_SESSION);
        loadBitFromKey(settings, POSITION, rows, PitboardHud::ROW_POSITION);
        loadBitFromKey(settings, TIME, rows, PitboardHud::ROW_TIME);
        loadBitFromKey(settings, LAP, rows, PitboardHud::ROW_LAP);
        loadBitFromKey(settings, LAST_LAP, rows, PitboardHud::ROW_LAST_LAP);
        loadBitFromKey(settings, GAP, rows, PitboardHud::ROW_GAP);
    }

    // SpeedWidget: save rows as named keys
    inline void saveSpeedRows(SettingsManager::HudSettings& settings, uint32_t rows) {
        using namespace Keys::SpeedRows;
        saveBitAsKey(settings, UNITS, rows, SpeedWidget::ROW_UNITS);
    }

    // SpeedWidget: load rows from named keys
    inline void loadSpeedRows(const SettingsManager::HudSettings& settings, uint32_t& rows) {
        using namespace Keys::SpeedRows;
        loadBitFromKey(settings, UNITS, rows, SpeedWidget::ROW_UNITS);
    }

    // FuelWidget: save rows as named keys
    inline void saveFuelRows(SettingsManager::HudSettings& settings, uint32_t rows) {
        using namespace Keys::FuelRows;
        saveBitAsKey(settings, FUEL, rows, FuelWidget::ROW_FUEL);
        saveBitAsKey(settings, USED, rows, FuelWidget::ROW_USED);
        saveBitAsKey(settings, AVG, rows, FuelWidget::ROW_AVG);
        saveBitAsKey(settings, EST, rows, FuelWidget::ROW_EST);
    }

    // FuelWidget: load rows from named keys
    inline void loadFuelRows(const SettingsManager::HudSettings& settings, uint32_t& rows) {
        using namespace Keys::FuelRows;
        loadBitFromKey(settings, FUEL, rows, FuelWidget::ROW_FUEL);
        loadBitFromKey(settings, USED, rows, FuelWidget::ROW_USED);
        loadBitFromKey(settings, AVG, rows, FuelWidget::ROW_AVG);
        loadBitFromKey(settings, EST, rows, FuelWidget::ROW_EST);
    }

    // SessionHud: save rows as named keys
    inline void saveSessionRows(SettingsManager::HudSettings& settings, uint32_t rows) {
        using namespace Keys::SessionRows;
        saveBitAsKey(settings, TRACK, rows, SessionHud::ROW_TRACK);
        saveBitAsKey(settings, FORMAT, rows, SessionHud::ROW_FORMAT);
        saveBitAsKey(settings, SERVER, rows, SessionHud::ROW_SERVER);
        saveBitAsKey(settings, WEATHER, rows, SessionHud::ROW_WEATHER);
    }

    // SessionHud: load rows from named keys
    // Note: an older "row_players" key (player count row) was dropped in v1.23
    // along with the memory-reading subsystem. Old INIs containing it are
    // silently ignored - we never look it up. ROW_WEATHER's bit shifted from
    // 1<<5 to 1<<4 when ROW_PLAYERS was removed, but persistence is by name
    // (row_weather) not by mask, so existing profiles are unaffected.
    inline void loadSessionRows(const SettingsManager::HudSettings& settings, uint32_t& rows) {
        using namespace Keys::SessionRows;
        loadBitFromKey(settings, TRACK, rows, SessionHud::ROW_TRACK);
        loadBitFromKey(settings, FORMAT, rows, SessionHud::ROW_FORMAT);
        loadBitFromKey(settings, SERVER, rows, SessionHud::ROW_SERVER);
        loadBitFromKey(settings, WEATHER, rows, SessionHud::ROW_WEATHER);
    }

    // LeanWidget: save rows as named keys
    inline void saveLeanRows(SettingsManager::HudSettings& settings, uint32_t rows) {
        using namespace Keys::LeanRows;
        saveBitAsKey(settings, ARC, rows, LeanWidget::ROW_ARC);
        saveBitAsKey(settings, LEAN_VALUE, rows, LeanWidget::ROW_LEAN_VALUE);
        saveBitAsKey(settings, STEER_BAR, rows, LeanWidget::ROW_STEER_BAR);
        saveBitAsKey(settings, STEER_VALUE, rows, LeanWidget::ROW_STEER_VALUE);
    }

    // LeanWidget: load rows from named keys
    inline void loadLeanRows(const SettingsManager::HudSettings& settings, uint32_t& rows) {
        using namespace Keys::LeanRows;
        loadBitFromKey(settings, ARC, rows, LeanWidget::ROW_ARC);
        loadBitFromKey(settings, LEAN_VALUE, rows, LeanWidget::ROW_LEAN_VALUE);
        loadBitFromKey(settings, STEER_BAR, rows, LeanWidget::ROW_STEER_BAR);
        loadBitFromKey(settings, STEER_VALUE, rows, LeanWidget::ROW_STEER_VALUE);
    }

#if GAME_HAS_TYRE_TEMP
    // TyreTempWidget: save rows as named keys
    inline void saveTyreTempRows(SettingsManager::HudSettings& settings, uint32_t rows) {
        using namespace Keys::TyreTempRows;
        saveBitAsKey(settings, BARS, rows, TyreTempWidget::ROW_BARS);
        saveBitAsKey(settings, VALUES, rows, TyreTempWidget::ROW_VALUES);
    }

    // TyreTempWidget: load rows from named keys
    inline void loadTyreTempRows(const SettingsManager::HudSettings& settings, uint32_t& rows) {
        using namespace Keys::TyreTempRows;
        loadBitFromKey(settings, BARS, rows, TyreTempWidget::ROW_BARS);
        loadBitFromKey(settings, VALUES, rows, TyreTempWidget::ROW_VALUES);
    }
#endif

#if GAME_HAS_ECU
    // EcuWidget: save chips as named keys
    inline void saveEcuRows(SettingsManager::HudSettings& settings, uint32_t rows) {
        using namespace Keys::EcuRows;
        saveBitAsKey(settings, MAP, rows, EcuWidget::ROW_MAP);
        saveBitAsKey(settings, TC, rows, EcuWidget::ROW_TC);
        saveBitAsKey(settings, EB, rows, EcuWidget::ROW_EB);
        saveBitAsKey(settings, AW, rows, EcuWidget::ROW_AW);
    }

    // EcuWidget: load chips from named keys
    inline void loadEcuRows(const SettingsManager::HudSettings& settings, uint32_t& rows) {
        using namespace Keys::EcuRows;
        loadBitFromKey(settings, MAP, rows, EcuWidget::ROW_MAP);
        loadBitFromKey(settings, TC, rows, EcuWidget::ROW_TC);
        loadBitFromKey(settings, EB, rows, EcuWidget::ROW_EB);
        loadBitFromKey(settings, AW, rows, EcuWidget::ROW_AW);
    }
#endif

    // BarsWidget: save columns as named keys
    inline void saveBarsColumns(SettingsManager::HudSettings& settings, uint32_t cols) {
        using namespace Keys::BarsCols;
        saveBitAsKey(settings, THROTTLE, cols, BarsWidget::COL_THROTTLE);
        saveBitAsKey(settings, BRAKE, cols, BarsWidget::COL_BRAKE);
        saveBitAsKey(settings, CLUTCH, cols, BarsWidget::COL_CLUTCH);
        saveBitAsKey(settings, RPM, cols, BarsWidget::COL_RPM);
        saveBitAsKey(settings, SUSPENSION, cols, BarsWidget::COL_SUSPENSION);
        saveBitAsKey(settings, FUEL, cols, BarsWidget::COL_FUEL);
        saveBitAsKey(settings, ENGINE_TEMP, cols, BarsWidget::COL_ENGINE_TEMP);
        saveBitAsKey(settings, WATER_TEMP, cols, BarsWidget::COL_WATER_TEMP);
    }

    // BarsWidget: load columns from named keys
    inline void loadBarsColumns(const SettingsManager::HudSettings& settings, uint32_t& cols) {
        using namespace Keys::BarsCols;
        loadBitFromKey(settings, THROTTLE, cols, BarsWidget::COL_THROTTLE);
        loadBitFromKey(settings, BRAKE, cols, BarsWidget::COL_BRAKE);
        loadBitFromKey(settings, CLUTCH, cols, BarsWidget::COL_CLUTCH);
        loadBitFromKey(settings, RPM, cols, BarsWidget::COL_RPM);
        loadBitFromKey(settings, SUSPENSION, cols, BarsWidget::COL_SUSPENSION);
        loadBitFromKey(settings, FUEL, cols, BarsWidget::COL_FUEL);
        loadBitFromKey(settings, ENGINE_TEMP, cols, BarsWidget::COL_ENGINE_TEMP);
        loadBitFromKey(settings, WATER_TEMP, cols, BarsWidget::COL_WATER_TEMP);
    }

    // EventLogHud: save events as named keys
    inline void saveEventLogEvents(SettingsManager::HudSettings& settings, uint32_t events) {
        using namespace Keys::EventLog;
        saveBitAsKey(settings, SESSION_STARTED, events, EVENT_SESSION_STARTED);
        saveBitAsKey(settings, SESSION_STATE, events, EVENT_SESSION_STATE);
        saveBitAsKey(settings, FASTEST_LAP, events, EVENT_FASTEST_LAP);
        saveBitAsKey(settings, PENALTY, events, EVENT_PENALTY);
        saveBitAsKey(settings, PENALTY_CLEAR, events, EVENT_PENALTY_CLEAR);
        saveBitAsKey(settings, RIDER_RETIRED, events, EVENT_RIDER_RETIRED);
        saveBitAsKey(settings, RIDER_DSQ, events, EVENT_RIDER_DSQ);
        saveBitAsKey(settings, RIDER_DNS, events, EVENT_RIDER_DNS);
        saveBitAsKey(settings, OVERTIME, events, EVENT_OVERTIME);
        saveBitAsKey(settings, FINAL_LAP, events, EVENT_FINAL_LAP);
        saveBitAsKey(settings, RIDER_FINISHED, events, EVENT_RIDER_FINISHED);
        saveBitAsKey(settings, LEADER_CHANGE, events, EVENT_LEADER_CHANGE);
        saveBitAsKey(settings, PIT_ENTRY, events, EVENT_PIT_ENTRY);
        saveBitAsKey(settings, PIT_EXIT, events, EVENT_PIT_EXIT);
        saveBitAsKey(settings, DIRECTOR, events, EVENT_DIRECTOR);
    }

    // EventLogHud: load events from named keys
    inline void loadEventLogEvents(const SettingsManager::HudSettings& settings, uint32_t& events) {
        using namespace Keys::EventLog;
        loadBitFromKey(settings, SESSION_STARTED, events, EVENT_SESSION_STARTED);
        loadBitFromKey(settings, SESSION_STATE, events, EVENT_SESSION_STATE);
        loadBitFromKey(settings, FASTEST_LAP, events, EVENT_FASTEST_LAP);
        loadBitFromKey(settings, PENALTY, events, EVENT_PENALTY);
        loadBitFromKey(settings, PENALTY_CLEAR, events, EVENT_PENALTY_CLEAR);
        loadBitFromKey(settings, RIDER_RETIRED, events, EVENT_RIDER_RETIRED);
        loadBitFromKey(settings, RIDER_DSQ, events, EVENT_RIDER_DSQ);
        loadBitFromKey(settings, RIDER_DNS, events, EVENT_RIDER_DNS);
        loadBitFromKey(settings, OVERTIME, events, EVENT_OVERTIME);
        loadBitFromKey(settings, FINAL_LAP, events, EVENT_FINAL_LAP);
        loadBitFromKey(settings, RIDER_FINISHED, events, EVENT_RIDER_FINISHED);
        loadBitFromKey(settings, LEADER_CHANGE, events, EVENT_LEADER_CHANGE);
        loadBitFromKey(settings, PIT_ENTRY, events, EVENT_PIT_ENTRY);
        loadBitFromKey(settings, PIT_EXIT, events, EVENT_PIT_EXIT);
        loadBitFromKey(settings, DIRECTOR, events, EVENT_DIRECTOR);
    }

    // NoticesHud: save notices as named keys
    inline void saveNotices(SettingsManager::HudSettings& settings, uint32_t notices) {
        using namespace Keys::Notices;
        saveBitAsKey(settings, WRONG_WAY, notices, NoticesHud::NOTICE_WRONG_WAY);
        saveBitAsKey(settings, BLUE_FLAG, notices, NoticesHud::NOTICE_BLUE_FLAG);
        saveBitAsKey(settings, LAST_LAP, notices, NoticesHud::NOTICE_LAST_LAP);
        saveBitAsKey(settings, FINISHED, notices, NoticesHud::NOTICE_FINISHED);
        saveBitAsKey(settings, ALLTIME_PB, notices, NoticesHud::NOTICE_ALLTIME_PB);
        saveBitAsKey(settings, FASTEST_LAP, notices, NoticesHud::NOTICE_FASTEST_LAP);
        saveBitAsKey(settings, SESSION_PB, notices, NoticesHud::NOTICE_SESSION_PB);
        saveBitAsKey(settings, DEFAULT_SETUP, notices, NoticesHud::NOTICE_DEFAULT_SETUP);
        saveBitAsKey(settings, OVERTIME, notices, NoticesHud::NOTICE_OVERTIME);
        saveBitAsKey(settings, HAZARD_STATIONARY, notices, NoticesHud::NOTICE_HAZARD_STATIONARY);
        saveBitAsKey(settings, HAZARD_WRONG_WAY, notices, NoticesHud::NOTICE_HAZARD_WRONG_WAY);
    }

    // NoticesHud: load notices from named keys
    inline void loadNotices(const SettingsManager::HudSettings& settings, uint32_t& notices) {
        using namespace Keys::Notices;
        loadBitFromKey(settings, WRONG_WAY, notices, NoticesHud::NOTICE_WRONG_WAY);
        loadBitFromKey(settings, BLUE_FLAG, notices, NoticesHud::NOTICE_BLUE_FLAG);
        loadBitFromKey(settings, LAST_LAP, notices, NoticesHud::NOTICE_LAST_LAP);
        loadBitFromKey(settings, FINISHED, notices, NoticesHud::NOTICE_FINISHED);
        loadBitFromKey(settings, ALLTIME_PB, notices, NoticesHud::NOTICE_ALLTIME_PB);
        loadBitFromKey(settings, FASTEST_LAP, notices, NoticesHud::NOTICE_FASTEST_LAP);
        loadBitFromKey(settings, SESSION_PB, notices, NoticesHud::NOTICE_SESSION_PB);
        loadBitFromKey(settings, DEFAULT_SETUP, notices, NoticesHud::NOTICE_DEFAULT_SETUP);
        loadBitFromKey(settings, OVERTIME, notices, NoticesHud::NOTICE_OVERTIME);
        loadBitFromKey(settings, HAZARD_STATIONARY, notices, NoticesHud::NOTICE_HAZARD_STATIONARY);
        loadBitFromKey(settings, HAZARD_WRONG_WAY, notices, NoticesHud::NOTICE_HAZARD_WRONG_WAY);
    }

    // TelemetryHud: save elements as named keys
    inline void saveTelemetryElements(SettingsManager::HudSettings& settings, uint32_t elems) {
        using namespace Keys::TelemetryElems;
        saveBitAsKey(settings, THROTTLE, elems, TelemetryHud::ELEM_THROTTLE);
        saveBitAsKey(settings, FRONT_BRAKE, elems, TelemetryHud::ELEM_FRONT_BRAKE);
        saveBitAsKey(settings, REAR_BRAKE, elems, TelemetryHud::ELEM_REAR_BRAKE);
        saveBitAsKey(settings, CLUTCH, elems, TelemetryHud::ELEM_CLUTCH);
        saveBitAsKey(settings, RPM, elems, TelemetryHud::ELEM_RPM);
        saveBitAsKey(settings, FRONT_SUSP, elems, TelemetryHud::ELEM_FRONT_SUSP);
        saveBitAsKey(settings, REAR_SUSP, elems, TelemetryHud::ELEM_REAR_SUSP);
        saveBitAsKey(settings, GEAR, elems, TelemetryHud::ELEM_GEAR);
    }

    // TelemetryHud: load elements from named keys
    inline void loadTelemetryElements(const SettingsManager::HudSettings& settings, uint32_t& elems) {
        using namespace Keys::TelemetryElems;
        loadBitFromKey(settings, THROTTLE, elems, TelemetryHud::ELEM_THROTTLE);
        loadBitFromKey(settings, FRONT_BRAKE, elems, TelemetryHud::ELEM_FRONT_BRAKE);
        loadBitFromKey(settings, REAR_BRAKE, elems, TelemetryHud::ELEM_REAR_BRAKE);
        loadBitFromKey(settings, CLUTCH, elems, TelemetryHud::ELEM_CLUTCH);
        loadBitFromKey(settings, RPM, elems, TelemetryHud::ELEM_RPM);
        loadBitFromKey(settings, FRONT_SUSP, elems, TelemetryHud::ELEM_FRONT_SUSP);
        loadBitFromKey(settings, REAR_SUSP, elems, TelemetryHud::ELEM_REAR_SUSP);
        loadBitFromKey(settings, GEAR, elems, TelemetryHud::ELEM_GEAR);
    }

    // PerformanceHud: save elements as named keys
    inline void savePerformanceElements(SettingsManager::HudSettings& settings, uint32_t elems) {
        using namespace Keys::PerformanceElems;
        saveBitAsKey(settings, FPS, elems, PerformanceHud::ELEM_FPS);
        saveBitAsKey(settings, CPU, elems, PerformanceHud::ELEM_CPU);
    }

    // PerformanceHud: load elements from named keys
    inline void loadPerformanceElements(const SettingsManager::HudSettings& settings, uint32_t& elems) {
        using namespace Keys::PerformanceElems;
        loadBitFromKey(settings, FPS, elems, PerformanceHud::ELEM_FPS);
        loadBitFromKey(settings, CPU, elems, PerformanceHud::ELEM_CPU);
    }

    // TimingHud: save secondary gap types as named keys
    inline void saveTimingSecondaryGaps(SettingsManager::HudSettings& settings, uint8_t gaps) {
        using namespace Keys::TimingGaps;
        saveBitAsKey(settings, TO_PB, gaps, GAP_TO_PB);
        saveBitAsKey(settings, TO_IDEAL, gaps, GAP_TO_IDEAL);
        saveBitAsKey(settings, TO_OVERALL, gaps, GAP_TO_OVERALL);
        saveBitAsKey(settings, TO_ALLTIME, gaps, GAP_TO_ALLTIME);
        saveBitAsKey(settings, TO_RECORD, gaps, GAP_TO_RECORD);
        saveBitAsKey(settings, TO_LASTLAP, gaps, GAP_TO_LASTLAP);
    }

    // TimingHud: load secondary gap types from named keys
    inline void loadTimingSecondaryGaps(const SettingsManager::HudSettings& settings, uint8_t& gaps) {
        using namespace Keys::TimingGaps;
        uint32_t gaps32 = gaps;
        loadBitFromKey(settings, TO_PB, gaps32, GAP_TO_PB);
        loadBitFromKey(settings, TO_IDEAL, gaps32, GAP_TO_IDEAL);
        loadBitFromKey(settings, TO_OVERALL, gaps32, GAP_TO_OVERALL);
        loadBitFromKey(settings, TO_ALLTIME, gaps32, GAP_TO_ALLTIME);
        loadBitFromKey(settings, TO_RECORD, gaps32, GAP_TO_RECORD);
        loadBitFromKey(settings, TO_LASTLAP, gaps32, GAP_TO_LASTLAP);
        gaps = static_cast<uint8_t>(gaps32);
    }

    // Helper to apply base HUD settings from a map
    // Buffers position to apply X/Y together atomically
    inline void applyBaseHudSettings(BaseHud& hud, const SettingsManager::HudSettings& settings) {
        using namespace Keys::Base;
        float pendingOffsetX = 0, pendingOffsetY = 0;
        bool hasOffsetX = false, hasOffsetY = false;
        // Companion instance: buffered so it applies as a unit after the loop. Absent
        // keys => the companion mirrors the game (authoritative apply, like colors).
        bool compConfigured = false, compVisible = true;
        float compX = 0, compY = 0;

        // Each profile's cache is the complete intended state for the HUD (base keys
        // are merged into every profile on load), so an absent color_/font_ key means
        // "no override". Track which ones we see and clear the rest below — without
        // this, a per-HUD override would leak across profile switches and survive a
        // reset, since these private BaseHud members aren't touched by resetToDefaults().
        std::array<bool, static_cast<size_t>(ColorSlot::COUNT)> colorSeen{};
        std::array<bool, static_cast<size_t>(FontCategory::COUNT)> fontSeen{};
        bool dropShadowSeen = false;

        for (const auto& [key, value] : settings) {
            try {
                if (key == VISIBLE) {
                    hud.setVisible(std::stoi(value) != 0);
                } else if (key == SHOW_TITLE) {
                    hud.setShowTitle(std::stoi(value) != 0);
                } else if (key == SHOW_BG_TEXTURE) {
                    hud.setShowBackgroundTexture(std::stoi(value) != 0);
                } else if (key == TEXTURE_VARIANT) {
                    hud.setTextureVariant(std::stoi(value));
                } else if (key == BG_OPACITY) {
                    hud.setBackgroundOpacity(validateOpacity(parseFiniteFloat(value)));
                } else if (key == SCALE) {
                    hud.setScale(validateScale(parseFiniteFloat(value)));
                } else if (key == OFFSET_X) {
                    pendingOffsetX = validateOffset(parseFiniteFloat(value));
                    hasOffsetX = true;
                } else if (key == OFFSET_Y) {
                    pendingOffsetY = validateOffset(parseFiniteFloat(value));
                    hasOffsetY = true;
                } else if (key == COMPANION_CONFIGURED) {
                    compConfigured = std::stoi(value) != 0;
                } else if (key == COMPANION_VISIBLE) {
                    compVisible = std::stoi(value) != 0;
                } else if (key == COMPANION_X) {
                    compX = validateOffset(parseFiniteFloat(value));
                } else if (key == COMPANION_Y) {
                    compY = validateOffset(parseFiniteFloat(value));
                } else {
                    // Per-HUD color/font overrides (power user INI feature)
                    ColorSlot colorSlot = parseColorKey(key);
                    if (colorSlot != ColorSlot::COUNT) {
                        hud.setColorOverride(colorSlot, PluginUtils::parseColorHex(value));
                        colorSeen[static_cast<size_t>(colorSlot)] = true;
                        continue;
                    }
                    FontCategory fontCategory = parseFontKey(key);
                    if (fontCategory != FontCategory::COUNT) {
                        hud.setFontOverride(fontCategory, value);
                        fontSeen[static_cast<size_t>(fontCategory)] = true;
                        continue;
                    }
                    if (key == "dropShadow") {
                        hud.setDropShadowOverride(std::stoi(value) != 0);
                        dropShadowSeen = true;
                        continue;
                    }
                }
            } catch (...) {
                DEBUG_WARN_F("Failed to parse base setting '%s=%s'", key.c_str(), value.c_str());
            }
        }

        // Clear any override not present in the applied settings (authoritative apply).
        for (size_t i = 0; i < colorSeen.size(); ++i) {
            if (!colorSeen[i]) hud.clearColorOverride(static_cast<ColorSlot>(i));
        }
        for (size_t i = 0; i < fontSeen.size(); ++i) {
            if (!fontSeen[i]) hud.clearFontOverride(static_cast<FontCategory>(i));
        }
        if (!dropShadowSeen) hud.clearDropShadowOverride();
        // Apply buffered position
        if (hasOffsetX || hasOffsetY) {
            float finalX = hasOffsetX ? pendingOffsetX : hud.getOffsetX();
            float finalY = hasOffsetY ? pendingOffsetY : hud.getOffsetY();
            hud.setPosition(finalX, finalY);
        }
        // Apply the companion instance authoritatively: configured => use saved
        // values; absent => mirror the game.
        if (compConfigured) hud.applyCompanionState(compVisible, compX, compY);
        else hud.clearCompanionState();
    }

} // namespace Settings
