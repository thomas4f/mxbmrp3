// ============================================================================
// core/font_config.h
// User-configurable font categories for HUD elements
// Maps semantic font categories (Title, Normal, Bold, etc.) to discovered fonts
// ============================================================================
#pragma once

#include <array>
#include <string>

// Font category identifiers for semantic font usage
enum class FontCategory {
    TITLE = 0,        // Used for HUD titles (default: EnterSansman)
    NORMAL,           // Used for normal text (default: RobotoMono-Regular)
    STRONG,           // Used for emphasis/important text (default: RobotoMono-Bold)
    DIGITS,           // Used for numeric displays (default: RobotoMono-Regular)
    MARKER,           // Marker/handwritten style (default: FuzzyBubbles-Regular)
    SMALL,            // Small labels on map/radar (default: Tiny5-Regular)
    COUNT
};

class FontConfig {
public:
    static FontConfig& getInstance();

    // Get font index for a category (returns game engine font index, 1-based)
    int getFont(FontCategory category) const;

    // Get current font name for a category
    const char* getFontName(FontCategory category) const;

    // Get current font display name for a category (formatted for UI)
    const char* getFontDisplayName(FontCategory category) const;

    // Set font for a category by font name
    void setFont(FontCategory category, const std::string& fontName);

    // Cycle to next/previous font in the available fonts for a category
    void cycleFont(FontCategory category, bool forward = true);

    // Reset all categories to defaults
    void resetToDefaults();

    // Get category name for display
    static const char* getCategoryName(FontCategory category);

    // Get default font name for a category
    static const char* getDefaultFontName(FontCategory category);

    // Get/set raw font name array (for save/load)
    const std::array<std::string, static_cast<size_t>(FontCategory::COUNT)>& getFontNames() const { return m_fontNames; }
    void setFontNames(const std::array<std::string, static_cast<size_t>(FontCategory::COUNT)>& names);

private:
    FontConfig();
    ~FontConfig() = default;
    FontConfig(const FontConfig&) = delete;
    FontConfig& operator=(const FontConfig&) = delete;

    // Stores the font filename (without extension) for each category
    std::array<std::string, static_cast<size_t>(FontCategory::COUNT)> m_fontNames;
};
