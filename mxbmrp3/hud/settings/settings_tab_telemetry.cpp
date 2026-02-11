// ============================================================================
// hud/settings/settings_tab_telemetry.cpp
// Tab renderer for Telemetry HUD settings
// ============================================================================
#include "settings_layout.h"
#include "../settings_hud.h"
#include "../telemetry_hud.h"

// Static member function of SettingsHud - inherits friend access to TelemetryHud
BaseHud* SettingsHud::renderTabTelemetry(SettingsLayoutContext& ctx) {
    TelemetryHud* hud = ctx.parent->getTelemetryHud();
    if (!hud) return nullptr;

    ctx.addTabTooltip("telemetry");

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
        hud, true, false, "telemetry.display", &hud->m_displayMode);
    ctx.addSpacing(0.5f);

    // === ELEMENTS SECTION ===
    ctx.addSectionHeader("Elements");

    ctx.addToggleControl("Throttle", (hud->m_enabledElements & TelemetryHud::ELEM_THROTTLE) != 0,
        SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledElements, TelemetryHud::ELEM_THROTTLE, true,
        "telemetry.throttle");
    ctx.addToggleControl("Front brake", (hud->m_enabledElements & TelemetryHud::ELEM_FRONT_BRAKE) != 0,
        SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledElements, TelemetryHud::ELEM_FRONT_BRAKE, true,
        "telemetry.front_brake");
    ctx.addToggleControl("Rear brake", (hud->m_enabledElements & TelemetryHud::ELEM_REAR_BRAKE) != 0,
        SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledElements, TelemetryHud::ELEM_REAR_BRAKE, true,
        "telemetry.rear_brake");
    ctx.addToggleControl("Clutch", (hud->m_enabledElements & TelemetryHud::ELEM_CLUTCH) != 0,
        SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledElements, TelemetryHud::ELEM_CLUTCH, true,
        "telemetry.clutch");
    ctx.addToggleControl("RPM", (hud->m_enabledElements & TelemetryHud::ELEM_RPM) != 0,
        SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledElements, TelemetryHud::ELEM_RPM, true,
        "telemetry.rpm");
    ctx.addToggleControl("Front suspension", (hud->m_enabledElements & TelemetryHud::ELEM_FRONT_SUSP) != 0,
        SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledElements, TelemetryHud::ELEM_FRONT_SUSP, true,
        "telemetry.front_susp");
    ctx.addToggleControl("Rear suspension", (hud->m_enabledElements & TelemetryHud::ELEM_REAR_SUSP) != 0,
        SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledElements, TelemetryHud::ELEM_REAR_SUSP, true,
        "telemetry.rear_susp");
    ctx.addToggleControl("Gear indicator", (hud->m_enabledElements & TelemetryHud::ELEM_GEAR) != 0,
        SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledElements, TelemetryHud::ELEM_GEAR, true,
        "telemetry.gear");

    return hud;
}
