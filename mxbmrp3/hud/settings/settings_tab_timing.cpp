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

        case ClickRegion::TIMING_DISPLAY_MODE_UP:
        case ClickRegion::TIMING_DISPLAY_MODE_DOWN:
            // Toggle between Splits and Always
            if (m_timing) {
                m_timing->m_displayMode = (m_timing->m_displayMode == ColumnMode::SPLITS)
                    ? ColumnMode::ALWAYS : ColumnMode::SPLITS;
                m_timing->setDataDirty();
                setDataDirty();
            }
            return true;

        case ClickRegion::TIMING_DURATION_UP:
        case ClickRegion::TIMING_DURATION_DOWN:
            // Cycle freeze duration: Off -> 1s -> 2s -> ... -> 10s -> Off
            if (m_timing) {
                bool forward = (region.type == ClickRegion::TIMING_DURATION_UP);
                m_timing->m_displayDurationMs = applyAcceleratedWrap(
                    m_timing->m_displayDurationMs, TimingHud::DURATION_STEP_MS,
                    TimingHud::MIN_DURATION_MS, TimingHud::MAX_DURATION_MS, forward);
                m_timing->setDataDirty();
                setDataDirty();
            }
            return true;

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
    ctx.addCycleControl("Show mode", showValue, 10,
        SettingsHud::ClickRegion::TIMING_DISPLAY_MODE_DOWN,
        SettingsHud::ClickRegion::TIMING_DISPLAY_MODE_UP,
        hud, true, false, "timing.show");

    // Freeze duration: how long to hold official times / gaps after crossing a split
    char freezeValue[16];
    bool freezeIsOff = (hud->m_displayDurationMs == 0);
    if (freezeIsOff) {
        strcpy_s(freezeValue, sizeof(freezeValue), "Off");
    } else {
        snprintf(freezeValue, sizeof(freezeValue), "%ds", hud->m_displayDurationMs / 1000);
    }
    ctx.addCycleControl("Freeze", freezeValue, 10,
        SettingsHud::ClickRegion::TIMING_DURATION_DOWN,
        SettingsHud::ClickRegion::TIMING_DURATION_UP,
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
