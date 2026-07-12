// ============================================================================
// core/settings_hud_registry.cpp
// The single per-HUD serializer registry: one ordered table of
// {section name, capture fn, apply fn}. captureToCache(), applyProfile(), and
// serializeSettings() all iterate THIS table, so a HUD is registered for capture,
// apply, and on-disk serialization in one place — collapsing what used to be three
// parallel hardcoded lists (the "third hardcoded list" / FriendsHud trap).
//
// Each cap_*/app_* body is the verbatim per-HUD block moved out of the old giant
// captureToCache/applyProfile; the only change is that the section-name literal
// became the `name` parameter (supplied by the table), so the name lives ONLY in
// the table. Generated once from the original functions; edit the functions here
// when a HUD's settings change, and add a table row (below) for a new HUD.
// ============================================================================
#include "settings_hud_registry.h"
// profile-level operations built on them (switch, copy-to-all, and the reset
// family). Split out of settings_manager.cpp, which keeps global-section
// serialization, the file serialize/build helpers, and save/load orchestration.
// ============================================================================
#include "settings_manager.h"
#include "settings_keys.h"
#include "settings_serde.h"
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

// Bring the centralized INI key names / serde helpers into scope.
using namespace Settings;

// Per-HUD capture/apply, defined as SettingsManager members (declared in
// settings_hud_registry_decls.inc) so they inherit SettingsManager's friendship
// with the HUD classes. Bodies are the verbatim per-HUD blocks moved out of the
// old captureToCache/applyProfile; the only change is the section-name literal
// became the `name` parameter (supplied by the registry table below).

void SettingsManager::cap_StandingsHud(const HudManager& hudManager, SettingsManager::ProfileCache& cache, const char* name) {
        using namespace Keys::Standings;
        HudSettings settings;
        const auto& hud = hudManager.getStandingsHud();
        captureBaseHudSettings(settings, hud);
        settings[DISPLAY_ROW_COUNT] = std::to_string(hud.m_displayRowCount);
        saveStandingsColumns(settings, hud.m_enabledColumns);  // Named keys instead of bitmask
        settings[GAP_MODE] = gapModeToString(hud.m_gapMode);
        settings[POSGAIN_MODE] = posGainModeToString(hud.m_posGainMode);
        settings[GAP_REFERENCE_MODE] = gapReferenceModeToString(hud.m_gapReferenceMode);
        settings[ANIMATION_MODE] = animationModeToString(hud.m_animationMode);
        settings[SHOW_HEADERS] = hud.m_bShowHeaders ? "1" : "0";
        settings[SHOW_SESSION_INFO] = hud.m_bShowSessionInfo ? "1" : "0";
        settings[LIVE_GAPS] = hud.m_bLiveGaps ? "1" : "0";
        settings[IniOnly::Standings::TOP_POSITIONS.key] = std::to_string(hud.m_topPositionsCount);
        settings[IniOnly::Standings::PLAYER_ROW_HIGHLIGHT.key] = hud.m_bPlayerRowHighlight ? "1" : "0";
        settings[IniOnly::Standings::PLAYER_ROW_HIGHLIGHT_BRAND.key] = hud.m_bPlayerRowHighlightBrand ? "1" : "0";
        settings[IniOnly::Standings::LAST_LAP_COLOR.key] = hud.m_bLastLapColorCode ? "1" : "0";
        settings[IniOnly::Standings::ANIMATION_DURATION_MS.key] = std::to_string(static_cast<int>(hud.m_animationDurationMs));
        settings[IniOnly::Standings::CLASSIC_LAYOUT.key] = hud.m_bClassicLayout ? "1" : "0";
        settings[IniOnly::Standings::NAME_MODE.key] = std::to_string(static_cast<int>(hud.m_nameMode));
        settings[IniOnly::Standings::SHORT_NAME_CHARS.key] = std::to_string(hud.m_shortNameChars);
        settings[IniOnly::Standings::LONG_NAME_CHARS.key] = std::to_string(hud.m_longNameChars);
        cache[name] = std::move(settings);
}

