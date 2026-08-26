// ============================================================================
// core/font_config.cpp
// User-configurable font categories for HUD elements
// ============================================================================
#include <vector>
#include "font_config.h"
#include "../diagnostics/call_counters.h"
#include "asset_manager.h"
#include "ui_config.h"
#include "../diagnostics/logger.h"

// Memoised theme lookup for this config's hot path.
//
// getThemeByName() is a linear string scan, and BaseHud::activeTheme()'s comment
// already calls it out as needing a memo -- it just memoised the HUD side and left
// these two, which are the ones actually in the inner loops: getColor runs per
// column per rider on a full grid, and getFont at every addString.
//
// Keyed on the same global generation counter, so discovery, a config reload and a
// change of selected theme all invalidate it; the name is compared too because the
// per-HUD override can ask for a different theme than the last caller did.
static const ThemeAsset* mxbResolveThemeMemo(const std::string& name) {
    static const ThemeAsset* s_cached = nullptr;
    static unsigned int s_gen = 0;              // 0 is never a live generation
    static std::string s_name;
    const unsigned int gen = mxbThemeGeneration();
    if (gen == s_gen && name == s_name) return s_cached;
    s_cached = AssetManager::getInstance().getThemeByName(name);
    s_gen = gen;
    s_name = name;
    return s_cached;
}


FontConfig& FontConfig::getInstance() {
    static FontConfig instance;
    return instance;
}

FontConfig::FontConfig() {
    resetToDefaults();
}

int FontConfig::getFont(FontCategory category) const {
    MXB_COUNT_CALL(GET_FONT);
    size_t index = static_cast<size_t>(category);
    if (index >= m_fontNames.size()) {
        return 1;  // Fallback to first font
    }

    // getFontName(), so the theme's font is resolved too -- reading m_fontNames
    // directly is how the whole precedence gets bypassed one call site at a time.
    //
    // const char*, NOT a std::string: this is called at every addString during a
    // rebuild (318 call sites) against a 2.08ms budget, and four of the six shipped
    // font names are past the 15-char SSO limit, so binding the result to a
    // std::string heap-allocated on every one of them.
    const char* fontName = getFontName(category);

    // Memoised: see FontIndexMemo. Everything below it -- the scan and the emphasis
    // lookup -- is skipped while neither the font set nor this category's chosen
    // name has moved, which is every frame in steady state.
    FontIndexMemo& memo = m_indexMemo[index];
    const unsigned int gen = mxbThemeGeneration();
    if (memo.gen == gen && memo.index != 0 && memo.name == fontName) return memo.index;

    int fontIndex = AssetManager::getInstance().getFontIndexByName(fontName);

    // ADAPT TO CONTEXT: Title and Strong take the heavier cut of whatever face
    // was chosen, when one shipped alongside it. Weight is what every design
    // system uses for hierarchy and what this HUD had no way to express -- the
    // categories already exist, so the choice belongs here rather than in the
    // picker, where it would be a second thing for the user to get right.
    // A subscript, not a lookup; 0 when the face has no companion.
    if (category == FontCategory::TITLE || category == FontCategory::STRONG) {
        const int heavier = AssetManager::getInstance().getEmphasisFontIndex(fontIndex);
        if (heavier != 0) fontIndex = heavier;
    }

    if (fontIndex == 0) {
        // Font not found, try to get default
        const char* defaultName = getDefaultFontName(category);
        DEBUG_WARN_F("Font '%s' not found for category %s, falling back to '%s'",
                     fontName, getCategoryName(category), defaultName);
        fontIndex = AssetManager::getInstance().getFontIndexByName(defaultName);
        if (fontIndex == 0) {
            DEBUG_WARN_F("Default font '%s' also not found, using first available font", defaultName);
            fontIndex = 1;  // Ultimate fallback
        }
    }

    // Stored AFTER the fallbacks, so a miss caches what the caller actually gets
    // rather than re-warning every frame for a font that is simply absent.
    memo.gen = gen;
    memo.name = fontName;
    memo.index = fontIndex;
    return fontIndex;
}

