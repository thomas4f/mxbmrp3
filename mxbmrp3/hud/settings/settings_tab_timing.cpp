// ============================================================================
// hud/settings/settings_tab_timing.cpp
// Tab renderer for Timing HUD settings
// ============================================================================
#include "settings_layout.h"
#include "../settings_hud.h"
#include "../timing_hud.h"

// Static member function of SettingsHud - handles click events for Timing tab
bool SettingsHud::handleClickTabTiming(const ClickRegion& region) {
    switch (region.type) {
        case ClickRegion::TIMING_LABEL_TOGGLE:
            if (m_timing) {
                m_timing->m_columnEnabled[TimingHud::COL_LABEL] = !m_timing->m_columnEnabled[TimingHud::COL_LABEL];
                m_timing->setDataDirty();
                setDataDirty();
            }
            return true;

        case ClickRegion::TIMING_TIME_TOGGLE:
            if (m_timing) {
                m_timing->m_columnEnabled[TimingHud::COL_TIME] = !m_timing->m_columnEnabled[TimingHud::COL_TIME];
                m_timing->setDataDirty();
                setDataDirty();
            }
            return true;

        case ClickRegion::TIMING_GAP_UP:
        case ClickRegion::TIMING_GAP_DOWN:
            if (m_timing) {
                bool forward = (region.type == ClickRegion::TIMING_GAP_UP);
                bool& gapEnabled = m_timing->m_columnEnabled[TimingHud::COL_GAP];

                if (!gapEnabled) {
                    // Off -> enable with first/last gap type
                    gapEnabled = true;
                    m_timing->m_primaryGapType = forward ? GAP_TYPE_INFO[0].flag : GAP_TYPE_INFO[GAP_TYPE_COUNT - 1].flag;
                } else {
                    // Find current gap type index
                    int currentIdx = -1;
                    for (int i = 0; i < GAP_TYPE_COUNT; i++) {
                        if (GAP_TYPE_INFO[i].flag == m_timing->m_primaryGapType) {
                            currentIdx = i;
                            break;
                        }
                    }

                    if (forward) {
                        if (currentIdx >= GAP_TYPE_COUNT - 1) {
                            // Last type -> Off
                            gapEnabled = false;
                        } else {
                            // Next type
                            m_timing->m_primaryGapType = GAP_TYPE_INFO[currentIdx + 1].flag;
                        }
                    } else {
                        if (currentIdx <= 0) {
                            // First type -> Off
                            gapEnabled = false;
                        } else {
                            // Previous type
                            m_timing->m_primaryGapType = GAP_TYPE_INFO[currentIdx - 1].flag;
                        }
                    }
                }
                m_timing->setDataDirty();
                setDataDirty();
            }
            return true;

        case ClickRegion::TIMING_DISPLAY_MODE_UP:
        case ClickRegion::TIMING_DISPLAY_MODE_DOWN:
            // Toggle between Splits and Always
            if (m_timing) {
                if (m_timing->m_displayMode == ColumnMode::SPLITS) {
                    m_timing->m_displayMode = ColumnMode::ALWAYS;
                } else {
                    m_timing->m_displayMode = ColumnMode::SPLITS;
                }
                m_timing->setDataDirty();
                setDataDirty();
            }
            return true;

        case ClickRegion::TIMING_DURATION_UP:
        case ClickRegion::TIMING_DURATION_DOWN:
            // Cycle freeze duration: Off -> 1s -> 2s -> ... -> 10s -> Off
            if (m_timing) {
                bool forward = (region.type == ClickRegion::TIMING_DURATION_UP);
                int& duration = m_timing->m_displayDurationMs;

                if (forward) {
                    if (duration >= TimingHud::MAX_DURATION_MS) {
                        duration = 0;  // Wrap to Off
                    } else {
                        duration += TimingHud::DURATION_STEP_MS;
                    }
                } else {
                    if (duration <= 0) {
                        duration = TimingHud::MAX_DURATION_MS;  // Wrap to 10s
                    } else {
                        duration -= TimingHud::DURATION_STEP_MS;
                    }
                }
                m_timing->setDataDirty();
                setDataDirty();
            }
            return true;

        case ClickRegion::TIMING_REFERENCE_TOGGLE:
            if (m_timing) {
                m_timing->m_showReference = !m_timing->m_showReference;
                m_timing->setDataDirty();
                setDataDirty();
            }
            return true;

        case ClickRegion::TIMING_LAYOUT_TOGGLE:
            if (m_timing) {
                m_timing->m_layoutVertical = !m_timing->m_layoutVertical;
                m_timing->setDataDirty();
                setDataDirty();
            }
            return true;

        case ClickRegion::TIMING_GAP_PB_TOGGLE:
            if (m_timing) {
                m_timing->setSecondaryGapType(GAP_TO_PB, !m_timing->isSecondaryGapEnabled(GAP_TO_PB));
                setDataDirty();
            }
            return true;

        case ClickRegion::TIMING_GAP_IDEAL_TOGGLE:
            if (m_timing) {
                m_timing->setSecondaryGapType(GAP_TO_IDEAL, !m_timing->isSecondaryGapEnabled(GAP_TO_IDEAL));
                setDataDirty();
            }
            return true;

        case ClickRegion::TIMING_GAP_OVERALL_TOGGLE:
            if (m_timing) {
                m_timing->setSecondaryGapType(GAP_TO_OVERALL, !m_timing->isSecondaryGapEnabled(GAP_TO_OVERALL));
                setDataDirty();
            }
            return true;

        case ClickRegion::TIMING_GAP_ALLTIME_TOGGLE:
            if (m_timing) {
                m_timing->setSecondaryGapType(GAP_TO_ALLTIME, !m_timing->isSecondaryGapEnabled(GAP_TO_ALLTIME));
                setDataDirty();
            }
            return true;

        case ClickRegion::TIMING_GAP_RECORD_TOGGLE:
            if (m_timing) {
                m_timing->setSecondaryGapType(GAP_TO_RECORD, !m_timing->isSecondaryGapEnabled(GAP_TO_RECORD));
                setDataDirty();
            }
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

    // Helper to get gap display text (Off or gap type name)
    auto getGapText = [&]() -> const char* {
        if (!hud->m_columnEnabled[TimingHud::COL_GAP]) {
            return "Off";
        }
        return TimingHud::getGapTypeName(hud->getPrimaryGapType());
    };

    bool gapEnabled = hud->m_columnEnabled[TimingHud::COL_GAP];

    // === DISPLAY SECTION ===
    ctx.addSectionHeader("Display");

    // Show mode: Splits (only after crossing splits) or Always
    const char* showValue = (hud->m_displayMode == ColumnMode::ALWAYS) ? "Always" : "Splits";
    ctx.addCycleControl("Show mode", showValue, 10,
        SettingsHud::ClickRegion::TIMING_DISPLAY_MODE_DOWN,
        SettingsHud::ClickRegion::TIMING_DISPLAY_MODE_UP,
        hud, true, false, "timing.show");

    // Freeze duration: how long to freeze gap values after crossing a split
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

    // Show reference toggle (applies to both primary and secondary gaps)
    ctx.addToggleControl("Show reference", hud->m_showReference,
        SettingsHud::ClickRegion::TIMING_REFERENCE_TOGGLE, hud, nullptr, 0, true,
        "timing.show_reference");

    // Layout toggle (Horizontal = primary row with secondary chips below, Vertical = columns side by side)
    ctx.addToggleControl("Vertical layout", hud->m_layoutVertical,
        SettingsHud::ClickRegion::TIMING_LAYOUT_TOGGLE, hud, nullptr, 0, true,
        "timing.layout");

    ctx.addSpacing(0.5f);

    // === PRIMARY ROW SECTION ===
    ctx.addSectionHeader("Primary Row");

    ctx.addToggleControl("Label", hud->m_columnEnabled[TimingHud::COL_LABEL],
        SettingsHud::ClickRegion::TIMING_LABEL_TOGGLE, hud, nullptr, 0, true,
        "timing.label");

    ctx.addToggleControl("Time", hud->m_columnEnabled[TimingHud::COL_TIME],
        SettingsHud::ClickRegion::TIMING_TIME_TOGGLE, hud, nullptr, 0, true,
        "timing.time");

    // Gap control: Off or gap type (merges toggle + comparison)
    ctx.addCycleControl("Gap", getGapText(), 10,
        SettingsHud::ClickRegion::TIMING_GAP_DOWN,
        SettingsHud::ClickRegion::TIMING_GAP_UP,
        hud, true, !gapEnabled, "timing.gap");

    ctx.addSpacing(0.5f);

    // === SECONDARY ROW SECTION ===
    ctx.addSectionHeader("Secondary Row");

    // Helper to check if a gap type is the primary (only when primary gap is visible)
    auto isPrimary = [&](GapTypeFlags type) { return gapEnabled && hud->getPrimaryGapType() == type; };

    // Secondary gap type toggles (shows "Primary" when matching primary gap type)
    ctx.addToggleControl("Session PB", hud->isSecondaryGapEnabled(GAP_TO_PB),
        SettingsHud::ClickRegion::TIMING_GAP_PB_TOGGLE, hud, nullptr, 0, !isPrimary(GAP_TO_PB),
        "timing.secondary_pb", isPrimary(GAP_TO_PB) ? "Primary" : nullptr);

    ctx.addToggleControl("Alltime", hud->isSecondaryGapEnabled(GAP_TO_ALLTIME),
        SettingsHud::ClickRegion::TIMING_GAP_ALLTIME_TOGGLE, hud, nullptr, 0, !isPrimary(GAP_TO_ALLTIME),
        "timing.secondary_alltime", isPrimary(GAP_TO_ALLTIME) ? "Primary" : nullptr);

    ctx.addToggleControl("Ideal", hud->isSecondaryGapEnabled(GAP_TO_IDEAL),
        SettingsHud::ClickRegion::TIMING_GAP_IDEAL_TOGGLE, hud, nullptr, 0, !isPrimary(GAP_TO_IDEAL),
        "timing.secondary_ideal", isPrimary(GAP_TO_IDEAL) ? "Primary" : nullptr);

    ctx.addToggleControl("Overall", hud->isSecondaryGapEnabled(GAP_TO_OVERALL),
        SettingsHud::ClickRegion::TIMING_GAP_OVERALL_TOGGLE, hud, nullptr, 0, !isPrimary(GAP_TO_OVERALL),
        "timing.secondary_overall", isPrimary(GAP_TO_OVERALL) ? "Primary" : nullptr);

    ctx.addToggleControl("Record", hud->isSecondaryGapEnabled(GAP_TO_RECORD),
        SettingsHud::ClickRegion::TIMING_GAP_RECORD_TOGGLE, hud, nullptr, 0, !isPrimary(GAP_TO_RECORD),
        "timing.secondary_record", isPrimary(GAP_TO_RECORD) ? "Primary" : nullptr);

    return hud;
}
