// ============================================================================
// core/settings_hud_profiles.cpp
// Per-profile HUD settings for SettingsManager: capturing live HUD state into
// the profile cache (captureToCache / captureToProfile / captureFactoryDefaults)
// and applying a cached profile back onto the live HUDs (applyProfile), plus the
// profile-level operations built on them (switch, copy-to-all, and the reset
// family). Split out of settings_manager.cpp, which keeps global-section
// serialization, the file serialize/build helpers, and save/load orchestration.
// ============================================================================
#include "settings_manager.h"
#include "../hud/settings/whats_new.h"
#include "settings_keys.h"
#include "settings_serde.h"
#include "settings_hud_registry.h"
#include "atomic_file_writer.h"
#include "hud_manager.h"
#include "profile_manager.h"
#include "../diagnostics/logger.h"
// settings_hud.h is core (every game has the settings menu, and getSettingsHud() is
// used unconditionally below), and it pulls records_hud.h itself; both .cpp files are
// compiled on every game, so neither include may be gated on GAME_HAS_RECORDS_PROVIDER
// — gating it broke the GPB/KRP builds (SettingsHud left incomplete -> C2027). The
// *provider* feature stays runtime/registration-gated; only these includes are always on.
#include "../hud/settings_hud.h"
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

void SettingsManager::captureCurrentState(const HudManager& hudManager) {
    ProfileType activeProfile = ProfileManager::getInstance().getActiveProfile();
    captureToProfile(hudManager, activeProfile);
}

#if defined(MXBMRP3_TEST_BUILD)
// Test-only (see settings_manager.h): the sections captureToCache() produces.
std::vector<std::string> SettingsManager::testCapturedSectionNames(const HudManager& hudManager) {
    ProfileCache tmp;
    captureToCache(hudManager, tmp);
    std::vector<std::string> names;
    names.reserve(tmp.size());
    for (const auto& kv : tmp) names.push_back(kv.first);
    std::sort(names.begin(), names.end());
    return names;
}
#endif

// Helper: capture all HUD settings to a cache (shared by captureToProfile and captureFactoryDefaults)
void SettingsManager::captureToCache(const HudManager& hudManager, ProfileCache& cache) {
    cache.clear();

    // Capture every registered section via the single per-HUD serializer registry
    // (settings_hud_registry) — the one ordered list that also drives applyProfile
    // and serializeSettings.
    for (const auto& s : Settings::hudSectionRegistry()) {
        s.capture(hudManager, cache, s.name);
    }
}

void SettingsManager::captureToProfile(const HudManager& hudManager, ProfileType profile) {
    if (profile >= ProfileType::COUNT) {
        DEBUG_WARN_F("captureToProfile called with invalid profile index: %d", static_cast<int>(profile));
        return;
    }
    captureToCache(hudManager, m_profileCache[static_cast<size_t>(profile)]);
    m_cacheInitialized = true;
}

void SettingsManager::captureFactoryDefaults(const HudManager& hudManager) {
    // Runtime check for release builds, assert for debug builds
    if (m_settingsLoaded) {
        DEBUG_WARN("captureFactoryDefaults() called after loadSettings() - using current values instead of defaults");
        return;
    }
    assert(!m_settingsLoaded && "captureFactoryDefaults() must be called before loadSettings()");

    if (m_factoryDefaultsCaptured) {
        return;  // Already captured
    }

    captureToCache(hudManager, m_hudDefaults);

    // Snapshot the pristine per-HUD constructor defaults for the reset paths. m_hudDefaults
    // is about to accumulate user-edited base-section keys during loadSettings() (the fold at
    // the [HudName] base-section handler), which is correct for sparse-save round-tripping but
    // would make "reset to defaults" restore the file's baseline instead of this build's
    // defaults. Keep an untouched copy so reset means factory, symmetric with m_globalDefaultsIni.
    m_hudFactoryDefaults = m_hudDefaults;

    // Capture the global (non-per-profile) sections as INI text now, so the reset paths can
    // replay them later. Reusing writeGlobalSettings() keeps this snapshot in lockstep with
    // save. INVARIANT: every subsystem must already hold its factory-default state when this
    // runs. Most do via their constructor, but some establish defaults in an initialize()
    // instead — notably HotkeyManager::initialize() sets the default key bindings (e.g. the
    // settings-toggle key). plugin_manager.cpp calls those initialize()s before HudManager's,
    // which is what triggers loadSettings()/this capture. If init is ever reordered so this
    // runs first, the snapshot would capture "unset" defaults and reset would wipe those
    // bindings — so keep subsystem default-init ahead of loadSettings().
    {
        std::ostringstream globalDefaults;
        writeGlobalSettings(globalDefaults, hudManager);
        m_globalDefaultsIni = globalDefaults.str();
    }
    m_factoryDefaultsCaptured = true;
    DEBUG_INFO("Captured HUD factory defaults for sparse INI format");
}

