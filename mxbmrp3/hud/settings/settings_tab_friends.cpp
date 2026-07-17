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
        // Max rows is a data-driven STEPPED control now - registered in
        // renderTabFriends via ctx.addSteppedControl.

        case ClickRegion::FRIENDS_HEADERS_TOGGLE:
            if (hud) {
                hud->m_bShowHeaders = !hud->m_bShowHeaders;
                hud->setDataDirty();
                setDataDirty();
            }
            return true;

        // Show mode is a data-driven CYCLE control now - registered in
        // renderTabFriends via ctx.addCycleControl (resetting the transient
        // ON_JOIN state is the descriptor's postStep).

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
    ctx.addSteppedControl("Max rows", rowCountValue, 10,
        SettingsHud::SteppedControl::clampInt(&hud->m_maxDisplayRows, 1,
            FriendsHud::MIN_DISPLAY_ROWS, FriendsHud::MAX_DISPLAY_ROWS, hud),
        hud, true, false, "friends.rows");

    ctx.addToggleControl("Column headers", hud->m_bShowHeaders,
        SettingsHud::ClickRegion::FRIENDS_HEADERS_TOGGLE, hud, nullptr, 0, true,
        "friends.headers");

    {
        SettingsHud::CycleControl showCycle = SettingsHud::CycleControl::enumMember(
            hud, &FriendsHud::m_showMode,
            static_cast<int>(FriendsHud::ShowMode::COUNT), hud);
        // Reset transient ON_JOIN state on mode change (exactly what the old
        // dedicated handler did).
        showCycle.postStep = [hud]() { hud->m_activityShowing = false; };
        ctx.addCycleControl("Show mode", FriendsHud::getShowModeName(hud->m_showMode), 10,
            showCycle, hud, true, false, "friends.showmode");
    }

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
