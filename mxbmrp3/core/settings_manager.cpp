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
#include <cstring>   // std::strcmp, for the v4 -> v5 font-slot migration
#include <set>       // the v7/v8 migrations' record of file-carried base keys
#include "../hud/standings_hud.h"
#include "../hud/pitboard_hud.h"
// settings_hud.h is core (every game has the settings menu, and getSettingsHud() is
// used unconditionally below), and it pulls records_hud.h itself; both .cpp files are
// compiled on every game, so neither include may be gated on GAME_HAS_RECORDS_PROVIDER
// — gating it broke the GPB/KRP builds (SettingsHud left incomplete -> C2027). The
// *provider* feature stays runtime/registration-gated; only these includes are always on.
#include "../hud/settings_hud.h"
#include "../hud/version_widget.h"
#include "../hud/gamepad_widget.h"
#include "../hud/radar_hud.h"
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
#include "install_prefs.h"
#endif
#include "xinput_reader.h"
#include "hotkey_manager.h"
#include "director_manager.h"
#include "companion_window.h"
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
    // 5: [Colors]/[Fonts] became SPARSE -- only slots the user pinned are written, so
    //    absence means "follow the theme". A file at 4 or below wrote all ten colours
    //    and all six fonts unconditionally, and the load path pins every key it sees;
    //    see the migration in loadSettings().
    // 6: the gamepad AND the pit board are chosen by PACK NAME (gamepads/<name>/,
    //    pitboards/<name>/) rather than by texture variant index. Files at 5 or below
    //    store the old index; the migration in loadSettings() maps the shipped
    //    variants onto their pack names. Both moved in the same version deliberately:
    //    6 is unreleased, and a file already written at 6 maps to the default board
    //    anyway, so a second version step would buy nothing.
    // 7: Notices and Timing offsetX means the panel's CENTRE, like the Gap Bar and
    //    Version already did, instead of a delta from a centre computed at render
    //    time. The migration in loadSettings() adds the anchor in.
    // 8: the Radar joins them. Its offsetX meant a LEFT EDGE, so unlike 7 the shift
    //    is half the panel's width and depends on the stored scale -- hence its own
    //    version rather than folding into the unreleased 7: a file already stamped 7
    //    would skip the shift, and every dev install is stamped 7 by now.
    constexpr int SETTINGS_VERSION = 9;

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