// ============================================================================
// applyActiveProfile / applyProfile - Apply cached settings to HUDs
// ============================================================================

void SettingsManager::applyActiveProfile(HudManager& hudManager) {
    ProfileType activeProfile = ProfileManager::getInstance().getActiveProfile();
    applyProfile(hudManager, activeProfile);
}

void SettingsManager::applyProfile(HudManager& hudManager, ProfileType profile) {
    if (profile >= ProfileType::COUNT) {
        DEBUG_WARN_F("applyProfile called with invalid profile index: %d", static_cast<int>(profile));
        return;
    }
    if (!m_cacheInitialized) {
        DEBUG_INFO("applyProfile skipped - cache not yet initialized (normal during first load)");
        return;
    }

    const ProfileCache& cache = m_profileCache[static_cast<size_t>(profile)];

    // Apply every registered section via the same single registry that capture and
    // serialize use, so a section is registered for all three in exactly one place.
    for (const auto& s : Settings::hudSectionRegistry()) {
        s.apply(hudManager, cache, s.name);
    }


    // Note: ColorConfig is now global (not per-profile) - loaded once in loadSettings

    // Rebuild all dirty HUDs immediately so quads reflect new settings before render
    hudManager.rebuildAllIfDirty();

    DEBUG_INFO_F("Applied profile: %s", ProfileManager::getProfileName(profile));
}

bool SettingsManager::switchProfile(HudManager& hudManager, ProfileType newProfile) {
    ProfileManager& profileManager = ProfileManager::getInstance();
    ProfileType oldProfile = profileManager.getActiveProfile();

    if (newProfile == oldProfile) return false;
    if (newProfile >= ProfileType::COUNT) {
        DEBUG_WARN_F("switchProfile called with invalid profile index: %d", static_cast<int>(newProfile));
        return false;
    }

    // Capture current state to old profile
    captureToProfile(hudManager, oldProfile);

    // Switch active profile
    profileManager.setActiveProfile(newProfile);

    // Apply new profile settings
    applyProfile(hudManager, newProfile);

    // Save to disk
    // Deferred: the change is applied live now; persistence waits for the leave-track flush
    // (or the manual Save button), so profile switches / applies / resets never write on track.
    markDirty();

    return true;
}

void SettingsManager::applyToAllProfiles(HudManager& hudManager) {
    ProfileType activeProfile = ProfileManager::getInstance().getActiveProfile();

    // Capture current HUD state to active profile (ensure it's up to date)
    captureToProfile(hudManager, activeProfile);

    // Copy active profile's cache to all other profiles
    const ProfileCache& sourceCache = m_profileCache[static_cast<size_t>(activeProfile)];
    for (int i = 0; i < static_cast<int>(ProfileType::COUNT); ++i) {
        ProfileType targetProfile = static_cast<ProfileType>(i);
        if (targetProfile != activeProfile) {
            m_profileCache[static_cast<size_t>(i)] = sourceCache;
        }
    }

    // Save to disk
    // Deferred: the change is applied live now; persistence waits for the leave-track flush
    // (or the manual Save button), so profile switches / applies / resets never write on track.
    markDirty();

    DEBUG_INFO_F("Applied %s profile settings to all profiles", ProfileManager::getProfileName(activeProfile));
}

