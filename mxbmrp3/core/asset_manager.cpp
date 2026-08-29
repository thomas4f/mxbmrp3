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
    m_gauges.clear();
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
    discoverGauges();

    m_initialized = true;

    DEBUG_INFO_F("AssetManager: Discovery complete - %zu fonts, %zu texture bases (%zu sprites), %zu icons, %zu themes, %zu gamepads, %zu pitboards, %zu gauges",
        m_fonts.size(), m_textures.size(), m_totalTextureSprites, m_icons.size(), m_themes.size(), m_gamepads.size(), m_pitboards.size(), m_gauges.size());
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
    CreateDirectoryA((userBaseDir + "\\" + GAUGES_SUBDIR).c_str(), NULL);
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

    // Ensure destination directories exist. CreateDirectoryA creates ONE level
    // only, so the parent (plugins\) is created explicitly first: in the game it
    // always exists (this DLL is loaded from it), but on a bare tree - the test
    // harness's fresh build directory, i.e. every CI run - a missing parent made
    // every CreateDirectoryA/CopyFileA below fail with ERROR_PATH_NOT_FOUND and
    // the whole sync silently no-op. Pinned by gauges_migration_test, which
    // run_tests.sh now runs against a wiped plugins tree.
    const std::string discoveryDir = DISCOVERY_DIR;
    CreateDirectoryA(discoveryDir.substr(0, discoveryDir.find('\\')).c_str(), NULL);
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
    // The NESTED asset types (<root>/<name>/<payload> + <type>.ini) cannot ride the
    // flat syncDirectory, so each pack folder is synced in turn. Without this a
    // user-authored pack has to be dropped straight into the game's plugins folder,
    // which every other asset type does not require -- and authoring one is exactly
    // the workflow the pack format exists to support.
    m_userBaseDir = userBaseDir;
    migrateLegacyGaugeArt(userBaseDir);
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
        // Sprite indices are 1-BASED (0 is reserved for SOLID_COLOR), so with no
        // textures at all the icon block still starts at 1 -- exactly what the
        // full path below computes. Leaving this at its reset value 0 hands every
        // later asset type 0-based indices while registration and the quads stay
        // 1-based: all themed/pack art draws off by one and each first file reads
        // as "no sprite". Latent until verifySpriteRegistrationOrder() caught it
        // on a staged tree; pinned by sprite_order_test.cpp (no-textures trees).
        m_firstIconSpriteIndex = 1;
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
// See the declaration for why this exists and why it reads the USER's folder.
//
// The generated pack always states `base = classic`, which does two jobs at once:
// a user who had drawn only ONE of the two faces gets a valid pack anyway (the
// other stem resolves from the shipped set, rather than the pack being skipped
// for an incomplete sprite set), and the ranges their art was drawn against --
// the compiled constants classic now carries -- come along with it.
//
// The marker is what makes this a ONE-TIME migration rather than a standing
// behaviour: without it, a user who looked at the generated pack and deleted it
// would get it back on the next launch, forever. It sits in the gauges root
// rather than inside the pack so that deleting the pack does not re-arm it.
void AssetManager::migrateLegacyGaugeArt(const std::string& userBaseDir) {
    if (userBaseDir.empty()) return;

    const std::string gaugesRoot = userBaseDir + "\\" + GAUGES_SUBDIR;
    const std::string marker = gaugesRoot + "\\.legacy-migrated";
    if (GetFileAttributesA(marker.c_str()) != INVALID_FILE_ATTRIBUTES) return;

    // The legacy stems, paired with what they become in a pack. Lowest variant
    // wins: the cycle is gone, so there is one face per gauge now, and variant 1
    // is the one every install drew by default.
    struct LegacyFace { const char* legacyStem; const char* packStem; };
    static constexpr LegacyFace kFaces[] = {
        { "tacho_widget",  "tacho"  },
        { "speedo_widget", "speedo" },
    };

    const std::string texturesDir = userBaseDir + "\\" + TEXTURES_SUBDIR + "\\";
    std::string found[2];
    bool any = false;
    for (int i = 0; i < 2; ++i) {
        for (int variant = 1; variant <= 9; ++variant) {
            const std::string candidate = texturesDir + kFaces[i].legacyStem + "_"
                                        + std::to_string(variant) + ".tga";
            if (GetFileAttributesA(candidate.c_str()) == INVALID_FILE_ATTRIBUTES) continue;
            found[i] = candidate;
            any = true;
            break;
        }
    }
    // Nothing to carry across. Deliberately does NOT write the marker: somebody
    // who drops custom gauge art in later still gets migrated, which is the whole
    // point of migrating from a folder people edit by hand.
    if (!any) return;

    const std::string packDir = gaugesRoot + "\\legacy";
    CreateDirectoryA(gaugesRoot.c_str(), NULL);
    CreateDirectoryA(packDir.c_str(), NULL);

    int copied = 0;
    for (int i = 0; i < 2; ++i) {
        if (found[i].empty()) continue;
        const std::string dest = packDir + "\\" + kFaces[i].packStem + ".tga";
        // TRUE is bFailIfExists -- so this does NOT overwrite. A pack folder that
        // already holds a face is one the user built; the migration must not
        // stamp on it. (The comment here used to name the opposite literal, which
        // is the kind of thing somebody later "reconciles" the wrong way.)
        if (CopyFileA(found[i].c_str(), dest.c_str(), TRUE)) ++copied;
    }

    const std::string ini = packDir + "\\" + PackIni::kGauges + ".ini";
    if (GetFileAttributesA(ini.c_str()) == INVALID_FILE_ATTRIBUTES) {
        std::ofstream f(ini, std::ios::binary);
        if (f.is_open()) {
            f << "; Written automatically when this version moved the dial faces into\r\n"
                 "; packs. Your own tacho_widget/speedo_widget art from mxbmrp3\\textures\\\r\n"
                 "; was copied in beside this file so it keeps being drawn.\r\n"
                 ";\r\n"
                 "; `base = classic` answers whatever this folder does not: the face you\r\n"
                 "; did not redraw, and the ranges your art was drawn against (they used\r\n"
                 "; to be compiled into the plugin, which is what this change fixes -- see\r\n"
                 "; the shipped gauges/classic/gauge.ini).\r\n"
                 ";\r\n"
                 "; This file is yours now: rename the pack, state your own [tacho] max if\r\n"
                 "; your face is not printed to 15000, or delete the whole folder. It is\r\n"
                 "; written once and never rewritten.\r\n"
                 "\r\n";
            // Header from the shared constant, never spelled here: see
            // PackIni::kSection for the bug that costs.
            f << "[" << PackIni::kSection << "]\r\n"
                 "name = Legacy\r\n"
                 "base = classic\r\n";
        }
    }

    // The marker goes down even if a copy failed: retrying every launch would
    // just log the same failure forever, and the folder is now the user's to fix.
    std::ofstream m(marker, std::ios::binary);
    if (m.is_open()) {
        m << "Delete this file to let the plugin look for pre-pack gauge art again.\r\n";
    }

    DEBUG_INFO_F("AssetManager: migrated %d legacy gauge face(s) from textures\\ into "
                 "gauges\\legacy - it is selected by default; pick another in "
                 "Settings > Widgets", copied);
}

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
// are each a flat folder of payload files plus a <type>.ini, so this deliberately
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


