// ============================================================================
// core/settings_manager.cpp
// Manages persistence of HUD settings (position, scale, visibility, etc.)
// Supports per-profile settings (Practice, Race, Spectate)
// ============================================================================
#include "settings_manager.h"
#include "settings_keys.h"
#include "settings_serde.h"
#include "settings_hud_registry.h"
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

// Bring the centralized INI key names / INI-only descriptors into scope so the
// existing `Keys::...` and `IniOnly::...` references below resolve unchanged.
using namespace Settings;

namespace {
    constexpr const char* SETTINGS_SUBDIRECTORY = "mxbmrp3";
    constexpr const char* SETTINGS_FILENAME = "mxbmrp3_settings.ini";

    // Settings format version - bump this when making incompatible changes
    // Version 1: Original format with bitmasks (implicit, no version field)
    // Version 2: Named keys instead of bitmasks for columns/rows/elements
    // Version 3: String enums instead of integers for all enum settings
    // Version 4: Base sections + sparse profile sections (reduced INI size)
    constexpr int SETTINGS_VERSION = 4;

    // The on-disk shape has been stable since v4 (base [HudName] sections + sparse
    // [HudName:Profile] overrides). The load dispatch keys off THIS floor, not off
    // == SETTINGS_VERSION, so a file written by any version >= this one still loads
    // its HUD sections after SETTINGS_VERSION is later bumped. Gating on
    // == SETTINGS_VERSION silently wiped every user's HUD settings the moment the
    // version was bumped (their v4 file then matched neither the v4+ nor the v3
    // branch and every [HudName] section was skipped). Only bump this floor when
    // the base/profile section layout itself changes incompatibly.
    constexpr int FIRST_BASE_SECTION_VERSION = 4;
}

SettingsManager& SettingsManager::getInstance() {
    static SettingsManager instance;
    return instance;
}

std::string SettingsManager::getSettingsFilePath(const char* savePath) const {
    if (!savePath || savePath[0] == '\0') {
        // Use relative path when savePath is not provided
        std::string subdir = std::string(".\\") + SETTINGS_SUBDIRECTORY;
        if (!CreateDirectoryA(subdir.c_str(), NULL)) {
            DWORD error = GetLastError();
            if (error != ERROR_ALREADY_EXISTS) {
                DEBUG_WARN_F("Failed to create settings directory: %s (error %lu)", subdir.c_str(), error);
            }
        }
        return subdir + "\\" + SETTINGS_FILENAME;
    }

    std::string path = savePath;
    if (!path.empty() && path.back() != '/' && path.back() != '\\') {
        path += '\\';
    }
    path += SETTINGS_SUBDIRECTORY;

    if (!CreateDirectoryA(path.c_str(), NULL)) {
        DWORD error = GetLastError();
        if (error != ERROR_ALREADY_EXISTS) {
            DEBUG_WARN_F("Failed to create settings directory: %s (error %lu)", path.c_str(), error);
        }
    }

    path += '\\';
    path += SETTINGS_FILENAME;
    return path;
}