// THE PRECEDENCE: built-in default -> the active theme's -> the user's. Mirrors
// ColorConfig::getThemeOrDefaultColor(); see there for why it is the Appearance
// theme rather than a per-HUD override.
const char* FontConfig::getThemeOrDefaultFontName(FontCategory category) {
    const size_t index = static_cast<size_t>(category);
    if (index < static_cast<size_t>(FontCategory::COUNT)) {
        const std::string& name = UiConfig::getInstance().getThemeName();
        if (!name.empty()) {
            if (const ThemeAsset* theme = mxbResolveThemeMemo(name)) {
                if (!theme->fonts[index].empty()) return theme->fonts[index].c_str();
            }
        }
    }
    return getDefaultFontName(category);
}

const char* FontConfig::getFontName(FontCategory category) const {
    size_t index = static_cast<size_t>(category);
    if (index >= m_fontNames.size()) {
        return getDefaultFontName(category);
    }
    return m_overridden[index] ? m_fontNames[index].c_str()
                               : getThemeOrDefaultFontName(category);
}

bool FontConfig::isOverridden(FontCategory category) const {
    const size_t index = static_cast<size_t>(category);
    return index < m_overridden.size() && m_overridden[index];
}

void FontConfig::clearOverride(FontCategory category) {
    const size_t index = static_cast<size_t>(category);
    if (index < m_overridden.size()) {
        m_overridden[index] = false;
        m_fontNames[index] = getThemeOrDefaultFontName(category);
    }
}

const char* FontConfig::getFontDisplayName(FontCategory category) const {
    size_t index = static_cast<size_t>(category);
    if (index >= m_fontNames.size()) {
        return "Unknown";
    }

    const std::string& fontName = m_fontNames[index];
    const FontAsset* font = AssetManager::getInstance().getFontByName(fontName);

    if (font) {
        return font->displayName.c_str();
    }

    return fontName.c_str();  // Fallback to raw name
}

void FontConfig::setFont(FontCategory category, const std::string& fontName) {
    size_t index = static_cast<size_t>(category);
    if (index >= m_fontNames.size()) {
        return;
    }

    m_fontNames[index] = fontName;
    m_overridden[index] = true;
    DEBUG_INFO_F("FontConfig: %s set to %s", getCategoryName(category), fontName.c_str());
}

void FontConfig::cycleFont(FontCategory category, bool forward) {
    size_t categoryIndex = static_cast<size_t>(category);
    if (categoryIndex >= m_fontNames.size()) {
        return;
    }

    const AssetManager& assetMgr = AssetManager::getInstance();
    const auto& all = assetMgr.getFonts();

    // THE RING SKIPS EMPHASIS COMPANIONS. A heavier cut of a face is not a
    // separate typeface choice: picking "IBM Plex Sans" must not also offer "IBM
    // Plex Sans SemiBold" as its own stop, or every family a theme uses doubles
    // the ring and the user can land on a weight the categories pick for themselves
    // (AssetManager::emphasisBaseOf, FontConfig::getFont). Filtered into a view
    // rather than skipped mid-walk, so the Default stops at both ends and the
    // off-list recovery below all still index one contiguous list.
    std::vector<const FontAsset*> fonts;
    fonts.reserve(all.size());
    for (const FontAsset& f : all) {
        if (!f.emphasisOnly) fonts.push_back(&f);
    }

    if (fonts.empty()) {
        DEBUG_WARN("FontConfig: No fonts available to cycle");
        return;
    }

    const int fontCount = static_cast<int>(fonts.size());

    // THE RING HAS ONE MORE STOP THAN THE FONT LIST: "Default", meaning the category
    // is not pinned and follows the active theme (or the built-in face where the
    // theme says nothing). Exactly ColorConfig::cycleColor's ring, and for the same
    // reason -- see the long note there.
    //
    // The font case is the subtler of the two, because landing back on the default
    // FACE looks like a way out and is not: the row reads "Default" (value equality,
    // deliberately -- see addFontRow), while the category stays pinned. Switch theme
    // afterwards and the pinned face does NOT follow it, so the label was describing
    // the pixels and not the behaviour. Only clearOverride un-pins, and before this
    // stop existed nothing in the cycler called it.
    if (!isOverridden(category)) {
        // On the Default stop: step onto the first (or last) font in the list.
        const int entry = forward ? 0 : fontCount - 1;
        setFont(category, fonts[entry]->filename);
        DEBUG_INFO_F("FontConfig: %s cycled off Default to %s (%s)",
            getCategoryName(category),
            fonts[entry]->filename.c_str(),
            fonts[entry]->displayName.c_str());
        return;
    }

    // Find current font index in the fonts list. Through getFontName() so cycling
    // starts from what is ON SCREEN -- a theme's font, if the user has not pinned
    // one -- rather than from a stale slot the precedence is not using.
    const std::string currentName = getFontName(category);
    int currentIndex = -1;

    for (size_t i = 0; i < fonts.size(); ++i) {
        if (fonts[i]->filename == currentName) {
            currentIndex = static_cast<int>(i);
            break;
        }
    }

    if (currentIndex < 0) {
        // Pinned to a face that is not in the list (a hand-edited ini, or a font
        // file removed since). Land on a known entry rather than guessing a
        // neighbour it has no position among.
        setFont(category, fonts[0]->filename);
        DEBUG_INFO_F("FontConfig: %s was pinned off-list, cycled to %s (%s)",
            getCategoryName(category),
            fonts[0]->filename.c_str(),
            fonts[0]->displayName.c_str());
        return;
    }

    const int newIndex = currentIndex + (forward ? 1 : -1);
    if (newIndex >= fontCount || newIndex < 0) {
        // Off the end in either direction -> the Default stop. clearOverride is what
        // makes the category follow the theme again AND what stops it being written
        // to the settings file (see isOverridden).
        clearOverride(category);
        DEBUG_INFO_F("FontConfig: %s cycled to Default", getCategoryName(category));
        return;
    }

    // Through setFont(), which is what MARKS THE CATEGORY AS OVERRIDDEN. Writing
    // m_fontNames directly stored the new name where getFontName() would never read
    // it -- the category was still following the theme, so a click changed the stored
    // value and nothing on screen.
    setFont(category, fonts[newIndex]->filename);
    DEBUG_INFO_F("FontConfig: %s cycled to %s (%s)",
        getCategoryName(category),
        fonts[newIndex]->filename.c_str(),
        fonts[newIndex]->displayName.c_str());
}

