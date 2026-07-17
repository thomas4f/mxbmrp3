// ============================================================================
// hud/settings/settings_tab_session_charts.cpp
// Tab renderer + click handler for the Session Charts HUD settings.
// ============================================================================
#include "settings_layout.h"
#include "../settings_hud.h"
#include "../session_charts_hud.h"
#include <algorithm>
#include <cstdio>

static const char* getColorModeName(SessionChartsHud::RiderColorMode mode) {
    switch (mode) {
        case SessionChartsHud::RiderColorMode::POSITION_PALETTE: return "Palette";
        case SessionChartsHud::RiderColorMode::BRAND:            return "Brand";
        default: return "Unknown";
    }
}

// Note: the Session Charts tab has no tab-specific click handler anymore -
// Rows to show / Top positions are data-driven STEPPED controls and Colors is
// a data-driven CYCLE control (registered in renderTabSessionCharts); the rest
// uses the common handlers.

// Static member of SettingsHud - inherits friend access to SessionChartsHud.
BaseHud* SettingsHud::renderTabSessionCharts(SettingsLayoutContext& ctx) {
    SessionChartsHud* hud = ctx.parent->getSessionChartsHud();
    if (!hud) return nullptr;

    ctx.addTabTooltip("session_charts");

    // === APPEARANCE SECTION ===
    ctx.addSectionHeader("Appearance");
    ctx.addStandardHudControls(hud);
    ctx.addSpacing(0.5f);

    // === CHARTS SECTION === (independent checkboxes; any combination stacks vertically)
    ctx.addSectionHeader("Charts");
    ctx.addToggleControl("Lap chart", (hud->m_enabledCharts & SessionChartsHud::CHART_LAP) != 0,
        SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledCharts, SessionChartsHud::CHART_LAP, true,
        "session_charts.chart_lap");
    ctx.addToggleControl("Race trace", (hud->m_enabledCharts & SessionChartsHud::CHART_TRACE) != 0,
        SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledCharts, SessionChartsHud::CHART_TRACE, true,
        "session_charts.chart_trace");
    ctx.addToggleControl("Gap", (hud->m_enabledCharts & SessionChartsHud::CHART_GAP) != 0,
        SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledCharts, SessionChartsHud::CHART_GAP, true,
        "session_charts.chart_gap");
    ctx.addToggleControl("Pace", (hud->m_enabledCharts & SessionChartsHud::CHART_PACE) != 0,
        SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledCharts, SessionChartsHud::CHART_PACE, true,
        "session_charts.chart_pace");
    ctx.addSpacing(0.5f);

    // === LAYOUT SECTION ===
    ctx.addSectionHeader("Layout");

    // Rider line colours
    ctx.addCycleControl("Colors", getColorModeName(hud->m_riderColorMode), 10,
        SettingsHud::CycleControl::enumMember(hud, &SessionChartsHud::m_riderColorMode,
            static_cast<int>(SessionChartsHud::RiderColorMode::COLOR_MODE_COUNT), hud),
        hud, true, false, "session_charts.colors", /*tooltipOnArrows=*/false);

    // Total rider lines (top-N + player window). Order matches the Standings HUD:
    // Rows to show first, then the Top positions pinned within it.
    // Both are plain ±1 clamped steppers with deliberately NO hold acceleration
    // (verbatim from the old handlers), so they use fixedInt. Arrows never had a
    // per-type tooltip. postStep keeps the pinned top-N no larger than the total
    // rows drawn (same as the old ROW_COUNT handler).
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", hud->m_displayRowCount);
    SettingsHud::SteppedControl rowsControl = SettingsHud::SteppedControl::fixedInt(
        &hud->m_displayRowCount, 1,
        SessionChartsHud::MIN_ROW_COUNT, SessionChartsHud::MAX_ROW_COUNT, hud);
    rowsControl.postStep = [hud]() {
        hud->m_topPositionsCount = std::min(hud->m_topPositionsCount, hud->m_displayRowCount);
    };
    ctx.addSteppedControl("Rows to show", buf, 10, rowsControl,
        hud, true, false, "session_charts.rows", /*tooltipOnArrows=*/false);

    // Top-N pinned leaders (label harmonized with the Standings HUD). The upper
    // bound tracks the rows drawn; it's re-resolved on every rebuild, and the
    // Rows control above re-clamps this value whenever it shrinks.
    snprintf(buf, sizeof(buf), "%d", hud->m_topPositionsCount);
    ctx.addSteppedControl("Top positions", buf, 10,
        SettingsHud::SteppedControl::fixedInt(&hud->m_topPositionsCount, 1,
            SessionChartsHud::MIN_TOP_COUNT,
            std::min(SessionChartsHud::MAX_TOP_COUNT, hud->m_displayRowCount), hud),
        hud, true, false, "session_charts.top_n", /*tooltipOnArrows=*/false);

    ctx.addSpacing(0.5f);

    // === CONTENT SECTION ===
    ctx.addSectionHeader("Content");

    ctx.addToggleControl("Grid lines", (hud->m_enabledElements & SessionChartsHud::ELEM_GRID) != 0,
        SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledElements, SessionChartsHud::ELEM_GRID, true,
        "session_charts.grid");
    ctx.addToggleControl("Axis labels", (hud->m_enabledElements & SessionChartsHud::ELEM_AXIS_LABELS) != 0,
        SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledElements, SessionChartsHud::ELEM_AXIS_LABELS, true,
        "session_charts.axis_labels");
    ctx.addToggleControl("Line labels", (hud->m_enabledElements & SessionChartsHud::ELEM_LEGEND) != 0,
        SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledElements, SessionChartsHud::ELEM_LEGEND, true,
        "session_charts.legend");
    ctx.addToggleControl("Reference line", (hud->m_enabledElements & SessionChartsHud::ELEM_ZERO_LINE) != 0,
        SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledElements, SessionChartsHud::ELEM_ZERO_LINE, true,
        "session_charts.zero_line");
    ctx.addToggleControl("Lap dots", (hud->m_enabledElements & SessionChartsHud::ELEM_DOTS) != 0,
        SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledElements, SessionChartsHud::ELEM_DOTS, true,
        "session_charts.dots");
    ctx.addToggleControl("Filter outliers", (hud->m_enabledElements & SessionChartsHud::ELEM_FILTER_OUTLIERS) != 0,
        SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledElements, SessionChartsHud::ELEM_FILTER_OUTLIERS, true,
        "session_charts.filter_outliers");

    return hud;
}
