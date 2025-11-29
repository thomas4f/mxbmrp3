// ============================================================================
// hud/base_hud.h
// Base class for all HUD display elements with common rendering and positioning logic
// ============================================================================
#pragma once
#include <vector>
#include <string>
#include <cmath>
#include "../vendor/piboso/mxb_api.h"
#include "../core/input_manager.h"
#include "../core/plugin_data.h"

// Configuration for individual HUD strings with per-string padding and backgrounds
struct HudStringConfig {
    std::string text;
    float x = 0.0f;
    float y = 0.0f;

    // Text formatting
    int justify = PluginConstants::Justify::LEFT;
    int fontIndex = PluginConstants::Fonts::ROBOTO_MONO;
    unsigned long color = PluginConstants::TextColors::PRIMARY;
    float fontSize = PluginConstants::FontSizes::NORMAL;

    // Layout padding (affects spacing and HUD bounds calculation)
    // This is "logical" padding that affects positioning
    float paddingLeft = 0.0f;
    float paddingRight = 0.0f;
    float paddingTop = 0.0f;
    float paddingBottom = 0.0f;

    // Optional background
    bool hasBackground = false;
    unsigned long backgroundColor = 0x000000;  // Black
    float backgroundOpacity = 0.85f;

    // Background padding (size of background quad around text)
    // Only used if hasBackground = true
    // Can be different from layout padding for visual effects
    float bgPaddingLeft = 0.0f;
    float bgPaddingRight = 0.0f;
    float bgPaddingTop = 0.0f;
    float bgPaddingBottom = 0.0f;

    // Cached text width (set to > 0 to skip recalculation in render)
    // PERFORMANCE: Caching this eliminates redundant calculateMonospaceTextWidth calls
    float cachedTextWidth = 0.0f;
};

class BaseHud {
public:
    BaseHud() : m_bDataDirty(true), m_bLayoutDirty(true), m_bDraggable(false), m_bDragging(false),
        m_fOffsetX(0.0f), m_fOffsetY(0.0f), m_fDragStartX(0.0f), m_fDragStartY(0.0f),
        m_fInitialOffsetX(0.0f), m_fInitialOffsetY(0.0f),
        m_fBoundsLeft(0.0f), m_fBoundsTop(0.0f), m_fBoundsRight(0.0f), m_fBoundsBottom(0.0f),
        m_fScale(1.0f), m_bVisible(true), m_bShowTitle(true), m_fBackgroundOpacity(0.85f),
        m_bShowBackgroundTexture(false), m_iBackgroundTextureIndex(0) {}

    virtual ~BaseHud() = default;

    virtual void update() = 0;
    virtual bool handlesDataType(DataChangeType dataType) const = 0;

    const std::vector<SPluginQuad_t>& getQuads() const { return m_quads; }
    const std::vector<SPluginString_t>& getStrings() const { return m_strings; }

    // Visibility controls
    void setVisible(bool visible) {
        if (m_bVisible != visible) {
            m_bVisible = visible;
            if (visible) setDataDirty();  // Rebuild when becoming visible
        }
    }
    bool isVisible() const { return m_bVisible; }

    void setShowTitle(bool showTitle) {
        if (m_bShowTitle != showTitle) {
            m_bShowTitle = showTitle;
            setDataDirty();
        }
    }
    bool getShowTitle() const { return m_bShowTitle; }

    void setBackgroundOpacity(float opacity) {
        // Clamp opacity to valid range [0.0, 1.0]
        if (opacity < 0.0f) opacity = 0.0f;
        if (opacity > 1.0f) opacity = 1.0f;

        // Round to nearest 10% increment to avoid floating point precision issues
        opacity = std::round(opacity * 10.0f) / 10.0f;

        if (m_fBackgroundOpacity != opacity) {
            m_fBackgroundOpacity = opacity;
            setDataDirty();
        }
    }
    float getBackgroundOpacity() const { return m_fBackgroundOpacity; }

