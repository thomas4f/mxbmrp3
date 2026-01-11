// ============================================================================
// hud/base_hud.cpp
// Base class for all HUD display elements with common rendering and positioning logic
// ============================================================================
#include "base_hud.h"
#include "../core/plugin_constants.h"
#include "../core/plugin_manager.h"
#include "../core/plugin_utils.h"
#include "../core/color_config.h"
#include "../core/settings_manager.h"
#include "../core/hud_manager.h"
#include "../core/asset_manager.h"
#include "../diagnostics/logger.h"
#include "../diagnostics/timer.h"
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

bool BaseHud::handleMouseInput(bool allowInput) {
    if (!m_bDraggable) return false;

    const InputManager& input = InputManager::getInstance();

    // Only process if cursor is enabled
    if (!input.isCursorEnabled()) {
        if (m_bDragging) {
            m_bDragging = false;
            DEBUG_INFO("Drag cancelled - cursor disabled");
        }
        return false;
    }

    // If input is not allowed (another HUD is being dragged), skip input processing
    if (!allowInput) {
        return m_bDragging;  // Return current drag state but don't process new input
    }

    const MouseButton& rightButton = input.getRightButton();
    const CursorPosition& cursor = input.getCursorPosition();

    // Start dragging on RMB click within bounds
    if (rightButton.isClicked() && cursor.isValid && !m_bDragging) {
        if (isPointInBounds(cursor.x, cursor.y)) {
            m_bDragging = true;
            m_fDragStartX = cursor.x;
            m_fDragStartY = cursor.y;
            m_fInitialOffsetX = m_fOffsetX;
            m_fInitialOffsetY = m_fOffsetY;
            DEBUG_INFO_F("Started dragging HUD (RMB) at cursor position: (%.3f, %.3f)",
                cursor.x, cursor.y);
        }
    }

    // Update position while dragging
    if (m_bDragging && rightButton.isPressed && cursor.isValid) {
        float deltaX = cursor.x - m_fDragStartX;
        float deltaY = cursor.y - m_fDragStartY;

        float newOffsetX = m_fInitialOffsetX + deltaX;
        float newOffsetY = m_fInitialOffsetY + deltaY;

        // Get actual window bounds and clamp position
        const WindowBounds& windowBounds = input.getWindowBounds();
        clampPositionToBounds(newOffsetX, newOffsetY, windowBounds);

        // Snap to grid if enabled (use separate horizontal/vertical grids for perfect alignment)
        if (ColorConfig::getInstance().getGridSnapping()) {
            newOffsetX = PluginConstants::HudGrid::SNAP_TO_GRID_X(newOffsetX);
            newOffsetY = PluginConstants::HudGrid::SNAP_TO_GRID_Y(newOffsetY);

            // Edge magnetism: snap to window edges if within one grid cell
            // This allows HUDs to be positioned flush against screen borders
            const float gridH = PluginConstants::HudGrid::GRID_SIZE_HORIZONTAL;
            const float gridV = PluginConstants::HudGrid::GRID_SIZE_VERTICAL;

            // Calculate where HUD edges would be with current offset
            float hudLeft = m_fBoundsLeft + newOffsetX;
            float hudRight = m_fBoundsRight + newOffsetX;
            float hudTop = m_fBoundsTop + newOffsetY;
            float hudBottom = m_fBoundsBottom + newOffsetY;

            // Snap to left/right edge if close
            if (std::abs(hudLeft - windowBounds.left) < gridH) {
                newOffsetX = windowBounds.left - m_fBoundsLeft;
            }
            else if (std::abs(hudRight - windowBounds.right) < gridH) {
                newOffsetX = windowBounds.right - m_fBoundsRight;
            }

            // Snap to top/bottom edge if close
            if (std::abs(hudTop - windowBounds.top) < gridV) {
                newOffsetY = windowBounds.top - m_fBoundsTop;
            }
            else if (std::abs(hudBottom - windowBounds.bottom) < gridV) {
                newOffsetY = windowBounds.bottom - m_fBoundsBottom;
            }
        }

        // Update position if changed
        if (m_fOffsetX != newOffsetX || m_fOffsetY != newOffsetY) {
            m_fOffsetX = newOffsetX;
            m_fOffsetY = newOffsetY;
            setLayoutDirty();  // Only layout dirty, not data
        }
    }

    // Stop dragging on RMB release
    if (m_bDragging && rightButton.isReleased()) {
        m_bDragging = false;
        DEBUG_INFO_F("Stopped dragging HUD at position offset: (%.3f, %.3f)",
            m_fOffsetX, m_fOffsetY);

        // Save settings immediately after dragging ends
        SettingsManager::getInstance().saveSettings(HudManager::getInstance(),
                                                     PluginManager::getInstance().getSavePath());
    }

    // Return true if we're currently dragging (tells HudManager to stop processing other HUDs)
    return m_bDragging;
}

