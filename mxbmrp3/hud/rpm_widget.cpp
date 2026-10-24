// ============================================================================
// hud/rpm_widget.cpp
// RPM widget - shift-light strip; see rpm_widget.h for what it is for.
// ============================================================================
#include "rpm_widget.h"

#include <algorithm>

#include "digit_roll.h"
#include "../core/plugin_data.h"
#include "../core/plugin_utils.h"
#include "../core/color_config.h"
#include "../core/ui_config.h"

using namespace PluginConstants;

namespace {
    // The strip covers the top half of the rev range: below that a light tells
    // the rider nothing they need to know.
    constexpr float RANGE_START = 0.5f;
    // Amber covers this share of the strip just below the first red segment.
    constexpr float AMBER_SHARE = 0.2f;
    // Without a known shift point (spectating, or a bike without one) this share
    // of the strip is red.
    constexpr float DEFAULT_RED_SHARE = 0.25f;
    // Limiter flash of the red segments: lit for half a period, dark for the
    // other half (5 Hz).
    constexpr long long FLASH_HALF_PERIOD_US = 100000;
}

int RpmWidget::redSegments(int segments, int shiftRPM, int limiterRPM) {
    // The red share of the strip is the shift-to-limiter share of its range,
    // and never all or none of it: one light at least is the shift light, and
    // one at least comes on before it.
    float share = DEFAULT_RED_SHARE;
    const float limiter = static_cast<float>(limiterRPM);
    const float shift = static_cast<float>(shiftRPM);
    const float start = limiter * RANGE_START;
    if (limiterRPM > 0 && shift > start && shift < limiter)
        share = (limiter - shift) / (limiter - start);
    return std::clamp(static_cast<int>(static_cast<float>(segments) * share + 0.5f), 1, segments - 1);
}

RpmWidget::RpmWidget()
{
    m_panelKind = PanelKind::Widget;
    m_bContentCard = true;
    setDraggable(true);
    m_quads.reserve(10 + 2 * MAX_SEGMENTS);  // 9-slice themed background + card + a segment and its glow each

    setTextureBaseName("rpm_widget");

    resetToDefaults();
    rebuildRenderData();
}

bool RpmWidget::handlesDataType(DataChangeType dataType) const {
    return dataType == DataChangeType::InputTelemetry ||
           dataType == DataChangeType::SpectateTarget ||
           dataType == DataChangeType::SessionData;
}

RpmWidget::Lights RpmWidget::readLights(int segments) {
    const PluginData& pluginData = PluginData::getInstance();
    const BikeTelemetryData& bike = pluginData.getBikeTelemetry();
    const SessionData& session = pluginData.getSessionData();
    // The shift point belongs to the PLAYER's bike (GearWidget's gate): a spectated
    // rider's strip is plain rpm over the session's range, with the default red
    // share and no limiter flash.
    const bool viewingPlayer = pluginData.getDisplayRaceNum() == pluginData.getPlayerRaceNum();
    const int shiftRPM = viewingPlayer ? session.shiftRPM : 0;
    // The strip tops out at the limiter. A vehicle WITHOUT one (KRP's direct-drive
    // karts report 0) tops out at its max rpm instead, and never flashes.
    const bool hasLimiter = session.limiterRPM > 0;
    const int topRPM = session.rpmRangeTop();

    Lights lights;
    lights.hasRange = bike.isValid && topRPM > 0;
    lights.red = redSegments(segments, shiftRPM, topRPM);
    if (lights.hasRange) {
        lights.onLimiter = viewingPlayer && hasLimiter && bike.rpm >= session.limiterRPM;
        if (lights.onLimiter) {
            lights.lit = segments;
            lights.flashOn = (DigitRoll::nowUs() / FLASH_HALF_PERIOD_US) % 2 == 0;
        } else {
            // THE FIRST RED LIGHT COMES ON AT THE SHIFT POINT, not near it: the
            // segments below it share [start, shift) and the red ones share
            // [shift, limiter), so a segment boundary sits exactly on the shift
            // point whatever the count. One even split over the whole range
            // would light the first red segment up to a segment's worth of revs
            // late. The limiter then takes over with the flash.
            const int red = lights.red;
            const float limiter = static_cast<float>(topRPM);
            const float start = limiter * RANGE_START;
            const int below = segments - red;
            const float shiftAt = static_cast<float>(shiftRPM);
            const float shift = (shiftAt > start && shiftAt < limiter)
                ? shiftAt
                : start + (limiter - start) * static_cast<float>(below) / static_cast<float>(segments);
            const float rpm = static_cast<float>(bike.rpm);
            // Where segment i comes on; `segments` is the top of the range.
            auto onAt = [&](int i) {
                return (i < below)
                    ? start + (shift - start) * static_cast<float>(i) / static_cast<float>(below)
                    : shift + (limiter - shift) * static_cast<float>(i - below) / static_cast<float>(red);
            };
            for (int i = 0; i < segments; ++i) {
                if (rpm < onAt(i)) break;
                lights.lit = i + 1;
            }
            // How far the revs are through the newest light's stretch, in
            // whole steps so the compare in update() moves a few times a
            // light, not every tick.
            if (lights.lit > 0) {
                const float from = onAt(lights.lit - 1);
                const float to = onAt(lights.lit);
                if (to > from)
                    lights.lead = std::clamp(static_cast<int>((rpm - from) / (to - from) * LEAD_STEPS),
                                             0, LEAD_STEPS - 1);
            }
        }
    }
    return lights;
}

