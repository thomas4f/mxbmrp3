// ============================================================================
// hud/settings/settings_tab_lap_log.cpp
// Tab renderer for Lap Log HUD settings
// ============================================================================
#include "settings_layout.h"
#include "../settings_hud.h"
#include "../lap_log_hud.h"

// Static member function of SettingsHud - handles click events for LapLog tab
bool SettingsHud::handleClickTabLapLog(const ClickRegion& region) {
    LapLogHud* lapLogHud = dynamic_cast<LapLogHud*>(region.targetHud);
    if (!lapLogHud) lapLogHud = m_lapLog;

    switch (region.type) {
        case ClickRegion::LAP_LOG_ROW_COUNT_UP:
            if (lapLogHud) {
                lapLogHud->m_maxDisplayLaps = std::min(
                    lapLogHud->m_maxDisplayLaps + 1,
                    LapLogHud::MAX_DISPLAY_LAPS);
                lapLogHud->setDataDirty();
                setDataDirty();
            }
            return true;

        case ClickRegion::LAP_LOG_ROW_COUNT_DOWN:
            if (lapLogHud) {
                lapLogHud->m_maxDisplayLaps = std::max(
                    lapLogHud->m_maxDisplayLaps - 1,
                    LapLogHud::MIN_DISPLAY_LAPS);
                lapLogHud->setDataDirty();
                setDataDirty();
            }
            return true;

        default:
            return false;
    }
}

// Static member function of SettingsHud - inherits friend access to LapLogHud
BaseHud* SettingsHud::renderTabLapLog(SettingsLayoutContext& ctx) {
    LapLogHud* hud = ctx.parent->getLapLogHud();
    if (!hud) return nullptr;

    ctx.addTabTooltip("lap_log");

    // === APPEARANCE SECTION ===
    ctx.addSectionHeader("Appearance");
    ctx.addStandardHudControls(hud);
    ctx.addSpacing(0.5f);

    // === CONFIGURATION SECTION ===
    ctx.addSectionHeader("Configuration");

    // Row count
    char rowCountValue[8];
    snprintf(rowCountValue, sizeof(rowCountValue), "%d", hud->m_maxDisplayLaps);
    ctx.addCycleControl("Laps to display", rowCountValue, 10,
        SettingsHud::ClickRegion::LAP_LOG_ROW_COUNT_DOWN,
        SettingsHud::ClickRegion::LAP_LOG_ROW_COUNT_UP,
        hud, true, false, "lap_log.rows");
    ctx.addSpacing(0.5f);

    // === COLUMNS SECTION ===
    ctx.addSectionHeader("Columns");

    // Column toggles
    ctx.addToggleControl("Lap number", (hud->m_enabledColumns & LapLogHud::COL_LAP) != 0,
        SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledColumns, LapLogHud::COL_LAP, true,
        "lap_log.col_lap");

    bool sectorsOn = (hud->m_enabledColumns & (LapLogHud::COL_S1 | LapLogHud::COL_S2 | LapLogHud::COL_S3)) != 0;
    ctx.addToggleControl("Sector times", sectorsOn,
        SettingsHud::ClickRegion::CHECKBOX, hud,
        &hud->m_enabledColumns, LapLogHud::COL_S1 | LapLogHud::COL_S2 | LapLogHud::COL_S3, true,
        "lap_log.col_sectors");

    ctx.addToggleControl("Lap time", (hud->m_enabledColumns & LapLogHud::COL_TIME) != 0,
        SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledColumns, LapLogHud::COL_TIME, true,
        "lap_log.col_time");

    return hud;
}