    // Background texture support
    void setShowBackgroundTexture(bool show) {
        if (m_bShowBackgroundTexture != show) {
            m_bShowBackgroundTexture = show;
            setDataDirty();
        }
    }
    bool getShowBackgroundTexture() const { return m_bShowBackgroundTexture; }

    void setBackgroundTextureIndex(int index) { m_iBackgroundTextureIndex = index; }
    int getBackgroundTextureIndex() const { return m_iBackgroundTextureIndex; }

    // Drag and drop functionality
    void setDraggable(bool draggable) { m_bDraggable = draggable; }
    bool isDraggable() const { return m_bDraggable; }
    bool isDragging() const { return m_bDragging; }

    void setPosition(float offsetX, float offsetY) {
        if (m_fOffsetX != offsetX || m_fOffsetY != offsetY) {
            m_fOffsetX = offsetX;
            m_fOffsetY = offsetY;
            setLayoutDirty();
        }
    }
    float getOffsetX() const { return m_fOffsetX; }
    float getOffsetY() const { return m_fOffsetY; }

    void setScale(float scale) {
        if (scale <= 0.0f) scale = 0.1f;
        if (m_fScale != scale) {
            m_fScale = scale;
            setDataDirty();
        }
    }
    float getScale() const { return m_fScale; }

    void validatePosition();

    void setDataDirty() {
        m_bDataDirty = true;
        m_bLayoutDirty = true;
    }

    void setLayoutDirty() {
        m_bLayoutDirty = true;
    }

    virtual bool handleMouseInput(bool allowInput = true);
    bool isPointInBounds(float x, float y) const;

protected:
    bool clampPositionToBounds(float& offsetX, float& offsetY, const WindowBounds& windowBounds) const;
    virtual void rebuildRenderData() = 0;
    virtual void rebuildLayout() { rebuildRenderData(); }

    bool isDataDirty() const { return m_bDataDirty; }
    bool isLayoutDirty() const { return m_bLayoutDirty; }

    void clearDataDirty() { m_bDataDirty = false; }
    void clearLayoutDirty() { m_bLayoutDirty = false; }

    void setBounds(float left, float top, float right, float bottom);

    void applyOffset(float& x, float& y) const {
        x += m_fOffsetX;
        y += m_fOffsetY;
    }

    // ========================================================================
    // Widget Initialization Helper
    // ========================================================================
    // Helper to initialize simple draggable widgets (eliminates constructor duplication)
    // Consolidates: DEBUG_INFO logging, setDraggable(true), opacity setting, string
    // reservation, and rebuildRenderData() call.
    //
    // Use for simple widgets with standard initialization. Do NOT use if widget needs:
    // - Non-draggable positioning (e.g., TimingWidget center display)
    // - Quad reservation (e.g., BarsWidget, TimingWidget)
    // - Custom scale/opacity values
    //
    // Examples:
    //   initializeWidget("TimeWidget", 2);              // Standard
    //   initializeWidget("SessionWidget", 4, 0.0f);         // Transparent background
    //
    // REFACTORING NOTE: Further extraction was considered but rejected:
    // - Dimension calculation: Widget-specific height logic varies significantly.
    //   Existing helpers (calculateBackgroundWidth, updateBackgroundQuadPosition,
    //   positionString) already provide sufficient abstraction.
    // - Update pattern: The dirty-flag pattern (11 lines) is clear and readable.
    //   Template method extraction would reduce clarity without significant benefit.
    void initializeWidget(const char* widgetName, int stringsReserve, float backgroundOpacity = 0.1f);

