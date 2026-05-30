// ============================================================================
// core/settings_manager.h
// Manages persistence of HUD settings (position, scale, visibility, etc.)
// Supports per-profile settings (Practice, Race, Spectate)
// ============================================================================
#pragma once

#include "profile_manager.h"
#include <array>
#include <string>
#include <unordered_map>
#include <map>
#include <vector>
#include <iosfwd>

// Forward declarations
class HudManager;

class SettingsManager {
public:
    static SettingsManager& getInstance();

    // Type alias for HUD settings (key -> value map)
    using HudSettings = std::map<std::string, std::string>;

    // Load settings from disk (call during plugin initialization)
    void loadSettings(HudManager& hudManager, const char* savePath);

    // Save settings to disk (call when settings change)
    void saveSettings(const HudManager& hudManager, const char* savePath);

    // Profile switching - captures current HUD state to old profile, applies new profile
    // Returns true if profile actually changed
    bool switchProfile(HudManager& hudManager, ProfileType newProfile);

    // Copy current profile's settings to all other profiles
    void applyToAllProfiles(HudManager& hudManager);

    // Reset every profile to the pristine factory snapshot (m_hudFactoryDefaults, this
    // build's constructor defaults), apply it to the live HUDs, and persist. Unlike
    // applyToAllProfiles (which copies the current, possibly-overridden state), this returns
    // every INI-controllable setting to its default. Also re-seeds m_hudDefaults to factory,
    // so a user's hand-edited base [HudName] keys are replaced by this build's defaults (a
    // full factory reset intentionally discards them, matching the global reset).
    void resetAllToFactoryDefaults(HudManager& hudManager);

    // Reset the named HUDs (active profile only) to the factory-default snapshot.
    // Covers every INI-controllable setting — including per-HUD color/font overrides
    // — without relying on the HUD's own resetToDefaults(). Does NOT persist; the
    // caller saves if needed. HUDs not present in the snapshot (e.g. the global
    // HelmetOverlay) are skipped.
    //
    // keepVisibility=true (default) preserves each HUD's current visibility so a
    // single-HUD tab's Reset doesn't hide the element the user is positioning. The
    // Widgets tab passes false: its per-widget "Visible" toggles are tab config the
    // user expects Reset to restore to factory defaults along with everything else.
    void resetHudsToFactoryDefaults(HudManager& hudManager, const std::vector<std::string>& hudNames, bool keepVisibility = true);

    // Reset ALL per-profile HUDs/widgets for the active profile to the factory-default
    // snapshot (visibility included) and apply to the live HUDs. Global state (colors,
    // fonts, hotkeys, rumble config, helmet overlay) and other profiles are untouched.
    // Does NOT persist; the caller saves if needed.
    void resetActiveProfileToFactoryDefaults(HudManager& hudManager);

    // Reset every GLOBAL (non-per-profile) setting — colors, fonts, hotkeys, rumble,
    // helmet overlay, display units, and all [General]/[Advanced] tunables/toggles — to
    // the factory-default snapshot captured at startup, and apply it to live state. This
    // replays the snapshot through the same applier loadSettings() uses, so reset, save,
    // and load all share one serialization and cannot drift as new global settings are
    // added. Developer mode (an INI-only power-user flag) is intentionally preserved.
    // Per-profile HUDs are handled separately by resetAllToFactoryDefaults(). Does NOT
    // persist; the caller saves if needed.
    void resetGlobalsToFactoryDefaults(HudManager& hudManager);

    // Reset only the named GLOBAL sections (e.g. {"Display", "Fonts", "Colors"}) to the
    // factory-default snapshot, leaving every other global section untouched. Used by the
    // per-tab "Reset <tab>" buttons for tabs whose settings map 1:1 to a global INI section
    // (Appearance -> [Display]/[Fonts]/[Colors], Rumble -> [Rumble], Helmet -> [HelmetOverlay],
    // Hotkeys -> [Hotkeys]). Shares the same replay path as resetGlobalsToFactoryDefaults(),
    // so a per-tab reset can't drift from save/load either. Section names match the INI
    // headers. Does NOT persist; the caller saves if needed.
    void resetGlobalSectionsToFactoryDefaults(HudManager& hudManager, const std::vector<std::string>& sections);

    // Copy current profile's settings to a specific target profile
    void copyToProfile(HudManager& hudManager, ProfileType targetProfile);

    // Capture current HUD state to the active profile's cache
    void captureCurrentState(const HudManager& hudManager);

