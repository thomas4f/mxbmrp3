// ============================================================================
// core/settings_manager_save.cpp
// SettingsManager's write path: serializeSettings() (the whole INI as text,
// a pure reading of the live setup), buildHudSection() (one HUD's base block
// plus its sparse per-profile overrides), and the two writers around it,
// saveSettings() and the leave-track flushIfDirty(). Moved verbatim out of
// settings_manager.cpp when it crossed the file budget; the class and its API
// are unchanged. The loader and its migrations stay in settings_manager.cpp.
// ============================================================================
#include "settings_manager.h"
#include "settings_manager_internal.h"
#include "settings_keys.h"
#include "settings_serde.h"
#include "settings_hud_registry.h"
#include "atomic_file_writer.h"
#include "exploration_stats.h"
#include "hud_manager.h"
#include "profile_manager.h"
#include "stats_manager.h"
#include "tracked_riders_manager.h"
#include "ui_config.h"
#include "../hud/settings_hud.h"
#include "../diagnostics/logger.h"

#include <sstream>
#include <string>

// The `Keys::...` / `IniOnly::...` references and the serde helpers resolve as
// they did in settings_manager.cpp.
using namespace Settings;
using namespace SettingsInternal;

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
    // on-disk serialization in exactly one place -- a HUD missing from a parallel
    // list would silently drop its settings on restart. settings_sections_test.cpp
    // asserts capture ⊆ serialized as a belt-and-suspenders guard.
    // Note: HelmetOverlayHud is global (own [HelmetOverlay] section), not per-profile.
    // Game-gated HUDs are #if'd out of the registry on builds without them, and
    // buildHudSection() returns "" for any section absent from m_hudDefaults.
    for (const Settings::HudSectionSerializer& s : Settings::hudSectionRegistry()) {
        file << buildHudSection(s.name);
    }

    std::string data = file.str();
    data += ExplorationStats::fingerprintTrailer(data);   // last, so nothing follows it
    return data;
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
    // What the setup being written says (Tyre Kicker, Keymaster, Test Pilot...):
    // here, on the write, never in serializeSettings(), which other callers use
    // to READ the setup and must stay a pure function of it.
    StatsManager::getInstance().exploration().observeSettings(hudManager);
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