void SettingsManager::app_StandingsHud(HudManager& hudManager, const SettingsManager::ProfileCache& cache, const char* name) {
        using namespace Keys::Standings;
        auto it = cache.find(name);
        if (it != cache.end()) {
            auto& hud = hudManager.getStandingsHud();
            applyBaseHudSettings(hud, it->second);

            const auto& settings = it->second;
            try {
                if (settings.count(DISPLAY_ROW_COUNT)) hud.m_displayRowCount = validateDisplayRows(std::stoi(settings.at(DISPLAY_ROW_COUNT)));
                loadStandingsColumns(settings, hud.m_enabledColumns);  // Named keys instead of bitmask
                // Positions-gained mode. Migrate from the old col_posgain bit (a plain on/off
                // that meant "since race start") when the new key is absent.
                if (settings.count(POSGAIN_MODE)) {
                    hud.m_posGainMode = stringToPosGainMode(settings.at(POSGAIN_MODE));
                } else {
                    hud.m_posGainMode = (hud.m_enabledColumns & StandingsHud::COL_POSGAIN)
                        ? StandingsHud::PosGainMode::RACE_START
                        : StandingsHud::PosGainMode::OFF;
                }
                if (settings.count(GAP_MODE)) {
                    hud.m_gapMode = stringToGapMode(settings.at(GAP_MODE));
                } else if (settings.count("showGapColumn")) {
                    // Migrate from old showGapColumn + gapScope
                    bool wasOn = std::stoi(settings.at("showGapColumn")) != 0;
                    if (!wasOn) {
                        hud.m_gapMode = StandingsHud::GapMode::OFF;
                    } else if (settings.count("gapScope") && settings.at("gapScope") == "PLAYER") {
                        hud.m_gapMode = StandingsHud::GapMode::PLAYER;
                    } else {
                        hud.m_gapMode = StandingsHud::GapMode::ALL;
                    }
                } else if (settings.count("gapColumnMode")) {
                    // Migrate from older GapColumnMode
                    hud.m_gapMode = (settings.at("gapColumnMode") == "OFF")
                        ? StandingsHud::GapMode::OFF : StandingsHud::GapMode::ALL;
                } else if (settings.count("officialGapMode") || settings.count("liveGapMode")) {
                    // Migrate from oldest officialGapMode/liveGapMode keys
                    bool hadOfficial = settings.count("officialGapMode") && settings.at("officialGapMode") != "OFF";
                    bool hadLive = settings.count("liveGapMode") && settings.at("liveGapMode") != "OFF";
                    if (!hadOfficial && !hadLive) {
                        hud.m_gapMode = StandingsHud::GapMode::OFF;
                    } else {
                        bool wasPlayer = (settings.count("officialGapMode") && settings.at("officialGapMode") == "PLAYER") ||
                                         (settings.count("liveGapMode") && settings.at("liveGapMode") == "PLAYER");
                        hud.m_gapMode = wasPlayer ? StandingsHud::GapMode::PLAYER : StandingsHud::GapMode::ALL;
                    }
                }
                if (settings.count(GAP_REFERENCE_MODE)) {
                    hud.m_gapReferenceMode = stringToGapReferenceMode(settings.at(GAP_REFERENCE_MODE));
                    if (hud.m_gapReferenceMode == StandingsHud::GapReferenceMode::ALTERNATING) {
                        hud.m_lastGapRefToggle = std::chrono::steady_clock::now();
                        hud.m_alternatingCurrent = StandingsHud::GapReferenceMode::LEADER;
                    }
                }
                if (settings.count(IniOnly::Standings::TOP_POSITIONS.key)) {
                    int topPos = std::stoi(settings.at(IniOnly::Standings::TOP_POSITIONS.key));
                    topPos = std::max(0, std::min(topPos, static_cast<int>(StandingsHud::MAX_TOP_POSITIONS)));
                    hud.m_topPositionsCount = topPos;
                }
                if (settings.count(IniOnly::Standings::PLAYER_ROW_HIGHLIGHT.key)) {
                    hud.m_bPlayerRowHighlight = std::stoi(settings.at(IniOnly::Standings::PLAYER_ROW_HIGHLIGHT.key)) != 0;
                }
                if (settings.count(IniOnly::Standings::PLAYER_ROW_HIGHLIGHT_BRAND.key)) {
                    hud.m_bPlayerRowHighlightBrand = std::stoi(settings.at(IniOnly::Standings::PLAYER_ROW_HIGHLIGHT_BRAND.key)) != 0;
                }
                if (settings.count(IniOnly::Standings::LAST_LAP_COLOR.key)) {
                    hud.m_bLastLapColorCode = std::stoi(settings.at(IniOnly::Standings::LAST_LAP_COLOR.key)) != 0;
                }
                if (settings.count(ANIMATION_MODE)) {
                    hud.m_animationMode = stringToAnimationMode(settings.at(ANIMATION_MODE));
                } else if (settings.count("animatePositions")) {
                    // Legacy: animatePositions=0/1 → OFF/BASIC. Read on load only; not
                    // re-emitted on save, so the key drops out of the INI on first
                    // re-save (intentional — replaced by animationMode).
                    hud.m_animationMode = (std::stoi(settings.at("animatePositions")) != 0)
                        ? StandingsHud::AnimationMode::BASIC
                        : StandingsHud::AnimationMode::OFF;
                }
                if (settings.count(SHOW_HEADERS)) {
                    hud.m_bShowHeaders = std::stoi(settings.at(SHOW_HEADERS)) != 0;
                }
                if (settings.count(SHOW_SESSION_INFO)) {
                    hud.m_bShowSessionInfo = std::stoi(settings.at(SHOW_SESSION_INFO)) != 0;
                }
                if (settings.count(LIVE_GAPS)) {
                    hud.m_bLiveGaps = std::stoi(settings.at(LIVE_GAPS)) != 0;
                }
                if (settings.count(IniOnly::Standings::ANIMATION_DURATION_MS.key)) {
                    int durationMs = std::stoi(settings.at(IniOnly::Standings::ANIMATION_DURATION_MS.key));
                    hud.m_animationDurationMs = static_cast<float>(std::max(50, std::min(1000, durationMs)));
                }
                if (settings.count(IniOnly::Standings::CLASSIC_LAYOUT.key)) {
                    hud.m_bClassicLayout = std::stoi(settings.at(IniOnly::Standings::CLASSIC_LAYOUT.key)) != 0;
                }
                if (settings.count(IniOnly::Standings::NAME_MODE.key)) {
                    int mode = std::stoi(settings.at(IniOnly::Standings::NAME_MODE.key));
                    if (mode >= 0 && mode <= 2) {
                        hud.m_nameMode = static_cast<StandingsHud::NameMode>(mode);
                    }
                } else {
                    // Migrate from old col_name boolean: off -> OFF, on -> SHORT
                    hud.m_nameMode = (hud.m_enabledColumns & StandingsHud::COL_NAME)
                        ? StandingsHud::NameMode::SHORT : StandingsHud::NameMode::OFF;
                }
                if (settings.count(IniOnly::Standings::SHORT_NAME_CHARS.key)) {
                    int chars = std::stoi(settings.at(IniOnly::Standings::SHORT_NAME_CHARS.key));
                    hud.m_shortNameChars = std::max(StandingsHud::MIN_SHORT_NAME_CHARS,
                        std::min(chars, StandingsHud::MAX_SHORT_NAME_CHARS));
                }
                if (settings.count(IniOnly::Standings::LONG_NAME_CHARS.key)) {
                    int chars = std::stoi(settings.at(IniOnly::Standings::LONG_NAME_CHARS.key));
                    hud.m_longNameChars = std::max(StandingsHud::MIN_LONG_NAME_CHARS,
                        std::min(chars, StandingsHud::MAX_LONG_NAME_CHARS));
                }
            } catch (const std::exception& e) {
                DEBUG_WARN_F("StandingsHud: Failed to parse settings: %s", e.what());
            }
            hud.setDataDirty();
        }
}

void SettingsManager::cap_MapHud(const HudManager& hudManager, SettingsManager::ProfileCache& cache, const char* name) {
        HudSettings settings;
        const auto& hud = hudManager.getMapHud();
        captureBaseHudSettings(settings, hud, false);  // Exclude offsetX/Y - anchor is source of truth
        // Map-specific settings
        settings["rotateToPlayer"] = std::to_string(hud.getRotateToPlayer() ? 1 : 0);
        settings["showOutline"] = std::to_string(hud.getShowOutline() ? 1 : 0);
        settings["showTrackMarkers"] = std::to_string(hud.getShowTrackMarkers() ? 1 : 0);
        settings["riderColorMode"] = riderColorModeToString(hud.getRiderColorMode());
        settings["trackWidthScale"] = std::to_string(hud.getTrackWidthScale());
        settings["labelMode"] = labelModeToString(hud.getLabelMode());
        settings[IniOnly::Map::LABEL_ANCHOR.key] = labelAnchorToString(hud.getLabelAnchor());
        settings["riderShape"] = shapeIndexToFilename(hud.getRiderShape());
        settings["anchorPoint"] = anchorPointToString(hud.getAnchorPoint());
        settings["anchorX"] = std::to_string(hud.m_fAnchorX);
        settings["anchorY"] = std::to_string(hud.m_fAnchorY);
        settings["zoomEnabled"] = std::to_string(hud.getZoomEnabled() ? 1 : 0);
        settings["zoomDistance"] = std::to_string(hud.getZoomDistance());
        settings["markerScale"] = std::to_string(hud.getMarkerScale());
        settings["detail"] = detailToString(hud.getDetail());
        cache[name] = std::move(settings);
}

