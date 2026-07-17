// ============================================================================
// hud/settings/settings_tab_timing.cpp
// Tab renderer for Timing HUD settings
// ============================================================================
#include "settings_layout.h"
#include "../settings_hud.h"
#include "../timing_hud.h"
#include "../../core/color_config.h"
#include "../../core/plugin_constants.h"

// Static member function of SettingsHud - handles click events for Timing tab
bool SettingsHud::handleClickTabTiming(const ClickRegion& region) {
    switch (region.type) {
        case ClickRegion::TIMING_TIME_TOGGLE:
            if (m_timing) {
                m_timing->setTimeEnabled(!m_timing->isTimeEnabled());
                setDataDirty();
            }
            return true;

        // Show mode (Splits/Always) is a data-driven CYCLE control now -
        // registered in renderTabTiming via ctx.addCycleControl.

        // Freeze duration is a data-driven STEPPED control now - registered in
        // renderTabTiming via ctx.addSteppedControl.

        // Comparison rows (one per gap type) — merged the old primary/secondary into a flat set.
        case ClickRegion::TIMING_GAP_PB_TOGGLE:
            if (m_timing) { m_timing->setComparisonEnabled(GAP_TO_PB, !m_timing->isComparisonEnabled(GAP_TO_PB)); setDataDirty(); }
            return true;
        case ClickRegion::TIMING_GAP_ALLTIME_TOGGLE:
            if (m_timing) { m_timing->setComparisonEnabled(GAP_TO_ALLTIME, !m_timing->isComparisonEnabled(GAP_TO_ALLTIME)); setDataDirty(); }
            return true;
        case ClickRegion::TIMING_GAP_IDEAL_TOGGLE:
            if (m_timing) { m_timing->setComparisonEnabled(GAP_TO_IDEAL, !m_timing->isComparisonEnabled(GAP_TO_IDEAL)); setDataDirty(); }
            return true;
        case ClickRegion::TIMING_GAP_OVERALL_TOGGLE:
            if (m_timing) { m_timing->setComparisonEnabled(GAP_TO_OVERALL, !m_timing->isComparisonEnabled(GAP_TO_OVERALL)); setDataDirty(); }
            return true;
        case ClickRegion::TIMING_GAP_LASTLAP_TOGGLE:
            if (m_timing) { m_timing->setComparisonEnabled(GAP_TO_LASTLAP, !m_timing->isComparisonEnabled(GAP_TO_LASTLAP)); setDataDirty(); }
            return true;
        case ClickRegion::TIMING_GAP_RECORD_TOGGLE:
            if (m_timing) { m_timing->setComparisonEnabled(GAP_TO_RECORD, !m_timing->isComparisonEnabled(GAP_TO_RECORD)); setDataDirty(); }
            return true;

        default:
            return false;
    }
}