std::string AssetManager::packIniPath(const std::string& dir, const std::string& packName,
                                      const char* stem) const {
    // The same pack's folder in the user's own tree, by swapping the discovery
    // root for the user root. Every caller builds `dir` as
    // DISCOVERY_DIR\<subdir>\<name>\, so the tail after the prefix is exactly
    // the part the two trees share -- and the prefix is CHECKED rather than
    // assumed, so a caller that ever builds a path some other way degrades to
    // "cannot tell" (which warns) instead of pointing somewhere wrong.
    std::string userDir;
    const std::string discoveryRoot = DISCOVERY_DIR;
    if (!m_userBaseDir.empty() && dir.compare(0, discoveryRoot.size(), discoveryRoot) == 0) {
        userDir = m_userBaseDir + dir.substr(discoveryRoot.size());
    }

    const PackIni::Resolved r = PackIni::resolve(dir, packName, stem, userDir);
    // Both present: the canonical file is what the plugin reads, so say which
    // edits are going nowhere. An upgrade produces exactly this -- the user-folder
    // sync copies the new ini in and never deletes the old one -- and a silent
    // win here is the same "I edited it and nothing happened" the rename exists
    // to remove.
    // Only when the duplicate is the user's own -- see PackIni::resolve. A
    // leftover from an older Setup is silent, which is what lets this warning
    // mean "an edit of yours is going nowhere" again.
    if (r.shadowed) {
        DEBUG_WARN_F("AssetManager: pack '%s' has both %s.ini and %s.ini - reading "
                     "%s.ini; the other is ignored and can be deleted",
                     packName.c_str(), stem, packName.c_str(), stem);
    }
    return r.path;
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