void SettingsManager::app_MapHud(HudManager& hudManager, const SettingsManager::ProfileCache& cache, const char* name) {
        auto it = cache.find(name);
        if (it != cache.end()) {
            auto& hud = hudManager.getMapHud();
            const auto& settings = it->second;
            applyBaseHudSettings(hud, settings);  // Position comes from anchor, not offsetX/Y

            try {
                // Map-specific settings
                if (settings.count("rotateToPlayer")) hud.setRotateToPlayer(std::stoi(settings.at("rotateToPlayer")) != 0);
                if (settings.count("showOutline")) hud.setShowOutline(std::stoi(settings.at("showOutline")) != 0);
                if (settings.count("showTrackMarkers")) hud.setShowTrackMarkers(std::stoi(settings.at("showTrackMarkers")) != 0);
                if (settings.count("riderColorMode")) hud.setRiderColorMode(stringToRiderColorMode(settings.at("riderColorMode")));
                if (settings.count("trackWidthScale")) hud.setTrackWidthScale(validateTrackWidthScale(parseFiniteFloat(settings.at("trackWidthScale"))));
                if (settings.count("labelMode")) hud.setLabelMode(stringToLabelMode(settings.at("labelMode")));
                if (settings.count(IniOnly::Map::LABEL_ANCHOR.key)) hud.setLabelAnchor(stringToLabelAnchor(settings.at(IniOnly::Map::LABEL_ANCHOR.key)));
                if (settings.count("riderShape")) hud.setRiderShape(filenameToShapeIndex(settings.at("riderShape"), 1));
                if (settings.count("zoomEnabled")) hud.setZoomEnabled(std::stoi(settings.at("zoomEnabled")) != 0);
                if (settings.count("zoomDistance")) hud.setZoomDistance(validateZoomDistance(parseFiniteFloat(settings.at("zoomDistance"))));
                if (settings.count("markerScale")) hud.setMarkerScale(parseFiniteFloat(settings.at("markerScale")));
                if (settings.count("detail")) hud.setDetail(stringToDetail(settings.at("detail")));

                // Anchor-based positioning
                if (settings.count("anchorPoint")) hud.setAnchorPoint(stringToAnchorPoint(settings.at("anchorPoint")));
                if (settings.count("anchorX")) hud.m_fAnchorX = parseFiniteFloat(settings.at("anchorX"));
                if (settings.count("anchorY")) hud.m_fAnchorY = parseFiniteFloat(settings.at("anchorY"));
                hud.updatePositionFromAnchor();
            } catch (const std::exception& e) {
                DEBUG_WARN_F("MapHud: Failed to parse settings: %s", e.what());
            }
            hud.setDataDirty();
        }
}

void SettingsManager::cap_RadarHud(const HudManager& hudManager, SettingsManager::ProfileCache& cache, const char* name) {
        HudSettings settings;
        const auto& hud = hudManager.getRadarHud();
        captureBaseHudSettings(settings, hud);
        settings["radarRange"] = std::to_string(hud.getRadarRange());
        settings["riderColorMode"] = radarRiderColorModeToString(hud.getRiderColorMode());
        settings["radarMode"] = radarModeToString(hud.getRadarMode());
        settings["proximityArrowMode"] = proximityArrowModeToString(hud.getProximityArrowMode());
        settings["proximityArrowShape"] = shapeIndexToFilename(hud.getProximityArrowShape());
        settings["proximityArrowScale"] = std::to_string(hud.getProximityArrowScale());
        settings["proximityArrowColorMode"] = proximityArrowColorModeToString(hud.getProximityArrowColorMode());
        settings["alertDistance"] = std::to_string(hud.getAlertDistance());
        settings["labelMode"] = radarLabelModeToString(hud.getLabelMode());
        settings["riderShape"] = shapeIndexToFilename(hud.getRiderShape());
        settings["markerScale"] = std::to_string(hud.getMarkerScale());
        cache[name] = std::move(settings);
}

void SettingsManager::app_RadarHud(HudManager& hudManager, const SettingsManager::ProfileCache& cache, const char* name) {
        auto it = cache.find(name);
        if (it != cache.end()) {
            auto& hud = hudManager.getRadarHud();
            applyBaseHudSettings(hud, it->second);

            const auto& settings = it->second;
            try {
                if (settings.count("radarRange")) {
                    float range = parseFiniteFloat(settings.at("radarRange"));
                    if (range < RadarHud::MIN_RADAR_RANGE) range = RadarHud::MIN_RADAR_RANGE;
                    if (range > RadarHud::MAX_RADAR_RANGE) range = RadarHud::MAX_RADAR_RANGE;
                    hud.setRadarRange(range);
                }
                // riderColorMode - string enum
                if (settings.count("riderColorMode")) {
                    hud.setRiderColorMode(stringToRadarRiderColorMode(settings.at("riderColorMode")));
                }
                if (settings.count("radarMode")) {
                    hud.setRadarMode(stringToRadarMode(settings.at("radarMode")));
                }
                if (settings.count("proximityArrowMode")) {
                    hud.setProximityArrowMode(stringToProximityArrowMode(settings.at("proximityArrowMode")));
                }
                if (settings.count("alertDistance")) {
                    float distance = parseFiniteFloat(settings.at("alertDistance"));
                    if (distance < RadarHud::MIN_ALERT_DISTANCE) distance = RadarHud::MIN_ALERT_DISTANCE;
                    if (distance > RadarHud::MAX_ALERT_DISTANCE) distance = RadarHud::MAX_ALERT_DISTANCE;
                    hud.setAlertDistance(distance);
                }
                if (settings.count("labelMode")) hud.setLabelMode(stringToRadarLabelMode(settings.at("labelMode")));
                if (settings.count("riderShape")) {
                    hud.setRiderShape(filenameToShapeIndex(settings.at("riderShape"), 1));
                }
                if (settings.count("proximityArrowShape")) {
                    hud.setProximityArrowShape(filenameToShapeIndex(settings.at("proximityArrowShape"), 1));
                }
                if (settings.count("proximityArrowScale")) hud.setProximityArrowScale(parseFiniteFloat(settings.at("proximityArrowScale")));
                if (settings.count("proximityArrowColorMode")) {
                    hud.setProximityArrowColorMode(stringToProximityArrowColorMode(settings.at("proximityArrowColorMode")));
                }
                if (settings.count("markerScale")) hud.setMarkerScale(parseFiniteFloat(settings.at("markerScale")));
            } catch (const std::exception& e) {
                DEBUG_WARN_F("RadarHud: Failed to parse settings: %s", e.what());
            }
            hud.setDataDirty();
        }
}

