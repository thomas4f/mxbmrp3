// ============================================================================
// core/settings_manager_global.cpp
// Global (non-per-profile) settings serialization for SettingsManager:
// writeGlobalSettings() / applyGlobalLine() and their helpers. These handle the
// [General], [Rumble], [HelmetOverlay], [Display], [WebServer], colors, fonts,
// hotkeys, and per-feature analytics-flag sections. Split out of
// settings_manager.cpp (which owns per-HUD capture/apply/serialize and load).
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
#include <algorithm>
#include <cassert>
#include <cmath>
#include <windows.h>

// Bring the centralized INI key names / serde helpers into scope.
using namespace Settings;

namespace {
// "StandingsHud" -> "hud_standings", "TyreTempWidget" -> "widget_tyre_temp".
// Strips the class suffix, prefixes by kind, and converts PascalCase to
// snake_case. The result is a STABLE analytics key, so don't change this
// transform once shipped or historical per-feature data fragments.
std::string sectionToFlagKey(const std::string& section) {
    auto endsWith = [](const std::string& s, const std::string& suf) {
        return s.size() >= suf.size() &&
               s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
    };
    std::string prefix, core;
    if (endsWith(section, "Widget")) { prefix = "widget_"; core = section.substr(0, section.size() - 6); }
    else if (endsWith(section, "Hud")) { prefix = "hud_"; core = section.substr(0, section.size() - 3); }
    else { return ""; }  // not a HUD/widget (e.g. the "Global" pseudo-entry) — skip

    std::string out;
    for (size_t i = 0; i < core.size(); ++i) {
        char c = core[i];
        if (c >= 'A' && c <= 'Z') {
            if (i > 0) out += '_';
            out += static_cast<char>(c - 'A' + 'a');
        } else {
            out += c;
        }
    }
    return prefix + out;
}
}  // namespace

void SettingsManager::getHudWidgetFlags(const HudManager& hudManager,
                                        std::vector<std::pair<std::string, int>>& outFlags) {
    // Reuse the canonical capture so this never needs its own HUD list to drift.
    ProfileCache cache;
    captureToCache(hudManager, cache);

    for (const auto& entry : cache) {
        std::string key = sectionToFlagKey(entry.first);
        if (key.empty()) continue;  // skip non-HUD/widget entries (e.g. "Global")
        auto it = entry.second.find("visible");
        const int on = (it != entry.second.end() && it->second == "1") ? 1 : 0;
        outFlags.emplace_back(std::move(key), on);
    }
    std::sort(outFlags.begin(), outFlags.end());
}

