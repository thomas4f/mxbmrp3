// ============================================================================
// core/color_config.h
// User-configurable color settings for HUD elements
// ============================================================================
#pragma once

#include <array>
#include <cstdint>
#include "plugin_utils.h"

// Predefined color palette that users can cycle through
// Note: Game uses ABGR format: (A << 24) | (B << 16) | (G << 8) | R
namespace ColorPalette {
    // Basic colors (using makeColor for correct ABGR format)
    constexpr unsigned long WHITE = PluginUtils::makeColor(255, 255, 255);      // #ffffff
    constexpr unsigned long LIGHT_GRAY = PluginUtils::makeColor(190, 190, 190); // #bebebe
    constexpr unsigned long GRAY = PluginUtils::makeColor(140, 140, 140);       // #8c8c8c
    constexpr unsigned long DARK_GRAY = PluginUtils::makeColor(100, 100, 100);  // #646464
    constexpr unsigned long BLACK = PluginUtils::makeColor(0, 0, 0);            // #000000

    // Accent colors
    constexpr unsigned long RED = PluginUtils::makeColor(255, 0, 0);            // #ff0000
    constexpr unsigned long GREEN = PluginUtils::makeColor(0, 255, 0);          // #00ff00
    constexpr unsigned long BLUE = PluginUtils::makeColor(0, 0, 255);           // #0000ff
    constexpr unsigned long YELLOW = PluginUtils::makeColor(255, 255, 0);       // #ffff00
    constexpr unsigned long ORANGE = PluginUtils::makeColor(255, 165, 0);       // #ffa500
    constexpr unsigned long CYAN = PluginUtils::makeColor(0, 255, 255);         // #00ffff
    constexpr unsigned long PURPLE = PluginUtils::makeColor(200, 0, 255);       // #c800ff
    constexpr unsigned long PINK = PluginUtils::makeColor(255, 105, 180);       // #ff69b4

    // The full palette for cycling through
    constexpr std::array<unsigned long, 13> ALL_COLORS = {
        WHITE, LIGHT_GRAY, GRAY, DARK_GRAY, BLACK,
        RED, GREEN, BLUE, YELLOW, ORANGE, CYAN, PURPLE, PINK
    };

    // Color names for display in settings UI
    const char* getColorName(unsigned long color);

    // Get index of a color in the palette (-1 if not found)
    int getColorIndex(unsigned long color);
}

// Color slot identifiers for the 10 configurable colors
enum class ColorSlot {
    PRIMARY = 0,      // Main text color
    SECONDARY,        // Secondary text color
    TERTIARY,         // Tertiary text color
    MUTED,            // Muted/disabled text color
    BACKGROUND,       // Background color
    POSITIVE,         // Positive/good indicator (e.g., faster times)
    WARNING,          // Warning indicator
    NEUTRAL,          // Neutral indicator
    NEGATIVE,         // Negative/bad indicator (e.g., slower times)
    ACCENT,           // Button/interactive element backgrounds
    COUNT
};

class ColorConfig {
public:
    static ColorConfig& getInstance();

    // Get color for a specific slot
    unsigned long getColor(ColorSlot slot) const;

    // Convenience getters for each slot
    unsigned long getPrimary() const { return m_colors[static_cast<size_t>(ColorSlot::PRIMARY)]; }
    unsigned long getSecondary() const { return m_colors[static_cast<size_t>(ColorSlot::SECONDARY)]; }
    unsigned long getTertiary() const { return m_colors[static_cast<size_t>(ColorSlot::TERTIARY)]; }
    unsigned long getMuted() const { return m_colors[static_cast<size_t>(ColorSlot::MUTED)]; }
    unsigned long getBackground() const { return m_colors[static_cast<size_t>(ColorSlot::BACKGROUND)]; }
    unsigned long getPositive() const { return m_colors[static_cast<size_t>(ColorSlot::POSITIVE)]; }
    unsigned long getWarning() const { return m_colors[static_cast<size_t>(ColorSlot::WARNING)]; }
    unsigned long getNeutral() const { return m_colors[static_cast<size_t>(ColorSlot::NEUTRAL)]; }
    unsigned long getNegative() const { return m_colors[static_cast<size_t>(ColorSlot::NEGATIVE)]; }
    unsigned long getAccent() const { return m_colors[static_cast<size_t>(ColorSlot::ACCENT)]; }

    // Set color for a specific slot
    void setColor(ColorSlot slot, unsigned long color);

    // Cycle to next/previous color in the palette for a slot
    void cycleColor(ColorSlot slot, bool forward = true);

    // Reset all colors to defaults
    void resetToDefaults();

    // Get/set raw color array (for save/load)
    const std::array<unsigned long, static_cast<size_t>(ColorSlot::COUNT)>& getColors() const { return m_colors; }
    void setColors(const std::array<unsigned long, static_cast<size_t>(ColorSlot::COUNT)>& colors) { m_colors = colors; }

    // Grid snapping setting (for HUD positioning)
    bool getGridSnapping() const { return m_bGridSnapping; }
    void setGridSnapping(bool enabled) { m_bGridSnapping = enabled; }

    // Drop shadow setting (for text rendering)
    bool getDropShadow() const { return m_bDropShadow; }
    void setDropShadow(bool enabled) { m_bDropShadow = enabled; }

    // Drop shadow advanced settings (INI-only)
    float getDropShadowOffsetX() const { return m_fDropShadowOffsetX; }
    float getDropShadowOffsetY() const { return m_fDropShadowOffsetY; }
    unsigned long getDropShadowColor() const { return m_ulDropShadowColor; }
    void setDropShadowOffsetX(float offset) { m_fDropShadowOffsetX = offset; }
    void setDropShadowOffsetY(float offset) { m_fDropShadowOffsetY = offset; }
    void setDropShadowColor(unsigned long color) { m_ulDropShadowColor = color; }

    // Get slot name for display
    static const char* getSlotName(ColorSlot slot);

    // Get default color for a slot
    static unsigned long getDefaultColor(ColorSlot slot);

private:
    ColorConfig();
    ~ColorConfig() = default;
    ColorConfig(const ColorConfig&) = delete;
    ColorConfig& operator=(const ColorConfig&) = delete;

    std::array<unsigned long, static_cast<size_t>(ColorSlot::COUNT)> m_colors;
    bool m_bGridSnapping = true;  // Grid snapping enabled by default
    bool m_bDropShadow = false;   // Drop shadow disabled by default

    // Drop shadow advanced settings (INI-only, as percentage of font size)
    float m_fDropShadowOffsetX = 0.03f;          // 3% of font size
    float m_fDropShadowOffsetY = 0.04f;          // 4% of font size
    unsigned long m_ulDropShadowColor = 0xAA000000;  // Semi-transparent black
};
