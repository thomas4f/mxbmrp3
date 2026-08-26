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
    constexpr unsigned long BROWN = PluginUtils::makeColor(139, 90, 43);        // #8b5a2b

    // The full palette for cycling through
    constexpr std::array<unsigned long, 14> ALL_COLORS = {
        WHITE, LIGHT_GRAY, GRAY, DARK_GRAY, BLACK,
        RED, GREEN, BLUE, YELLOW, ORANGE, CYAN, PURPLE, PINK, BROWN
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

// Slot from the ini key that names it ("primary"), or -1. The names are the same
// ones the settings file's [Colors] section uses, so a theme's palette and a user's
// override are written identically -- one vocabulary, two files.
int colorSlotFromName(const char* name);

class ColorConfig {
public:
    static ColorConfig& getInstance();

    // Get color for a specific slot
    unsigned long getColor(ColorSlot slot) const;

    // Convenience getters for each slot
    unsigned long getPrimary() const { return getColor(ColorSlot::PRIMARY); }
    unsigned long getSecondary() const { return getColor(ColorSlot::SECONDARY); }
    unsigned long getTertiary() const { return getColor(ColorSlot::TERTIARY); }
    unsigned long getMuted() const { return getColor(ColorSlot::MUTED); }
    unsigned long getBackground() const { return getColor(ColorSlot::BACKGROUND); }
    unsigned long getPositive() const { return getColor(ColorSlot::POSITIVE); }
    unsigned long getWarning() const { return getColor(ColorSlot::WARNING); }
    unsigned long getNeutral() const { return getColor(ColorSlot::NEUTRAL); }
    unsigned long getNegative() const { return getColor(ColorSlot::NEGATIVE); }
    unsigned long getAccent() const { return getColor(ColorSlot::ACCENT); }

    // Set color for a specific slot. This is a USER OVERRIDE: it pins the slot, so
    // it survives a theme change and is what gets written to the settings file.
    void setColor(ColorSlot slot, unsigned long color);

    // Whether the user has pinned this slot, i.e. whether getColor() is answering
    // from the override rather than from the theme or the built-in default.
    //
    // Load-bearing for PERSISTENCE, not just for the UI: only pinned slots are
    // written, so a palette the user never touched keeps following whatever theme
    // they pick. Writing all ten unconditionally is what would freeze the first
    // theme's colours into the settings file forever.
    bool isOverridden(ColorSlot slot) const;
    void clearOverride(ColorSlot slot);

    // The colour in effect for a slot when the user has NOT pinned it: the active
    // theme's if it states one, else the built-in default.
    static unsigned long getThemeOrDefaultColor(ColorSlot slot);

    // Cycle to next/previous color in the palette for a slot
    void cycleColor(ColorSlot slot, bool forward = true);

    // Reset all colors to defaults
    void resetToDefaults();


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
    // Per-slot: has the user set this, or is it following the theme? See
    // isOverridden().
    std::array<bool, static_cast<size_t>(ColorSlot::COUNT)> m_overridden{};
};
