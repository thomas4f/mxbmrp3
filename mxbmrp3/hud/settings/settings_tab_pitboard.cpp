// ============================================================================
// hud/settings/settings_tab_pitboard.cpp
// Tab renderer for Pitboard HUD settings
// ============================================================================
#include "settings_layout.h"
#include "../settings_hud.h"
#include "../pitboard_hud.h"

// Static member function of SettingsHud - handles click events for Pitboard tab
bool SettingsHud::handleClickTabPitboard(const ClickRegion& region) {
    PitboardHud* pitboardHud = dynamic_cast<PitboardHud*>(region.targetHud);
    if (!pitboardHud) pitboardHud = m_pitboard;

    switch (region.type) {
        case ClickRegion::PITBOARD_SHOW_MODE_UP:
            if (pitboardHud) {
                int mode = static_cast<int>(pitboardHud->m_displayMode);
                mode = (mode + 1) % 3;  // 3 modes: Always, Pit, Splits
                pitboardHud->m_displayMode = static_cast<PitboardHud::DisplayMode>(mode);
                rebuildRenderData();
            }
            return true;

        case ClickRegion::PITBOARD_SHOW_MODE_DOWN:
            if (pitboardHud) {
                int mode = static_cast<int>(pitboardHud->m_displayMode);
                mode = (mode + 2) % 3;  // Go backward
                pitboardHud->m_displayMode = static_cast<PitboardHud::DisplayMode>(mode);
                rebuildRenderData();
            }
            return true;

        default:
            return false;
    }
}

// Static member function of SettingsHud - inherits friend access to PitboardHud
BaseHud* SettingsHud::renderTabPitboard(SettingsLayoutContext& ctx) {
    PitboardHud* hud = ctx.parent->getPitboardHud();
    if (!hud) return nullptr;

    ctx.addTabTooltip("pitboard");

    // === APPEARANCE SECTION ===
    ctx.addSectionHeader("Appearance");
    ctx.addStandardHudControls(hud, false);  // No title support
    ctx.addSpacing(0.5f);

    // === CONFIGURATION SECTION ===
    ctx.addSectionHeader("Configuration");

    // Display mode control (Always/Pit/Splits)
    const char* displayModeText = "";
    if (hud->m_displayMode == PitboardHud::MODE_ALWAYS) {
        displayModeText = "Always";
    } else if (hud->m_displayMode == PitboardHud::MODE_PIT) {
        displayModeText = "Pit";
    } else if (hud->m_displayMode == PitboardHud::MODE_SPLITS) {
        displayModeText = "Splits";
    }
    ctx.addCycleControl("Show mode", displayModeText, 10,
        SettingsHud::ClickRegion::PITBOARD_SHOW_MODE_DOWN,
        SettingsHud::ClickRegion::PITBOARD_SHOW_MODE_UP,
        hud, true, false, "pitboard.show_mode");
    ctx.addSpacing(0.5f);

    // === ROWS SECTION ===
    ctx.addSectionHeader("Rows");

    ctx.addToggleControl("Rider name", (hud->m_enabledRows & PitboardHud::ROW_RIDER_ID) != 0,
        SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledRows, PitboardHud::ROW_RIDER_ID, true,
        "pitboard.rider");
    ctx.addToggleControl("Session info", (hud->m_enabledRows & PitboardHud::ROW_SESSION) != 0,
        SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledRows, PitboardHud::ROW_SESSION, true,
        "pitboard.session");
    ctx.addToggleControl("Position", (hud->m_enabledRows & PitboardHud::ROW_POSITION) != 0,
        SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledRows, PitboardHud::ROW_POSITION, true,
        "pitboard.position");
    ctx.addToggleControl("Time elapsed", (hud->m_enabledRows & PitboardHud::ROW_TIME) != 0,
        SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledRows, PitboardHud::ROW_TIME, true,
        "pitboard.time");
    ctx.addToggleControl("Lap number", (hud->m_enabledRows & PitboardHud::ROW_LAP) != 0,
        SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledRows, PitboardHud::ROW_LAP, true,
        "pitboard.lap");
    ctx.addToggleControl("Last lap time", (hud->m_enabledRows & PitboardHud::ROW_LAST_LAP) != 0,
        SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledRows, PitboardHud::ROW_LAST_LAP, true,
        "pitboard.last_lap");
    ctx.addToggleControl("Gap to leader", (hud->m_enabledRows & PitboardHud::ROW_GAP) != 0,
        SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledRows, PitboardHud::ROW_GAP, true,
        "pitboard.gap");

    return hud;
}
