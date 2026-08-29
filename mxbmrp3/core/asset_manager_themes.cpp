// ============================================================================
// core/asset_manager_themes.cpp
// AssetManager's theme discovery: scanning themes/<name>/ (standalone themes
// and skins), the theme ini reader, and the theme geometry mapping. Split
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

    // TWO PHASES, same shape and same reasons as the other pack types: standalone
    // themes first, then skins resolved against them. A skin brings no art of its
    // own in the common case, so the completeness check below would reject it --
    // it is checked against the RESOLVED set instead, which the base guarantees.
    for (const std::string& dir : dirs) {
        const std::string base = themesRoot + "\\" + dir + "\\";
        int w = 0, h = 0;

        auto exists = [&](const std::string& stem) {
            return readTgaDimensions(base + stem + ".tga", w, h);
        };

        if (!readPackStringKey(packIniPath(base, dir, PackIni::kTheme), "pack.base").empty()) continue;

        if (!exists("frame_center")) {
            DEBUG_WARN_F("AssetManager: theme '%s' skipped - center.tga is required", dir.c_str());
            continue;
        }

        ThemeAsset theme;
        theme.name = dir;
        theme.displayName = generateDisplayName(dir);
        readThemeIni(packIniPath(base, theme.name, PackIni::kTheme), theme);

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
            if (theme.spriteFiles.empty()) theme.firstOwnSprite = spriteIndex;
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
    const size_t standaloneThemes = m_themes.size();

    // SKINS. A skin inherits the base BY VALUE -- every sprite index, the palette,
    // the fonts, the box terms, the icon overrides -- and then overwrites only what
    // it actually brings. Sprite indices are global and already registered, so a
    // skin that redraws nothing registers nothing: `spriteFiles` stays empty and
    // HudManager adds no files for it.
    //
    // Resolution is PER FILE, not per slice set: a skin that supplies card_center
    // and no card corners gets the base's corners, exactly as the documented rule
    // says ("what this folder states wins; what it leaves out comes from the
    // base"). The only fallback re-applied below is for a skin INTRODUCING a card
    // or button set the base did not have, where there is no base value to inherit.
    for (const std::string& dir : dirs) {
        const std::string skinDir = themesRoot + "\\" + dir + "\\";
        const std::string ini = packIniPath(skinDir, dir, PackIni::kTheme);
        const std::string baseKey = readPackStringKey(ini, "pack.base");
        if (baseKey.empty()) continue;

        const ThemeAsset* basePack = nullptr;
        for (size_t i = 0; i < standaloneThemes; ++i) {
            if (m_themes[i].name == baseKey) { basePack = &m_themes[i]; break; }
        }
        if (!basePack) {
            DEBUG_WARN_F("AssetManager: theme '%s' skipped - base '%s' is not a "
                         "standalone theme (a base cannot itself declare a base)",
                         dir.c_str(), baseKey.c_str());
            continue;
        }

        ThemeAsset theme = *basePack;
        theme.name = dir;
        theme.baseName = baseKey;
        theme.displayName = generateDisplayName(dir);
        // Only this folder's OWN files from here on; everything else keeps the
        // base's index, which is already in the sprite list under the base's name.
        theme.spriteFiles.clear();
        theme.firstOwnSprite = -1;   // the copied value is the BASE's first file
        theme.iconOverrideShape.clear();

        int w = 0, h = 0;
        auto owns = [&](const std::string& stem) {
            return readTgaDimensions(skinDir + stem + ".tga", w, h);
        };
        auto add = [&](const std::string& stem) {
            if (theme.spriteFiles.empty()) theme.firstOwnSprite = spriteIndex;
            theme.spriteFiles.push_back(stem);
            return spriteIndex++;
        };

        if (owns("frame_center")) theme.centerSprite = add("frame_center");
        for (int i = 0; i < 4; ++i) {
            const std::string fc = std::string("frame_") + kCornerFiles[i];
            const std::string fe = std::string("frame_") + kEdgeFiles[i];
            if (owns(fc)) theme.cornerSprites[i] = add(fc);
            if (owns(fe)) theme.edgeSprites[i]   = add(fe);
        }
        if (owns("card_center")) theme.cardCenterSprite = add("card_center");
        for (int i = 0; i < 4; ++i) {
            const std::string ic = std::string("card_") + kCornerFiles[i];
            const std::string ie = std::string("card_") + kEdgeFiles[i];
            if (owns(ic)) theme.cardCornerSprites[i] = add(ic);
            if (owns(ie)) theme.cardEdgeSprites[i]   = add(ie);
        }
        if (owns("button_center")) theme.buttonCenterSprite = add("button_center");
        for (int i = 0; i < 4; ++i) {
            const std::string bc = std::string("button_") + kCornerFiles[i];
            const std::string be = std::string("button_") + kEdgeFiles[i];
            if (owns(bc)) theme.buttonCornerSprites[i] = add(bc);
            if (owns(be)) theme.buttonEdgeSprites[i]   = add(be);
        }
        // A skin that INTRODUCES a set the base lacked has nothing to inherit for
        // the pieces it did not draw, and a partial set draws a card with holes in
        // it -- so those fall back to the centre, the same flat band the standalone
        // path settles for.
        if (theme.cardCenterSprite != 0) {
            for (int i = 0; i < 4; ++i) {
                if (theme.cardCornerSprites[i] == 0) theme.cardCornerSprites[i] = theme.cardCenterSprite;
                if (theme.cardEdgeSprites[i] == 0)   theme.cardEdgeSprites[i]   = theme.cardCenterSprite;
            }
        }
        if (theme.buttonCenterSprite != 0) {
            for (int i = 0; i < 4; ++i) {
                if (theme.buttonCornerSprites[i] == 0) theme.buttonCornerSprites[i] = theme.buttonCenterSprite;
                if (theme.buttonEdgeSprites[i] == 0)   theme.buttonEdgeSprites[i]   = theme.buttonCenterSprite;
            }
        }

        // Icon overrides, per file like the slices. The base's map is inherited, so
        // this only replaces the ones the skin redraws -- and the shape map is
        // rebuilt for every entry, base or own, because it is keyed by sprite.
        {
            const std::string iconDir = skinDir + ICONS_SUBDIR + "\\";
            for (const IconAsset& icon : m_icons) {
                if (readTgaDimensions(iconDir + icon.filename + ".tga", w, h)) {
                    theme.iconOverrides[icon.filename] =
                        add(std::string(ICONS_SUBDIR) + "\\" + icon.filename);
                }
                auto it = theme.iconOverrides.find(icon.filename);
                if (it != theme.iconOverrides.end()) {
                    theme.iconOverrideShape[it->second] =
                        static_cast<int>(&icon - &m_icons[0]) + 1;
                }
            }
        }

        readThemeIni(ini, theme);
        warnIfCardWithoutInner(theme);

        DEBUG_INFO_F("AssetManager: Found theme skin '%s' (%s) over '%s', %zu own file(s)",
                     theme.name.c_str(), theme.displayName.c_str(), baseKey.c_str(),
                     theme.spriteFiles.size());
        m_themes.push_back(std::move(theme));
    }

    bumpThemeGeneration();
    size_t overrides = 0;
    for (const ThemeAsset& t : m_themes) overrides += t.iconOverrides.size();
    DEBUG_INFO_F("AssetManager: Found %zu themes (%zu icon overrides)", m_themes.size(), overrides);
}

// The optional per-theme ini (see packIniPath). It carries the WHOLE per-theme
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

        // [theme] name - OPTIONAL, exactly like a pad's [pad] name and a board's
        // [board] name. Absent (or empty), the picker keeps showing the
        // folder-derived title, so every theme that shipped before this reads
        // unchanged; present, it is the only way to spell something the folder
        // name cannot -- an acronym, or a title the directory has no room for.
        // Answered here with the other non-numeric keys, before the numeric path
        // below and before the unknown-key warning.
        if (std::strcmp(key, "pack.name") == 0) {
            if (rawValue && *rawValue) t.displayName = rawValue;
            return true;
        }
        // ANSWERED, not applied. Theme discovery reads `base` out of band, before
        // it decides which phase handles the folder (readPackStringKey), so there
        // is nothing to do here -- but the key still has to be CLAIMED, or it
        // falls through to the numeric path and every documented theme skin logs
        // "'pack.base=carbon-dark' is not a number - ignored" on startup and on
        // every RELOAD_CONFIG.
        if (std::strcmp(key, "pack.base") == 0) return true;

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