void BaseHud::validatePosition() {
    // If HUD is dirty (e.g., scale was just changed), update bounds before validating
    // This ensures we validate against the correct scaled dimensions
    if (isDataDirty() || isLayoutDirty()) {
        update();
    }

    const WindowBounds& windowBounds = InputManager::getInstance().getWindowBounds();

    // Use helper to clamp position to window bounds
    if (clampPositionToBounds(m_fOffsetX, m_fOffsetY, windowBounds)) {
        setLayoutDirty();  // Only layout dirty, not data
        DEBUG_INFO_F("HUD position adjusted to fit window bounds: (%.3f, %.3f)",
            m_fOffsetX, m_fOffsetY);
    }
}

bool BaseHud::checkFrequentUpdates() {
    if (!needsFrequentUpdates()) {
        return false;
    }

    auto now = std::chrono::steady_clock::now();
    auto sinceLastTick = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - m_lastTickUpdate
    ).count();

    if (sinceLastTick >= TICK_UPDATE_INTERVAL_MS) {
        m_lastTickUpdate = now;
        setDataDirty();
        return true;
    }

    return false;
}

void BaseHud::setBounds(float left, float top, float right, float bottom) {
    m_fBoundsLeft = left;
    m_fBoundsTop = top;
    m_fBoundsRight = right;
    m_fBoundsBottom = bottom;
}

bool BaseHud::isPointInBounds(float x, float y) const {
    // Apply current offset to bounds for hit testing
    float boundsLeft = m_fBoundsLeft + m_fOffsetX;
    float boundsTop = m_fBoundsTop + m_fOffsetY;
    float boundsRight = m_fBoundsRight + m_fOffsetX;
    float boundsBottom = m_fBoundsBottom + m_fOffsetY;

    return (x >= boundsLeft && x <= boundsRight && y >= boundsTop && y <= boundsBottom);
}

bool BaseHud::clampPositionToBounds(float& offsetX, float& offsetY, const WindowBounds& windowBounds) const {
    // Calculate HUD edges in screen space with proposed offset
    float hudLeft = m_fBoundsLeft + offsetX;
    float hudRight = m_fBoundsRight + offsetX;
    float hudTop = m_fBoundsTop + offsetY;
    float hudBottom = m_fBoundsBottom + offsetY;

    bool needsAdjustment = false;

    // Clamp horizontally to keep HUD within window bounds
    if (hudLeft < windowBounds.left) {
        offsetX = windowBounds.left - m_fBoundsLeft;
        needsAdjustment = true;
    }
    else if (hudRight > windowBounds.right) {
        offsetX = windowBounds.right - m_fBoundsRight;
        needsAdjustment = true;
    }

    // Clamp vertically to keep HUD within window bounds
    if (hudTop < windowBounds.top) {
        offsetY = windowBounds.top - m_fBoundsTop;
        needsAdjustment = true;
    }
    else if (hudBottom > windowBounds.bottom) {
        offsetY = windowBounds.bottom - m_fBoundsBottom;
        needsAdjustment = true;
    }

    return needsAdjustment;
}

void BaseHud::processDirtyFlags() {
    if (isDataDirty()) {
        rebuildRenderData();
        onAfterDataRebuild();
        clearDataDirty();
        clearLayoutDirty();
    }
    else if (isLayoutDirty()) {
        rebuildLayout();
        clearLayoutDirty();
    }
}