    // Apply cached settings for active profile to HUDs
    void applyActiveProfile(HudManager& hudManager);

    // Store the save path for later use (set during loadSettings)
    const std::string& getSavePath() const { return m_savePath; }

    // Developer mode - shows debug options in UI when enabled via INI
    bool isDeveloperMode() const { return m_developerMode; }
    void setDeveloperMode(bool enabled) { m_developerMode = enabled; }

private:
    SettingsManager() = default;
    ~SettingsManager() = default;
    SettingsManager(const SettingsManager&) = delete;
    SettingsManager& operator=(const SettingsManager&) = delete;

    std::string getSettingsFilePath(const char* savePath) const;

    // Profile settings cache: profile -> hudName -> key -> value
    using ProfileCache = std::unordered_map<std::string, HudSettings>;

    // Capture HUD state to a specific profile's cache
    void captureToProfile(const HudManager& hudManager, ProfileType profile);

    // Apply a specific profile's cached settings to HUDs
    void applyProfile(HudManager& hudManager, ProfileType profile);

    // Helper: capture all HUD settings to a cache (shared by captureToProfile and captureFactoryDefaults)
    void captureToCache(const HudManager& hudManager, ProfileCache& cache);

    // Serialize all GLOBAL (non-per-profile) sections — [General], [Advanced], [Display],
    // [Colors], [Fonts], [Rumble], [HelmetOverlay], [Hotkeys] — to a stream. Single source
    // of truth shared by saveSettings() (writes the file) and captureFactoryDefaults()
    // (captures the startup snapshot into m_globalDefaultsIni).
    void writeGlobalSettings(std::ostream& out, const HudManager& hudManager) const;

    // Apply one parsed key/value belonging to a global section to the live singletons.
    // Returns true if the section was a recognized global section (so the caller stops
    // parsing the line further). Shared by loadSettings() and resetGlobalsToFactoryDefaults().
    bool applyGlobalLine(const std::string& section, const std::string& key,
                         const std::string& value, HudManager& hudManager);

    // Replay the captured global-defaults snapshot through applyGlobalLine(). When
    // sectionFilter is null, every section is applied; otherwise only sections whose name
    // appears in the filter. Shared by resetGlobalsToFactoryDefaults() (full) and
    // resetGlobalSectionsToFactoryDefaults() (per-tab). Developer mode is preserved.
    void replayGlobalDefaults(HudManager& hudManager, const std::vector<std::string>* sectionFilter);

    // --- Cached state ---
    std::array<ProfileCache, static_cast<size_t>(ProfileType::COUNT)> m_profileCache;

    // HUD defaults cache: hudName -> key -> default value
    // Used to write sparse INI files (only non-default values per profile). NOTE: at load,
    // user-edited base [HudName] section keys are folded into this map (so they round-trip
    // as the save baseline). It is therefore NOT a clean factory snapshot — use
    // m_hudFactoryDefaults for reset.
    ProfileCache m_hudDefaults;

    // Immutable factory snapshot of per-HUD settings, copied from m_hudDefaults in
    // captureFactoryDefaults() BEFORE the user's file is parsed — i.e. pure constructor
    // state, never folded with base-section edits. The reset paths replay from this so
    // "reset to defaults" means this build's defaults (symmetric with m_globalDefaultsIni
    // for globals), not whatever the file happened to declare. This is what makes a HUD
    // default improved in a new version actually take effect on Reset.
    ProfileCache m_hudFactoryDefaults;
    bool m_factoryDefaultsCaptured = false;

    // Factory-default snapshot of the global sections, captured as INI text in
    // captureFactoryDefaults() (before the user's file is parsed, while every singleton
    // still holds its constructor defaults). Replayed by resetGlobalsToFactoryDefaults().
    std::string m_globalDefaultsIni;

    // Track whether settings have been loaded (for assertion in captureFactoryDefaults)
    bool m_settingsLoaded = false;

    // Capture factory defaults from HudManager (call once at initialization, BEFORE loading settings)
    // IMPORTANT: Must be called before loadSettings() - asserts if called after
    void captureFactoryDefaults(const HudManager& hudManager);

    // Track if cache has been initialized (vs empty due to fresh install)
    bool m_cacheInitialized = false;

    // Stored save path for saving on profile switch
    std::string m_savePath;

    // Developer mode flag (shows debug options in UI)
    bool m_developerMode = false;
};