void SettingsManager::loadSettingsImpl(HudManager& hudManager, const char* savePath) {
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

    // ABSENCE IS AUTHORITATIVE for [Colors] and [Fonts], which means releasing every
    // pin BEFORE the parse and letting the file re-pin only what it actually states.
    //
    // Those two sections became SPARSE on write -- only slots the user pinned are
    // emitted, and an absent key means "follow the theme". The read side was never
    // made to match: applyGlobalLine() only ever calls setColor/setFont for keys it
    // SEES, and the only clearOverride() calls in the whole load path were inside the
    // v4 -> v5 migration, which a current file skips. So absence could not release
    // anything, and the two supported ways to un-pin a slot both failed silently:
    //
    //   Reload Config after changing a colour with auto-save off -- the discarded
    //   value stayed pinned and was written straight back out on the next save.
    //
    //   Deleting `accent=...` from [Colors] by hand -- the line reappeared, because
    //   the slot was still pinned in memory when the file was rewritten.
    //
    // replayGlobalDefaults() already does exactly this before replaying the defaults
    // snapshot (settings_hud_profiles.cpp) and is the shape copied here; the reset path
    // was fixed for this and the load path was not.
    //
    // AFTER the is_open() check, deliberately: a missing file returns above, and
    // releasing pins for a file that could not be read would silently discard the
    // user's palette on any transient I/O failure.
    for (int i = 0; i < static_cast<int>(ColorSlot::COUNT); ++i) {
        ColorConfig::getInstance().clearOverride(static_cast<ColorSlot>(i));
    }
    for (int i = 0; i < static_cast<int>(FontCategory::COUNT); ++i) {
        FontConfig::getInstance().clearOverride(static_cast<FontCategory>(i));
    }

    std::string line;
    std::string currentSection;
    std::string currentHudName;
    int currentProfileIndex = -1;
    int loadedVersion = 0;  // Version 0 means old format (pre-versioning)

    // Which centre-anchor base keys the FILE actually carried, recorded during
    // the fold below for the v7/v8 migrations: m_hudDefaults is seeded from the
    // factory snapshot, whose values are already in the NEW semantics, so a
    // defaults entry may be shifted only when the file's base section overwrote
    // it. Entries are "<hud>|<key>".
    std::set<std::string> baseAnchorKeysInFile;

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

        // Strip inline comments (everything after ';') -- except where the value
        // IS a folder name the user chose, in which case a `;` is data. See
        // Settings::isFolderNameValue for the bug that costs (a permanently
        // destroyed theme/pack choice) and why the test is on the KEY.
        size_t commentPos = Settings::isFolderNameValue(key)
                          ? std::string::npos : value.find(';');
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
                if ((currentHudName == "NoticesHud" || currentHudName == "TimingHud" ||
                     currentHudName == "RadarHud") &&
                    (key == Keys::Base::OFFSET_X || key == Keys::Base::COMPANION_X)) {
                    baseAnchorKeysInFile.insert(currentHudName + "|" + key);
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

    // SPARSE COLOURS/FONTS MIGRATION (files written at version <= 4).
    //
    // Those files list every colour and every font, and applyGlobalLine pins each key it
    // reads -- so an upgrading user has all sixteen slots marked "mine" and a theme's
    // palette and font set can never show through. That is the branch's headline feature
    // silently dead for every existing install, recoverable only via Appearance > Reset.
    //
    // The migration is deliberately CONSERVATIVE: unpin only the slots whose stored value
    // equals the built-in default, which is exactly the set that carries no user intent --
    // they are in the file because the old writer wrote everything, not because anyone
    // chose them. A slot the user really did set to a non-default value stays pinned, and
    // a slot they set to precisely the built-in default loses nothing but a pin (the value
    // is unchanged; it can now follow a theme, which is what a fresh install would do).
    // `> 0` AGREES WITH THE DISPATCH ABOVE, which treats a version-less file as the
    // CURRENT format (a header dropped by hand-editing is a supported workflow). A
    // current file already expresses absence properly, so there is nothing to release;
    // migrating it would be the loader calling the same file old here and current
    // there. The cost is a genuinely ancient hand-edited file staying pinned, whose
    // route out is Appearance > Reset -- accepted, because the alternative reads user
    // intent out of a file we have just declared to be current.
    if (loadedVersion > 0 && loadedVersion < 5) {
        int freedColors = 0, freedFonts = 0;
        for (int i = 0; i < static_cast<int>(ColorSlot::COUNT); ++i) {
            const ColorSlot slot = static_cast<ColorSlot>(i);
            if (ColorConfig::getInstance().isOverridden(slot) &&
                ColorConfig::getInstance().getColor(slot) == ColorConfig::getDefaultColor(slot)) {
                ColorConfig::getInstance().clearOverride(slot);
                freedColors++;
            }
        }
        for (int i = 0; i < static_cast<int>(FontCategory::COUNT); ++i) {
            const FontCategory cat = static_cast<FontCategory>(i);
            const char* def = FontConfig::getDefaultFontName(cat);
            const char* cur = FontConfig::getInstance().getFontName(cat);
            // strcmp, not ==: both sides are const char*, so == compares POINTERS and
            // would have been false for every slot -- a migration that silently did
            // nothing for fonts while looking correct.
            if (FontConfig::getInstance().isOverridden(cat) && def && cur &&
                std::strcmp(cur, def) == 0) {
                FontConfig::getInstance().clearOverride(cat);
                freedFonts++;
            }
        }
        DEBUG_INFO_F("Settings v%d -> v%d: released %d colour and %d font slot(s) to follow the theme",
                     loadedVersion, SETTINGS_VERSION, freedColors, freedFonts);
    }

    // CENTRE-ANCHOR MIGRATION (files written at version <= 6).
    //
    // Notices and Timing stored offsetX as a DELTA from their computed screen-centre
    // while the Gap Bar and Version stored the CENTRE itself -- four panels, two
    // meanings for one key. v7 unifies on the centre (default 0.5), so the stack
    // reads alike in the INI and a width change recentres every member the same way.
    // The stored value gains the 0.5, which is what the semantic change costs.
    //
    // NOT PIXEL-EXACT, and it cannot be. The old left edge was
    // snapEdgeX(0.5 - panelW/2) -- SNAPPED, with grid snapping on by default -- so
    // the old rendered centre was 0.5 plus that snap delta, up to half a cell (the
    // shipped stack measured 0.49775, about 4px at 1920). The new anchor is
    // deliberately unsnapped, because snapping an edge and holding a centre are
    // incompatible quantizations (see BaseHud::centerAnchoredPanelLeft). So an
    // upgraded panel lands on its true centre, and may shift by up to half a cell
    // getting there.
    //
    // The migration could not reproduce the old value even in principle: the snap
    // delta depends on panelW, which depends on the scale, theme and fonts resolved
    // at draw time, none of which exist while an INI is being parsed. The half-cell
    // is the one-time cost of the panels now being where they always claimed to be
    // -- and it lands on Gap Bar and Version too, which need no key shift (they
    // already stored the centre) but shed the same snap.
    //
    // companionX rides the same layout
    // and gets the same shift, only where the companion has actually diverged (the
    // key is absent otherwise, and absent must stay absent -- see
    // captureBaseHudSettings for why that gate is load-bearing).
    //
    // `> 0` agrees with the version dispatch above: a version-less file is treated
    // as CURRENT, so there is nothing to shift.
    if (loadedVersion > 0 && loadedVersion < 7) {
        auto shiftKey = [](SettingsManager::HudSettings& section, const char* key) -> bool {
            auto it = section.find(key);
            if (it == section.end()) return false;
            try {
                const float shifted = std::stof(it->second) + 0.5f;
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%.6f", shifted);
                it->second = buf;
                return true;
            } catch (const std::exception&) {
                return false;   // a hand-edited non-number is left alone, like everywhere
            }
        };
        int shifted = 0;
        for (auto& cache : m_profileCache) {
            for (const char* name : { "NoticesHud", "TimingHud" }) {
                auto it = cache.find(name);
                if (it == cache.end()) continue;
                if (shiftKey(it->second, Keys::Base::OFFSET_X)) ++shifted;
                if (shiftKey(it->second, Keys::Base::COMPANION_X)) ++shifted;
            }
        }
        // m_hudDefaults holds the SAME folded base-section keys (the parse writes
        // both, see above) and is what buildHudSection() writes back as the base
        // section -- with the profile overrides diffed against it. Skip it and the
        // saved file keeps an old-semantics base value forever: a landmine for the
        // hand-editor who deletes a profile override, and a pinned explicit
        // offset in every profile that no future default change can reach.
        // ONLY keys the file's base section carried (baseAnchorKeysInFile): the
        // rest of m_hudDefaults is the factory snapshot, already centre-anchored.
        for (const char* name : { "NoticesHud", "TimingHud" }) {
            auto it = m_hudDefaults.find(name);
            if (it == m_hudDefaults.end()) continue;
            for (const char* key : { Keys::Base::OFFSET_X, Keys::Base::COMPANION_X }) {
                if (!baseAnchorKeysInFile.count(std::string(name) + "|" + key)) continue;
                if (shiftKey(it->second, key)) ++shifted;
            }
        }
        DEBUG_INFO_F("Settings v%d -> v%d: re-anchored %d Notices/Timing offset(s) on the centre",
                     loadedVersion, SETTINGS_VERSION, shifted);
    }

    // RADAR CENTRE-ANCHOR MIGRATION (files written at version <= 7).
    //
    // Same destination as the block above, different arithmetic. Notices and Timing
    // stored a DELTA from their centre, so re-anchoring them was + 0.5 and needed no
    // geometry. The Radar stored its LEFT EDGE, so the shift is half the panel's
    // width -- which depends on the scale stored in the same section.
    //
    // The width is RadarHud's own, so there is one formula rather than a second
    // spelling of it here. It is the UNTHEMED width: exact for every configuration in
    // which the dial artwork is on (artwork bypasses theming -- see
    // resolveActiveTheme) and for THEME_NONE, which is this HUD's default. A radar
    // with its texture switched off AND a theme named lands a few pixels out and can
    // be nudged; nothing else can.
    //
    // It is deliberately half the CONTENT width, not half the drawn box: the old
    // default 0.43275f was 0.5 minus exactly this quantity, so an untouched radar
    // migrates to precisely 0.5. fitPanelToGrid rounds the drawn box up to whole
    // cells, so a radar the user DRAGGED moves by half that rounding -- under 3px at
    // 1080p, and toward centred rather than away from it.
    if (loadedVersion > 0 && loadedVersion < 8) {
        auto shiftByHalfWidth = [](SettingsManager::HudSettings& section, const char* key,
                                   float halfWidth) -> bool {
            auto it = section.find(key);
            if (it == section.end()) return false;
            try {
                const float shifted = std::stof(it->second) + halfWidth;
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%.6f", shifted);
                it->second = buf;
                return true;
            } catch (const std::exception&) {
                return false;   // a hand-edited non-number is left alone, like everywhere
            }
        };
        // One section's worth of shift, reused for the profile caches and for
        // m_hudDefaults -- the latter for the same reason as the v7 block above:
        // it is the base section the file writer emits, and the baseline the
        // profile diffs are computed against. `mayShift` gates per key: the
        // caches hold only file-loaded values (always shiftable), while the
        // defaults map is factory-seeded, already centre-anchored, and may only
        // be shifted where the file's base section overwrote it.
        auto shiftRadarSection = [&](SettingsManager::HudSettings& section,
                                     auto&& mayShift) -> int {
            float scale = 1.0f;
            auto scaleIt = section.find(Keys::Base::SCALE);
            if (scaleIt != section.end()) {
                try { scale = std::stof(scaleIt->second); }
                catch (const std::exception&) { scale = 1.0f; }
            }
            if (!(scale > 0.0f) || !std::isfinite(scale)) scale = 1.0f;
            const float halfWidth = RadarHud::unthemedContentWidth(scale) * 0.5f;
            int n = 0;
            for (const char* key : { Keys::Base::OFFSET_X, Keys::Base::COMPANION_X }) {
                if (!mayShift(key)) continue;
                if (shiftByHalfWidth(section, key, halfWidth)) ++n;
            }
            return n;
        };
        int shifted = 0;
        for (auto& cache : m_profileCache) {
            auto it = cache.find("RadarHud");
            if (it == cache.end()) continue;
            shifted += shiftRadarSection(it->second, [](const char*) { return true; });
        }
        {
            auto it = m_hudDefaults.find("RadarHud");
            if (it != m_hudDefaults.end()) {
                shifted += shiftRadarSection(it->second, [&](const char* key) {
                    return baseAnchorKeysInFile.count(std::string("RadarHud|") + key) > 0;
                });
            }
        }
        DEBUG_INFO_F("Settings v%d -> v%d: re-anchored %d Radar offset(s) on the centre",
                     loadedVersion, SETTINGS_VERSION, shifted);
    }

    // ROW-PITCH DEFAULT MIGRATION (files written at version <= 8).
    //
    // The default uiLineHeight moved from 1.17335 -- the pre-knob shipped pitch,
    // kept while the ratio was newly settable -- to 1.1, a tenth of a row of air
    // between text rows instead of a sixth. See LayoutMetrics::lineHeightRatio.
    //
    // A migration is needed at all because the writer emits this key into EVERY
    // INI whether or not anyone chose it, so nobody would ever see a changed
    // default: the stored 1.17335 would keep winning forever.
    //
    // CONSERVATIVE, exactly like the v5 colour/font unpinning: move only a value
    // that IS the old default, because that is the set carrying no user intent --
    // it is in the file because the old writer wrote it, not because anyone picked
    // it. Any other value was chosen and is left alone. Someone who deliberately
    // set 1.17335 loses their choice here; that is the same trade v5 made, and the
    // alternative is a default nobody can ever be moved off.
    //
    // Acts on the LIVE metric rather than a cache: [Advanced] is applied straight
    // into LayoutConfig by applyGlobalLine during the parse above (there is no
    // per-key cache to rewrite), and this runs after the file is closed, so the
    // value read here is the one the file carried. layoutSetLineHeight re-derives
    // the lattice, and the next save writes the new number.
    if (loadedVersion > 0 && loadedVersion < 9) {
        LayoutMetrics& live = LayoutConfig::getInstance().mutableDefaults();
        if (std::fabs(live.lineHeightRatio -
                      LayoutMetrics::PREV_DEFAULT_LINE_HEIGHT_RATIO) < 1e-4f) {
            layoutSetLineHeight(live, LayoutMetrics{}.lineHeightRatio);
            DEBUG_INFO_F("Settings v%d -> v%d: row pitch follows the new default (%.5f)",
                         loadedVersion, SETTINGS_VERSION, live.lineHeightRatio);
        }
    }

    // GAMEPAD AND PIT BOARD PACK MIGRATION (files written at version <= 5).
    //
    // The pad used to be chosen by TEXTURE VARIANT -- gamepad_widget_1.tga was the
    // Xbox pad and _2 the DualShock -- and the variant number is what the file
    // stores. Packs are named folders now, so the number has to become a name or
    // every upgrading user silently loses their pad.
    //
    // Conservative in the same way the v5 block above is: map only the two variants
    // that ever shipped, and only when the file has no pack name yet. Anything else
    // is LEFT ALONE rather than reset -- a variant this build does not recognise is
    // more likely a hand-edit or a newer file than a mistake to correct, and the
    // widget already degrades an unresolvable pad to the shipped default at render
    // time without touching what is stored.
    //
    // Variant 0 needs no row: it meant "no background texture", which the base-HUD
    // showBackgroundTexture key already carries faithfully, so those users keep the
    // pad switched off and simply pick up the default pack underneath it.
    //
    // `> 0` agrees with the version dispatch for the same reason spelled out above:
    // a version-less file is treated as CURRENT, so there is nothing to migrate.
    if (loadedVersion > 0 && loadedVersion < 6) {
        static const std::pair<const char*, const char*> kVariantToPack[] = {
            {"1", "xbox"},
            {"2", "ds4"},
        };
        // The pit board only ever SHIPPED one texture, so its whole map is the one
        // row -- a user who dropped in a pitboard_hud_2.tga was already outside what
        // shipped, and that unrecognised variant is left alone like any other.
        static const std::pair<const char*, const char*> kVariantToBoard[] = {
            {"1", "classic"},
        };

        auto mapVariant = [](SettingsManager::HudSettings& section, const char* packKey,
                             const std::pair<const char*, const char*>* table, size_t count) -> bool {
            if (section.count(packKey)) return false;                 // already named
            auto variantIt = section.find(Keys::Base::TEXTURE_VARIANT);
            if (variantIt == section.end()) return false;
            for (size_t i = 0; i < count; ++i) {
                if (variantIt->second != table[i].first) continue;
                section[packKey] = table[i].second;
                return true;
            }
            return false;
        };

        int migratedPads = 0, migratedBoards = 0;
        for (auto& cache : m_profileCache) {
            auto padIt = cache.find("GamepadWidget");
            if (padIt != cache.end() &&
                mapVariant(padIt->second, Keys::Gamepad::PACK, kVariantToPack,
                           sizeof(kVariantToPack) / sizeof(kVariantToPack[0]))) {
                ++migratedPads;
            }
            auto boardIt = cache.find("PitboardHud");
            if (boardIt != cache.end() &&
                mapVariant(boardIt->second, Keys::Pitboard::PACK, kVariantToBoard,
                           sizeof(kVariantToBoard) / sizeof(kVariantToBoard[0]))) {
                ++migratedBoards;
            }
        }
        // triggerFillMode CAME OUT of the per-variant layout blocks with the rest of the
        // pad geometry, but unlike the geometry it did not move into the PACK -- it is a
        // display preference, so it became a widget-level key in [GamepadWidget]. The
        // pack migration above therefore does not carry it, and the old
        // [GamepadWidget_Layout_N] sections are no longer parsed into anything that reads
        // them: an upgrading user's choice would be silently dropped and the section
        // pruned from the rewritten file, making it unrecoverable.
        //
        // The old sections DO still reach the cache (parseSectionName treats any unknown
        // [Name] as a base section), so the value is sitting right here to be rescued.
        //
        // WHICH variant's value: the one the user was actually looking at, i.e. the pad
        // their textureVariant selected. Falling back to the lowest-numbered block that
        // has the key covers a file whose variant was never written.
        int migratedFill = 0;
        for (auto& cache : m_profileCache) {
            auto padIt = cache.find("GamepadWidget");
            if (padIt == cache.end()) continue;
            if (padIt->second.count(IniOnly::Gamepad::TRIGGER_FILL_MODE.key)) continue;  // already current

            std::string chosen;
            auto variantIt = padIt->second.find(Keys::Base::TEXTURE_VARIANT);
            if (variantIt != padIt->second.end()) {
                auto sec = cache.find("GamepadWidget_Layout_" + variantIt->second);
                if (sec != cache.end()) {
                    auto fill = sec->second.find("triggerFillMode");
                    if (fill != sec->second.end()) chosen = fill->second;
                }
            }
            if (chosen.empty()) {
                for (int v = 1; v <= 10 && chosen.empty(); ++v) {
                    auto sec = cache.find("GamepadWidget_Layout_" + std::to_string(v));
                    if (sec == cache.end()) continue;
                    auto fill = sec->second.find("triggerFillMode");
                    if (fill != sec->second.end()) chosen = fill->second;
                }
            }
            if (!chosen.empty()) {
                padIt->second[IniOnly::Gamepad::TRIGGER_FILL_MODE.key] = chosen;
                ++migratedFill;
            }
        }

        DEBUG_INFO_F("Settings v%d -> v%d: mapped %d gamepad and %d pitboard texture "
                     "variant(s) onto pack names, rescued %d trigger fill mode(s)",
                     loadedVersion, SETTINGS_VERSION, migratedPads, migratedBoards,
                     migratedFill);
    }

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

// See the declaration. A THIN WRAPPER, and that is the whole point: loadSettingsImpl
// returns early when there is no settings file, which is the FRESH INSTALL -- the
// one case Setup's opt-out marker exists for. Hanging applyInstallPrefs() off the
// tail of the implementation meant it ran on every launch that did not need it and
// was skipped on the launch that did, silently, with the second launch behaving
// correctly so the feature looked fine to anyone who checked twice.
//
// One exit and one call, so a future early return cannot reintroduce that. It runs
// LAST either way: installPrefsSeen and the stored Analytics value are both applied
// by then, so this either overrides a value it is entitled to override or does
// nothing at all.
void SettingsManager::loadSettings(HudManager& hudManager, const char* savePath) {
    loadSettingsImpl(hudManager, savePath);
    applyInstallPrefs();
}

// See the declaration and core/install_prefs.h. Reads the marker Setup may have
// left beside the plugin and honours it once.
//
// Marks the settings dirty rather than writing immediately: the choice then
// persists through the SAME deferred save every other setting uses, so a fresh
// install does not write the settings file from inside loadSettings().
void SettingsManager::applyInstallPrefs() {
#if GAME_HAS_ANALYTICS
    std::string text;
    try {
        std::ifstream f(InstallPrefs::kPath, std::ios::binary);
        if (!f.is_open()) return;   // the overwhelmingly common case
        std::ostringstream buf;
        buf << f.rdbuf();
        text = buf.str();
    } catch (...) {
        DEBUG_WARN("Install prefs: unreadable, ignoring");
        return;
    }

    const InstallPrefs::Prefs prefs = InstallPrefs::parse(text);
    if (!InstallPrefs::shouldApply(prefs, m_installPrefsSeen)) return;

    AnalyticsManager::getInstance().setEnabled(false);
    m_installPrefsSeen = InstallPrefs::stampToRecord(prefs);
    markDirty();
    DEBUG_INFO_F("Install prefs: analytics disabled by Setup (stamp '%s')",
                 m_installPrefsSeen.c_str());
#endif
}

// See the declaration: one owner for the export folder.
std::string SettingsManager::getBenchmarksDir() const {
    if (m_savePath.empty()) return std::string();
    std::string dir = m_savePath;
    if (dir.back() != '/' && dir.back() != '\\') dir += '\\';
    dir += "mxbmrp3";
    // Idempotent, and both levels are needed: AtomicFileWriter does not create parents.
    CreateDirectoryA(dir.c_str(), nullptr);
    dir += "\\benchmarks";
    CreateDirectoryA(dir.c_str(), nullptr);
    return dir;
}