std::string SettingsManager::serializeSettings(const HudManager& hudManager, const char* savePath) {
    m_savePath = savePath ? savePath : "";

    // Capture current live state to the active profile before building the file.
    // Note: This modifies m_profileCache, which is why serialize/save is non-const.
    captureCurrentState(hudManager);

    // Build the full file into an in-memory stream; the caller writes it atomically.
    std::ostringstream file;

    // Write header comment with usage notes
    file << "; MXBMRP3 Settings File\n";
    file << "; To edit manually, disable Auto-Save in Settings > General,\n";
    file << "; then reload in-game with the hotkey after saving changes.\n";
    file << "\n";

    // Write Settings section (format versioning)
    file << "[Settings]\n";
    file << "version=" << SETTINGS_VERSION << "\n\n";

    // Write Profiles section
    const ProfileManager& profileManager = ProfileManager::getInstance();
    file << "[Profiles]\n";
    file << "activeProfile=" << static_cast<int>(profileManager.getActiveProfile()) << "\n";
    file << "autoSwitch=" << (profileManager.isAutoSwitchEnabled() ? 1 : 0) << "\n";
    // Last-focused settings tab (by name), restored on load so reopening the menu lands
    // where the player left it. Menu-navigation state, kept here with the active profile
    // (and, like it, deliberately outside the factory-defaults snapshot so "Reset all
    // settings" doesn't move the player's open tab).
    file << "activeTab=" << hudManager.getSettingsHud().getActiveTabName() << "\n\n";

    // Write all global (non-per-profile) sections via the shared serializer, keeping the
    // factory-defaults snapshot (see captureFactoryDefaults) in sync with the saved output.
    writeGlobalSettings(file, hudManager);

    // Save tracked riders to separate JSON file
    TrackedRidersManager::getInstance().save();

    // Per-profile HUD/widget sections, in the registry's fixed order for a stable
    // file. The per-HUD serializer registry (settings_hud_registry) is the SINGLE
    // source of truth for the section list: captureToCache, applyProfile, and this
    // serializer all iterate it, so a HUD is registered for capture, apply, and
    // on-disk serialization in exactly one place. This replaced the old parallel
    // "hudOrder" array whose omission silently dropped a HUD's settings on restart
    // (the FriendsHud bug). settings_sections_test.cpp still asserts capture ⊆
    // serialized as a belt-and-suspenders guard.
    // Note: HelmetOverlayHud is global (own [HelmetOverlay] section), not per-profile.
    // Game-gated HUDs are #if'd out of the registry on builds without them, and
    // buildHudSection() returns "" for any section absent from m_hudDefaults.
    for (const Settings::HudSectionSerializer& s : Settings::hudSectionRegistry()) {
        file << buildHudSection(s.name);
    }

    // GamepadWidget / PitboardHud per-variant layout blocks (read live from the widgets).
    file << buildGamepadLayouts(hudManager);
    file << buildPitboardLayouts(hudManager);

    return file.str();
}

// Build one HUD/widget's block: base [Section] + sparse [Section:Profile] overrides.
// "" if the section has no defaults entry (a game-gated HUD absent from this build).
std::string SettingsManager::buildHudSection(const char* hudName) const {
    auto defaultIt = m_hudDefaults.find(hudName);
    if (defaultIt == m_hudDefaults.end()) return std::string();

    std::ostringstream file;

    // Write base section [HudName] with default values
    file << "[" << hudName << "]\n";

    // Write base properties first (for consistent ordering)
    writeBaseHudSettings(file, defaultIt->second);

    // Write HUD-specific properties (with inline comments for IniOnly settings)
    for (const auto& [key, value] : defaultIt->second) {
        if (isBaseKey(key)) continue;
        writeSettingWithComment(file, hudName, key, value);
    }
    file << "\n";

    // Write profile-specific overrides [HudName:ProfileName]
    // Only write values that differ from defaults
    for (int profileIdx = 0; profileIdx < static_cast<int>(ProfileType::COUNT); ++profileIdx) {
        ProfileType profile = static_cast<ProfileType>(profileIdx);
        const ProfileCache& cache = m_profileCache[static_cast<size_t>(profileIdx)];
        const char* profileName = ProfileManager::getProfileName(profile);

        auto cacheIt = cache.find(hudName);
        if (cacheIt == cache.end()) continue;

        // Collect keys that differ from defaults
        std::vector<std::pair<std::string, std::string>> diffKeys;
        for (const auto& [key, value] : cacheIt->second) {
            bool isDifferent = true;
            auto defKeyIt = defaultIt->second.find(key);
            if (defKeyIt != defaultIt->second.end() && defKeyIt->second == value) {
                isDifferent = false;
            }
            if (isDifferent) {
                diffKeys.emplace_back(key, value);
            }
        }

        // Only write section if there are differences
        if (!diffKeys.empty()) {
            file << "[" << hudName << ":" << profileName << "]\n";

            // Write differing keys (base properties first for consistency)
            for (const auto& [key, value] : diffKeys) {
                if (isBaseKey(key)) {
                    file << key << "=" << value << "\n";
                }
            }
            for (const auto& [key, value] : diffKeys) {
                if (!isBaseKey(key)) {
                    file << key << "=" << value << "\n";
                }
            }
            file << "\n";
        }
    }

    return file.str();
}

