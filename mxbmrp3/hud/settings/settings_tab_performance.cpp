// ============================================================================
// hud/settings/settings_tab_performance.cpp
// Tab renderer for Performance HUD settings
// ============================================================================
#include "settings_layout.h"
#include "../settings_hud.h"
#include "../performance_hud.h"

// Static member function of SettingsHud - inherits friend access to PerformanceHud
BaseHud* SettingsHud::renderTabPerformance(SettingsLayoutContext& ctx) {
    PerformanceHud* hud = ctx.parent->getPerformanceHud();
    if (!hud) return nullptr;

    ctx.addTabTooltip("performance");

    // === APPEARANCE SECTION ===
    ctx.addSectionHeader("Appearance");
    ctx.addStandardHudControls(hud);
    ctx.addSpacing(0.5f);

    // === DISPLAY MODE SECTION ===
    ctx.addSectionHeader("Display Mode");

    // Display mode cycle control
    const char* modeText = "";
    switch (hud->m_displayMode) {
        case 0: modeText = "Graphs"; break;
        case 1: modeText = "Numbers"; break;
        case 2: modeText = "Both"; break;
    }
    ctx.addCycleControl("Style", modeText, 10,
        SettingsHud::ClickRegion::DISPLAY_MODE_DOWN,
        SettingsHud::ClickRegion::DISPLAY_MODE_UP,
        hud, true, false, "performance.display", &hud->m_displayMode);
    ctx.addSpacing(0.5f);

    // === ELEMENTS SECTION ===
    ctx.addSectionHeader("Elements");

    ctx.addToggleControl("Frames per second", (hud->m_enabledElements & PerformanceHud::ELEM_FPS) != 0,
        SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledElements, PerformanceHud::ELEM_FPS, true,
        "performance.fps");
    ctx.addToggleControl("CPU usage", (hud->m_enabledElements & PerformanceHud::ELEM_CPU) != 0,
        SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledElements, PerformanceHud::ELEM_CPU, true,
        "performance.cpu");

    return hud;
}
