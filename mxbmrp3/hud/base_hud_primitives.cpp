// ============================================================================
// hud/base_hud_primitives.cpp
// BaseHud's drawing primitives: arcs, dots, icons, line segments, needle and
// rotated-sprite quads, grid lines, strip charts, temperature colour, quad
// positioning, scaled dimensions, and the styled-string builder. Split from
// base_hud_render.cpp; every method body (and the file-local floatEquals
// helper the styled strings use) is unchanged.
// ============================================================================
#include "base_hud.h"
#include "../diagnostics/call_counters.h"
#include "../core/plugin_constants.h"
#include "../core/plugin_utils.h"
#include "../core/asset_manager.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
    // Epsilon comparison for floating-point values
    // Required to avoid precision issues when comparing scaled font sizes
    constexpr float FLOAT_EPSILON = 0.0001f;

    inline bool floatEquals(float a, float b) {
        return std::abs(a - b) < FLOAT_EPSILON;
    }
}
// ONE ARC EMITTER for every HUD that draws a ring. FmxHud and LeanWidget each
// carried a byte-identical private copy of this (they differed only in a
// `numSegments < 1` guard), so a fix to one silently left the other behind --
// which is exactly what happened with the cost below.
//
// NO sin/cos PER SEGMENT. Each vertex pair needs (sin, cos) of its own angle, and
// the obvious spelling calls both every iteration: at 32 segments per arc and eight
// arcs, FmxHud spent ~400 transcendental calls per rebuild, which is most of its
// 9.3us/frame. The angles are an arithmetic sequence, so the pair can be ROTATED
// instead of recomputed -- one angle-addition per step, two transcendentals for the
// whole arc regardless of length:
//
//     sin(A+d) = sinA*cosd + cosA*sind
//     cos(A+d) = cosA*cosd - sinA*sind
//
// The rotor accumulates in double so the drift over an arc is far below a pixel;
// recomputing from scratch would defeat the point, and float would visibly wander
// on a long arc.
void BaseHud::addArcSegment(float centerX, float centerY, float innerRadius, float outerRadius,
                            float startAngleRad, float endAngleRad, unsigned long color,
                            int numSegments) {
    using namespace PluginConstants;
    if (numSegments < 1) numSegments = 1;

    const double step = (static_cast<double>(endAngleRad) - startAngleRad) / numSegments;
    const double cosStep = std::cos(step);
    const double sinStep = std::sin(step);

    // The rotating unit vector, seeded at the start angle. sin/cos rather than
    // cos/sin because 0 rad means UP here and positive means clockwise.
    double sinA = std::sin(static_cast<double>(startAngleRad));
    double cosA = std::cos(static_cast<double>(startAngleRad));

    float prevInnerX = 0.0f, prevInnerY = 0.0f;
    float prevOuterX = 0.0f, prevOuterY = 0.0f;
    bool hasPrevPoint = false;

    for (int i = 0; i <= numSegments; ++i) {
        const float sa = static_cast<float>(sinA);
        const float ca = static_cast<float>(cosA);

        const float innerX = centerX + sa * innerRadius / UI_ASPECT_RATIO;
        const float innerY = centerY - ca * innerRadius;
        const float outerX = centerX + sa * outerRadius / UI_ASPECT_RATIO;
        const float outerY = centerY - ca * outerRadius;

        if (hasPrevPoint) {
            float pix = prevInnerX, piy = prevInnerY;
            float pox = prevOuterX, poy = prevOuterY;
            float cix = innerX,     ciy = innerY;
            float cox = outerX,     coy = outerY;
            applyOffset(pix, piy);
            applyOffset(pox, poy);
            applyOffset(cix, ciy);
            applyOffset(cox, coy);

            // prevOuter -> prevInner -> currInner -> currOuter (counter-clockwise,
            // matching what the engine expects).
            SPluginQuad_t quad;
            quad.m_aafPos[0][0] = pox; quad.m_aafPos[0][1] = poy;
            quad.m_aafPos[1][0] = pix; quad.m_aafPos[1][1] = piy;
            quad.m_aafPos[2][0] = cix; quad.m_aafPos[2][1] = ciy;
            quad.m_aafPos[3][0] = cox; quad.m_aafPos[3][1] = coy;
            quad.m_iSprite = SpriteIndex::SOLID_COLOR;
            quad.m_ulColor = color;
            m_quads.push_back(quad);
        }

        prevInnerX = innerX; prevInnerY = innerY;
        prevOuterX = outerX; prevOuterY = outerY;
        hasPrevPoint = true;

        const double nextSin = sinA * cosStep + cosA * sinStep;
        const double nextCos = cosA * cosStep - sinA * sinStep;
        sinA = nextSin;
        cosA = nextCos;
    }
}

