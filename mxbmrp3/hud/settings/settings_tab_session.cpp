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
        case ClickRegion::SESSION_PASSWORD_MODE_UP:
            if (sessionHud) {
                constexpr int numModes = static_cast<int>(PasswordDisplayMode::COUNT);
                int mode = static_cast<int>(sessionHud->m_passwordMode);
                mode = (mode + 1) % numModes;
                sessionHud->m_passwordMode = static_cast<PasswordDisplayMode>(mode);
                sessionHud->setDataDirty();
                rebuildRenderData();
            }
            return true;

        case ClickRegion::SESSION_PASSWORD_MODE_DOWN:
            if (sessionHud) {
                constexpr int numModes = static_cast<int>(PasswordDisplayMode::COUNT);
                int mode = static_cast<int>(sessionHud->m_passwordMode);
                mode = (mode + numModes - 1) % numModes;  // Go backward
                sessionHud->m_passwordMode = static_cast<PasswordDisplayMode>(mode);
                sessionHud->setDataDirty();
                rebuildRenderData();
            }
            return true;

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

    // === CONFIGURATION SECTION ===
    ctx.addSectionHeader("Configuration");

    // Icons toggle
    ctx.addToggleControl("Show icons", hud->m_bShowIcons,
        SettingsHud::ClickRegion::SESSION_ICONS_TOGGLE, hud, nullptr, 0, true,
        "session.icons");
    ctx.addSpacing(0.5f);

    // === ROWS SECTION ===
    // Order matches display order: Type, Format, Track, Server, Password, Players
    ctx.addSectionHeader("Rows");

    ctx.addToggleControl("Session type", (hud->m_enabledRows & SessionHud::ROW_TYPE) != 0,
        SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledRows, SessionHud::ROW_TYPE, true,
        "session.type");
    ctx.addToggleControl("Format & state", (hud->m_enabledRows & SessionHud::ROW_FORMAT) != 0,
        SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledRows, SessionHud::ROW_FORMAT, true,
        "session.format");
    ctx.addToggleControl("Track name", (hud->m_enabledRows & SessionHud::ROW_TRACK) != 0,
        SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledRows, SessionHud::ROW_TRACK, true,
        "session.track");
    ctx.addToggleControl("Weather & temp", (hud->m_enabledRows & SessionHud::ROW_WEATHER) != 0,
        SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledRows, SessionHud::ROW_WEATHER, true,
        "session.weather");
#if GAME_HAS_SERVER_INFO
    ctx.addToggleControl("Server name", (hud->m_enabledRows & SessionHud::ROW_SERVER) != 0,
        SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledRows, SessionHud::ROW_SERVER, true,
        "session.server");

    // Password mode as a cycle (Off hides the row entirely) - right after server
    const char* passwordModeText = "";
    switch (hud->m_passwordMode) {
        case PasswordDisplayMode::Off:      passwordModeText = "Off"; break;
        case PasswordDisplayMode::Hidden:   passwordModeText = "Hidden"; break;
        case PasswordDisplayMode::AsHost:   passwordModeText = "As Host"; break;
        case PasswordDisplayMode::AsClient: passwordModeText = "As Client"; break;
    }
    ctx.addCycleControl("Password", passwordModeText, 10,
        SettingsHud::ClickRegion::SESSION_PASSWORD_MODE_DOWN,
        SettingsHud::ClickRegion::SESSION_PASSWORD_MODE_UP,
        hud, true, hud->m_passwordMode == PasswordDisplayMode::Off,
        "session.password_mode");

    ctx.addToggleControl("Player count", (hud->m_enabledRows & SessionHud::ROW_PLAYERS) != 0,
        SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledRows, SessionHud::ROW_PLAYERS, true,
        "session.players");
#endif

    return hud;
}
