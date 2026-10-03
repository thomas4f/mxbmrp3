// ============================================================================
// core/settings_manager_global.cpp
// Global (non-per-profile) settings serialization for SettingsManager:
// globalSectionRegistry() (one row per global INI section: its writer and its
// applier), the writeGlobalSettings() / applyGlobalLine() dispatchers over it, and
// the [General], [Updates], [Advanced], [Display], [Colors], [Fonts] and
// [Fingerprint] pairs. The subsystem sections' pairs ([Rumble], [HelmetOverlay],
// [Spotter], [Director], [Achievements], [Recorder], [Hotkeys]) live in
// settings_manager_global_features.cpp, the stream-chat ones in
// settings_manager_stream_chat.cpp. settings_manager.cpp owns per-HUD
// capture/apply/serialize and load.
// ============================================================================
#include "settings_manager.h"
#include "layout_config.h"
#include "settings_keys.h"
#include "../hud/settings/whats_new.h"
#include "settings_serde_hud.h"
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
// — gated, the GPB/KRP builds fail (SettingsHud left incomplete -> C2027). The
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
#include "../hud/achievement_widget.h"
#include "achievement_manager.h"
#include "../hud/event_log_hud.h"
#include "fmx_manager.h"
#include "color_config.h"
#include "font_config.h"
#include "ui_config.h"
#include "update_checker.h"
#include "update_downloader.h"
#include "spotter_manager.h"
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
#include "install_prefs.h"
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
#include <cstdio>
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

// The one ordered table of global sections: each row carries its section's writer
// and applier, so adding a section is one row and a section cannot be written
// without being read back (or the reverse). Row order is the order
// writeGlobalSettings() emits the sections in. A null writer is a section this
// file only reads: [Fingerprint] is the trailer exploration_stats.cpp appends
// after everything else.
const std::vector<SettingsManager::GlobalSectionSerializer>& SettingsManager::globalSectionRegistry() {
    static const std::vector<GlobalSectionSerializer> registry = {
        {"General",       &SettingsManager::writeGeneralSettings,       &SettingsManager::applyGeneralLine},
        {"Updates",       &SettingsManager::writeUpdatesSettings,       &SettingsManager::applyUpdatesLine},
        {"Advanced",      &SettingsManager::writeAdvancedSettings,      &SettingsManager::applyAdvancedLine},
        {"Display",       &SettingsManager::writeDisplaySettings,       &SettingsManager::applyDisplayLine},
        {"Colors",        &SettingsManager::writeColorsSettings,        &SettingsManager::applyColorsLine},
        {"Fonts",         &SettingsManager::writeFontsSettings,         &SettingsManager::applyFontsLine},
        {"Rumble",        &SettingsManager::writeRumbleSettings,        &SettingsManager::applyRumbleLine},
        {"HelmetOverlay", &SettingsManager::writeHelmetOverlaySettings, &SettingsManager::applyHelmetOverlayLine},
        {"Spotter",       &SettingsManager::writeSpotterSettings,       &SettingsManager::applySpotterLine},
        {"Director",      &SettingsManager::writeDirectorSettings,      &SettingsManager::applyDirectorLine},
        {"Achievements",  &SettingsManager::writeAchievementsSettings,  &SettingsManager::applyAchievementsLine},
        {"StreamChat",    &SettingsManager::writeStreamChatSettings,    &SettingsManager::applyStreamChatLine},
        {"Twitch",        &SettingsManager::writeTwitchSettings,        &SettingsManager::applyTwitchLine},
        {"YouTube",       &SettingsManager::writeYouTubeSettings,       &SettingsManager::applyYouTubeLine},
#if GAME_HAS_RECORDER
        {"Recorder",      &SettingsManager::writeRecorderSettings,      &SettingsManager::applyRecorderLine},
#endif
        {"Hotkeys",       &SettingsManager::writeHotkeysSettings,       &SettingsManager::applyHotkeysLine},
        {"Fingerprint",   nullptr,                                      &SettingsManager::applyFingerprintLine},
    };
    return registry;
}