void BaseHud::addDot(float x, float y, unsigned long color, float size) {
    using namespace PluginConstants;

    SPluginQuad_t quadEntry;

    // Apply offset before setting quad positions
    applyOffset(x, y);

    // Create a small square centered at (x, y)
    // Apply aspect ratio correction to horizontal dimension to maintain square appearance
    float halfSizeX = (size * 0.5f) / UI_ASPECT_RATIO;
    float halfSizeY = size * 0.5f;

    quadEntry.m_aafPos[0][0] = x - halfSizeX;  // Top-left
    quadEntry.m_aafPos[0][1] = y - halfSizeY;
    quadEntry.m_aafPos[1][0] = x - halfSizeX;  // Bottom-left
    quadEntry.m_aafPos[1][1] = y + halfSizeY;
    quadEntry.m_aafPos[2][0] = x + halfSizeX;  // Bottom-right
    quadEntry.m_aafPos[2][1] = y + halfSizeY;
    quadEntry.m_aafPos[3][0] = x + halfSizeX;  // Top-right
    quadEntry.m_aafPos[3][1] = y - halfSizeY;

    quadEntry.m_iSprite = SpriteIndex::SOLID_COLOR;
    quadEntry.m_ulColor = color;

    m_quads.push_back(quadEntry);
}

void BaseHud::addIcon(float x, float y, int spriteIndex, unsigned long color, float size) {
    using namespace PluginConstants;

    SPluginQuad_t quadEntry;

    // Apply offset before setting quad positions
    applyOffset(x, y);

    // Centered square, aspect-corrected so the icon stays round (not stretched).
    float halfSizeX = (size * 0.5f) / UI_ASPECT_RATIO;
    float halfSizeY = size * 0.5f;

    quadEntry.m_aafPos[0][0] = x - halfSizeX;  // Top-left
    quadEntry.m_aafPos[0][1] = y - halfSizeY;
    quadEntry.m_aafPos[1][0] = x - halfSizeX;  // Bottom-left
    quadEntry.m_aafPos[1][1] = y + halfSizeY;
    quadEntry.m_aafPos[2][0] = x + halfSizeX;  // Bottom-right
    quadEntry.m_aafPos[2][1] = y + halfSizeY;
    quadEntry.m_aafPos[3][0] = x + halfSizeX;  // Top-right
    quadEntry.m_aafPos[3][1] = y - halfSizeY;

    quadEntry.m_iSprite = spriteIndex;
    quadEntry.m_ulColor = color;

    m_quads.push_back(quadEntry);
}

void BaseHud::addLineSegment(float x1, float y1, float x2, float y2, unsigned long color, float thickness) {
    using namespace PluginConstants;

    SPluginQuad_t quadEntry;

    // Apply offset
    applyOffset(x1, y1);
    applyOffset(x2, y2);

    // Calculate perpendicular direction for thickness
    float dx = x2 - x1;
    float dy = y2 - y1;
    float len = std::sqrt(dx * dx + dy * dy);

    if (len < 0.0001f) return;  // Skip zero-length segments

    // Perpendicular vector (normalized) - try reversed direction
    float px = dy / len;
    float py = -dx / len;

    // Half thickness offset (apply aspect ratio correction to horizontal component)
    float hx = (px * thickness * 0.5f) / PluginConstants::UI_ASPECT_RATIO;
    float hy = py * thickness * 0.5f;

    // Create rectangle quad (match stick trail pattern exactly: p1+perp, p1-perp, p2-perp, p2+perp)
    quadEntry.m_aafPos[0][0] = x1 + hx;
    quadEntry.m_aafPos[0][1] = y1 + hy;
    quadEntry.m_aafPos[1][0] = x1 - hx;
    quadEntry.m_aafPos[1][1] = y1 - hy;
    quadEntry.m_aafPos[2][0] = x2 - hx;
    quadEntry.m_aafPos[2][1] = y2 - hy;
    quadEntry.m_aafPos[3][0] = x2 + hx;
    quadEntry.m_aafPos[3][1] = y2 + hy;

    quadEntry.m_iSprite = SpriteIndex::SOLID_COLOR;
    quadEntry.m_ulColor = color | 0xFF000000;  // Ensure full alpha

    m_quads.push_back(quadEntry);
}

