// ============================================================================
// hud/settings/settings_tab_delta_trace.cpp
// Tab renderer for Delta Trace HUD settings
// ============================================================================
#include "settings_layout.h"
#include "../settings_hud.h"
#include "../delta_trace_hud.h"
#include "../../core/hud_manager.h"

// Static member function of SettingsHud - inherits friend access to DeltaTraceHud
BaseHud* SettingsHud::renderTabDeltaTrace(SettingsLayoutContext& ctx) {
    DeltaTraceHud* hud = &HudManager::getInstance().getDeltaTraceHud();

    ctx.addTabTooltip("delta_trace");

    // === APPEARANCE SECTION ===
    ctx.addSectionHeading("Appearance");
    ctx.addStandardHudControls(hud);

    // === LAYOUT SECTION ===
    ctx.addSectionHeading("Layout");
    {
        char rowsBuf[8];
        snprintf(rowsBuf, sizeof(rowsBuf), "%d", hud->m_graphRows);
        ctx.addSteppedControl("Graph height", rowsBuf,
            SettingsHud::SteppedControl::clampInt(&hud->m_graphRows, 1,
                DeltaTraceHud::MIN_GRAPH_ROWS, DeltaTraceHud::MAX_GRAPH_ROWS, hud),
            hud, true, false, "delta_trace.graph_rows");
    }

    // === CONTENT SECTION ===
    // Reference names as the Gap Bar and the Map's Lap delta spell them.
    ctx.addSectionHeading("Content");
    ctx.addReferenceControl("Gap reference", &hud->m_referenceDefault, &hud->m_reference, hud,
        "delta_trace.reference");

    // Peaks: where the lap was furthest ahead and furthest behind.
    {
        static const char* const kOnOff[] = { "Off", "On" };
        SettingsHud::CycleControl peaks;
        peaks.count = 2;
        peaks.get = [hud]() { return hud->m_showPeaks ? 1 : 0; };
        peaks.set = [hud](int v) { hud->m_showPeaks = (v != 0); };
        peaks.nameOf = [](int v) { return std::string(kOnOff[v]); };
        peaks.dirtyHud = hud;
        ctx.addCycleControl("Peaks", hud->m_showPeaks ? "On" : "Off", peaks, hud, true, !hud->m_showPeaks,
                            "delta_trace.peaks");
    }

    return hud;
}