unsigned long RpmWidget::segmentInk(int i, int segments, const Lights& lights,
                                    unsigned long positive, unsigned long warning, unsigned long negative) {
    // The last `red` segments are red, the AMBER_SHARE of the strip before them
    // amber, the rest green.
    const int redFrom = segments - std::min(lights.red, segments);
    const int amberFrom = std::max(0, redFrom - static_cast<int>(static_cast<float>(segments) * AMBER_SHARE + 0.5f));
    return segmentRed(i, segments, lights) ? negative
         : (i >= amberFrom) ? warning : positive;
}

unsigned long RpmWidget::segmentColor(int i, int segments, const Lights& lights,
                                      unsigned long positive, unsigned long warning, unsigned long negative) {
    const unsigned long color = segmentInk(i, segments, lights, positive, warning, negative);
    if (!segmentLit(i, segments, lights)) return PluginUtils::applyOpacity(color, UNLIT_OPACITY);
    // The newest one fades up over its stretch; on the limiter all are whole.
    if (lights.onLimiter || i != lights.lit - 1 || lights.lead == LEAD_STEPS - 1) return color;
    return PluginUtils::applyOpacity(color, onOpacity(stepShare(lights.lead)));
}

bool RpmWidget::segmentLit(int i, int segments, const Lights& lights) {
    // On the limiter only the red segments flash; the rest stay lit.
    return (lights.onLimiter && segmentRed(i, segments, lights)) ? lights.flashOn : i < lights.lit;
}

void RpmWidget::update() {
    if (!isVisibleAnySurface()) {
        clearDataDirty();
        clearLayoutDirty();
        return;
    }

    // GATED ON WHAT IS DRAWN, not on the telemetry tick: RPM moves every tick,
    // but the quads only change when a segment lights or steps up, or the flash
    // flips.
    const Lights lights = readLights(std::clamp(m_segments, MIN_SEGMENTS, MAX_SEGMENTS));
    if (lights != m_lights) {
        m_lights = lights;
        setDataDirty();
    }

    if (isDataDirty() || isLayoutDirty()) rebuildAndRecord();
    clearDataDirty();
    clearLayoutDirty();
}

void RpmWidget::rebuildLayout() {
    // BOX-MODEL: one source of geometry (see gear_widget).
    rebuildRenderData();
}

