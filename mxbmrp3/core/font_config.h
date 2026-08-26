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

// Category from the ini key that names it ("title"), or -1. Same names as the
// settings file's [Fonts] section.
int fontCategoryFromName(const char* name);

class FontConfig {
public:
    static FontConfig& getInstance();

    // Get font index for a category (returns game engine font index, 1-based)
    int getFont(FontCategory category) const;

    // Get current font name for a category
    const char* getFontName(FontCategory category) const;

    // Get current font display name for a category (formatted for UI)
    const char* getFontDisplayName(FontCategory category) const;

    // Set font for a category by font name. A USER OVERRIDE -- it pins the
    // category, so it survives a theme change and is what gets persisted.
    void setFont(FontCategory category, const std::string& fontName);

    // Whether the user pinned this category. Only pinned ones are written, so a
    // category the user never touched keeps following whatever theme is active.
    bool isOverridden(FontCategory category) const;
    void clearOverride(FontCategory category);

    // The font in effect when the user has NOT pinned it: the active theme's if it
    // names one, else the built-in default.
    //
    // LIFETIME: the returned pointer is BORROWED, and which owner it points at
    // depends on the answer -- a built-in default is a string literal and lives
    // forever, but a theme's font name aliases that ThemeAsset's own std::string.
    // AssetManager::reloadThemeLayouts() ASSIGNS over `theme.fonts` (so a deleted
    // key falls back to the built-in), and discovery rebuilds m_themes outright;
    // either invalidates the pointer. So: use it, or copy it, before returning to
    // the caller -- never store it across a RELOAD_CONFIG or an asset rediscovery.
    // Callers today all consume it immediately (a strcmp, or a copy into
    // m_fontNames), which is the pattern to keep.
    static const char* getThemeOrDefaultFontName(FontCategory category);

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
    std::array<bool, static_cast<size_t>(FontCategory::COUNT)> m_overridden{};

    // THE NAME -> INDEX STEP, memoised per category. getFont() runs at every
    // addString (318 call sites) and resolved the name through
    // AssetManager::getFontIndexByName(), a LINEAR SCAN with a string compare per
    // entry -- plus a second lookup for TITLE/STRONG's heavier cut.
    //
    // That scan's own comment argued a map would buy nothing because "m_fonts is a
    // handful of entries". True at the six fonts that shipped with 1.28; the font
    // set is EIGHTEEN now, so every string a HUD draws pays three times the compares
    // it used to, and a widget drawing one number cost 6us to rebuild.
    //
    // Keyed on the theme generation (covers discovery, a config reload and a change
    // of selected theme) AND the resolved name, which is what changes when the user
    // picks a different face for a category -- so no separate invalidation to forget.
    struct FontIndexMemo { unsigned int gen = 0; std::string name; int index = 0; };
    mutable std::array<FontIndexMemo, static_cast<size_t>(FontCategory::COUNT)> m_indexMemo;
};
