// ============================================================================
// hud/settings/settings_tab_radar.cpp
// Tab renderer for Radar HUD settings
// ============================================================================
#include "settings_layout.h"
#include "../settings_hud.h"
#include "../radar_hud.h"
#include "../../core/asset_manager.h"

// Helper to cycle enum value forward or backward with wrapping
template<typename T>
static T cycleEnumRadar(T current, int count, bool forward) {
    int val = static_cast<int>(current);
    if (forward) {
        val = (val + 1) % count;
    } else {
        val = (val - 1 + count) % count;
    }
    return static_cast<T>(val);
}

// Static member function of SettingsHud - handles click events for Radar tab
bool SettingsHud::handleClickTabRadar(const ClickRegion& region) {
    RadarHud* radarHud = dynamic_cast<RadarHud*>(region.targetHud);
    // Some radar clicks don't use targetHud, use m_radarHud instead
    if (!radarHud) radarHud = m_radarHud;

    switch (region.type) {
        case ClickRegion::RADAR_RANGE_UP:
        case ClickRegion::RADAR_RANGE_DOWN:
            if (radarHud) {
                bool increase = (region.type == ClickRegion::RADAR_RANGE_UP);
                float newRange = radarHud->getRadarRange() + (increase ? RadarHud::RADAR_RANGE_STEP : -RadarHud::RADAR_RANGE_STEP);
                radarHud->setRadarRange(newRange);
                rebuildRenderData();
            }
            return true;

        case ClickRegion::RADAR_COLORIZE_UP:
        case ClickRegion::RADAR_COLORIZE_DOWN:
            if (radarHud) {
                auto newMode = cycleEnumRadar(radarHud->getRiderColorMode(), 3,
                    region.type == ClickRegion::RADAR_COLORIZE_UP);
                radarHud->setRiderColorMode(newMode);
                rebuildRenderData();
            }
            return true;

        case ClickRegion::RADAR_MODE_UP:
        case ClickRegion::RADAR_MODE_DOWN:
            if (radarHud) {
                int current = static_cast<int>(radarHud->getRadarMode());
                int count = 3;  // Off, On, Auto-hide
                if (region.type == ClickRegion::RADAR_MODE_UP) {
                    current = (current + 1) % count;
                } else {
                    current = (current - 1 + count) % count;
                }
                radarHud->setRadarMode(static_cast<RadarHud::RadarMode>(current));
                setDataDirty();
            }
            return true;

        case ClickRegion::RADAR_PROXIMITY_ARROWS_UP:
        case ClickRegion::RADAR_PROXIMITY_ARROWS_DOWN:
            if (radarHud) {
                int current = static_cast<int>(radarHud->getProximityArrowMode());
                int next = (region.type == ClickRegion::RADAR_PROXIMITY_ARROWS_UP)
                    ? (current + 1) % 3
                    : (current + 2) % 3;
                radarHud->setProximityArrowMode(static_cast<RadarHud::ProximityArrowMode>(next));
                setDataDirty();
            }
            return true;

        case ClickRegion::RADAR_ALERT_DISTANCE_UP:
        case ClickRegion::RADAR_ALERT_DISTANCE_DOWN:
            if (radarHud) {
                bool increase = (region.type == ClickRegion::RADAR_ALERT_DISTANCE_UP);
                float newDist = radarHud->getAlertDistance() + (increase ? RadarHud::ALERT_DISTANCE_STEP : -RadarHud::ALERT_DISTANCE_STEP);
                radarHud->setAlertDistance(newDist);
                rebuildRenderData();
            }
            return true;

        case ClickRegion::RADAR_LABEL_MODE_UP:
        case ClickRegion::RADAR_LABEL_MODE_DOWN:
            if (radarHud) {
                auto newMode = cycleEnumRadar(radarHud->getLabelMode(), 4,
                    region.type == ClickRegion::RADAR_LABEL_MODE_UP);
                radarHud->setLabelMode(newMode);
                rebuildRenderData();
            }
            return true;

        case ClickRegion::RADAR_PROXIMITY_SHAPE_UP:
        case ClickRegion::RADAR_PROXIMITY_SHAPE_DOWN:
            if (radarHud) {
                bool forward = (region.type == ClickRegion::RADAR_PROXIMITY_SHAPE_UP);
                int iconCount = static_cast<int>(AssetManager::getInstance().getIconCount());
                int current = radarHud->getProximityArrowShape();
                int next;
                if (forward) {
                    next = current + 1;
                    if (next > iconCount) next = 1;
                } else {
                    next = current - 1;
                    if (next < 1) next = iconCount;
                }
                radarHud->setProximityArrowShape(next);
                rebuildRenderData();
            }
            return true;

        case ClickRegion::RADAR_PROXIMITY_SCALE_UP:
        case ClickRegion::RADAR_PROXIMITY_SCALE_DOWN:
            if (radarHud) {
                float scale = radarHud->getProximityArrowScale();
                float step = 0.1f;
                if (region.type == ClickRegion::RADAR_PROXIMITY_SCALE_UP) {
                    scale += step;
                } else {
                    scale -= step;
                }
                radarHud->setProximityArrowScale(scale);
                setDataDirty();
            }
            return true;

        case ClickRegion::RADAR_PROXIMITY_COLOR_UP:
        case ClickRegion::RADAR_PROXIMITY_COLOR_DOWN:
            if (radarHud) {
                int current = static_cast<int>(radarHud->getProximityArrowColorMode());
                int next = (current + 1) % 2;
                radarHud->setProximityArrowColorMode(static_cast<RadarHud::ProximityArrowColorMode>(next));
                setDataDirty();
            }
            return true;

        case ClickRegion::RADAR_RIDER_SHAPE_UP:
        case ClickRegion::RADAR_RIDER_SHAPE_DOWN:
            if (radarHud) {
                bool forward = (region.type == ClickRegion::RADAR_RIDER_SHAPE_UP);
                int iconCount = static_cast<int>(AssetManager::getInstance().getIconCount());
                int current = radarHud->getRiderShape();
                int next;
                if (forward) {
                    next = current + 1;
                    if (next > iconCount) next = 1;
                } else {
                    next = current - 1;
                    if (next < 1) next = iconCount;
                }
                radarHud->setRiderShape(next);
                rebuildRenderData();
            }
            return true;

        case ClickRegion::RADAR_MARKER_SCALE_UP:
        case ClickRegion::RADAR_MARKER_SCALE_DOWN:
            if (radarHud) {
                bool increase = (region.type == ClickRegion::RADAR_MARKER_SCALE_UP);
                float newScale = radarHud->getMarkerScale() + (increase ? 0.1f : -0.1f);
                radarHud->setMarkerScale(newScale);
                rebuildRenderData();
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
        case RadarHud::RadarMode::ON:        radarModeDisplayStr = "On"; break;
        case RadarHud::RadarMode::AUTO_HIDE: radarModeDisplayStr = "Auto-hide"; break;
    }
    ctx.addCycleControl("Radar mode", radarModeDisplayStr, 10,
        SettingsHud::ClickRegion::RADAR_MODE_DOWN,
        SettingsHud::ClickRegion::RADAR_MODE_UP,
        hud, true, radarModeIsOff, "radar.mode");

    // Range control
    char rangeValue[16];
    snprintf(rangeValue, sizeof(rangeValue), "%.0fm", hud->getRadarRange());
    ctx.addCycleControl("Radar range", rangeValue, 10,
        SettingsHud::ClickRegion::RADAR_RANGE_DOWN,
        SettingsHud::ClickRegion::RADAR_RANGE_UP,
        hud, true, false, "radar.range");

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
        SettingsHud::ClickRegion::RADAR_COLORIZE_DOWN,
        SettingsHud::ClickRegion::RADAR_COLORIZE_UP,
        hud, true, false, "radar.colorize");

    // Rider shape control - uses all icons from AssetManager
    std::string radarShapeStr = getShapeDisplayName(hud->getRiderShape(), 10);
    ctx.addCycleControl("Marker icon", radarShapeStr.c_str(), 10,
        SettingsHud::ClickRegion::RADAR_RIDER_SHAPE_DOWN,
        SettingsHud::ClickRegion::RADAR_RIDER_SHAPE_UP,
        hud, true, false, "radar.rider_shape");

    // Marker scale control (independent scale for icons/labels)
    char radarMarkerScaleValue[16];
    snprintf(radarMarkerScaleValue, sizeof(radarMarkerScaleValue), "%.0f%%", hud->getMarkerScale() * 100.0f);
    ctx.addCycleControl("Marker scale", radarMarkerScaleValue, 10,
        SettingsHud::ClickRegion::RADAR_MARKER_SCALE_DOWN,
        SettingsHud::ClickRegion::RADAR_MARKER_SCALE_UP,
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
        SettingsHud::ClickRegion::RADAR_LABEL_MODE_DOWN,
        SettingsHud::ClickRegion::RADAR_LABEL_MODE_UP,
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
    ctx.addCycleControl("Arrow mode", proxArrowModeStr, 10,
        SettingsHud::ClickRegion::RADAR_PROXIMITY_ARROWS_DOWN,
        SettingsHud::ClickRegion::RADAR_PROXIMITY_ARROWS_UP,
        hud, true, proxArrowIsOff, "radar.proximity_arrows");

    // Alert distance control (when triangles/arrows activate)
    char alertValue[16];
    snprintf(alertValue, sizeof(alertValue), "%.0fm", hud->getAlertDistance());
    ctx.addCycleControl("Alert distance", alertValue, 10,
        SettingsHud::ClickRegion::RADAR_ALERT_DISTANCE_DOWN,
        SettingsHud::ClickRegion::RADAR_ALERT_DISTANCE_UP,
        hud, true, false, "radar.alert_distance");

    // Proximity arrow color mode control
    const char* proxColorModeStr = "";
    switch (hud->getProximityArrowColorMode()) {
        case RadarHud::ProximityArrowColorMode::DISTANCE: proxColorModeStr = "Distance"; break;
        case RadarHud::ProximityArrowColorMode::POSITION: proxColorModeStr = "Position"; break;
    }
    ctx.addCycleControl("Arrow colors", proxColorModeStr, 10,
        SettingsHud::ClickRegion::RADAR_PROXIMITY_COLOR_DOWN,
        SettingsHud::ClickRegion::RADAR_PROXIMITY_COLOR_UP,
        hud, true, false, "radar.proximity_color");

    // Proximity arrow shape control
    std::string proxShapeStr = getShapeDisplayName(hud->getProximityArrowShape(), 10);
    ctx.addCycleControl("Arrow icon", proxShapeStr.c_str(), 10,
        SettingsHud::ClickRegion::RADAR_PROXIMITY_SHAPE_DOWN,
        SettingsHud::ClickRegion::RADAR_PROXIMITY_SHAPE_UP,
        hud, true, false, "radar.proximity_shape");

    // Proximity arrow scale control
    char proxScaleValue[16];
    snprintf(proxScaleValue, sizeof(proxScaleValue), "%.0f%%", hud->getProximityArrowScale() * 100.0f);
    ctx.addCycleControl("Arrow scale", proxScaleValue, 10,
        SettingsHud::ClickRegion::RADAR_PROXIMITY_SCALE_DOWN,
        SettingsHud::ClickRegion::RADAR_PROXIMITY_SCALE_UP,
        hud, true, false, "radar.proximity_scale");

    return hud;
}