void SettingsManager::resetAllToFactoryDefaults(HudManager& hudManager) {
    if (!m_factoryDefaultsCaptured) {
        // Shouldn't happen post-load, but fall back to applyToAllProfiles rather
        // than wiping every profile to an empty cache.
        DEBUG_WARN("resetAllToFactoryDefaults: factory defaults not captured, falling back to applyToAllProfiles");
        applyToAllProfiles(hudManager);
        return;
    }

    // Seed every profile with the pristine factory snapshot (this build's constructor
    // defaults), not m_hudDefaults — the latter has the file's base-section edits folded in,
    // which would make a full factory reset preserve stale/old-version defaults. This covers
    // every INI-controllable per-HUD setting (including INI-only overrides) regardless of
    // whether an individual HUD's resetToDefaults() happens to reset it.
    for (auto& cache : m_profileCache) {
        cache = m_hudFactoryDefaults;
    }
    // Re-seed the sparse-save baseline to factory too, so the rewritten base [HudName]
    // sections reflect this build's defaults and every profile collapses to a minimal diff.
    // (This is the path that intentionally discards user base-section edits — a full factory
    // reset is exactly when that's wanted.)
    m_hudDefaults = m_hudFactoryDefaults;
    m_cacheInitialized = true;

    // Push the active profile's (now default) settings onto the live HUDs.
    applyActiveProfile(hudManager);

    // Persist. saveSettings re-captures the (now default) live state into the
    // active profile and rewrites the file from scratch, so stale per-profile
    // overrides are dropped (and stale base-section defaults are replaced with
    // this build's factory defaults).
    // Deferred: the change is applied live now; persistence waits for the leave-track flush
    // (or the manual Save button), so profile switches / applies / resets never write on track.
    markDirty();

    DEBUG_INFO("Reset all profiles to factory defaults");
}

void SettingsManager::resetHudsToFactoryDefaults(HudManager& hudManager, const std::vector<std::string>& hudNames, bool keepVisibility) {
    if (!m_factoryDefaultsCaptured || !m_cacheInitialized) {
        return;  // Nothing to restore from yet (shouldn't happen post-load).
    }

    ProfileType active = ProfileManager::getInstance().getActiveProfile();

    // Sync the active-profile cache with the live HUDs first. applyActiveProfile()
    // below re-applies the whole cache, so this ensures HUDs we're NOT resetting
    // keep their current (possibly unsaved) state instead of being reverted.
    captureToProfile(hudManager, active);

    ProfileCache& cache = m_profileCache[static_cast<size_t>(active)];
    for (const auto& name : hudNames) {
        // Source from the pristine factory snapshot, not m_hudDefaults (which has file
        // base-section edits folded in). A per-tab reset restores only the active profile,
        // so we deliberately leave m_hudDefaults untouched — other profiles still use it as
        // their save baseline; the reset values just get written as explicit profile diffs.
        auto defIt = m_hudFactoryDefaults.find(name);
        if (defIt == m_hudFactoryDefaults.end()) continue;  // not a per-profile HUD (e.g. HelmetOverlay)

        HudSettings& hudCache = cache[name];

        // For single-HUD tabs, a reset shouldn't hide the element the user is
        // positioning, so keep its current visibility (full "Reset all settings"
        // resets visibility instead). The Widgets tab opts out via keepVisibility=
        // false: its per-widget "Visible" toggles are tab config that Reset should
        // restore to factory defaults like everything else.
        std::string keepVisible;
        if (keepVisibility) {
            auto visIt = hudCache.find(Keys::Base::VISIBLE);
            if (visIt != hudCache.end()) keepVisible = visIt->second;
        }

        hudCache = defIt->second;  // pristine factory defaults (no base-section edits)
        if (!keepVisible.empty()) hudCache[Keys::Base::VISIBLE] = keepVisible;
    }

    // Push the updated cache onto the live HUDs. applyBaseHudSettings is authoritative,
    // so per-profile color/font overrides absent from the defaults are cleared too.
    applyActiveProfile(hudManager);
}