void RpmWidget::rebuildRenderData() {
    clearStrings();
    m_quads.clear();

    auto dim = getScaledDimensions();

    // BOX-MODEL: horizontal, one normal row (the Gap Bar's height) that
    // spans Gear + Speed by default. Vertical, the strip turned on its end: as
    // tall as Gear and Speed's content, and as wide as the horizontal strip is
    // tall -- the same row measured along x, so the aspect ratio converts it.
    PanelWant want;
    if (m_bVertical) {
        want.contentW = dim.lineHeightNormal / UI_ASPECT_RATIO;
        want.sectionH = { dim.lineHeightLarge + dim.lineHeightNormal };   // GearWidget's row
    } else {
        want.contentW = PluginUtils::calculateMonospaceTextWidth(
            std::clamp(m_widthChars, MIN_WIDTH, MAX_WIDTH), dim.fontSize);
        want.sectionH = { dim.lineHeightNormal };
    }
    // A vertical strip keeps its column width: the title may overhang it rather
    // than widen it into a block.
    want.captionW = m_bVertical ? 0.0f : planTitleWidth(dim, "RPM");
    PanelPlan& p = planPanel(dim, want);

    addPlanBackground(p, 0.0f, 0.0f);
    addPlanTitle(p, "RPM", this->getColor(ColorSlot::PRIMARY));

    const int segments = std::clamp(m_segments, MIN_SEGMENTS, MAX_SEGMENTS);
    const float stripX = p.contentX();
    const float stripW = p.contentW();
    const float stripY = p.contentY();
    // THE ROW THE PLAN LAID OUT (see GearWidget): the last section absorbs the
    // panel's ceil remainder.
    const float stripH = p.H(p.g.sections[0].h);
    // The strip's length is its width, or its height when vertical.
    const float length = m_bVertical ? stripH : stripW;
    const float gap = m_bSegmentGaps ? length / static_cast<float>(segments) * SEGMENT_GAP_SHARE : 0.0f;

    // Every segment is the same length and the (segments - 1) gaps sit only
    // BETWEEN them, so the strip spans its full content length either way.
    const float segLen = (length - gap * static_cast<float>(segments - 1)) / static_cast<float>(segments);

    const unsigned long positive = this->getColor(ColorSlot::POSITIVE);
    const unsigned long warning = this->getColor(ColorSlot::WARNING);
    const unsigned long negative = this->getColor(ColorSlot::NEGATIVE);

    // Horizontal fills left to right, vertical bottom to top.
    auto segmentRect = [&](int i, float& x, float& y, float& w, float& h) {
        const float along = (segLen + gap) * static_cast<float>(i);
        x = m_bVertical ? stripX : stripX + along;
        y = m_bVertical ? stripY + stripH - along - segLen : stripY;
        w = m_bVertical ? stripW : segLen;
        h = m_bVertical ? segLen : stripH;
    };

    // The lit segments' glow, all of it before any segment so no halo lies over
    // a neighbour. Its reach is a share of the strip's thickness (as a height).
    if (glowActive()) {
        const float spreadY = (m_bVertical ? stripW * UI_ASPECT_RATIO : stripH) * GLOW_SPREAD;
        for (int i = 0; i < segments; ++i) {
            if (!segmentLit(i, segments, m_lights)) continue;
            float x, y, w, h;
            segmentRect(i, x, y, w, h);
            // The red segments glow full, the green and amber at the calm share;
            // the newest one full from its first step.
            const unsigned long color = segmentInk(i, segments, m_lights, positive, warning, negative);
            addGlow(x, y, w, h, spreadY, color,
                    GLOW_OPACITY * (segmentRed(i, segments, m_lights) ? 1.0f : UiConfig::GLOW_CALM));
        }
    }

    for (int i = 0; i < segments; ++i) {
        const unsigned long color = segmentColor(i, segments, m_lights, positive, warning, negative);
        float x, y, w, h;
        segmentRect(i, x, y, w, h);
        SPluginQuad_t quad{};
        applyOffset(x, y);
        setQuadPositions(quad, x, y, w, h);
        quad.m_iSprite = SpriteIndex::SOLID_COLOR;
        quad.m_ulColor = color;
        m_quads.push_back(quad);
    }

    setBounds(0.0f, 0.0f, p.width(), p.height());
}

void RpmWidget::resetToDefaults() {
    m_bVisible = true;            // on for fresh installs, under Gear + Speed
    m_bShowTitle = false;
    setTextureVariant(0);
    m_fBackgroundOpacity = 0.0f;  // bare, like Gear and Speed above it
    setScale(1.0f);
    m_segments = 15;
    m_bSegmentGaps = true;
    m_bVertical = false;
    m_widthChars = DEFAULT_WIDTH;
    setPosition(cellsX(161), cellsY(82));   // under Gear + Speed
    setDataDirty();
}
