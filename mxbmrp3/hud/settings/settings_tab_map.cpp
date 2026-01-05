// ============================================================================
// hud/settings/settings_tab_map.cpp
// Tab renderer for Map HUD settings
// ============================================================================
#include "settings_layout.h"
#include "../settings_hud.h"
#include "../map_hud.h"
#include "../../core/asset_manager.h"
#include <cmath>

// Helper to cycle enum value forward or backward with wrapping
template<typename T>
static T cycleEnum(T current, int count, bool forward) {
    int val = static_cast<int>(current);
    if (forward) {
        val = (val + 1) % count;
    } else {
        val = (val - 1 + count) % count;
    }
    return static_cast<T>(val);
}

// Static member function of SettingsHud - handles click events for Map tab
bool SettingsHud::handleClickTabMap(const ClickRegion& region) {
    MapHud* mapHud = dynamic_cast<MapHud*>(region.targetHud);

    switch (region.type) {
        case ClickRegion::MAP_ROTATION_TOGGLE:
            if (mapHud) {
                mapHud->setRotateToPlayer(!mapHud->getRotateToPlayer());
                rebuildRenderData();
            }
            return true;

        case ClickRegion::MAP_OUTLINE_TOGGLE:
            if (mapHud) {
                mapHud->setShowOutline(!mapHud->getShowOutline());
                rebuildRenderData();
            }
            return true;

        case ClickRegion::MAP_COLORIZE_UP:
        case ClickRegion::MAP_COLORIZE_DOWN:
            if (mapHud) {
                auto newMode = cycleEnum(mapHud->getRiderColorMode(), 3,
                    region.type == ClickRegion::MAP_COLORIZE_UP);
                mapHud->setRiderColorMode(newMode);
                rebuildRenderData();
            }
            return true;

        case ClickRegion::MAP_TRACK_WIDTH_UP:
        case ClickRegion::MAP_TRACK_WIDTH_DOWN:
            if (mapHud) {
                bool increase = (region.type == ClickRegion::MAP_TRACK_WIDTH_UP);
                float newScale = mapHud->getTrackWidthScale() + (increase ? 0.1f : -0.1f);
                mapHud->setTrackWidthScale(newScale);
                rebuildRenderData();
            }
            return true;

        case ClickRegion::MAP_LABEL_MODE_UP:
        case ClickRegion::MAP_LABEL_MODE_DOWN:
            if (mapHud) {
                auto newMode = cycleEnum(mapHud->getLabelMode(), 4,
                    region.type == ClickRegion::MAP_LABEL_MODE_UP);
                mapHud->setLabelMode(newMode);
                rebuildRenderData();
            }
            return true;

        case ClickRegion::MAP_RANGE_UP:
        case ClickRegion::MAP_RANGE_DOWN:
            if (mapHud) {
                // Discrete range values: Full (index -1), then 50, 100, 200, 500
                static constexpr float RANGE_VALUES[] = { 50.0f, 100.0f, 200.0f, 500.0f };
                static constexpr int NUM_VALUES = sizeof(RANGE_VALUES) / sizeof(RANGE_VALUES[0]);
                bool increase = (region.type == ClickRegion::MAP_RANGE_UP);

                // Find current index (-1 = Full, 0-3 = zoom values)
                int currentIndex = -1;
                if (mapHud->getZoomEnabled()) {
                    float currentDist = mapHud->getZoomDistance();
                    for (int i = 0; i < NUM_VALUES; ++i) {
                        if (std::abs(currentDist - RANGE_VALUES[i]) < 0.5f) {
                            currentIndex = i;
                            break;
                        }
                    }
                    if (currentIndex == -1) currentIndex = 0;
                }

                // Calculate new index with wrapping
                int newIndex;
                if (increase) {
                    newIndex = (currentIndex + 1 + 1) % (NUM_VALUES + 1) - 1;
                } else {
                    newIndex = (currentIndex + NUM_VALUES + 1) % (NUM_VALUES + 1) - 1;
                }

                // Apply new value
                if (newIndex == -1) {
                    mapHud->setZoomEnabled(false);
                } else {
                    mapHud->setZoomEnabled(true);
                    mapHud->setZoomDistance(RANGE_VALUES[newIndex]);
                }
                rebuildRenderData();
            }
            return true;

        case ClickRegion::MAP_RIDER_SHAPE_UP:
        case ClickRegion::MAP_RIDER_SHAPE_DOWN:
            if (mapHud) {
                bool forward = (region.type == ClickRegion::MAP_RIDER_SHAPE_UP);
                int iconCount = static_cast<int>(AssetManager::getInstance().getIconCount());
                int current = mapHud->getRiderShape();
                int next;
                if (forward) {
                    next = (current + 1) % (iconCount + 1);
                } else {
                    next = (current - 1 + iconCount + 1) % (iconCount + 1);
                }
                mapHud->setRiderShape(next);
                rebuildRenderData();
            }
            return true;

        case ClickRegion::MAP_MARKER_SCALE_UP:
        case ClickRegion::MAP_MARKER_SCALE_DOWN:
            if (mapHud) {
                bool increase = (region.type == ClickRegion::MAP_MARKER_SCALE_UP);
                float newScale = mapHud->getMarkerScale() + (increase ? 0.1f : -0.1f);
                mapHud->setMarkerScale(newScale);
                rebuildRenderData();
            }
            return true;

        default:
            return false;
    }
}

