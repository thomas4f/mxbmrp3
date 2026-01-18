// ============================================================================
// hud/settings/settings_tab_gap_bar.cpp
// Tab renderer for Gap Bar HUD settings
// ============================================================================
#include "settings_layout.h"
#include "../settings_hud.h"
#include "../gap_bar_hud.h"
#include "../../core/asset_manager.h"

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

        case ClickRegion::GAPBAR_MARKER_MODE_UP:
        case ClickRegion::GAPBAR_MARKER_MODE_DOWN:
            if (m_gapBar) {
                bool forward = (region.type == ClickRegion::GAPBAR_MARKER_MODE_UP);
                int current = static_cast<int>(m_gapBar->m_markerMode);
                int next = forward ? (current + 1) % 3 : (current + 2) % 3;
                m_gapBar->m_markerMode = static_cast<GapBarHud::MarkerMode>(next);
                m_gapBar->setDataDirty();
                setDataDirty();
            }
            return true;

        case ClickRegion::GAPBAR_ICON_UP:
        case ClickRegion::GAPBAR_ICON_DOWN:
            if (m_gapBar) {
                bool forward = (region.type == ClickRegion::GAPBAR_ICON_UP);
                int iconCount = static_cast<int>(AssetManager::getInstance().getIconCount());
                int current = m_gapBar->m_riderIconIndex;
                int next;
                if (forward) {
                    next = (current + 1) % (iconCount + 1);  // +1 for "Off" option
                } else {
                    next = (current + iconCount) % (iconCount + 1);
                }
                m_gapBar->m_riderIconIndex = next;
                m_gapBar->setDataDirty();
                setDataDirty();
            }
            return true;

        case ClickRegion::GAPBAR_GAP_TEXT_TOGGLE:
            if (m_gapBar) {
                m_gapBar->m_showGapText = !m_gapBar->m_showGapText;
                m_gapBar->setDataDirty();
                setDataDirty();
            }
            return true;

        case ClickRegion::GAPBAR_GAP_BAR_TOGGLE:
            if (m_gapBar) {
                m_gapBar->m_showGapBar = !m_gapBar->m_showGapBar;
                m_gapBar->setDataDirty();
                setDataDirty();
            }
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

        case ClickRegion::GAPBAR_MARKER_SCALE_UP:
            if (m_gapBar) {
                float newScale = m_gapBar->m_fMarkerScale + 0.1f;
                if (newScale > GapBarHud::MAX_MARKER_SCALE) newScale = GapBarHud::MAX_MARKER_SCALE;
                m_gapBar->m_fMarkerScale = newScale;
                m_gapBar->setDataDirty();
                setDataDirty();
            }
            return true;

        case ClickRegion::GAPBAR_MARKER_SCALE_DOWN:
            if (m_gapBar) {
                float newScale = m_gapBar->m_fMarkerScale - 0.1f;
                if (newScale < GapBarHud::MIN_MARKER_SCALE) newScale = GapBarHud::MIN_MARKER_SCALE;
                m_gapBar->m_fMarkerScale = newScale;
                m_gapBar->setDataDirty();
                setDataDirty();
            }
            return true;

        case ClickRegion::GAPBAR_LABEL_MODE_UP:
        case ClickRegion::GAPBAR_LABEL_MODE_DOWN:
            if (m_gapBar) {
                bool forward = (region.type == ClickRegion::GAPBAR_LABEL_MODE_UP);
                int current = static_cast<int>(m_gapBar->m_labelMode);
                int next = forward ? (current + 1) % 4 : (current + 3) % 4;
                m_gapBar->m_labelMode = static_cast<GapBarHud::LabelMode>(next);
                m_gapBar->setDataDirty();
                setDataDirty();
            }
            return true;

        case ClickRegion::GAPBAR_COLOR_MODE_UP:
        case ClickRegion::GAPBAR_COLOR_MODE_DOWN:
            if (m_gapBar) {
                bool forward = (region.type == ClickRegion::GAPBAR_COLOR_MODE_UP);
                int current = static_cast<int>(m_gapBar->m_riderColorMode);
                int next = forward ? (current + 1) % 3 : (current + 2) % 3;
                m_gapBar->m_riderColorMode = static_cast<GapBarHud::RiderColorMode>(next);
                m_gapBar->setDataDirty();
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

    // Show Gap Text toggle
    ctx.addToggleControl("Show gap", hud->m_showGapText,
        SettingsHud::ClickRegion::GAPBAR_GAP_TEXT_TOGGLE, hud, nullptr, 0, true, "gap_bar.show_gap");

    // Show Gap Bar toggle (green/red visualization)
    ctx.addToggleControl("Show gap bar", hud->m_showGapBar,
        SettingsHud::ClickRegion::GAPBAR_GAP_BAR_TOGGLE, hud, nullptr, 0, true, "gap_bar.show_gap_bar");

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

    // === MARKERS SECTION ===
    ctx.addSpacing(0.5f);
    ctx.addSectionHeader("Markers");

    // Marker mode cycle control (Ghost / Opponents / Both)
    const char* markerModeStr = "";
    switch (hud->m_markerMode) {
        case GapBarHud::MarkerMode::GHOST:           markerModeStr = "Ghost"; break;
        case GapBarHud::MarkerMode::OPPONENTS:       markerModeStr = "Opponents"; break;
        case GapBarHud::MarkerMode::GHOST_OPPONENTS: markerModeStr = "Both"; break;
    }
    ctx.addCycleControl("Mode", markerModeStr, 10,
        SettingsHud::ClickRegion::GAPBAR_MARKER_MODE_DOWN,
        SettingsHud::ClickRegion::GAPBAR_MARKER_MODE_UP,
        hud, true, false, "gap_bar.marker_mode");

    // Color mode control (Uniform/Brand/Position)
    const char* colorModeStr = "";
    switch (hud->m_riderColorMode) {
        case GapBarHud::RiderColorMode::UNIFORM:      colorModeStr = "Uniform"; break;
        case GapBarHud::RiderColorMode::BRAND:        colorModeStr = "Brand"; break;
        case GapBarHud::RiderColorMode::RELATIVE_POS: colorModeStr = "Position"; break;
    }
    ctx.addCycleControl("Marker colors", colorModeStr, 10,
        SettingsHud::ClickRegion::GAPBAR_COLOR_MODE_DOWN,
        SettingsHud::ClickRegion::GAPBAR_COLOR_MODE_UP,
        hud, true, false, "gap_bar.marker_colors");

    // Icon cycle control (0=default icon, 1-N=other icons)
    // When index is 0, show the default icon's name (circle-chevron-up)
    int iconIndex = hud->m_riderIconIndex;
    std::string iconStr;
    if (iconIndex == 0) {
        // Get display name of default icon (circle-chevron-up)
        const auto& assetMgr = AssetManager::getInstance();
        int defaultSpriteIndex = assetMgr.getIconSpriteIndex("circle-chevron-up");
        if (defaultSpriteIndex > 0) {
            iconStr = assetMgr.getIconDisplayName(defaultSpriteIndex);
            if (iconStr.length() > 10) iconStr.resize(10);
        }
        if (iconStr.empty()) iconStr = "Circle Chev";  // Fallback if icon not found
    } else {
        iconStr = getShapeDisplayName(iconIndex, 10);
    }
    ctx.addCycleControl("Marker icon", iconStr.c_str(), 10,
        SettingsHud::ClickRegion::GAPBAR_ICON_DOWN,
        SettingsHud::ClickRegion::GAPBAR_ICON_UP,
        hud, true, false, "gap_bar.icon");

    // Marker scale control (50%-300%)
    char markerScaleValue[16];
    snprintf(markerScaleValue, sizeof(markerScaleValue), "%.0f%%", hud->m_fMarkerScale * 100.0f);
    ctx.addCycleControl("Marker scale", markerScaleValue, 10,
        SettingsHud::ClickRegion::GAPBAR_MARKER_SCALE_DOWN,
        SettingsHud::ClickRegion::GAPBAR_MARKER_SCALE_UP,
        hud, true, false, "gap_bar.marker_scale");

    // Label mode control (Off/Position/Race Num/Both)
    const char* labelModeStr = "";
    bool labelIsOff = (hud->m_labelMode == GapBarHud::LabelMode::NONE);
    switch (hud->m_labelMode) {
        case GapBarHud::LabelMode::NONE:     labelModeStr = "Off"; break;
        case GapBarHud::LabelMode::POSITION: labelModeStr = "Position"; break;
        case GapBarHud::LabelMode::RACE_NUM: labelModeStr = "Race Num"; break;
        case GapBarHud::LabelMode::BOTH:     labelModeStr = "Both"; break;
    }
    ctx.addCycleControl("Marker labels", labelModeStr, 10,
        SettingsHud::ClickRegion::GAPBAR_LABEL_MODE_DOWN,
        SettingsHud::ClickRegion::GAPBAR_LABEL_MODE_UP,
        hud, true, labelIsOff, "gap_bar.labels");

    return hud;
}