void BaseHud::addNeedleQuad(float centerX, float centerY, float angleRad,
                            float needleLength, float needleWidth, unsigned long color) {
    using namespace PluginConstants;

    // Create needle as a trapezoid shape (flat tip, wider base)
    // The needle points from center outward in the direction of angleRad
    // Uses clockwise vertex order and applyOffset() on each point individually

    // Calculate tip center (pointing outward)
    float tipCenterX = centerX + std::sin(angleRad) * needleLength / UI_ASPECT_RATIO;
    float tipCenterY = centerY - std::cos(angleRad) * needleLength;

    // Calculate base center (opposite of tip, small distance from center)
    float baseLength = needleLength * 0.15f;  // Base extends 15% of needle length behind center
    float baseCenterX = centerX - std::sin(angleRad) * baseLength / UI_ASPECT_RATIO;
    float baseCenterY = centerY + std::cos(angleRad) * baseLength;

    // Calculate perpendicular direction for width
    float perpAngle = angleRad + Math::PI * 0.5f;  // 90 degrees to the right

    // Tip is narrower (30% of base width) - creates flat but tapered look
    float tipHalfWidth = needleWidth * 0.15f;
    float baseHalfWidth = needleWidth * 0.5f;

    // Calculate tip left and right points
    float tipLeftX = tipCenterX + std::sin(perpAngle) * tipHalfWidth / UI_ASPECT_RATIO;
    float tipLeftY = tipCenterY - std::cos(perpAngle) * tipHalfWidth;
    float tipRightX = tipCenterX - std::sin(perpAngle) * tipHalfWidth / UI_ASPECT_RATIO;
    float tipRightY = tipCenterY + std::cos(perpAngle) * tipHalfWidth;

    // Calculate base left and right points
    float baseLeftX = baseCenterX + std::sin(perpAngle) * baseHalfWidth / UI_ASPECT_RATIO;
    float baseLeftY = baseCenterY - std::cos(perpAngle) * baseHalfWidth;
    float baseRightX = baseCenterX - std::sin(perpAngle) * baseHalfWidth / UI_ASPECT_RATIO;
    float baseRightY = baseCenterY + std::cos(perpAngle) * baseHalfWidth;

    // Apply HUD offset to each point individually (MapHud pattern)
    applyOffset(tipLeftX, tipLeftY);
    applyOffset(tipRightX, tipRightY);
    applyOffset(baseRightX, baseRightY);
    applyOffset(baseLeftX, baseLeftY);

    // Create quad with clockwise vertex order: tipLeft -> tipRight -> baseRight -> baseLeft
    // NOTE: Must use clockwise for proper rendering (counter-clockwise gets face-culled)
    SPluginQuad_t needle;
    needle.m_aafPos[0][0] = tipLeftX;      // Front left
    needle.m_aafPos[0][1] = tipLeftY;
    needle.m_aafPos[1][0] = tipRightX;     // Front right (clockwise)
    needle.m_aafPos[1][1] = tipRightY;
    needle.m_aafPos[2][0] = baseRightX;    // Back right
    needle.m_aafPos[2][1] = baseRightY;
    needle.m_aafPos[3][0] = baseLeftX;     // Back left (completes trapezoid)
    needle.m_aafPos[3][1] = baseLeftY;

    needle.m_iSprite = SpriteIndex::SOLID_COLOR;
    needle.m_ulColor = color;
    m_quads.push_back(needle);
}