void SettingsManager::writeGlobalSettings(std::ostream& out, const HudManager& hudManager) const {
    for (const GlobalSectionSerializer& s : globalSectionRegistry()) {
        if (s.write) (this->*s.write)(out, hudManager);
    }
}

bool SettingsManager::applyGlobalLine(const std::string& section, const std::string& key,
                                      const std::string& value, HudManager& hudManager) {
    for (const GlobalSectionSerializer& s : globalSectionRegistry()) {
        if (section == s.name) {
            (this->*s.apply)(key, value, hudManager);
            return true;
        }
    }
    return false;
}

// Write General section (global preferences)
void SettingsManager::writeGeneralSettings(std::ostream& out, [[maybe_unused]] const HudManager& hudManager) const {
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
    // WHICH "New" MARKERS THE PLAYER HAS ALREADY SEEN -- see hud/settings/whats_new.h.
    // Written even when empty so the key is visible to anyone reading the file, and
    // so clearing it by hand is an obvious way to see the markers again.
    out << "whatsNewSeen=" << WhatsNew::serialize() << " ; dismissed 'New' markers\n";
#if GAME_HAS_ANALYTICS
    // Which Setup analytics opt-out has already been honoured (core/install_prefs.h).
    // Written only once one HAS been, so the key stays invisible for the overwhelming
    // majority of installs that never saw a marker.
    if (!m_installPrefsSeen.empty()) {
        out << "installPrefsSeen=" << m_installPrefsSeen
            << " ; Setup opt-out already applied; delete to re-apply\n";
    }
#endif
#if GAME_HAS_HTTP_SERVER
    out << "webServer=" << (HttpServer::getInstance().isEnabled() ? 1 : 0) << " ; Web overlay server (port and throttle in [Advanced])\n";
#endif
    out << "\n";
}

void SettingsManager::applyGeneralLine(const std::string& key, const std::string& value, HudManager& hudManager) {
    try {
        if (key == "whatsNewSeen") {
            WhatsNew::deserialize(value);
            return;
        }
#if GAME_HAS_ANALYTICS
        if (key == "installPrefsSeen") {
            m_installPrefsSeen = value;
            return;
        }
#endif
        if (key == "autoSave") {
            UiConfig::getInstance().setAutoSave(std::stoi(value) != 0);
        }
        // Legacy read-only fallbacks for the update settings that live in [Updates]:
        // an old INI carries them under [General], so read them here to preserve
        // values on upgrade. Saving writes them only under [Updates], so they migrate
        // on the next save and these branches stop matching. (checkForUpdates is an
        // older alias.)
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
        } else if (key == "updateTagSeen") {
            UpdateChecker::getInstance().setUpdateTagSeenVersion(value);
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
        // Legacy read-only fallbacks for these eight [Display] keys: an old INI
        // carries them under [General], so read them here to preserve values on
        // upgrade. Saving writes them only under [Display], so they migrate on the
        // next save and these branches stop matching.
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
}

// Write Updates section (auto-update settings, owned solely by the Updates tab, so
// the tab maps 1:1 to one INI section and resets via section replay).
// updateChannel is written before dismissedVersion because
// setChannel() clears the dismissed version when the channel changes — applying it
// first keeps a same-channel dismissal intact on load.
void SettingsManager::writeUpdatesSettings(std::ostream& out, const HudManager& /*hudManager*/) const {
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
    // The sidebar tag's own seen-version, deliberately separate from the skip
    // above (see UpdateChecker::shouldShowUpdateTag). Written unconditionally
    // for the same reason that one is.
    out << "updateTagSeen=" << UpdateChecker::getInstance().getUpdateTagSeenVersion() << "\n";
    out << "\n";
}

// Handle Updates section (auto-update settings)
void SettingsManager::applyUpdatesLine(const std::string& key, const std::string& value, HudManager& /*hudManager*/) {
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
        } else if (key == "updateTagSeen") {
            UpdateChecker::getInstance().setUpdateTagSeenVersion(value);
        }
    } catch (const std::exception& e) {
        DEBUG_WARN_F("Updates: Failed to parse settings: %s", e.what());
    }
}

