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

using namespace PluginConstants;

namespace {
    // The strip covers the top half of the rev range: below that a light tells
    // the rider nothing they need to know.
    constexpr float RANGE_START = 0.5f;
    // Amber covers this share of the strip just below the first red segment.
    constexpr float AMBER_SHARE = 0.2f;
    // A gap is this share of one segment's pitch.
    constexpr float GAP_SHARE = 0.25f;
    // Without a known shift point (spectating, or a bike without one) this share
    // of the strip is red.
    constexpr float DEFAULT_RED_SHARE = 0.25f;
    // Limiter flash of the red segments: lit for half a period, dark for the
    // other half (5 Hz).
    constexpr long long FLASH_HALF_PERIOD_US = 100000;
    // An unlit segment is its own colour at this opacity, so the strip's shape
    // and colour bands read before anything lights.
    constexpr float UNLIT_OPACITY = 0.25f;
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
    m_quads.reserve(10 + MAX_SEGMENTS);  // 9-slice themed background + card + one quad per segment

    setTextureBaseName("rpm_widget");

    resetToDefaults();
    rebuildRenderData();
}

bool RpmWidget::handlesDataType(DataChangeType dataType) const {
    return dataType == DataChangeType::InputTelemetry ||
           dataType == DataChangeType::SpectateTarget ||
           dataType == DataChangeType::SessionData;
}

void RpmWidget::update() {
    if (!isVisibleAnySurface()) {
        clearDataDirty();
        clearLayoutDirty();
        return;
    }

    // GATED ON WHAT IS DRAWN, not on the telemetry tick: RPM moves every tick,
    // but the quads only change when a segment lights or the flash flips.
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
    const bool hasRange = bike.isValid && topRPM > 0;

    const int segments = std::clamp(m_segments, MIN_SEGMENTS, MAX_SEGMENTS);
    const int red = redSegments(segments, shiftRPM, topRPM);
    int lit = 0;
    bool onLimiter = false;
    bool flashOn = false;
    if (hasRange) {
        onLimiter = viewingPlayer && hasLimiter && bike.rpm >= session.limiterRPM;
        if (onLimiter) {
            lit = segments;
            flashOn = (DigitRoll::nowUs() / FLASH_HALF_PERIOD_US) % 2 == 0;
        } else {
            // THE FIRST RED LIGHT COMES ON AT THE SHIFT POINT, not near it: the
            // segments below it share [start, shift) and the red ones share
            // [shift, limiter), so a segment boundary sits exactly on the shift
            // point whatever the count. One even split over the whole range
            // would light the first red segment up to a segment's worth of revs
            // late. The limiter then takes over with the flash.
            const float limiter = static_cast<float>(topRPM);
            const float start = limiter * RANGE_START;
            const int below = segments - red;
            const float shiftAt = static_cast<float>(shiftRPM);
            const float shift = (shiftAt > start && shiftAt < limiter)
                ? shiftAt
                : start + (limiter - start) * static_cast<float>(below) / static_cast<float>(segments);
            const float rpm = static_cast<float>(bike.rpm);
            for (int i = 0; i < segments; ++i) {
                const float at = (i < below)
                    ? start + (shift - start) * static_cast<float>(i) / static_cast<float>(below)
                    : shift + (limiter - shift) * static_cast<float>(i - below) / static_cast<float>(red);
                if (rpm < at) break;
                lit = i + 1;
            }
        }
    }

    if (lit != m_lit || flashOn != m_flashOn || onLimiter != m_onLimiter || hasRange != m_hasRange ||
        red != m_red) {
        m_lit = lit;
        m_flashOn = flashOn;
        m_onLimiter = onLimiter;
        m_hasRange = hasRange;
        m_red = red;
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

    // BOX-MODEL: horizontal, one normal row (the Version widget's height) that
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
    const float gap = m_bSegmentGaps ? length / static_cast<float>(segments) * GAP_SHARE : 0.0f;

    // Every segment is the same length and the (segments - 1) gaps sit only
    // BETWEEN them, so the strip spans its full content length either way.
    const float segLen = (length - gap * static_cast<float>(segments - 1)) / static_cast<float>(segments);

    const unsigned long positive = this->getColor(ColorSlot::POSITIVE);
    const unsigned long warning = this->getColor(ColorSlot::WARNING);
    const unsigned long negative = this->getColor(ColorSlot::NEGATIVE);

    // The last m_red segments are red, the AMBER_SHARE of the strip before them
    // amber, the rest green.
    const int redFrom = segments - std::min(m_red, segments);
    const int amberFrom = std::max(0, redFrom - static_cast<int>(static_cast<float>(segments) * AMBER_SHARE + 0.5f));

    for (int i = 0; i < segments; ++i) {
        unsigned long color = (i >= redFrom) ? negative
                            : (i >= amberFrom) ? warning : positive;
        // On the limiter only the red segments flash; the rest stay lit.
        const bool lit = (m_onLimiter && i >= redFrom) ? m_flashOn : i < m_lit;
        if (!lit) color = PluginUtils::applyOpacity(color, UNLIT_OPACITY);

        // Horizontal fills left to right, vertical bottom to top.
        const float along = (segLen + gap) * static_cast<float>(i);
        float x = m_bVertical ? stripX : stripX + along;
        float y = m_bVertical ? stripY + stripH - along - segLen : stripY;
        SPluginQuad_t quad{};
        applyOffset(x, y);
        setQuadPositions(quad, x, y, m_bVertical ? stripW : segLen, m_bVertical ? segLen : stripH);
        quad.m_iSprite = SpriteIndex::SOLID_COLOR;
        quad.m_ulColor = color;
        m_quads.push_back(quad);
    }

    setBounds(0.0f, 0.0f, p.width(), p.height());
}

void RpmWidget::resetToDefaults() {
    m_bVisible = false;           // opt-in, like the other gauges
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