void BaseHud::addRotatedSpriteQuad(float screenX, float screenY, float halfSize,
                                   float cosYaw, float sinYaw, int spriteIndex,
                                   unsigned long color) {
    using namespace PluginConstants;

    // Define corner offsets in uniform (square) space for proper rotation
    // TL, BL, BR, TR in local space
    float corners[4][2] = {
        {-halfSize, -halfSize},  // Top-left
        {-halfSize,  halfSize},  // Bottom-left
        { halfSize,  halfSize},  // Bottom-right
        { halfSize, -halfSize}   // Top-right
    };

    // Rotate corners in uniform space, then apply aspect ratio to X
    float rotatedCorners[4][2];
    for (int i = 0; i < 4; i++) {
        float dx = corners[i][0];
        float dy = corners[i][1];
        // Rotate in uniform space
        float rotX = dx * cosYaw - dy * sinYaw;
        float rotY = dx * sinYaw + dy * cosYaw;
        // Apply aspect ratio to X after rotation
        rotatedCorners[i][0] = screenX + rotX / UI_ASPECT_RATIO;
        rotatedCorners[i][1] = screenY + rotY;
        applyOffset(rotatedCorners[i][0], rotatedCorners[i][1]);
    }

    // Create rotated sprite quad
    SPluginQuad_t sprite;
    sprite.m_aafPos[0][0] = rotatedCorners[0][0];  // Top-left
    sprite.m_aafPos[0][1] = rotatedCorners[0][1];
    sprite.m_aafPos[1][0] = rotatedCorners[1][0];  // Bottom-left
    sprite.m_aafPos[1][1] = rotatedCorners[1][1];
    sprite.m_aafPos[2][0] = rotatedCorners[2][0];  // Bottom-right
    sprite.m_aafPos[2][1] = rotatedCorners[2][1];
    sprite.m_aafPos[3][0] = rotatedCorners[3][0];  // Top-right
    sprite.m_aafPos[3][1] = rotatedCorners[3][1];
    sprite.m_iSprite = spriteIndex;
    sprite.m_ulColor = color;
    m_quads.push_back(sprite);
}

unsigned long BaseHud::calculateTemperatureColor(float temp, float optTemp,
                                                 float alarmLow, float alarmHigh) {
    // Temperature color gradient:
    // - Below alarmLow: Deep blue (too cold)
    // - alarmLow to optTemp: Blue -> Green gradient (warming up)
    // - At optTemp: Green (optimal)
    // - optTemp to alarmHigh: Green -> Yellow -> Red gradient (getting hot)
    // - Above alarmHigh: Deep red (too hot)

    // Color constants (RGB values)
    constexpr unsigned char BLUE_R = 0x40, BLUE_G = 0x80, BLUE_B = 0xFF;   // Cold blue
    constexpr unsigned char GREEN_R = 0x40, GREEN_G = 0xFF, GREEN_B = 0x40; // Optimal green
    constexpr unsigned char YELLOW_R = 0xFF, YELLOW_G = 0xD0, YELLOW_B = 0x40; // Warning yellow
    constexpr unsigned char RED_R = 0xFF, RED_G = 0x40, RED_B = 0x40;      // Hot red

    unsigned char r, g, b;

    if (temp <= alarmLow) {
        // Below alarm low - solid blue (too cold)
        r = BLUE_R;
        g = BLUE_G;
        b = BLUE_B;
    } else if (temp < optTemp) {
        // Between alarmLow and optTemp - blue to green gradient
        float range = optTemp - alarmLow;
        float t = (range > 0.0f) ? (temp - alarmLow) / range : 1.0f;
        r = static_cast<unsigned char>(BLUE_R + t * (GREEN_R - BLUE_R));
        g = static_cast<unsigned char>(BLUE_G + t * (GREEN_G - BLUE_G));
        b = static_cast<unsigned char>(BLUE_B + t * (GREEN_B - BLUE_B));
    } else if (temp <= alarmHigh) {
        // Between optTemp and alarmHigh - green to yellow to red gradient
        float range = alarmHigh - optTemp;
        float normalized = (range > 0.0f) ? (temp - optTemp) / range : 0.0f;

        if (normalized < 0.5f) {
            // Green to yellow (first half)
            float t = normalized * 2.0f;
            r = static_cast<unsigned char>(GREEN_R + t * (YELLOW_R - GREEN_R));
            g = static_cast<unsigned char>(GREEN_G + t * (YELLOW_G - GREEN_G));
            b = static_cast<unsigned char>(GREEN_B + t * (YELLOW_B - GREEN_B));
        } else {
            // Yellow to red (second half)
            float t = (normalized - 0.5f) * 2.0f;
            r = static_cast<unsigned char>(YELLOW_R + t * (RED_R - YELLOW_R));
            g = static_cast<unsigned char>(YELLOW_G + t * (RED_G - YELLOW_G));
            b = static_cast<unsigned char>(YELLOW_B + t * (RED_B - YELLOW_B));
        }
    } else {
        // Above alarm high - solid red (too hot)
        r = RED_R;
        g = RED_G;
        b = RED_B;
    }

    return PluginUtils::makeColor(r, g, b);
}