// Write Advanced section (power-user settings)
void SettingsManager::writeAdvancedSettings(std::ostream& out, const HudManager& /*hudManager*/) const {
    out << "[Advanced]\n";
    out << IniOnly::Advanced::DEVELOPER_MODE.key << "=" << (m_developerMode ? 1 : 0) << " ; " << IniOnly::Advanced::DEVELOPER_MODE.description << "\n";
    out << IniOnly::Advanced::UI_FONT_SIZE.key << "=" << layoutDefaults().fontSizeNormal << " ; " << IniOnly::Advanced::UI_FONT_SIZE.description << "\n";
    out << IniOnly::Advanced::UI_LINE_HEIGHT.key << "=" << layoutDefaults().lineHeightRatio << " ; " << IniOnly::Advanced::UI_LINE_HEIGHT.description << "\n";
    // The box model's air-term built-ins, written in the most compact shorthand
    // that parses back to the same sides (PanelBox::formatSides).
    out << IniOnly::Advanced::BOX_PANEL_PADDING.key << "=" << PanelBox::formatSides(layoutDefaults().boxPanelPadding) << " ; " << IniOnly::Advanced::BOX_PANEL_PADDING.description << "\n";
    out << IniOnly::Advanced::BOX_TITLE_MARGIN.key << "=" << PanelBox::formatSides(layoutDefaults().boxTitleMargin) << " ; " << IniOnly::Advanced::BOX_TITLE_MARGIN.description << "\n";
    out << IniOnly::Advanced::BOX_TITLE_PADDING.key << "=" << PanelBox::formatSides(layoutDefaults().boxTitlePadding) << " ; " << IniOnly::Advanced::BOX_TITLE_PADDING.description << "\n";
    out << IniOnly::Advanced::BOX_CONTENT_MARGIN.key << "=" << PanelBox::formatSides(layoutDefaults().boxContentMargin) << " ; " << IniOnly::Advanced::BOX_CONTENT_MARGIN.description << "\n";
    out << IniOnly::Advanced::BOX_CONTENT_PADDING.key << "=" << PanelBox::formatSides(layoutDefaults().boxContentPadding) << " ; " << IniOnly::Advanced::BOX_CONTENT_PADDING.description << "\n";
    out << IniOnly::Advanced::BOX_BUTTON_MARGIN.key << "=" << PanelBox::formatSides(layoutDefaults().boxButtonMargin) << " ; " << IniOnly::Advanced::BOX_BUTTON_MARGIN.description << "\n";
    out << IniOnly::Advanced::BOX_BUTTON_PADDING.key << "=" << PanelBox::formatSides(layoutDefaults().boxButtonPadding) << " ; " << IniOnly::Advanced::BOX_BUTTON_PADDING.description << "\n";
    out << IniOnly::Advanced::BOX_PANEL_GAP.key << "=" << PanelBox::formatSides({layoutDefaults().boxPanelGap, layoutDefaults().boxPanelGap, layoutDefaults().boxPanelGap, layoutDefaults().boxPanelGap}) << " ; " << IniOnly::Advanced::BOX_PANEL_GAP.description << "\n";
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
    // Bind through a CONST reference so these pure reads resolve to the const
    // proximityTuning() overload. getInstance() hands back a non-const PluginData,
    // so writing it inline picks the mutable overload for what is only a read —
    // harmless, but it hands a writable handle to the clamped fields to code that
    // has no business writing them. Setting always goes through the clamped
    // setters (see applyAdvancedLine below and proximity_tuning.h).
    const ProximityTuning& prox = static_cast<const PluginData&>(PluginData::getInstance()).proximityTuning();
    out << IniOnly::Advanced::HAZARD_STATIONARY_TOLERANCE.key << "=" << prox.hazardStationaryTolerance << " ; " << IniOnly::Advanced::HAZARD_STATIONARY_TOLERANCE.description << "\n";
    out << IniOnly::Advanced::HAZARD_STATIONARY_DURATION_MS.key << "=" << prox.hazardStationaryDurationMs << " ; " << IniOnly::Advanced::HAZARD_STATIONARY_DURATION_MS.description << "\n";
    out << IniOnly::Advanced::HAZARD_WRONG_WAY_DURATION_MS.key << "=" << prox.hazardWrongWayDurationMs << " ; " << IniOnly::Advanced::HAZARD_WRONG_WAY_DURATION_MS.description << "\n";
    out << IniOnly::Advanced::HAZARD_AWARENESS_DISTANCE.key << "=" << prox.hazardAwarenessDistance << " ; " << IniOnly::Advanced::HAZARD_AWARENESS_DISTANCE.description << "\n";
    out << IniOnly::Advanced::HAZARD_WRONG_WAY_AWARENESS_DISTANCE.key << "=" << prox.hazardWrongWayAwarenessDistance << " ; " << IniOnly::Advanced::HAZARD_WRONG_WAY_AWARENESS_DISTANCE.description << "\n";
    out << IniOnly::Advanced::HAZARD_COOLDOWN_MS.key << "=" << prox.hazardCooldownMs << " ; " << IniOnly::Advanced::HAZARD_COOLDOWN_MS.description << "\n";
    out << IniOnly::Advanced::HAZARD_GRACE_PERIOD_MS.key << "=" << prox.hazardGracePeriodMs << " ; " << IniOnly::Advanced::HAZARD_GRACE_PERIOD_MS.description << "\n";
    out << IniOnly::Advanced::BLUE_FLAG_AWARENESS_DISTANCE.key << "=" << prox.blueFlagAwarenessDistance << " ; " << IniOnly::Advanced::BLUE_FLAG_AWARENESS_DISTANCE.description << "\n";
    out << IniOnly::Advanced::GAP_NOTIFY_INTERVAL_MS.key << "=" << PluginData::getInstance().getGapNotifyIntervalMs() << " ; " << IniOnly::Advanced::GAP_NOTIFY_INTERVAL_MS.description << "\n";
    out << IniOnly::Advanced::PLUGIN_THREAD.key << "=" << (UiConfig::getInstance().getPluginThread() ? 1 : 0) << " ; " << IniOnly::Advanced::PLUGIN_THREAD.description << "\n";
    // overlayInGame and overlayRefreshHz are retired keys: ACCEPTED and ignored on
    // load (see applyAdvancedLine) so an existing INI does not error, and never
    // written back.
    out << IniOnly::Advanced::HW_ACCEL.key << "=" << (CompanionWindow::getInstance().getHwAccel() ? 1 : 0) << " ; " << IniOnly::Advanced::HW_ACCEL.description << "\n";
    out << IniOnly::Advanced::GL_IN_GAME.key << "=" << (UiConfig::getInstance().getGlInGame() ? 1 : 0) << " ; " << IniOnly::Advanced::GL_IN_GAME.description << "\n";
    out << IniOnly::Advanced::GL_PROBE.key << "=" << UiConfig::getInstance().getGlProbe() << " ; " << IniOnly::Advanced::GL_PROBE.description << "\n";
    out << IniOnly::Advanced::GL_PROBE_X.key << "=" << UiConfig::getInstance().getGlProbeX() << " ; " << IniOnly::Advanced::GL_PROBE_X.description << "\n";
    out << IniOnly::Advanced::GL_PROBE_Y.key << "=" << UiConfig::getInstance().getGlProbeY() << " ; " << IniOnly::Advanced::GL_PROBE_Y.description << "\n";
    out << IniOnly::Advanced::GL_PROBE_QUADS.key << "=" << UiConfig::getInstance().getGlProbeQuads() << " ; " << IniOnly::Advanced::GL_PROBE_QUADS.description << "\n";
    out << IniOnly::Advanced::GL_PROBE_BATCH.key << "=" << UiConfig::getInstance().getGlProbeBatch() << " ; " << IniOnly::Advanced::GL_PROBE_BATCH.description << "\n";
    out << IniOnly::Advanced::RENDER_PROBE_QUADS.key << "=" << UiConfig::getInstance().getRenderProbeQuads() << " ; " << IniOnly::Advanced::RENDER_PROBE_QUADS.description << "\n";
    out << IniOnly::Advanced::RENDER_PROBE_FULLSCREEN.key << "=" << (UiConfig::getInstance().getRenderProbeFullscreen() ? 1 : 0) << " ; " << IniOnly::Advanced::RENDER_PROBE_FULLSCREEN.description << "\n";
    out << IniOnly::Advanced::RENDER_PROBE_TYPE.key << "=" << UiConfig::getInstance().getRenderProbeType() << " ; " << IniOnly::Advanced::RENDER_PROBE_TYPE.description << "\n";
    out << IniOnly::Advanced::RENDER_PROBE_SPRITE.key << "=" << UiConfig::getInstance().getRenderProbeSprite() << " ; " << IniOnly::Advanced::RENDER_PROBE_SPRITE.description << "\n";
    out << IniOnly::Advanced::RENDER_PROBE_TEXT_CHARS.key << "=" << UiConfig::getInstance().getRenderProbeTextChars() << " ; " << IniOnly::Advanced::RENDER_PROBE_TEXT_CHARS.description << "\n";
    out << IniOnly::Advanced::RENDER_PROBE_ALPHA.key << "=" << UiConfig::getInstance().getRenderProbeAlpha() << " ; " << IniOnly::Advanced::RENDER_PROBE_ALPHA.description << "\n";
#if GAME_HAS_HTTP_SERVER
    out << IniOnly::Advanced::WEB_SERVER_PORT.key << "=" << HttpServer::getInstance().getPort() << " ; " << IniOnly::Advanced::WEB_SERVER_PORT.description << "\n";
    out << IniOnly::Advanced::WEB_SERVER_THROTTLE_MS.key << "=" << HttpServer::getInstance().getThrottleMs() << " ; " << IniOnly::Advanced::WEB_SERVER_THROTTLE_MS.description << "\n";
    out << IniOnly::Advanced::WEB_SERVER_BIND_ADDRESS.key << "=" << HttpServer::getInstance().getBindAddress() << " ; " << IniOnly::Advanced::WEB_SERVER_BIND_ADDRESS.description << "\n";
#endif
    out << "\n";
}