// Build the GamepadWidget per-variant layout blocks (read live from the widget).
std::string SettingsManager::buildGamepadLayouts(const HudManager& hudManager) const {
    std::ostringstream file;
    // Write GamepadWidget per-variant layouts (not per-profile, global)
    // Only save layouts that actually exist (default: variants 1 and 2)
    {
        file << "# GamepadWidget Per-Variant Layouts\n";
        const auto& gamepadWidget = hudManager.getGamepadWidget();

        // Check which layouts exist and save them
        for (int variant = 1; variant <= 10; ++variant) {
            const auto* layout = gamepadWidget.getLayoutIfExists(variant);
            if (!layout) continue;

            file << "[GamepadWidget_Layout_" << variant << "]\n";
            file << "backgroundWidth=" << layout->backgroundWidth << "\n";
            file << "triggerWidth=" << layout->triggerWidth << "\n";
            file << "triggerHeight=" << layout->triggerHeight << "\n";
            file << "bumperWidth=" << layout->bumperWidth << "\n";
            file << "bumperHeight=" << layout->bumperHeight << "\n";
            file << "dpadWidth=" << layout->dpadWidth << "\n";
            file << "dpadHeight=" << layout->dpadHeight << "\n";
            file << "faceButtonSize=" << layout->faceButtonSize << "\n";
            file << "menuButtonWidth=" << layout->menuButtonWidth << "\n";
            file << "menuButtonHeight=" << layout->menuButtonHeight << "\n";
            file << "stickSize=" << layout->stickSize << "\n";
            file << "leftTriggerX=" << layout->leftTriggerX << "\n";
            file << "leftTriggerY=" << layout->leftTriggerY << "\n";
            file << "rightTriggerX=" << layout->rightTriggerX << "\n";
            file << "rightTriggerY=" << layout->rightTriggerY << "\n";
            file << "leftBumperX=" << layout->leftBumperX << "\n";
            file << "leftBumperY=" << layout->leftBumperY << "\n";
            file << "rightBumperX=" << layout->rightBumperX << "\n";
            file << "rightBumperY=" << layout->rightBumperY << "\n";
            file << "leftStickX=" << layout->leftStickX << "\n";
            file << "leftStickY=" << layout->leftStickY << "\n";
            file << "rightStickX=" << layout->rightStickX << "\n";
            file << "rightStickY=" << layout->rightStickY << "\n";
            file << "dpadX=" << layout->dpadX << "\n";
            file << "dpadY=" << layout->dpadY << "\n";
            file << "faceButtonsX=" << layout->faceButtonsX << "\n";
            file << "faceButtonsY=" << layout->faceButtonsY << "\n";
            file << "menuButtonsX=" << layout->menuButtonsX << "\n";
            file << "menuButtonsY=" << layout->menuButtonsY << "\n";
            file << "dpadSpacing=" << layout->dpadSpacing << "\n";
            file << "faceButtonSpacing=" << layout->faceButtonSpacing << "\n";
            file << "menuButtonSpacing=" << layout->menuButtonSpacing << "\n";
            writeSettingWithComment(file, "GamepadWidget", "triggerFillMode", std::to_string(layout->triggerFillMode));
            file << "\n";
        }
    }
    return file.str();
}

