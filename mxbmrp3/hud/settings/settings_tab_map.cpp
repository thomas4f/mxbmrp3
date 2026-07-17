// ============================================================================
// hud/settings/settings_tab_map.cpp
// Tab renderer for Map HUD settings
// ============================================================================
#include "settings_layout.h"
#include "../settings_hud.h"
#include "../map_hud.h"
#include "../../core/asset_manager.h"
#include <cmath>

// Static member function of SettingsHud - handles click events for Map tab
bool SettingsHud::handleClickTabMap(const ClickRegion& region) {
    MapHud* mapHud = dynamic_cast<MapHud*>(region.targetHud);

    switch (region.type) {
        // Track width, Detail and Marker scale are data-driven STEPPED controls -
        // registered in renderTabMap via ctx.addSteppedControl (their setters were
        // just clamp + dedup + setDataDirty) and handled by the shared
        // SettingsHud::applySteppedControl. Zoom range and Track outline keep
        // dedicated handlers: their "Off"/"Full" state below the minimum can't be
        // expressed as a plain clamped step.
        case ClickRegion::MAP_ROTATION_TOGGLE:
            if (mapHud) {
                mapHud->setRotateToPlayer(!mapHud->getRotateToPlayer());
                rebuildRenderData();
            }
            return true;

        case ClickRegion::MAP_OUTLINE_UP:
        case ClickRegion::MAP_OUTLINE_DOWN:
            if (mapHud) {
                // One control for on/off + width, mirroring the zoom Range UX:
                // "Off" sits just below the minimum width — stepping down past the
                // minimum disables the outline, stepping up from Off re-enables at
                // the minimum. 1% base step with hold-acceleration.
                bool increase = (region.type == ClickRegion::MAP_OUTLINE_UP);
                if (!mapHud->getShowOutline()) {
                    if (increase) {
                        mapHud->setShowOutline(true);
                        mapHud->setOutlineWidthScale(MapHud::MIN_OUTLINE_WIDTH_SCALE);
                    }
                } else {
                    float newScale = applyAcceleratedStep(mapHud->getOutlineWidthScale(), 0.01f, increase);
                    if (!increase && newScale < MapHud::MIN_OUTLINE_WIDTH_SCALE) {
                        mapHud->setShowOutline(false);  // step below the minimum → Off
                    } else {
                        mapHud->setOutlineWidthScale(newScale);  // setter clamps the top
                    }
                }
                rebuildRenderData();
            }
            return true;

        case ClickRegion::MAP_MARKERS_TOGGLE:
            if (mapHud) {
                mapHud->setShowTrackMarkers(!mapHud->getShowTrackMarkers());
                rebuildRenderData();
            }
            return true;

        // Marker colors / Marker labels are data-driven CYCLE controls now -
        // registered in renderTabMap via ctx.addCycleControl.

        case ClickRegion::MAP_RANGE_UP:
        case ClickRegion::MAP_RANGE_DOWN:
            if (mapHud) {
                // Continuous range with hold-acceleration (10m base step), clamped to
                // [MIN_ZOOM_DISTANCE, MAX_ZOOM_DISTANCE]. "Full" (zoom off) sits just
                // below the minimum: stepping down past it disables zoom, stepping up
                // from it re-enables at the minimum distance.
                bool increase = (region.type == ClickRegion::MAP_RANGE_UP);
                if (!mapHud->getZoomEnabled()) {
                    // From "Full": only UP does anything — enable at the minimum.
                    if (increase) {
                        mapHud->setZoomEnabled(true);
                        mapHud->setZoomDistance(MapHud::MIN_ZOOM_DISTANCE);
                    }
                } else {
                    float newDist = applyAcceleratedStep(mapHud->getZoomDistance(), 10.0f, increase);
                    if (!increase && newDist < MapHud::MIN_ZOOM_DISTANCE) {
                        mapHud->setZoomEnabled(false);  // step below the minimum → Full
                    } else {
                        mapHud->setZoomDistance(newDist);  // setZoomDistance clamps the top
                    }
                }
                rebuildRenderData();
            }
            return true;

        case ClickRegion::MAP_RIDER_SHAPE_UP:
        case ClickRegion::MAP_RIDER_SHAPE_DOWN:
            if (mapHud) {
                bool forward = (region.type == ClickRegion::MAP_RIDER_SHAPE_UP);
                // [0..count] with 0 = Off; skips HUD identity icons.
                mapHud->setRiderShape(AssetManager::getInstance()
                    .stepShapeIndexSkippingHud(mapHud->getRiderShape(), forward));
                rebuildRenderData();
            }
            return true;

        case ClickRegion::MAP_DETAIL_ADAPTIVE_TOGGLE:
            if (mapHud) {
                mapHud->setAdaptiveDetail(!mapHud->getAdaptiveDetail());
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

    // === LAYOUT SECTION ===
    ctx.addSectionHeader("Layout");

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
    // Order: the ribbon itself first (width, then the tessellation pair that
    // shapes it), decorations after (outline rim, markers).
    ctx.addSectionHeader("Track");

    // Track line width scale: accelerated 1% step, clamped to [50%, 300%]
    char trackWidthValue[16];
    snprintf(trackWidthValue, sizeof(trackWidthValue), "%.0f%%", hud->getTrackWidthScale() * 100.0f);
    ctx.addSteppedControl("Track width", trackWidthValue, 10,
        SettingsHud::SteppedControl::stepFloat(&hud->m_fTrackWidthScale, 0.01f,
            MapHud::MIN_TRACK_WIDTH_SCALE, MapHud::MAX_TRACK_WIDTH_SCALE, hud),
        hud, true, false, "map.track_width");

    // Detail scale (20-200%) — ribbon quad density, the map's CPU/GPU budget dial.
    // Accelerated 1% step — the same feel as Track width / Marker scale.
    char detailValue[16];
    snprintf(detailValue, sizeof(detailValue), "%.0f%%", hud->getDetailScale() * 100.0f);
    ctx.addSteppedControl("Detail", detailValue, 10,
        SettingsHud::SteppedControl::stepFloat(&hud->m_fDetailScale, 0.01f,
            MapHud::MIN_DETAIL_SCALE, MapHud::MAX_DETAIL_SCALE, hud),
        hud, true, false, "map.detail");

    // Adaptive detail — normalize density in screen space across tracks/zoom
    ctx.addToggleControl("Adaptive detail", hud->getAdaptiveDetail(),
        SettingsHud::ClickRegion::MAP_DETAIL_ADAPTIVE_TOGGLE, hud, nullptr, 0, true,
        "map.detail_adaptive");

    // Outline width (Off = no outline, or rim width as a percentage)
    char outlineValue[16];
    if (hud->getShowOutline()) {
        snprintf(outlineValue, sizeof(outlineValue), "%.0f%%", hud->getOutlineWidthScale() * 100.0f);
    } else {
        snprintf(outlineValue, sizeof(outlineValue), "Off");
    }
    ctx.addCycleControl("Track outline", outlineValue, 10,
        SettingsHud::ClickRegion::MAP_OUTLINE_DOWN,
        SettingsHud::ClickRegion::MAP_OUTLINE_UP,
        hud, true, !hud->getShowOutline(), "map.outline");

    // Track markers toggle (S/F, sector markers, segment lines)
    ctx.addToggleControl("Show markers", hud->getShowTrackMarkers(),
        SettingsHud::ClickRegion::MAP_MARKERS_TOGGLE, hud, nullptr, 0, true,
        "map.markers");
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
        SettingsHud::CycleControl::enumMember(hud, &MapHud::m_riderColorMode, 3, hud),
        hud, true, false, "map.colorize");

    // Rider shape control (0=OFF, 1-N=shapes)
    int mapShapeIndex = hud->getRiderShape();
    bool shapeIsOff = (mapShapeIndex == 0);
    std::string shapeStr = getShapeDisplayName(mapShapeIndex, 10);
    ctx.addCycleControl("Marker icon", shapeStr.c_str(), 10,
        SettingsHud::ClickRegion::MAP_RIDER_SHAPE_DOWN,
        SettingsHud::ClickRegion::MAP_RIDER_SHAPE_UP,
        hud, true, shapeIsOff, "map.rider_shape");

    // Marker scale control: accelerated 1% step, clamped to [50%, 300%]
    char mapMarkerScaleValue[16];
    snprintf(mapMarkerScaleValue, sizeof(mapMarkerScaleValue), "%.0f%%", hud->getMarkerScale() * 100.0f);
    ctx.addSteppedControl("Marker scale", mapMarkerScaleValue, 10,
        SettingsHud::SteppedControl::stepFloat(&hud->m_fMarkerScale, 0.01f,
            MapHud::MIN_MARKER_SCALE, MapHud::MAX_MARKER_SCALE, hud),
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
        SettingsHud::CycleControl::enumMember(hud, &MapHud::m_labelMode, 4, hud),
        hud, true, labelIsOff, "map.labels");

    // Performance tip (mirrors the "More options" footer style on the Widgets tab)
    ctx.currentY += ctx.lineHeightNormal * 0.5f;
    ctx.parent->addString("Tip: lower Detail or Track outline to gain FPS.",
        ctx.labelX, ctx.currentY,
        PluginConstants::Justify::LEFT, PluginConstants::Fonts::getNormal(),
        ColorConfig::getInstance().getMuted(), ctx.fontSize * 0.9f);

    return hud;
}
