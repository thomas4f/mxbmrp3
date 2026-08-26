// ============================================================================
// core/settings_serde_hud.h
// The HUD-typed half of the settings serde helpers: enum<->string converters
// whose types belong to concrete HUD/widget classes, the MapHud value
// validators, and the per-HUD column/row bitmask save/loads. Split from
// settings_serde.h so the HUD-free half stops rebuilding on every HUD header
// edit; see that file's header for the namespace constraint both halves share.
// Consumers that serialize concrete HUDs (the settings_hud_registry TUs,
// settings_manager_global.cpp) include THIS header; TUs that don't
// (settings_hud_profiles.cpp, settings_manager.cpp) include settings_serde.h.
// ============================================================================
#pragma once

#include "settings_serde.h"

#include "../hud/timing_hud.h"
#include "../hud/standings_hud.h"
#include "../hud/map_hud.h"
#include "../hud/radar_hud.h"
#include "../hud/gap_bar_hud.h"
#include "../hud/pitboard_hud.h"
#include "../hud/telemetry_hud.h"
#include "../hud/performance_hud.h"
#include "../hud/speed_widget.h"
#include "../hud/compass_widget.h"
#include "../hud/fuel_widget.h"
#include "../hud/session_hud.h"
#include "../hud/lean_widget.h"
#include "../hud/bars_widget.h"
#include "../hud/event_log_hud.h"
#include "../hud/notices_hud.h"
#include "../hud/ideal_lap_hud.h"
#include "../hud/lap_log_hud.h"
// settings_hud.h is core (every game has the settings menu), and it pulls
// records_hud.h itself; both .cpp files are compiled on every game, so neither
// include may be gated on GAME_HAS_RECORDS_PROVIDER — gating it broke the
// GPB/KRP builds (SettingsHud left incomplete -> C2027). The *provider* feature
// stays runtime/registration-gated; only these includes are always on.
#include "../hud/records_hud.h"
#include "../hud/settings_hud.h"
#if GAME_HAS_TYRE_TEMP
#include "../hud/tyre_temp_widget.h"
#endif
#if GAME_HAS_ECU
#include "../hud/ecu_widget.h"
#endif

namespace Settings {

    // ========================================================================
    // Enum string conversion helpers (HUD-typed)
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

    // MarkerLabel::Mode — MapHud/RadarHud/GapBarHud share this type (marker_label.h),
    // so one converter pair serves all three (Map/Radar save it as text; GapBar
    // predates that and stays int-serialized).
    inline const char* labelModeToString(MapHud::LabelMode mode) {
        switch (mode) {
            case MapHud::LabelMode::NONE: return "NONE";
            case MapHud::LabelMode::POSITION: return "POSITION";
            case MapHud::LabelMode::RACE_NUM: return "RACE_NUM";
            case MapHud::LabelMode::BOTH: return "BOTH";
            default: return "NONE";
        }
    }

    // ACCEPTS A BARE NUMBER TOO, and that is not a courtesy to hand-editors: the gap
    // bar wrote `labelMode=2` for its entire shipped life while Map and Radar -- the
    // same enum, the same key, the same setting -- wrote `labelMode=RACE_NUM`. Reading
    // only names would have degraded every existing gap-bar INI to the default on
    // upgrade, silently, since an unparseable value is indistinguishable from an absent
    // one at this layer. So the legacy spelling stays READABLE while nothing writes it
    // any more, and the next save rewrites it as a name.
    //
    // Kept in the shared converter rather than in the gap bar's apply, so all three read
    // both spellings and no future move between them can reintroduce the same break.
    inline MapHud::LabelMode stringToLabelMode(const std::string& str, MapHud::LabelMode defaultVal = MapHud::LabelMode::NONE) {
        if (str == "NONE") return MapHud::LabelMode::NONE;
        if (str == "POSITION") return MapHud::LabelMode::POSITION;
        if (str == "RACE_NUM") return MapHud::LabelMode::RACE_NUM;
        if (str == "BOTH") return MapHud::LabelMode::BOTH;
        if (str.size() == 1 && str[0] >= '0' && str[0] <= '3') {
            return static_cast<MapHud::LabelMode>(str[0] - '0');
        }
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

    // ========================================================================
    // MapHud value validators
    // ========================================================================

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

    // ========================================================================
    // Per-HUD column/row bitmask save/loads
    // ========================================================================

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

    // TimingHud: the readout rows, driven by READOUT_INFO so a new readout is one
    // row in that table and nothing here. Each carries its own INI key.
    inline void saveTimingReadouts(SettingsManager::HudSettings& settings, uint32_t readouts) {
        for (int i = 0; i < READOUT_COUNT; i++) {
            saveBitAsKey(settings, READOUT_INFO[i].key, readouts, READOUT_INFO[i].flag);
        }
    }

    inline void loadTimingReadouts(const SettingsManager::HudSettings& settings, uint32_t& readouts) {
        for (int i = 0; i < READOUT_COUNT; i++) {
            loadBitFromKey(settings, READOUT_INFO[i].key, readouts, READOUT_INFO[i].flag);
        }
    }

} // namespace Settings