void SettingsManager::cap_PitboardHud(const HudManager& hudManager, SettingsManager::ProfileCache& cache, const char* name) {
        HudSettings settings;
        const auto& hud = hudManager.getPitboardHud();
        captureBaseHudSettings(settings, hud);
        savePitboardRows(settings, hud.m_enabledRows);  // Named keys instead of bitmask
        settings["displayMode"] = pitboardDisplayModeToString(hud.m_displayMode);
        settings["gapCompareMode"] = pitboardGapCompareModeToString(hud.m_gapCompareMode);
        cache[name] = std::move(settings);
}

void SettingsManager::app_PitboardHud(HudManager& hudManager, const SettingsManager::ProfileCache& cache, const char* name) {
        auto it = cache.find(name);
        if (it != cache.end()) {
            auto& hud = hudManager.getPitboardHud();
            applyBaseHudSettings(hud, it->second);

            const auto& settings = it->second;
            try {
                loadPitboardRows(settings, hud.m_enabledRows);  // Named keys instead of bitmask
                if (settings.count("displayMode")) hud.m_displayMode = stringToPitboardDisplayMode(settings.at("displayMode"));
                if (settings.count("gapCompareMode")) hud.m_gapCompareMode = stringToPitboardGapCompareMode(settings.at("gapCompareMode"));
            } catch (const std::exception& e) {
                DEBUG_WARN_F("PitboardHud: Failed to parse settings: %s", e.what());
            }
            hud.setDataDirty();
        }
}

#if GAME_HAS_RECORDS_PROVIDER
void SettingsManager::cap_RecordsHud(const HudManager& hudManager, SettingsManager::ProfileCache& cache, const char* name) {
        HudSettings settings;
        const auto& hud = hudManager.getRecordsHud();
        captureBaseHudSettings(settings, hud);
        saveRecordsColumns(settings, hud.m_enabledColumns);  // Named keys instead of bitmask
        settings["recordsToShow"] = std::to_string(hud.m_recordsToShow);
        settings["showHeaders"] = hud.m_bShowHeaders ? "1" : "0";
        settings[IniOnly::Records::SHOW_FOOTER.key] = hud.m_bShowFooter ? "1" : "0";
        cache[name] = std::move(settings);
}

void SettingsManager::app_RecordsHud(HudManager& hudManager, const SettingsManager::ProfileCache& cache, const char* name) {
        auto it = cache.find(name);
        if (it != cache.end()) {
            auto& hud = hudManager.getRecordsHud();
            applyBaseHudSettings(hud, it->second);

            const auto& settings = it->second;
            try {
                loadRecordsColumns(settings, hud.m_enabledColumns);  // Named keys instead of bitmask
                if (settings.count("recordsToShow")) {
                    int count = std::stoi(settings.at("recordsToShow"));
                    if (count >= 3 && count <= 30) hud.m_recordsToShow = count;
                }
                if (settings.count("showHeaders")) {
                    hud.m_bShowHeaders = std::stoi(settings.at("showHeaders")) != 0;
                }
                if (settings.count(IniOnly::Records::SHOW_FOOTER.key)) {
                    hud.m_bShowFooter = std::stoi(settings.at(IniOnly::Records::SHOW_FOOTER.key)) != 0;
                }
            } catch (const std::exception& e) {
                DEBUG_WARN_F("RecordsHud: Failed to parse settings: %s", e.what());
            }
            hud.setDataDirty();
        }
}
#endif

void SettingsManager::cap_LapLogHud(const HudManager& hudManager, SettingsManager::ProfileCache& cache, const char* name) {
        HudSettings settings;
        const auto& hud = hudManager.getLapLogHud();
        captureBaseHudSettings(settings, hud);
        saveLapLogColumns(settings, hud.m_enabledColumns);  // Named keys instead of bitmask
        settings["maxDisplayLaps"] = std::to_string(hud.m_maxDisplayLaps);
        settings["displayOrder"] = std::to_string(static_cast<int>(hud.m_displayOrder));
        settings["showGapRow"] = hud.m_showGapRow ? "1" : "0";
        settings["showHeaders"] = hud.m_bShowHeaders ? "1" : "0";
        cache[name] = std::move(settings);
}

void SettingsManager::app_LapLogHud(HudManager& hudManager, const SettingsManager::ProfileCache& cache, const char* name) {
        auto it = cache.find(name);
        if (it != cache.end()) {
            auto& hud = hudManager.getLapLogHud();
            applyBaseHudSettings(hud, it->second);

            const auto& settings = it->second;
            try {
                loadLapLogColumns(settings, hud.m_enabledColumns);  // Named keys instead of bitmask
                if (settings.count("maxDisplayLaps")) hud.m_maxDisplayLaps = validateDisplayLaps(std::stoi(settings.at("maxDisplayLaps")));
                if (settings.count("displayOrder")) {
                    int order = std::stoi(settings.at("displayOrder"));
                    hud.m_displayOrder = (order == 1) ? LapLogHud::DisplayOrder::NEWEST_FIRST : LapLogHud::DisplayOrder::OLDEST_FIRST;
                }
                if (settings.count("showGapRow")) hud.m_showGapRow = (settings.at("showGapRow") == "1");
                if (settings.count("showHeaders")) hud.m_bShowHeaders = std::stoi(settings.at("showHeaders")) != 0;
            } catch (const std::exception& e) {
                DEBUG_WARN_F("LapLogHud: Failed to parse settings: %s", e.what());
            }
            hud.setDataDirty();
        }
}

void SettingsManager::cap_SessionChartsHud(const HudManager& hudManager, SettingsManager::ProfileCache& cache, const char* name) {
        HudSettings settings;
        const auto& hud = hudManager.getSessionChartsHud();
        captureBaseHudSettings(settings, hud);
        settings["enabledCharts"] = std::to_string(static_cast<int>(hud.m_enabledCharts));
        settings["enabledElements"] = std::to_string(static_cast<int>(hud.m_enabledElements));
        settings["colorMode"] = std::to_string(static_cast<int>(hud.m_riderColorMode));
        settings["topPositionsCount"] = std::to_string(hud.m_topPositionsCount);
        settings["displayRowCount"] = std::to_string(hud.m_displayRowCount);
        // Advanced tuning (INI-only)
        settings["outlierFactor"] = std::to_string(hud.m_outlierFactor);
        cache[name] = std::move(settings);
}