// Handle Advanced section (power-user settings)
void SettingsManager::applyAdvancedLine(const std::string& key, const std::string& value, HudManager& hudManager) {
    try {
        if (key == "developerMode") {
            m_developerMode = (std::stoi(value) != 0);
        } else if (key == "crashOnReload") {   // dev knob, never written back (settings_manager.h)
            m_crashOnReload = (std::stoi(value) != 0);
        }
        // The two layout roots. Clamped, not rejected: this file is one the plugin
        // itself rewrites, so there is no author to warn and no previous value worth
        // keeping -- landing on the nearest sane number is the useful behaviour.
        // Both call derive(), so the whole vocabulary follows immediately. Nothing
        // downstream needs re-seeding: a theme has no layout of its own, so it does
        // not matter that settings load runs AFTER theme discovery.
        // parseFiniteFloat, not bare std::stof: "nan" parses cleanly and then poisons
        // every derived metric (see layoutSetFontSize). The setters guard too -- this
        // is the load half of the both-ends rule, and it keeps the fallback here
        // rather than relying on the clamp to notice.
        else if (key == "uiFontSize") {
            layoutSetFontSize(LayoutConfig::getInstance().mutableDefaults(),
                              Settings::parseFiniteFloat(value, LayoutMetrics{}.fontSizeNormal));
        } else if (key == "uiLineHeight") {
            layoutSetLineHeight(LayoutConfig::getInstance().mutableDefaults(),
                                Settings::parseFiniteFloat(value, LayoutMetrics{}.lineHeightRatio));
        }
        // The box model's air-term built-ins: CSS shorthand, each side
        // clamped in layoutSetBoxSides (parseSides never throws, so no
        // guard beyond the both-ends clamp is owed here).
        else if (key == IniOnly::Advanced::BOX_PANEL_PADDING.key) {
            layoutSetBoxSides(LayoutConfig::getInstance().mutableDefaults().boxPanelPadding,
                              value, LayoutMetrics{}.boxPanelPadding);
        } else if (key == IniOnly::Advanced::BOX_TITLE_MARGIN.key) {
            layoutSetBoxSides(LayoutConfig::getInstance().mutableDefaults().boxTitleMargin,
                              value, LayoutMetrics{}.boxTitleMargin);
        } else if (key == IniOnly::Advanced::BOX_TITLE_PADDING.key) {
            layoutSetBoxSides(LayoutConfig::getInstance().mutableDefaults().boxTitlePadding,
                              value, LayoutMetrics{}.boxTitlePadding);
        } else if (key == IniOnly::Advanced::BOX_CONTENT_MARGIN.key) {
            layoutSetBoxSides(LayoutConfig::getInstance().mutableDefaults().boxContentMargin,
                              value, LayoutMetrics{}.boxContentMargin);
        } else if (key == IniOnly::Advanced::BOX_CONTENT_PADDING.key) {
            layoutSetBoxSides(LayoutConfig::getInstance().mutableDefaults().boxContentPadding,
                              value, LayoutMetrics{}.boxContentPadding);
        } else if (key == IniOnly::Advanced::BOX_BUTTON_MARGIN.key) {
            layoutSetBoxSides(LayoutConfig::getInstance().mutableDefaults().boxButtonMargin,
                              value, LayoutMetrics{}.boxButtonMargin);
        } else if (key == IniOnly::Advanced::BOX_BUTTON_PADDING.key) {
            layoutSetBoxSides(LayoutConfig::getInstance().mutableDefaults().boxButtonPadding,
                              value, LayoutMetrics{}.boxButtonPadding);
        } else if (key == IniOnly::Advanced::BOX_PANEL_GAP.key) {
            layoutSetBoxScalar(LayoutConfig::getInstance().mutableDefaults().boxPanelGap,
                               value, LayoutMetrics{}.boxPanelGap);
        }
        // Legacy read-only fallbacks for updateChannel/updateDebugMode, which live in
        // [Updates]: an old INI carries them under [Advanced]; read them so values
        // survive the upgrade, then they migrate to [Updates] on the next save.
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
            PluginData::getInstance().proximityTuning().setHazardStationaryTolerance(parseFiniteFloat(value));
        } else if (key == "hazardStationaryDurationMs") {
            PluginData::getInstance().proximityTuning().setHazardStationaryDurationMs(std::stoi(value));
        } else if (key == "hazardWrongWayDurationMs") {
            PluginData::getInstance().proximityTuning().setHazardWrongWayDurationMs(std::stoi(value));
        } else if (key == "hazardAwarenessDistance") {
            PluginData::getInstance().proximityTuning().setHazardAwarenessDistance(parseFiniteFloat(value));
        } else if (key == "hazardWrongWayAwarenessDistance") {
            PluginData::getInstance().proximityTuning().setHazardWrongWayAwarenessDistance(parseFiniteFloat(value));
        } else if (key == "hazardCooldownMs") {
            PluginData::getInstance().proximityTuning().setHazardCooldownMs(std::stoi(value));
        } else if (key == "hazardGracePeriodMs") {
            PluginData::getInstance().proximityTuning().setHazardGracePeriodMs(std::stoi(value));
        } else if (key == "blueFlagAwarenessDistance") {
            PluginData::getInstance().proximityTuning().setBlueFlagAwarenessDistance(parseFiniteFloat(value));
        } else if (key == "gapNotifyIntervalMs") {
            PluginData::getInstance().setGapNotifyIntervalMs(std::stoi(value));
        } else if (key == "pluginThread") {
            UiConfig::getInstance().setPluginThread(std::stoi(value) != 0);
        } else if (key == "overlayInGame") {
            // RETIRED KEY (the overlay window it enabled does not exist; Direct
            // GL Rendering is the replacement). This branch only stops an
            // existing INI from looking corrupt, and says so once if the key is
            // on.
            //
            // An explicit branch rather than a silent fall-through, which would
            // leave a user wondering why their setting does nothing. An
            // accepted-and-explained key is cheaper than a support question.
            if (std::stoi(value) != 0) {
                DEBUG_WARN("[Advanced] overlayInGame is retired and ignored - the "
                           "overlay renderer has been replaced by Direct GL "
                           "Rendering (General tab). Remove the key to silence this.");
            }
        } else if (key == "overlayRefreshHz") {
            // Retired key, accepted and ignored like overlayInGame above:
            // parsing it rather than letting it fall through keeps an old
            // INI from looking corrupt.
        } else if (key == "hwAccel") {
            CompanionWindow::getInstance().setHwAccel(std::stoi(value) != 0);
        } else if (key == "glInGame") {
            UiConfig::getInstance().setGlInGame(std::stoi(value) != 0);
            // Re-applying the key is the retry gesture after a latched
            // failure.
            HudManager::getInstance().clearGlFailLatch();
        } else if (key == "glProbe") {
            UiConfig::getInstance().setGlProbe(std::stoi(value));
        } else if (key == "glProbeX") {
            UiConfig::getInstance().setGlProbeX(parseFiniteFloat(value));
        } else if (key == "glProbeY") {
            UiConfig::getInstance().setGlProbeY(parseFiniteFloat(value));
        } else if (key == "glProbeQuads") {
            UiConfig::getInstance().setGlProbeQuads(std::stoi(value));
        } else if (key == "glProbeBatch") {
            UiConfig::getInstance().setGlProbeBatch(std::stoi(value));
        } else if (key == "renderProbeQuads") {
            UiConfig::getInstance().setRenderProbeQuads(std::stoi(value));
        } else if (key == "renderProbeFullscreen") {
            UiConfig::getInstance().setRenderProbeFullscreen(std::stoi(value) != 0);
        } else if (key == "renderProbeType") {
            UiConfig::getInstance().setRenderProbeType(std::stoi(value));
        } else if (key == "renderProbeSprite") {
            UiConfig::getInstance().setRenderProbeSprite(std::stoi(value));
        } else if (key == "renderProbeTextChars") {
            UiConfig::getInstance().setRenderProbeTextChars(std::stoi(value));
        } else if (key == "renderProbeAlpha") {
            UiConfig::getInstance().setRenderProbeAlpha(std::stoi(value));
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
}

// Write Display section (units, clock format, and display toggles; shown first
// on the Appearance tab)
void SettingsManager::writeDisplaySettings(std::ostream& out, const HudManager& hudManager) const {
    out << "[Display]\n";
    out << "speedUnit=" << speedUnitToString(hudManager.getSpeedWidget().m_speedUnit) << "\n";
    out << "fuelUnit=" << fuelUnitToString(hudManager.getFuelWidget().m_fuelUnit) << "\n";
    out << "tempUnit=" << tempUnitToString(UiConfig::getInstance().getTemperatureUnit()) << "\n";
    out << "format24h=" << (hudManager.getClockWidget().getFormat24h() ? 1 : 0) << "\n";
    out << "shortTimeFormat=" << (PluginData::getInstance().isShortTimeFormat() ? 1 : 0) << "\n";
    out << "dropShadow=" << (UiConfig::getInstance().getDropShadow() ? 1 : 0) << "\n";
    out << "titleIcons=" << (UiConfig::getInstance().getTitleIcons() ? 1 : 0) << "\n";
    out << "gridSnapping=" << (UiConfig::getInstance().getGridSnapping() ? 1 : 0) << "\n";
    // Panel theme by NAME (empty = none). A name survives theme folders being
    // added, removed or reordered; an index would silently repoint.
    out << Settings::Keys::Global::PANEL_THEME << "="
        << UiConfig::getInstance().getThemeName() << "\n";
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
    out << "companionRefreshHz=" << CompanionWindow::getInstance().getRefreshHz()
        << " ; Companion render cadence: 0 = V-Sync (match the monitor), N = cap at N Hz\n";
    out << "displayTarget=" << displayTargetToString(UiConfig::getInstance().getDisplayTarget()) << "\n\n";
}

// Handle Display section (speed/fuel/temp units + clock format; shown first on
// the Appearance tab)
void SettingsManager::applyDisplayLine(const std::string& key, const std::string& value, HudManager& hudManager) {
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
        } else if (key == Settings::Keys::Global::PANEL_THEME) {
            // Stored verbatim without validating against the discovered set:
            // settings load before assets are guaranteed discovered, and an
            // unknown name already degrades to "no theme" at render time.
            UiConfig::getInstance().setThemeName(value);
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
}

// Colors and Fonts: ONLY the slots the user actually pinned.
//
// A theme may state a palette and a font set, and the precedence is built-in
// default -> theme -> user (ColorConfig::getThemeOrDefaultColor). Writing all
// ten colours unconditionally would freeze whatever theme was active at the
// first save into the settings file as if the user had chosen every one of them
// -- and every theme picked afterwards would ship a palette that could never
// apply. Sparse is what keeps an untouched slot following the theme.
//
// Same shape as the sparse per-HUD save, and the same reason.
void SettingsManager::writeColorsSettings(std::ostream& out, const HudManager& /*hudManager*/) const {
    const ColorConfig& colorConfig = ColorConfig::getInstance();
    out << "[Colors]\n";
    for (int i = 0; i < static_cast<int>(ColorSlot::COUNT); ++i) {
        const ColorSlot slot = static_cast<ColorSlot>(i);
        if (!colorConfig.isOverridden(slot)) continue;
        std::string key = ColorConfig::getSlotName(slot);
        for (char& c : key) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
        out << key << "=" << PluginUtils::formatColorHex(colorConfig.getColor(slot)) << "\n";
    }
    out << "\n";
}

// Handle Colors section
void SettingsManager::applyColorsLine(const std::string& key, const std::string& value, HudManager& /*hudManager*/) {
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
}

// Fonts: sparse for the same reason as the colours above.
void SettingsManager::writeFontsSettings(std::ostream& out, const HudManager& /*hudManager*/) const {
    const FontConfig& fontConfig = FontConfig::getInstance();
    out << "[Fonts]\n";
    for (int i = 0; i < static_cast<int>(FontCategory::COUNT); ++i) {
        const FontCategory cat = static_cast<FontCategory>(i);
        if (!fontConfig.isOverridden(cat)) continue;
        std::string key = FontConfig::getCategoryName(cat);
        for (char& c : key) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
        out << key << "=" << fontConfig.getFontName(cat) << "\n";
    }
    out << "\n";
}

// Handle Fonts section
void SettingsManager::applyFontsLine(const std::string& key, const std::string& value, HudManager& /*hudManager*/) {
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
}

// The file's own fingerprint trailer (exploration_stats.h): the hash the
// writer took over everything above it.
void SettingsManager::applyFingerprintLine(const std::string& key, const std::string& value, HudManager& /*hudManager*/) {
    if (key == "settings") {
        try { m_expectedFileHash = std::stoull(value, nullptr, 16); } catch (const std::exception&) { m_expectedFileHash = 0; }
    }
}