void BaseHud::addHorizontalGridLine(float x, float y, float width, unsigned long color, float thickness) {
    using namespace PluginConstants;

    SPluginQuad_t quadEntry;

    // Apply offset before setting quad positions
    float ox = x, oy = y;
    applyOffset(ox, oy);

    // Use width directly (no aspect ratio correction needed - already in correct coordinate space)
    float halfThickness = thickness * 0.5f;

    quadEntry.m_aafPos[0][0] = ox;                      // Top-left
    quadEntry.m_aafPos[0][1] = oy - halfThickness;
    quadEntry.m_aafPos[1][0] = ox;                      // Bottom-left
    quadEntry.m_aafPos[1][1] = oy + halfThickness;
    quadEntry.m_aafPos[2][0] = ox + width;              // Bottom-right
    quadEntry.m_aafPos[2][1] = oy + halfThickness;
    quadEntry.m_aafPos[3][0] = ox + width;              // Top-right
    quadEntry.m_aafPos[3][1] = oy - halfThickness;

    quadEntry.m_iSprite = SpriteIndex::SOLID_COLOR;
    quadEntry.m_ulColor = color;

    m_quads.push_back(quadEntry);
}

void BaseHud::addStripChartFrame(float x, float y, float width, float height,
                                 const char* topLabel, const char* midLabel, const char* botLabel,
                                 const ScaledDimensions& dims) {
    // Grid lines at 100%/50%/0% of the value range, drawn before the traces so
    // the data renders on top.
    const unsigned long gridColor = this->getColor(ColorSlot::MUTED);  // Muted gray for subtle grid lines
    const float gridLineThickness = stripChartGridThickness();
    static constexpr float GRID_FRACTIONS[] = { 1.0f, 0.5f, 0.0f };
    for (float fraction : GRID_FRACTIONS) {
        float gridY = y + height - (fraction * height);
        addHorizontalGridLine(x, gridY, width, gridColor, gridLineThickness);
    }

    // Axis labels down the left edge (top / middle / bottom), matching the grid lines.
    const float labelX = x + dims.paddingH * STRIP_CHART_LABEL_INSET;
    const unsigned long labelColor = this->getColor(ColorSlot::TERTIARY);
    const int labelFont = this->getFont(FontCategory::SMALL);
    addString(topLabel, labelX, y, PluginConstants::Justify::LEFT, labelFont, labelColor, dims.fontSizeSmall);
    addString(midLabel, labelX, y + height * 0.5f, PluginConstants::Justify::LEFT, labelFont, labelColor, dims.fontSizeSmall);
    addString(botLabel, labelX, y + height - dims.lineHeightSmall, PluginConstants::Justify::LEFT, labelFont, labelColor, dims.fontSizeSmall);
}


void BaseHud::setQuadPositions(SPluginQuad_t& quad, float x, float y, float width, float height) {
    quad.m_aafPos[0][0] = x;
    quad.m_aafPos[0][1] = y;
    quad.m_aafPos[1][0] = x;
    quad.m_aafPos[1][1] = y + height;
    quad.m_aafPos[2][0] = x + width;
    quad.m_aafPos[2][1] = y + height;
    quad.m_aafPos[3][0] = x + width;
    quad.m_aafPos[3][1] = y;
}

