// ============================================================================
// core/settings_manager.h
// Manages persistence of HUD settings (position, scale, visibility, etc.)
// Supports per-profile settings (Practice, Race, Spectate)
// ============================================================================
#pragma once

#include "../game/game_config.h"   // GAME_HAS_* — gates the per-HUD registry decls (.inc)
#include "profile_manager.h"
#include <array>
#include <string>
#include <unordered_map>
#include <map>
#include <vector>
#include <iosfwd>

// Forward declarations
class HudManager;

// The per-HUD serializer registry (settings_hud_registry.h). Forward-declared here
// so hudSectionRegistry() can be a friend of SettingsManager, letting it reference
// the private static cap_*/app_* members below.
namespace Settings {
    struct HudSectionSerializer;
    const std::vector<HudSectionSerializer>& hudSectionRegistry();
}

class SettingsManager {
public:
    static SettingsManager& getInstance();

    // Type alias for HUD settings (key -> value map)
    using HudSettings = std::map<std::string, std::string>;

    // Profile settings cache: hudName -> (key -> value). Public so the per-HUD
    // serializer registry (settings_hud_registry) can carry capture/apply function
    // pointers over it. Populated per profile in m_profileCache.
    using ProfileCache = std::unordered_map<std::string, HudSettings>;

    // Load settings from disk (call during plugin initialization)
    void loadSettings(HudManager& hudManager, const char* savePath);

    // Save settings to disk synchronously (temp-file + atomic replace on the calling
    // thread). Use for the paths that must be durable before returning: explicit Save,
    // Reset-to-defaults, and plugin shutdown. Also clears the dirty flag.
    void saveSettings(const HudManager& hudManager, const char* savePath);

    // Mark settings as changed WITHOUT writing to disk. The frequent auto-save path — a HUD
    // drag/scale, a toggle, a hotkey binding — calls this. Serializing settings costs a couple
    // of milliseconds (capture every HUD + build the whole INI), which is a visible frame spike
    // if done while the player is on track. So the write is DEFERRED: the live change is applied
    // immediately (visible), only the persistence waits. flushIfDirty() writes it later, at a
    // moment when a hitch doesn't matter — leaving the track (pit/exit) or shutdown. If the game
    // crashes on track, unsaved edits are lost, which is acceptable for HUD settings (the game's
    // own unsaved state would be lost too).
    void markDirty() { m_settingsDirty = true; }

    // True if settings changed since the last save (unsaved changes pending). Drives the
    // settings-panel Save button: lit + clickable when dirty, grayed "Saved" when clean.
    bool isDirty() const { return m_settingsDirty; }

    // Auto-flush on the track->off-track transitions (RunStop = pits, RunDeinit = exit): if
    // Auto-Save is ON and settings are dirty, serialize + write them now (synchronous, atomic)
    // where a frame hitch is invisible — never during active riding. No-op if Auto-Save is off
    // (manual mode: the user persists via the Save button) or nothing changed. Uses the stored
    // save path from loadSettings().
    void flushIfDirty(const HudManager& hudManager);

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

#if defined(MXBMRP3_TEST_BUILD)
    // Test/benchmark only: crank the cost-driving settings of the heavy HUDs to
    // their maximum (all columns/rows/events, max row counts, long names) so the
    // headless benchmark driver can profile the plugin's WORST-CASE per-frame
    // rebuild cost, not just default settings. Not part of any in-game flow;
    // compiled out of every shipping DLL.
    void testMaxAllHudSettings(HudManager& hudManager);
#endif

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

    // Emit one analytics flag per HUD/widget: key "hud_<name>" / "widget_<name>"
    // (the class suffix stripped and PascalCase -> snake_case), value 1 if
    // currently visible else 0. Derived from the canonical settings capture, so
    // a newly added HUD/widget appears automatically with no separate list to
    // maintain. Sorted by key. Used by AnalyticsManager for feature-usage stats.
    void getHudWidgetFlags(const HudManager& hudManager,
                           std::vector<std::pair<std::string, int>>& outFlags);

    // Store the save path for later use (set during loadSettings)
    const std::string& getSavePath() const { return m_savePath; }

    // Developer mode - shows debug options in UI when enabled via INI
    bool isDeveloperMode() const { return m_developerMode; }
    void setDeveloperMode(bool enabled) { m_developerMode = enabled; }

#if defined(MXBMRP3_TEST_BUILD)
    // Test-only: the section names captureToCache() produces for the current live
    // HUDs (sorted). Used by settings_sections_test to assert that every captured
    // section is actually serialized — i.e. the capture list and the serialize
    // order list (serializeSettings) cannot silently diverge, which is exactly the
    // "third hardcoded list" trap that dropped FriendsHud. Never compiled into a
    // shipping DLL (gated on MXBMRP3_TEST_BUILD, like test_hooks.cpp).
    std::vector<std::string> testCapturedSectionNames(const HudManager& hudManager);
#endif

private:
    SettingsManager() = default;
    ~SettingsManager() = default;
    SettingsManager(const SettingsManager&) = delete;
    SettingsManager& operator=(const SettingsManager&) = delete;

    // The registry accessor builds its table from the per-HUD cap_*/app_* members
    // below; as a friend it can take their addresses even though they are private.
    friend const std::vector<Settings::HudSectionSerializer>& Settings::hudSectionRegistry();

    // Per-HUD capture/apply serializers (defined in settings_hud_registry.cpp).
    // Static MEMBERS of SettingsManager so they inherit its `friend`-ship with the
    // HUD classes (they read/write private HUD members), while the registry table
    // in settings_hud_registry.cpp iterates them for capture, apply, and serialize.
    // Each name is registered exactly once (one table row), so a HUD can never be
    // captured/applied without also being serialized (the FriendsHud trap).
#include "settings_hud_registry_decls.inc"

    std::string getSettingsFilePath(const char* savePath) const;

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

    // Capture live state to the active profile, then serialize the full settings file to a
    // string. The single serialization path shared by saveSettings() (sync write) and
    // flushIfDirty(). Sets m_savePath as a side effect (like the old saveSettings).
    std::string serializeSettings(const HudManager& hudManager, const char* savePath);

    // Build one HUD/widget's serialized block: its base [Section] plus the sparse per-profile
    // [Section:Profile] overrides. Returns "" for a section absent from m_hudDefaults (a
    // game-gated HUD not in this build). Reads only cached state, so it is const.
    std::string buildHudSection(const char* hudName) const;

    // Build the GamepadWidget / PitboardHud per-variant layout blocks (read live from the widgets).
    std::string buildGamepadLayouts(const HudManager& hudManager) const;
    std::string buildPitboardLayouts(const HudManager& hudManager) const;

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

    // Set by markDirty() when a HUD setting changes; cleared by saveSettings()/flushIfDirty()
    // once written. The write itself is deferred to a track->off-track transition so it never
    // spikes a gameplay frame (see markDirty).
    bool m_settingsDirty = false;
};
