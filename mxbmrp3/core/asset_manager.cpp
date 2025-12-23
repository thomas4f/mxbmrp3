// ============================================================================
// core/asset_manager.cpp
// Dynamic asset discovery and management for fonts, textures, and icons
// ============================================================================
#include "asset_manager.h"
#include "../diagnostics/logger.h"
#include <windows.h>
#include <algorithm>
#include <cctype>
#include <sstream>

AssetManager& AssetManager::getInstance() {
    static AssetManager instance;
    return instance;
}

void AssetManager::discoverAssets() {
    if (m_initialized) {
        DEBUG_WARN("AssetManager::discoverAssets called multiple times");
        return;
    }

    DEBUG_INFO("AssetManager: Starting asset discovery...");

    // Clear any existing data
    m_fonts.clear();
    m_textures.clear();
    m_icons.clear();
    m_fontNameToIndex.clear();
    m_textureNameToIndex.clear();
    m_iconNameToIndex.clear();
    m_totalTextureSprites = 0;
    m_firstIconSpriteIndex = 0;

    // Discover assets in order (fonts, textures, icons)
    discoverFonts();
    discoverTextures();
    discoverIcons();

    m_initialized = true;

    DEBUG_INFO_F("AssetManager: Discovery complete - %zu fonts, %zu texture bases (%zu sprites), %zu icons",
        m_fonts.size(), m_textures.size(), m_totalTextureSprites, m_icons.size());
}

void AssetManager::discoverFonts() {
    std::string searchPath = std::string(DISCOVERY_DIR) + "\\" + FONTS_SUBDIR + "\\*.fnt";

    WIN32_FIND_DATAA findData;
    HANDLE hFind = FindFirstFileA(searchPath.c_str(), &findData);

    if (hFind == INVALID_HANDLE_VALUE) {
        DEBUG_WARN_F("AssetManager: No fonts found in %s\\%s", DISCOVERY_DIR, FONTS_SUBDIR);
        return;
    }

    int fontIndex = 1;  // Game engine uses 1-based font indices
    do {
        if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            std::string filename = findData.cFileName;

            // Remove .fnt extension
            size_t dotPos = filename.rfind('.');
            std::string baseName = (dotPos != std::string::npos) ? filename.substr(0, dotPos) : filename;

            FontAsset font;
            font.filename = baseName;
            font.displayName = generateDisplayName(baseName);
            font.fontIndex = fontIndex++;

            m_fontNameToIndex[baseName] = m_fonts.size();
            m_fonts.push_back(font);

            DEBUG_INFO_F("AssetManager: Found font [%d] %s (%s)",
                font.fontIndex, font.filename.c_str(), font.displayName.c_str());
        }
    } while (FindNextFileA(hFind, &findData));

    FindClose(hFind);
}

void AssetManager::discoverTextures() {
    std::string searchPath = std::string(DISCOVERY_DIR) + "\\" + TEXTURES_SUBDIR + "\\*.tga";

    WIN32_FIND_DATAA findData;
    HANDLE hFind = FindFirstFileA(searchPath.c_str(), &findData);

    if (hFind == INVALID_HANDLE_VALUE) {
        DEBUG_WARN_F("AssetManager: No textures found in %s\\%s", DISCOVERY_DIR, TEXTURES_SUBDIR);
        return;
    }

    // First pass: collect all variants and group by base name
    std::map<std::string, std::vector<int>> variantMap;

    do {
        if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            std::string filename = findData.cFileName;
            std::string baseName;
            int variant;

            if (parseTextureFilename(filename, baseName, variant)) {
                variantMap[baseName].push_back(variant);
            } else {
                DEBUG_WARN_F("AssetManager: Could not parse texture filename: %s", filename.c_str());
            }
        }
    } while (FindNextFileA(hFind, &findData));

    FindClose(hFind);

    // Second pass: build texture assets with sorted variants
    // Sort base names for consistent ordering
    std::vector<std::string> sortedBaseNames;
    for (const auto& pair : variantMap) {
        sortedBaseNames.push_back(pair.first);
    }
    std::sort(sortedBaseNames.begin(), sortedBaseNames.end());

    int spriteIndex = 1;  // Start at 1 (0 is reserved for SOLID_COLOR)

    for (const std::string& baseName : sortedBaseNames) {
        std::vector<int>& variants = variantMap[baseName];
        std::sort(variants.begin(), variants.end());

        TextureAsset texture;
        texture.baseName = baseName;
        texture.variants = variants;
        texture.firstSpriteIndex = spriteIndex;

        m_textureNameToIndex[baseName] = m_textures.size();
        m_textures.push_back(texture);

        DEBUG_INFO_F("AssetManager: Found texture '%s' with %zu variants (sprites %d-%d)",
            baseName.c_str(), variants.size(), spriteIndex, spriteIndex + static_cast<int>(variants.size()) - 1);

        spriteIndex += static_cast<int>(variants.size());
    }

    m_totalTextureSprites = spriteIndex - 1;
    m_firstIconSpriteIndex = spriteIndex;  // Icons start after textures
}

