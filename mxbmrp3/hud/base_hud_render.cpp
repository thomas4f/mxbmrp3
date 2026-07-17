// ============================================================================
// hud/base_hud_render.cpp
// BaseHud shared rendering primitives — the drawing helpers every HUD builds on:
// strings/labels, background quads, dots, icons, line segments, needle quads,
// grid lines, strip charts, temperature colour, and the styled-string builder.
// Extracted verbatim from base_hud.cpp when it grew past ~1k lines; the BaseHud
// class, members, and public API are unchanged — only where these method bodies
// (and the file-local floatEquals helper they use) live moves. Same byte-
// identical-extraction pattern as the plugin_data / http_server splits.
// ============================================================================
#include "base_hud.h"
#include "../core/plugin_constants.h"
#include "../core/plugin_manager.h"
#include "../core/plugin_utils.h"
#include "../core/color_config.h"
#include "../core/font_config.h"
#include "../core/ui_config.h"
#include "../core/settings_manager.h"
#include "../core/hud_manager.h"
#include "../core/asset_manager.h"
#include "../core/companion_window.h"
#include "../diagnostics/logger.h"
#include "../handlers/draw_handler.h"
#include "../diagnostics/timer.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
    // Epsilon comparison for floating-point values
    // Required to avoid precision issues when comparing scaled font sizes
    constexpr float FLOAT_EPSILON = 0.0001f;

    inline bool floatEquals(float a, float b) {
        return std::abs(a - b) < FLOAT_EPSILON;
    }
}

// ============================================================================
// Shared HUD Rendering Helpers (eliminates duplication across HUDs)
// ============================================================================

// Emits one render string. IMPORTANT: this always pushes an entry, even when `text` is
// empty — it never skips. Index-coordinated layout fast paths (e.g. StandingsHud /
// IdealLapHud / StatsHud rebuildLayout) reposition strings by index and assume a stable
// per-row string count/order across rebuilds. Callers therefore pass "" for a blank cell
// rather than skipping the call; don't "optimize" empties away here or the indices desync
// and rows scramble on drag/scale.
void BaseHud::addString(const char* text, float x, float y, int justify, int fontIndex,
                        unsigned long color, float fontSize, bool skipShadow) {
    SPluginString_t stringEntry;

    strncpy_s(stringEntry.m_szString, sizeof(stringEntry.m_szString), text, sizeof(stringEntry.m_szString) - 1);
    stringEntry.m_szString[sizeof(stringEntry.m_szString) - 1] = '\0';

    applyOffset(x, y);
    stringEntry.m_afPos[0] = x;
    stringEntry.m_afPos[1] = y;
    stringEntry.m_iFont = fontIndex;
    stringEntry.m_fSize = fontSize;
    stringEntry.m_iJustify = justify;
    stringEntry.m_ulColor = color;

    m_strings.push_back(stringEntry);
    m_stringSkipShadow.push_back(skipShadow);  // Track shadow flag (shadow generated at collection time)
}

void BaseHud::addTitleString(const char* text, float x, float y, int justify, int fontIndex,
                             unsigned long color, float fontSize) {
    // Reset per-rebuild title-icon tracking; re-established below when an icon is emitted.
    m_titleStringIndex = -1;
    m_titleIconQuadIndex = -1;

    // Always add a string to keep indices consistent, but use empty string if title is hidden
    if (!m_bShowTitle) {
        addString("", x, y, justify, fontIndex, color, fontSize);
        return;
    }

    // Optional HUD identity icon to the left of the title text. Only for left-justified
    // titles (the standard for every HUD) so centered/right layouts are left undisturbed.
    // Falls back to plain text when the global setting is off or no icon is assigned.
    int spriteIndex = (UiConfig::getInstance().getTitleIcons() && justify == PluginConstants::Justify::LEFT)
        ? AssetManager::getInstance().getIconSpriteIndex(getIconName()) : 0;

    if (spriteIndex <= 0) {
        addString(text, x, y, justify, fontIndex, color, fontSize);
        return;
    }

    // Emit the icon quad (position fixed up by finalizeTitleIcon) then the un-advanced
    // title string, and record their indices so the layout fast path can keep them in
    // sync during drag/scale.
    // Icons fill their glyph box more than text fills the em, so draw the icon a bit
    // smaller than the title font (~12px at 1080p) while still centring it on the full
    // title-font height (see finalizeTitleIcon).
    m_titleFontSize = fontSize;
    m_titleIconSize = fontSize * PluginConstants::TITLE_ICON_SCALE;
    m_titleIconQuadIndex = static_cast<int>(m_quads.size());
    addIcon(x, y, spriteIndex, color, m_titleIconSize);  // placeholder position (corners reset below)
    m_titleStringIndex = static_cast<int>(m_strings.size());
    addString(text, x, y, justify, fontIndex, color, fontSize);  // un-advanced

    // Place the icon and advance the title text using the offset-applied position that
    // addString just stored (so the result matches the layout fast path exactly).
    finalizeTitleIcon(m_strings[m_titleStringIndex].m_afPos[0], m_strings[m_titleStringIndex].m_afPos[1]);
}

