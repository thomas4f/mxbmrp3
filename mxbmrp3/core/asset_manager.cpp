// ============================================================================
// core/asset_manager.cpp
// Dynamic asset discovery and management for fonts, textures, and icons
// ============================================================================
#include "asset_manager.h"
#include "../diagnostics/call_counters.h"
#include "icon_resolve.h"
#include "layout_config.h"
#include "../diagnostics/logger.h"
#include <windows.h>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <fstream>

AssetManager& AssetManager::getInstance() {
    static AssetManager instance;
    return instance;
}

void AssetManager::discoverAssets(const char* savePath) {
    if (m_initialized) {
        DEBUG_WARN("AssetManager::discoverAssets called multiple times");
        return;
    }

    // Sync user override assets before discovery
    syncUserAssets(savePath);

    DEBUG_INFO("AssetManager: Starting asset discovery...");

    // Clear any existing data
    m_fonts.clear();
    m_textures.clear();
    m_icons.clear();
    m_themes.clear();
    m_gamepads.clear();
    m_pitboards.clear();
    m_fontNameToIndex.clear();
    m_textureNameToIndex.clear();
    m_iconNameToIndex.clear();
    m_totalTextureSprites = 0;
    m_firstIconSpriteIndex = 0;

    // Discover assets in order (fonts, textures, icons, themes).
    // Themes are LAST on purpose: their sprite indices sit past the icon block, so
    // adding or removing a theme cannot shift a texture or icon index (those are
    // persisted in user settings as variant/shape numbers).
    discoverFonts();
    discoverTextures();
    discoverIcons();
    discoverThemes();
    // Gamepad packs sit past the theme block for the same reason themes sit past the
    // icons: a pack added or removed must not shift an index anything else persists.
    // Nothing persists a pad index at all any more -- the setting names the pack --
    // but the ordering rule is what keeps that true for the neighbours.
    discoverGamepads();
    discoverPitboards();

    m_initialized = true;

    DEBUG_INFO_F("AssetManager: Discovery complete - %zu fonts, %zu texture bases (%zu sprites), %zu icons, %zu themes, %zu gamepads, %zu pitboards",
        m_fonts.size(), m_textures.size(), m_totalTextureSprites, m_icons.size(), m_themes.size(), m_gamepads.size(), m_pitboards.size());
}

void AssetManager::syncUserAssets(const char* savePath) {
    if (!savePath || savePath[0] == '\0') {
        DEBUG_INFO("AssetManager: No savePath provided, skipping user asset sync");
        return;
    }

    // Build user override base path: savePath/mxbmrp3/
    std::string userBaseDir = savePath;
    if (!userBaseDir.empty() && userBaseDir.back() != '\\' && userBaseDir.back() != '/') {
        userBaseDir += '\\';
    }
    userBaseDir += USER_OVERRIDE_DIR;

    // Create user override directories so users know where to put custom assets.
    // CreateDirectoryA is idempotent (ignores ERROR_ALREADY_EXISTS).
    CreateDirectoryA(userBaseDir.c_str(), NULL);
    CreateDirectoryA((userBaseDir + "\\" + FONTS_SUBDIR).c_str(), NULL);
    CreateDirectoryA((userBaseDir + "\\" + TEXTURES_SUBDIR).c_str(), NULL);
    CreateDirectoryA((userBaseDir + "\\" + ICONS_SUBDIR).c_str(), NULL);
    CreateDirectoryA((userBaseDir + "\\" + THEMES_SUBDIR).c_str(), NULL);
    CreateDirectoryA((userBaseDir + "\\" + GAMEPADS_SUBDIR).c_str(), NULL);
    CreateDirectoryA((userBaseDir + "\\" + PITBOARDS_SUBDIR).c_str(), NULL);
    CreateDirectoryA((userBaseDir + "\\" + SPOTTERS_SUBDIR).c_str(), NULL);
    CreateDirectoryA((userBaseDir + "\\" + WEB_SUBDIR).c_str(), NULL);
    CreateDirectoryA((userBaseDir + "\\" + WEB_SUBDIR + "\\logos").c_str(), NULL);
    CreateDirectoryA((userBaseDir + "\\" + WEB_SUBDIR + "\\js").c_str(), NULL);
    CreateDirectoryA((userBaseDir + "\\" + WEB_SUBDIR + "\\fonts").c_str(), NULL);
    CreateDirectoryA((userBaseDir + "\\" + WEB_SUBDIR + "\\icons").c_str(), NULL);

    // Check if user override directory has any content to sync
    DWORD attrs = GetFileAttributesA(userBaseDir.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        return;
    }

    DEBUG_INFO_F("AssetManager: Syncing user assets from %s", userBaseDir.c_str());

    // Ensure destination directories exist
    CreateDirectoryA(DISCOVERY_DIR, NULL);

    std::string destFonts = std::string(DISCOVERY_DIR) + "\\" + FONTS_SUBDIR;
    std::string destTextures = std::string(DISCOVERY_DIR) + "\\" + TEXTURES_SUBDIR;
    std::string destIcons = std::string(DISCOVERY_DIR) + "\\" + ICONS_SUBDIR;
    std::string destWeb = std::string(DISCOVERY_DIR) + "\\" + WEB_SUBDIR;

    CreateDirectoryA(destFonts.c_str(), NULL);
    CreateDirectoryA(destTextures.c_str(), NULL);
    CreateDirectoryA(destIcons.c_str(), NULL);
    CreateDirectoryA(destWeb.c_str(), NULL);

    std::string destWebLogos = destWeb + "\\logos";
    std::string destWebJs = destWeb + "\\js";
    std::string destWebFonts = destWeb + "\\fonts";
    std::string destWebIcons = destWeb + "\\icons";
    CreateDirectoryA(destWebLogos.c_str(), NULL);
    CreateDirectoryA(destWebJs.c_str(), NULL);
    CreateDirectoryA(destWebFonts.c_str(), NULL);
    CreateDirectoryA(destWebIcons.c_str(), NULL);

    // Sync each asset type. The web overlay is organized into subfolders
    // (js/ fonts/ icons/), so each is synced individually — the top-level "*.*"
    // pass covers root files (index.html, style.css, custom.css, sw.js).
    syncDirectory(userBaseDir + "\\" + FONTS_SUBDIR, destFonts, "*.fnt");
    syncDirectory(userBaseDir + "\\" + TEXTURES_SUBDIR, destTextures, "*.tga");
    syncDirectory(userBaseDir + "\\" + ICONS_SUBDIR, destIcons, "*.tga");
    // The NESTED asset types (<root>/<name>/<payload> + <name>.ini) cannot ride the
    // flat syncDirectory, so each pack folder is synced in turn. Without this a
    // user-authored pack has to be dropped straight into the game's plugins folder,
    // which every other asset type does not require -- and authoring one is exactly
    // the workflow the pack format exists to support.
    m_userBaseDir = userBaseDir;
    syncAllPackTypes(userBaseDir);
    syncDirectory(userBaseDir + "\\" + WEB_SUBDIR, destWeb, "*.*");
    syncDirectory(userBaseDir + "\\" + WEB_SUBDIR + "\\logos", destWebLogos, "*.png");
    syncDirectory(userBaseDir + "\\" + WEB_SUBDIR + "\\js", destWebJs, "*.js");
    syncDirectory(userBaseDir + "\\" + WEB_SUBDIR + "\\fonts", destWebFonts, "*.ttf");
    syncDirectory(userBaseDir + "\\" + WEB_SUBDIR + "\\icons", destWebIcons, "*.svg");
}