void AssetManager::discoverIcons() {
    std::string searchPath = std::string(DISCOVERY_DIR) + "\\" + ICONS_SUBDIR + "\\*.tga";

    WIN32_FIND_DATAA findData;
    HANDLE hFind = FindFirstFileA(searchPath.c_str(), &findData);

    if (hFind == INVALID_HANDLE_VALUE) {
        DEBUG_WARN_F("AssetManager: No icons found in %s\\%s", DISCOVERY_DIR, ICONS_SUBDIR);
        return;
    }

    // Collect all icon filenames
    std::vector<std::string> iconFiles;
    do {
        if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            iconFiles.push_back(findData.cFileName);
        }
    } while (FindNextFileA(hFind, &findData));

    FindClose(hFind);

    // Sort alphabetically for consistent ordering
    std::sort(iconFiles.begin(), iconFiles.end());

    int spriteIndex = m_firstIconSpriteIndex;

    for (const std::string& filename : iconFiles) {
        // Remove .tga extension
        size_t dotPos = filename.rfind('.');
        std::string baseName = (dotPos != std::string::npos) ? filename.substr(0, dotPos) : filename;

        IconAsset icon;
        icon.filename = baseName;
        icon.displayName = generateDisplayName(baseName);
        icon.spriteIndex = spriteIndex++;

        m_iconNameToIndex[baseName] = m_icons.size();
        m_icons.push_back(icon);
    }

    DEBUG_INFO_F("AssetManager: Found %zu icons (sprites %d-%d)",
        m_icons.size(), m_firstIconSpriteIndex,
        m_firstIconSpriteIndex + static_cast<int>(m_icons.size()) - 1);
}

bool AssetManager::parseTextureFilename(const std::string& filename, std::string& baseName, int& variant) const {
    // Expected format: "base_name_N.tga" where N is the variant number
    // Find the .tga extension
    size_t dotPos = filename.rfind('.');
    if (dotPos == std::string::npos) {
        return false;
    }

    std::string nameWithoutExt = filename.substr(0, dotPos);

    // Find the last underscore (before the variant number)
    size_t lastUnderscore = nameWithoutExt.rfind('_');
    if (lastUnderscore == std::string::npos || lastUnderscore == nameWithoutExt.length() - 1) {
        return false;
    }

    // Extract the variant number string
    std::string variantStr = nameWithoutExt.substr(lastUnderscore + 1);

    // Check if it's all digits
    for (char c : variantStr) {
        if (!std::isdigit(static_cast<unsigned char>(c))) {
            return false;
        }
    }

    // Parse the variant number
    try {
        variant = std::stoi(variantStr);
        if (variant <= 0) {
            return false;  // Variant must be positive
        }
    } catch (...) {
        return false;
    }

    baseName = nameWithoutExt.substr(0, lastUnderscore);
    return true;
}

std::string AssetManager::generateDisplayName(const std::string& filename) const {
    std::string result;
    bool capitalizeNext = true;

    for (size_t i = 0; i < filename.length(); ++i) {
        char c = filename[i];

        if (c == '-' || c == '_') {
            // Replace separators with spaces
            result += ' ';
            capitalizeNext = true;
        } else if (std::isupper(static_cast<unsigned char>(c)) && i > 0 &&
                   std::islower(static_cast<unsigned char>(filename[i-1]))) {
            // CamelCase: add space before uppercase letter following lowercase
            result += ' ';
            result += c;
            capitalizeNext = false;
        } else if (capitalizeNext) {
            result += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            capitalizeNext = false;
        } else {
            result += c;
        }
    }

    return result;
}

std::string AssetManager::getFontPath(size_t index) const {
    if (index >= m_fonts.size()) {
        return "";
    }

    return std::string(RESOURCE_DIR) + "\\" + FONTS_SUBDIR + "\\" + m_fonts[index].filename + ".fnt";
}

const FontAsset* AssetManager::getFontByName(const std::string& name) const {
    auto it = m_fontNameToIndex.find(name);
    if (it != m_fontNameToIndex.end()) {
        return &m_fonts[it->second];
    }
    return nullptr;
}

int AssetManager::getFontIndexByName(const std::string& name) const {
    const FontAsset* font = getFontByName(name);
    return font ? font->fontIndex : 0;
}

const TextureAsset* AssetManager::getTextureByName(const std::string& baseName) const {
    auto it = m_textureNameToIndex.find(baseName);
    if (it != m_textureNameToIndex.end()) {
        return &m_textures[it->second];
    }
    return nullptr;
}

int AssetManager::getSpriteIndex(const std::string& baseName, int variant) const {
    const TextureAsset* texture = getTextureByName(baseName);
    if (!texture) {
        return 0;  // Not found
    }

    // Find the variant in the sorted list
    for (size_t i = 0; i < texture->variants.size(); ++i) {
        if (texture->variants[i] == variant) {
            return texture->firstSpriteIndex + static_cast<int>(i);
        }
    }

    return 0;  // Variant not found
}

std::vector<int> AssetManager::getAvailableVariants(const std::string& baseName) const {
    const TextureAsset* texture = getTextureByName(baseName);
    if (texture) {
        return texture->variants;
    }
    return {};
}

std::string AssetManager::getTexturePath(const std::string& baseName, int variant) const {
    std::ostringstream path;
    path << RESOURCE_DIR << "\\" << TEXTURES_SUBDIR << "\\" << baseName << "_" << variant << ".tga";
    return path.str();
}

std::string AssetManager::getIconPath(size_t index) const {
    if (index >= m_icons.size()) {
        return "";
    }

    return std::string(RESOURCE_DIR) + "\\" + ICONS_SUBDIR + "\\" + m_icons[index].filename + ".tga";
}

int AssetManager::getIconSpriteIndex(const std::string& name) const {
    auto it = m_iconNameToIndex.find(name);
    if (it != m_iconNameToIndex.end()) {
        return m_icons[it->second].spriteIndex;
    }
    return 0;
}
