// ============================================================================
// core/asset_manager.h
// Dynamic asset discovery and management for fonts, textures, and icons
// Scans mxbmrp3_data subdirectories at startup to build asset registries
// ============================================================================
#pragma once

#include <string>
#include <vector>
#include <map>
#include <array>

// Forward declarations
struct TextureVariantInfo;

// Texture asset identifier - maps a base name to its variants
struct TextureAsset {
    std::string baseName;              // e.g., "standings_hud"
    std::vector<int> variants;         // e.g., [1, 2, 3] for _1, _2, _3 files
    int firstSpriteIndex = 0;          // Index of variant 1 in the sprite array
};

// Font asset info
struct FontAsset {
    std::string filename;              // e.g., "RobotoMono-Regular"
    std::string displayName;           // e.g., "Roboto Mono"
    int fontIndex = 0;                 // Index in the font array (1-based for game engine)
};

// Icon asset info
struct IconAsset {
    std::string filename;              // e.g., "trophy-solid-full"
    std::string displayName;           // e.g., "Trophy"
    int spriteIndex = 0;               // Index in the sprite array
};

class AssetManager {
public:
    static AssetManager& getInstance();

    // Initialize - must be called before HudManager::setupDefaultResources
    // Syncs user override assets from savePath, then scans directories and builds asset registries
    // savePath: Game save directory (e.g., C:\Users\X\Documents\PiBoSo\MX Bikes\)
    void discoverAssets(const char* savePath);

    // Check if assets have been discovered
    bool isInitialized() const { return m_initialized; }

    // ========================================================================
    // Font Access
    // ========================================================================

    // Get all discovered fonts
    const std::vector<FontAsset>& getFonts() const { return m_fonts; }

    // Get font count
    size_t getFontCount() const { return m_fonts.size(); }

    // Get font path for registration (e.g., "mxbmrp3_data\\fonts\\RobotoMono-Regular.fnt")
    std::string getFontPath(size_t index) const;

    // Get font by name (returns nullptr if not found)
    const FontAsset* getFontByName(const std::string& name) const;

    // Get font index by name (returns 0 if not found, which is invalid)
    int getFontIndexByName(const std::string& name) const;

    // ========================================================================
    // Texture Access
    // ========================================================================

    // Get all discovered texture bases (unique base names with their variants)
    const std::vector<TextureAsset>& getTextures() const { return m_textures; }

    // Get texture asset by base name (e.g., "standings_hud")
    const TextureAsset* getTextureByName(const std::string& baseName) const;

    // Get sprite index for a specific variant (0 = not found)
    // variant: 1-based variant number (1, 2, 3, etc.)
    int getSpriteIndex(const std::string& baseName, int variant) const;

    // Get available variants for a texture base name
    // Returns empty vector if texture not found
    std::vector<int> getAvailableVariants(const std::string& baseName) const;

    // Get texture path for registration
    std::string getTexturePath(const std::string& baseName, int variant) const;

    // Get total number of texture sprites (for HudManager buffer allocation)
    size_t getTotalTextureSprites() const { return m_totalTextureSprites; }

    // ========================================================================
    // Icon Access
    // ========================================================================

    // Get all discovered icons
    const std::vector<IconAsset>& getIcons() const { return m_icons; }

    // Get icon count
    size_t getIconCount() const { return m_icons.size(); }

    // Get icon path for registration
    std::string getIconPath(size_t index) const;

    // Get icon sprite index by name (returns 0 if not found)
    int getIconSpriteIndex(const std::string& name) const;

    // Get icon filename by sprite index (returns empty string if not found)
    std::string getIconFilename(int spriteIndex) const;

    // Get icon display name by sprite index (returns empty string if not found)
    std::string getIconDisplayName(int spriteIndex) const;

    // Get first icon sprite index (for calculating offsets)
    int getFirstIconSpriteIndex() const { return m_firstIconSpriteIndex; }

    // ========================================================================
    // Path Configuration
    // ========================================================================

    // Discovery path (for FindFirstFileA - relative to game executable)
    static constexpr const char* DISCOVERY_DIR = "plugins\\mxbmrp3_data";
    // Resource path (for game engine - it adds "plugins\" prefix automatically)
    static constexpr const char* RESOURCE_DIR = "mxbmrp3_data";
    static constexpr const char* FONTS_SUBDIR = "fonts";
    static constexpr const char* TEXTURES_SUBDIR = "textures";
    static constexpr const char* ICONS_SUBDIR = "icons";
    // User override directory (under savePath, e.g., Documents\PiBoSo\MX Bikes\mxbmrp3\)
    static constexpr const char* USER_OVERRIDE_DIR = "mxbmrp3";

private:
    AssetManager() = default;
    ~AssetManager() = default;
    AssetManager(const AssetManager&) = delete;
    AssetManager& operator=(const AssetManager&) = delete;

    // User asset sync - copies user overrides from savePath to plugin data directory
    void syncUserAssets(const char* savePath);
    void syncDirectory(const std::string& sourceDir, const std::string& destDir, const char* extension);

    // Discovery helpers
    void discoverFonts();
    void discoverTextures();
    void discoverIcons();

    // Parse texture filename to extract base name and variant number
    // e.g., "standings_hud_1.tga" -> ("standings_hud", 1)
    bool parseTextureFilename(const std::string& filename, std::string& baseName, int& variant) const;

    // Generate display name from filename
    // e.g., "RobotoMono-Regular" -> "Roboto Mono"
    std::string generateDisplayName(const std::string& filename) const;

    // Asset storage
    std::vector<FontAsset> m_fonts;
    std::vector<TextureAsset> m_textures;
    std::vector<IconAsset> m_icons;

    // Quick lookup maps
    std::map<std::string, size_t> m_fontNameToIndex;
    std::map<std::string, size_t> m_textureNameToIndex;
    std::map<std::string, size_t> m_iconNameToIndex;

    // Sprite index tracking
    size_t m_totalTextureSprites = 0;
    int m_firstIconSpriteIndex = 0;

    bool m_initialized = false;
};
