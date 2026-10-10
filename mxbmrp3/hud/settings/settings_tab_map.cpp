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
        // SettingsHud::applySteppedControl. Range and Track outline are stepped
        // descriptors too (Track outline with Off one step below the minimum at the
        // slider's left end); Mode and Marker icon are shared cycles.
        case ClickRegion::MAP_ROTATION_TOGGLE:
            if (mapHud) {
                mapHud->setRotateToPlayer(!mapHud->getRotateToPlayer());
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

        case ClickRegion::MAP_DETAIL_ADAPTIVE_TOGGLE:
            if (mapHud) {
                mapHud->setAdaptiveDetail(!mapHud->getAdaptiveDetail());
                rebuildRenderData();
            }
            return true;

        case ClickRegion::MAP_RANGE_ADAPTIVE_TOGGLE:
            if (mapHud) {
                mapHud->setAdaptiveRange(!mapHud->getAdaptiveRange());
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
    ctx.addSectionHeading("Appearance");
    ctx.addStandardHudControls(hud);
    // === LAYOUT SECTION ===
    ctx.addSectionHeading("Layout");
    ctx.beginColumns(2, 5);   // side by side (beginColumns): Follow's rows left, the view's right

    // Mode: the whole track (Overview), or a window that follows the rider. Range
    // is the window's size, so it is greyed at Overview (the stored distance is
    // kept for when Follow comes back) rather than sharing one slider with it.
    SettingsHud::CycleControl area;
    area.count = 2;
    area.get = [hud]() { return hud->getZoomEnabled() ? 1 : 0; };
    area.set = [hud](int v) { hud->setZoomEnabled(v == 1); };
    area.dirtyHud = hud;
    ctx.addCycleControl("Mode", hud->getZoomEnabled() ? "Follow" : "Overview", area,
        hud, true, false, "map.mode");

    char rangeValue[16];
    snprintf(rangeValue, sizeof(rangeValue), "%.0fm", hud->getZoomDistance());
    ctx.addSteppedControl("Range", rangeValue, SettingsHud::SteppedControl::accessor(
            [hud]() { return hud->getZoomDistance(); },
            [hud](float v) { hud->setZoomDistance(v); },
            10.0f, MapHud::MIN_ZOOM_DISTANCE, MapHud::MAX_ZOOM_DISTANCE, hud),
        hud, hud->getZoomEnabled(), false, "map.range");

    // Adaptive range: Follow's window grows with speed (Range at a standstill,
    // twice it flat out). Greyed at Overview like Range.
    ctx.addToggleControl("Adaptive range", hud->getAdaptiveRange(),
        SettingsHud::ClickRegion::MAP_RANGE_ADAPTIVE_TOGGLE, hud, nullptr, 0, hud->getZoomEnabled(),
        "map.range_adaptive");

    // Rotation toggle
    ctx.addToggleControl("Rotate map", hud->getRotateToPlayer(),
        SettingsHud::ClickRegion::MAP_ROTATION_TOGGLE, hud, nullptr, 0, true,
        "map.rotation");

    // Tilt: Off (flat), or how far the map is laid on the ground, seen from
    // above and behind. Zoomed only: the whole track tilted adds little over the
    // flat map, so at Overview it is greyed out (the stored tilt is kept for when
    // zoom comes back).
    char tiltValue[16];
    if (hud->getTilt() == 0) snprintf(tiltValue, sizeof(tiltValue), "Off");
    else snprintf(tiltValue, sizeof(tiltValue), "%d\xB0", hud->getTilt());
    ctx.addSteppedControl("Tilt", tiltValue,
        SettingsHud::SteppedControl::clampInt(&hud->m_tiltDeg, MapHud::TILT_STEP_DEG, 0, MapHud::MAX_TILT_DEG, hud),
        hud, hud->getZoomEnabled(), false, "map.tilt");

    // === TRACK SECTION ===
    // Order: the ribbon itself first (width, then the tessellation pair that
    // shapes it), decorations after (outline rim, markers).
    ctx.endColumns();

    ctx.addSectionHeading("Track");
    // The switches and sliders side by side; Lap delta, whose values are longer,
    // takes the full row under them with its arrows in their column.
    ctx.beginColumns(2, 5);

    // Track line width scale: accelerated 1% step, clamped to [50%, 300%]
    char trackWidthValue[16];
    snprintf(trackWidthValue, sizeof(trackWidthValue), "%.0f%%", hud->getTrackWidthScale() * 100.0f);
    ctx.addSteppedControl("Track width", trackWidthValue,
        SettingsHud::SteppedControl::stepFloat(&hud->m_fTrackWidthScale, 0.01f,
            MapHud::MIN_TRACK_WIDTH_SCALE, MapHud::MAX_TRACK_WIDTH_SCALE, hud),
        hud, true, false, "map.track_width");

    // Detail scale (20-200%) — ribbon quad density, the map's CPU/GPU budget dial.
    // Accelerated 1% step — the same feel as Track width / Marker scale.
    char detailValue[16];
    snprintf(detailValue, sizeof(detailValue), "%.0f%%", hud->getDetailScale() * 100.0f);
    ctx.addSteppedControl("Detail", detailValue,
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
    // Off sits one 1% step below the minimum width, at the slider's left end.
    constexpr float kOutlineOff = MapHud::MIN_OUTLINE_WIDTH_SCALE - 0.01f;
    ctx.addSteppedControl("Track outline", outlineValue, SettingsHud::SteppedControl::accessor(
            [hud]() { return hud->getShowOutline() ? hud->getOutlineWidthScale() : kOutlineOff; },
            [hud](float v) {
                const bool on = v >= MapHud::MIN_OUTLINE_WIDTH_SCALE - 0.001f;
                hud->setShowOutline(on);
                if (on) hud->setOutlineWidthScale(v);
            },
            0.01f, kOutlineOff, MapHud::MAX_OUTLINE_WIDTH_SCALE, hud),
        hud, true, !hud->getShowOutline(), "map.outline");

    // Track markers toggle (S/F, sector markers, segment lines)
    ctx.addToggleControl("Show markers", hud->getShowTrackMarkers(),
        SettingsHud::ClickRegion::MAP_MARKERS_TOGGLE, hud, nullptr, 0, true,
        "map.markers");
    ctx.endColumns();

    // Lap delta: Off, Default (General's reference), or the reference lap the
    // track colours compare against (the Gap Bar's names, in its order). The
    // list order differs from the enum's, where DEFAULT came last to keep the
    // stored values: Off, Default, then the references.
    static const char* const kLapDeltas[] = { "Off", "Default", "Session PB", "All-time", "Last lap" };
    static_assert(sizeof(kLapDeltas) / sizeof(kLapDeltas[0]) == MapHud::LAP_DELTA_COUNT, "one name per state");
    SettingsHud::CycleControl lapDelta;
    lapDelta.count = MapHud::LAP_DELTA_COUNT;
    lapDelta.get = [hud]() {
        const int v = static_cast<int>(hud->m_lapDelta);
        return hud->m_lapDelta == MapHud::LapDelta::DEFAULT ? 1 : (v == 0 ? 0 : v + 1);
    };
    lapDelta.set = [hud](int i) {
        hud->m_lapDelta = (i == 1) ? MapHud::LapDelta::DEFAULT
                        : static_cast<MapHud::LapDelta>(i == 0 ? 0 : i - 1);
    };
    lapDelta.nameOf = [](int i) { return std::string(kLapDeltas[i]); };
    lapDelta.dirtyHud = hud;
    ctx.setRowLabelChars(16);
    ctx.addCycleControl("Lap delta", kLapDeltas[lapDelta.get()], lapDelta,
        hud, true, hud->m_lapDelta == MapHud::LapDelta::OFF, "map.lap_delta");
    ctx.setRowLabelChars(0);

    // === RIDER MARKERS SECTION ===
    ctx.addSectionHeading("Rider Markers");
    // The icon's name takes a full row; the three short ones sit side by side
    // under it, all with their arrows in one column.
    constexpr int MARKER_LABEL_CHARS = 14;
    ctx.setRowLabelChars(MARKER_LABEL_CHARS);
    // Rider shape control (0=OFF, 1-N=shapes)
    int mapShapeIndex = hud->getRiderShape();
    bool shapeIsOff = (mapShapeIndex == 0);
    std::string shapeStr = getShapeDisplayName(mapShapeIndex);
    ctx.addCycleControl("Marker icon", shapeStr.c_str(),
        iconCycle([hud]() { return hud->getRiderShape(); }, [hud](int v) { hud->setRiderShape(v); }, hud),
        hud, true, shapeIsOff, "map.rider_shape");

    ctx.setRowLabelChars(0);
    ctx.beginColumns(2, 3, MARKER_LABEL_CHARS);


    // Rider color mode
    static const char* const kColorModes[] = { "Uniform", "Brand", "Position" };
    const char* mapColorModeStr = cycleName(kColorModes, static_cast<int>(hud->getRiderColorMode()));
    ctx.addCycleControl("Marker colors", mapColorModeStr,
        SettingsHud::CycleControl::enumMember(hud, &MapHud::m_riderColorMode, 3, hud, kColorModes),
        hud, true, false, "map.colorize");

    // Marker scale control: accelerated 1% step, clamped to [50%, 300%]
    char mapMarkerScaleValue[16];
    snprintf(mapMarkerScaleValue, sizeof(mapMarkerScaleValue), "%.0f%%", hud->getMarkerScale() * 100.0f);
    ctx.addSteppedControl("Marker scale", mapMarkerScaleValue,
        SettingsHud::SteppedControl::stepFloat(&hud->m_fMarkerScale, 0.01f,
            MapHud::MIN_MARKER_SCALE, MapHud::MAX_MARKER_SCALE, hud),
        hud, true, false, "map.marker_scale");

    // Label mode control
    static const char* const kLabelModes[] = { "Off", "Position", "Race number", "Both" };
    bool labelIsOff = (hud->getLabelMode() == MapHud::LabelMode::NONE);
    const char* modeStr = cycleName(kLabelModes, static_cast<int>(hud->getLabelMode()));
    ctx.addCycleControl("Marker labels", modeStr,
        SettingsHud::CycleControl::enumMember(hud, &MapHud::m_labelMode, 4, hud, kLabelModes),
        hud, true, labelIsOff, "map.labels");
    ctx.endColumns();

    ctx.addNote("Tip: lower Detail or Track outline to gain FPS.");

    return hud;
}
