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

        case ClickRegion::LAP_LOG_ORDER_UP:
        case ClickRegion::LAP_LOG_ORDER_DOWN:
            if (lapLogHud) {
                // Toggle between OLDEST_FIRST and NEWEST_FIRST
                lapLogHud->m_displayOrder = (lapLogHud->m_displayOrder == LapLogHud::DisplayOrder::OLDEST_FIRST)
                    ? LapLogHud::DisplayOrder::NEWEST_FIRST
                    : LapLogHud::DisplayOrder::OLDEST_FIRST;
                lapLogHud->setDataDirty();
                setDataDirty();
            }
            return true;

        case ClickRegion::LAP_LOG_GAP_ROW_TOGGLE:
            if (lapLogHud) {
                lapLogHud->m_showGapRow = !lapLogHud->m_showGapRow;
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

    // Display order
    const char* orderValue = (hud->m_displayOrder == LapLogHud::DisplayOrder::OLDEST_FIRST)
        ? "Oldest" : "Newest";
    ctx.addCycleControl("Display order", orderValue, 10,
        SettingsHud::ClickRegion::LAP_LOG_ORDER_DOWN,
        SettingsHud::ClickRegion::LAP_LOG_ORDER_UP,
        hud, true, false, "lap_log.order");

    // Sector times toggle
    bool sectorsOn = (hud->m_enabledColumns & LapLogHud::COL_SECTORS) != 0;
    ctx.addToggleControl("Sector times", sectorsOn,
        SettingsHud::ClickRegion::CHECKBOX, hud,
        &hud->m_enabledColumns, LapLogHud::COL_SECTORS, true,
        "lap_log.col_sectors");

    // Gap row toggle
    ctx.addToggleControl("Live gap row", hud->m_showGapRow,
        SettingsHud::ClickRegion::LAP_LOG_GAP_ROW_TOGGLE, hud, nullptr, 0, true,
        "lap_log.gap_row");

    return hud;
}