void SettingsManager::resetActiveProfileToFactoryDefaults(HudManager& hudManager) {
    if (!m_factoryDefaultsCaptured || !m_cacheInitialized) {
        return;  // Nothing to restore from yet (shouldn't happen post-load).
    }

    ProfileType active = ProfileManager::getInstance().getActiveProfile();

    // Replace the whole active-profile cache with the pristine factory snapshot (this build's
    // defaults), not m_hudDefaults (file base-section edits folded in). This covers every
    // per-profile HUD/widget (including INI-only members and color/font overrides) in one shot.
    // Only the active profile is reset, so m_hudDefaults (the shared save baseline for the
    // other profiles) is left untouched. Global sections (colors/fonts/hotkeys) and other
    // profiles aren't in this cache either, so they're left untouched.
    m_profileCache[static_cast<size_t>(active)] = m_hudFactoryDefaults;
    applyActiveProfile(hudManager);
}

void SettingsManager::replayGlobalDefaults(HudManager& hudManager, const std::vector<std::string>* sectionFilter,
                                           const std::vector<GlobalKeyRef>* keyFilter, bool keysAreExclusions) {
    if (m_globalDefaultsIni.empty()) {
        // Snapshot not captured yet (shouldn't happen post-load); nothing to restore.
        return;
    }

    // Developer mode is an INI-only power-user flag; no reset path touches it even though it
    // lives in [Advanced]. Preserve it across the snapshot replay.
    const bool developerMode = m_developerMode;

    // ...and so are the dismissed what's-new markers, for the same reason: they are a
    // record of what this player has already been shown, not a setting they chose.
    // Restoring them would re-decorate the menu with tags for features they went and
    // looked at, which reads as the plugin having forgotten rather than as a reset.
    // The snapshot is captured BEFORE the file loads, so its whatsNewSeen is empty and
    // replaying it would otherwise clear the set. (To see the markers again on purpose:
    // clear whatsNewSeen in the INI.)
    const std::string whatsNewSeen = WhatsNew::serialize();
    // The update tag's seen-version rides along for the same reason: it records what
    // this player has already been SHOWN, not a setting they chose, so a factory
    // reset that re-armed a tag for an update they went and looked at would read as
    // the plugin having forgotten. (The SKIP version is a choice and is reset.)
    const std::string updateTagSeen = UpdateChecker::getInstance().getUpdateTagSeenVersion();

    // Colours and fonts are SPARSE in the snapshot -- only slots the user pinned are
    // written -- so absence means "follow the theme" and the replay alone cannot
    // express it. Clear the overrides first, exactly like the per-HUD apply clears
    // authoritatively when a key is absent; the replay then re-pins whatever the
    // snapshot does carry. Without this, "reset" would leave every override standing,
    // because there is no line in the snapshot saying to remove it.
    //
    // GATED ON THE FILTER, because this function is also the per-TAB reset. Ungated, the
    // clear ran before the section filter was consulted, so "Reset Hotkeys" -- or Rumble,
    // Helmet, Updates, Director -- wiped every colour and font the user had pinned on the
    // Appearance tab: the filter then kept the [Colors]/[Fonts] lines OUT of the replay,
    // so nothing put them back. A tab reset must touch that tab's sections and nothing
    // else.
    const auto sectionSelected = [&](const char* name) {
        return !sectionFilter ||
               std::find(sectionFilter->begin(), sectionFilter->end(), name) != sectionFilter->end();
    };
    // Not reachable from the key-list path (see resetGlobalKeysToFactoryDefaults): an
    // include-list never names a colour or font, and clearing every override for a tab
    // that asked for two [Display] keys is exactly the over-reach the gate above fixed.
    const bool clearsSparseOverrides = !(keyFilter && !keysAreExclusions);
    if (clearsSparseOverrides && sectionSelected("Colors")) {
        for (int i = 0; i < static_cast<int>(ColorSlot::COUNT); ++i) {
            ColorConfig::getInstance().clearOverride(static_cast<ColorSlot>(i));
        }
    }
    if (clearsSparseOverrides && sectionSelected("Fonts")) {
        for (int i = 0; i < static_cast<int>(FontCategory::COUNT); ++i) {
            FontConfig::getInstance().clearOverride(static_cast<FontCategory>(i));
        }
    }

    // Replay the captured default INI through the same applier loadSettings() uses, so the
    // reset path covers every global setting that save/load cover, automatically.
    std::istringstream stream(m_globalDefaultsIni);
    std::string line;
    std::string section;
    while (std::getline(stream, line)) {
        size_t start = line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue;
        size_t end = line.find_last_not_of(" \t\r\n");
        line = line.substr(start, end - start + 1);
        if (line.empty() || line[0] == '#') continue;

        if (line.front() == '[' && line.back() == ']') {
            section = line.substr(1, line.length() - 2);
            continue;
        }

        // Skip lines outside the requested sections (null filter = apply everything).
        if (sectionFilter &&
            std::find(sectionFilter->begin(), sectionFilter->end(), section) == sectionFilter->end()) {
            continue;
        }

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
            size_t valueEnd = value.find_last_not_of(" \t");
            value = (valueEnd != std::string::npos) ? value.substr(0, valueEnd + 1) : std::string();
        }

        if (keyFilter) {
            bool listed = false;
            for (const GlobalKeyRef& k : *keyFilter) {
                if (section == k.section && key == k.key) { listed = true; break; }
            }
            // Exclusions: the listed keys are the ones to skip. Include-list: the
            // listed keys are the only ones to apply.
            if (keysAreExclusions == listed) continue;
        }

        applyGlobalLine(section, key, value, hudManager);
    }

    m_developerMode = developerMode;
    WhatsNew::deserialize(whatsNewSeen);
    UpdateChecker::getInstance().setUpdateTagSeenVersion(updateTagSeen);
}

