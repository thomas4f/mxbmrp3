// ============================================================================
// hud/settings/settings_tab_gap_bar.cpp
// Tab renderer for Gap Bar HUD settings
// ============================================================================
#include "settings_layout.h"
#include "../settings_hud.h"
#include "../gap_bar_hud.h"

// Static member function of SettingsHud - handles click events for GapBar tab
bool SettingsHud::handleClickTabGapBar(const ClickRegion& region) {
    switch (region.type) {
        case ClickRegion::GAPBAR_FREEZE_UP:
            if (m_gapBar) {
                m_gapBar->m_freezeDurationMs = std::min(
                    m_gapBar->m_freezeDurationMs + GapBarHud::FREEZE_STEP_MS,
                    GapBarHud::MAX_FREEZE_MS);
                m_gapBar->setDataDirty();
                setDataDirty();
            }
            return true;

        case ClickRegion::GAPBAR_FREEZE_DOWN:
            if (m_gapBar) {
                m_gapBar->m_freezeDurationMs = std::max(
                    m_gapBar->m_freezeDurationMs - GapBarHud::FREEZE_STEP_MS,
                    GapBarHud::MIN_FREEZE_MS);
                m_gapBar->setDataDirty();
                setDataDirty();
            }
            return true;

        case ClickRegion::GAPBAR_MARKER_TOGGLE:
            if (m_gapBar) {
                m_gapBar->m_showMarkers = !m_gapBar->m_showMarkers;
                m_gapBar->setDataDirty();
                setDataDirty();
            }
            return true;

        case ClickRegion::GAPBAR_MODE_CYCLE:
            // Mode removed - gap bar now always uses gap-based display
            return true;

        case ClickRegion::GAPBAR_RANGE_UP:
            if (m_gapBar) {
                m_gapBar->m_gapRangeMs = std::min(
                    m_gapBar->m_gapRangeMs + GapBarHud::RANGE_STEP_MS,
                    GapBarHud::MAX_RANGE_MS);
                m_gapBar->setDataDirty();
                setDataDirty();
            }
            return true;

        case ClickRegion::GAPBAR_RANGE_DOWN:
            if (m_gapBar) {
                m_gapBar->m_gapRangeMs = std::max(
                    m_gapBar->m_gapRangeMs - GapBarHud::RANGE_STEP_MS,
                    GapBarHud::MIN_RANGE_MS);
                m_gapBar->setDataDirty();
                setDataDirty();
            }
            return true;

        case ClickRegion::GAPBAR_WIDTH_UP:
            if (m_gapBar) {
                m_gapBar->setBarWidth(m_gapBar->m_barWidthPercent + GapBarHud::WIDTH_STEP_PERCENT);
                setDataDirty();
            }
            return true;

        case ClickRegion::GAPBAR_WIDTH_DOWN:
            if (m_gapBar) {
                m_gapBar->setBarWidth(m_gapBar->m_barWidthPercent - GapBarHud::WIDTH_STEP_PERCENT);
                setDataDirty();
            }
            return true;

        default:
            return false;
    }
}

// Static member function of SettingsHud - inherits friend access to GapBarHud
BaseHud* SettingsHud::renderTabGapBar(SettingsLayoutContext& ctx) {
    GapBarHud* hud = ctx.parent->getGapBarHud();
    if (!hud) return nullptr;

    ctx.addTabTooltip("gap_bar");

    // === APPEARANCE SECTION ===
    ctx.addSectionHeader("Appearance");

    // Add standard HUD controls (Visible, Texture, Opacity, Scale)
    // Note: Gap Bar HUD has no title support
    ctx.addStandardHudControls(hud, false);

    // === GAP BAR SECTION ===
    ctx.addSpacing(0.5f);
    ctx.addSectionHeader("Gap Bar");

    // Markers toggle (show both position markers)
    ctx.addToggleControl("Markers", hud->m_showMarkers,
        SettingsHud::ClickRegion::GAPBAR_MARKER_TOGGLE, hud, nullptr, 0, true, "gap_bar.markers");

    // Width control (bar width percentage)
    char widthValue[16];
    snprintf(widthValue, sizeof(widthValue), "%d%%", hud->m_barWidthPercent);
    ctx.addCycleControl("Width", widthValue, 10,
        SettingsHud::ClickRegion::GAPBAR_WIDTH_DOWN,
        SettingsHud::ClickRegion::GAPBAR_WIDTH_UP,
        hud, true, false, "gap_bar.width");

    // Range control (how much time fits from center to edge)
    char rangeValue[16];
    snprintf(rangeValue, sizeof(rangeValue), "%ds", hud->m_gapRangeMs / 1000);
    ctx.addCycleControl("Range", rangeValue, 10,
        SettingsHud::ClickRegion::GAPBAR_RANGE_DOWN,
        SettingsHud::ClickRegion::GAPBAR_RANGE_UP,
        hud, true, false, "gap_bar.range");

    // Freeze control (freeze duration for official times)
    char freezeValue[16];
    bool gapFreezeIsOff = (hud->m_freezeDurationMs == 0);
    if (gapFreezeIsOff) {
        strcpy_s(freezeValue, sizeof(freezeValue), "Off");
    } else {
        snprintf(freezeValue, sizeof(freezeValue), "%ds", hud->m_freezeDurationMs / 1000);
    }
    ctx.addCycleControl("Freeze", freezeValue, 10,
        SettingsHud::ClickRegion::GAPBAR_FREEZE_DOWN,
        SettingsHud::ClickRegion::GAPBAR_FREEZE_UP,
        hud, true, gapFreezeIsOff, "gap_bar.freeze");

    return hud;
}
