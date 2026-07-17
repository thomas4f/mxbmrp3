// ============================================================================
// hud/settings/settings_tab_radar.cpp
// Tab renderer for Radar HUD settings
// ============================================================================
#include "settings_layout.h"
#include "../settings_hud.h"
#include "../radar_hud.h"
#include "../../core/asset_manager.h"
#include <cmath>

// Static member function of SettingsHud - handles click events for Radar tab
bool SettingsHud::handleClickTabRadar(const ClickRegion& region) {
    RadarHud* radarHud = dynamic_cast<RadarHud*>(region.targetHud);
    // Some radar clicks don't use targetHud, use m_radarHud instead
    if (!radarHud) radarHud = m_radarHud;

    switch (region.type) {
        // Radar range, Alert distance, Arrow scale and Marker scale are data-driven
        // STEPPED controls - registered in renderTabRadar via ctx.addSteppedControl
        // (their setters were just clamp + dedup + setDataDirty) and handled by the
        // shared SettingsHud::applySteppedControl.

        // Show mode / Marker colors / Marker labels / Arrow mode / Arrow colors
        // are data-driven CYCLE controls now - registered in renderTabRadar via
        // ctx.addCycleControl.

        case ClickRegion::RADAR_PROXIMITY_SHAPE_UP:
        case ClickRegion::RADAR_PROXIMITY_SHAPE_DOWN:
            if (radarHud) {
                bool forward = (region.type == ClickRegion::RADAR_PROXIMITY_SHAPE_UP);
                // [1..count], no Off slot; skips HUD identity icons.
                radarHud->setProximityArrowShape(AssetManager::getInstance()
                    .stepShapeIndexSkippingHud(radarHud->getProximityArrowShape(), forward, false));
                setDataDirty();
            }
            return true;

        case ClickRegion::RADAR_RIDER_SHAPE_UP:
        case ClickRegion::RADAR_RIDER_SHAPE_DOWN:
            if (radarHud) {
                bool forward = (region.type == ClickRegion::RADAR_RIDER_SHAPE_UP);
                // [1..count], no Off slot; skips HUD identity icons.
                radarHud->setRiderShape(AssetManager::getInstance()
                    .stepShapeIndexSkippingHud(radarHud->getRiderShape(), forward, false));
                setDataDirty();
            }
            return true;

        default:
            return false;
    }
}