void SettingsManager::resetGlobalsToFactoryDefaults(HudManager& hudManager) {
    replayGlobalDefaults(hudManager, nullptr);
    DEBUG_INFO("Reset global settings to factory defaults");
}

void SettingsManager::resetGlobalSectionsToFactoryDefaults(HudManager& hudManager, const std::vector<std::string>& sections,
                                                           const std::vector<GlobalKeyRef>& excludeKeys) {
    replayGlobalDefaults(hudManager, &sections, excludeKeys.empty() ? nullptr : &excludeKeys, /*keysAreExclusions=*/true);
    DEBUG_INFO_F("Reset %zu global section(s) to factory defaults", sections.size());
}

void SettingsManager::resetGlobalKeysToFactoryDefaults(HudManager& hudManager, const std::vector<GlobalKeyRef>& keys) {
    if (keys.empty()) return;
    replayGlobalDefaults(hudManager, nullptr, &keys, /*keysAreExclusions=*/false);
    DEBUG_INFO_F("Reset %zu global key(s) to factory defaults", keys.size());
}

void SettingsManager::copyToProfile(HudManager& hudManager, ProfileType targetProfile) {
    ProfileType activeProfile = ProfileManager::getInstance().getActiveProfile();

    // Don't copy to self
    if (targetProfile == activeProfile) {
        DEBUG_WARN("Cannot copy profile to itself");
        return;
    }

    // Capture current HUD state to active profile (ensure it's up to date)
    captureToProfile(hudManager, activeProfile);

    // Copy to target profile
    m_profileCache[static_cast<size_t>(targetProfile)] = m_profileCache[static_cast<size_t>(activeProfile)];

    // Save to disk
    // Deferred: the change is applied live now; persistence waits for the leave-track flush
    // (or the manual Save button), so profile switches / applies / resets never write on track.
    markDirty();

    DEBUG_INFO_F("Copied %s profile settings to %s",
        ProfileManager::getProfileName(activeProfile),
        ProfileManager::getProfileName(targetProfile));
}