void BaseHud::setQuadPositionsArrowRight(SPluginQuad_t& quad, float x, float y,
                                         float width, float height) {
    // Same winding as setQuadPositions (TL, BL, then the right-hand pair), with the
    // right edge collapsed to one point so the quad renders as a triangle.
    const float tipY = y + height * 0.5f;
    quad.m_aafPos[0][0] = x;         quad.m_aafPos[0][1] = y;
    quad.m_aafPos[1][0] = x;         quad.m_aafPos[1][1] = y + height;
    quad.m_aafPos[2][0] = x + width; quad.m_aafPos[2][1] = tipY;
    quad.m_aafPos[3][0] = x + width; quad.m_aafPos[3][1] = tipY;
}

void BaseHud::setQuadPositionsRotatedCW(SPluginQuad_t& quad, float x, float y,
                                        float width, float height) {
    // Vertex order is fixed by the sprite's own corners -- 0 = its top-left, 1 =
    // bottom-left, 2 = bottom-right, 3 = top-right. Turning the picture clockwise
    // therefore means handing vertex 0 the rect's TOP-RIGHT and walking the rest
    // round from there. The apex of an up-caret (the midpoint of vertices 0 and 3)
    // lands on the rect's right edge, mid-height.
    quad.m_aafPos[0][0] = x + width; quad.m_aafPos[0][1] = y;
    quad.m_aafPos[1][0] = x;         quad.m_aafPos[1][1] = y;
    quad.m_aafPos[2][0] = x;         quad.m_aafPos[2][1] = y + height;
    quad.m_aafPos[3][0] = x + width; quad.m_aafPos[3][1] = y + height;
}

void BaseHud::updateBackgroundQuadPosition(float startX, float startY, float width, float height) {
    if (m_quads.empty()) return;

    float x = startX;
    float y = startY;
    applyOffset(x, y);

    // A themed background occupies 9 quads, not 1. Rewrite the whole recorded span
    // -- moving only m_quads[0] would slide the centre slice out from under a
    // stationary frame. The span is re-validated because a HUD may have rebuilt
    // with a different theme state since it was recorded.
    // >= SLICE_COUNT, not ==: a themed panel also carries the reserved fill strips (see
    // finalizeThemedFill). Testing for exactly nine sent every themed drag down the FLAT
    // path below, which rewrites the centre slice as a full-panel quad -- the frame
    // stays put and the fill swallows it.
    if (m_bgQuadCount >= NineSlice::SLICE_COUNT && m_bgQuadFirst >= 0 &&
        static_cast<size_t>(m_bgQuadFirst) + m_bgQuadCount <= m_quads.size()) {
        if (const ThemeAsset* theme = activeTheme()) {
            // The covering rects were recorded in the OLD offset's space; the panel is
            // moving, so they move with it and the strips are re-cut against them.
            // BOTH deltas read BEFORE m_bgRect* is overwritten below -- against the
            // new rect they are identically zero, which is a silent no-op rather than
            // a compile error.
            const float dx = x - m_bgRectX;
            const float dy = y - m_bgRectY;
            emitThemedBackground(*theme, x, y, width, height, m_bgQuadFirst);
            m_bgRectX = x; m_bgRectY = y; m_bgRectW = width; m_bgRectH = height;
            m_bandLeft += dx;     m_bandRight += dx;
            m_bandTop += dy;      m_bandBottom += dy;
            m_wholeCardTop += dy; m_wholeCardBottom += dy;
            m_fillFirst = m_bgQuadFirst;   // re-arm: finalize consumes it (see there)
            finalizeThemedFill();
            return;
        }
    }

    applyTextureAspectCorrection(x, y, width, height);

    // Same bounds check the themed branch above does. This one tested only
    // `> 0` -- reachable if a rebuild skips addBackgroundQuad while a stale index
    // survives, which is exactly the shape the themed branch already guards. Two
    // branches of one function disagreeing about whether the index is trustworthy
    // is how the trustworthy one ends up wrong.
    // A panel whose background was never emitted (zero opacity) has no quad here to
    // move -- and m_bgQuadFirst points at the first CONTENT quad, so rewriting it
    // would stretch a row or an icon across the whole panel on the first drag.
    if (m_bgQuadCount <= 0) {
        m_bgRectX = x; m_bgRectY = y; m_bgRectW = width; m_bgRectH = height;
        return;
    }
    const size_t bgIndex = (m_bgQuadFirst > 0) ? static_cast<size_t>(m_bgQuadFirst) : 0;
    if (bgIndex >= m_quads.size()) return;
    setQuadPositions(m_quads[bgIndex], x, y, width, height);
}

