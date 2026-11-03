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
    ctx.addSectionHeading(TR("Appearance"));
    ctx.addStandardHudControls(hud);
    // === LAYOUT SECTION ===
    ctx.addSectionHeading(TR("Layout"));

    char rowCountValue[8];
    snprintf(rowCountValue, sizeof(rowCountValue), "%d", hud->m_maxDisplayRows);
    ctx.addSteppedControl(TR("Rows to show"), rowCountValue,
        SettingsHud::SteppedControl::clampInt(&hud->m_maxDisplayRows, 1,
            FriendsHud::MIN_DISPLAY_ROWS, FriendsHud::MAX_DISPLAY_ROWS, hud),
        hud, true, false, "friends.rows");

    {
        // English, in state order, translated where shown (as on every tab).
        static const char* const kShowModes[] = { N_("Always"), N_("Friends"), N_("On join") };
        static_assert(sizeof(kShowModes) / sizeof(kShowModes[0]) ==
                      static_cast<size_t>(FriendsHud::ShowMode::COUNT), "one name per show mode");
        SettingsHud::CycleControl showCycle = SettingsHud::CycleControl::enumMember(
            hud, &FriendsHud::m_showMode,
            static_cast<int>(FriendsHud::ShowMode::COUNT), hud, kShowModes);
        // Reset transient ON_JOIN state on mode change (exactly what the old
        // dedicated handler did).
        showCycle.postStep = [hud]() { hud->m_activityShowing = false; };
        ctx.addCycleControl(TR("Show mode"), cycleName(kShowModes, static_cast<int>(hud->m_showMode)),
            showCycle, hud, true, false, "friends.showmode");
    }

    ctx.addToggleControl(TR("Show myself"), hud->m_showSelf,
        SettingsHud::ClickRegion::FRIENDS_SELF_TOGGLE, hud, nullptr, 0, true,
        "friends.self");

    // === CONTENT SECTION ===
    ctx.addSectionHeading(TR("Content"));

    ctx.addToggleControl(TR("Column headers"), hud->m_bShowHeaders,
        SettingsHud::ClickRegion::FRIENDS_HEADERS_TOGGLE, hud, nullptr, 0, true,
        "friends.headers");

    bool serverOn = (hud->m_enabledColumns & FriendsHud::COL_SERVER) != 0;
    ctx.addToggleControl(TR("Server"), serverOn, SettingsHud::ClickRegion::CHECKBOX, hud,
        &hud->m_enabledColumns, FriendsHud::COL_SERVER, true, "friends.col_server");

    bool trackOn = (hud->m_enabledColumns & FriendsHud::COL_TRACK) != 0;
    ctx.addToggleControl(TR("Track"), trackOn, SettingsHud::ClickRegion::CHECKBOX, hud,
        &hud->m_enabledColumns, FriendsHud::COL_TRACK, true, "friends.col_track");

    bool infoOn = (hud->m_enabledColumns & FriendsHud::COL_INFO) != 0;
    ctx.addToggleControl(TR("Info"), infoOn, SettingsHud::ClickRegion::CHECKBOX, hud,
        &hud->m_enabledColumns, FriendsHud::COL_INFO, true, "friends.col_info");

    bool timerOn = (hud->m_enabledColumns & FriendsHud::COL_TIMER) != 0;
    ctx.addToggleControl(TR("Timer"), timerOn, SettingsHud::ClickRegion::CHECKBOX, hud,
        &hud->m_enabledColumns, FriendsHud::COL_TIMER, true, "friends.col_timer");

    return hud;
}