void SettingsManager::app_SessionChartsHud(HudManager& hudManager, const SettingsManager::ProfileCache& cache, const char* name) {
        auto it = cache.find(name);
        if (it != cache.end()) {
            auto& hud = hudManager.getSessionChartsHud();
            applyBaseHudSettings(hud, it->second);

            const auto& settings = it->second;
            try {
                if (settings.count("enabledCharts")) {
                    int charts = std::stoi(settings.at("enabledCharts"));
                    charts &= static_cast<int>(SessionChartsHud::CHART_ALLFLAGS);  // ignore stray bits
                    hud.m_enabledCharts = static_cast<uint32_t>(charts);
                }
                if (settings.count("enabledElements")) {
                    int elems = std::stoi(settings.at("enabledElements"));
                    hud.m_enabledElements = static_cast<uint32_t>(elems);
                }
                if (settings.count("colorMode")) {
                    int mode = std::stoi(settings.at("colorMode"));
                    int maxMode = static_cast<int>(SessionChartsHud::RiderColorMode::COLOR_MODE_COUNT) - 1;
                    if (mode >= 0 && mode <= maxMode) {
                        hud.m_riderColorMode = static_cast<SessionChartsHud::RiderColorMode>(mode);
                    }
                }
                if (settings.count("displayRowCount")) {
                    int count = std::stoi(settings.at("displayRowCount"));
                    hud.m_displayRowCount = std::max(SessionChartsHud::MIN_ROW_COUNT,
                                                     std::min(count, SessionChartsHud::MAX_ROW_COUNT));
                }
                if (settings.count("topPositionsCount")) {
                    int count = std::stoi(settings.at("topPositionsCount"));
                    int maxTop = std::min(SessionChartsHud::MAX_TOP_COUNT, hud.m_displayRowCount);
                    hud.m_topPositionsCount = std::max(SessionChartsHud::MIN_TOP_COUNT,
                                                       std::min(count, maxTop));
                }
                // Advanced tuning (INI-only)
                if (settings.count("outlierFactor")) {
                    float factor = parseFiniteFloat(settings.at("outlierFactor"));
                    hud.m_outlierFactor = std::max(1.05f, std::min(factor, 5.0f));
                }
            } catch (const std::exception& e) {
                DEBUG_WARN_F("SessionChartsHud: Failed to parse settings: %s", e.what());
            }
            hud.setDataDirty();
        }
}

#if GAME_HAS_FMX
void SettingsManager::cap_FmxHud(const HudManager& hudManager, SettingsManager::ProfileCache& cache, const char* name) {
        using namespace Keys::Fmx;
        HudSettings settings;
        const auto& hud = hudManager.getFmxHud();
        captureBaseHudSettings(settings, hud);
        settings[ENABLED_ROWS] = std::to_string(hud.m_enabledRows);
        settings[MAX_CHAIN_DISPLAY_ROWS] = std::to_string(hud.m_maxChainDisplayRows);
        settings[SHOW_DEBUG_LOGGING] = hud.m_showDebugLogging ? "1" : "0";

        // Per-trick enable flags (INI-only). Skip index 0 (NONE) and the RIGHT
        // half of L/R pairs — they share a key with the LEFT variant. The
        // gate in FmxManager indexes by full enum, so apply() mirrors the
        // value to both indices when reading back.
        const auto& fmxCfg = FmxManager::getInstance().getConfig();
        for (int i = 1; i < static_cast<int>(Fmx::TrickType::COUNT); ++i) {
            auto t = static_cast<Fmx::TrickType>(i);
            if (Fmx::getTrickDirection(t) == Fmx::TrickDirection::RIGHT) continue;
            std::string key = std::string(TRICK_ENABLED_PREFIX) + Fmx::getTrickIniKey(t);
            settings[key] = fmxCfg.tricksEnabled[i] ? "1" : "0";
        }

        cache[name] = std::move(settings);
}

void SettingsManager::app_FmxHud(HudManager& hudManager, const SettingsManager::ProfileCache& cache, const char* name) {
        using namespace Keys::Fmx;
        auto it = cache.find(name);
        if (it != cache.end()) {
            auto& hud = hudManager.getFmxHud();
            applyBaseHudSettings(hud, it->second);

            const auto& settings = it->second;
            try {
                if (settings.count(ENABLED_ROWS)) {
                    hud.m_enabledRows = static_cast<uint32_t>(std::stoul(settings.at(ENABLED_ROWS)));
                }
                if (settings.count(MAX_CHAIN_DISPLAY_ROWS)) {
                    int rows = std::stoi(settings.at(MAX_CHAIN_DISPLAY_ROWS));
                    hud.m_maxChainDisplayRows = std::max(0, std::min(10, rows));
                }
                if (settings.count(SHOW_DEBUG_LOGGING)) {
                    hud.m_showDebugLogging = settings.at(SHOW_DEBUG_LOGGING) == "1";
                    FmxManager::getInstance().setLoggingEnabled(hud.m_showDebugLogging);
                }

                // Per-trick enable flags (INI-only). Always reset the mask to
                // defaults first so a profile switch is deterministic — without
                // this, a profile lacking the keys would silently inherit the
                // previously applied profile's disable state. Skip RIGHT half
                // of L/R pairs (shared key); mirror the value to both indices.
                auto& fmxMgr = FmxManager::getInstance();
                Fmx::FmxConfig fmxCfg = fmxMgr.getConfig();
                for (auto& b : fmxCfg.tricksEnabled) b = true;
                for (int i = 1; i < static_cast<int>(Fmx::TrickType::COUNT); ++i) {
                    auto t = static_cast<Fmx::TrickType>(i);
                    if (Fmx::getTrickDirection(t) == Fmx::TrickDirection::RIGHT) continue;
                    std::string key = std::string(TRICK_ENABLED_PREFIX) + Fmx::getTrickIniKey(t);
                    auto entry = settings.find(key);
                    if (entry != settings.end()) {
                        bool enabled = std::stoi(entry->second) != 0;
                        fmxCfg.tricksEnabled[i] = enabled;
                        Fmx::TrickType opposite = Fmx::flipTrickDirection(t);
                        if (opposite != t) {
                            fmxCfg.tricksEnabled[static_cast<int>(opposite)] = enabled;
                        }
                    }
                }
                fmxMgr.setConfig(fmxCfg);
            } catch (const std::exception& e) {
                DEBUG_WARN_F("FmxHud: Failed to parse settings: %s", e.what());
            }
            hud.setDataDirty();
        }
}
#endif

