// ============================================================================
// core/asset_manager_packs.cpp
// AssetManager's art-pack handling: the gamepad/pitboard/gauge ini readers,
// pack discovery (gamepads, pitboards, gauges) with base-pack sprite
// resolution, the pack/theme layout reloads, and the per-type getters. Split
// from asset_manager.cpp; every method body is unchanged.
// ============================================================================
#include "asset_manager.h"
#include "asset_manager_internal.h"
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
        if (std::strcmp(key, "pack.name") == 0) {
            if (rawValue && *rawValue) p.displayName = rawValue;
            return true;
        }
        if (std::strcmp(key, "pack.base") == 0) {
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
                     "[pack] name/base, [art] width/height, [size], [offset] and [spacing])",
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

        if (std::strcmp(key, "pack.name") == 0) {
            if (rawValue && *rawValue) b.displayName = rawValue;
            return true;
        }
        if (std::strcmp(key, "pack.base") == 0) {
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
                     "[pack] name/base, [art] width/height, [text] color and [offset])",
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

// The gauge ini's whole surface. Same vocabulary rules as the two above: every
// key optional, sections scope the short property names, one shared parse walk.
//
// The two needle colours are the non-numeric pair, authored as #rrggbb like a
// theme's colours and a board's text -- and, like those, NOT on the float path,
// because a 32-bit colour word does not survive the parse.
static void readGaugeIniPairs(const std::string& iniPath, GaugesAsset& set) {
    struct Ctx { GaugesAsset* s; };
    Ctx ctx{ &set };

    layoutForEachIniPairRaw(iniPath, [](const char* key, float parsed, const char* rawValue,
                                        bool numeric, void* raw) -> bool {
        GaugesAsset& g = *static_cast<Ctx*>(raw)->s;

        if (std::strcmp(key, "pack.name") == 0) {
            if (rawValue && *rawValue) g.displayName = rawValue;
            return true;
        }
        if (std::strcmp(key, "pack.base") == 0) {
            if (rawValue && *rawValue) g.baseName = rawValue;
            return true;
        }
        const bool tachoColor = (std::strcmp(key, "tacho.needle-color") == 0);
        if (tachoColor || std::strcmp(key, "speedo.needle-color") == 0) {
            unsigned long abgr = 0;
            if (rawValue && PluginUtils::parseRgbHex(rawValue, abgr)) {
                GaugeLayout::Dial& dial = tachoColor ? g.geometry.tacho : g.geometry.speedo;
                dial.needleColor = static_cast<uint32_t>(abgr);
            } else {
                DEBUG_WARN_F("Gauges '%s': '%s=%s' is not an RGB hex colour - ignored "
                             "(write #rrggbb, e.g. #ffffff)",
                             g.name.c_str(), key, rawValue ? rawValue : "");
            }
            return true;
        }

        if (!numeric) {
            DEBUG_WARN_F("Gauges '%s': key '%s' is not a number - ignored",
                         g.name.c_str(), key);
            return true;
        }

        if (GaugeLayout::applyGaugeGeometryIni(g.geometry, key, parsed)) return true;

        DEBUG_WARN_F("Gauges '%s': unknown key '%s' - ignored (the sections are "
                     "[pack] name/base and [tacho]/[speedo] min/max/min-angle/"
                     "max-angle/needle-length/needle-width/needle-color, plus "
                     "[speedo] max-mph/odometer-y/tripmeter-y)",
                     g.name.c_str(), key);
        return true;
    }, &ctx);
}

// BOTH handlers, for the reason stated at readPitboardIni.
static void readGaugeIni(const std::string& iniPath, GaugesAsset& set) {
    try {
        readGaugeIniPairs(iniPath, set);
    } catch (const std::exception& e) {
        DEBUG_WARN_F("Gauges '%s': failed to read '%s': %s",
                     set.name.c_str(), iniPath.c_str(), e.what());
    } catch (...) {
        DEBUG_WARN_F("Gauges '%s': failed to read '%s'",
                     set.name.c_str(), iniPath.c_str());
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
                                     const char* stem,
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
        readIni(packIniPath(dir, pack.name, stem), pack);
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
        //
        // A SKIN resets to its BASE instead, which is the same rule
        // reloadPackLayouts follows for the other pack types: a key the skin
        // deletes must fall back to what it layers over, not to the built-in.
        // The base is guaranteed already re-read by this loop, because discovery
        // appends every standalone theme before any skin. (Changing `base` at
        // runtime is not honoured -- that binding, like the sprite files, is
        // discovery-time.)
        const ThemeAsset defaults;
        const ThemeAsset* inheritFrom = &defaults;
        if (!theme.baseName.empty()) {
            for (const ThemeAsset& candidate : m_themes) {
                if (candidate.name == theme.baseName && candidate.baseName.empty()) {
                    inheritFrom = &candidate;
                    break;
                }
            }
        }
        const ThemeAsset& fresh = *inheritFrom;
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
        // Back to the folder-derived title FIRST, so deleting the optional
        // [theme] name reverts on RELOAD_CONFIG instead of keeping the last one
        // read -- the same absence-is-authoritative rule the geometry reset
        // above follows.
        theme.displayName = generateDisplayName(theme.name);
        readThemeIni(packIniPath(dir, theme.name, PackIni::kTheme), theme);
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
    reloadPackLayouts(m_gamepads, GAMEPADS_SUBDIR, PackIni::kGamepad, &readGamepadIni);
    reloadPackLayouts(m_pitboards, PITBOARDS_SUBDIR, PackIni::kPitboard, &readPitboardIni,
                      &seedPitboardArt);
    reloadPackLayouts(m_gauges, GAUGES_SUBDIR, PackIni::kGauges, &readGaugeIni);

    DEBUG_INFO_F("AssetManager: reloaded layout for %zu theme(s), %zu gamepad(s), %zu pitboard(s), %zu gauges pack(s)",
                 m_themes.size(), m_gamepads.size(), m_pitboards.size(), m_gauges.size());
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
        const std::string ini = packIniPath(base, dir, PackIni::kGamepad);
        if (!readPackStringKey(ini, "pack.base").empty()) continue;

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
        readGamepadIni(ini, pad);

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
        const std::string ini = packIniPath(skinDir, dir, PackIni::kGamepad);
        const std::string baseKey = readPackStringKey(ini, "pack.base");
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
        readGamepadIni(ini, pad);

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
        const std::string ini = packIniPath(base, dir, PackIni::kPitboard);
        if (!readPackStringKey(ini, "pack.base").empty()) continue;

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
        readPitboardIni(ini, board);

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
        const std::string ini = packIniPath(skinDir, dir, PackIni::kPitboard);
        const std::string baseKey = readPackStringKey(ini, "pack.base");
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
        readPitboardIni(ini, board);

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

// ============================================================================
// Gauges packs
// ============================================================================

void AssetManager::discoverGauges() {
    const std::string root = std::string(DISCOVERY_DIR) + "\\" + GAUGES_SUBDIR;
    std::vector<std::string> dirs;
    if (!listPackDirs(root, dirs)) {
        DEBUG_WARN_F("AssetManager: No gauges directory (%s) - the tacho and speedo "
                     "have no faces to draw", root.c_str());
        return;
    }

    // Gauge packs sit past the board block, which sits past the pads, which sit
    // past the themes -- one contiguous run per type, appended in the order
    // setupDefaultResources registers them.
    int spriteIndex = m_firstIconSpriteIndex + static_cast<int>(m_icons.size());
    for (const ThemeAsset& t : m_themes) spriteIndex += static_cast<int>(t.spriteFiles.size());
    spriteIndex += static_cast<int>(m_gamepads.size()) * GamepadSprite::COUNT;
    spriteIndex += static_cast<int>(m_pitboards.size()) * PitboardSprite::COUNT;

    // Two phases, same shape and same reasons as the two discoveries above:
    // standalone sets first, then skins resolved against them.
    for (const std::string& dir : dirs) {
        const std::string base = root + "\\" + dir + "\\";
        const std::string ini = packIniPath(base, dir, PackIni::kGauges);
        if (!readPackStringKey(ini, "pack.base").empty()) continue;

        // Verify the whole set BEFORE handing out any index, so a rejected pack
        // cannot shift the indices of the packs after it.
        bool complete = true;
        int w = 0, h = 0;
        for (const char* stem : GaugeSprite::kStems) {
            if (readTgaDimensions(base + stem + ".tga", w, h)) continue;
            DEBUG_WARN_F("AssetManager: gauges '%s' skipped - missing %s.tga",
                         dir.c_str(), stem);
            complete = false;
            break;
        }
        if (!complete) continue;

        GaugesAsset set;
        set.name = dir;
        set.displayName = generateDisplayName(dir);
        // No art SEED here, unlike a pit board: a dial is drawn as a circle at the
        // widget's own size, so the face's pixel dimensions carry no layout meaning
        // and there is nothing to recover from the .tga.
        readGaugeIni(ini, set);

        for (int i = 0; i < GaugeSprite::COUNT; ++i) set.sprites[i] = spriteIndex++;

        DEBUG_INFO_F("AssetManager: Found gauges '%s' (%s), tacho 0-%.0f, speedo 0-%.0f, sprite %d",
                     set.name.c_str(), set.displayName.c_str(),
                     set.geometry.tacho.max, set.geometry.speedo.max,
                     set.sprites[GaugeSprite::TACHO]);
        m_gauges.push_back(std::move(set));
    }
    const size_t standaloneSets = m_gauges.size();

    for (const std::string& dir : dirs) {
        const std::string skinDir = root + "\\" + dir + "\\";
        const std::string ini = packIniPath(skinDir, dir, PackIni::kGauges);
        const std::string baseKey = readPackStringKey(ini, "pack.base");
        if (baseKey.empty()) continue;

        const GaugesAsset* basePack = nullptr;
        for (size_t i = 0; i < standaloneSets; ++i) {
            if (m_gauges[i].name == baseKey) { basePack = &m_gauges[i]; break; }
        }
        if (!basePack) {
            DEBUG_WARN_F("AssetManager: gauges '%s' skipped - base '%s' is not a "
                         "standalone pack (a base cannot itself declare a base)",
                         dir.c_str(), baseKey.c_str());
            continue;
        }

        GaugesAsset set;
        set.name = dir;
        set.displayName = generateDisplayName(dir);
        set.baseName = baseKey;
        // Base geometry first, then the skin's own keys override what they state.
        set.geometry = basePack->geometry;
        readGaugeIni(ini, set);

        int w = 0, h = 0;
        for (int i = 0; i < GaugeSprite::COUNT; ++i) {
            set.spriteFromBase[i] =
                !readTgaDimensions(skinDir + GaugeSprite::kStems[i] + ".tga", w, h);
            set.sprites[i] = spriteIndex++;
        }

        DEBUG_INFO_F("AssetManager: Found gauges skin '%s' (%s) over '%s', sprite %d",
                     set.name.c_str(), set.displayName.c_str(), baseKey.c_str(),
                     set.sprites[GaugeSprite::TACHO]);
        m_gauges.push_back(std::move(set));
    }

    DEBUG_INFO_F("AssetManager: Found %zu gauges pack(s)", m_gauges.size());
    warnAboutStrandedGaugeTextures();
}

// Gauge art sitting in the PLUGIN's own textures directory, which nothing reads
// any more.
//
// This is the half of the upgrade migrateLegacyGaugeArt() cannot fix. It works
// from the user's Documents tree, because a file there was put there by a person;
// a file in the plugins tree cannot say whose it is. Two very different people
// have one:
//
//   - somebody who hand-dropped their own art straight into the game folder,
//     which was a workable habit before packs. Their art has just stopped being
//     drawn, and nothing else in the plugin will ever tell them why.
//   - everybody upgrading from a version that SHIPPED tacho_widget_1.tga there.
//     File /r copies and never deletes, so the old face is simply left behind.
//
// Nothing can separate them from the file alone, so the line is written for both
// readers and says what each should do. Once per launch, into a log, which is the
// proportionate place: for the first person it is the only explanation they will
// get, and for the second it is a stale file they may as well delete.
void AssetManager::warnAboutStrandedGaugeTextures() const {
    static constexpr const char* kLegacyStems[] = { "tacho_widget", "speedo_widget" };
    const std::string dir = std::string(DISCOVERY_DIR) + "\\" + TEXTURES_SUBDIR + "\\";

    for (const char* stem : kLegacyStems) {
        for (int variant = 1; variant <= 9; ++variant) {
            const std::string path = dir + stem + "_" + std::to_string(variant) + ".tga";
            if (GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES) continue;
            DEBUG_WARN_F("AssetManager: '%s_%d.tga' is no longer used - the dial faces "
                         "now come from gauges packs. If you drew it, copy it to "
                         "mxbmrp3\\gauges\\<yourset>\\%s.tga in your Documents plugin "
                         "folder (with a gauge.ini saying 'base = classic') and pick it "
                         "in Settings > Widgets. If you did not, it is left over from an "
                         "older version and can be deleted.",
                         stem, variant,
                         std::strcmp(stem, "tacho_widget") == 0 ? "tacho" : "speedo");
        }
    }
}

const GaugesAsset* AssetManager::getGaugesByName(const std::string& name) const {
    for (const GaugesAsset& g : m_gauges) {
        if (g.name == name) return &g;
    }
    return nullptr;
}

const GaugesAsset* AssetManager::getDefaultGauges() const {
    // The MIGRATED pack first, when there is one. It only exists for somebody who
    // had drawn their own faces before packs (see migrateLegacyGaugeArt), and for
    // them the shipped set is not a neutral default -- it is their art being
    // replaced. Nobody else has this folder, so nobody else is affected.
    if (const GaugesAsset* migrated = getGaugesByName(LEGACY_GAUGES)) return migrated;
    if (const GaugesAsset* shipped = getGaugesByName(DEFAULT_GAUGES)) return shipped;
    return m_gauges.empty() ? nullptr : &m_gauges.front();
}

std::string AssetManager::getGaugesPath(const std::string& packName, const char* stem) const {
    return std::string(RESOURCE_DIR) + "\\" + GAUGES_SUBDIR + "\\" + packName + "\\" + stem + ".tga";
}
