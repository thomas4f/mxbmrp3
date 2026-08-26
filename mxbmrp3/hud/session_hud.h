// ============================================================================
// hud/session_hud.h
// Session HUD - displays session info (server, track, format, weather)
// ============================================================================
#pragma once

#include "base_hud.h"
#include "../core/plugin_data.h"
#include "../core/plugin_constants.h"
#include "../core/widget_constants.h"
#include "../game/game_config.h"

class SessionHud : public BaseHud {
public:
    // Row visibility flags (configurable via settings)
    // Bit 0 (the old ROW_TYPE) is retired — the session type now shows in the
    // StandingsHud title. Remaining bit values are unchanged so saved named-key
    // rows still map correctly.
    enum RowFlags : uint32_t {
        ROW_TRACK    = 1 << 1,  // Track name
        ROW_FORMAT   = 1 << 2,  // Format + Session state (e.g., "10:00 + 2 Laps, In Progress")
        ROW_SERVER   = 1 << 3,  // Server name / "Testing" / "Unknown" (headline row)
        ROW_WEATHER  = 1 << 4,  // Weather conditions + temperatures (e.g., "Sunny, 24 / 32 C" for air/track)

        ROW_DEFAULT = ROW_TRACK | ROW_FORMAT | ROW_SERVER  // Server, Track, Format (weather disabled)
    };

    SessionHud();
    virtual ~SessionHud() = default;

    void update() override;
    bool handlesDataType(DataChangeType dataType) const override;
    const char* getIconName() const override { return "hud-session"; }
    void resetToDefaults();

    // Public for settings access
    uint32_t m_enabledRows = ROW_DEFAULT;
    bool m_bShowIcons = true;  // Show row icons

protected:
    void rebuildLayout() override;

private:
    void rebuildRenderData() override;

    // Helper to calculate content height based on enabled rows
    float calculateContentHeight(const ScaledDimensions& dim) const;

    // Helper to calculate icon quad corner positions (shared between rebuild and layout)
    void calculateIconQuadCorners(float x, float y, float fontSize, float corners[4][2]) const;

    // Where the ROW icon quads begin, recorded at build time. It was a hardcoded 1
    // ("background is quad 0"), which stopped being true the moment a theme was
    // active: a themed panel is nine slices, plus nine for the title band, plus nine
    // for the body card, plus the title's own identity icon. The layout fast path
    // then repositioned FRAME SLICES as if they were row icons. Recording the real
    // index is the fix BaseHud::m_bContentCard's declaration warns about by name.
    int m_firstIconQuad = 1;

    // Cached data to avoid unnecessary rebuilds
    static constexpr int CACHE_UNINITIALIZED = -2;  // Sentinel distinct from -1 (valid serverType=Unknown)
    int m_cachedSessionState;
    int m_cachedSessionLength;
    int m_cachedSessionNumLaps;
    int m_cachedServerType;
    bool m_cachedMultiRider;   // >1 rider last render (drives the "Online" server label)
    char m_cachedServerName[100];
    int m_cachedConditions;
    float m_cachedAirTemperature;
    float m_cachedTrackTemperature;
};
