// ============================================================================
// core/profile_manager.cpp
// Manages per-event-type settings profiles (Practice, Race, Spectate)
// ============================================================================
#include "profile_manager.h"
#include "../diagnostics/logger.h"

ProfileManager& ProfileManager::getInstance() {
    static ProfileManager instance;
    return instance;
}

ProfileManager::ProfileManager()
    : m_activeProfile(ProfileType::PRACTICE)
    , m_autoSwitchEnabled(false)
{
    DEBUG_INFO("ProfileManager created");
}

void ProfileManager::setActiveProfile(ProfileType profile) {
    if (profile >= ProfileType::COUNT) {
        DEBUG_WARN_F("Invalid profile type: %d", static_cast<int>(profile));
        return;
    }

    if (m_activeProfile != profile) {
        DEBUG_INFO_F("Profile changed: %s -> %s",
            getProfileName(m_activeProfile),
            getProfileName(profile));
        m_activeProfile = profile;
    }
}

void ProfileManager::setAutoSwitchEnabled(bool enabled) {
    if (m_autoSwitchEnabled != enabled) {
        m_autoSwitchEnabled = enabled;
        DEBUG_INFO_F("Auto-switch %s", enabled ? "enabled" : "disabled");
    }
}

const char* ProfileManager::getProfileName(ProfileType profile) {
    switch (profile) {
        case ProfileType::PRACTICE: return "Practice";
        case ProfileType::QUALIFY:  return "Qualify";
        case ProfileType::RACE:     return "Race";
        case ProfileType::SPECTATE: return "Spectate";
        default:                    return "Unknown";
    }
}

ProfileType ProfileManager::getNextProfile(ProfileType current) {
    int next = (static_cast<int>(current) + 1) % static_cast<int>(ProfileType::COUNT);
    return static_cast<ProfileType>(next);
}

ProfileType ProfileManager::getPreviousProfile(ProfileType current) {
    int prev = (static_cast<int>(current) - 1 + static_cast<int>(ProfileType::COUNT)) % static_cast<int>(ProfileType::COUNT);
    return static_cast<ProfileType>(prev);
}

void ProfileManager::cycleProfile() {
    setActiveProfile(getNextProfile(m_activeProfile));
}