void BaseHud::applyTextureAspectCorrection(float& x, float& y, float& width, float& height) const {
    using namespace PluginConstants;

    if (!m_bShowBackgroundTexture || m_iBackgroundTextureIndex <= 0) return;

    float textureAspect = AssetManager::getInstance().getTextureAspectRatio(m_iBackgroundTextureIndex);
    if (textureAspect <= 0.0f) return;

    // Convert content dimensions to pixel-space aspect ratio
    // In normalized 16:9 coords: pixel_width = w * 16, pixel_height = h * 9
    // So content pixel aspect = (width * 16) / (height * 9) = width * UI_ASPECT_RATIO / height
    float contentAspect = (height > 0.0001f) ? (width * UI_ASPECT_RATIO / height) : textureAspect;

    if (contentAspect < textureAspect) {
        // Content is taller than texture - expand width to match texture aspect
        float newWidth = height * textureAspect / UI_ASPECT_RATIO;
        x -= (newWidth - width) * 0.5f;  // Center horizontally
        width = newWidth;
    } else if (contentAspect > textureAspect) {
        // Content is wider than texture - expand height to match texture aspect
        float newHeight = width * UI_ASPECT_RATIO / textureAspect;
        y -= (newHeight - height) * 0.5f;  // Center vertically
        height = newHeight;
    }
}

BaseHud::ScaledDimensions BaseHud::getScaledDimensions() const {
    MXB_COUNT_CALL(GET_SCALED_DIMENSIONS);
    // THE migration point. Nearly every HUD lays out through this struct rather
    // than reaching for the constants itself, so pointing it at layout() is what
    // makes the whole UI follow a theme's spacing -- without touching the HUDs.
    const LayoutMetrics& L = layout();
    return {
        L.fontSizeNormal * m_fScale,
        L.fontSizeExtraSmall * m_fScale,
        L.fontSizeSmall * m_fScale,
        L.fontSizeLarge * m_fScale,
        L.fontSizeExtraLarge * m_fScale,
        // Theme-aware: a themed HUD's content is pushed in far enough to clear the
        // frame's edge slices AND the edge slice of the title band wrapped around it.
        // Applied HERE rather than at each title/row site so the full rebuild and
        // every HUD's rebuildLayout fast path pick it up identically -- an indent
        // applied only at caption time would be lost the moment the HUD was dragged.
        contentPaddingX(),
        contentPaddingY(),
        L.lineHeightExtraSmall * m_fScale,
        L.lineHeightSmall * m_fScale,
        L.lineHeightLarge * m_fScale,
        L.lineHeightNormal * m_fScale,
        L.lineHeightExtraLarge * m_fScale,
        L.cellW * m_fScale,
        L.cellH * m_fScale,
        m_fScale
    };
}

unsigned long BaseHud::getTextColorWithOpacity(uint8_t r, uint8_t g, uint8_t b) const {
    uint8_t alpha = static_cast<uint8_t>(m_fBackgroundOpacity * 255.0f);
    return PluginUtils::makeColor(r, g, b, alpha);
}

float BaseHud::calculateBackgroundWidth(int charWidth) const {
    auto dim = getScaledDimensions();
    return PluginUtils::calculateMonospaceTextWidth(charWidth, dim.fontSize)
        + dim.paddingH + dim.paddingH;
}

float BaseHud::calculateBackgroundHeight(int rowCount, bool includeTitle) const {
    auto dim = getScaledDimensions();
    // titleRowHeight(), not a bare lineHeightLarge. Both answer "how tall is a title
    // row", and having two answers is how a HUD sized through this helper could tile
    // differently from one that reserved its row directly -- the exact class of bug
    // check_hud_helpers.sh rule 7 exists for. Identical at the shipped metrics (the
    // band is 0.042 against a 0.047 large row, so the max() picks the row either way);
    // the point is that it stays identical when the band grows.
    float titleHeight = (includeTitle && m_bShowTitle)
        ? titleRowHeight(dim.fontSizeLarge, dim.lineHeightLarge) : 0.0f;
    return panelHeight(dim, titleHeight + (rowCount * dim.lineHeightNormal));
}

