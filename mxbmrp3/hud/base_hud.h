// ============================================================================
// hud/base_hud.h
// Base class for all HUD display elements with common rendering and positioning logic
// ============================================================================
#pragma once
#include <vector>
#include <string>
#include <cmath>
#include <chrono>
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
    int fontIndex = PluginConstants::Fonts::getNormal();
    unsigned long color = PluginUtils::makeColor(255, 255, 255);  // White default
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
    // Standard update interval for live timing displays (~165Hz for smooth ticking)
    static constexpr int TICK_UPDATE_INTERVAL_MS = 6;

    BaseHud() : m_bDataDirty(true), m_bLayoutDirty(true), m_bDraggable(false), m_bDragging(false),
        m_fOffsetX(0.0f), m_fOffsetY(0.0f), m_fDragStartX(0.0f), m_fDragStartY(0.0f),
        m_fInitialOffsetX(0.0f), m_fInitialOffsetY(0.0f),
        m_fBoundsLeft(0.0f), m_fBoundsTop(0.0f), m_fBoundsRight(0.0f), m_fBoundsBottom(0.0f),
        m_fScale(1.0f), m_bVisible(true), m_bShowTitle(true), m_fBackgroundOpacity(0.85f),
        m_bShowBackgroundTexture(false), m_iBackgroundTextureIndex(0),
        m_lastTickUpdate() {}

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

    // Legacy texture index support (for compatibility)
    void setBackgroundTextureIndex(int index) { m_iBackgroundTextureIndex = index; }
    int getBackgroundTextureIndex() const { return m_iBackgroundTextureIndex; }

    // Dynamic texture variant support
    // Sets the base texture name (e.g., "standings_hud") for this HUD
    void setTextureBaseName(const std::string& baseName);
    const std::string& getTextureBaseName() const { return m_textureBaseName; }

    // Texture variant: 0 = Off (solid color), 1+ = variant number
    void setTextureVariant(int variant);
    int getTextureVariant() const { return m_textureVariant; }

    // Cycle through available variants: Off -> 1 -> 2 -> ... -> Off
    void cycleTextureVariant(bool forward = true);

    // Get available variants for this HUD's texture (empty if no texture set)
    std::vector<int> getAvailableTextureVariants() const;

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

    virtual void setScale(float scale) {
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

    // ========================================================================
    // Frequent Update Support (for live timing displays)
    // ========================================================================
    // Override needsFrequentUpdates() to return true when HUD should tick at high frequency.
    // Call checkFrequentUpdates() in update() to apply the standard ticking logic.
    virtual bool needsFrequentUpdates() const { return false; }

    // Check if enough time has passed since last tick update; if so, marks data dirty.
    // Returns true if an update was triggered, false otherwise.
    // Use this in update() instead of duplicating the tick check logic.
    bool checkFrequentUpdates();

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
    // Standard Dirty Flag Handling
    // ========================================================================
    // Call processDirtyFlags() in update() implementations to handle the common pattern:
    //   - If data dirty: rebuild all, call onAfterDataRebuild(), clear both flags
    //   - Else if layout dirty: rebuild layout only, clear layout flag
    //
    // Override onAfterDataRebuild() if widget needs to update caches after rebuildRenderData().
    void processDirtyFlags();
    virtual void onAfterDataRebuild() {}

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

    // Helper for click detection - checks if point (x,y) is inside rectangle
    // Shared by StandingsHud, RecordsHud, MapHud for click region testing
    static bool isPointInRect(float x, float y, float rectX, float rectY, float width, float height) {
        return x >= rectX && x <= rectX + width && y >= rectY && y <= rectY + height;
    }

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

    // Dynamic texture support
    std::string m_textureBaseName;  // Base texture name (e.g., "standings_hud")
    int m_textureVariant = 0;       // Selected variant: 0 = Off, 1+ = variant number

    // Position and bounds (protected so derived classes can access for advanced positioning)
    float m_fOffsetX, m_fOffsetY;
    float m_fBoundsLeft, m_fBoundsTop, m_fBoundsRight, m_fBoundsBottom;

    // Frequent update timing (for live timing displays)
    std::chrono::steady_clock::time_point m_lastTickUpdate;

private:
    bool m_bDataDirty;
    bool m_bLayoutDirty;

    bool m_bDraggable;
    bool m_bDragging;
    float m_fDragStartX, m_fDragStartY;
    float m_fInitialOffsetX, m_fInitialOffsetY;
};