void SettingsManager::cap_StatsHud(const HudManager& hudManager, SettingsManager::ProfileCache& cache, const char* name) {
        using namespace Keys::Stats;
        HudSettings settings;
        const auto& hud = hudManager.getStatsHud();
        captureBaseHudSettings(settings, hud);
        settings[VISIBILITY_MODE] = std::to_string(static_cast<int>(hud.m_visibilityMode));
        settings[SHOW_LAP] = hud.m_showLap ? "1" : "0";
        settings[SHOW_SESSION] = hud.m_showSession ? "1" : "0";
        settings[SHOW_ALLTIME] = hud.m_showAllTime ? "1" : "0";
        cache[name] = std::move(settings);
}

void SettingsManager::app_StatsHud(HudManager& hudManager, const SettingsManager::ProfileCache& cache, const char* name) {
        using namespace Keys::Stats;
        auto it = cache.find(name);
        if (it != cache.end()) {
            auto& hud = hudManager.getStatsHud();
            applyBaseHudSettings(hud, it->second);

            const auto& settings = it->second;
            try {
                if (settings.count(VISIBILITY_MODE)) {
                    int mode = std::stoi(settings.at(VISIBILITY_MODE));
                    if (mode >= 0 && mode < static_cast<int>(StatsHud::VisibilityMode::COUNT)) {
                        hud.m_visibilityMode = static_cast<StatsHud::VisibilityMode>(mode);
                    }
                }
                if (settings.count(SHOW_LAP)) {
                    hud.m_showLap = settings.at(SHOW_LAP) == "1";
                }
                if (settings.count(SHOW_SESSION)) {
                    hud.m_showSession = settings.at(SHOW_SESSION) == "1";
                }
                if (settings.count(SHOW_ALLTIME)) {
                    hud.m_showAllTime = settings.at(SHOW_ALLTIME) == "1";
                }
            } catch (const std::exception& e) {
                DEBUG_WARN_F("StatsHud: Failed to parse settings: %s", e.what());
            }
            hud.setDataDirty();
        }
}

void SettingsManager::cap_EventLogHud(const HudManager& hudManager, SettingsManager::ProfileCache& cache, const char* name) {
        HudSettings settings;
        const auto& hud = hudManager.getEventLogHud();
        captureBaseHudSettings(settings, hud);
        settings["displayMode"] = std::to_string(static_cast<int>(hud.m_displayMode));
        settings["displayOrder"] = std::to_string(static_cast<int>(hud.m_displayOrder));
        settings["maxDisplayEvents"] = std::to_string(hud.m_maxDisplayEvents);
        settings["autoHideDurationMs"] = std::to_string(hud.m_autoHideDurationMs);
        settings["timestampMode"] = std::to_string(static_cast<int>(hud.m_timestampMode));
        settings["showIcons"] = hud.m_showIcons ? "1" : "0";
        saveEventLogEvents(settings, hud.m_enabledEvents);
        cache[name] = std::move(settings);
}

void SettingsManager::app_EventLogHud(HudManager& hudManager, const SettingsManager::ProfileCache& cache, const char* name) {
        auto it = cache.find(name);
        if (it != cache.end()) {
            auto& hud = hudManager.getEventLogHud();
            applyBaseHudSettings(hud, it->second);

            const auto& settings = it->second;
            try {
                loadEventLogEvents(settings, hud.m_enabledEvents);
                if (settings.count("displayMode")) {
                    int mode = std::stoi(settings.at("displayMode"));
                    if (mode >= 0 && mode <= 2) {
                        hud.m_displayMode = static_cast<EventLogHud::DisplayMode>(mode);
                    }
                }
                if (settings.count("displayOrder")) {
                    int order = std::stoi(settings.at("displayOrder"));
                    hud.m_displayOrder = (order == 1)
                        ? EventLogHud::DisplayOrder::NEWEST_FIRST
                        : EventLogHud::DisplayOrder::OLDEST_FIRST;
                }
                if (settings.count("maxDisplayEvents")) {
                    int max = std::stoi(settings.at("maxDisplayEvents"));
                    hud.m_maxDisplayEvents = std::max(EventLogHud::MIN_DISPLAY_EVENTS,
                                                      std::min(max, EventLogHud::MAX_DISPLAY_EVENTS));
                }
                if (settings.count("autoHideDurationMs")) {
                    int duration = std::stoi(settings.at("autoHideDurationMs"));
                    if (duration >= EventLogHud::MIN_AUTO_HIDE_MS && duration <= EventLogHud::MAX_AUTO_HIDE_MS) {
                        hud.m_autoHideDurationMs = duration;
                    }
                }
                if (settings.count("timestampMode")) {
                    int mode = std::stoi(settings.at("timestampMode"));
                    if (mode >= 0 && mode <= 2) {
                        hud.m_timestampMode = static_cast<EventLogHud::TimestampMode>(mode);
                    }
                }
                // Legacy: migrate old useWallClock bool to new timestampMode
                else if (settings.count("useWallClock")) {
                    hud.m_timestampMode = (settings.at("useWallClock") == "1")
                        ? EventLogHud::TimestampMode::CLOCK
                        : EventLogHud::TimestampMode::SESSION;
                }
                if (settings.count("showIcons")) {
                    hud.m_showIcons = (settings.at("showIcons") == "1");
                }
            } catch (const std::exception& e) {
                DEBUG_WARN_F("EventLogHud: Failed to parse settings: %s", e.what());
            }
            hud.setDataDirty();
        }
}

#if GAME_HAS_STEAM_FRIENDS
void SettingsManager::cap_FriendsHud(const HudManager& hudManager, SettingsManager::ProfileCache& cache, const char* name) {
        HudSettings settings;
        const auto& hud = hudManager.getFriendsHud();
        captureBaseHudSettings(settings, hud);
        saveBitAsKey(settings, "col_server", hud.m_enabledColumns, FriendsHud::COL_SERVER);
        saveBitAsKey(settings, "col_track",  hud.m_enabledColumns, FriendsHud::COL_TRACK);
        saveBitAsKey(settings, "col_info",   hud.m_enabledColumns, FriendsHud::COL_INFO);
        saveBitAsKey(settings, "col_timer",  hud.m_enabledColumns, FriendsHud::COL_TIMER);
        settings["maxDisplayRows"] = std::to_string(hud.m_maxDisplayRows);
        settings["showHeaders"] = hud.m_bShowHeaders ? "1" : "0";
        settings["showMode"] = std::to_string(static_cast<int>(hud.m_showMode));
        settings["onJoinDurationMs"] = std::to_string(hud.m_onJoinDurationMs);  // INI-only
        settings["showSelf"] = hud.m_showSelf ? "1" : "0";
        cache[name] = std::move(settings);
}