bool BaseHud::positionString(size_t stringIndex, float x, float y) {
    if (stringIndex >= m_strings.size()) {
        return false;
    }
    applyOffset(x, y);
    m_strings[stringIndex].m_afPos[0] = x;
    // The SAME centring addString applied when this string was built. The reposition
    // fast paths take the string's own size from the entry rather than being told it,
    // so a caller cannot pass one that disagrees with what is on screen.
    m_strings[stringIndex].m_afPos[1] = y + rowCenterOffset(m_strings[stringIndex].m_fSize);
    return true;
}

// ============================================================================
// Styled String Rendering (per-string padding and backgrounds)
// ============================================================================

void BaseHud::addStyledString(const HudStringConfig& config) {
    m_styledStringConfigs.push_back(config);
}

void BaseHud::renderStyledStrings() {
    using namespace PluginConstants;

    for (const auto& config : m_styledStringConfigs) {
        // Use cached text width if available (PERFORMANCE OPTIMIZATION)
        float textWidth = (config.cachedTextWidth > 0.0f)
            ? config.cachedTextWidth
            : PluginUtils::calculateMonospaceTextWidth(static_cast<int>(config.text.length()), config.fontSize);
        float lineHeight = floatEquals(config.fontSize, layoutDefaults().fontSizeLarge * m_fScale)
                          ? layoutDefaults().lineHeightLarge * m_fScale
                          : layoutDefaults().lineHeightNormal * m_fScale;

        // Add background quad if requested
        if (config.hasBackground) {
            float bgX = config.x - config.bgPaddingLeft;
            float bgY = config.y - config.bgPaddingTop;
            float bgWidth = textWidth + config.bgPaddingLeft + config.bgPaddingRight;
            float bgHeight = lineHeight + config.bgPaddingTop + config.bgPaddingBottom;

            SPluginQuad_t quadEntry;
            applyOffset(bgX, bgY);
            setQuadPositions(quadEntry, bgX, bgY, bgWidth, bgHeight);
            quadEntry.m_iSprite = SpriteIndex::SOLID_COLOR;

            // Use the per-string background color and opacity
            uint8_t alpha = static_cast<uint8_t>(config.backgroundOpacity * 255.0f);
            uint8_t r = (config.backgroundColor >> 16) & 0xFF;
            uint8_t g = (config.backgroundColor >> 8) & 0xFF;
            uint8_t b = config.backgroundColor & 0xFF;
            quadEntry.m_ulColor = PluginUtils::makeColor(r, g, b, alpha);

            m_quads.push_back(quadEntry);
        }

        // Add the text string
        addString(config.text.c_str(), config.x, config.y, config.justify,
                 config.fontIndex, config.color, config.fontSize);
    }
}

BaseHud::StyledStringBounds BaseHud::calculateStyledStringBounds() const {
    using namespace PluginConstants;

    if (m_styledStringConfigs.empty()) {
        return {0.0f, 0.0f, 0.0f, 0.0f};
    }

    float minX = 1e10f;  // Large positive value
    float minY = 1e10f;
    float maxX = -1e10f; // Large negative value
    float maxY = -1e10f;

    for (const auto& config : m_styledStringConfigs) {
        // Use cached text width if available (PERFORMANCE OPTIMIZATION)
        float textWidth = (config.cachedTextWidth > 0.0f)
            ? config.cachedTextWidth
            : PluginUtils::calculateMonospaceTextWidth(static_cast<int>(config.text.length()), config.fontSize);
        float lineHeight = floatEquals(config.fontSize, layoutDefaults().fontSizeLarge * m_fScale)
                          ? layoutDefaults().lineHeightLarge * m_fScale
                          : layoutDefaults().lineHeightNormal * m_fScale;

        // Calculate bounds including layout padding
        float left = config.x - config.paddingLeft;
        float right = config.x + textWidth + config.paddingRight;
        float top = config.y - config.paddingTop;
        float bottom = config.y + lineHeight + config.paddingBottom;

        // Update min/max using ternary operators (avoids Windows macro conflicts)
        minX = (left < minX) ? left : minX;
        maxX = (right > maxX) ? right : maxX;
        minY = (top < minY) ? top : minY;
        maxY = (bottom > maxY) ? bottom : maxY;
    }

    return {minX, minY, maxX, maxY};
}
