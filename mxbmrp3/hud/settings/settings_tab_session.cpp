// ============================================================================
// hud/settings/settings_tab_session.cpp
// Tab renderer for Session HUD settings
// ============================================================================
#include "settings_layout.h"
#include "../settings_hud.h"
#include "../session_hud.h"
#include "../../game/game_config.h"

// Static member function of SettingsHud - handles click events for Session tab
bool SettingsHud::handleClickTabSession(const ClickRegion& region) {
    SessionHud* sessionHud = dynamic_cast<SessionHud*>(region.targetHud);
    if (!sessionHud) sessionHud = m_session;

    switch (region.type) {
        case ClickRegion::SESSION_ICONS_TOGGLE:
            if (sessionHud) {
                sessionHud->m_bShowIcons = !sessionHud->m_bShowIcons;
                sessionHud->setDataDirty();
                rebuildRenderData();
            }
            return true;

        default:
            return false;
    }
}

// Static member function of SettingsHud - inherits friend access to SessionHud
BaseHud* SettingsHud::renderTabSession(SettingsLayoutContext& ctx) {
    SessionHud* hud = ctx.parent->getSessionHud();
    if (!hud) return nullptr;

    ctx.addTabTooltip("session");

    // === APPEARANCE SECTION ===
    ctx.addSectionHeader("Appearance");
    ctx.addStandardHudControls(hud, true);  // With title support
    ctx.addSpacing(0.5f);

    // === LAYOUT SECTION ===
    ctx.addSectionHeader("Layout");

    // Icons toggle
    ctx.addToggleControl("Show icons", hud->m_bShowIcons,
        SettingsHud::ClickRegion::SESSION_ICONS_TOGGLE, hud, nullptr, 0, true,
        "session.icons");
    ctx.addSpacing(0.5f);

    // === CONTENT SECTION ===
    // Order matches display order: Server (headline), Track, Format, Weather
    ctx.addSectionHeader("Content");

    ctx.addToggleControl("Server name", (hud->m_enabledRows & SessionHud::ROW_SERVER) != 0,
        SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledRows, SessionHud::ROW_SERVER, true,
        "session.server");
    ctx.addToggleControl("Track name", (hud->m_enabledRows & SessionHud::ROW_TRACK) != 0,
        SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledRows, SessionHud::ROW_TRACK, true,
        "session.track");
    ctx.addToggleControl("Format & state", (hud->m_enabledRows & SessionHud::ROW_FORMAT) != 0,
        SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledRows, SessionHud::ROW_FORMAT, true,
        "session.format");
    ctx.addToggleControl("Weather & temp", (hud->m_enabledRows & SessionHud::ROW_WEATHER) != 0,
        SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledRows, SessionHud::ROW_WEATHER, true,
        "session.weather");

    return hud;
}
