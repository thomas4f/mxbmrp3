// ============================================================================
// hud/settings/settings_tab_performance.cpp
// Tab renderer for Performance HUD settings
// ============================================================================
#include "settings_layout.h"
#include "../settings_hud.h"
#include "../../core/render_probe_sweep.h"
#include "../performance_hud.h"
#include "../benchmark_widget.h"
#include "../../core/settings_manager.h"
#include "../../core/hud_manager.h"

// Static member function of SettingsHud - inherits friend access to PerformanceHud
BaseHud* SettingsHud::renderTabPerformance(SettingsLayoutContext& ctx) {
    PerformanceHud* hud = ctx.parent->getPerformanceHud();
    if (!hud) return nullptr;

    ctx.addTabTooltip("performance");

    // === APPEARANCE SECTION ===
    ctx.addSectionHeading("Appearance");
    ctx.addStandardHudControls(hud);
    // === LAYOUT SECTION ===
    ctx.addSectionHeading("Layout");

    // Display mode cycle control
    const char* modeText = "";
    switch (hud->m_displayMode) {
        case 0: modeText = "Graphs"; break;
        case 1: modeText = "Numbers"; break;
        case 2: modeText = "Both"; break;
    }
    ctx.addCycleControl("Style", modeText, 10,
        SettingsHud::CycleControl::enumMember(hud, &PerformanceHud::m_displayMode, 3, hud),
        hud, true, false, "performance.display");

    // Panel HEIGHT, in text rows. The knob the Performance HUD was missing:
    // its graph was a fixed line count, so the panel could only be resized by the
    // scale slider, which moves the fonts too. One row is lineHeightNormal, so every
    // value keeps the panel a whole number of grid cells tall.
    {
        char rowsBuf[8];
        snprintf(rowsBuf, sizeof(rowsBuf), "%d", hud->m_graphRows);
        ctx.addSteppedControl("Graph height", rowsBuf, 10,
            SettingsHud::SteppedControl::clampInt(&hud->m_graphRows, 1,
                PerformanceHud::MIN_GRAPH_ROWS, PerformanceHud::MAX_GRAPH_ROWS, hud),
            hud, true, false, "performance.graph_rows");
    }
    // === CONTENT SECTION ===
    ctx.addSectionHeading("Content");

    ctx.addToggleControl("Frames per second", (hud->m_enabledElements & PerformanceHud::ELEM_FPS) != 0,
        SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledElements, PerformanceHud::ELEM_FPS, true,
        "performance.fps");
    ctx.addToggleControl("Plugin time", (hud->m_enabledElements & PerformanceHud::ELEM_CPU) != 0,
        SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledElements, PerformanceHud::ELEM_CPU, true,
        "performance.cpu");

    // === DEVELOPER SECTION (only visible in developer mode) ===
    if (SettingsManager::getInstance().isDeveloperMode()) {
        BenchmarkWidget* benchmark = HudManager::getInstance().getBenchmarkWidget();
        if (benchmark) {
            ctx.addSectionHeading("Developer");
            // Active surface: the row emits HUD_TOGGLE, which edits the focused
            // surface, so the state shown must come from the same one.
            ctx.addToggleControl("Benchmark profiler", benchmark->isVisibleOnActiveSurface(),
                SettingsHud::ClickRegion::HUD_TOGGLE, benchmark, nullptr, 0, true,
                "performance.benchmark");

            // The render-probe sweep, beside the profiler it complements: that one
            // measures OUR cpu per callback and per panel, this one measures what the
            // engine charges to draw what we hand it -- a cost no in-plugin timer can
            // see. It lives here rather than on a hotkey because it is a thing you run
            // deliberately, once, while looking at the screen: it takes about a minute,
            // drops the frame rate on purpose, and writes a report. A key binding for
            // that is a key you press by accident.
            // The gap the Updates tab puts before Check Now. A button hard against
            // the row above it reads as part of that row rather than as its own act.
            ctx.addSpacing();

            RenderProbeSweep& sweep = RenderProbeSweep::getInstance();
            const bool running = sweep.isRunning();
            // Both labels 9 characters, so the button does not resize mid-sweep.
            ctx.addActionButton(running ? "  Stop   " : "Run sweep", 11,
                                SettingsHud::ClickRegion::PROBE_SWEEP,
                                running ? SettingsLayoutContext::ButtonRole::Negative
                                        : SettingsLayoutContext::ButtonRole::Positive,
                                true);
            ctx.addInlineNote(running ? "Sweeping - about a minute, frame rate drops."
                                      : "Measures the engine's per-primitive draw cost.");
        }
    }

    return hud;
}

// The Developer section's one button. Everything else on this tab is a generic
// control, which is why this handler did not exist until the sweep needed it.
bool SettingsHud::handleClickTabPerformance(const ClickRegion& region) {
    if (region.type != ClickRegion::PROBE_SWEEP) return false;
    RenderProbeSweep& sweep = RenderProbeSweep::getInstance();
    // One button for both edges: a sweep you cannot stop from where you started it is
    // a sweep you have to sit through. The abort path still writes its report.
    if (sweep.isRunning()) sweep.abort(); else sweep.start();
    setDataDirty();
    return true;
}