// Static member function of SettingsHud - inherits friend access to TimingHud
BaseHud* SettingsHud::renderTabTiming(SettingsLayoutContext& ctx) {
    TimingHud* hud = ctx.parent->getTimingHud();
    if (!hud) return nullptr;

    ctx.addTabTooltip("timing");

    // === APPEARANCE SECTION ===
    ctx.addSectionHeader("Appearance");
    ctx.addStandardHudControls(hud, false);  // No title support (center display)
    ctx.addSpacing(0.5f);

    // === LAYOUT SECTION ===
    ctx.addSectionHeader("Layout");

    // Show mode: Splits (only after crossing splits) or Always
    const char* showValue = (hud->m_displayMode == ColumnMode::ALWAYS) ? "Always" : "At Splits";
    // Two-state cycle over {SPLITS, ALWAYS}: ColumnMode also has OFF, which
    // this control never selects, so the get/set lambdas map the pair onto a
    // 0/1 index instead of cycling the raw enum range.
    {
        SettingsHud::CycleControl showCycle;
        showCycle.get = [hud]() { return hud->m_displayMode == ColumnMode::ALWAYS ? 1 : 0; };
        showCycle.set = [hud](int v) { hud->m_displayMode = v ? ColumnMode::ALWAYS : ColumnMode::SPLITS; };
        showCycle.count = 2;
        showCycle.dirtyHud = hud;
        ctx.addCycleControl("Show mode", showValue, 10, showCycle,
            hud, true, false, "timing.show");
    }

    // Freeze duration: how long to hold official times / gaps after crossing a split
    char freezeValue[16];
    bool freezeIsOff = (hud->m_displayDurationMs == 0);
    if (freezeIsOff) {
        strcpy_s(freezeValue, sizeof(freezeValue), "Off");
    } else {
        snprintf(freezeValue, sizeof(freezeValue), "%ds", hud->m_displayDurationMs / 1000);
    }
    ctx.addSteppedControl("Freeze", freezeValue, 10,
        SettingsHud::SteppedControl::wrapInt(&hud->m_displayDurationMs,
            TimingHud::DURATION_STEP_MS, TimingHud::MIN_DURATION_MS, TimingHud::MAX_DURATION_MS, hud),
        hud, true, freezeIsOff, "timing.freeze");

    // Big time row toggle
    ctx.addToggleControl("Time", hud->isTimeEnabled(),
        SettingsHud::ClickRegion::TIMING_TIME_TOGGLE, hud, nullptr, 0, true,
        "timing.time");

    ctx.addSpacing(0.5f);

    // === COMPARISONS SECTION ===
    // Each enabled comparison is one row (name + value). No primary/secondary distinction.
    ctx.addSectionHeader("Comparisons");

    ctx.addToggleControl("Session PB", hud->isComparisonEnabled(GAP_TO_PB),
        SettingsHud::ClickRegion::TIMING_GAP_PB_TOGGLE, hud, nullptr, 0, true, "timing.gap_pb");
    ctx.addToggleControl("Alltime PB", hud->isComparisonEnabled(GAP_TO_ALLTIME),
        SettingsHud::ClickRegion::TIMING_GAP_ALLTIME_TOGGLE, hud, nullptr, 0, true, "timing.gap_alltime");
    ctx.addToggleControl("Ideal", hud->isComparisonEnabled(GAP_TO_IDEAL),
        SettingsHud::ClickRegion::TIMING_GAP_IDEAL_TOGGLE, hud, nullptr, 0, true, "timing.gap_ideal");
    ctx.addToggleControl("Overall", hud->isComparisonEnabled(GAP_TO_OVERALL),
        SettingsHud::ClickRegion::TIMING_GAP_OVERALL_TOGGLE, hud, nullptr, 0, true, "timing.gap_overall");
    ctx.addToggleControl("Last Lap", hud->isComparisonEnabled(GAP_TO_LASTLAP),
        SettingsHud::ClickRegion::TIMING_GAP_LASTLAP_TOGGLE, hud, nullptr, 0, true, "timing.gap_lastlap");
    ctx.addToggleControl("Record", hud->isComparisonEnabled(GAP_TO_RECORD),
        SettingsHud::ClickRegion::TIMING_GAP_RECORD_TOGGLE, hud, nullptr, 0, true, "timing.gap_record");

    // Info text - same style as the other tab hints
    ColorConfig& colors = ColorConfig::getInstance();
    ctx.currentY += ctx.lineHeightNormal * 0.5f;
    ctx.parent->addString("Toggle PB scope (Bike/Category) in the General tab.", ctx.labelX, ctx.currentY,
        PluginConstants::Justify::LEFT, PluginConstants::Fonts::getNormal(),
        colors.getMuted(), ctx.fontSize * 0.9f);
    ctx.currentY += ctx.lineHeightNormal;
    ctx.parent->addString("Bind Segment Add/Remove (Hotkeys tab) for sections.", ctx.labelX, ctx.currentY,
        PluginConstants::Justify::LEFT, PluginConstants::Fonts::getNormal(),
        colors.getMuted(), ctx.fontSize * 0.9f);

    return hud;
}
