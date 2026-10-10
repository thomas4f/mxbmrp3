// ============================================================================
// hud/settings/settings_tab_radar.cpp
// Tab renderer for Radar HUD settings
// ============================================================================
#include "settings_layout.h"
#include "../settings_hud.h"
#include "../radar_hud.h"
#include "../../core/asset_manager.h"
#include <cmath>

// No click handler: every Radar row is a shared toggle, stepped or cycle
// descriptor (Marker icon / Arrow icon are iconCycles without Off - the radar
// always draws a marker).

// Static member function of SettingsHud - inherits friend access to RadarHud
BaseHud* SettingsHud::renderTabRadar(SettingsLayoutContext& ctx) {
    RadarHud* hud = ctx.parent->getRadarHud();
    if (!hud) return nullptr;

    ctx.addTabTooltip("radar");

    // === APPEARANCE SECTION ===
    ctx.addSectionHeading("Appearance");
    ctx.addStandardHudControls(hud);
    // === LAYOUT SECTION ===
    ctx.addSectionHeading("Layout");

    // Mode control (Off/On/Auto-hide)
    static const char* const kShowModes[] = { "Off", "Always", "Auto-hide" };
    bool radarModeIsOff = (hud->getRadarMode() == RadarHud::RadarMode::OFF);
    const char* radarModeDisplayStr = cycleName(kShowModes, static_cast<int>(hud->getRadarMode()));
    ctx.addCycleControl("Show mode", radarModeDisplayStr,
        SettingsHud::CycleControl::enumMember(hud, &RadarHud::m_radarMode, 3, hud, kShowModes),
        hud, true, radarModeIsOff, "radar.mode");

    // Range control: accelerated 10m step (matches the map's zoom range),
    // clamped to [10m, 200m]
    char rangeValue[16];
    snprintf(rangeValue, sizeof(rangeValue), "%.0fm", hud->getRadarRange());
    ctx.addSteppedControl("Radar range", rangeValue,
        SettingsHud::SteppedControl::stepFloat(&hud->m_fRadarRangeMeters,
            RadarHud::RADAR_RANGE_STEP,
            RadarHud::MIN_RADAR_RANGE, RadarHud::MAX_RADAR_RANGE, hud),
        hud, true, false, "radar.range");

    // === RIDER MARKERS SECTION ===
    ctx.addSectionHeading("Rider Markers");

    // Rider color mode cycle
    static const char* const kColorModes[] = { "Uniform", "Brand", "Position" };
    const char* radarColorModeStr = cycleName(kColorModes, static_cast<int>(hud->getRiderColorMode()));
    ctx.addCycleControl("Marker colors", radarColorModeStr,
        SettingsHud::CycleControl::enumMember(hud, &RadarHud::m_riderColorMode, 3, hud, kColorModes),
        hud, true, false, "radar.colorize");

    // Rider shape control - uses all icons from AssetManager
    std::string radarShapeStr = getShapeDisplayName(hud->getRiderShape());
    ctx.addCycleControl("Marker icon", radarShapeStr.c_str(),
        iconCycle([hud]() { return hud->getRiderShape(); }, [hud](int v) { hud->setRiderShape(v); },
                  hud, /*allowOff=*/false),
        hud, true, false, "radar.rider_shape");

    // Marker scale control (independent scale for icons/labels): accelerated
    // 1% step, clamped to [50%, 300%]
    char radarMarkerScaleValue[16];
    snprintf(radarMarkerScaleValue, sizeof(radarMarkerScaleValue), "%.0f%%", hud->getMarkerScale() * 100.0f);
    ctx.addSteppedControl("Marker scale", radarMarkerScaleValue,
        SettingsHud::SteppedControl::stepFloat(&hud->m_fMarkerScale, 0.01f,
            RadarHud::MIN_MARKER_SCALE, RadarHud::MAX_MARKER_SCALE, hud),
        hud, true, false, "radar.marker_scale");

    // Label mode control
    static const char* const kLabelModes[] = { "Off", "Position", "Race number", "Both" };
    bool radarLabelIsOff = (hud->getLabelMode() == RadarHud::LabelMode::NONE);
    const char* radarModeStr = cycleName(kLabelModes, static_cast<int>(hud->getLabelMode()));
    ctx.addCycleControl("Marker labels", radarModeStr,
        SettingsHud::CycleControl::enumMember(hud, &RadarHud::m_labelMode, 4, hud, kLabelModes),
        hud, true, radarLabelIsOff, "radar.labels");
    // === PROXIMITY ARROWS SECTION ===
    ctx.addSectionHeading("Proximity Arrows");

    // Proximity arrows mode control (Off/Edge/Circle)
    static const char* const kArrowModes[] = { "Off", "Edge", "Circle" };
    bool proxArrowIsOff = (hud->getProximityArrowMode() == RadarHud::ProximityArrowMode::OFF);
    const char* proxArrowModeStr = cycleName(kArrowModes, static_cast<int>(hud->getProximityArrowMode()));
    // tooltipOnArrows=false: the proximity-arrow cycles historically had no
    // per-type tooltip fallback, so keep the tooltip on the row region only.
    ctx.addCycleControl("Arrow mode", proxArrowModeStr,
        SettingsHud::CycleControl::enumMember(hud, &RadarHud::m_proximityArrowMode, 3, hud, kArrowModes),
        hud, true, proxArrowIsOff, "radar.proximity_arrows", /*tooltipOnArrows=*/false);

    // Alert distance control (when triangles/arrows activate): accelerated 10m
    // step, clamped to [10m, 100m]
    char alertValue[16];
    snprintf(alertValue, sizeof(alertValue), "%.0fm", hud->getAlertDistance());
    ctx.addSteppedControl("Alert distance", alertValue,
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
    ctx.addCycleControl("Arrow colors", proxColorModeStr,
        SettingsHud::CycleControl::enumMember(hud, &RadarHud::m_proximityArrowColorMode, 2, hud),
        hud, !proxArrowIsOff, false, "radar.proximity_color", /*tooltipOnArrows=*/false);

    // Proximity arrow shape control
    std::string proxShapeStr = getShapeDisplayName(hud->getProximityArrowShape());
    ctx.addCycleControl("Arrow icon", proxShapeStr.c_str(),
        iconCycle([hud]() { return hud->getProximityArrowShape(); },
                  [hud](int v) { hud->setProximityArrowShape(v); }, hud, /*allowOff=*/false),
        hud, !proxArrowIsOff, false, "radar.proximity_shape");

    // Proximity arrow scale control: accelerated 1% step, clamped to [50%, 300%].
    // Arrows never had a per-type tooltip.
    char proxScaleValue[16];
    snprintf(proxScaleValue, sizeof(proxScaleValue), "%.0f%%", hud->getProximityArrowScale() * 100.0f);
    ctx.addSteppedControl("Arrow scale", proxScaleValue,
        SettingsHud::SteppedControl::stepFloat(&hud->m_fProximityArrowScale, 0.01f,
            RadarHud::MIN_PROXIMITY_ARROW_SCALE, RadarHud::MAX_PROXIMITY_ARROW_SCALE, hud),
        hud, !proxArrowIsOff, false, "radar.proximity_scale", /*tooltipOnArrows=*/false);

    return hud;
}
