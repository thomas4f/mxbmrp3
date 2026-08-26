// ============================================================================
// hud/settings/settings_tab_pitboard.cpp
// Tab renderer for Pitboard HUD settings
// ============================================================================
#include "settings_layout.h"
#include "../settings_hud.h"
#include "../pitboard_hud.h"
#include "../../core/asset_manager.h"

// Note: the Pitboard tab has no tab-specific click handler anymore - Show mode
// and Gap compare are data-driven CYCLE controls (registered in
// renderTabPitboard via ctx.addCycleControl) and the rest uses the common
// handlers.

// Static member function of SettingsHud - inherits friend access to PitboardHud
BaseHud* SettingsHud::renderTabPitboard(SettingsLayoutContext& ctx) {
    PitboardHud* hud = ctx.parent->getPitboardHud();
    if (!hud) return nullptr;

    ctx.addTabTooltip("pitboard");

    // === APPEARANCE SECTION ===
    ctx.addSectionHeading("Appearance");
    // The BOARD PACK (artwork plus the geometry that places rows on it) is the
    // Texture row addStandardHudControls draws for a pack HUD -- see
    // SettingsLayoutContext::addPackControl. It used to be a SECOND control here
    // called "Board", sitting under a Theme row that could never take effect;
    // one row named the same thing every other HUD names it replaces both.
    ctx.addStandardHudControls(hud);

    // === LAYOUT SECTION ===
    ctx.addSectionHeading("Layout");

    // Display mode control (Always/Pit/Splits)
    const char* displayModeText = "";
    if (hud->m_displayMode == PitboardHud::MODE_ALWAYS) {
        displayModeText = "Always";
    } else if (hud->m_displayMode == PitboardHud::MODE_PIT) {
        displayModeText = "At Pit";
    } else if (hud->m_displayMode == PitboardHud::MODE_SPLITS) {
        displayModeText = "At Splits";
    }
    ctx.addCycleControl("Show mode", displayModeText, 10,
        SettingsHud::CycleControl::enumMember(hud, &PitboardHud::m_displayMode,
            PitboardHud::MODE_COUNT, hud),
        hud, true, false, "pitboard.show_mode");
    // === CONTENT SECTION ===
    ctx.addSectionHeading("Content");

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
    ctx.addToggleControl("Gap row", (hud->m_enabledRows & PitboardHud::ROW_GAP) != 0,
        SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledRows, PitboardHud::ROW_GAP, true,
        "pitboard.gap");

    // Gap compare mode (Auto/Leader/Session PB/Ideal/Alltime PB/Overall/Record)
    const char* gapModeText = "";
    switch (hud->m_gapCompareMode) {
        case PitboardHud::GAP_AUTO:       gapModeText = "Auto"; break;
        case PitboardHud::GAP_LEADER:     gapModeText = "Leader"; break;
        case PitboardHud::GAP_SESSION_PB: gapModeText = "Session PB"; break;
        case PitboardHud::GAP_IDEAL:      gapModeText = "Ideal"; break;
        case PitboardHud::GAP_ALLTIME_PB: gapModeText = "Alltime PB"; break;
        case PitboardHud::GAP_OVERALL:    gapModeText = "Overall"; break;
        case PitboardHud::GAP_RECORD:     gapModeText = "Record"; break;
        default: gapModeText = "Auto"; break;
    }
    bool gapEnabled = (hud->m_enabledRows & PitboardHud::ROW_GAP) != 0;
    ctx.addCycleControl("Gap compare", gapModeText, 10,
        SettingsHud::CycleControl::enumMember(hud, &PitboardHud::m_gapCompareMode,
            PitboardHud::GAP_COUNT, hud),
        hud, gapEnabled, false, "pitboard.gap_compare");

    return hud;
}