void AssetManager::syncDirectory(const std::string& sourceDir, const std::string& destDir, const char* extension) {
    std::string searchPath = sourceDir + "\\" + extension;

    WIN32_FIND_DATAA findData;
    HANDLE hFind = FindFirstFileA(searchPath.c_str(), &findData);

    if (hFind == INVALID_HANDLE_VALUE) {
        // No files found - this is normal if user hasn't added overrides for this type
        return;
    }

    do {
        // Skip symlinks/junctions (defense-in-depth): CopyFileA follows a file
        // symlink and copies the TARGET's contents, and copies under web\ are then
        // served by the embedded HTTP server — a planted link could expose an
        // arbitrary local file over the overlay endpoint. Real override files are
        // never reparse points.
        if (findData.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
            DEBUG_WARN_F("AssetManager: Skipping reparse point in user assets: %s",
                findData.cFileName);
            continue;
        }
        if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            std::string destPath = destDir + "\\" + findData.cFileName;

            // Check if destination exists with same timestamp (skip if unchanged)
            WIN32_FILE_ATTRIBUTE_DATA destAttrs;
            if (GetFileAttributesExA(destPath.c_str(), GetFileExInfoStandard, &destAttrs)) {
                // Compare timestamps - CopyFileA preserves source timestamp on dest
                if (CompareFileTime(&findData.ftLastWriteTime, &destAttrs.ftLastWriteTime) == 0) {
                    // Timestamps match, skip copy
                    continue;
                }
            }

            std::string sourcePath = sourceDir + "\\" + findData.cFileName;
            if (CopyFileA(sourcePath.c_str(), destPath.c_str(), FALSE)) {
                DEBUG_INFO_F("AssetManager: Copied user asset: %s", findData.cFileName);
            } else {
                DEBUG_WARN_F("AssetManager: Failed to copy user asset: %s (error %lu)",
                    findData.cFileName, GetLastError());
            }
        }
    } while (FindNextFileA(hFind, &findData));

    FindClose(hFind);
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

    // SECOND PASS: pair each heavier cut with the Regular it belongs to. Two
    // passes because discovery order is the directory's, so "Roboto-Medium" can
    // be seen before "Roboto-Regular" exists to point at it.
    //
    // The companion stays in m_fonts -- it must load and keep an engine index
    // like any other face -- and is only hidden from the CYCLER. Removing it
    // here instead would shift every later font's engine index, which is a
    // bigger change than it looks and buys nothing.
    for (FontAsset& f : m_fonts) {
        const std::string base = emphasisBaseOf(f.filename);
        if (base.empty()) continue;
        auto it = m_fontNameToIndex.find(base);
        if (it == m_fontNameToIndex.end()) continue;   // a weight with no Regular: leave it pickable
        f.emphasisOnly = true;
        m_fonts[it->second].emphasisIndex = f.fontIndex;
        DEBUG_INFO_F("AssetManager: %s is the emphasis cut of %s",
            f.filename.c_str(), base.c_str());
    }
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

        // Read TGA dimensions for each variant to compute aspect ratios
        for (int v : variants) {
            std::string tgaPath = std::string(DISCOVERY_DIR) + "\\" + TEXTURES_SUBDIR + "\\" + baseName + "_" + std::to_string(v) + ".tga";
            int texW = 0, texH = 0;
            if (readTgaDimensions(tgaPath, texW, texH) && texH > 0) {
                float aspect = static_cast<float>(texW) / static_cast<float>(texH);
                // Clamp to sane range to guard against malformed TGA headers
                if (aspect < 0.1f) aspect = 0.1f;
                if (aspect > 10.0f) aspect = 10.0f;
                texture.aspectRatios.push_back(aspect);
                DEBUG_INFO_F("AssetManager: Texture '%s_%d' dimensions %dx%d (aspect %.3f)",
                    baseName.c_str(), v, texW, texH, aspect);
            } else {
                texture.aspectRatios.push_back(0.0f);  // Unknown - skip correction
                DEBUG_WARN_F("AssetManager: Could not read dimensions for '%s_%d.tga'",
                    baseName.c_str(), v);
            }
        }

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

// Copy every pack type's user folder across. The table it walks is PACK_TYPES in
// the header, which is also where the reason it is a table lives.
void AssetManager::syncAllPackTypes(const std::string& userBaseDir) {
    if (userBaseDir.empty()) return;
    for (const PackType& pt : PACK_TYPES) {
        syncPackDirectories(userBaseDir + "\\" + pt.subdir,
                            std::string(DISCOVERY_DIR) + "\\" + pt.subdir,
                            pt.label, pt.media);
    }
}

// Copy each <root>/<name>/ folder from the user override directory into the plugin
// data directory. One level of nesting only, which is the whole format for every
// asset type this serves -- a theme, a gamepad pack, a pit board and a spotter voice
// are each a flat folder of payload files plus a <name>.ini, so this deliberately
// does not recurse further.
void AssetManager::syncPackDirectories(const std::string& sourceRoot, const std::string& destRoot,
                                       const char* label, const char* mediaPattern) {
    const std::string searchPath = sourceRoot + "\\*";
    WIN32_FIND_DATAA findData;
    HANDLE hFind = FindFirstFileA(searchPath.c_str(), &findData);
    if (hFind == INVALID_HANDLE_VALUE) return;   // no user themes; not an error

    CreateDirectoryA(destRoot.c_str(), NULL);

    int synced = 0;
    do {
        if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        const std::string name = findData.cFileName;
        if (name == "." || name == "..") continue;

        const std::string src = sourceRoot + "\\" + name;
        const std::string dst = destRoot + "\\" + name;
        CreateDirectoryA(dst.c_str(), NULL);
        if (mediaPattern) syncDirectory(src, dst, mediaPattern);
        syncDirectory(src, dst, "*.ini");
        // ONE fixed subfolder, not recursion: a theme's optional icon overrides. The
        // flat-folder rule above still holds for everything else -- this is a named
        // shape the format knows about, so the sync stays as auditable as it was.
        // Art-carrying types only: a spotter voice has no icons\ in its format, and
        // walking for one would widen what an audio pack may drop into the plugin
        // folder.
        if (mediaPattern && std::strcmp(mediaPattern, "*.tga") == 0) {
            const std::string iconSrc = src + "\\" + ICONS_SUBDIR;
            if (GetFileAttributesA(iconSrc.c_str()) != INVALID_FILE_ATTRIBUTES) {
                const std::string iconDst = dst + "\\" + ICONS_SUBDIR;
                CreateDirectoryA(iconDst.c_str(), NULL);
                syncDirectory(iconSrc, iconDst, "*.tga");
            }
        }
        ++synced;
    } while (FindNextFileA(hFind, &findData));

    FindClose(hFind);
    if (synced > 0) DEBUG_INFO_F("AssetManager: Synced %d user %s folder(s)", synced, label);
}

// A theme asked for a title band or a body card and has no card slice set to draw
// one with. Silence here is the same failure the unknown-key warning exists for, one
// level down: the key is spelled right, it parses, it is applied -- and the screen
// does not change, because hasThemedTitleBand()/hasThemedContentCard() both require
// hasCard(). Measured before this existed: a theme with `title-band = 1` and
// `content = 1` but no card_center.tga produced not one line of output.
//
// Gated on cardKeysSet, not on the values: titleBand DEFAULTS to true, so warning on
// the value would fire for every deliberately plain-framed theme, on every load.
static void warnIfCardWithoutInner(const ThemeAsset& theme) {
    if (!theme.cardKeysSet || theme.hasCard()) return;
    DEBUG_WARN_F("Theme '%s': [card] keys are set but the theme has no card slices, so "
                 "no title band or content card is drawn - add the card_ slice set "
                 "(card_center.tga plus card_corner_*.tga / card_edge_*.tga)",
                 theme.name.c_str());
}

