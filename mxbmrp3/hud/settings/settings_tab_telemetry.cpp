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
    ctx.addSectionHeading(TR("Appearance"));
    ctx.addStandardHudControls(hud);
    // === LAYOUT SECTION ===
    ctx.addSectionHeading(TR("Layout"));

    // Display mode dropdown
    static const char* const kStyles[] = { N_("Graphs"), N_("Numbers"), N_("Both") };
    ctx.addCycleControl(TR("Style"), cycleName(kStyles, hud->m_displayMode),
        SettingsHud::CycleControl::enumMember(hud, &TelemetryHud::m_displayMode, 3, hud, kStyles),
        hud, true, false, "telemetry.display");

    // Panel HEIGHT, in text rows. The knob the Telemetry HUD was missing: its graph
    // was a fixed line count, so the panel could only be resized by the scale slider,
    // which moves the fonts too. One row is lineHeightNormal, so every value keeps the
    // panel a whole number of grid cells tall.
    {
        char rowsBuf[8];
        snprintf(rowsBuf, sizeof(rowsBuf), "%d", hud->m_graphRows);
        ctx.addSteppedControl(TR("Graph height"), rowsBuf,
            SettingsHud::SteppedControl::clampInt(&hud->m_graphRows, 1,
                TelemetryHud::MIN_GRAPH_ROWS, TelemetryHud::MAX_GRAPH_ROWS, hud),
            hud, true, false, "telemetry.graph_rows");
    }
    // === CONTENT SECTION ===
    ctx.addSectionHeading(TR("Content"));

    ctx.addToggleControl(TR("Throttle"), (hud->m_enabledElements & TelemetryHud::ELEM_THROTTLE) != 0,
        SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledElements, TelemetryHud::ELEM_THROTTLE, true,
        "telemetry.throttle");
    ctx.addToggleControl(TR("Front brake"), (hud->m_enabledElements & TelemetryHud::ELEM_FRONT_BRAKE) != 0,
        SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledElements, TelemetryHud::ELEM_FRONT_BRAKE, true,
        "telemetry.front_brake");
    ctx.addToggleControl(TR("Rear brake"), (hud->m_enabledElements & TelemetryHud::ELEM_REAR_BRAKE) != 0,
        SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledElements, TelemetryHud::ELEM_REAR_BRAKE, true,
        "telemetry.rear_brake");
    ctx.addToggleControl(TR("Clutch"), (hud->m_enabledElements & TelemetryHud::ELEM_CLUTCH) != 0,
        SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledElements, TelemetryHud::ELEM_CLUTCH, true,
        "telemetry.clutch");
    ctx.addToggleControl(TR("RPM"), (hud->m_enabledElements & TelemetryHud::ELEM_RPM) != 0,
        SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledElements, TelemetryHud::ELEM_RPM, true,
        "telemetry.rpm");
    ctx.addToggleControl(TR("Front suspension"), (hud->m_enabledElements & TelemetryHud::ELEM_FRONT_SUSP) != 0,
        SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledElements, TelemetryHud::ELEM_FRONT_SUSP, true,
        "telemetry.front_susp");
    ctx.addToggleControl(TR("Rear suspension"), (hud->m_enabledElements & TelemetryHud::ELEM_REAR_SUSP) != 0,
        SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledElements, TelemetryHud::ELEM_REAR_SUSP, true,
        "telemetry.rear_susp");
    ctx.addToggleControl(TR("Gear indicator"), (hud->m_enabledElements & TelemetryHud::ELEM_GEAR) != 0,
        SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledElements, TelemetryHud::ELEM_GEAR, true,
        "telemetry.gear");

    return hud;
}