void SettingsManager::writeGlobalSettings(std::ostream& out, const HudManager& hudManager) const {
    // Write General section (global preferences)
    out << "[General]\n";
    out << "autoSave=" << (UiConfig::getInstance().getAutoSave() ? 1 : 0) << "\n";
    out << "controller=" << XInputReader::getInstance().getRumbleConfig().controllerIndex << "\n";
    out << "pbScope=" << pbScopeToString(UiConfig::getInstance().getPBScope()) << "\n";
#if GAME_HAS_RECORDS_PROVIDER
    out << "recordsAutoFetch=" << (hudManager.getRecordsHud().m_bAutoFetch ? 1 : 0) << "\n";
    out << "recordsProvider=" << dataProviderToString(hudManager.getRecordsHud().m_provider) << "\n";
#endif
#if GAME_HAS_DISCORD
    out << "discordRichPresence=" << (DiscordManager::getInstance().isEnabled() ? 1 : 0) << "\n";
#endif
#if GAME_HAS_STEAM_FRIENDS
    out << "steamFriends=" << (SteamFriendsManager::getInstance().isEnabled() ? 1 : 0) << "\n";
#endif
#if GAME_HAS_ANALYTICS
    out << "analytics=" << (AnalyticsManager::getInstance().isEnabled() ? 1 : 0) << " ; Anonymous usage stats (opt-out)\n";
#endif
    out << "filterDnsRiders=" << (PluginData::getInstance().isFilterDnsRiders() ? 1 : 0) << "\n";
#if GAME_HAS_HTTP_SERVER
    out << "webServer=" << (HttpServer::getInstance().isEnabled() ? 1 : 0) << " ; Web overlay server (port and throttle in [Advanced])\n";
#endif
    out << "\n";

    // Write Updates section (auto-update settings, owned solely by the Updates tab).
    // Consolidated here from [General] (updateMode/dismissedVersion) and [Advanced]
    // (updateChannel/updateDebugMode) so the tab maps 1:1 to one INI section and resets
    // via section replay. updateChannel is written before dismissedVersion because
    // setChannel() clears the dismissed version when the channel changes — applying it
    // first keeps a same-channel dismissal intact on load.
    {
        const char* channelStr = (UpdateChecker::getInstance().getChannel() == UpdateChecker::UpdateChannel::PRERELEASE) ? "prerelease" : "stable";
        const char* updateModeStr = "off";
        switch (UpdateChecker::getInstance().getMode()) {
            case UpdateChecker::UpdateMode::OFF: updateModeStr = "off"; break;
            case UpdateChecker::UpdateMode::NOTIFY: updateModeStr = "notify"; break;
        }
        out << "[Updates]\n";
        out << "updateChannel=" << channelStr << "\n";
        out << "updateMode=" << updateModeStr << "\n";
        out << "updateDebugMode=" << (UpdateChecker::getInstance().isDebugMode() ? 1 : 0) << "\n";
        // Dismissed version (suppresses re-notifying about an update the user dismissed).
        // Written unconditionally — even when empty — so the factory-defaults snapshot
        // (captured before load, when it is empty) carries the key, and a full reset
        // restores it to empty via the normal [Updates] replay. A conditional write would
        // omit it from the snapshot, leaving a stale dismissal stuck across "Reset all
        // settings". An empty value loads as a no-op (setDismissedVersion("")).
        out << "dismissedVersion=" << UpdateChecker::getInstance().getDismissedVersion() << "\n";
        out << "donationNudge=" << (UpdateDownloader::getInstance().isDonationNudgeEnabled() ? 1 : 0) << "\n";
        out << "\n";
    }

    // Write Advanced section (power-user settings)
    out << "[Advanced]\n";
    out << IniOnly::Advanced::DEVELOPER_MODE.key << "=" << (m_developerMode ? 1 : 0) << " ; " << IniOnly::Advanced::DEVELOPER_MODE.description << "\n";
    // Note: updateChannel/updateMode/updateDebugMode/dismissedVersion moved to [Updates]
    // Note: mapPixelSpacing moved to [MapHud]
    // Note: speedoNeedleColor, speedoShowOdometer, speedoShowTripmeter moved to [SpeedoWidget]
    // Note: tachoNeedleColor moved to [TachoWidget]
    // Note: leanArcFillColor moved to [LeanWidget]
    // Note: recordsShowFooter moved to [RecordsHud]
    // Note: standingsTopPositions, standingsUseAccentHighlight moved to [StandingsHud]
    out << IniOnly::Advanced::DROP_SHADOW_OFFSET_X.key << "=" << UiConfig::getInstance().getDropShadowOffsetX() << " ; " << IniOnly::Advanced::DROP_SHADOW_OFFSET_X.description << "\n";
    out << IniOnly::Advanced::DROP_SHADOW_OFFSET_Y.key << "=" << UiConfig::getInstance().getDropShadowOffsetY() << " ; " << IniOnly::Advanced::DROP_SHADOW_OFFSET_Y.description << "\n";
    out << IniOnly::Advanced::DROP_SHADOW_COLOR.key << "=" << PluginUtils::formatColorHex(UiConfig::getInstance().getDropShadowColor()) << " ; " << IniOnly::Advanced::DROP_SHADOW_COLOR.description << "\n";
    out << IniOnly::Advanced::HOLD_REPEAT_FAST_MS.key << "=" << UiConfig::getInstance().getHoldRepeatFastMs() << " ; " << IniOnly::Advanced::HOLD_REPEAT_FAST_MS.description << "\n";
    out << IniOnly::Advanced::GRID_OVERLAY.key << "=" << (UiConfig::getInstance().getGridOverlay() ? 1 : 0) << " ; " << IniOnly::Advanced::GRID_OVERLAY.description << "\n";
    out << IniOnly::Advanced::GRID_OVERLAY_MAJOR_EVERY.key << "=" << UiConfig::getInstance().getGridOverlayMajorEvery() << " ; " << IniOnly::Advanced::GRID_OVERLAY_MAJOR_EVERY.description << "\n";
    out << IniOnly::Advanced::GRID_OVERLAY_COLOR.key << "=" << PluginUtils::formatColorHex(UiConfig::getInstance().getGridOverlayColor()) << " ; " << IniOnly::Advanced::GRID_OVERLAY_COLOR.description << "\n";
    out << IniOnly::Advanced::GRID_OVERLAY_MAJOR_COLOR.key << "=" << PluginUtils::formatColorHex(UiConfig::getInstance().getGridOverlayMajorColor()) << " ; " << IniOnly::Advanced::GRID_OVERLAY_MAJOR_COLOR.description << "\n";
    out << IniOnly::Advanced::CURSOR_ACTIVATION_THRESHOLD.key << "=" << UiConfig::getInstance().getCursorActivationThreshold() << " ; " << IniOnly::Advanced::CURSOR_ACTIVATION_THRESHOLD.description << "\n";
    out << IniOnly::Advanced::SEGMENT_SNAP_TO_SPLITS.key << "=" << (UiConfig::getInstance().getSnapSegmentsToSplits() ? 1 : 0) << " ; " << IniOnly::Advanced::SEGMENT_SNAP_TO_SPLITS.description << "\n";
    out << IniOnly::Advanced::SEGMENT_SNAP_THRESHOLD.key << "=" << UiConfig::getInstance().getSegmentSnapThreshold() << " ; " << IniOnly::Advanced::SEGMENT_SNAP_THRESHOLD.description << "\n";
    out << IniOnly::Advanced::HAZARD_STATIONARY_TOLERANCE.key << "=" << PluginData::getInstance().getHazardStationaryTolerance() << " ; " << IniOnly::Advanced::HAZARD_STATIONARY_TOLERANCE.description << "\n";
    out << IniOnly::Advanced::HAZARD_STATIONARY_DURATION_MS.key << "=" << PluginData::getInstance().getHazardStationaryDurationMs() << " ; " << IniOnly::Advanced::HAZARD_STATIONARY_DURATION_MS.description << "\n";
    out << IniOnly::Advanced::HAZARD_WRONG_WAY_DURATION_MS.key << "=" << PluginData::getInstance().getHazardWrongWayDurationMs() << " ; " << IniOnly::Advanced::HAZARD_WRONG_WAY_DURATION_MS.description << "\n";
    out << IniOnly::Advanced::HAZARD_AWARENESS_DISTANCE.key << "=" << PluginData::getInstance().getHazardAwarenessDistance() << " ; " << IniOnly::Advanced::HAZARD_AWARENESS_DISTANCE.description << "\n";
    out << IniOnly::Advanced::HAZARD_COOLDOWN_MS.key << "=" << PluginData::getInstance().getHazardCooldownMs() << " ; " << IniOnly::Advanced::HAZARD_COOLDOWN_MS.description << "\n";
    out << IniOnly::Advanced::HAZARD_GRACE_PERIOD_MS.key << "=" << PluginData::getInstance().getHazardGracePeriodMs() << " ; " << IniOnly::Advanced::HAZARD_GRACE_PERIOD_MS.description << "\n";
    out << IniOnly::Advanced::BLUE_FLAG_AWARENESS_DISTANCE.key << "=" << PluginData::getInstance().getBlueFlagAwarenessDistance() << " ; " << IniOnly::Advanced::BLUE_FLAG_AWARENESS_DISTANCE.description << "\n";
    out << IniOnly::Advanced::GAP_NOTIFY_INTERVAL_MS.key << "=" << PluginData::getInstance().getGapNotifyIntervalMs() << " ; " << IniOnly::Advanced::GAP_NOTIFY_INTERVAL_MS.description << "\n";
#if GAME_HAS_HTTP_SERVER
    out << IniOnly::Advanced::WEB_SERVER_PORT.key << "=" << HttpServer::getInstance().getPort() << " ; " << IniOnly::Advanced::WEB_SERVER_PORT.description << "\n";
    out << IniOnly::Advanced::WEB_SERVER_THROTTLE_MS.key << "=" << HttpServer::getInstance().getThrottleMs() << " ; " << IniOnly::Advanced::WEB_SERVER_THROTTLE_MS.description << "\n";
    out << IniOnly::Advanced::WEB_SERVER_BIND_ADDRESS.key << "=" << HttpServer::getInstance().getBindAddress() << " ; " << IniOnly::Advanced::WEB_SERVER_BIND_ADDRESS.description << "\n";
#endif
    out << "\n";

    // Write Display section (units, clock format, and display toggles; shown first
    // on the Appearance tab)
    out << "[Display]\n";
    out << "speedUnit=" << speedUnitToString(hudManager.getSpeedWidget().m_speedUnit) << "\n";
    out << "fuelUnit=" << fuelUnitToString(hudManager.getFuelWidget().m_fuelUnit) << "\n";
    out << "tempUnit=" << tempUnitToString(UiConfig::getInstance().getTemperatureUnit()) << "\n";
    out << "format24h=" << (hudManager.getClockWidget().getFormat24h() ? 1 : 0) << "\n";
    out << "shortTimeFormat=" << (PluginData::getInstance().isShortTimeFormat() ? 1 : 0) << "\n";
    out << "dropShadow=" << (UiConfig::getInstance().getDropShadow() ? 1 : 0) << "\n";
    out << "titleIcons=" << (UiConfig::getInstance().getTitleIcons() ? 1 : 0) << "\n";
    out << "gridSnapping=" << (UiConfig::getInstance().getGridSnapping() ? 1 : 0) << "\n";
    out << "screenClamping=" << (UiConfig::getInstance().getScreenClamping() ? 1 : 0) << "\n";
    out << "menuOnlyCursor=" << (UiConfig::getInstance().getMenuOnlyCursor() ? 1 : 0) << "\n";
    // Companion window geometry (full-window rect). Written before displayTarget so
    // it's restored before the window opens on load. w<=0 => open at default.
    {
        int gx, gy, gw, gh; CompanionWindow::getInstance().getSavedGeometry(gx, gy, gw, gh);
        out << "companionWindowX=" << gx << "\n";
        out << "companionWindowY=" << gy << "\n";
        out << "companionWindowW=" << gw << "\n";
        out << "companionWindowH=" << gh << "\n";
        out << "companionWindowMax=" << (CompanionWindow::getInstance().getSavedMaximized() ? 1 : 0) << "\n";
    }
    // INI-only: companion render cadence. 0 = V-Sync (match the monitor, tear-free),
    // N = fixed N Hz cap (lower to save CPU). No settings-menu control by design.
    out << "companionRefreshHz=" << CompanionWindow::getInstance().getRefreshHz() << "\n";
    out << "displayTarget=" << displayTargetToString(UiConfig::getInstance().getDisplayTarget()) << "\n\n";

    // Write Colors section
    const ColorConfig& colorConfig = ColorConfig::getInstance();
    out << "[Colors]\n";
    out << "primary=" << PluginUtils::formatColorHex(colorConfig.getPrimary()) << "\n";
    out << "secondary=" << PluginUtils::formatColorHex(colorConfig.getSecondary()) << "\n";
    out << "tertiary=" << PluginUtils::formatColorHex(colorConfig.getTertiary()) << "\n";
    out << "muted=" << PluginUtils::formatColorHex(colorConfig.getMuted()) << "\n";
    out << "background=" << PluginUtils::formatColorHex(colorConfig.getBackground()) << "\n";
    out << "positive=" << PluginUtils::formatColorHex(colorConfig.getPositive()) << "\n";
    out << "warning=" << PluginUtils::formatColorHex(colorConfig.getWarning()) << "\n";
    out << "neutral=" << PluginUtils::formatColorHex(colorConfig.getNeutral()) << "\n";
    out << "negative=" << PluginUtils::formatColorHex(colorConfig.getNegative()) << "\n";
    out << "accent=" << PluginUtils::formatColorHex(colorConfig.getAccent()) << "\n\n";

    // Write Fonts section
    const FontConfig& fontConfig = FontConfig::getInstance();
    out << "[Fonts]\n";
    out << "title=" << fontConfig.getFontName(FontCategory::TITLE) << "\n";
    out << "normal=" << fontConfig.getFontName(FontCategory::NORMAL) << "\n";
    out << "strong=" << fontConfig.getFontName(FontCategory::STRONG) << "\n";
    out << "digits=" << fontConfig.getFontName(FontCategory::DIGITS) << "\n";
    out << "marker=" << fontConfig.getFontName(FontCategory::MARKER) << "\n";
    out << "small=" << fontConfig.getFontName(FontCategory::SMALL) << "\n\n";

    // Write Rumble section (effect configuration)
    // Always save global config to INI (per-bike effects go to JSON)
    const RumbleConfig& rumbleConfig = XInputReader::getInstance().getGlobalRumbleConfig();
    out << "[Rumble]\n";
    out << "enabled=" << (rumbleConfig.enabled ? 1 : 0) << "\n";
    out << "additive_blend=" << (rumbleConfig.additiveBlend ? 1 : 0) << "\n";
    out << "rumble_when_crashed=" << (rumbleConfig.rumbleWhenCrashed ? 1 : 0) << "\n";
    out << "use_per_bike_effects=" << (rumbleConfig.usePerBikeEffects ? 1 : 0) << "\n";
    out << "send_interval_ms=" << XInputReader::getInstance().getRumbleSendIntervalMs()
        << " ; Min ms between rumble updates; raise to reduce Bluetooth traffic (4-200, default 10)\n";
    // Suspension effect (with optional front/rear split)
    out << "susp_min_input=" << rumbleConfig.suspensionEffect.minInput << "\n";
    out << "susp_max_input=" << rumbleConfig.suspensionEffect.maxInput << "\n";
    out << "susp_light_strength=" << rumbleConfig.suspensionEffect.lightStrength << "\n";
    out << "susp_heavy_strength=" << rumbleConfig.suspensionEffect.heavyStrength << "\n";
    out << "susp_split=" << (rumbleConfig.suspensionSplit ? 1 : 0) << "\n";
    out << "susp_split_init=" << (rumbleConfig.suspensionSplitInitialized ? 1 : 0) << "\n";
    out << "susp_front_min_input=" << rumbleConfig.suspensionEffectFront.minInput << "\n";
    out << "susp_front_max_input=" << rumbleConfig.suspensionEffectFront.maxInput << "\n";
    out << "susp_front_light_strength=" << rumbleConfig.suspensionEffectFront.lightStrength << "\n";
    out << "susp_front_heavy_strength=" << rumbleConfig.suspensionEffectFront.heavyStrength << "\n";
    out << "susp_rear_min_input=" << rumbleConfig.suspensionEffectRear.minInput << "\n";
    out << "susp_rear_max_input=" << rumbleConfig.suspensionEffectRear.maxInput << "\n";
    out << "susp_rear_light_strength=" << rumbleConfig.suspensionEffectRear.lightStrength << "\n";
    out << "susp_rear_heavy_strength=" << rumbleConfig.suspensionEffectRear.heavyStrength << "\n";
    // Wheelspin effect
    out << "wheel_min_input=" << rumbleConfig.wheelspinEffect.minInput << "\n";
    out << "wheel_max_input=" << rumbleConfig.wheelspinEffect.maxInput << "\n";
    out << "wheel_light_strength=" << rumbleConfig.wheelspinEffect.lightStrength << "\n";
    out << "wheel_heavy_strength=" << rumbleConfig.wheelspinEffect.heavyStrength << "\n";
    // Brake lockup effect (with optional front/rear split)
    out << "lockup_min_input=" << rumbleConfig.brakeLockupEffect.minInput << "\n";
    out << "lockup_max_input=" << rumbleConfig.brakeLockupEffect.maxInput << "\n";
    out << "lockup_light_strength=" << rumbleConfig.brakeLockupEffect.lightStrength << "\n";
    out << "lockup_heavy_strength=" << rumbleConfig.brakeLockupEffect.heavyStrength << "\n";
    out << "lockup_split=" << (rumbleConfig.brakeLockupSplit ? 1 : 0) << "\n";
    out << "lockup_split_init=" << (rumbleConfig.brakeLockupSplitInitialized ? 1 : 0) << "\n";
    out << "lockup_front_min_input=" << rumbleConfig.brakeLockupEffectFront.minInput << "\n";
    out << "lockup_front_max_input=" << rumbleConfig.brakeLockupEffectFront.maxInput << "\n";
    out << "lockup_front_light_strength=" << rumbleConfig.brakeLockupEffectFront.lightStrength << "\n";
    out << "lockup_front_heavy_strength=" << rumbleConfig.brakeLockupEffectFront.heavyStrength << "\n";
    out << "lockup_rear_min_input=" << rumbleConfig.brakeLockupEffectRear.minInput << "\n";
    out << "lockup_rear_max_input=" << rumbleConfig.brakeLockupEffectRear.maxInput << "\n";
    out << "lockup_rear_light_strength=" << rumbleConfig.brakeLockupEffectRear.lightStrength << "\n";
    out << "lockup_rear_heavy_strength=" << rumbleConfig.brakeLockupEffectRear.heavyStrength << "\n";
    // RPM effect
    out << "rpm_min_input=" << rumbleConfig.rpmEffect.minInput << "\n";
    out << "rpm_max_input=" << rumbleConfig.rpmEffect.maxInput << "\n";
    out << "rpm_light_strength=" << rumbleConfig.rpmEffect.lightStrength << "\n";
    out << "rpm_heavy_strength=" << rumbleConfig.rpmEffect.heavyStrength << "\n";
    // Slide effect
    out << "slide_min_input=" << rumbleConfig.slideEffect.minInput << "\n";
    out << "slide_max_input=" << rumbleConfig.slideEffect.maxInput << "\n";
    out << "slide_light_strength=" << rumbleConfig.slideEffect.lightStrength << "\n";
    out << "slide_heavy_strength=" << rumbleConfig.slideEffect.heavyStrength << "\n";
    // Surface effect
    out << "surface_min_input=" << rumbleConfig.surfaceEffect.minInput << "\n";
    out << "surface_max_input=" << rumbleConfig.surfaceEffect.maxInput << "\n";
    out << "surface_light_strength=" << rumbleConfig.surfaceEffect.lightStrength << "\n";
    out << "surface_heavy_strength=" << rumbleConfig.surfaceEffect.heavyStrength << "\n";
    // Steer effect
    out << "steer_min_input=" << rumbleConfig.steerEffect.minInput << "\n";
    out << "steer_max_input=" << rumbleConfig.steerEffect.maxInput << "\n";
    out << "steer_light_strength=" << rumbleConfig.steerEffect.lightStrength << "\n";
    out << "steer_heavy_strength=" << rumbleConfig.steerEffect.heavyStrength << "\n";
    // Wheelie effect
    out << "wheelie_min_input=" << rumbleConfig.wheelieEffect.minInput << "\n";
    out << "wheelie_max_input=" << rumbleConfig.wheelieEffect.maxInput << "\n";
    out << "wheelie_light_strength=" << rumbleConfig.wheelieEffect.lightStrength << "\n";
    out << "wheelie_heavy_strength=" << rumbleConfig.wheelieEffect.heavyStrength << "\n";
    // Rev limiter effect (Min/Max are percent of the bike's limiter RPM)
    out << "revlim_min_input=" << rumbleConfig.revLimiterEffect.minInput << "\n";
    out << "revlim_max_input=" << rumbleConfig.revLimiterEffect.maxInput << "\n";
    out << "revlim_light_strength=" << rumbleConfig.revLimiterEffect.lightStrength << "\n";
    out << "revlim_heavy_strength=" << rumbleConfig.revLimiterEffect.heavyStrength << "\n";
    // Pit limiter effect (binary input)
    out << "pitlim_min_input=" << rumbleConfig.pitLimiterEffect.minInput << "\n";
    out << "pitlim_max_input=" << rumbleConfig.pitLimiterEffect.maxInput << "\n";
    out << "pitlim_light_strength=" << rumbleConfig.pitLimiterEffect.lightStrength << "\n";
    out << "pitlim_heavy_strength=" << rumbleConfig.pitLimiterEffect.heavyStrength << "\n\n";

    // Write HelmetOverlay section (global, not per-profile)
    {
        const auto& hud = hudManager.getHelmetOverlayHud();
        out << "[HelmetOverlay]\n";
        out << "visible=" << (hud.isVisible() ? 1 : 0) << "\n";
        out << "helmetEnabled=" << (hud.m_helmetEnabled ? 1 : 0) << "\n";
        out << "visorMode=" << hud.m_visorMode << "\n";
        out << "helmetUpperVariant=" << hud.m_helmetUpperVariant << "\n";
        out << "helmetLowerVariant=" << hud.m_helmetLowerVariant << "\n";
        out << "helmetUpperOffsetY=" << hud.m_helmetUpperOffsetY << "\n";
        out << "helmetLowerOffsetY=" << hud.m_helmetLowerOffsetY << "\n";
        out << "helmetTiltStrength=" << hud.m_helmetTiltStrength << "\n";
        out << "helmetVibrationStrength=" << hud.m_helmetVibrationStrength << "\n";
        out << "helmetVibrationSensitivity=" << hud.m_helmetVibrationSensitivity << "\n";
        out << "helmetZoom=" << hud.m_helmetZoom << "\n";
        out << "visorTintColor=" << PluginUtils::formatColorHex(hud.m_visorTintColor) << "\n";
        out << "visorTintOpacity=" << hud.m_visorTintOpacity << "\n\n";
    }

    // Write Director section (global, not per-profile)
    {
        const DirectorManager& director = DirectorManager::getInstance();
        out << "[Director]\n";
        out << "enabled=" << (director.isEnabled() ? 1 : 0) << "\n";
        out << "minShotSec=" << director.getMinShotSec() << "\n";
        out << "maxShotSec=" << director.getMaxShotSec() << "\n";
        out << "battleGapMs=" << director.getBattleGapMs() << "\n";
        out << "battleMaxPos=" << director.getBattleMaxPos() << "\n";
        out << "manualResumeSec=" << director.getManualResumeSec() << "\n";
        out << "gamepadTakeover=" << (director.getGamepadTakeover() ? 1 : 0) << "\n";
        out << "camFront=" << (director.getCamFront() ? 1 : 0) << "\n";
        out << "camRear=" << (director.getCamRear() ? 1 : 0) << "\n";
        out << "camHelmet=" << (director.getCamHelmet() ? 1 : 0) << "\n";
        out << "camHelmet2=" << (director.getCamHelmet2() ? 1 : 0) << "\n";
        out << "camForks=" << (director.getCamForks() ? 1 : 0) << "\n";
        out << "followBattles=" << (director.getFollowBattles() ? 1 : 0) << "\n";
        out << "followIncidents=" << (director.getFollowIncidents() ? 1 : 0) << "\n";
        out << "followFastestLap=" << (director.getFollowFastestLap() ? 1 : 0) << "\n";
        out << "finishLock=" << (director.getFinishLock() ? 1 : 0) << "\n";
        out << "catchOvertakes=" << (director.getCatchOvertakes() ? 1 : 0) << "\n";
        out << "followLappers=" << (director.getFollowLappers() ? 1 : 0) << "\n";
        out << "followDrops=" << (director.getFollowDrops() ? 1 : 0) << "\n";
        out << "followPace=" << (director.getFollowPace() ? 1 : 0) << "\n";
        out << "varietyEvery=" << director.getVarietyEvery() << "\n";
        out << "holdSec=" << director.getHoldSec() << "\n";
        out << "incidentMaxSec=" << director.getIncidentMaxSec() << "\n";  // INI-only tunable (no GUI)
        // The status-button HUD is global too (like HelmetOverlay) - persist its base
        // settings here rather than in the per-profile HUD cache.
        if (const DirectorWidget* hud = hudManager.getDirectorWidget()) {
            out << "hudVisible=" << (hud->isVisible() ? 1 : 0) << "\n";
            out << "hudX=" << hud->getOffsetX() << "\n";
            out << "hudY=" << hud->getOffsetY() << "\n";
            out << "hudScale=" << hud->getScale() << "\n";
            out << "hudOpacity=" << hud->getBackgroundOpacity() << "\n";
        }
        out << "\n";
    }

#if GAME_HAS_RECORDER
    // Write Recorder section (global; hidden developer tool). Off by default;
    // a developer sets enabled=1 by hand-editing the INI to capture a callback
    // tape for the test harness. No HUD / no hotkey / no settings-menu control.
    out << "[Recorder]\n";
    out << "enabled=" << (EventRecorder::getInstance().isRecordingEnabled() ? 1 : 0)
        << " ; Dev-only: capture the raw callback stream to mxbmrp3\\tapes\\ for headless replay\n\n";
#endif

    // Write Hotkeys section. Keys are named per action (e.g. standings_key) so
    // the file is self-documenting; values are numeric codes: _key = Windows
    // virtual-key code, _mod = modifier bitmask (1=Ctrl, 2=Shift, 4=Alt),
    // _btn = controller button. 0 means unbound. Actions with no row in the
    // settings UI (e.g. rumble/helmet/performance) are still written here and
    // can be bound by hand-editing.
    const HotkeyManager& hotkeyMgr = HotkeyManager::getInstance();
    out << "[Hotkeys]\n";
    for (int i = 0; i < static_cast<int>(HotkeyAction::COUNT); ++i) {
        HotkeyAction action = static_cast<HotkeyAction>(i);
        const HotkeyBinding& binding = hotkeyMgr.getBinding(action);
        const char* name = getActionConfigName(action);

        out << name << "_key=" << static_cast<int>(binding.keyboard.keyCode) << "\n";
        out << name << "_mod=" << static_cast<int>(binding.keyboard.modifiers) << "\n";
        out << name << "_btn=" << static_cast<int>(binding.controller) << "\n";
    }
    out << "\n";

}

