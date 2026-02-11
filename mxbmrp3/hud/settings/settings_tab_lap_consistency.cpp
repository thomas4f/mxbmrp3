// ============================================================================
// hud/settings/settings_tab_lap_consistency.cpp
// Tab renderer for Lap Consistency HUD settings
// ============================================================================
#include "settings_layout.h"
#include "../settings_hud.h"
#include "../lap_consistency_hud.h"

// Helper function to get display mode name (matches PerformanceHud/TelemetryHud)
static const char* getStyleName(uint8_t mode) {
    switch (mode) {
        case LapConsistencyHud::DISPLAY_GRAPHS: return "Graphs";
        case LapConsistencyHud::DISPLAY_VALUES: return "Numbers";
        case LapConsistencyHud::DISPLAY_BOTH:   return "Both";
        default: return "Unknown";
    }
}

// Helper function to get reference mode name (matches TimingHud naming)
static const char* getReferenceModeName(LapConsistencyHud::ReferenceMode mode) {
    switch (mode) {
        case LapConsistencyHud::ReferenceMode::SESSION_PB: return "Session PB";
        case LapConsistencyHud::ReferenceMode::ALLTIME:    return "Alltime";
        case LapConsistencyHud::ReferenceMode::IDEAL:      return "Ideal";
        case LapConsistencyHud::ReferenceMode::OVERALL:    return "Overall";
#if GAME_HAS_RECORDS_PROVIDER
        case LapConsistencyHud::ReferenceMode::RECORD:     return "Record";
#endif
        case LapConsistencyHud::ReferenceMode::AVERAGE:    return "Average";
        default: return "Unknown";
    }
}

// Helper function to get trend mode name
static const char* getTrendModeName(LapConsistencyHud::TrendMode mode) {
    switch (mode) {
        case LapConsistencyHud::TrendMode::OFF:     return "Off";
        case LapConsistencyHud::TrendMode::LINE:    return "Line";
        case LapConsistencyHud::TrendMode::AVERAGE: return "Average";
        case LapConsistencyHud::TrendMode::LINEAR:  return "Linear";
        default: return "Unknown";
    }
}

// Static member function of SettingsHud - handles click events for LapConsistency tab
bool SettingsHud::handleClickTabLapConsistency(const ClickRegion& region) {
    LapConsistencyHud* hud = dynamic_cast<LapConsistencyHud*>(region.targetHud);
    if (!hud) hud = m_lapConsistency;
    if (!hud) return false;

    switch (region.type) {
        case ClickRegion::LAP_CONSISTENCY_DISPLAY_MODE_UP:
        case ClickRegion::LAP_CONSISTENCY_DISPLAY_MODE_DOWN: {
            int current = static_cast<int>(hud->m_displayMode);
            int numModes = 3;  // DISPLAY_GRAPHS, DISPLAY_VALUES, DISPLAY_BOTH
            if (region.type == ClickRegion::LAP_CONSISTENCY_DISPLAY_MODE_UP) {
                current = (current + 1) % numModes;
            } else {
                current = (current - 1 + numModes) % numModes;
            }
            hud->m_displayMode = static_cast<uint8_t>(current);
            hud->setDataDirty();
            setDataDirty();
            return true;
        }

        case ClickRegion::LAP_CONSISTENCY_REFERENCE_UP:
        case ClickRegion::LAP_CONSISTENCY_REFERENCE_DOWN: {
            int current = static_cast<int>(hud->m_referenceMode);
            int numModes = static_cast<int>(LapConsistencyHud::ReferenceMode::REFERENCE_COUNT);
            if (region.type == ClickRegion::LAP_CONSISTENCY_REFERENCE_UP) {
                current = (current + 1) % numModes;
            } else {
                current = (current - 1 + numModes) % numModes;
            }
            hud->m_referenceMode = static_cast<LapConsistencyHud::ReferenceMode>(current);
            hud->setDataDirty();
            setDataDirty();
            return true;
        }

        case ClickRegion::LAP_CONSISTENCY_LAP_COUNT_UP:
            hud->m_lapCount = std::min(hud->m_lapCount + 5, LapConsistencyHud::MAX_LAP_COUNT);
            hud->setDataDirty();
            setDataDirty();
            return true;

        case ClickRegion::LAP_CONSISTENCY_LAP_COUNT_DOWN:
            hud->m_lapCount = std::max(hud->m_lapCount - 5, LapConsistencyHud::MIN_LAP_COUNT);
            hud->setDataDirty();
            setDataDirty();
            return true;

        case ClickRegion::LAP_CONSISTENCY_TREND_MODE_UP:
        case ClickRegion::LAP_CONSISTENCY_TREND_MODE_DOWN: {
            int current = static_cast<int>(hud->m_trendMode);
            int numModes = static_cast<int>(LapConsistencyHud::TrendMode::TREND_COUNT);
            if (region.type == ClickRegion::LAP_CONSISTENCY_TREND_MODE_UP) {
                current = (current + 1) % numModes;
            } else {
                current = (current - 1 + numModes) % numModes;
            }
            hud->m_trendMode = static_cast<LapConsistencyHud::TrendMode>(current);
            hud->setDataDirty();
            setDataDirty();
            return true;
        }

        default:
            return false;
    }
}

