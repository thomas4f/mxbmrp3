// ============================================================================
// hud/friends_hud.h
// Friends HUD - shows Steam friends who are in the same game, where they are
// (server / offline / unknown), and whether they're on your server + track.
//
// Data comes from SteamFriendsManager::getFriends() (the read side of Steam rich
// presence). The manager owns ingestion + the same-server computation; this HUD
// only formats a snapshot into a table, per the "HUDs don't cache raw game
// data" rule.
//
// Columns: [badge] Name | Server | Track | Info | Timer
//   Name   = Steam persona name (+ a same-server badge); always shown.
//   Server = server name; blank when offline. Toggle.
//   Track  = the friend's current track. Toggle.
//   Info   = "Session (Format), State" comma-joined (empties skipped), or
//            "In Menus" / "Unknown" between sessions. Toggle.
//   Timer  = the friend's session clock (MM:SS / N TO GO / FINAL LAP /
//            CHECKERED) - a coarse rich-presence snapshot, not a live tick. Toggle.
// ============================================================================
#pragma once

#include "base_hud.h"
#include "../core/plugin_constants.h"
#include <cstdint>
#include <chrono>

class FriendsHud : public BaseHud {
public:
    FriendsHud();
    virtual ~FriendsHud() = default;

    void update() override;
    bool handlesDataType(DataChangeType dataType) const override;
    void resetToDefaults();

    // Optional columns (Name + the same-server badge are always shown).
    enum ColumnFlags : uint32_t {
        COL_SERVER = 1 << 0,
        COL_TRACK  = 1 << 1,
        COL_INFO   = 1 << 2,
        COL_TIMER  = 1 << 3,
        COL_DEFAULT = COL_SERVER | COL_TRACK  // Name + Server + Track; Info/Timer toggle on
    };

    // Row-count limits (public so the settings UI can clamp).
    static constexpr int MIN_DISPLAY_ROWS = 1;
    static constexpr int MAX_DISPLAY_ROWS = 16;

    // When the HUD displays while enabled (the Visible toggle is the master on/off).
    enum class ShowMode : uint8_t {
        ALWAYS = 0,        // always shown
        WITH_FRIENDS = 1,  // only while >=1 friend is in-game
        ON_JOIN = 2,       // briefly, when a friend comes in-game, then hide
        COUNT = 3
    };
    static const char* getShowModeName(ShowMode mode);

    friend class SettingsHud;
    friend class SettingsManager;

protected:
    void rebuildLayout() override;

private:
    void rebuildRenderData() override;

    bool isColumnEnabled(ColumnFlags col) const { return (m_enabledColumns & col) != 0; }

    static constexpr float START_X = 0.0f;
    static constexpr float START_Y = 0.0f;

    // Never render narrower than the standard HUD width (matches FMX / Stats /
    // Standings / IdealLap), so an empty / "Steam off" box isn't a tiny sliver.
    static constexpr int MIN_WIDTH_CHARS = 27;

    // Column content widths in characters (include a 1-char trailing gap).
    // Default columns (badge + Name + Server + Track) sum to 43 = the LapLog HUD's
    // width. Strings longer than their column are truncated (PluginUtils::fitText).
    static constexpr int COL_BADGE_W  = 2;
    static constexpr int COL_NAME_W   = 12;
    static constexpr int COL_SERVER_W = 17;  // server name is the more important field
    static constexpr int COL_TRACK_W  = 12;
    static constexpr int COL_INFO_W   = 32;  // "Race 2 (8:00 + 6L), In Progress" - truncates beyond
    static constexpr int COL_TIMER_W  = 11;  // "FINAL LAP" / "12 TO GO" / "CHECKERED" / MM:SS

    // Total background width in characters for the currently enabled columns.
    int getBackgroundWidthChars() const;

    uint32_t m_enabledColumns = COL_DEFAULT;
    int  m_maxDisplayRows = 8;
    bool m_bShowHeaders = true;          // column-header row
    ShowMode m_showMode = ShowMode::WITH_FRIENDS;  // when to display while enabled
    bool m_showSelf = true;              // include our own presence as a row ("You")

    // ON_JOIN state: show for a window after friend activity (timestamp owned by
    // the manager), then hide. The show<->hide flip is evaluated in update().
    // The window length is INI-only (no settings-tab control): "onJoinDurationMs".
    int  m_onJoinDurationMs = 15000;
    bool m_activityShowing = false;

    // Cached "circle" icon sprite for the same-server badge (resolved once, since
    // a font glyph like U+25CF isn't reliably present in every configured font).
    int  m_badgeIcon = 0;
    bool m_badgeIconResolved = false;
};
