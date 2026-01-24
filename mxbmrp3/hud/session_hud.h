// ============================================================================
// hud/session_hud.h
// Session HUD - displays session info (type, track, format, server, players, password)
// ============================================================================
#pragma once

#include "base_hud.h"
#include "../core/plugin_data.h"
#include "../core/plugin_constants.h"
#include "../core/widget_constants.h"
#include "../game/game_config.h"

// Password display mode for SessionHud
enum class PasswordDisplayMode : uint8_t {
    Off,        // Don't show password row at all
    Hidden,     // Show password row with asterisks (e.g., "****")
    AsHost,     // Show actual password only when hosting
    AsClient,   // Show actual password only when connected as client
    COUNT       // Number of modes (for cycling)
};

class SessionHud : public BaseHud {
public:
    // Row visibility flags (configurable via settings)
    enum RowFlags : uint32_t {
        ROW_TYPE     = 1 << 0,  // Session type (e.g., "PRACTICE", "RACE 2")
        ROW_TRACK    = 1 << 1,  // Track name
        ROW_FORMAT   = 1 << 2,  // Format + Session state (e.g., "10:00 + 2 Laps, In Progress")
        ROW_SERVER   = 1 << 3,  // Server name (only shown when online, MX Bikes only)
        ROW_PLAYERS  = 1 << 4,  // Player count (only shown when online, MX Bikes only)
        ROW_WEATHER  = 1 << 5,  // Weather conditions + temperatures (e.g., "Sunny, 24 / 32 C" for air/track)

#if GAME_HAS_SERVER_INFO
        ROW_DEFAULT = 0x1F      // Type, Track, Format, Server, Players (weather disabled)
#else
        ROW_DEFAULT = 0x07      // Type, Track, Format only (no server info available)
#endif
    };

    SessionHud();
    virtual ~SessionHud() = default;

    void update() override;
    bool handlesDataType(DataChangeType dataType) const override;
    void resetToDefaults();

    // Public for settings access
    uint32_t m_enabledRows = ROW_DEFAULT;
    PasswordDisplayMode m_passwordMode = PasswordDisplayMode::Hidden;  // Off = no password row
    bool m_bShowIcons = true;  // Show row icons

    // Helper to count enabled rows
    int getEnabledRowCount() const;

protected:
    void rebuildLayout() override;

private:
    void rebuildRenderData() override;

    // Helper to calculate content height based on enabled rows
    float calculateContentHeight(const ScaledDimensions& dim) const;

    // Helper to determine if password should be shown based on mode and connection type
    bool shouldShowPassword() const;

    // Helper to get password display text based on mode
    const char* getPasswordDisplayText() const;

    // Helper to calculate icon quad corner positions (shared between rebuild and layout)
    void calculateIconQuadCorners(float x, float y, float fontSize, float corners[4][2]) const;

    // Cached data to avoid unnecessary rebuilds
    int m_cachedEventType;
    int m_cachedSession;
    int m_cachedSessionState;
    int m_cachedSessionLength;
    int m_cachedSessionNumLaps;
    int m_cachedConnectionType;
    int m_cachedServerClientsCount;
    int m_cachedServerMaxClients;
    char m_cachedServerName[100];
    char m_cachedServerPassword[64];
    int m_cachedConditions;
    float m_cachedAirTemperature;
    float m_cachedTrackTemperature;
};