// Static member function of SettingsHud - inherits friend access to LapConsistencyHud
BaseHud* SettingsHud::renderTabLapConsistency(SettingsLayoutContext& ctx) {
    LapConsistencyHud* hud = ctx.parent->getLapConsistencyHud();
    if (!hud) return nullptr;

    ctx.addTabTooltip("lap_consistency");

    // === APPEARANCE SECTION ===
    ctx.addSectionHeader("Appearance");
    ctx.addStandardHudControls(hud);
    ctx.addSpacing(0.5f);

    // === DISPLAY MODE SECTION (matches PerformanceHud/TelemetryHud) ===
    ctx.addSectionHeader("Display Mode");

    // Style: Graphs / Numbers / Both
    ctx.addCycleControl("Style", getStyleName(hud->m_displayMode), 10,
        SettingsHud::ClickRegion::LAP_CONSISTENCY_DISPLAY_MODE_DOWN,
        SettingsHud::ClickRegion::LAP_CONSISTENCY_DISPLAY_MODE_UP,
        hud, true, false, "lap_consistency.style");

    // Reference mode
    ctx.addCycleControl("Reference", getReferenceModeName(hud->m_referenceMode), 10,
        SettingsHud::ClickRegion::LAP_CONSISTENCY_REFERENCE_DOWN,
        SettingsHud::ClickRegion::LAP_CONSISTENCY_REFERENCE_UP,
        hud, true, false, "lap_consistency.reference");

    // Lap count
    char lapCountValue[8];
    snprintf(lapCountValue, sizeof(lapCountValue), "%d", hud->m_lapCount);
    ctx.addCycleControl("Laps to show", lapCountValue, 10,
        SettingsHud::ClickRegion::LAP_CONSISTENCY_LAP_COUNT_DOWN,
        SettingsHud::ClickRegion::LAP_CONSISTENCY_LAP_COUNT_UP,
        hud, true, false, "lap_consistency.lap_count");

    // Trend line mode
    ctx.addCycleControl("Trend line", getTrendModeName(hud->m_trendMode), 10,
        SettingsHud::ClickRegion::LAP_CONSISTENCY_TREND_MODE_DOWN,
        SettingsHud::ClickRegion::LAP_CONSISTENCY_TREND_MODE_UP,
        hud, true, false, "lap_consistency.trend_mode");

    ctx.addSpacing(0.5f);

    // === STATISTICS SECTION ===
    ctx.addSectionHeader("Statistics");

    ctx.addToggleControl("Reference", (hud->m_enabledStats & LapConsistencyHud::STAT_REF) != 0,
        SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledStats, LapConsistencyHud::STAT_REF, true,
        "lap_consistency.stat_ref");
    ctx.addToggleControl("Best", (hud->m_enabledStats & LapConsistencyHud::STAT_BEST) != 0,
        SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledStats, LapConsistencyHud::STAT_BEST, true,
        "lap_consistency.stat_best");
    ctx.addToggleControl("Average", (hud->m_enabledStats & LapConsistencyHud::STAT_AVG) != 0,
        SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledStats, LapConsistencyHud::STAT_AVG, true,
        "lap_consistency.stat_avg");
    ctx.addToggleControl("Worst", (hud->m_enabledStats & LapConsistencyHud::STAT_WORST) != 0,
        SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledStats, LapConsistencyHud::STAT_WORST, true,
        "lap_consistency.stat_worst");
    ctx.addToggleControl("Last", (hud->m_enabledStats & LapConsistencyHud::STAT_LAST) != 0,
        SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledStats, LapConsistencyHud::STAT_LAST, true,
        "lap_consistency.stat_last");
    ctx.addToggleControl("Std deviation", (hud->m_enabledStats & LapConsistencyHud::STAT_STDDEV) != 0,
        SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledStats, LapConsistencyHud::STAT_STDDEV, true,
        "lap_consistency.stat_stddev");
    ctx.addToggleControl("Trend", (hud->m_enabledStats & LapConsistencyHud::STAT_TREND) != 0,
        SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledStats, LapConsistencyHud::STAT_TREND, true,
        "lap_consistency.stat_trend");
    ctx.addToggleControl("Consistency", (hud->m_enabledStats & LapConsistencyHud::STAT_CONS) != 0,
        SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledStats, LapConsistencyHud::STAT_CONS, true,
        "lap_consistency.stat_cons");

    return hud;
}