// Build the PitboardHud per-texture layout blocks (read live from the widget).
std::string SettingsManager::buildPitboardLayouts(const HudManager& hudManager) const {
    std::ostringstream file;
    // Write PitboardHud per-texture layouts (not per-profile, global)
    {
        file << "# PitboardHud Per-Texture Layouts\n";
        const auto& pitboardHud = hudManager.getPitboardHud();

        // Check which layouts exist and save them
        for (int variant = 1; variant <= 10; ++variant) {
            const auto* layout = pitboardHud.getLayoutIfExists(variant);
            if (!layout) continue;

            file << "[PitboardHud_Layout_" << variant << "]\n";
            file << "riderIdX=" << layout->riderIdX << "\n";
            file << "riderIdY=" << layout->riderIdY << "\n";
            file << "sessionX=" << layout->sessionX << "\n";
            file << "sessionY=" << layout->sessionY << "\n";
            file << "positionX=" << layout->positionX << "\n";
            file << "positionY=" << layout->positionY << "\n";
            file << "timeX=" << layout->timeX << "\n";
            file << "timeY=" << layout->timeY << "\n";
            file << "lapX=" << layout->lapX << "\n";
            file << "lapY=" << layout->lapY << "\n";
            file << "lastLapX=" << layout->lastLapX << "\n";
            file << "lastLapY=" << layout->lastLapY << "\n";
            file << "gapX=" << layout->gapX << "\n";
            file << "gapY=" << layout->gapY << "\n";
            file << "\n";
        }
    }
    return file.str();
}

void SettingsManager::saveSettings(const HudManager& hudManager, const char* savePath) {
    // Synchronous path (explicit Save / Reset / leave-track flush / shutdown): serialize, then
    // write on this thread so the file is durable before we return.
    const std::string filePath = getSettingsFilePath(savePath);
    const std::string data = serializeSettings(hudManager, savePath);
    DEBUG_INFO_F("Saving settings to: %s (synchronous)", filePath.c_str());
    if (AtomicFileWriter::writeFileAtomic(filePath, data)) {
        DEBUG_INFO("Settings saved successfully");
        m_settingsDirty = false;   // persisted; nothing pending
    } else {
        DEBUG_WARN_F("Failed to save settings: %s", filePath.c_str());
    }
}

void SettingsManager::flushIfDirty(const HudManager& hudManager) {
    // Called on the track->off-track transition (pits / exit). Auto-persist pending changes
    // where the ~2ms serialize is invisible — but only when Auto-Save is on. With Auto-Save
    // off the user is in manual mode (persists via the Save button), so leaving the track must
    // NOT write. No-op if nothing changed either way.
    if (!m_settingsDirty) return;
    if (!UiConfig::getInstance().getAutoSave()) return;
    saveSettings(hudManager, m_savePath.c_str());   // clears m_settingsDirty on success
}

