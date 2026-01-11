// ============================================================================
// hud/settings/settings_tab_ideal_lap.cpp
// Tab renderer for Ideal Lap HUD settings
// ============================================================================
#include "settings_layout.h"
#include "../settings_hud.h"
#include "../ideal_lap_hud.h"

// Static member function of SettingsHud - inherits friend access to IdealLapHud
BaseHud* SettingsHud::renderTabIdealLap(SettingsLayoutContext& ctx) {
    IdealLapHud* hud = ctx.parent->getIdealLapHud();
    if (!hud) return nullptr;

    ctx.addTabTooltip("ideal_lap");

    // === APPEARANCE SECTION ===
    ctx.addSectionHeader("Appearance");
    ctx.addStandardHudControls(hud);
    ctx.addSpacing(0.5f);

    // === DATA DISPLAY SECTION ===
    ctx.addSectionHeader("Data Display");

    // Sector rows (S1, S2, S3)
    bool sectorsOn = (hud->m_enabledRows & IdealLapHud::ROW_SECTORS) != 0;
    ctx.addToggleControl("Show sector times", sectorsOn,
        SettingsHud::ClickRegion::CHECKBOX, hud,
        &hud->m_enabledRows, IdealLapHud::ROW_SECTORS, true,
        "ideal_lap.sectors");

    // Lap rows (Last, Best, Ideal)
    bool lapsOn = (hud->m_enabledRows & IdealLapHud::ROW_LAPS) != 0;
    ctx.addToggleControl("Show lap times", lapsOn,
        SettingsHud::ClickRegion::CHECKBOX, hud,
        &hud->m_enabledRows, IdealLapHud::ROW_LAPS, true,
        "ideal_lap.laps");

    return hud;
}
