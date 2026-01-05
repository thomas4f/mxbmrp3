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

    // Sector rows (S1, S2, S3 as group)
    bool sectorsOn = (hud->m_enabledRows & (IdealLapHud::ROW_S1 | IdealLapHud::ROW_S2 | IdealLapHud::ROW_S3)) != 0;
    ctx.addToggleControl("Show sector times", sectorsOn,
        SettingsHud::ClickRegion::CHECKBOX, hud,
        &hud->m_enabledRows, IdealLapHud::ROW_S1 | IdealLapHud::ROW_S2 | IdealLapHud::ROW_S3, true,
        "ideal_lap.sectors");

    // Lap rows (Last, Best, Ideal as group)
    bool lapsOn = (hud->m_enabledRows & (IdealLapHud::ROW_LAST | IdealLapHud::ROW_BEST | IdealLapHud::ROW_IDEAL)) != 0;
    ctx.addToggleControl("Show lap times", lapsOn,
        SettingsHud::ClickRegion::CHECKBOX, hud,
        &hud->m_enabledRows, IdealLapHud::ROW_LAST | IdealLapHud::ROW_BEST | IdealLapHud::ROW_IDEAL, true,
        "ideal_lap.laps");

    return hud;
}