void SettingsManager::loadSettings(HudManager& hudManager, const char* savePath) {
    std::string filePath = getSettingsFilePath(savePath);
    m_savePath = savePath ? savePath : "";
    m_settingsDirty = false;   // freshly (re)loaded state matches disk

    // Capture factory defaults from HUDs BEFORE loading any settings
    // This gives us the constructor default values to use for sparse saving
    captureFactoryDefaults(hudManager);

    // Mark that settings loading has started (used by assertion in captureFactoryDefaults)
    m_settingsLoaded = true;

    std::ifstream file(filePath);
    if (!file.is_open()) {
        DEBUG_INFO_F("No settings file found at: %s (using defaults)", filePath.c_str());
        // Initialize cache with current (default) state for all profiles
        for (int i = 0; i < static_cast<int>(ProfileType::COUNT); ++i) {
            captureToProfile(hudManager, static_cast<ProfileType>(i));
        }
        return;
    }

    DEBUG_INFO_F("Loading settings from: %s", filePath.c_str());

    // Clear existing cache
    for (auto& cache : m_profileCache) {
        cache.clear();
    }

    std::string line;
    std::string currentSection;
    std::string currentHudName;
    int currentProfileIndex = -1;
    int loadedVersion = 0;  // Version 0 means old format (pre-versioning)

    while (std::getline(file, line)) {
        // Trim whitespace
        size_t start = line.find_first_not_of(" \t\r\n");
        size_t end = line.find_last_not_of(" \t\r\n");
        if (start == std::string::npos) continue;
        if (end == std::string::npos) continue;
        line = line.substr(start, end - start + 1);

        // Skip comments
        if (line[0] == '#') continue;

        // Check for section header
        if (line.length() >= 3 && line.front() == '[' && line.back() == ']') {
            currentSection = line.substr(1, line.length() - 2);
            parseSectionName(currentSection, currentHudName, currentProfileIndex);
            continue;
        }

        // Parse key=value
        size_t equals = line.find('=');
        if (equals == std::string::npos) continue;

        std::string key = line.substr(0, equals);
        std::string value = line.substr(equals + 1);

        // Strip inline comments (everything after ';')
        size_t commentPos = value.find(';');
        if (commentPos != std::string::npos) {
            value.resize(commentPos);
            // Trim trailing whitespace from value
            size_t valueEnd = value.find_last_not_of(" \t");
            if (valueEnd != std::string::npos) {
                value.resize(valueEnd + 1);
            } else {
                value.clear();  // Value was only whitespace before comment
            }
        }

        // Handle Settings section (format versioning)
        if (currentHudName == "Settings") {
            try {
                if (key == "version") {
                    loadedVersion = std::stoi(value);
                    DEBUG_INFO_F("Settings file version: %d (current: %d)", loadedVersion, SETTINGS_VERSION);
                }
            } catch (const std::exception& e) {
                DEBUG_WARN_F("Settings: Failed to parse version: %s", e.what());
            }
            continue;
        }

        // Handle Profiles section
        if (currentHudName == "Profiles") {
            try {
                if (key == "activeProfile") {
                    int profileIdx = std::stoi(value);
                    if (profileIdx >= 0 && profileIdx < static_cast<int>(ProfileType::COUNT)) {
                        ProfileManager::getInstance().setActiveProfile(static_cast<ProfileType>(profileIdx));
                    }
                } else if (key == "autoSwitch") {
                    ProfileManager::getInstance().setAutoSwitchEnabled(std::stoi(value) != 0);
                } else if (key == "activeTab") {
                    // Restore the last-focused settings tab (by name; ignored if the tab
                    // doesn't exist on this build). Never throws, so it's fine in the try.
                    hudManager.getSettingsHud().setActiveTabByName(value.c_str());
                }
            } catch (const std::exception& e) {
                DEBUG_WARN_F("Profiles: Failed to parse settings: %s", e.what());
            }
            continue;
        }

        // Global (non-per-profile) sections share one applier so loadSettings() and
        // resetGlobalsToFactoryDefaults() cannot drift as new globals are added.
        if (applyGlobalLine(currentHudName, key, value, hudManager)) {
            continue;
        }

        // Handle GamepadWidget_Layout_N sections (per-variant layouts)
        if (currentHudName.length() > 20 && currentHudName.substr(0, 20) == "GamepadWidget_Layout") {
            try {
                int variant = std::stoi(currentHudName.substr(21));
                if (variant >= 1 && variant <= 10) {
                    auto& hud = hudManager.getGamepadWidget();
                    auto& layout = hud.getLayout(variant);

                    if (key == "backgroundWidth") layout.backgroundWidth = parseFiniteFloat(value);
                    else if (key == "triggerWidth") layout.triggerWidth = parseFiniteFloat(value);
                    else if (key == "triggerHeight") layout.triggerHeight = parseFiniteFloat(value);
                    else if (key == "bumperWidth") layout.bumperWidth = parseFiniteFloat(value);
                    else if (key == "bumperHeight") layout.bumperHeight = parseFiniteFloat(value);
                    else if (key == "dpadWidth") layout.dpadWidth = parseFiniteFloat(value);
                    else if (key == "dpadHeight") layout.dpadHeight = parseFiniteFloat(value);
                    else if (key == "faceButtonSize") layout.faceButtonSize = parseFiniteFloat(value);
                    else if (key == "menuButtonWidth") layout.menuButtonWidth = parseFiniteFloat(value);
                    else if (key == "menuButtonHeight") layout.menuButtonHeight = parseFiniteFloat(value);
                    else if (key == "stickSize") layout.stickSize = parseFiniteFloat(value);
                    else if (key == "leftTriggerX") layout.leftTriggerX = parseFiniteFloat(value);
                    else if (key == "leftTriggerY") layout.leftTriggerY = parseFiniteFloat(value);
                    else if (key == "rightTriggerX") layout.rightTriggerX = parseFiniteFloat(value);
                    else if (key == "rightTriggerY") layout.rightTriggerY = parseFiniteFloat(value);
                    else if (key == "leftBumperX") layout.leftBumperX = parseFiniteFloat(value);
                    else if (key == "leftBumperY") layout.leftBumperY = parseFiniteFloat(value);
                    else if (key == "rightBumperX") layout.rightBumperX = parseFiniteFloat(value);
                    else if (key == "rightBumperY") layout.rightBumperY = parseFiniteFloat(value);
                    else if (key == "leftStickX") layout.leftStickX = parseFiniteFloat(value);
                    else if (key == "leftStickY") layout.leftStickY = parseFiniteFloat(value);
                    else if (key == "rightStickX") layout.rightStickX = parseFiniteFloat(value);
                    else if (key == "rightStickY") layout.rightStickY = parseFiniteFloat(value);
                    else if (key == "dpadX") layout.dpadX = parseFiniteFloat(value);
                    else if (key == "dpadY") layout.dpadY = parseFiniteFloat(value);
                    else if (key == "faceButtonsX") layout.faceButtonsX = parseFiniteFloat(value);
                    else if (key == "faceButtonsY") layout.faceButtonsY = parseFiniteFloat(value);
                    else if (key == "menuButtonsX") layout.menuButtonsX = parseFiniteFloat(value);
                    else if (key == "menuButtonsY") layout.menuButtonsY = parseFiniteFloat(value);
                    else if (key == "dpadSpacing") layout.dpadSpacing = parseFiniteFloat(value);
                    else if (key == "faceButtonSpacing") layout.faceButtonSpacing = parseFiniteFloat(value);
                    else if (key == "menuButtonSpacing") layout.menuButtonSpacing = parseFiniteFloat(value);
                    else if (key == "triggerFillMode") layout.triggerFillMode = std::stoi(value);
                }
            } catch (const std::exception& e) {
                DEBUG_WARN_F("GamepadWidget Layout: Failed to parse settings: %s", e.what());
            }
            continue;
        }

        // Handle PitboardHud_Layout_N sections (per-texture layouts)
        if (currentHudName.length() > 18 && currentHudName.substr(0, 18) == "PitboardHud_Layout") {
            try {
                int variant = std::stoi(currentHudName.substr(19));
                if (variant >= 1 && variant <= 10) {
                    auto& hud = hudManager.getPitboardHud();
                    auto& layout = hud.getLayout(variant);

                    // Clamp layout offsets to reasonable range (-1.0 to 1.0)
                    auto clampOffset = [](float v) { return std::clamp(v, -1.0f, 1.0f); };

                    if (key == "riderIdX") layout.riderIdX = clampOffset(parseFiniteFloat(value));
                    else if (key == "riderIdY") layout.riderIdY = clampOffset(parseFiniteFloat(value));
                    else if (key == "sessionX") layout.sessionX = clampOffset(parseFiniteFloat(value));
                    else if (key == "sessionY") layout.sessionY = clampOffset(parseFiniteFloat(value));
                    else if (key == "positionX") layout.positionX = clampOffset(parseFiniteFloat(value));
                    else if (key == "positionY") layout.positionY = clampOffset(parseFiniteFloat(value));
                    else if (key == "timeX") layout.timeX = clampOffset(parseFiniteFloat(value));
                    else if (key == "timeY") layout.timeY = clampOffset(parseFiniteFloat(value));
                    else if (key == "lapX") layout.lapX = clampOffset(parseFiniteFloat(value));
                    else if (key == "lapY") layout.lapY = clampOffset(parseFiniteFloat(value));
                    else if (key == "lastLapX") layout.lastLapX = clampOffset(parseFiniteFloat(value));
                    else if (key == "lastLapY") layout.lastLapY = clampOffset(parseFiniteFloat(value));
                    else if (key == "gapX") layout.gapX = clampOffset(parseFiniteFloat(value));
                    else if (key == "gapY") layout.gapY = clampOffset(parseFiniteFloat(value));
                }
            } catch (const std::exception& e) {
                DEBUG_WARN_F("PitboardHud Layout: Failed to parse settings: %s", e.what());
            }
            continue;
        }

        // Handle HUD settings sections
        // Version 4+: [HudName] for base/defaults, [HudName:ProfileName] for overrides
        // Version 3: [HudName:0], [HudName:1], etc. (no base sections)
        //
        // Match ANY version in the base-section era (>= FIRST_BASE_SECTION_VERSION),
        // not just == SETTINGS_VERSION, so a future version bump doesn't orphan the
        // current on-disk format (see the constant's comment). loadedVersion == 0
        // (no/unparseable [Settings]version line) is also treated as the current
        // format: the version line is the first thing written, so a version-less
        // file is a current file whose header was dropped by hand-editing (a
        // supported workflow) far more often than a genuine pre-v3 relic — and
        // assuming current preserves the user's HUD layout instead of silently
        // resetting it. A true v1/v2 file still falls back to defaults per key
        // (bad values are caught by applyProfile's per-HUD try/catch).
        if (loadedVersion >= FIRST_BASE_SECTION_VERSION || loadedVersion == 0) {
            if (currentProfileIndex == -1) {
                // Base section [HudName] - apply to ALL profiles as baseline
                for (int i = 0; i < static_cast<int>(ProfileType::COUNT); ++i) {
                    m_profileCache[i][currentHudName][key] = value;
                }
                // Also update defaults so base keys round-trip correctly on save
                // (without this, user-added base keys like color_primary would migrate to profile sections)
                // Normalize color values to canonical format so string diffs work after captureToProfile
                if (key.rfind("color_", 0) == 0) {
                    try {
                        m_hudDefaults[currentHudName][key] = PluginUtils::formatColorHex(PluginUtils::parseColorHex(value));
                    } catch (const std::exception&) {
                        // Malformed hand-edited color value - keep the raw string
                        // rather than aborting the whole loadSettings() parse
                        DEBUG_WARN_F("Invalid color value '%s' for [%s] %s", value.c_str(), currentHudName.c_str(), key.c_str());
                        m_hudDefaults[currentHudName][key] = value;
                    }
                } else {
                    m_hudDefaults[currentHudName][key] = value;
                }
            } else if (currentProfileIndex >= 0 && currentProfileIndex < static_cast<int>(ProfileType::COUNT)) {
                // Profile-specific section [HudName:ProfileName] - overlay onto that profile
                m_profileCache[currentProfileIndex][currentHudName][key] = value;
            }
        } else if (loadedVersion == 3) {
            // v3 format: only profile-specific sections [HudName:0], [HudName:1], etc.
            if (currentProfileIndex >= 0 && currentProfileIndex < static_cast<int>(ProfileType::COUNT)) {
                m_profileCache[currentProfileIndex][currentHudName][key] = value;
            }
        }
        // Version < 3 settings are not supported (too old)
    }

    file.close();

    // Check if we need to reset to defaults due to old version
    // We support v3+ (v3 used numeric profile indices, v4+ uses named profiles)
    if (loadedVersion > 0 && loadedVersion < 3) {
        DEBUG_INFO_F("Settings version too old (file: %d, minimum: 3) - resetting HUD settings to defaults",
                    loadedVersion);
        DEBUG_INFO("Note: Global settings (colors, fonts, hotkeys) are preserved");
        for (int i = 0; i < static_cast<int>(ProfileType::COUNT); ++i) {
            captureToProfile(hudManager, static_cast<ProfileType>(i));
        }
    }

    // If cache is empty (corrupted file or first run), initialize all profiles with defaults
    bool anyProfileEmpty = false;
    for (const auto& cache : m_profileCache) {
        if (cache.empty()) {
            anyProfileEmpty = true;
            break;
        }
    }
    if (anyProfileEmpty && loadedVersion >= 3) {
        DEBUG_INFO("Initializing profiles with defaults (empty cache despite valid version)");
        for (int i = 0; i < static_cast<int>(ProfileType::COUNT); ++i) {
            captureToProfile(hudManager, static_cast<ProfileType>(i));
        }
    }

    m_cacheInitialized = true;

    // Drop retired keys so they don't linger in the rewritten file. The
    // forward-compatible loader preserves unknown keys by design; this prunes
    // only keys we have explicitly retired (renamed/removed), keeping upgraded
    // files tidy without touching genuinely-unknown keys.
    {
        static const std::pair<const char*, const char*> kRetiredKeys[] = {
            {"StandingsHud", "useAccentHighlight"},        // 1.22.0.0 -> playerRowHighlightAccent (#175)
            {"StandingsHud", "playerRowHighlightAccent"},  // -> playerRowHighlightBrand (this branch)
        };
        for (const auto& [hudName, key] : kRetiredKeys) {
            auto defIt = m_hudDefaults.find(hudName);
            if (defIt != m_hudDefaults.end()) defIt->second.erase(key);
            for (auto& cache : m_profileCache) {
                auto hudIt = cache.find(hudName);
                if (hudIt != cache.end()) hudIt->second.erase(key);
            }
        }
    }

    // Apply active profile to HUDs
    applyActiveProfile(hudManager);

    // Cleanup any leftover files from previous updates
    UpdateDownloader::getInstance().cleanupOldFiles();

    // Show donation nudge if this is the first load after a successful auto-install.
    // Always consume the sentinel (clear it) even when the nudge is disabled, so a
    // disabled user doesn't leave the pending file lingering on disk; gate only the
    // showing on the enabled flag.
    bool nudgePending = UpdateDownloader::getInstance().checkAndClearDonationNudge();
    if (nudgePending && UpdateDownloader::getInstance().isDonationNudgeEnabled()) {
        hudManager.getVersionWidget().showDonationNudge();
    }

    // Trigger update check on startup if enabled
    if (UpdateChecker::getInstance().isEnabled()) {
        DEBUG_INFO("Update check enabled, checking for updates on startup");
        // Set one-time startup callback to show version widget notification when update is found.
        // Note: Manual checks from settings UI will set their own callback, overwriting this one.
        UpdateChecker::getInstance().setCompletionCallback([]() {
            UpdateChecker& checker = UpdateChecker::getInstance();
            if (checker.getStatus() == UpdateChecker::Status::UPDATE_AVAILABLE) {
                if (checker.shouldShowUpdateNotification()) {
                    // Show the version widget with update notification
                    HudManager::getInstance().getVersionWidget().showUpdateNotification();
                } else {
                    DEBUG_INFO_F("Update %s available but dismissed by user",
                                checker.getLatestVersion().c_str());
                }
            }
        });
        UpdateChecker::getInstance().checkForUpdates();
    }

    DEBUG_INFO("Settings loaded successfully");
}