// Static member function of SettingsHud - inherits friend access to RadarHud
BaseHud* SettingsHud::renderTabRadar(SettingsLayoutContext& ctx) {
    RadarHud* hud = ctx.parent->getRadarHud();
    if (!hud) return nullptr;

    ctx.addTabTooltip("radar");

    // === APPEARANCE SECTION ===
    ctx.addSectionHeader("Appearance");
    ctx.addStandardHudControls(hud, false);  // No title support
    ctx.addSpacing(0.5f);

    // === RADAR SECTION ===
    ctx.addSectionHeader("Radar");

    // Mode control (Off/On/Auto-hide)
    const char* radarModeDisplayStr = "";
    bool radarModeIsOff = (hud->getRadarMode() == RadarHud::RadarMode::OFF);
    switch (hud->getRadarMode()) {
        case RadarHud::RadarMode::OFF:       radarModeDisplayStr = "Off"; break;
        case RadarHud::RadarMode::ON:        radarModeDisplayStr = "Always"; break;
        case RadarHud::RadarMode::AUTO_HIDE: radarModeDisplayStr = "Auto-hide"; break;
    }
    ctx.addCycleControl("Show mode", radarModeDisplayStr, 10,
        SettingsHud::CycleControl::enumMember(hud, &RadarHud::m_radarMode, 3, hud),
        hud, true, radarModeIsOff, "radar.mode");

    // Range control: accelerated 10m step (matches the map's zoom range),
    // clamped to [10m, 200m]
    char rangeValue[16];
    snprintf(rangeValue, sizeof(rangeValue), "%.0fm", hud->getRadarRange());
    ctx.addSteppedControl("Radar range", rangeValue, 10,
        SettingsHud::SteppedControl::stepFloat(&hud->m_fRadarRangeMeters,
            RadarHud::RADAR_RANGE_STEP,
            RadarHud::MIN_RADAR_RANGE, RadarHud::MAX_RADAR_RANGE, hud),
        hud, true, false, "radar.range");

    ctx.addSpacing(0.5f);

    // === RIDER MARKERS SECTION ===
    ctx.addSectionHeader("Rider Markers");

    // Rider color mode cycle
    const char* radarColorModeStr = "";
    switch (hud->getRiderColorMode()) {
        case RadarHud::RiderColorMode::UNIFORM:      radarColorModeStr = "Uniform"; break;
        case RadarHud::RiderColorMode::BRAND:        radarColorModeStr = "Brand"; break;
        case RadarHud::RiderColorMode::RELATIVE_POS: radarColorModeStr = "Position"; break;
    }
    ctx.addCycleControl("Marker colors", radarColorModeStr, 10,
        SettingsHud::CycleControl::enumMember(hud, &RadarHud::m_riderColorMode, 3, hud),
        hud, true, false, "radar.colorize");

    // Rider shape control - uses all icons from AssetManager
    std::string radarShapeStr = getShapeDisplayName(hud->getRiderShape(), 10);
    ctx.addCycleControl("Marker icon", radarShapeStr.c_str(), 10,
        SettingsHud::ClickRegion::RADAR_RIDER_SHAPE_DOWN,
        SettingsHud::ClickRegion::RADAR_RIDER_SHAPE_UP,
        hud, true, false, "radar.rider_shape");

    // Marker scale control (independent scale for icons/labels): accelerated
    // 1% step, clamped to [50%, 300%]
    char radarMarkerScaleValue[16];
    snprintf(radarMarkerScaleValue, sizeof(radarMarkerScaleValue), "%.0f%%", hud->getMarkerScale() * 100.0f);
    ctx.addSteppedControl("Marker scale", radarMarkerScaleValue, 10,
        SettingsHud::SteppedControl::stepFloat(&hud->m_fMarkerScale, 0.01f,
            RadarHud::MIN_MARKER_SCALE, RadarHud::MAX_MARKER_SCALE, hud),
        hud, true, false, "radar.marker_scale");

    // Label mode control
    const char* radarModeStr = "";
    bool radarLabelIsOff = (hud->getLabelMode() == RadarHud::LabelMode::NONE);
    switch (hud->getLabelMode()) {
        case RadarHud::LabelMode::NONE:     radarModeStr = "Off"; break;
        case RadarHud::LabelMode::POSITION: radarModeStr = "Position"; break;
        case RadarHud::LabelMode::RACE_NUM: radarModeStr = "Race Num"; break;
        case RadarHud::LabelMode::BOTH:     radarModeStr = "Both"; break;
        default:
            radarModeStr = "Unknown";
            break;
    }
    ctx.addCycleControl("Marker labels", radarModeStr, 10,
        SettingsHud::CycleControl::enumMember(hud, &RadarHud::m_labelMode, 4, hud),
        hud, true, radarLabelIsOff, "radar.labels");
    ctx.addSpacing(0.5f);

    // === PROXIMITY ARROWS SECTION ===
    ctx.addSectionHeader("Proximity Arrows");

    // Proximity arrows mode control (Off/Edge/Circle)
    const char* proxArrowModeStr = "";
    bool proxArrowIsOff = (hud->getProximityArrowMode() == RadarHud::ProximityArrowMode::OFF);
    switch (hud->getProximityArrowMode()) {
        case RadarHud::ProximityArrowMode::OFF:    proxArrowModeStr = "Off"; break;
        case RadarHud::ProximityArrowMode::EDGE:   proxArrowModeStr = "Edge"; break;
        case RadarHud::ProximityArrowMode::CIRCLE: proxArrowModeStr = "Circle"; break;
    }
    // tooltipOnArrows=false: the proximity-arrow cycles historically had no
    // per-type tooltip fallback, so keep the tooltip on the row region only.
    ctx.addCycleControl("Arrow mode", proxArrowModeStr, 10,
        SettingsHud::CycleControl::enumMember(hud, &RadarHud::m_proximityArrowMode, 3, hud),
        hud, true, proxArrowIsOff, "radar.proximity_arrows", /*tooltipOnArrows=*/false);

    // Alert distance control (when triangles/arrows activate): accelerated 10m
    // step, clamped to [10m, 100m]
    char alertValue[16];
    snprintf(alertValue, sizeof(alertValue), "%.0fm", hud->getAlertDistance());
    ctx.addSteppedControl("Alert distance", alertValue, 10,
        SettingsHud::SteppedControl::stepFloat(&hud->m_fAlertDistance,
            RadarHud::ALERT_DISTANCE_STEP,
            RadarHud::MIN_ALERT_DISTANCE, RadarHud::MAX_ALERT_DISTANCE, hud),
        hud, !proxArrowIsOff, false, "radar.alert_distance");

    // Proximity arrow color mode control
    const char* proxColorModeStr = "";
    switch (hud->getProximityArrowColorMode()) {
        case RadarHud::ProximityArrowColorMode::DISTANCE: proxColorModeStr = "Distance"; break;
        case RadarHud::ProximityArrowColorMode::POSITION: proxColorModeStr = "Position"; break;
    }
    ctx.addCycleControl("Arrow colors", proxColorModeStr, 10,
        SettingsHud::CycleControl::enumMember(hud, &RadarHud::m_proximityArrowColorMode, 2, hud),
        hud, !proxArrowIsOff, false, "radar.proximity_color", /*tooltipOnArrows=*/false);

    // Proximity arrow shape control
    std::string proxShapeStr = getShapeDisplayName(hud->getProximityArrowShape(), 10);
    ctx.addCycleControl("Arrow icon", proxShapeStr.c_str(), 10,
        SettingsHud::ClickRegion::RADAR_PROXIMITY_SHAPE_DOWN,
        SettingsHud::ClickRegion::RADAR_PROXIMITY_SHAPE_UP,
        hud, !proxArrowIsOff, false, "radar.proximity_shape");

    // Proximity arrow scale control: accelerated 1% step, clamped to [50%, 300%].
    // Arrows never had a per-type tooltip.
    char proxScaleValue[16];
    snprintf(proxScaleValue, sizeof(proxScaleValue), "%.0f%%", hud->getProximityArrowScale() * 100.0f);
    ctx.addSteppedControl("Arrow scale", proxScaleValue, 10,
        SettingsHud::SteppedControl::stepFloat(&hud->m_fProximityArrowScale, 0.01f,
            RadarHud::MIN_PROXIMITY_ARROW_SCALE, RadarHud::MAX_PROXIMITY_ARROW_SCALE, hud),
        hud, !proxArrowIsOff, false, "radar.proximity_scale", /*tooltipOnArrows=*/false);

    return hud;
}