    // Shared helper methods for HUD rendering (eliminates duplication across HUDs)
    void addString(const char* text, float x, float y, int justify, int fontIndex,
                   unsigned long color, float fontSize);
    void addTitleString(const char* text, float x, float y, int justify, int fontIndex,
                        unsigned long color, float fontSize);
    void addBackgroundQuad(float x, float y, float width, float height);
    void addDot(float x, float y, unsigned long color, float size);
    void addLineSegment(float x1, float y1, float x2, float y2, unsigned long color, float thickness);
    void addHorizontalGridLine(float x, float y, float width, unsigned long color, float thickness);
    static void setQuadPositions(SPluginQuad_t& quad, float x, float y, float width, float height);

    // Helper to update background quad position during rebuildLayout (reduces duplication)
    void updateBackgroundQuadPosition(float startX, float startY, float width, float height);

    // Styled string rendering with per-string padding and backgrounds
    void addStyledString(const HudStringConfig& config);
    void renderStyledStrings();

    // Calculate bounds for all styled strings (for HUD sizing)
    struct StyledStringBounds {
        float minX, minY, maxX, maxY;
        float width() const { return maxX - minX; }
        float height() const { return maxY - minY; }
    };
    StyledStringBounds calculateStyledStringBounds() const;

    // Scaled dimensions helper (eliminates repeated calculations in rebuildLayout/rebuildRenderData)
    struct ScaledDimensions {
        float fontSize;
        float fontSizeExtraSmall;
        float fontSizeSmall;
        float fontSizeLarge;
        float fontSizeExtraLarge;
        float paddingH;
        float paddingV;
        float lineHeightExtraSmall;
        float lineHeightSmall;
        float lineHeightLarge;
        float lineHeightNormal;
        float lineHeightExtraLarge;
        float scale;

        // Grid-aligned padding helpers (ensures strings align across HUDs)
        // Vertical grid unit = half-line-height (0.0111 unscaled, scaled by scale factor)
        float gridV(float units) const {
            constexpr float GRID_UNIT_V = 0.0111f;
            return GRID_UNIT_V * units * scale;
        }

        // Horizontal grid unit = char width (0.0055 unscaled, scaled by scale factor)
        float gridH(float units) const {
            constexpr float GRID_UNIT_H = 0.0055f;
            return GRID_UNIT_H * units * scale;
        }
    };
    ScaledDimensions getScaledDimensions() const;

    // Helper method to calculate text color with opacity (eliminates duplication in widgets)
    unsigned long getTextColorWithOpacity(uint8_t r = 255, uint8_t g = 255, uint8_t b = 255) const;

    // Helper methods to calculate background dimensions consistently (eliminates duplication in HUDs)
    float calculateBackgroundWidth(int charWidth) const;
    float calculateBackgroundHeight(int rowCount, bool includeTitle = true) const;

    // Helper method to position a string at (x, y) with offset applied (eliminates duplication in widget layouts)
    // Returns true if string was positioned, false if stringIndex >= m_strings.size()
    bool positionString(size_t stringIndex, float x, float y);

    std::vector<SPluginQuad_t> m_quads;
    std::vector<SPluginString_t> m_strings;
    std::vector<HudStringConfig> m_styledStringConfigs;  // Storage for styled string configurations
    float m_fScale;

    // Visibility and display options (protected so derived classes can access)
    bool m_bVisible;
    bool m_bShowTitle;
    float m_fBackgroundOpacity;  // 0.0 (fully transparent) to 1.0 (fully opaque)
    bool m_bShowBackgroundTexture;  // If true and texture exists, render sprite background
    int m_iBackgroundTextureIndex;  // 1-based sprite index (0 = no texture)

    // Position and bounds (protected so derived classes can access for advanced positioning)
    float m_fOffsetX, m_fOffsetY;
    float m_fBoundsLeft, m_fBoundsTop, m_fBoundsRight, m_fBoundsBottom;

private:
    bool m_bDataDirty;
    bool m_bLayoutDirty;

    bool m_bDraggable;
    bool m_bDragging;
    float m_fDragStartX, m_fDragStartY;
    float m_fInitialOffsetX, m_fInitialOffsetY;
};