bool SettingsManager::applyGlobalLine(const std::string& section, const std::string& key,
                                      const std::string& value, HudManager& hudManager) {
    if (section == "General") {
        try {
            if (key == "autoSave") {
                UiConfig::getInstance().setAutoSave(std::stoi(value) != 0);
            }
            // Legacy read-only fallbacks: update settings relocated to [Updates]. Old INIs
            // still carry them under [General], so read them here to preserve values on
            // upgrade. Saving writes them only under [Updates], so they migrate on the next
            // save and these branches stop matching. (checkForUpdates is an even older alias.)
            else if (key == "updateMode") {
                // Supported modes: off, notify (auto is treated as notify for backward compatibility)
                if (value == "off") {
                    UpdateChecker::getInstance().setMode(UpdateChecker::UpdateMode::OFF);
                } else if (value == "notify" || value == "auto") {
                    UpdateChecker::getInstance().setMode(UpdateChecker::UpdateMode::NOTIFY);
                }
            } else if (key == "checkForUpdates") {
                UpdateChecker::getInstance().setEnabled(std::stoi(value) != 0);
            } else if (key == "updateChannel") {
                // Load channel before dismissedVersion (setChannel clears dismissedVersion on change)
                if (value == "prerelease") {
                    UpdateChecker::getInstance().setChannel(UpdateChecker::UpdateChannel::PRERELEASE);
                } else {
                    UpdateChecker::getInstance().setChannel(UpdateChecker::UpdateChannel::STABLE);
                }
            } else if (key == "dismissedVersion") {
                UpdateChecker::getInstance().setDismissedVersion(value);
            } else if (key == "controller") {
                int idx = std::stoi(value);
                XInputReader::getInstance().getRumbleConfig().controllerIndex = idx;
                XInputReader::getInstance().setControllerIndex(idx);
            } else if (key == "pbScope") {
                UiConfig::getInstance().setPBScope(stringToPBScope(value));
            }
#if GAME_HAS_RECORDS_PROVIDER
            else if (key == "recordsAutoFetch") {
                hudManager.getRecordsHud().m_bAutoFetch = (std::stoi(value) != 0);
            } else if (key == "recordsProvider") {
                hudManager.getRecordsHud().m_provider = stringToDataProvider(value);
            }
#endif
#if GAME_HAS_DISCORD
            else if (key == "discordRichPresence") {
                DiscordManager::getInstance().setEnabled(std::stoi(value) != 0);
            }
#endif
#if GAME_HAS_STEAM_FRIENDS
            else if (key == "steamFriends") {
                SteamFriendsManager::getInstance().setEnabled(std::stoi(value) != 0);
            }
#endif
#if GAME_HAS_ANALYTICS
            else if (key == "analytics") {
                AnalyticsManager::getInstance().setEnabled(std::stoi(value) != 0);
            }
#endif
            else if (key == "filterDnsRiders") {
                PluginData::getInstance().setFilterDnsRiders(std::stoi(value) != 0);
            }
#if GAME_HAS_HTTP_SERVER
            else if (key == "webServer") {
                HttpServer::getInstance().setEnabled(std::stoi(value) != 0);
            }
#endif
            // Legacy read-only fallbacks: these eight relocated to [Display]. Old INIs
            // still carry them under [General], so read them here to preserve values
            // on upgrade. Saving writes them only under [Display], so they migrate on
            // the next save and these branches stop matching.
            else if (key == "speedUnit") {
                hudManager.getSpeedWidget().m_speedUnit = stringToSpeedUnit(value);
            } else if (key == "fuelUnit") {
                hudManager.getFuelWidget().m_fuelUnit = stringToFuelUnit(value);
            } else if (key == "tempUnit") {
                UiConfig::getInstance().setTemperatureUnit(stringToTempUnit(value));
            } else if (key == "format24h") {
                hudManager.getClockWidget().setFormat24h(std::stoi(value) != 0);
            } else if (key == "shortTimeFormat") {
                PluginData::getInstance().setShortTimeFormat(std::stoi(value) != 0);
            } else if (key == "dropShadow") {
                UiConfig::getInstance().setDropShadow(std::stoi(value) != 0);
            } else if (key == "gridSnapping") {
                UiConfig::getInstance().setGridSnapping(std::stoi(value) != 0);
            } else if (key == "screenClamping") {
                UiConfig::getInstance().setScreenClamping(std::stoi(value) != 0);
            }
        } catch (const std::exception& e) {
            DEBUG_WARN_F("General: Failed to parse settings: %s", e.what());
        }
        return true;
    }

    // Handle Display section (speed/fuel/temp units + clock format; moved here
    // from [General] — shown first on the Appearance tab)
    if (section == "Display") {
        try {
            if (key == "speedUnit") {
                hudManager.getSpeedWidget().m_speedUnit = stringToSpeedUnit(value);
            } else if (key == "fuelUnit") {
                hudManager.getFuelWidget().m_fuelUnit = stringToFuelUnit(value);
            } else if (key == "tempUnit") {
                UiConfig::getInstance().setTemperatureUnit(stringToTempUnit(value));
            } else if (key == "format24h") {
                hudManager.getClockWidget().setFormat24h(std::stoi(value) != 0);
            } else if (key == "shortTimeFormat") {
                PluginData::getInstance().setShortTimeFormat(std::stoi(value) != 0);
            } else if (key == "dropShadow") {
                UiConfig::getInstance().setDropShadow(std::stoi(value) != 0);
            } else if (key == "titleIcons") {
                UiConfig::getInstance().setTitleIcons(std::stoi(value) != 0);
            } else if (key == "gridSnapping") {
                UiConfig::getInstance().setGridSnapping(std::stoi(value) != 0);
            } else if (key == "screenClamping") {
                UiConfig::getInstance().setScreenClamping(std::stoi(value) != 0);
            } else if (key == "menuOnlyCursor") {
                UiConfig::getInstance().setMenuOnlyCursor(std::stoi(value) != 0);
            } else if (key == "companionWindowX" || key == "companionWindowY" ||
                       key == "companionWindowW" || key == "companionWindowH") {
                // Restore one component of the saved window rect (read-modify-write;
                // the four keys arrive on separate lines, all before displayTarget).
                int gx, gy, gw, gh;
                CompanionWindow::getInstance().getSavedGeometry(gx, gy, gw, gh);
                int v = std::stoi(value);
                if (key == "companionWindowX") gx = v;
                else if (key == "companionWindowY") gy = v;
                else if (key == "companionWindowW") gw = v;
                else gh = v;
                CompanionWindow::getInstance().setSavedGeometry(gx, gy, gw, gh);
            } else if (key == "companionWindowMax") {
                CompanionWindow::getInstance().setSavedMaximized(std::stoi(value) != 0);
            } else if (key == "companionRefreshHz") {
                CompanionWindow::getInstance().setRefreshHz(std::stoi(value));
            } else if (key == "displayTarget") {
                DisplayTarget target = stringToDisplayTarget(value);
                UiConfig::getInstance().setDisplayTarget(target);
                // Open/close the companion window to match (in-game suppression is
                // read live in HudManager::draw).
                CompanionWindow::getInstance().setEnabled(target != DisplayTarget::IN_GAME);
            }
        } catch (const std::exception& e) {
            DEBUG_WARN_F("Display: Failed to parse settings: %s", e.what());
        }
        return true;
    }

    // Handle Updates section (auto-update settings; consolidated from [General]/[Advanced])
    if (section == "Updates") {
        try {
            if (key == "updateChannel") {
                // Apply channel before dismissedVersion: setChannel() clears the dismissed
                // version when the channel changes, so a same-channel dismissal stays intact.
                if (value == "prerelease") {
                    UpdateChecker::getInstance().setChannel(UpdateChecker::UpdateChannel::PRERELEASE);
                } else {
                    UpdateChecker::getInstance().setChannel(UpdateChecker::UpdateChannel::STABLE);
                }
            } else if (key == "updateMode") {
                // Supported modes: off, notify (legacy "auto" maps to notify)
                if (value == "off") {
                    UpdateChecker::getInstance().setMode(UpdateChecker::UpdateMode::OFF);
                } else if (value == "notify" || value == "auto") {
                    UpdateChecker::getInstance().setMode(UpdateChecker::UpdateMode::NOTIFY);
                }
            } else if (key == "updateDebugMode") {
                bool debugMode = (std::stoi(value) != 0);
                UpdateChecker::getInstance().setDebugMode(debugMode);
                UpdateDownloader::getInstance().setDebugMode(debugMode);
            } else if (key == "dismissedVersion") {
                UpdateChecker::getInstance().setDismissedVersion(value);
            } else if (key == "donationNudge") {
                UpdateDownloader::getInstance().setDonationNudgeEnabled(std::stoi(value) != 0);
            }
        } catch (const std::exception& e) {
            DEBUG_WARN_F("Updates: Failed to parse settings: %s", e.what());
        }
        return true;
    }

    // Handle Advanced section (power-user settings)
    if (section == "Advanced") {
        try {
            if (key == "developerMode") {
                m_developerMode = (std::stoi(value) != 0);
            }
            // Legacy read-only fallbacks: updateChannel/updateDebugMode relocated to [Updates].
            // Old INIs carry them under [Advanced]; read them so values survive the upgrade,
            // then they migrate to [Updates] on the next save.
            else if (key == "updateChannel") {
                if (value == "prerelease") {
                    UpdateChecker::getInstance().setChannel(UpdateChecker::UpdateChannel::PRERELEASE);
                } else {
                    UpdateChecker::getInstance().setChannel(UpdateChecker::UpdateChannel::STABLE);
                }
            } else if (key == "updateDebugMode") {
                bool debugMode = (std::stoi(value) != 0);
                UpdateChecker::getInstance().setDebugMode(debugMode);
                UpdateDownloader::getInstance().setDebugMode(debugMode);
            // Note: mapPixelSpacing moved to [MapHud]
            } else if (key == "speedoNeedleColor") {
                hudManager.getSpeedoWidget().setNeedleColor(PluginUtils::parseColorHex(value, hudManager.getSpeedoWidget().getNeedleColor()));
            } else if (key == "speedoShowOdometer") {
                hudManager.getSpeedoWidget().setShowOdometer(std::stoi(value) != 0);
            } else if (key == "speedoShowTripmeter") {
                hudManager.getSpeedoWidget().setShowTripmeter(std::stoi(value) != 0);
            } else if (key == "tachoNeedleColor") {
                hudManager.getTachoWidget().setNeedleColor(PluginUtils::parseColorHex(value, hudManager.getTachoWidget().getNeedleColor()));
            } else if (key == "leanArcFillColor") {
                hudManager.getLeanWidget().setArcFillColor(PluginUtils::parseColorHex(value, hudManager.getLeanWidget().getArcFillColor()));
            }
#if GAME_HAS_RECORDS_PROVIDER
            else if (key == "recordsShowFooter") {
                hudManager.getRecordsHud().m_bShowFooter = (std::stoi(value) != 0);
            }
#endif
            else if (key == "standingsTopPositions") {
                int topPos = std::stoi(value);
                // Clamp to valid range (0 to MAX_TOP_POSITIONS)
                topPos = std::max(0, std::min(topPos, static_cast<int>(StandingsHud::MAX_TOP_POSITIONS)));
                hudManager.getStandingsHud().m_topPositionsCount = topPos;
            } else if (key == "dropShadowOffsetX") {
                UiConfig::getInstance().setDropShadowOffsetX(parseFiniteFloat(value));
            } else if (key == "dropShadowOffsetY") {
                UiConfig::getInstance().setDropShadowOffsetY(parseFiniteFloat(value));
            } else if (key == "dropShadowColor") {
                UiConfig::getInstance().setDropShadowColor(PluginUtils::parseColorHex(value, UiConfig::getInstance().getDropShadowColor()));
            } else if (key == "holdRepeatFastMs") {
                UiConfig::getInstance().setHoldRepeatFastMs(std::stoi(value));
            } else if (key == "gridOverlay") {
                UiConfig::getInstance().setGridOverlay(std::stoi(value) != 0);
            } else if (key == "gridOverlayMajorEvery") {
                UiConfig::getInstance().setGridOverlayMajorEvery(std::stoi(value));
            } else if (key == "gridOverlayColor") {
                UiConfig::getInstance().setGridOverlayColor(PluginUtils::parseColorHex(value, UiConfig::getInstance().getGridOverlayColor()));
            } else if (key == "gridOverlayMajorColor") {
                UiConfig::getInstance().setGridOverlayMajorColor(PluginUtils::parseColorHex(value, UiConfig::getInstance().getGridOverlayMajorColor()));
            } else if (key == "cursorActivationThreshold") {
                UiConfig::getInstance().setCursorActivationThreshold(parseFiniteFloat(value));
            } else if (key == "segmentSnapToSplits") {
                UiConfig::getInstance().setSnapSegmentsToSplits(std::stoi(value) != 0);
            } else if (key == "segmentSnapThreshold") {
                UiConfig::getInstance().setSegmentSnapThreshold(parseFiniteFloat(value));
            } else if (key == "hazardStationaryTolerance") {
                PluginData::getInstance().setHazardStationaryTolerance(parseFiniteFloat(value));
            } else if (key == "hazardStationaryDurationMs") {
                PluginData::getInstance().setHazardStationaryDurationMs(std::stoi(value));
            } else if (key == "hazardWrongWayDurationMs") {
                PluginData::getInstance().setHazardWrongWayDurationMs(std::stoi(value));
            } else if (key == "hazardAwarenessDistance") {
                PluginData::getInstance().setHazardAwarenessDistance(parseFiniteFloat(value));
            } else if (key == "hazardCooldownMs") {
                PluginData::getInstance().setHazardCooldownMs(std::stoi(value));
            } else if (key == "hazardGracePeriodMs") {
                PluginData::getInstance().setHazardGracePeriodMs(std::stoi(value));
            } else if (key == "blueFlagAwarenessDistance") {
                PluginData::getInstance().setBlueFlagAwarenessDistance(parseFiniteFloat(value));
            } else if (key == "gapNotifyIntervalMs") {
                PluginData::getInstance().setGapNotifyIntervalMs(std::stoi(value));
            }
#if GAME_HAS_HTTP_SERVER
            else if (key == "webServerPort") {
                HttpServer::getInstance().setPort(std::stoi(value));
            } else if (key == "webServerThrottleMs") {
                HttpServer::getInstance().setThrottleMs(std::stoi(value));
            } else if (key == "webServerBindAddress") {
                HttpServer::getInstance().setBindAddress(value);
            }
#endif
        } catch (const std::exception& e) {
            DEBUG_WARN_F("Advanced: Failed to parse settings: %s", e.what());
        }
        return true;
    }

    // Handle Colors section
    if (section == "Colors") {
        ColorConfig& colorConfig = ColorConfig::getInstance();
        try {
            if (key == "primary") {
                colorConfig.setColor(ColorSlot::PRIMARY, PluginUtils::parseColorHex(value, colorConfig.getColor(ColorSlot::PRIMARY)));
            } else if (key == "secondary") {
                colorConfig.setColor(ColorSlot::SECONDARY, PluginUtils::parseColorHex(value, colorConfig.getColor(ColorSlot::SECONDARY)));
            } else if (key == "tertiary") {
                colorConfig.setColor(ColorSlot::TERTIARY, PluginUtils::parseColorHex(value, colorConfig.getColor(ColorSlot::TERTIARY)));
            } else if (key == "muted") {
                colorConfig.setColor(ColorSlot::MUTED, PluginUtils::parseColorHex(value, colorConfig.getColor(ColorSlot::MUTED)));
            } else if (key == "background") {
                colorConfig.setColor(ColorSlot::BACKGROUND, PluginUtils::parseColorHex(value, colorConfig.getColor(ColorSlot::BACKGROUND)));
            } else if (key == "positive") {
                colorConfig.setColor(ColorSlot::POSITIVE, PluginUtils::parseColorHex(value, colorConfig.getColor(ColorSlot::POSITIVE)));
            } else if (key == "warning") {
                colorConfig.setColor(ColorSlot::WARNING, PluginUtils::parseColorHex(value, colorConfig.getColor(ColorSlot::WARNING)));
            } else if (key == "neutral") {
                colorConfig.setColor(ColorSlot::NEUTRAL, PluginUtils::parseColorHex(value, colorConfig.getColor(ColorSlot::NEUTRAL)));
            } else if (key == "negative") {
                colorConfig.setColor(ColorSlot::NEGATIVE, PluginUtils::parseColorHex(value, colorConfig.getColor(ColorSlot::NEGATIVE)));
            } else if (key == "accent") {
                colorConfig.setColor(ColorSlot::ACCENT, PluginUtils::parseColorHex(value, colorConfig.getColor(ColorSlot::ACCENT)));
            }
        } catch (const std::exception& e) {
            DEBUG_WARN_F("Colors: Failed to parse settings: %s", e.what());
        }
        return true;
    }

    // Handle Fonts section
    if (section == "Fonts") {
        FontConfig& fontConfig = FontConfig::getInstance();
        if (key == "title") {
            fontConfig.setFont(FontCategory::TITLE, value);
        } else if (key == "normal") {
            fontConfig.setFont(FontCategory::NORMAL, value);
        } else if (key == "strong") {
            fontConfig.setFont(FontCategory::STRONG, value);
        } else if (key == "digits") {
            fontConfig.setFont(FontCategory::DIGITS, value);
        } else if (key == "marker") {
            fontConfig.setFont(FontCategory::MARKER, value);
        } else if (key == "small") {
            fontConfig.setFont(FontCategory::SMALL, value);
        }
        return true;
    }

    // Handle Rumble section (effect configuration)
    // Always load into global config (per-bike profiles loaded from JSON)
    if (section == "Rumble") {
        RumbleConfig& config = XInputReader::getInstance().getGlobalRumbleConfig();
        try {
            if (key == "enabled") {
                config.enabled = std::stoi(value) != 0;
            } else if (key == "additive_blend") {
                config.additiveBlend = std::stoi(value) != 0;
            } else if (key == "rumble_when_crashed") {
                config.rumbleWhenCrashed = std::stoi(value) != 0;
            } else if (key == "use_per_bike_effects" || key == "use_per_bike_profiles") {
                // Note: use_per_bike_profiles is backward compatible alias
                config.usePerBikeEffects = std::stoi(value) != 0;
            } else if (key == "send_interval_ms") {
                // Global (never per-bike): lives on XInputReader, not RumbleConfig
                XInputReader::getInstance().setRumbleSendIntervalMs(std::stoi(value));
            } else if (key == "disable_on_crash") {
                // Backward compatibility: invert the old setting
                config.rumbleWhenCrashed = std::stoi(value) == 0;
            }
            // Suspension effect - new format
            else if (key == "susp_min_input") {
                config.suspensionEffect.minInput = parseFiniteFloat(value);
            } else if (key == "susp_max_input") {
                config.suspensionEffect.maxInput = parseFiniteFloat(value);
            } else if (key == "susp_light_strength") {
                config.suspensionEffect.lightStrength = parseFiniteFloat(value);
            } else if (key == "susp_heavy_strength") {
                config.suspensionEffect.heavyStrength = parseFiniteFloat(value);
            } else if (key == "susp_split") {
                config.suspensionSplit = std::stoi(value) != 0;
            } else if (key == "susp_split_init") {
                config.suspensionSplitInitialized = std::stoi(value) != 0;
            } else if (key == "susp_front_min_input") {
                config.suspensionEffectFront.minInput = parseFiniteFloat(value);
            } else if (key == "susp_front_max_input") {
                config.suspensionEffectFront.maxInput = parseFiniteFloat(value);
            } else if (key == "susp_front_light_strength") {
                config.suspensionEffectFront.lightStrength = parseFiniteFloat(value);
            } else if (key == "susp_front_heavy_strength") {
                config.suspensionEffectFront.heavyStrength = parseFiniteFloat(value);
            } else if (key == "susp_rear_min_input") {
                config.suspensionEffectRear.minInput = parseFiniteFloat(value);
            } else if (key == "susp_rear_max_input") {
                config.suspensionEffectRear.maxInput = parseFiniteFloat(value);
            } else if (key == "susp_rear_light_strength") {
                config.suspensionEffectRear.lightStrength = parseFiniteFloat(value);
            } else if (key == "susp_rear_heavy_strength") {
                config.suspensionEffectRear.heavyStrength = parseFiniteFloat(value);
            }
            // Wheelspin effect
            else if (key == "wheel_min_input") {
                config.wheelspinEffect.minInput = parseFiniteFloat(value);
            } else if (key == "wheel_max_input") {
                config.wheelspinEffect.maxInput = parseFiniteFloat(value);
            } else if (key == "wheel_light_strength") {
                config.wheelspinEffect.lightStrength = parseFiniteFloat(value);
            } else if (key == "wheel_heavy_strength") {
                config.wheelspinEffect.heavyStrength = parseFiniteFloat(value);
            }
            // Brake lockup effect
            else if (key == "lockup_min_input") {
                config.brakeLockupEffect.minInput = parseFiniteFloat(value);
            } else if (key == "lockup_max_input") {
                config.brakeLockupEffect.maxInput = parseFiniteFloat(value);
            } else if (key == "lockup_light_strength") {
                config.brakeLockupEffect.lightStrength = parseFiniteFloat(value);
            } else if (key == "lockup_heavy_strength") {
                config.brakeLockupEffect.heavyStrength = parseFiniteFloat(value);
            } else if (key == "lockup_split") {
                config.brakeLockupSplit = std::stoi(value) != 0;
            } else if (key == "lockup_split_init") {
                config.brakeLockupSplitInitialized = std::stoi(value) != 0;
            } else if (key == "lockup_front_min_input") {
                config.brakeLockupEffectFront.minInput = parseFiniteFloat(value);
            } else if (key == "lockup_front_max_input") {
                config.brakeLockupEffectFront.maxInput = parseFiniteFloat(value);
            } else if (key == "lockup_front_light_strength") {
                config.brakeLockupEffectFront.lightStrength = parseFiniteFloat(value);
            } else if (key == "lockup_front_heavy_strength") {
                config.brakeLockupEffectFront.heavyStrength = parseFiniteFloat(value);
            } else if (key == "lockup_rear_min_input") {
                config.brakeLockupEffectRear.minInput = parseFiniteFloat(value);
            } else if (key == "lockup_rear_max_input") {
                config.brakeLockupEffectRear.maxInput = parseFiniteFloat(value);
            } else if (key == "lockup_rear_light_strength") {
                config.brakeLockupEffectRear.lightStrength = parseFiniteFloat(value);
            } else if (key == "lockup_rear_heavy_strength") {
                config.brakeLockupEffectRear.heavyStrength = parseFiniteFloat(value);
            }
            // RPM effect
            else if (key == "rpm_min_input") {
                config.rpmEffect.minInput = parseFiniteFloat(value);
            } else if (key == "rpm_max_input") {
                config.rpmEffect.maxInput = parseFiniteFloat(value);
            } else if (key == "rpm_light_strength") {
                config.rpmEffect.lightStrength = parseFiniteFloat(value);
            } else if (key == "rpm_heavy_strength") {
                config.rpmEffect.heavyStrength = parseFiniteFloat(value);
            }
            // Slide effect
            else if (key == "slide_min_input") {
                config.slideEffect.minInput = parseFiniteFloat(value);
            } else if (key == "slide_max_input") {
                config.slideEffect.maxInput = parseFiniteFloat(value);
            } else if (key == "slide_light_strength") {
                config.slideEffect.lightStrength = parseFiniteFloat(value);
            } else if (key == "slide_heavy_strength") {
                config.slideEffect.heavyStrength = parseFiniteFloat(value);
            }
            // Surface effect
            else if (key == "surface_min_input") {
                config.surfaceEffect.minInput = parseFiniteFloat(value);
            } else if (key == "surface_max_input") {
                config.surfaceEffect.maxInput = parseFiniteFloat(value);
            } else if (key == "surface_light_strength") {
                config.surfaceEffect.lightStrength = parseFiniteFloat(value);
            } else if (key == "surface_heavy_strength") {
                config.surfaceEffect.heavyStrength = parseFiniteFloat(value);
            }
            // Steer effect
            else if (key == "steer_min_input") {
                config.steerEffect.minInput = parseFiniteFloat(value);
            } else if (key == "steer_max_input") {
                config.steerEffect.maxInput = parseFiniteFloat(value);
            } else if (key == "steer_light_strength") {
                config.steerEffect.lightStrength = parseFiniteFloat(value);
            } else if (key == "steer_heavy_strength") {
                config.steerEffect.heavyStrength = parseFiniteFloat(value);
            }
            // Wheelie effect
            else if (key == "wheelie_min_input") {
                config.wheelieEffect.minInput = parseFiniteFloat(value);
            } else if (key == "wheelie_max_input") {
                config.wheelieEffect.maxInput = parseFiniteFloat(value);
            } else if (key == "wheelie_light_strength") {
                config.wheelieEffect.lightStrength = parseFiniteFloat(value);
            } else if (key == "wheelie_heavy_strength") {
                config.wheelieEffect.heavyStrength = parseFiniteFloat(value);
            }
            // Rev limiter effect
            else if (key == "revlim_min_input") {
                config.revLimiterEffect.minInput = parseFiniteFloat(value);
            } else if (key == "revlim_max_input") {
                config.revLimiterEffect.maxInput = parseFiniteFloat(value);
            } else if (key == "revlim_light_strength") {
                config.revLimiterEffect.lightStrength = parseFiniteFloat(value);
            } else if (key == "revlim_heavy_strength") {
                config.revLimiterEffect.heavyStrength = parseFiniteFloat(value);
            }
            // Pit limiter effect
            else if (key == "pitlim_min_input") {
                config.pitLimiterEffect.minInput = parseFiniteFloat(value);
            } else if (key == "pitlim_max_input") {
                config.pitLimiterEffect.maxInput = parseFiniteFloat(value);
            } else if (key == "pitlim_light_strength") {
                config.pitLimiterEffect.lightStrength = parseFiniteFloat(value);
            } else if (key == "pitlim_heavy_strength") {
                config.pitLimiterEffect.heavyStrength = parseFiniteFloat(value);
            }
        } catch (const std::exception& e) {
            DEBUG_WARN_F("Rumble: Failed to parse settings: %s", e.what());
        }
        return true;
    }

    // Handle HelmetOverlay section (global, not per-profile)
    if (section == "HelmetOverlay") {
        auto& hud = hudManager.getHelmetOverlayHud();
        try {
            if (key == "visible") {
                hud.setVisible(std::stoi(value) != 0);
            } else if (key == "helmetEnabled") {
                hud.m_helmetEnabled = std::stoi(value) != 0;
            } else if (key == "visorMode") {
                hud.m_visorMode = std::clamp(std::stoi(value), 0, HelmetOverlayHud::VISOR_MODE_COUNT - 1);
            } else if (key == "helmetUpperVariant") {
                hud.m_helmetUpperVariant = std::stoi(value);
            } else if (key == "helmetLowerVariant") {
                hud.m_helmetLowerVariant = std::stoi(value);
            } else if (key == "helmetUpperOffsetY") {
                hud.m_helmetUpperOffsetY = parseFiniteFloat(value);
            } else if (key == "helmetLowerOffsetY") {
                hud.m_helmetLowerOffsetY = parseFiniteFloat(value);
            } else if (key == "helmetTiltStrength") {
                hud.m_helmetTiltStrength = parseFiniteFloat(value);
            } else if (key == "helmetVibrationStrength") {
                hud.m_helmetVibrationStrength = parseFiniteFloat(value);
            } else if (key == "helmetVibrationSensitivity") {
                hud.m_helmetVibrationSensitivity = parseFiniteFloat(value);
            } else if (key == "helmetZoom") {
                hud.m_helmetZoom = parseFiniteFloat(value);
            } else if (key == "visorTintColor") {
                hud.m_visorTintColor = PluginUtils::parseColorHex(value, hud.m_visorTintColor);
            } else if (key == "visorTintOpacity") {
                hud.m_visorTintOpacity = parseFiniteFloat(value);
            }
        } catch (const std::exception& e) {
            DEBUG_WARN_F("HelmetOverlay: Failed to parse setting '%s': %s", key.c_str(), e.what());
        }
        return true;
    }

    // Handle Director section (global, not per-profile)
    if (section == "Director") {
        DirectorManager& director = DirectorManager::getInstance();
        try {
            if (key == "enabled") {
                director.setEnabled(std::stoi(value) != 0);
            } else if (key == "minShotSec") {
                director.setMinShotSec(std::stoi(value));
            } else if (key == "maxShotSec") {
                director.setMaxShotSec(std::stoi(value));
            } else if (key == "battleGapMs") {
                director.setBattleGapMs(std::stoi(value));
            } else if (key == "battleMaxPos") {
                director.setBattleMaxPos(std::stoi(value));
            } else if (key == "manualResumeSec") {
                director.setManualResumeSec(std::stoi(value));
            } else if (key == "gamepadTakeover") {
                director.setGamepadTakeover(std::stoi(value) != 0);
            } else if (key == "camFront") {
                director.setCamFront(std::stoi(value) != 0);
            } else if (key == "camRear") {
                director.setCamRear(std::stoi(value) != 0);
            } else if (key == "camHelmet") {
                director.setCamHelmet(std::stoi(value) != 0);
            } else if (key == "camHelmet2") {
                director.setCamHelmet2(std::stoi(value) != 0);
            } else if (key == "camForks") {
                director.setCamForks(std::stoi(value) != 0);
            } else if (key == "followBattles") {
                director.setFollowBattles(std::stoi(value) != 0);
            } else if (key == "followIncidents") {
                director.setFollowIncidents(std::stoi(value) != 0);
            } else if (key == "followFastestLap") {
                director.setFollowFastestLap(std::stoi(value) != 0);
            } else if (key == "finishLock") {
                director.setFinishLock(std::stoi(value) != 0);
            } else if (key == "catchOvertakes") {
                director.setCatchOvertakes(std::stoi(value) != 0);
            } else if (key == "followLappers") {
                director.setFollowLappers(std::stoi(value) != 0);
            } else if (key == "followDrops") {
                director.setFollowDrops(std::stoi(value) != 0);
            } else if (key == "followPace") {
                director.setFollowPace(std::stoi(value) != 0);
            } else if (key == "varietyEvery") {
                director.setVarietyEvery(std::stoi(value));
            } else if (key == "holdSec" || key == "incidentLingerSec") {
                // incidentLingerSec is the legacy key for the (now shared) hold.
                director.setHoldSec(std::stoi(value));
            } else if (key == "incidentMaxSec") {
                director.setIncidentMaxSec(std::stoi(value));
            } else if (key == "hudVisible") {
                if (auto* h = hudManager.getDirectorWidget()) h->setVisible(std::stoi(value) != 0);
            } else if (key == "hudX") {
                // isfinite-guard persisted floats: a garbage/Inf/NaN value in the INI
                // must not reach setPosition/setScale (invariant, like finiteOrZero in
                // stats_manager). If it isn't finite, keep the constructor default.
                // NOTE: use std::stof here, NOT parseFiniteFloat — parseFiniteFloat maps
                // non-finite to 0.0f (which passes the isfinite check below and would set
                // 0.0 instead of keeping the default). This site owns its own guard.
                float v = std::stof(value);
                if (std::isfinite(v)) { if (auto* h = hudManager.getDirectorWidget()) h->setPosition(v, h->getOffsetY()); }
            } else if (key == "hudY") {
                float v = std::stof(value);
                if (std::isfinite(v)) { if (auto* h = hudManager.getDirectorWidget()) h->setPosition(h->getOffsetX(), v); }
            } else if (key == "hudScale") {
                float v = std::stof(value);
                if (std::isfinite(v)) { if (auto* h = hudManager.getDirectorWidget()) h->setScale(v); }
            } else if (key == "hudOpacity") {
                float v = std::stof(value);
                if (std::isfinite(v)) { if (auto* h = hudManager.getDirectorWidget()) h->setBackgroundOpacity(v); }
            }
        } catch (const std::exception& e) {
            DEBUG_WARN_F("Director: Failed to parse setting '%s': %s", key.c_str(), e.what());
        }
        return true;
    }

#if GAME_HAS_RECORDER
    // Handle Recorder section (hidden dev tool). Only reads the enabled flag;
    // the actual session tape is opened at startup if enabled (see plugin_manager).
    if (section == "Recorder") {
        try {
            if (key == "enabled") {
                EventRecorder::getInstance().setRecordingEnabled(std::stoi(value) != 0);
            }
        } catch (const std::exception& e) {
            DEBUG_WARN_F("Recorder: Failed to parse setting '%s': %s", key.c_str(), e.what());
        }
        return true;
    }
#endif

    // Handle Hotkeys section
    if (section == "Hotkeys") {
        HotkeyManager& hotkeyMgr = HotkeyManager::getInstance();
        try {
            // Split at the LAST underscore: <name>_<suffix> (suffix = key/mod/btn).
            // Names can contain underscores (e.g. "lap_log", "overlay_last_lap").
            size_t lastUnderscore = key.rfind('_');
            if (lastUnderscore == std::string::npos) return true;
            std::string name = key.substr(0, lastUnderscore);
            std::string suffix = key.substr(lastUnderscore + 1);

            // Resolve the action. New keys are name-based ("standings_key"); the
            // old index-based form ("action0_key") is still accepted so existing
            // configs migrate automatically - read here, written back in the new
            // form on the next save.
            HotkeyAction action = HotkeyAction::COUNT;
            if (name.length() > 6 && name.substr(0, 6) == "action") {
                int idx = std::stoi(name.substr(6));
                if (idx >= 0 && idx < static_cast<int>(HotkeyAction::COUNT)) {
                    action = static_cast<HotkeyAction>(idx);
                }
            } else {
                for (int i = 0; i < static_cast<int>(HotkeyAction::COUNT); ++i) {
                    if (name == getActionConfigName(static_cast<HotkeyAction>(i))) {
                        action = static_cast<HotkeyAction>(i);
                        break;
                    }
                }
            }

            if (action != HotkeyAction::COUNT) {
                HotkeyBinding binding = hotkeyMgr.getBinding(action);
                if (suffix == "key") {
                    binding.keyboard.keyCode = static_cast<uint8_t>(std::stoi(value));
                } else if (suffix == "mod") {
                    binding.keyboard.modifiers = static_cast<ModifierFlags>(std::stoi(value));
                } else if (suffix == "btn") {
                    binding.controller = static_cast<ControllerButton>(std::stoi(value));
                }
                hotkeyMgr.setBinding(action, binding);
            }
        } catch (const std::exception& e) {
            DEBUG_WARN_F("Hotkeys: Failed to parse settings: %s", e.what());
        }
        return true;
    }

    return false;
}