void SettingsManager::app_FriendsHud(HudManager& hudManager, const SettingsManager::ProfileCache& cache, const char* name) {
        auto it = cache.find(name);
        if (it != cache.end()) {
            auto& hud = hudManager.getFriendsHud();
            applyBaseHudSettings(hud, it->second);

            const auto& settings = it->second;
            try {
                loadBitFromKey(settings, "col_server", hud.m_enabledColumns, FriendsHud::COL_SERVER);
                loadBitFromKey(settings, "col_track",  hud.m_enabledColumns, FriendsHud::COL_TRACK);
                loadBitFromKey(settings, "col_info",   hud.m_enabledColumns, FriendsHud::COL_INFO);
                loadBitFromKey(settings, "col_timer",  hud.m_enabledColumns, FriendsHud::COL_TIMER);
                if (settings.count("maxDisplayRows")) {
                    int r = std::stoi(settings.at("maxDisplayRows"));
                    hud.m_maxDisplayRows = std::max(FriendsHud::MIN_DISPLAY_ROWS, std::min(FriendsHud::MAX_DISPLAY_ROWS, r));
                }
                if (settings.count("showHeaders")) hud.m_bShowHeaders = std::stoi(settings.at("showHeaders")) != 0;
                if (settings.count("showMode")) {
                    int sm = std::stoi(settings.at("showMode"));
                    if (sm >= 0 && sm < static_cast<int>(FriendsHud::ShowMode::COUNT)) {
                        hud.m_showMode = static_cast<FriendsHud::ShowMode>(sm);
                    }
                }
                if (settings.count("onJoinDurationMs")) {
                    int ms = std::stoi(settings.at("onJoinDurationMs"));
                    hud.m_onJoinDurationMs = std::max(1000, std::min(120000, ms));  // 1s..2min
                }
                if (settings.count("showSelf")) hud.m_showSelf = std::stoi(settings.at("showSelf")) != 0;
            } catch (const std::exception& e) {
                DEBUG_WARN_F("FriendsHud: Failed to parse settings: %s", e.what());
            }
            hud.setDataDirty();
        }
}
#endif

void SettingsManager::cap_IdealLapHud(const HudManager& hudManager, SettingsManager::ProfileCache& cache, const char* name) {
        HudSettings settings;
        const auto& hud = hudManager.getIdealLapHud();
        captureBaseHudSettings(settings, hud);
        saveIdealLapRows(settings, hud.m_enabledRows);  // Named keys instead of bitmask
        cache[name] = std::move(settings);
}

void SettingsManager::app_IdealLapHud(HudManager& hudManager, const SettingsManager::ProfileCache& cache, const char* name) {
        auto it = cache.find(name);
        if (it != cache.end()) {
            auto& hud = hudManager.getIdealLapHud();
            applyBaseHudSettings(hud, it->second);

            const auto& settings = it->second;
            try {
                loadIdealLapRows(settings, hud.m_enabledRows);  // Named keys instead of bitmask
            } catch (const std::exception& e) {
                DEBUG_WARN_F("IdealLapHud: Failed to parse settings: %s", e.what());
            }
            hud.setDataDirty();
        }
}

void SettingsManager::cap_TelemetryHud(const HudManager& hudManager, SettingsManager::ProfileCache& cache, const char* name) {
        HudSettings settings;
        const auto& hud = hudManager.getTelemetryHud();
        captureBaseHudSettings(settings, hud);
        saveTelemetryElements(settings, hud.m_enabledElements);  // Named keys instead of bitmask
        settings["displayMode"] = displayModeToString(hud.m_displayMode);
        cache[name] = std::move(settings);
}

void SettingsManager::app_TelemetryHud(HudManager& hudManager, const SettingsManager::ProfileCache& cache, const char* name) {
        auto it = cache.find(name);
        if (it != cache.end()) {
            auto& hud = hudManager.getTelemetryHud();
            applyBaseHudSettings(hud, it->second);

            const auto& settings = it->second;
            try {
                loadTelemetryElements(settings, hud.m_enabledElements);  // Named keys instead of bitmask
                if (settings.count("displayMode")) hud.m_displayMode = stringToDisplayMode(settings.at("displayMode"));
            } catch (const std::exception& e) {
                DEBUG_WARN_F("TelemetryHud: Failed to parse settings: %s", e.what());
            }
            hud.setDataDirty();
        }
}

void SettingsManager::cap_PerformanceHud(const HudManager& hudManager, SettingsManager::ProfileCache& cache, const char* name) {
        HudSettings settings;
        const auto& hud = hudManager.getPerformanceHud();
        captureBaseHudSettings(settings, hud);
        savePerformanceElements(settings, hud.m_enabledElements);  // Named keys instead of bitmask
        settings["displayMode"] = displayModeToString(hud.m_displayMode);
        cache[name] = std::move(settings);
}

void SettingsManager::app_PerformanceHud(HudManager& hudManager, const SettingsManager::ProfileCache& cache, const char* name) {
        auto it = cache.find(name);
        if (it != cache.end()) {
            auto& hud = hudManager.getPerformanceHud();
            applyBaseHudSettings(hud, it->second);

            const auto& settings = it->second;
            try {
                loadPerformanceElements(settings, hud.m_enabledElements);  // Named keys instead of bitmask
                if (settings.count("displayMode")) hud.m_displayMode = stringToDisplayMode(settings.at("displayMode"));
            } catch (const std::exception& e) {
                DEBUG_WARN_F("PerformanceHud: Failed to parse settings: %s", e.what());
            }
            hud.setDataDirty();
        }
}

