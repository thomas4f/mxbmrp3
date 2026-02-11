// ============================================================================
// hud/settings/settings_tab_fmx.cpp
// Tab renderer for FMX (Freestyle Motocross) HUD settings
// ============================================================================
#include "settings_layout.h"
#include "../settings_hud.h"
#include "../fmx_hud.h"
#include "../../core/fmx_manager.h"
#include "../../core/settings_manager.h"

// Static member function of SettingsHud - handles click events for FMX tab
bool SettingsHud::handleClickTabFmx(const ClickRegion& region) {
    FmxHud* hud = dynamic_cast<FmxHud*>(region.targetHud);
    if (!hud) hud = m_fmxHud;
    if (!hud) return false;

    switch (region.type) {
        case ClickRegion::FMX_DEBUG_TOGGLE:
            hud->m_showDebugLogging = !hud->m_showDebugLogging;
            FmxManager::getInstance().setLoggingEnabled(hud->m_showDebugLogging);
            hud->setDataDirty();
            setDataDirty();
            return true;

        case ClickRegion::FMX_CHAIN_ROWS_UP:
            hud->m_maxChainDisplayRows = std::min(10, hud->m_maxChainDisplayRows + 1);
            hud->setDataDirty();
            setDataDirty();
            return true;

        case ClickRegion::FMX_CHAIN_ROWS_DOWN:
            hud->m_maxChainDisplayRows = std::max(0, hud->m_maxChainDisplayRows - 1);
            hud->setDataDirty();
            setDataDirty();
            return true;

        default:
            return false;
    }
}

// Static member function of SettingsHud - inherits friend access to FmxHud
BaseHud* SettingsHud::renderTabFmx(SettingsLayoutContext& ctx) {
    FmxHud* hud = ctx.parent->getFmxHud();
    if (!hud) return nullptr;

    ctx.addTabTooltip("fmx");

    ctx.addStandardHudControls(hud);
    ctx.addSpacing(0.5f);

    // === DISPLAY ELEMENTS SECTION (per-profile) ===
    ctx.addSectionHeader("Display Elements");

    // Trick stack: cycle Off, 1, 2, ..., 10
    int trickRows = hud->m_maxChainDisplayRows;
    char rowsValue[8];
    if (trickRows == 0) {
        snprintf(rowsValue, sizeof(rowsValue), "Off");
    } else {
        snprintf(rowsValue, sizeof(rowsValue), "%d", trickRows);
    }
    ctx.addCycleControl("Trick stack", rowsValue, 10,
        SettingsHud::ClickRegion::FMX_CHAIN_ROWS_DOWN,
        SettingsHud::ClickRegion::FMX_CHAIN_ROWS_UP,
        hud, true, trickRows == 0, "fmx.chain_rows");

    // Trick stats only relevant when trick stack is enabled
    bool trickStackEnabled = trickRows > 0;
    ctx.addToggleControl("Trick stats", (hud->m_enabledRows & FmxHud::ROW_TRICK_STATS) != 0,
        SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledRows, FmxHud::ROW_TRICK_STATS,
        trickStackEnabled, "fmx.row_trick_stats");

    ctx.addToggleControl("Combo arc & score", (hud->m_enabledRows & FmxHud::ROW_COMBO_ARC) != 0,
        SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledRows, FmxHud::ROW_COMBO_ARC, true,
        "fmx.row_combo_arc");

    ctx.addToggleControl("Rotation arcs", (hud->m_enabledRows & FmxHud::ROW_ARCS) != 0,
        SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledRows, FmxHud::ROW_ARCS, true,
        "fmx.row_arcs");

    // === DEVELOPER SECTION (only visible in developer mode) ===
    if (SettingsManager::getInstance().isDeveloperMode()) {
        ctx.addSpacing(0.5f);
        ctx.addSectionHeader("Developer");

        ctx.addToggleControl("Log telemetry", hud->m_showDebugLogging,
            SettingsHud::ClickRegion::FMX_DEBUG_TOGGLE, hud, nullptr, 0, true,
            "fmx.debug_logging");

        ctx.addToggleControl("Debug values", (hud->m_enabledRows & FmxHud::ROW_DEBUG_VALUES) != 0,
            SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledRows, FmxHud::ROW_DEBUG_VALUES, true,
            "fmx.row_debug_values");
    }

    return hud;
}