// Static member function of SettingsHud - inherits friend access to MapHud
BaseHud* SettingsHud::renderTabMap(SettingsLayoutContext& ctx) {
    MapHud* hud = ctx.parent->getMapHud();
    if (!hud) return nullptr;

    ctx.addTabTooltip("map");

    // === APPEARANCE SECTION ===
    ctx.addSectionHeader("Appearance");
    ctx.addStandardHudControls(hud);
    ctx.addSpacing(0.5f);

    // === VIEW SECTION ===
    ctx.addSectionHeader("View");

    // Range control (Full = no zoom, or zoom distance in meters)
    char rangeValue[16];
    if (hud->getZoomEnabled()) {
        snprintf(rangeValue, sizeof(rangeValue), "%.0fm", hud->getZoomDistance());
    } else {
        snprintf(rangeValue, sizeof(rangeValue), "Full");
    }
    ctx.addCycleControl("Zoom range", rangeValue, 10,
        SettingsHud::ClickRegion::MAP_RANGE_DOWN,
        SettingsHud::ClickRegion::MAP_RANGE_UP,
        hud, true, false, "map.range");

    // Rotation toggle
    ctx.addToggleControl("Rotate with player", hud->getRotateToPlayer(),
        SettingsHud::ClickRegion::MAP_ROTATION_TOGGLE, hud, nullptr, 0, true,
        "map.rotation");
    ctx.addSpacing(0.5f);

    // === TRACK SECTION ===
    ctx.addSectionHeader("Track");

    // Outline toggle
    ctx.addToggleControl("Show track outline", hud->getShowOutline(),
        SettingsHud::ClickRegion::MAP_OUTLINE_TOGGLE, hud, nullptr, 0, true,
        "map.outline");

    // Track line width scale
    char trackWidthValue[16];
    snprintf(trackWidthValue, sizeof(trackWidthValue), "%.0f%%", hud->getTrackWidthScale() * 100.0f);
    ctx.addCycleControl("Track width", trackWidthValue, 10,
        SettingsHud::ClickRegion::MAP_TRACK_WIDTH_DOWN,
        SettingsHud::ClickRegion::MAP_TRACK_WIDTH_UP,
        hud, true, false, "map.track_width");
    ctx.addSpacing(0.5f);

    // === RIDER MARKERS SECTION ===
    ctx.addSectionHeader("Rider Markers");

    // Rider color mode
    const char* mapColorModeStr = "";
    switch (hud->getRiderColorMode()) {
        case MapHud::RiderColorMode::UNIFORM:      mapColorModeStr = "Uniform"; break;
        case MapHud::RiderColorMode::BRAND:        mapColorModeStr = "Brand"; break;
        case MapHud::RiderColorMode::RELATIVE_POS: mapColorModeStr = "Position"; break;
    }
    ctx.addCycleControl("Marker colors", mapColorModeStr, 10,
        SettingsHud::ClickRegion::MAP_COLORIZE_DOWN,
        SettingsHud::ClickRegion::MAP_COLORIZE_UP,
        hud, true, false, "map.colorize");

    // Rider shape control (0=OFF, 1-N=shapes)
    int mapShapeIndex = hud->getRiderShape();
    bool shapeIsOff = (mapShapeIndex == 0);
    std::string shapeStr = getShapeDisplayName(mapShapeIndex, 10);
    ctx.addCycleControl("Marker icon", shapeStr.c_str(), 10,
        SettingsHud::ClickRegion::MAP_RIDER_SHAPE_DOWN,
        SettingsHud::ClickRegion::MAP_RIDER_SHAPE_UP,
        hud, true, shapeIsOff, "map.rider_shape");

    // Marker scale control
    char mapMarkerScaleValue[16];
    snprintf(mapMarkerScaleValue, sizeof(mapMarkerScaleValue), "%.0f%%", hud->getMarkerScale() * 100.0f);
    ctx.addCycleControl("Marker scale", mapMarkerScaleValue, 10,
        SettingsHud::ClickRegion::MAP_MARKER_SCALE_DOWN,
        SettingsHud::ClickRegion::MAP_MARKER_SCALE_UP,
        hud, true, false, "map.marker_scale");

    // Label mode control
    const char* modeStr = "";
    bool labelIsOff = (hud->getLabelMode() == MapHud::LabelMode::NONE);
    switch (hud->getLabelMode()) {
        case MapHud::LabelMode::NONE:     modeStr = "Off"; break;
        case MapHud::LabelMode::POSITION: modeStr = "Position"; break;
        case MapHud::LabelMode::RACE_NUM: modeStr = "Race Num"; break;
        case MapHud::LabelMode::BOTH:     modeStr = "Both"; break;
        default:
            modeStr = "Unknown";
            break;
    }
    ctx.addCycleControl("Marker labels", modeStr, 10,
        SettingsHud::ClickRegion::MAP_LABEL_MODE_DOWN,
        SettingsHud::ClickRegion::MAP_LABEL_MODE_UP,
        hud, true, labelIsOff, "map.labels");

    return hud;
}