void FontConfig::resetToDefaults() {
    // Clears the OVERRIDES: the categories go back to following the active theme,
    // not to the built-in faces. See ColorConfig::resetToDefaults().
    for (size_t i = 0; i < m_fontNames.size(); ++i) {
        m_overridden[i] = false;
        m_fontNames[i] = getThemeOrDefaultFontName(static_cast<FontCategory>(i));
    }
}

// The ini spelling of a category, lower case -- derived from getCategoryName() for
// the same reason colorSlotFromName() derives its own.
int fontCategoryFromName(const char* name) {
    if (!name) return -1;
    for (int i = 0; i < static_cast<int>(FontCategory::COUNT); ++i) {
        const char* cat = FontConfig::getCategoryName(static_cast<FontCategory>(i));
        size_t k = 0;
        for (; cat[k] && name[k]; ++k) {
            const char a = (cat[k] >= 'A' && cat[k] <= 'Z') ? static_cast<char>(cat[k] + 32) : cat[k];
            const char b = (name[k] >= 'A' && name[k] <= 'Z') ? static_cast<char>(name[k] + 32) : name[k];
            if (a != b) break;
        }
        if (cat[k] == '\0' && name[k] == '\0') return i;
    }
    return -1;
}

const char* FontConfig::getCategoryName(FontCategory category) {
    switch (category) {
        case FontCategory::TITLE:       return "Title";
        case FontCategory::NORMAL:      return "Normal";
        case FontCategory::STRONG:      return "Strong";
        case FontCategory::DIGITS:      return "Digits";
        case FontCategory::MARKER:      return "Marker";
        case FontCategory::SMALL:       return "Small";
        default:                        return "Unknown";
    }
}

const char* FontConfig::getDefaultFontName(FontCategory category) {
    switch (category) {
        case FontCategory::TITLE:       return "EnterSansman-Italic";
        case FontCategory::NORMAL:      return "RobotoMono-Regular";
        case FontCategory::STRONG:      return "RobotoMono-Bold";
        case FontCategory::DIGITS:      return "RobotoMono-Regular";
        case FontCategory::MARKER:      return "FuzzyBubbles-Regular";
        case FontCategory::SMALL:       return "Tiny5-Regular";
        default:                        return "RobotoMono-Regular";
    }
}

void FontConfig::setFontNames(const std::array<std::string, static_cast<size_t>(FontCategory::COUNT)>& names) {
    m_fontNames = names;
}
