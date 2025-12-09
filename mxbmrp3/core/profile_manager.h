// ============================================================================
// core/profile_manager.h
// Manages per-event-type settings profiles (Practice, Race, Spectate)
// ============================================================================
#pragma once

#include <cstdint>

// Profile types for different event contexts
enum class ProfileType : uint8_t {
    PRACTICE = 0,   // Practice, Warmup sessions
    QUALIFY = 1,    // Pre-Qualify, Qualify Practice, Qualify sessions
    RACE = 2,       // Race 1, Race 2, SR sessions
    SPECTATE = 3,   // Spectating or viewing replay

    COUNT = 4       // Number of profiles
};

class ProfileManager {
public:
    static ProfileManager& getInstance();

    // Profile selection
    ProfileType getActiveProfile() const { return m_activeProfile; }
    void setActiveProfile(ProfileType profile);

    // Auto-switching based on game state
    bool isAutoSwitchEnabled() const { return m_autoSwitchEnabled; }
    void setAutoSwitchEnabled(bool enabled);

    // Get profile name for display
    static const char* getProfileName(ProfileType profile);

    // Get next profile in cycle order (for UI)
    static ProfileType getNextProfile(ProfileType current);

    // Cycle to next profile (for UI)
    void cycleProfile();

private:
    ProfileManager();
    ~ProfileManager() = default;
    ProfileManager(const ProfileManager&) = delete;
    ProfileManager& operator=(const ProfileManager&) = delete;

    ProfileType m_activeProfile;
    bool m_autoSwitchEnabled;
};
