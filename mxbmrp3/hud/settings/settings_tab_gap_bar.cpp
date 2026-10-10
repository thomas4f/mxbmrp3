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
        // SettingsHud::applySteppedControl / applyCycleControl. Marker icon is a
        // shared iconCycle whose 0 is the default circle-chevron-up.

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
    ctx.addSectionHeading("Appearance");

    // Add standard HUD controls (Visible, Title, Texture|Theme, Opacity, Scale)
    ctx.addStandardHudControls(hud);
    // === LAYOUT SECTION ===
    ctx.addSectionHeading("Layout");

    // Width control (bar width percentage): accelerated 1% clamp over [50, 400]
    // (setBarWidth was just clamp + dedup + setDataDirty - same behavior).
    char widthValue[16];
    snprintf(widthValue, sizeof(widthValue), "%d%%", hud->m_barWidthPercent);
    ctx.addSteppedControl("Width", widthValue,
        SettingsHud::SteppedControl::clampInt(&hud->m_barWidthPercent,
            GapBarHud::WIDTH_STEP_PERCENT,
            GapBarHud::MIN_WIDTH_PERCENT, GapBarHud::MAX_WIDTH_PERCENT, hud),
        hud, true, false, "gap_bar.width");

    // Range control (how much time fits from center to edge): accelerated 250ms
    // clamp over [Auto, 5s], Auto being the step below 1s. Values are multiples
    // of 250ms, so show whole seconds plainly and trim trailing zeros on the
    // fractional steps ("1.25s", "2.5s").
    char rangeValue[16];
    if (hud->m_gapRangeMs == GapBarHud::RANGE_AUTO) {
        snprintf(rangeValue, sizeof(rangeValue), "Auto");
    } else if (hud->m_gapRangeMs % 1000 == 0) {
        snprintf(rangeValue, sizeof(rangeValue), "%ds", hud->m_gapRangeMs / 1000);
    } else if (hud->m_gapRangeMs % 500 == 0) {
        snprintf(rangeValue, sizeof(rangeValue), "%.1fs", hud->m_gapRangeMs / 1000.0f);
    } else {
        snprintf(rangeValue, sizeof(rangeValue), "%.2fs", hud->m_gapRangeMs / 1000.0f);
    }
    ctx.addSteppedControl("Range", rangeValue,
        SettingsHud::SteppedControl::clampInt(&hud->m_gapRangeMs,
            GapBarHud::RANGE_STEP_MS,
            GapBarHud::RANGE_AUTO, GapBarHud::MAX_RANGE_MS, hud),
        hud, true, false, "gap_bar.range");

    // Freeze control (freeze duration for official times)
    ctx.addFreezeControl("Freeze", &hud->m_freezeDurationMs, true, hud, true, "gap_bar.freeze", true);

    // === CONTENT SECTION ===
    ctx.addSectionHeading("Content");

    // Show Gap Text toggle
    ctx.addToggleControl("Show gap", hud->m_showGapText,
        SettingsHud::ClickRegion::GAPBAR_GAP_TEXT_TOGGLE, hud, nullptr, 0, true, "gap_bar.show_gap");

    // Show Gap Bar toggle (green/red visualization)
    ctx.addToggleControl("Show gap bar", hud->m_showGapBar,
        SettingsHud::ClickRegion::GAPBAR_GAP_BAR_TOGGLE, hud, nullptr, 0, true, "gap_bar.show_gap_bar");

    // Which lap the gap (and the ghost) is measured against: after the two rows
    // it qualifies, as on the Lap Log tab. All three are laps the tracker sampled;
    // only the all-time one is on disk, so it is the one that is there before the
    // session's first PB.
    ctx.addReferenceControl("Gap reference", &hud->m_referenceDefault, &hud->m_reference, hud,
        "gap_bar.reference");

    // Split ticks (On / Off)
    {
        SettingsHud::CycleControl splits;
        splits.get = [hud]() { return hud->m_showSplits ? 1 : 0; };
        splits.set = [hud](int v) { hud->m_showSplits = (v != 0); };
        splits.count = 2;
        splits.dirtyHud = hud;
        ctx.addCycleControl("Splits", hud->m_showSplits ? "On" : "Off", splits,
            hud, true, !hud->m_showSplits, "gap_bar.splits");
    }

    // === RIDER MARKERS SECTION ===
    ctx.addSectionHeading("Rider Markers");

    // Marker mode cycle control (Ghost / Opponents / Both / Off)
    static const char* const kMarkerModes[] = { "Ghost", "Opponents", "Both", "Off" };
    const char* markerModeStr = cycleName(kMarkerModes, static_cast<int>(hud->m_markerMode));
    ctx.addCycleControl("Mode", markerModeStr,
        SettingsHud::CycleControl::enumMember(hud, &GapBarHud::m_markerMode, GapBarHud::MARKER_MODE_COUNT, hud, kMarkerModes),
        hud, true, hud->m_markerMode == GapBarHud::MarkerMode::OFF, "gap_bar.marker_mode");

    // Color mode control (Uniform/Brand/Position)
    static const char* const kColorModes[] = { "Uniform", "Brand", "Position" };
    const char* colorModeStr = cycleName(kColorModes, static_cast<int>(hud->m_riderColorMode));
    // tooltipOnArrows=false: these arrows historically had no per-type tooltip
    // fallback, so keep the tooltip on the row region only.
    ctx.addCycleControl("Marker colors", colorModeStr,
        SettingsHud::CycleControl::enumMember(hud, &GapBarHud::m_riderColorMode, 3, hud, kColorModes),
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
        }
        if (iconStr.empty()) iconStr = "Circle Chevron Up";  // Fallback if icon not found
    } else {
        iconStr = getShapeDisplayName(iconIndex);
    }
    ctx.addCycleControl("Marker icon", iconStr.c_str(),
        iconCycle([hud]() { return hud->m_riderIconIndex; }, [hud](int v) { hud->m_riderIconIndex = v; },
                  hud, /*allowOff=*/true, "circle-chevron-up"),
        hud, true, false, "gap_bar.icon");

    // Marker scale control (50%-300%)
    char markerScaleValue[16];
    snprintf(markerScaleValue, sizeof(markerScaleValue), "%.0f%%", hud->m_fMarkerScale * 100.0f);
    ctx.addSteppedControl("Marker scale", markerScaleValue,
        SettingsHud::SteppedControl::stepFloat(&hud->m_fMarkerScale, 0.01f,
            GapBarHud::MIN_MARKER_SCALE, GapBarHud::MAX_MARKER_SCALE, hud),
        hud, true, false, "gap_bar.marker_scale");

    // Label mode control (Off/Position/Race number/Both)
    static const char* const kLabelModes[] = { "Off", "Position", "Race number", "Both" };
    bool labelIsOff = (hud->m_labelMode == GapBarHud::LabelMode::NONE);
    const char* labelModeStr = cycleName(kLabelModes, static_cast<int>(hud->m_labelMode));
    ctx.addCycleControl("Marker labels", labelModeStr,
        SettingsHud::CycleControl::enumMember(hud, &GapBarHud::m_labelMode, 4, hud, kLabelModes),
        hud, true, labelIsOff, "gap_bar.labels");

    return hud;
}