// Scan themes/<name>/ for the three required sprites. A directory missing any of
// them is skipped with a warning rather than half-registered -- a theme that can
// only draw two of its three slices would render a visibly broken panel, and the
// user's setting would still name it.
//
// Sprite indices continue past the icon block (see discoverAssets), so the sw
// renderer's `firstIcon` boundary classes them as icons. That is harmless: the
// flag only picks a directory, and Renderer::tex() resolves path-style names
// (which theme sprites use) before consulting it.
void AssetManager::discoverThemes() {
    const std::string themesRoot = std::string(DISCOVERY_DIR) + "\\" + THEMES_SUBDIR;
    const std::string searchPath = themesRoot + "\\*";

    WIN32_FIND_DATAA findData;
    HANDLE hFind = FindFirstFileA(searchPath.c_str(), &findData);
    if (hFind == INVALID_HANDLE_VALUE) {
        DEBUG_INFO_F("AssetManager: No themes directory (%s) - themed backgrounds unavailable", themesRoot.c_str());
        return;
    }

    std::vector<std::string> dirs;
    do {
        if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        const std::string name = findData.cFileName;
        if (name == "." || name == "..") continue;
        dirs.push_back(name);
    } while (FindNextFileA(hFind, &findData));
    FindClose(hFind);

    std::sort(dirs.begin(), dirs.end());

    // Themes start after the icon block.
    int spriteIndex = m_firstIconSpriteIndex + static_cast<int>(m_icons.size());

    // A theme is three SLICE SETS of nine files each, named <set>_<part>.tga. The
    // set names are the theme ini's section names -- frame, card, button -- so the
    // file a skinner opens and the key that sizes it read the same.
    static const char* kCornerFiles[4] = {"corner_tl", "corner_tr", "corner_bl", "corner_br"};
    static const char* kEdgeFiles[4]   = {"edge_top", "edge_bottom", "edge_left", "edge_right"};

    for (const std::string& dir : dirs) {
        const std::string base = themesRoot + "\\" + dir + "\\";
        int w = 0, h = 0;

        auto exists = [&](const std::string& stem) {
            return readTgaDimensions(base + stem + ".tga", w, h);
        };

        if (!exists("frame_center")) {
            DEBUG_WARN_F("AssetManager: theme '%s' skipped - center.tga is required", dir.c_str());
            continue;
        }

        ThemeAsset theme;
        theme.name = dir;
        theme.displayName = generateDisplayName(dir);
        readThemeIni(themeIniPath(base, theme.name), theme);

        // Sprite indices are handed out as files are found, but a theme can still be
        // rejected below -- and HudManager only registers the sprites of themes that
        // made it into m_themes. So remember where this theme started and rewind on
        // rejection, or every LATER theme's indices shift by the dropped file count
        // and each one draws another theme's sprites.
        const int themeFirstSprite = spriteIndex;

        // Register a file once and hand back its sprite index. `spriteFiles` and the
        // index counter advance together, which is what keeps this in step with
        // HudManager's registration loop.
        auto add = [&](const std::string& stem) {
            theme.spriteFiles.push_back(stem);
            return spriteIndex++;
        };

        theme.centerSprite = add("frame_center");

        // A 9-slice is nine distinct files: center + four corners + four edges. There
        // is no shared corner.tga/edge.tga shorthand: every slice is drawn AS AUTHORED
        // (NineSlice::build emits one winding for all nine), so a single bitmap in all
        // four corners would only ever be right for art with no orientation at all.
        bool complete = true;
        for (int i = 0; i < 4; ++i) {
            const std::string fc = std::string("frame_") + kCornerFiles[i];
            const std::string fe = std::string("frame_") + kEdgeFiles[i];
            theme.cornerSprites[i] = exists(fc) ? add(fc) : 0;
            theme.edgeSprites[i]   = exists(fe) ? add(fe) : 0;
            if (theme.cornerSprites[i] == 0 || theme.edgeSprites[i] == 0) complete = false;
        }

        // CARD slice set, optional. Per-position like the frame set, and registered
        // AFTER it so a theme without card slices keeps exactly the sprite indices
        // it had before this existed.
        if (exists("card_center")) {
            theme.cardCenterSprite = add("card_center");
            for (int i = 0; i < 4; ++i) {
                const std::string ic = std::string("card_") + kCornerFiles[i];
                const std::string ie = std::string("card_") + kEdgeFiles[i];
                theme.cardCornerSprites[i] = exists(ic) ? add(ic) : 0;
                theme.cardEdgeSprites[i]   = exists(ie) ? add(ie) : 0;
            }
            // A partial card set would draw a card with holes in it; fall back to
            // the centre alone, which is a perfectly good flat band.
            for (int i = 0; i < 4; ++i) {
                if (theme.cardCornerSprites[i] == 0) theme.cardCornerSprites[i] = theme.cardCenterSprite;
                if (theme.cardEdgeSprites[i] == 0)   theme.cardEdgeSprites[i]   = theme.cardCenterSprite;
            }
        }

        // BUTTON slice set, optional, registered after the card set.
        if (exists("button_center")) {
            theme.buttonCenterSprite = add("button_center");
            for (int i = 0; i < 4; ++i) {
                const std::string bc = std::string("button_") + kCornerFiles[i];
                const std::string be = std::string("button_") + kEdgeFiles[i];
                theme.buttonCornerSprites[i] = exists(bc) ? add(bc) : 0;
                theme.buttonEdgeSprites[i]   = exists(be) ? add(be) : 0;
            }
            for (int i = 0; i < 4; ++i) {
                if (theme.buttonCornerSprites[i] == 0) theme.buttonCornerSprites[i] = theme.buttonCenterSprite;
                if (theme.buttonEdgeSprites[i] == 0)   theme.buttonEdgeSprites[i]   = theme.buttonCenterSprite;
            }
        }

        // ICON OVERRIDES: themes/<name>/icons/<icon>.tga, one sprite each, registered
        // after the slice sets so a theme without them keeps the indices it had before
        // this existed. Ordered by the BASE set so registration order is a property of
        // the install rather than of directory enumeration -- the same discipline the
        // slice order needs (see ThemeAsset::spriteFiles).
        //
        // A file that names no base icon is IGNORED, loudly: a theme may restyle the
        // vocabulary, never extend it (see ThemeAsset::iconOverrides), and silence
        // here would read as "my icon did not load" with nothing to go on.
        {
            const std::string iconDir = base + ICONS_SUBDIR + "\\";
            for (const IconAsset& icon : m_icons) {
                if (!readTgaDimensions(iconDir + icon.filename + ".tga", w, h)) continue;
                const int sprite = add(std::string(ICONS_SUBDIR) + "\\" + icon.filename);
                theme.iconOverrides[icon.filename] = sprite;
                theme.iconOverrideShape[sprite] =
                    static_cast<int>(&icon - &m_icons[0]) + 1;
            }
            const std::string iconSearch = iconDir + "*.tga";
            WIN32_FIND_DATAA iconFind;
            HANDLE hIcon = FindFirstFileA(iconSearch.c_str(), &iconFind);
            if (hIcon != INVALID_HANDLE_VALUE) {
                do {
                    if (iconFind.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                    std::string stem = iconFind.cFileName;
                    const size_t dot = stem.find_last_of('.');
                    if (dot != std::string::npos) stem = stem.substr(0, dot);
                    if (theme.iconOverrides.count(stem)) continue;
                    DEBUG_WARN_F("Theme '%s': icons\\%s.tga does not match any icon in the "
                                 "base set, so it is ignored - a theme may restyle an icon, "
                                 "not add one (put new glyphs in the user icons folder)",
                                 dir.c_str(), stem.c_str());
                } while (FindNextFileA(hIcon, &iconFind));
                FindClose(hIcon);
            }
        }

        if (!complete) {
            DEBUG_WARN_F("AssetManager: theme '%s' skipped - it needs the whole frame_ "
                         "slice set (frame_center.tga, frame_corner_tl/tr/bl/br.tga, "
                         "frame_edge_top/bottom/left/right.tga)", dir.c_str());
            spriteIndex = themeFirstSprite;   // rewind; nothing of this theme is registered
            continue;                         // dropped before push_back, so never half-registered
        }

        warnIfCardWithoutInner(theme);
        m_themes.push_back(theme);
    }

    bumpThemeGeneration();
    size_t overrides = 0;
    for (const ThemeAsset& t : m_themes) overrides += t.iconOverrides.size();
    DEBUG_INFO_F("AssetManager: Found %zu themes (%zu icon overrides)", m_themes.size(), overrides);
}

// The optional per-theme ini (see themeIniPath). It carries the WHOLE per-theme
// surface -- the three slice sizes, the tint and card switches with their per-family
// overrides, [colors] and [fonts] -- and nothing else. It could once additionally set
// any non-root layout key, forty of them; that vocabulary is gone (see
// core/layout_metrics.h for the measurement that removed it), which is why the
// unknown-key message below can name the entire surface in one line.
//
// Shares layoutForEachIniPairRaw() rather than running its own loop: the two parses
// have to agree on comments, whitespace and number handling, and a second copy is
// exactly the kind that drifts once one side is fixed.
//
// This is the walk itself, split out from readThemeIni() below only so that
// function's try/catch wraps something without re-indenting a hundred lines.
static void readThemeIniPairs(const std::string& iniPath, ThemeAsset& theme) {
    struct Ctx { ThemeAsset* t; const std::string* path; };
    Ctx ctx{ &theme, &iniPath };

    layoutForEachIniPairRaw(iniPath, [](const char* key, float parsed, const char* rawValue,
                                        bool numeric, void* raw) -> bool {
        Ctx& c = *static_cast<Ctx*>(raw);
        ThemeAsset& t = *c.t;

        // [colors] / [fonts] first: their values are not numbers, so they must be
        // answered before anything reaches the numeric path.
        if (std::strncmp(key, "colors.", 7) == 0) {
            const int slot = colorSlotFromName(key + 7);
            if (slot < 0) {
                DEBUG_WARN_F("Theme '%s': unknown colour slot '%s' - ignored (the slots are "
                             "primary, secondary, tertiary, muted, background, positive, "
                             "warning, neutral, negative, accent)", t.name.c_str(), key + 7);
                return false;
            }
            unsigned long abgr = 0;
            if (!PluginUtils::parseRgbHex(rawValue, abgr)) {
                DEBUG_WARN_F("Theme '%s': '%s=%s' is not an RGB hex colour - ignored "
                             "(write #rrggbb, e.g. #ff8800)", t.name.c_str(), key, rawValue);
                return false;
            }
            t.colors[static_cast<size_t>(slot)] = abgr;
            t.colorSet[static_cast<size_t>(slot)] = true;
            return true;
        }
        if (std::strncmp(key, "fonts.", 6) == 0) {
            const int cat = fontCategoryFromName(key + 6);
            if (cat < 0) {
                DEBUG_WARN_F("Theme '%s': unknown font category '%s' - ignored (the categories "
                             "are title, normal, strong, digits, marker, small)",
                             t.name.c_str(), key + 6);
                return false;
            }
            // The NAME is stored, not a resolved index: fonts are discovered
            // separately and a theme may name one that this install does not have.
            // FontConfig falls back when it cannot resolve it, which is the same
            // thing it does for a user's saved font.
            t.fonts[static_cast<size_t>(cat)] = rawValue;
            return true;
        }

        // THE BOX-MODEL KEYS, before the numeric guard: their values are CSS
        // shorthand ("2", "2 4", "1 2 3 4"), so a multi-number value never
        // reads as numeric and must be answered here. One helper for all
        // twelve; `border` additionally holds each side to whole cells (the
        // slice-lands-on-the-lattice argument at ThemeAsset::frameBorder),
        // while margin/padding accept fractions — half a cell of air is the
        // shipped default in the model, and air has no art to keep on a
        // lattice.
        //
        // Each key ALSO feeds its legacy scalar where the value is expressible
        // there (uniform sides; padding's vert/horiz pair; margin's t+b as the
        // visible gap), so the new vocabulary drives the pre-box-model layout
        // while the port is in flight. The bridge is one-directional: legacy
        // keys never populate the box terms.
        {
            struct BoxKey {
                const char* key;
                ThemeAsset::BoxTerm ThemeAsset::* term;
                bool border;       // whole-cells rule per side
                bool cardArt;      // uses card_*.tga → sets cardKeysSet
                bool scalar;       // one number only (no per-side shorthand)
                // Exempt from the whole-cells rule. The rule exists so a box's
                // reserve quantises to the grid and its CONTENT ROWS stay on the
                // lattice; a button has no rows -- it is one centred label in a
                // box of its own -- so nothing downstream of it can fall off a
                // lattice it never sat on. Without the exemption a bordered
                // button cannot be shorter than 2*border + 2*padding + 2 = 4
                // cells, which is above what Fluent, Primer and HIG specify --
                // the design systems this was calibrated against, whether or not
                // a theme for them ships -- and no value a theme can write
                // reaches their sizes.
                bool fracBorder;
            };
            static const BoxKey kBoxKeys[] = {
                // panel.gap is one number, not shorthand: junction-only air
                // between stacked children has no sides. (Unrelated to the
                // removed pre-box-model `[content] gap` spelling, which was a
                // section-seam scalar the content margins replaced.)
                { "panel.gap",       &ThemeAsset::boxPanelGap,       false, false, true, false },
                { "panel.border",    &ThemeAsset::boxPanelBorder,    true,  false, false, false },
                { "panel.padding",   &ThemeAsset::boxPanelPadding,   false, false, false, false },
                { "title.margin",    &ThemeAsset::boxTitleMargin,    false, false, false, false },
                { "title.border",    &ThemeAsset::boxTitleBorder,    true,  true,  false, false },
                { "title.padding",   &ThemeAsset::boxTitlePadding,   false, false, false, false },
                { "content.margin",  &ThemeAsset::boxContentMargin,  false, false, false, false },
                { "content.border",  &ThemeAsset::boxContentBorder,  true,  true,  false, false },
                { "content.padding", &ThemeAsset::boxContentPadding, false, false, false, false },
                { "button.margin",   &ThemeAsset::boxButtonMargin,   false, false, false, false },
                { "button.border",   &ThemeAsset::boxButtonBorder,   true,  false, false, true  },
                { "button.padding",  &ThemeAsset::boxButtonPadding,  false, false, false, false },
            };
            for (const BoxKey& bk : kBoxKeys) {
                if (std::strcmp(key, bk.key) != 0) continue;
                const PanelBox::Sides s = PanelBox::parseSides(rawValue);
                const double vals[4] = { s.t, s.r, s.b, s.l };
                for (const double v : vals) {
                    if (v < 0.0 || v > 12.0) {
                        DEBUG_WARN_F("Theme '%s': '%s=%s' has a side outside 0..12 cells - "
                                     "keeping the previous value", t.name.c_str(), key, rawValue);
                        return true;
                    }
                    const double rounded = std::floor(v + 0.5);
                    if (bk.border && !bk.fracBorder && std::fabs(v - rounded) >= 0.001) {
                        DEBUG_WARN_F("Theme '%s': '%s=%s' - a border is a slice size and "
                                     "must be whole cells (air terms may be fractional) - "
                                     "keeping the previous value", t.name.c_str(), key, rawValue);
                        return true;
                    }
                }
                const bool uniform = s.t == s.r && s.r == s.b && s.b == s.l;
                if (bk.scalar && !uniform) {
                    DEBUG_WARN_F("Theme '%s': '%s=%s' takes ONE number, not per-side "
                                 "shorthand - keeping the previous value",
                                 t.name.c_str(), key, rawValue);
                    return true;
                }
                ThemeAsset::BoxTerm& dst = t.*(bk.term);
                dst.v = s;
                dst.set = true;
                if (bk.cardArt) t.cardKeysSet = true;
                // The legacy bridge, per key. Uniform borders feed the per-set
                // scalar; [panel] padding feeds the x/y overrides from its
                // horizontal/vertical pairs; [content] margin's facing pair is
                // the visible gap two carded boxes keep (the seam is the SUM).
                const auto f = [](double v2) { return static_cast<float>(v2); };
                if (bk.term == &ThemeAsset::boxPanelBorder && uniform) t.frameBorder = f(s.t);
                if (bk.term == &ThemeAsset::boxTitleBorder && uniform) t.titleBorderOverride = f(s.t);
                if (bk.term == &ThemeAsset::boxContentBorder && uniform) t.cardBorder = f(s.t);
                if (bk.term == &ThemeAsset::boxButtonBorder && uniform) t.buttonBorder = f(s.t);
                if (bk.term == &ThemeAsset::boxPanelPadding) {
                    if (s.l == s.r) t.panelPaddingXOverride = f(s.l);
                    if (s.t == s.b) t.panelPaddingYOverride = f(s.t);
                }
                if (bk.term == &ThemeAsset::boxContentMargin && s.t == s.b)
                    t.sectionGapOverride = f(s.t + s.b);
                return true;
            }
        }

        // Everything below is numeric. A non-number here is a typo, and saying so is
        // the whole reason this walk sees non-numeric lines at all.
        if (!numeric) {
            DEBUG_WARN_F("Theme '%s': '%s=%s' is not a number - ignored",
                         t.name.c_str(), key, rawValue);
            return false;
        }

        // (The box keys above are the WHOLE per-side layout surface. The
        // pre-box-model spellings -- [frame] border, [card] border,
        // [content] gap, [panel] padding-x/-y -- are gone rather than aliased:
        // nothing shipped with them, and an unknown-key warning is the honest
        // answer. The scalar fields they fed remain, fed by the box keys'
        // legacy bridge above until the last own-geometry panel migrates.)
        if (std::strcmp(key, "frame.tint") == 0)  { t.tintable = (parsed != 0.0f); return true; }
        if (std::strcmp(key, "card.title-band") == 0) { t.titleBand = (parsed != 0.0f); t.cardKeysSet = true; return true; }
        // PER-FAMILY overrides, one pair per PanelKind. The plain key above is the
        // default for all three; these say "except for this family". Stored as a
        // tri-state so an absent key inherits rather than reading as an explicit 0.
        {
            struct { const char* key; int kind; bool band; } kFamily[] = {
                { "card.hud-title-band",      0, true  }, { "card.hud-content",      0, false },
                { "card.widget-title-band",   1, true  }, { "card.widget-content",   1, false },
                { "card.settings-title-band", 2, true  }, { "card.settings-content", 2, false },
            };
            for (const auto& f : kFamily) {
                if (std::strcmp(key, f.key) != 0) continue;
                (f.band ? t.titleBandFor : t.contentCardFor)[f.kind] = (parsed != 0.0f) ? 1 : 0;
                t.cardKeysSet = true;
                return true;
            }
        }
        if (std::strcmp(key, "card.content") == 0)    { t.contentCard = (parsed != 0.0f); t.cardKeysSet = true; return true; }

        // Everything a theme may say is handled above, so reaching here means a
        // typo or a stale name. Silence was the wrong default: a key that does
        // nothing looks exactly like a key that does nothing YET, and the whole point
        // of a reloadable file is a fast edit-look loop -- one that a misspelling
        // turns into staring at an unchanged screen.
        //
        // The list in the message is the WHOLE per-theme surface. It used to fall
        // through to the layout vocabulary here, forty more keys that no shipped theme
        // ever set a line of.
        DEBUG_WARN_F("Theme '%s': unknown key '%s' - ignored (a theme sets the box keys "
                     "[panel] border/padding and [title]/[content]/[button] "
                     "margin/border/padding in CSS shorthand, plus [frame] tint, "
                     "[card] title-band/content and the per-family "
                     "{hud,widget,settings}-{title-band,content}, "
                     "[colors] and [fonts])", t.name.c_str(), key);
        return false;
    }, &ctx);
}

void AssetManager::readThemeIni(const std::string& iniPath, ThemeAsset& theme) {
    // The declaration promises this never throws, and until this guard existed that
    // was an OBSERVATION about the parser rather than a guarantee: the walk uses
    // strtof and never std::stof, so no conversion throws today -- but a theme file
    // is user-editable, this runs inside asset discovery, and one exception here
    // aborts discovery for EVERY asset type, leaving the plugin with no fonts. That
    // is the same failure the base-section parseColorHex bug caused for the settings
    // load, which is why CLAUDE.md makes exception-guarding an INI parse site a rule
    // rather than a judgement call. The catch keeps whatever the file applied before
    // the throw; a half-read theme is a cosmetic problem, no assets is not.
    try {
        readThemeIniPairs(iniPath, theme);
    } catch (const std::exception& e) {
        DEBUG_WARN_F("Theme '%s': failed to read %s (%s) - keeping what was applied",
                     theme.name.c_str(), iniPath.c_str(), e.what());
    } catch (...) {
        DEBUG_WARN_F("Theme '%s': failed to read %s - keeping what was applied",
                     theme.name.c_str(), iniPath.c_str());
    }

}

std::string AssetManager::themeIniPath(const std::string& dir, const std::string& themeName) {
    return dir + themeName + ".ini";
}

// The pad ini's whole surface. Sections scope the short property names exactly as
// the theme ini does ([size] w means one thing, [offset] left-stick-x another), and
// it shares layoutForEachIniPairRaw for the same reason readThemeIni does: two
// hand-edited parsers that disagree about comments or whitespace is a bug waiting
// for whichever one is fixed first.
//
// Every key is optional. A pad that states nothing renders at the built-in
// PadGeometry defaults rather than at zero -- silence must not mean "put every
// button in the top-left corner".
static void readGamepadIniPairs(const std::string& iniPath, GamepadAsset& pad) {
    struct Ctx { GamepadAsset* p; };
    Ctx ctx{ &pad };

    layoutForEachIniPairRaw(iniPath, [](const char* key, float parsed, const char* rawValue,
                                        bool numeric, void* raw) -> bool {
        GamepadAsset& p = *static_cast<Ctx*>(raw)->p;

        // [pad] name and base are the two non-numeric keys, answered before
        // anything reaches the numeric path below.
        if (std::strcmp(key, "pad.name") == 0) {
            if (rawValue && *rawValue) p.displayName = rawValue;
            return true;
        }
        if (std::strcmp(key, "pad.base") == 0) {
            if (rawValue && *rawValue) p.baseName = rawValue;
            return true;
        }

        if (!numeric) {
            DEBUG_WARN_F("Gamepad '%s': key '%s' is not a number - ignored",
                         p.name.c_str(), key);
            return true;
        }

        // The key->field table lives in gamepad_geometry.h so a unit test can drive
        // the same mapping without a filesystem; see the note there.
        if (GamepadLayout::applyPadGeometryIni(p.geometry, key, parsed)) return true;

        DEBUG_WARN_F("Gamepad '%s': unknown key '%s' - ignored (the sections are "
                     "[pad] name/base, [art] width/height, [size], [offset] and [spacing])",
                     p.name.c_str(), key);
        return true;
    }, &ctx);
}

// The board ini's surface. Same shape as readGamepadIniPairs above -- [board] name
// is the one non-numeric key, everything else goes through the header's table.
static void readPitboardIniPairs(const std::string& iniPath, PitboardAsset& board) {
    struct Ctx { PitboardAsset* b; };
    Ctx ctx{ &board };

    layoutForEachIniPairRaw(iniPath, [](const char* key, float parsed, const char* rawValue,
                                        bool numeric, void* raw) -> bool {
        PitboardAsset& b = *static_cast<Ctx*>(raw)->b;

        if (std::strcmp(key, "board.name") == 0) {
            if (rawValue && *rawValue) b.displayName = rawValue;
            return true;
        }
        if (std::strcmp(key, "board.base") == 0) {
            if (rawValue && *rawValue) b.baseName = rawValue;
            return true;
        }
        // [text] color is authored as #rrggbb, same as a theme's colours and for
        // the same reason (plugin_utils.h at parseRgbHex): the game's ABGR
        // packing stays out of hand-written files. Not the numeric path - a
        // 32-bit colour word does not survive the float parse.
        if (std::strcmp(key, "text.color") == 0) {
            unsigned long abgr = 0;
            if (rawValue && PluginUtils::parseRgbHex(rawValue, abgr)) {
                b.geometry.textColor = static_cast<uint32_t>(abgr);
            } else {
                DEBUG_WARN_F("Pitboard '%s': 'text.color=%s' is not an RGB hex colour "
                             "- ignored (write #rrggbb, e.g. #ffffff)",
                             b.name.c_str(), rawValue ? rawValue : "");
            }
            return true;
        }

        if (!numeric) {
            DEBUG_WARN_F("Pitboard '%s': key '%s' is not a number - ignored",
                         b.name.c_str(), key);
            return true;
        }

        if (PitboardLayout::applyBoardGeometryIni(b.geometry, key, parsed)) return true;

        DEBUG_WARN_F("Pitboard '%s': unknown key '%s' - ignored (the sections are "
                     "[board] name/base, [art] width/height, [text] color and [offset])",
                     b.name.c_str(), key);
        return true;
    }, &ctx);
}

// BOTH handlers, like readThemeIni above. A bare `catch (const std::exception&)`
// leaves a non-std throw to escape into asset discovery, which is the failure that
// guard exists to stop -- and these three sit at the same trust boundary (a
// hand-edited ini), so they must not differ in how completely they hold it.
static void readPitboardIni(const std::string& iniPath, PitboardAsset& board) {
    try {
        readPitboardIniPairs(iniPath, board);
    } catch (const std::exception& e) {
        DEBUG_WARN_F("Pitboard '%s': failed to read '%s': %s",
                     board.name.c_str(), iniPath.c_str(), e.what());
    } catch (...) {
        DEBUG_WARN_F("Pitboard '%s': failed to read '%s'",
                     board.name.c_str(), iniPath.c_str());
    }
}

static void readGamepadIni(const std::string& iniPath, GamepadAsset& pad) {
    // File I/O on a hand-edited file: one malformed number must not abort the whole
    // asset scan (the naked-std::stoul lesson from the settings loader).
    try {
        readGamepadIniPairs(iniPath, pad);
    } catch (const std::exception& e) {
        DEBUG_WARN_F("Gamepad '%s': failed to read '%s': %s",
                     pad.name.c_str(), iniPath.c_str(), e.what());
    } catch (...) {
        DEBUG_WARN_F("Gamepad '%s': failed to read '%s'",
                     pad.name.c_str(), iniPath.c_str());
    }
}

// Peek ONE string key out of a pack ini before the pack is built -- discovery
// needs to know whether a directory is a standalone pack or a skin before it
// decides which phase handles it, and building the whole asset to learn one
// key would run the geometry mapping twice for every pack.
static std::string readPackStringKey(const std::string& iniPath, const char* fullKey) {
    struct Ctx { const char* key; std::string value; };
    Ctx ctx{ fullKey, {} };
    try {
        layoutForEachIniPairRaw(iniPath, [](const char* key, float, const char* rawValue,
                                            bool, void* raw) -> bool {
            Ctx& c = *static_cast<Ctx*>(raw);
            if (std::strcmp(key, c.key) == 0 && rawValue && *rawValue) c.value = rawValue;
            return true;
        }, &ctx);
    } catch (...) {
        // A malformed ini is the pack's own problem, answered (with a warning)
        // when the full reader runs; the peek just reports "no base".
    }
    return ctx.value;
}

// See the declaration. One body for both pack types, because they differ only in
// which ini reader they hand the file to -- the reset, the display name and the
// path are identical, and writing them out twice is what let the board's copy go
// missing entirely.
//
// The geometry is reset to its own default first, so a key DELETED from a pack's
// ini goes back to the built-in value instead of keeping its last override -- the
// same rule the theme loop above follows, and the reason a reload is an authoring
// tool rather than a one-way ratchet. Sprite indices are NOT touched: they reach
// the game once at init and are held by number everywhere after.
void AssetManager::seedPitboardArt(const std::string& dir, PitboardAsset& board) {
    int w = 0, h = 0;
    if (readTgaDimensions(dir + PitboardSprite::kStems[PitboardSprite::BACKGROUND] + ".tga", w, h)
        && w > 0 && h > 0) {
        board.geometry.artWidth = static_cast<float>(w);
        board.geometry.artHeight = static_cast<float>(h);
    }
}

template <typename Pack>
void AssetManager::reloadPackLayouts(std::vector<Pack>& packs, const char* subdir,
                                     void (*readIni)(const std::string&, Pack&),
                                     void (*seed)(const std::string&, Pack&)) {
    for (Pack& pack : packs) {
        // RESET, SEED, READ -- the same three steps discovery takes, in the same
        // order, because a step discovery takes and the reload does not is a setting
        // the RELOAD_CONFIG hotkey silently deletes. The seed is where that bit:
        // resetting to the struct's defaults threw away the pit board's TGA-derived
        // art size, which only discovery knew how to recover.
        //
        // A SKIN resets to its BASE's geometry, not the built-ins -- and the base
        // is guaranteed already re-read by this loop, because discovery appends
        // all standalone packs before any skin. (Changing `base` in the ini at
        // runtime is not honoured: the base binding, like the sprite files, is
        // discovery-time; a reload re-reads layout only.)
        pack.geometry = decltype(pack.geometry){};
        if (!pack.baseName.empty()) {
            for (const Pack& candidate : packs) {
                if (candidate.name == pack.baseName && candidate.baseName.empty()) {
                    pack.geometry = candidate.geometry;
                    break;
                }
            }
        }
        pack.displayName = generateDisplayName(pack.name);
        const std::string dir = std::string(DISCOVERY_DIR) + "\\" + subdir + "\\"
                              + pack.name + "\\";
        if (seed) seed(dir, pack);
        readIni(dir + pack.name + ".ini", pack);
    }
}

void AssetManager::reloadThemeLayouts() {
    bumpThemeGeneration();   // every cached ThemeAsset* may now point at stale metrics
    // Re-copy the user's packs FIRST. A pack is authored in the user's own Documents
    // folder (savePath\mxbmrp3\<type>\<name>\), and that copy only reached the plugin
    // folder at startup -- so before this, editing the file you are meant to edit and
    // pressing RELOAD_CONFIG did nothing at all, silently, because the hotkey re-read
    // the stale copy under plugins\.
    //
    // .tga TOO, now that one surface can act on it. Sprite indices still reach the
    // GAME only once at init, so changed art cannot appear there without a restart --
    // this used to be the reason for skipping .tga entirely, since copying a file that
    // changes nothing on screen is worse than not copying it. What changed is the
    // COMPANION window: its software renderer opens each .tga itself, so
    // CompanionWindow::requestArtReload() makes the new bytes visible on the next
    // frame. Copying them across is the half of that the game thread owns.
    //
    // Spotter voices ride the same call: the pack author's edit-reload-listen loop
    // is the same loop as the theme author's, and SpotterManager::reloadCuePack()
    // runs right after this on the hotkey — it re-reads the copy under plugins\,
    // so without this pass it would re-read the stale one.
    syncAllPackTypes(m_userBaseDir);

    for (ThemeAsset& theme : m_themes) {
        // Reset to the ThemeAsset defaults first, so a key DELETED from a theme's file
        // goes back to the built-in value instead of keeping its last override.
        // Sprite indices are NOT touched -- they are resolved at discovery and held by
        // number everywhere after.
        const ThemeAsset fresh;
        theme.frameBorder = fresh.frameBorder;
        theme.cardBorder  = fresh.cardBorder;
        theme.titleBorderOverride   = fresh.titleBorderOverride;
        theme.sectionGapOverride    = fresh.sectionGapOverride;
        theme.panelPaddingXOverride = fresh.panelPaddingXOverride;
        theme.panelPaddingYOverride = fresh.panelPaddingYOverride;
        theme.buttonBorder = fresh.buttonBorder;
        // The box terms need the same reset as the legacy scalars above, and for a
        // sharper reason: the two vocabularies describe the SAME frame. Resetting only
        // the scalar half means a deleted `border` key leaves the scalar back at its
        // built-in while the box term still carries the override, and the panel is then
        // drawn from two sources that disagree about its thickness.
        theme.boxPanelBorder   = fresh.boxPanelBorder;
        theme.boxPanelPadding  = fresh.boxPanelPadding;
        theme.boxPanelGap      = fresh.boxPanelGap;
        theme.boxTitleMargin   = fresh.boxTitleMargin;
        theme.boxTitleBorder   = fresh.boxTitleBorder;
        theme.boxTitlePadding  = fresh.boxTitlePadding;
        theme.boxContentMargin  = fresh.boxContentMargin;
        theme.boxContentBorder  = fresh.boxContentBorder;
        theme.boxContentPadding = fresh.boxContentPadding;
        theme.boxButtonMargin  = fresh.boxButtonMargin;
        theme.boxButtonBorder  = fresh.boxButtonBorder;
        theme.boxButtonPadding = fresh.boxButtonPadding;
        theme.tintable    = fresh.tintable;
        theme.titleBand   = fresh.titleBand;
        theme.contentCard = fresh.contentCard;
        for (int i = 0; i < 3; ++i) {
            theme.titleBandFor[i]   = fresh.titleBandFor[i];
            theme.contentCardFor[i] = fresh.contentCardFor[i];
        }
        // ...and the palette and font set, for the same reason and with a sharper
        // edge: readThemeIni only ever sets colorSet[i] = TRUE, so without this a
        // deleted [colors] line keeps applying its last value forever. These are the
        // two sections a skinner iterates on hardest, so they were the two where
        // "delete a line and the built-in comes back" mattered most and worked least.
        theme.colors   = fresh.colors;
        theme.colorSet = fresh.colorSet;
        theme.fonts    = fresh.fonts;
        const std::string dir = std::string(DISCOVERY_DIR) + "\\" + THEMES_SUBDIR + "\\"
                              + theme.name + "\\";
        theme.cardKeysSet = false;
        readThemeIni(themeIniPath(dir, theme.name), theme);
        warnIfCardWithoutInner(theme);
    }
    // Pad and board packs get the same treatment, and for the same reason: placing
    // buttons on a controller photograph -- or rows on a board -- is an
    // iterate-and-look loop, so re-reading the ini without a game restart is most of
    // what makes authoring one bearable. Sprite indices are untouched here too; only
    // the numbers are re-read.
    //
    // ONE CALL PER PACK TYPE, through one helper, because the two loops were written
    // out by hand and only one of them existed: the board's was simply missing, so
    // RELOAD_CONFIG re-read a pad's ini and silently ignored a board's -- while the
    // sync above copied the board files across and README promised the hotkey worked.
    // The count line below is what should have caught it (it passed m_pitboards.size()
    // to a format string with two conversions, so the extra argument went nowhere).
    // Written as one generic helper so a third pack type is a call, not a copy.
    reloadPackLayouts(m_gamepads, GAMEPADS_SUBDIR, &readGamepadIni);
    reloadPackLayouts(m_pitboards, PITBOARDS_SUBDIR, &readPitboardIni, &seedPitboardArt);

    DEBUG_INFO_F("AssetManager: reloaded layout for %zu theme(s), %zu gamepad(s), %zu pitboard(s)",
                 m_themes.size(), m_gamepads.size(), m_pitboards.size());
}

const ThemeAsset* AssetManager::getThemeByName(const std::string& name) const {
    for (const ThemeAsset& t : m_themes) {
        if (t.name == name) return &t;
    }
    return nullptr;
}

// ============================================================================
// Gamepad packs
// ============================================================================

// Scan gamepads/<name>/ for the full sprite set named by GamepadSprite::kStems.
//
// ALL of them, or the pack is skipped with a warning rather than half-registered --
// the same line discoverThemes() takes on the frame set, and for the same reason: a
// pad missing its d-pad art would draw a visible hole while the user's setting still
// named it. It also keeps every accepted pack exactly COUNT sprites wide, which is
// what lets HudManager walk kStems and stay in step without a per-pack file list.
// List the immediate subdirectories of a pack root, sorted, so discovery order is
// stable across runs and filesystems. Shared by the two nested pack types; the
// per-type work (which stems are required, which ini reader, what the asset holds)
// stays in each discoverX because that is where they genuinely differ.
//
// Returns false when the root does not exist, which is not an error -- an install
// without that pack type simply has none.
static bool listPackDirs(const std::string& root, std::vector<std::string>& dirs) {
    WIN32_FIND_DATAA findData;
    HANDLE hFind = FindFirstFileA((root + "\\*").c_str(), &findData);
    if (hFind == INVALID_HANDLE_VALUE) return false;
    do {
        if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        const std::string name = findData.cFileName;
        if (name == "." || name == "..") continue;
        dirs.push_back(name);
    } while (FindNextFileA(hFind, &findData));
    FindClose(hFind);
    std::sort(dirs.begin(), dirs.end());
    return true;
}

void AssetManager::discoverGamepads() {
    const std::string root = std::string(DISCOVERY_DIR) + "\\" + GAMEPADS_SUBDIR;
    std::vector<std::string> dirs;
    if (!listPackDirs(root, dirs)) {
        DEBUG_WARN_F("AssetManager: No gamepads directory (%s) - the gamepad widget "
                     "has no art to draw", root.c_str());
        return;
    }

    // Packs start after the theme block, which itself starts after the icons. Themes
    // are registered last in setupDefaultResources today, so pads follow them.
    int spriteIndex = m_firstIconSpriteIndex + static_cast<int>(m_icons.size());
    for (const ThemeAsset& t : m_themes) spriteIndex += static_cast<int>(t.spriteFiles.size());

    // TWO PHASES, because `base` may name a pack that sorts after the skin
    // ("aaa-dark" over "xbox"): standalone packs first, then everything that
    // declares a base, resolved against the standalone set. Skins therefore
    // sort after their bases in the cycle order, which also reads sensibly.
    //
    // Phase 1 -- standalone packs, the all-or-nothing rule unchanged: every
    // stem must resolve to a readable .tga before any index is handed out, so
    // a rejected pack cannot shift the indices of the packs after it (the
    // rewind-on-rejection bug discoverThemes() documents, avoided by not
    // allocating in the first place).
    for (const std::string& dir : dirs) {
        const std::string base = root + "\\" + dir + "\\";
        if (!readPackStringKey(base + dir + ".ini", "pad.base").empty()) continue;

        bool complete = true;
        int w = 0, h = 0;
        for (const char* stem : GamepadSprite::kStems) {
            if (readTgaDimensions(base + stem + ".tga", w, h)) continue;
            DEBUG_WARN_F("AssetManager: gamepad '%s' skipped - missing %s.tga (a pack "
                         "with no `base` needs the whole set of %d sprites)",
                         dir.c_str(), stem, static_cast<int>(GamepadSprite::COUNT));
            complete = false;
            break;
        }
        if (!complete) continue;

        GamepadAsset pad;
        pad.name = dir;
        pad.displayName = generateDisplayName(dir);
        readGamepadIni(base + dir + ".ini", pad);

        for (int i = 0; i < GamepadSprite::COUNT; ++i) pad.sprites[i] = spriteIndex++;

        DEBUG_INFO_F("AssetManager: Found gamepad '%s' (%s), sprites %d-%d",
                     pad.name.c_str(), pad.displayName.c_str(),
                     pad.sprites[0], pad.sprites[GamepadSprite::COUNT - 1]);
        m_gamepads.push_back(std::move(pad));
    }
    const size_t standaloneGamepads = m_gamepads.size();

    // Phase 2 -- skins. Each stem resolves to the skin's own file when present,
    // else the base's (complete by phase 1), so the resolved set is always
    // whole and the no-half-registered-pack rule survives the feature.
    for (const std::string& dir : dirs) {
        const std::string skinDir = root + "\\" + dir + "\\";
        const std::string baseKey = readPackStringKey(skinDir + dir + ".ini", "pad.base");
        if (baseKey.empty()) continue;

        // The base must be a phase-1 pack: present, and baseless itself. One
        // level only -- a chain would make "what does this pack draw" require
        // walking a graph, and a cycle would never terminate.
        const GamepadAsset* basePack = nullptr;
        for (size_t i = 0; i < standaloneGamepads; ++i) {
            if (m_gamepads[i].name == baseKey) { basePack = &m_gamepads[i]; break; }
        }
        if (!basePack) {
            DEBUG_WARN_F("AssetManager: gamepad '%s' skipped - base '%s' is not a "
                         "standalone pack (a base cannot itself declare a base)",
                         dir.c_str(), baseKey.c_str());
            continue;
        }

        GamepadAsset pad;
        pad.name = dir;
        pad.displayName = generateDisplayName(dir);
        pad.baseName = baseKey;
        // The base's geometry is the starting point; the skin's ini then
        // overrides only what it states -- the sparse rule, aimed at the base
        // instead of the built-ins.
        pad.geometry = basePack->geometry;
        readGamepadIni(skinDir + dir + ".ini", pad);

        int w = 0, h = 0;
        for (int i = 0; i < GamepadSprite::COUNT; ++i) {
            pad.spriteFromBase[i] =
                !readTgaDimensions(skinDir + GamepadSprite::kStems[i] + ".tga", w, h);
            pad.sprites[i] = spriteIndex++;
        }

        DEBUG_INFO_F("AssetManager: Found gamepad skin '%s' (%s) over '%s', sprites %d-%d",
                     pad.name.c_str(), pad.displayName.c_str(), baseKey.c_str(),
                     pad.sprites[0], pad.sprites[GamepadSprite::COUNT - 1]);
        m_gamepads.push_back(std::move(pad));
    }

    DEBUG_INFO_F("AssetManager: Found %zu gamepad pack(s)", m_gamepads.size());
}

const GamepadAsset* AssetManager::getGamepadByName(const std::string& name) const {
    for (const GamepadAsset& p : m_gamepads) {
        if (p.name == name) return &p;
    }
    return nullptr;
}

const GamepadAsset* AssetManager::getDefaultGamepad() const {
    if (const GamepadAsset* shipped = getGamepadByName(DEFAULT_GAMEPAD)) return shipped;
    // The shipped pack was deleted. Any pad beats none, and discovery is sorted, so
    // this is at least stable across runs.
    return m_gamepads.empty() ? nullptr : &m_gamepads.front();
}

std::string AssetManager::getGamepadPath(const std::string& packName, const char* stem) const {
    return std::string(RESOURCE_DIR) + "\\" + GAMEPADS_SUBDIR + "\\" + packName + "\\" + stem + ".tga";
}

// Scan pitboards/<name>/ for the stems named by PitboardSprite::kStems. All of
// them or the pack is skipped, same rule discoverGamepads() and discoverThemes()
// apply, and for the same reason: a board with no artwork would draw an empty
// panel while the user's setting still named it.
void AssetManager::discoverPitboards() {
    const std::string root = std::string(DISCOVERY_DIR) + "\\" + PITBOARDS_SUBDIR;
    std::vector<std::string> dirs;
    if (!listPackDirs(root, dirs)) {
        DEBUG_WARN_F("AssetManager: No pitboards directory (%s) - the pitboard has no "
                     "art to draw", root.c_str());
        return;
    }

    // Board packs sit past the gamepad block, which sits past the themes, which sit
    // past the icons -- one contiguous run per type, appended in the order
    // setupDefaultResources registers them.
    int spriteIndex = m_firstIconSpriteIndex + static_cast<int>(m_icons.size());
    for (const ThemeAsset& t : m_themes) spriteIndex += static_cast<int>(t.spriteFiles.size());
    spriteIndex += static_cast<int>(m_gamepads.size()) * GamepadSprite::COUNT;

    // Two phases, same shape and same reasons as discoverGamepads(): standalone
    // boards first, then skins resolved against them.
    for (const std::string& dir : dirs) {
        const std::string base = root + "\\" + dir + "\\";
        if (!readPackStringKey(base + dir + ".ini", "board.base").empty()) continue;

        // Verify the whole set BEFORE handing out any index, so a rejected pack
        // cannot shift the indices of the packs after it.
        bool complete = true;
        int w = 0, h = 0;
        for (const char* stem : PitboardSprite::kStems) {
            if (readTgaDimensions(base + stem + ".tga", w, h)) continue;
            DEBUG_WARN_F("AssetManager: pitboard '%s' skipped - missing %s.tga",
                         dir.c_str(), stem);
            complete = false;
            break;
        }
        if (!complete) continue;

        PitboardAsset board;
        board.name = dir;
        board.displayName = generateDisplayName(dir);
        // The .tga's REAL dimensions are the honest default for the aspect, so a
        // pack that states no [art] block still draws its own art undistorted --
        // the ini only has to say so when the author wants something else.
        //
        // Through the shared seed, not inline: this was written out here only, so the
        // reload path reset the geometry and had no way to recover it (see
        // seedPitboardArt). One implementation, reached from both.
        seedPitboardArt(base, board);
        readPitboardIni(base + dir + ".ini", board);

        for (int i = 0; i < PitboardSprite::COUNT; ++i) board.sprites[i] = spriteIndex++;

        DEBUG_INFO_F("AssetManager: Found pitboard '%s' (%s), art %.0fx%.0f, sprite %d",
                     board.name.c_str(), board.displayName.c_str(),
                     board.geometry.artWidth, board.geometry.artHeight,
                     board.sprites[PitboardSprite::BACKGROUND]);
        m_pitboards.push_back(std::move(board));
    }
    const size_t standaloneBoards = m_pitboards.size();

    for (const std::string& dir : dirs) {
        const std::string skinDir = root + "\\" + dir + "\\";
        const std::string baseKey = readPackStringKey(skinDir + dir + ".ini", "board.base");
        if (baseKey.empty()) continue;

        const PitboardAsset* basePack = nullptr;
        for (size_t i = 0; i < standaloneBoards; ++i) {
            if (m_pitboards[i].name == baseKey) { basePack = &m_pitboards[i]; break; }
        }
        if (!basePack) {
            DEBUG_WARN_F("AssetManager: pitboard '%s' skipped - base '%s' is not a "
                         "standalone pack (a base cannot itself declare a base)",
                         dir.c_str(), baseKey.c_str());
            continue;
        }

        PitboardAsset board;
        board.name = dir;
        board.displayName = generateDisplayName(dir);
        board.baseName = baseKey;
        // Base geometry first (which carries the base art's seeded dimensions);
        // then the skin's own art re-seeds ONLY if the skin brings its own file,
        // so swapped-in art at a different aspect still draws undistorted; then
        // the skin's ini keys override what they state.
        board.geometry = basePack->geometry;
        seedPitboardArt(skinDir, board);
        readPitboardIni(skinDir + dir + ".ini", board);

        int w = 0, h = 0;
        for (int i = 0; i < PitboardSprite::COUNT; ++i) {
            board.spriteFromBase[i] =
                !readTgaDimensions(skinDir + PitboardSprite::kStems[i] + ".tga", w, h);
            board.sprites[i] = spriteIndex++;
        }

        DEBUG_INFO_F("AssetManager: Found pitboard skin '%s' (%s) over '%s', art %.0fx%.0f, sprite %d",
                     board.name.c_str(), board.displayName.c_str(), baseKey.c_str(),
                     board.geometry.artWidth, board.geometry.artHeight,
                     board.sprites[PitboardSprite::BACKGROUND]);
        m_pitboards.push_back(std::move(board));
    }

    DEBUG_INFO_F("AssetManager: Found %zu pitboard pack(s)", m_pitboards.size());
}

const PitboardAsset* AssetManager::getPitboardByName(const std::string& name) const {
    for (const PitboardAsset& b : m_pitboards) {
        if (b.name == name) return &b;
    }
    return nullptr;
}

const PitboardAsset* AssetManager::getDefaultPitboard() const {
    if (const PitboardAsset* shipped = getPitboardByName(DEFAULT_PITBOARD)) return shipped;
    return m_pitboards.empty() ? nullptr : &m_pitboards.front();
}

std::string AssetManager::getPitboardPath(const std::string& packName, const char* stem) const {
    return std::string(RESOURCE_DIR) + "\\" + PITBOARDS_SUBDIR + "\\" + packName + "\\" + stem + ".tga";
}

std::string AssetManager::getThemePath(const std::string& themeName, const char* part) const {
    return std::string(RESOURCE_DIR) + "\\" + THEMES_SUBDIR + "\\" + themeName + "\\" + part + ".tga";
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

int AssetManager::getFontIndexByName(const char* name) const {
    MXB_COUNT_CALL(FONT_INDEX_BY_NAME);
    if (!name) return 0;
    // Compared without building a std::string. m_fonts is a handful of entries and
    // this is the hot path; a map keyed on const char* would need its own comparator
    // for no measurable gain over the scan getFontByName already does.
    for (const FontAsset& f : m_fonts) {
        if (f.filename == name) return f.fontIndex;
    }
    return 0;
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

int AssetManager::getBaseIconSpriteIndex(const std::string& name) const {
    auto it = m_iconNameToIndex.find(name);
    if (it != m_iconNameToIndex.end()) {
        return m_icons[it->second].spriteIndex;
    }
    return 0;
}

// The theme whose icons apply right now, memoised against themeGeneration().
//
// getThemeByName() is a linear scan over the theme list and this sits on the icon
// path -- which a table HUD walks once per drawn row -- so it is cached exactly the
// way BaseHud caches its own resolved theme, and for the same measured reason.
const ThemeAsset* AssetManager::activeIconTheme() const {
    const unsigned int gen = themeGeneration();
    if (m_iconThemeCacheGen == gen) return m_iconThemeCache;
    const std::string& sel = UiConfig::getInstance().getThemeName();
    m_iconThemeCache = sel.empty() ? nullptr : getThemeByName(sel);
    m_iconThemeCacheGen = gen;
    return m_iconThemeCache;
}

int AssetManager::getIconSpriteIndex(const std::string& name) const {
    // Theme first, base second -- and per NAME, so a theme that overrides three
    // icons inherits the rest rather than having to ship all 112.
    const ThemeAsset* theme = activeIconTheme();
    return IconResolve::spriteForName(name, getBaseIconSpriteIndex(name),
                                      theme ? &theme->iconOverrides : nullptr);
}

int AssetManager::shapeIndexForSprite(int spriteIndex) const {
    const ThemeAsset* theme = activeIconTheme();
    return IconResolve::shapeForSprite(spriteIndex, m_firstIconSpriteIndex,
                                       static_cast<int>(m_icons.size()),
                                       theme ? &theme->iconOverrideShape : nullptr);
}

int AssetManager::iconSpriteForShape(int shapeIndex) const {
    // Shape index is a position in the BASE vocabulary (that is what persistence and
    // the pickers agree on); the theme only gets to change which sprite that position
    // draws as. Doing both steps here is what keeps the two from drifting apart at
    // the ten call sites that used to open-code the arithmetic.
    const int count = static_cast<int>(m_icons.size());
    const std::string name = (shapeIndex > 0 && shapeIndex <= count)
        ? m_icons[static_cast<size_t>(shapeIndex - 1)].filename : std::string();
    const ThemeAsset* theme = activeIconTheme();
    return IconResolve::spriteForShape(shapeIndex, m_firstIconSpriteIndex, count, name,
                                       theme ? &theme->iconOverrides : nullptr);
}

std::string AssetManager::getIconFilename(int spriteIndex) const {
    int arrayIndex = spriteIndex - m_firstIconSpriteIndex;
    if (arrayIndex >= 0 && arrayIndex < static_cast<int>(m_icons.size())) {
        return m_icons[arrayIndex].filename;
    }
    return "";
}

bool AssetManager::isHudIdentityShape(int shapeIndex) const {
    if (shapeIndex < 1) return false;  // 0 = Off/default, never a HUD identity icon
    int spriteIndex = m_firstIconSpriteIndex + shapeIndex - 1;
    return getIconFilename(spriteIndex).rfind("hud-", 0) == 0;
}

int AssetManager::stepShapeIndexSkippingHud(int shapeIndex, bool forward, bool allowOff) const {
    int count = static_cast<int>(m_icons.size());
    if (count < 1) return shapeIndex;
    // Range is [lo..count]; lo is 0 when an Off/default slot is allowed, else 1.
    // Walk at most (count+1) steps so we always terminate even if every icon were
    // somehow a HUD identity icon.
    int lo = allowOff ? 0 : 1;
    int shape = shapeIndex;
    for (int i = 0; i <= count; ++i) {
        if (forward) { if (++shape > count) shape = lo; }
        else         { if (--shape < lo)    shape = count; }
        if (!isHudIdentityShape(shape)) return shape;  // 0 always passes
    }
    return shapeIndex;
}

std::string AssetManager::getIconDisplayName(int spriteIndex) const {
    int arrayIndex = spriteIndex - m_firstIconSpriteIndex;
    if (arrayIndex >= 0 && arrayIndex < static_cast<int>(m_icons.size())) {
        return m_icons[arrayIndex].displayName;
    }
    return "";
}

float AssetManager::getTextureAspectRatio(int spriteIndex) const {
    if (spriteIndex <= 0) return 0.0f;

    // Find which texture asset this sprite belongs to
    for (const auto& texture : m_textures) {
        int offset = spriteIndex - texture.firstSpriteIndex;
        if (offset >= 0 && offset < static_cast<int>(texture.aspectRatios.size())) {
            return texture.aspectRatios[offset];
        }
    }
    return 0.0f;
}

bool AssetManager::readTgaDimensions(const std::string& path, int& width, int& height) {
    // TGA header: bytes 12-13 = width, bytes 14-15 = height (little-endian uint16)
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return false;

    unsigned char header[18];
    if (!file.read(reinterpret_cast<char*>(header), 18)) return false;

    width = header[12] | (header[13] << 8);
    height = header[14] | (header[15] << 8);

    // Reject unreasonable dimensions that would produce extreme aspect ratios
    if (width <= 0 || height <= 0 || width > 16384 || height > 16384) return false;

    return true;
}