namespace Settings {

const std::vector<HudSectionSerializer>& hudSectionRegistry() {
    static const std::vector<HudSectionSerializer> kRegistry = {
        { "StandingsHud", &SettingsManager::cap_StandingsHud, &SettingsManager::app_StandingsHud },
        { "MapHud", &SettingsManager::cap_MapHud, &SettingsManager::app_MapHud },
        { "RadarHud", &SettingsManager::cap_RadarHud, &SettingsManager::app_RadarHud },
        { "PitboardHud", &SettingsManager::cap_PitboardHud, &SettingsManager::app_PitboardHud },
#if GAME_HAS_RECORDS_PROVIDER
        { "RecordsHud", &SettingsManager::cap_RecordsHud, &SettingsManager::app_RecordsHud },
#endif
        { "LapLogHud", &SettingsManager::cap_LapLogHud, &SettingsManager::app_LapLogHud },
        { "SessionChartsHud", &SettingsManager::cap_SessionChartsHud, &SettingsManager::app_SessionChartsHud },
#if GAME_HAS_FMX
        { "FmxHud", &SettingsManager::cap_FmxHud, &SettingsManager::app_FmxHud },
#endif
        { "StatsHud", &SettingsManager::cap_StatsHud, &SettingsManager::app_StatsHud },
        { "EventLogHud", &SettingsManager::cap_EventLogHud, &SettingsManager::app_EventLogHud },
#if GAME_HAS_STEAM_FRIENDS
        { "FriendsHud", &SettingsManager::cap_FriendsHud, &SettingsManager::app_FriendsHud },
#endif
        { "IdealLapHud", &SettingsManager::cap_IdealLapHud, &SettingsManager::app_IdealLapHud },
        { "TelemetryHud", &SettingsManager::cap_TelemetryHud, &SettingsManager::app_TelemetryHud },
        { "PerformanceHud", &SettingsManager::cap_PerformanceHud, &SettingsManager::app_PerformanceHud },
        { "LapWidget", &SettingsManager::cap_LapWidget, &SettingsManager::app_LapWidget },
        { "PositionWidget", &SettingsManager::cap_PositionWidget, &SettingsManager::app_PositionWidget },
        { "TimeWidget", &SettingsManager::cap_TimeWidget, &SettingsManager::app_TimeWidget },
        { "ClockWidget", &SettingsManager::cap_ClockWidget, &SettingsManager::app_ClockWidget },
        { "SessionHud", &SettingsManager::cap_SessionHud, &SettingsManager::app_SessionHud },
        { "SpeedWidget", &SettingsManager::cap_SpeedWidget, &SettingsManager::app_SpeedWidget },
        { "GearWidget", &SettingsManager::cap_GearWidget, &SettingsManager::app_GearWidget },
        { "SpeedoWidget", &SettingsManager::cap_SpeedoWidget, &SettingsManager::app_SpeedoWidget },
        { "TachoWidget", &SettingsManager::cap_TachoWidget, &SettingsManager::app_TachoWidget },
        { "TimingHud", &SettingsManager::cap_TimingHud, &SettingsManager::app_TimingHud },
        { "GapBarHud", &SettingsManager::cap_GapBarHud, &SettingsManager::app_GapBarHud },
        { "BarsWidget", &SettingsManager::cap_BarsWidget, &SettingsManager::app_BarsWidget },
        { "VersionWidget", &SettingsManager::cap_VersionWidget, &SettingsManager::app_VersionWidget },
        { "NoticesHud", &SettingsManager::cap_NoticesHud, &SettingsManager::app_NoticesHud },
        { "FuelWidget", &SettingsManager::cap_FuelWidget, &SettingsManager::app_FuelWidget },
        { "GamepadWidget", &SettingsManager::cap_GamepadWidget, &SettingsManager::app_GamepadWidget },
        { "LeanWidget", &SettingsManager::cap_LeanWidget, &SettingsManager::app_LeanWidget },
        { "GForceWidget", &SettingsManager::cap_GForceWidget, &SettingsManager::app_GForceWidget },
        { "CompassWidget", &SettingsManager::cap_CompassWidget, &SettingsManager::app_CompassWidget },
#if GAME_HAS_TYRE_TEMP
        { "TyreTempWidget", &SettingsManager::cap_TyreTempWidget, &SettingsManager::app_TyreTempWidget },
#endif
#if GAME_HAS_ECU
        { "EcuWidget", &SettingsManager::cap_EcuWidget, &SettingsManager::app_EcuWidget },
#endif
        { "SettingsButtonWidget", &SettingsManager::cap_SettingsButtonWidget, &SettingsManager::app_SettingsButtonWidget },
        { "PointerWidget", &SettingsManager::cap_PointerWidget, &SettingsManager::app_PointerWidget },
        { "RumbleHud", &SettingsManager::cap_RumbleHud, &SettingsManager::app_RumbleHud },
        { "BenchmarkWidget", &SettingsManager::cap_BenchmarkWidget, &SettingsManager::app_BenchmarkWidget },
        { "Global", &SettingsManager::cap_Global, &SettingsManager::app_Global },
    };
    return kRegistry;
}

} // namespace Settings

#if defined(MXBMRP3_TEST_BUILD)
// Benchmark-only: crank the cost-driving settings of the heavy HUDs to maximum so
// the headless bench driver can profile worst-case rebuild cost. Defined here
// because this TU already includes every HUD header and SettingsManager is their
// friend. Compiled out of shipping DLLs.
void SettingsManager::testMaxAllHudSettings(HudManager& hudManager) {
    {   // Standings: all 10 columns, max rows, long names, all gaps + posgain + colored anim
        StandingsHud& h = hudManager.getStandingsHud();
        h.m_enabledColumns = 0x3FF;
        h.m_displayRowCount = 50;
        h.m_nameMode = StandingsHud::NameMode::LONG;
        h.m_gapMode = StandingsHud::GapMode::ALL;
        h.m_gapReferenceMode = StandingsHud::GapReferenceMode::ALTERNATING;
        h.m_posGainMode = StandingsHud::PosGainMode::LAST_SPLIT;
        h.m_animationMode = StandingsHud::AnimationMode::COLORED;
        h.setVisible(true); h.setDataDirty();
    }
    {   // Lap log: all columns, many laps
        LapLogHud& h = hudManager.getLapLogHud();
        h.m_enabledColumns = 0xFFFFFFFFu;
        h.m_maxDisplayLaps = 30;
        h.setVisible(true); h.setDataDirty();
    }
    {   // Event log: all event types, many rows
        EventLogHud& h = hudManager.getEventLogHud();
        h.m_enabledEvents = 0xFFFFFFFFu;
        h.m_maxDisplayEvents = 30;
        h.setVisible(true); h.setDataDirty();
    }
    {   // Ideal lap: all rows
        IdealLapHud& h = hudManager.getIdealLapHud();
        h.m_enabledRows = 0xFFFFFFFFu;
        h.setVisible(true); h.setDataDirty();
    }
    {   // Stats: show every category
        StatsHud& h = hudManager.getStatsHud();
        h.m_showLap = true; h.m_showSession = true; h.m_showAllTime = true;
        h.setVisible(true); h.setDataDirty();
    }
#if GAME_HAS_RECORDS_PROVIDER
    {   // Records: all columns
        RecordsHud& h = hudManager.getRecordsHud();
        h.m_enabledColumns = 0xFFFFFFFFu;
        h.setVisible(true); h.setDataDirty();
    }
#endif
    {   // Map: highest detail + all overlays + labels
        MapHud& h = hudManager.getMapHud();
        h.setDetail(MapHud::Detail::HIGH);
        h.setShowOutline(true);
        h.setShowTrackMarkers(true);
        h.setLabelMode(MapHud::LabelMode::BOTH);
        h.setVisible(true); h.setDataDirty();
    }
}
#endif  // MXBMRP3_TEST_BUILD