// Places the title icon quad with its left edge at (baseX) and centered on the title glyph,
// then advances the title text right past the icon. baseX/baseY are the offset-applied,
// un-advanced title position. No-op when there is no title icon.
void BaseHud::finalizeTitleIcon(float baseX, float baseY) {
    if (m_titleIconQuadIndex < 0 || m_titleStringIndex < 0) return;
    if (m_titleIconQuadIndex >= static_cast<int>(m_quads.size())) return;
    if (m_titleStringIndex >= static_cast<int>(m_strings.size())) return;

    float size = m_titleIconSize;
    float halfX = (size * 0.5f) / PluginConstants::UI_ASPECT_RATIO;
    float halfY = size * 0.5f;
    // Icon left edge at baseX; vertically centered on the full title-font height (not
    // the icon's own height) so a smaller icon still sits centred on the title text.
    float cx = baseX + halfX;
    float cy = baseY + m_titleFontSize * 0.5f;

    SPluginQuad_t& q = m_quads[m_titleIconQuadIndex];
    q.m_aafPos[0][0] = cx - halfX; q.m_aafPos[0][1] = cy - halfY;  // top-left
    q.m_aafPos[1][0] = cx - halfX; q.m_aafPos[1][1] = cy + halfY;  // bottom-left
    q.m_aafPos[2][0] = cx + halfX; q.m_aafPos[2][1] = cy + halfY;  // bottom-right
    q.m_aafPos[3][0] = cx + halfX; q.m_aafPos[3][1] = cy - halfY;  // top-right

    // Advance the title text past the icon plus a small gap.
    float advance = size / PluginConstants::UI_ASPECT_RATIO + size * 0.35f;
    m_strings[m_titleStringIndex].m_afPos[0] = baseX + advance;
}

// Re-derives the title icon position from the title string's current position. Safe to call
// after any rebuild and idempotent: it places the icon only when the title is NOT already
// advanced past it. This means a full rebuild (which self-places via addTitleString) and a
// layout fast path that *doesn't* reposition the title (e.g. an early-returning rebuildLayout
// on an empty HUD) are both left untouched, while a fast path that DID move the title to its
// un-advanced base gets the icon + advance re-applied.
void BaseHud::positionTitleIcon() {
    if (m_titleIconQuadIndex < 0 || m_titleStringIndex < 0) return;
    if (m_titleIconQuadIndex >= static_cast<int>(m_quads.size())) return;
    if (m_titleStringIndex >= static_cast<int>(m_strings.size())) return;

    float advance = m_titleIconSize / PluginConstants::UI_ASPECT_RATIO + m_titleIconSize * 0.35f;
    float titleX = m_strings[m_titleStringIndex].m_afPos[0];
    float iconLeft = m_quads[m_titleIconQuadIndex].m_aafPos[0][0];
    // Invariant when correctly placed: icon's left edge sits one advance left of the title.
    if (std::fabs(iconLeft - (titleX - advance)) < 1.0e-5f) return;

    // Title is at a fresh un-advanced base; place the icon there and advance the title.
    finalizeTitleIcon(titleX, m_strings[m_titleStringIndex].m_afPos[1]);
}

