// ============================================================================
// hud/settings/settings_tab_friends.cpp
// Tab renderer for Friends HUD settings
// ============================================================================
#include "settings_layout.h"
#include "../settings_hud.h"
#include "../friends_hud.h"

// Member function of SettingsHud - handles click events for the Friends tab
bool SettingsHud::handleClickTabFriends(const ClickRegion& region) {
    FriendsHud* hud = dynamic_cast<FriendsHud*>(region.targetHud);
    if (!hud) hud = m_friends;

    switch (region.type) {
        case ClickRegion::FRIENDS_ROW_COUNT_UP:
            if (hud) {
                hud->m_maxDisplayRows = std::min(hud->m_maxDisplayRows + 1, FriendsHud::MAX_DISPLAY_ROWS);
                hud->setDataDirty();
                setDataDirty();
            }
            return true;

        case ClickRegion::FRIENDS_ROW_COUNT_DOWN:
            if (hud) {
                hud->m_maxDisplayRows = std::max(hud->m_maxDisplayRows - 1, FriendsHud::MIN_DISPLAY_ROWS);
                hud->setDataDirty();
                setDataDirty();
            }
            return true;

        case ClickRegion::FRIENDS_HEADERS_TOGGLE:
            if (hud) {
                hud->m_bShowHeaders = !hud->m_bShowHeaders;
                hud->setDataDirty();
                setDataDirty();
            }
            return true;

        case ClickRegion::FRIENDS_SHOW_MODE_UP:
        case ClickRegion::FRIENDS_SHOW_MODE_DOWN:
            if (hud) {
                const int count = static_cast<int>(FriendsHud::ShowMode::COUNT);
                const int step = (region.type == ClickRegion::FRIENDS_SHOW_MODE_UP) ? 1 : (count - 1);
                hud->m_showMode = static_cast<FriendsHud::ShowMode>(
                    (static_cast<int>(hud->m_showMode) + step) % count);
                hud->m_activityShowing = false;  // reset transient ON_JOIN state on mode change
                hud->setDataDirty();
                setDataDirty();
            }
            return true;

        case ClickRegion::FRIENDS_SELF_TOGGLE:
            if (hud) {
                hud->m_showSelf = !hud->m_showSelf;
                hud->setDataDirty();
                setDataDirty();
            }
            return true;

        default:
            return false;
    }
}

BaseHud* SettingsHud::renderTabFriends(SettingsLayoutContext& ctx) {
    FriendsHud* hud = ctx.parent->getFriendsHud();
    if (!hud) return nullptr;

    ctx.addTabTooltip("friends");

    // === APPEARANCE SECTION ===
    ctx.addSectionHeader("Appearance");
    ctx.addStandardHudControls(hud);
    ctx.addSpacing(0.5f);

    // === LAYOUT SECTION ===
    ctx.addSectionHeader("Layout");

    char rowCountValue[8];
    snprintf(rowCountValue, sizeof(rowCountValue), "%d", hud->m_maxDisplayRows);
    ctx.addCycleControl("Max rows", rowCountValue, 10,
        SettingsHud::ClickRegion::FRIENDS_ROW_COUNT_DOWN,
        SettingsHud::ClickRegion::FRIENDS_ROW_COUNT_UP,
        hud, true, false, "friends.rows");

    ctx.addToggleControl("Column headers", hud->m_bShowHeaders,
        SettingsHud::ClickRegion::FRIENDS_HEADERS_TOGGLE, hud, nullptr, 0, true,
        "friends.headers");

    ctx.addCycleControl("Show mode", FriendsHud::getShowModeName(hud->m_showMode), 10,
        SettingsHud::ClickRegion::FRIENDS_SHOW_MODE_DOWN,
        SettingsHud::ClickRegion::FRIENDS_SHOW_MODE_UP,
        hud, true, false, "friends.showmode");

    ctx.addToggleControl("Show myself", hud->m_showSelf,
        SettingsHud::ClickRegion::FRIENDS_SELF_TOGGLE, hud, nullptr, 0, true,
        "friends.self");

    ctx.addSpacing(0.5f);

    // === CONTENT SECTION ===
    ctx.addSectionHeader("Content");

    bool serverOn = (hud->m_enabledColumns & FriendsHud::COL_SERVER) != 0;
    ctx.addToggleControl("Server", serverOn, SettingsHud::ClickRegion::CHECKBOX, hud,
        &hud->m_enabledColumns, FriendsHud::COL_SERVER, true, "friends.col_server");

    bool trackOn = (hud->m_enabledColumns & FriendsHud::COL_TRACK) != 0;
    ctx.addToggleControl("Track", trackOn, SettingsHud::ClickRegion::CHECKBOX, hud,
        &hud->m_enabledColumns, FriendsHud::COL_TRACK, true, "friends.col_track");

    bool infoOn = (hud->m_enabledColumns & FriendsHud::COL_INFO) != 0;
    ctx.addToggleControl("Info", infoOn, SettingsHud::ClickRegion::CHECKBOX, hud,
        &hud->m_enabledColumns, FriendsHud::COL_INFO, true, "friends.col_info");

    bool timerOn = (hud->m_enabledColumns & FriendsHud::COL_TIMER) != 0;
    ctx.addToggleControl("Timer", timerOn, SettingsHud::ClickRegion::CHECKBOX, hud,
        &hud->m_enabledColumns, FriendsHud::COL_TIMER, true, "friends.col_timer");

    return hud;
}