// ============================================================================
// Dynamic Texture Variant Support
// ============================================================================

void BaseHud::setTextureBaseName(const std::string& baseName) {
    m_textureBaseName = baseName;

    // If variant is set, update the background texture index
    if (m_textureVariant > 0) {
        int spriteIndex = AssetManager::getInstance().getSpriteIndex(baseName, m_textureVariant);
        if (spriteIndex > 0) {
            m_iBackgroundTextureIndex = spriteIndex;
        }
    }
}

void BaseHud::setTextureVariant(int variant) {
    if (variant < 0) variant = 0;

    if (m_textureVariant != variant) {
        m_textureVariant = variant;

        // Update background texture index based on variant
        if (variant == 0) {
            // Variant 0 = Off (solid color background)
            m_bShowBackgroundTexture = false;
        } else if (!m_textureBaseName.empty()) {
            int spriteIndex = AssetManager::getInstance().getSpriteIndex(m_textureBaseName, variant);
            if (spriteIndex > 0) {
                m_iBackgroundTextureIndex = spriteIndex;
                m_bShowBackgroundTexture = true;
            } else {
                // Variant not found, fall back to solid color
                m_bShowBackgroundTexture = false;
                DEBUG_WARN_F("Texture variant %d not found for %s", variant, m_textureBaseName.c_str());
            }
        }

        setDataDirty();
    }
}

void BaseHud::cycleTextureVariant(bool forward) {
    if (m_textureBaseName.empty()) {
        return;
    }

    std::vector<int> variants = getAvailableTextureVariants();
    if (variants.empty()) {
        return;
    }

    // Build cycle order: 0 (Off), then all variants
    std::vector<int> cycleOrder = {0};
    cycleOrder.insert(cycleOrder.end(), variants.begin(), variants.end());

    // Find current position in cycle
    int currentIndex = 0;
    for (size_t i = 0; i < cycleOrder.size(); ++i) {
        if (cycleOrder[i] == m_textureVariant) {
            currentIndex = static_cast<int>(i);
            break;
        }
    }

    // Calculate next position
    int cycleSize = static_cast<int>(cycleOrder.size());
    int newIndex;
    if (forward) {
        newIndex = (currentIndex + 1) % cycleSize;
    } else {
        newIndex = (currentIndex - 1 + cycleSize) % cycleSize;
    }

    setTextureVariant(cycleOrder[newIndex]);
}

std::vector<int> BaseHud::getAvailableTextureVariants() const {
    if (m_textureBaseName.empty()) {
        return {};
    }

    return AssetManager::getInstance().getAvailableVariants(m_textureBaseName);
}

// ============================================================================
// Shared HUD Rendering Helpers (eliminates duplication across HUDs)
// ============================================================================

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
    // Always add a string to keep indices consistent, but use empty string if title is hidden
    if (!m_bShowTitle) {
        addString("", x, y, justify, fontIndex, color, fontSize);
    } else {
        addString(text, x, y, justify, fontIndex, color, fontSize);
    }
}

void BaseHud::addBackgroundQuad(float x, float y, float width, float height) {
    using namespace PluginConstants;

    // Always add quad to keep indices consistent, but use transparent color if hidden
    SPluginQuad_t quadEntry;

    applyOffset(x, y);
    setQuadPositions(quadEntry, x, y, width, height);

    // Check if background texture should be used
    if (m_bShowBackgroundTexture && m_iBackgroundTextureIndex > 0) {
        // Use sprite texture for background
        quadEntry.m_iSprite = m_iBackgroundTextureIndex;
        // White color with opacity to allow texture to show through
        quadEntry.m_ulColor = PluginUtils::applyOpacity(ColorPalette::WHITE, m_fBackgroundOpacity);
    } else {
        // Use solid color background
        quadEntry.m_iSprite = SpriteIndex::SOLID_COLOR;
        // Get configured background color and apply opacity
        unsigned long bgColor = ColorConfig::getInstance().getBackground();
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
        setQuadPositions(m_quads[0], x, y, width, height);
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
