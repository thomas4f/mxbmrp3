// ============================================================================
// core/font_config.cpp
// User-configurable font categories for HUD elements
// ============================================================================
#include "font_config.h"
#include "asset_manager.h"
#include "../diagnostics/logger.h"

FontConfig& FontConfig::getInstance() {
    static FontConfig instance;
    return instance;
}

FontConfig::FontConfig() {
    resetToDefaults();
}

int FontConfig::getFont(FontCategory category) const {
    size_t index = static_cast<size_t>(category);
    if (index >= m_fontNames.size()) {
        return 1;  // Fallback to first font
    }

    const std::string& fontName = m_fontNames[index];
    int fontIndex = AssetManager::getInstance().getFontIndexByName(fontName);

    if (fontIndex == 0) {
        // Font not found, try to get default
        const char* defaultName = getDefaultFontName(category);
        DEBUG_WARN_F("Font '%s' not found for category %s, falling back to '%s'",
                     fontName.c_str(), getCategoryName(category), defaultName);
        fontIndex = AssetManager::getInstance().getFontIndexByName(defaultName);
        if (fontIndex == 0) {
            DEBUG_WARN_F("Default font '%s' also not found, using first available font", defaultName);
            fontIndex = 1;  // Ultimate fallback
        }
    }

    return fontIndex;
}

const char* FontConfig::getFontName(FontCategory category) const {
    size_t index = static_cast<size_t>(category);
    if (index >= m_fontNames.size()) {
        return getDefaultFontName(category);
    }
    return m_fontNames[index].c_str();
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
    DEBUG_INFO_F("FontConfig: %s set to %s", getCategoryName(category), fontName.c_str());
}

void FontConfig::cycleFont(FontCategory category, bool forward) {
    size_t categoryIndex = static_cast<size_t>(category);
    if (categoryIndex >= m_fontNames.size()) {
        return;
    }

    const AssetManager& assetMgr = AssetManager::getInstance();
    const auto& fonts = assetMgr.getFonts();

    if (fonts.empty()) {
        DEBUG_WARN("FontConfig: No fonts available to cycle");
        return;
    }

    // Find current font index in the fonts list
    const std::string& currentName = m_fontNames[categoryIndex];
    int currentIndex = -1;

    for (size_t i = 0; i < fonts.size(); ++i) {
        if (fonts[i].filename == currentName) {
            currentIndex = static_cast<int>(i);
            break;
        }
    }

    // Calculate next index
    int newIndex;
    int fontCount = static_cast<int>(fonts.size());

    if (currentIndex < 0) {
        // Current font not found, start from beginning
        newIndex = 0;
    } else if (forward) {
        newIndex = (currentIndex + 1) % fontCount;
    } else {
        newIndex = (currentIndex - 1 + fontCount) % fontCount;
    }

    m_fontNames[categoryIndex] = fonts[newIndex].filename;

    DEBUG_INFO_F("FontConfig: %s cycled to %s (%s)",
        getCategoryName(category),
        fonts[newIndex].filename.c_str(),
        fonts[newIndex].displayName.c_str());
}

void FontConfig::resetToDefaults() {
    m_fontNames[static_cast<size_t>(FontCategory::TITLE)] = getDefaultFontName(FontCategory::TITLE);
    m_fontNames[static_cast<size_t>(FontCategory::NORMAL)] = getDefaultFontName(FontCategory::NORMAL);
    m_fontNames[static_cast<size_t>(FontCategory::STRONG)] = getDefaultFontName(FontCategory::STRONG);
    m_fontNames[static_cast<size_t>(FontCategory::DIGITS)] = getDefaultFontName(FontCategory::DIGITS);
    m_fontNames[static_cast<size_t>(FontCategory::MARKER)] = getDefaultFontName(FontCategory::MARKER);
    m_fontNames[static_cast<size_t>(FontCategory::SMALL)] = getDefaultFontName(FontCategory::SMALL);

    DEBUG_INFO("FontConfig: Reset to defaults");
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