void BaseHud::addBackgroundQuad(float x, float y, float width, float height) {
    using namespace PluginConstants;

    // Always add quad to keep indices consistent, but use transparent color if hidden
    SPluginQuad_t quadEntry;

    applyOffset(x, y);

    // Check if background texture should be used
    if (m_bShowBackgroundTexture && m_iBackgroundTextureIndex > 0) {
        applyTextureAspectCorrection(x, y, width, height);
        setQuadPositions(quadEntry, x, y, width, height);

        // Use sprite texture for background
        quadEntry.m_iSprite = m_iBackgroundTextureIndex;
        // White color with opacity to allow texture to show through
        quadEntry.m_ulColor = PluginUtils::applyOpacity(ColorPalette::WHITE, m_fBackgroundOpacity);
    } else {
        setQuadPositions(quadEntry, x, y, width, height);

        // Use solid color background
        quadEntry.m_iSprite = SpriteIndex::SOLID_COLOR;
        // Get configured background color and apply opacity (uses per-HUD override if set)
        unsigned long bgColor = this->getColor(ColorSlot::BACKGROUND);
        quadEntry.m_ulColor = PluginUtils::applyOpacity(bgColor, m_fBackgroundOpacity);
    }

    m_quads.push_back(quadEntry);
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

void BaseHud::addStripChartHistoryLine(const std::deque<float>& history, unsigned long color,
                                       float x, float y, float width, float height,
                                       float lineThickness, size_t maxHistory) {
    if (history.size() < 2) return;

    // Calculate point spacing based on max history size for consistent graph width
    float pointSpacing = width / (maxHistory - 1);

    // Offset so newest data is always at right edge (scrolls left as data fills in)
    size_t offset = maxHistory - history.size();

    // Draw line segments connecting consecutive points
    for (size_t i = 0; i < history.size() - 1; ++i) {
        float value1 = std::max(0.0f, std::min(1.0f, history[i]));
        float value2 = std::max(0.0f, std::min(1.0f, history[i + 1]));

        // Skip segments where both values are near zero
        if (value1 < 0.01f && value2 < 0.01f) continue;

        float x1 = x + (offset + i) * pointSpacing;
        float x2 = x + (offset + i + 1) * pointSpacing;
        float y1 = y + height - (value1 * height);
        float y2 = y + height - (value2 * height);

        addLineSegment(x1, y1, x2, y2, color, lineThickness);
    }
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

void BaseHud::updateBackgroundQuadPosition(float startX, float startY, float width, float height) {
    if (!m_quads.empty()) {
        float x = startX;
        float y = startY;
        applyOffset(x, y);

        applyTextureAspectCorrection(x, y, width, height);

        setQuadPositions(m_quads[0], x, y, width, height);
    }
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
    using namespace PluginConstants;
    return {
        FontSizes::NORMAL * m_fScale,
        FontSizes::EXTRA_SMALL * m_fScale,
        FontSizes::SMALL * m_fScale,
        FontSizes::LARGE * m_fScale,
        FontSizes::EXTRA_LARGE * m_fScale,
        Padding::HUD_HORIZONTAL * m_fScale,
        Padding::HUD_VERTICAL * m_fScale,
        LineHeights::EXTRA_SMALL * m_fScale,
        LineHeights::SMALL * m_fScale,
        LineHeights::LARGE * m_fScale,
        LineHeights::NORMAL * m_fScale,
        LineHeights::EXTRA_LARGE * m_fScale,
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
    float titleHeight = (includeTitle && m_bShowTitle) ? dim.lineHeightLarge : 0.0f;
    return dim.paddingV + titleHeight + (rowCount * dim.lineHeightNormal) + dim.paddingV;
}

bool BaseHud::positionString(size_t stringIndex, float x, float y) {
    if (stringIndex >= m_strings.size()) {
        return false;
    }
    applyOffset(x, y);
    m_strings[stringIndex].m_afPos[0] = x;
    m_strings[stringIndex].m_afPos[1] = y;
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
        float lineHeight = floatEquals(config.fontSize, FontSizes::LARGE * m_fScale)
                          ? LineHeights::LARGE * m_fScale
                          : LineHeights::NORMAL * m_fScale;

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
        float lineHeight = floatEquals(config.fontSize, FontSizes::LARGE * m_fScale)
                          ? LineHeights::LARGE * m_fScale
                          : LineHeights::NORMAL * m_fScale;

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
