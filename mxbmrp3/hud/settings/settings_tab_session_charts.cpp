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

// Static member of SettingsHud - handles clicks for the Session Charts tab.
bool SettingsHud::handleClickTabSessionCharts(const ClickRegion& region) {
    SessionChartsHud* hud = dynamic_cast<SessionChartsHud*>(region.targetHud);
    if (!hud) hud = m_sessionCharts;
    if (!hud) return false;

    switch (region.type) {
        case ClickRegion::SESSION_CHARTS_COLOR_MODE_UP:
        case ClickRegion::SESSION_CHARTS_COLOR_MODE_DOWN: {
            int n = static_cast<int>(SessionChartsHud::RiderColorMode::COLOR_MODE_COUNT);
            int cur = static_cast<int>(hud->m_riderColorMode);
            cur = (region.type == ClickRegion::SESSION_CHARTS_COLOR_MODE_UP)
                    ? (cur + 1) % n : (cur - 1 + n) % n;
            hud->m_riderColorMode = static_cast<SessionChartsHud::RiderColorMode>(cur);
            hud->setDataDirty();
            setDataDirty();
            return true;
        }

        case ClickRegion::SESSION_CHARTS_TOP_COUNT_UP:
        case ClickRegion::SESSION_CHARTS_TOP_COUNT_DOWN: {
            int step = (region.type == ClickRegion::SESSION_CHARTS_TOP_COUNT_UP) ? 1 : -1;
            int maxTop = std::min(SessionChartsHud::MAX_TOP_COUNT, hud->m_displayRowCount);
            hud->m_topPositionsCount = std::max(SessionChartsHud::MIN_TOP_COUNT,
                                                std::min(hud->m_topPositionsCount + step, maxTop));
            hud->setDataDirty();
            setDataDirty();
            return true;
        }

        case ClickRegion::SESSION_CHARTS_ROW_COUNT_UP:
        case ClickRegion::SESSION_CHARTS_ROW_COUNT_DOWN: {
            int step = (region.type == ClickRegion::SESSION_CHARTS_ROW_COUNT_UP) ? 1 : -1;
            hud->m_displayRowCount = std::max(SessionChartsHud::MIN_ROW_COUNT,
                                              std::min(hud->m_displayRowCount + step, SessionChartsHud::MAX_ROW_COUNT));
            // Keep the pinned top-N no larger than the total rows drawn.
            hud->m_topPositionsCount = std::min(hud->m_topPositionsCount, hud->m_displayRowCount);
            hud->setDataDirty();
            setDataDirty();
            return true;
        }

        default:
            return false;
    }
}

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
        SettingsHud::ClickRegion::SESSION_CHARTS_COLOR_MODE_DOWN,
        SettingsHud::ClickRegion::SESSION_CHARTS_COLOR_MODE_UP,
        hud, true, false, "session_charts.colors");

    // Total rider lines (top-N + player window). Order matches the Standings HUD:
    // Rows to show first, then the Top positions pinned within it.
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", hud->m_displayRowCount);
    ctx.addCycleControl("Rows to show", buf, 10,
        SettingsHud::ClickRegion::SESSION_CHARTS_ROW_COUNT_DOWN,
        SettingsHud::ClickRegion::SESSION_CHARTS_ROW_COUNT_UP,
        hud, true, false, "session_charts.rows");

    // Top-N pinned leaders (label harmonized with the Standings HUD)
    snprintf(buf, sizeof(buf), "%d", hud->m_topPositionsCount);
    ctx.addCycleControl("Top positions", buf, 10,
        SettingsHud::ClickRegion::SESSION_CHARTS_TOP_COUNT_DOWN,
        SettingsHud::ClickRegion::SESSION_CHARTS_TOP_COUNT_UP,
        hud, true, false, "session_charts.top_n");

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
