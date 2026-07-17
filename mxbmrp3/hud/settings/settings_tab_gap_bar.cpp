// ============================================================================
// hud/settings/settings_tab_gap_bar.cpp
// Tab renderer for Gap Bar HUD settings
// ============================================================================
#include "settings_layout.h"
#include "../settings_hud.h"
#include "../gap_bar_hud.h"
#include "../../core/asset_manager.h"
#include <cmath>

// Static member function of SettingsHud - handles click events for GapBar tab
bool SettingsHud::handleClickTabGapBar(const ClickRegion& region) {
    switch (region.type) {
        // Width, Range, Freeze duration and Marker scale are data-driven STEPPED
        // controls; the Mode / Marker colors / Marker labels mod-N cycles are
        // data-driven CYCLE controls - registered in renderTabGapBar via
        // ctx.addSteppedControl / ctx.addCycleControl and handled by the shared
        // SettingsHud::applySteppedControl / applyCycleControl.

        case ClickRegion::GAPBAR_ICON_UP:
        case ClickRegion::GAPBAR_ICON_DOWN:
            if (m_gapBar) {
                bool forward = (region.type == ClickRegion::GAPBAR_ICON_UP);
                // [0..count] with 0 = default icon; skips HUD identity icons.
                m_gapBar->m_riderIconIndex = AssetManager::getInstance()
                    .stepShapeIndexSkippingHud(m_gapBar->m_riderIconIndex, forward);
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
    ctx.addSpacing(0.5f);

    // === GAP BAR SECTION ===
    ctx.addSectionHeader("Gap Bar");

    // Show Gap Text toggle
    ctx.addToggleControl("Show gap", hud->m_showGapText,
        SettingsHud::ClickRegion::GAPBAR_GAP_TEXT_TOGGLE, hud, nullptr, 0, true, "gap_bar.show_gap");

    // Show Gap Bar toggle (green/red visualization)
    ctx.addToggleControl("Show gap bar", hud->m_showGapBar,
        SettingsHud::ClickRegion::GAPBAR_GAP_BAR_TOGGLE, hud, nullptr, 0, true, "gap_bar.show_gap_bar");

    // Width control (bar width percentage): accelerated 1% clamp over [50, 400]
    // (setBarWidth was just clamp + dedup + setDataDirty - same behavior).
    char widthValue[16];
    snprintf(widthValue, sizeof(widthValue), "%d%%", hud->m_barWidthPercent);
    ctx.addSteppedControl("Width", widthValue, 10,
        SettingsHud::SteppedControl::clampInt(&hud->m_barWidthPercent,
            GapBarHud::WIDTH_STEP_PERCENT,
            GapBarHud::MIN_WIDTH_PERCENT, GapBarHud::MAX_WIDTH_PERCENT, hud),
        hud, true, false, "gap_bar.width");

    // Range control (how much time fits from center to edge): accelerated 250ms
    // clamp over [1s, 5s]. Values are multiples of 250ms, so show whole seconds
    // plainly and trim trailing zeros on the fractional steps ("1.25s", "2.5s").
    char rangeValue[16];
    if (hud->m_gapRangeMs % 1000 == 0) {
        snprintf(rangeValue, sizeof(rangeValue), "%ds", hud->m_gapRangeMs / 1000);
    } else if (hud->m_gapRangeMs % 500 == 0) {
        snprintf(rangeValue, sizeof(rangeValue), "%.1fs", hud->m_gapRangeMs / 1000.0f);
    } else {
        snprintf(rangeValue, sizeof(rangeValue), "%.2fs", hud->m_gapRangeMs / 1000.0f);
    }
    ctx.addSteppedControl("Range", rangeValue, 10,
        SettingsHud::SteppedControl::clampInt(&hud->m_gapRangeMs,
            GapBarHud::RANGE_STEP_MS,
            GapBarHud::MIN_RANGE_MS, GapBarHud::MAX_RANGE_MS, hud),
        hud, true, false, "gap_bar.range");

    // Freeze control (freeze duration for official times)
    char freezeValue[16];
    bool gapFreezeIsOff = (hud->m_freezeDurationMs == 0);
    if (gapFreezeIsOff) {
        strcpy_s(freezeValue, sizeof(freezeValue), "Off");
    } else {
        snprintf(freezeValue, sizeof(freezeValue), "%ds", hud->m_freezeDurationMs / 1000);
    }
    ctx.addSteppedControl("Freeze", freezeValue, 10,
        SettingsHud::SteppedControl::wrapInt(&hud->m_freezeDurationMs,
            GapBarHud::FREEZE_STEP_MS, GapBarHud::MIN_FREEZE_MS, GapBarHud::MAX_FREEZE_MS, hud),
        hud, true, gapFreezeIsOff, "gap_bar.freeze");

    // === RIDER MARKERS SECTION ===
    ctx.addSpacing(0.5f);
    ctx.addSectionHeader("Rider Markers");

    // Marker mode cycle control (Ghost / Opponents / Both)
    const char* markerModeStr = "";
    switch (hud->m_markerMode) {
        case GapBarHud::MarkerMode::GHOST:           markerModeStr = "Ghost"; break;
        case GapBarHud::MarkerMode::OPPONENTS:       markerModeStr = "Opponents"; break;
        case GapBarHud::MarkerMode::GHOST_OPPONENTS: markerModeStr = "Both"; break;
    }
    ctx.addCycleControl("Mode", markerModeStr, 10,
        SettingsHud::CycleControl::enumMember(hud, &GapBarHud::m_markerMode, 3, hud),
        hud, true, false, "gap_bar.marker_mode");

    // Color mode control (Uniform/Brand/Position)
    const char* colorModeStr = "";
    switch (hud->m_riderColorMode) {
        case GapBarHud::RiderColorMode::UNIFORM:      colorModeStr = "Uniform"; break;
        case GapBarHud::RiderColorMode::BRAND:        colorModeStr = "Brand"; break;
        case GapBarHud::RiderColorMode::RELATIVE_POS: colorModeStr = "Position"; break;
    }
    // tooltipOnArrows=false: these arrows historically had no per-type tooltip
    // fallback, so keep the tooltip on the row region only.
    ctx.addCycleControl("Marker colors", colorModeStr, 10,
        SettingsHud::CycleControl::enumMember(hud, &GapBarHud::m_riderColorMode, 3, hud),
        hud, true, false, "gap_bar.marker_colors", /*tooltipOnArrows=*/false);

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
    ctx.addSteppedControl("Marker scale", markerScaleValue, 10,
        SettingsHud::SteppedControl::stepFloat(&hud->m_fMarkerScale, 0.01f,
            GapBarHud::MIN_MARKER_SCALE, GapBarHud::MAX_MARKER_SCALE, hud),
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
        SettingsHud::CycleControl::enumMember(hud, &GapBarHud::m_labelMode, 4, hud),
        hud, true, labelIsOff, "gap_bar.labels");

    return hud;
}
