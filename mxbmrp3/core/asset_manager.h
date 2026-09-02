// ============================================================================
// core/asset_manager.h
// Dynamic asset discovery and management for fonts, textures, icons, and web overlay
// Scans mxbmrp3_data subdirectories at startup to build asset registries
// ============================================================================
// file-budget: 1300 one class + the per-type asset structs and kStems tables its static_asserts guard
#pragma once

#include "ui_config.h"      // mxbThemeGeneration()
#include "color_config.h"   // ColorSlot
#include "font_config.h"    // FontCategory

#include "layout_metrics.h"
#include "panel_box.h"      // PanelBox::Sides — the box-model term storage below
#include "pack_ini_path.h" // PackIni::k* — the canonical per-type ini stems
#include "../hud/gamepad_geometry.h"    // GamepadLayout::PadGeometry (header-only, no deps)
#include "../hud/pitboard_geometry.h"   // PitboardLayout::BoardGeometry (ditto)
#include "../hud/gauge_geometry.h"      // GaugeLayout::GaugeGeometry (ditto)

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
    std::vector<float> aspectRatios;   // Per-variant pixel aspect ratio (width/height), parallel to variants
};

// Font asset info
struct FontAsset {
    std::string filename;              // e.g., "RobotoMono-Regular"
    std::string displayName;           // e.g., "Roboto Mono"
    int fontIndex = 0;                 // Index in the font array (1-based for game engine)
    // The heavier cut of this same face, as an engine font index (0 = none).
    // Set on the REGULAR entry, pointing at its companion.
    int emphasisIndex = 0;
    // True on the companion itself: it loads and is addressable by name, but the
    // font cycler steps past it. A weight is not a typeface choice -- picking
    // "Roboto" should not also offer "Roboto Medium" as a separate answer, and
    // the categories that want weight ask for it themselves (FontConfig::getFont).
    bool emphasisOnly = false;
};

// "Roboto-Medium" -> "Roboto-Regular"; empty when this stem is not a heavier cut
// of some other face. Pure so the rule is unit-testable -- discoverFonts() needs
// a directory, this needs a string.
//
// MEDIUM AND SEMIBOLD ONLY, deliberately NOT BOLD. RobotoMono-Bold ships as a
// user-selectable face; folding it in would take it out of the cycler and
// silently strand anyone who has picked it. The faces this rule is for are the
// ones that exist to BE emphasis companions.
inline std::string emphasisBaseOf(const std::string& stem) {
    for (const char* suffix : { "-Medium", "-SemiBold" }) {
        const size_t n = std::string(suffix).size();
        if (stem.size() > n && stem.compare(stem.size() - n, n, suffix) == 0) {
            return stem.substr(0, stem.size() - n) + "-Regular";
        }
    }
    return std::string();
}

// Icon asset info
struct IconAsset {
    std::string filename;              // e.g., "trophy-solid-full"
    std::string displayName;           // e.g., "Trophy"
    int spriteIndex = 0;               // Index in the sprite array
};

// A 9-slice panel theme: nine sprites (four corners, four edges, a center) that
// BaseHud tiles into a resizable themed background.
//
// Nine separate files rather than one atlas because the game's quad primitive has
// no UV / source-rect field (SPluginQuad_t is 4 corner positions + ONE whole
// sprite), and each is drawn exactly as authored -- no rotation, no mirroring.
// See BaseHud::addThemedBackground() and NineSlice::build().
struct ThemeAsset {
    // The theme's SEMANTIC PALETTE and font set, both optional and both sparse.
    //
    // A theme states only the slots it has an opinion about; the rest fall through
    // to the built-in defaults, and a user's Appearance override beats both. That
    // three-step precedence (built-in -> theme -> user) is the same one the layout
    // vocabulary already has, and it is what lets a theme ship a look without
    // overwriting a palette somebody tuned by hand.
    //
    // Colours arrive as RGB hex in the ini ("#ff8800", the notation a skinner types
    // everywhere else) and are stored here already converted to the game's ABGR --
    // one conversion, at the edge, so nothing downstream has to know the game packs
    // its bytes backwards.
    std::array<unsigned long, static_cast<size_t>(ColorSlot::COUNT)> colors{};
    std::array<bool, static_cast<size_t>(ColorSlot::COUNT)> colorSet{};
    // Empty = this theme has no opinion about that category.
    std::array<std::string, static_cast<size_t>(FontCategory::COUNT)> fonts;

    std::string name;                  // directory name, e.g. "carbon-dark"
    std::string displayName;           // e.g., "Carbon Dark"

    // [pack] base -- the theme this one layers over, empty for a standalone one.
    // Same rule and same one-level limit as the other pack types: what this
    // folder and its ini state wins, per FILE and per KEY; what they leave out is
    // answered from the base.
    //
    // It matters more here than anywhere else, and not for the reason it does on
    // a pad. Theme art is cut from a master by tools/themeslice, so sharing
    // SLICES is the small win. The big one is that a theme's ini carries the
    // whole palette, the fonts and the box terms, and a standalone theme is only
    // accepted with its full slice set present -- so "Carbon Dark but my team's
    // colours" would cost 27 .tga copies to change three lines. With a base it
    // is an ini and nothing else: `spriteFiles` stays EMPTY and every sprite
    // index is the base's, already registered.
    std::string baseName;

    // THE FRAME SET -- the 9-slice around a whole panel, and the only mandatory one.
    // Nine files: frame_center.tga, frame_corner_tl/tr/bl/br.tga and
    // frame_edge_top/bottom/left/right.tga. A directory missing any of them is
    // skipped rather than half-registered.
    //
    // ONE VOCABULARY, three sets: frame, card, button. Each set is nine files named
    // <set>_<part>.tga, and the set name is also its section name in the theme ini --
    // so `[frame] size` sizes the frame_*.tga art, and there is nothing to translate
    // between the file you open and the key that governs it.
    //
    // Each file is drawn AS AUTHORED (see NineSlice::build), so frame_corner_tr.tga is
    // a picture of a top-right corner and frame_edge_left.tga of a left edge. There is
    // deliberately no one-file-serves-four shorthand: it would need the renderer to
    // rotate, and every corner file would then look like a top-left corner in an
    // image editor.
    int centerSprite = 0;
    int cornerSprites[4] = {0, 0, 0, 0};   // NineSlice::Corner order: TL,TR,BL,BR
    int edgeSprites[4]   = {0, 0, 0, 0};   // NineSlice::Side order:   TOP,BOTTOM,LEFT,RIGHT

    // Registration list, in the exact order discovery assigned indices. HudManager
    // pushes these verbatim; the two orders must not diverge.
    // The files THIS folder registers, in registration order. A skin lists only
    // what it redraws -- often nothing at all -- because a sprite it does not
    // provide keeps the base's already-registered index. That is what makes
    // HudManager's loop correct without knowing about bases: every stem in here
    // really does live in this theme's own folder.
    std::vector<std::string> spriteFiles;

    // The 1-based sprite index discovery assigned to spriteFiles[0]; -1 while the
    // folder has registered nothing. Indices within one folder are consecutive, so
    // spriteFiles[k] holds index firstOwnSprite + k -- the contract
    // HudManager::verifySpriteRegistrationOrder() checks against the registered
    // sprite table after setupDefaultResources() (the theme-sprite-order
    // invariant, CLAUDE.md).
    int firstOwnSprite = -1;

    // Corner size in GRID CELLS -- how many cells of the horizontal lattice the
    // corner art spans. The Y extent is that same distance in pixels, so a corner is
    // square on screen (equal x/y deltas are NOT equal pixels in 16:9 normalized
    // space); NineSlice::cellsToBorderX / cellsToBorderY are the only converters.
    //
    // Whole cells, and that is the whole point of the unit. A normalized fraction
    // ceiled onto the grid makes the knob lie: a whole range of values produces the
    // same cell margin while continuing to resize the drawn art. In cells the
    // number IS the margin, the art follows it, and the frame lands on the lattice
    // by construction.
    float frameBorder = 3.0f;

    // Whether the HUD's background COLOUR is applied to the sprites.
    //
    // The renderer modulates texel * tint, so this is all-or-nothing per theme and
    // cannot be mixed within one:
    //   tintable (default) -- sprites are white + alpha, so the HUD's background
    //     colour and opacity recolour them. One theme serves any palette.
    //   not tintable -- the sprites carry their own colours, so the tint must be
    //     WHITE (opacity only) or those colours are multiplied away. This is what a
    //     theme whose identity IS its colour needs; the cost is that the background
    //     colour setting no longer affects it.
    // Getting this wrong is silent and looks like "the theme is just dark": a baked
    // cyan glow times a near-black background is near-black.
    bool tintable = true;

    // THE CARD SET: a second, quieter 9-slice for elements drawn INSIDE a panel --
    // HUD title bands and (settings) section cards -- rather than for the frame.
    //
    // It exists because reusing the frame slices for these does not work. A panel's
    // corner treatment is distinctive by design; repeat it a few pixels inside
    // itself and it stops reading as a motif and starts reading as noise. The more
    // character a theme has, the worse that gets.
    //
    // card_center.tga enables the set; the per-position card_corner_*.tga /
    // card_edge_*.tga are optional and fall back to card_center rather than dropping
    // the theme, since a partial set still draws a perfectly good flat band. A theme
    // with no card_center simply has no title bands or section cards.
    int cardCenterSprite = 0;
    int cardCornerSprites[4] = {0, 0, 0, 0};
    int cardEdgeSprites[4]   = {0, 0, 0, 0};
    // Usually much smaller than the frame `inset`: a card is a band, not a panel.
    float cardBorder = 2.0f;   // Card (title band / body card) corner, in cells

    // THE TITLE BAND'S OWN CORNER SIZE, when a theme wants one different from the body
    // card's. Same nine sprites -- this is a SCALE, not a second art set.
    //
    // -1 IS THE POINT of the sentinel: the two sizes started as one number and most
    // themes want them to stay one, so an absent `band-size` must mean "whatever `size`
    // is" and keep following it when a skinner edits `size` alone. A default of 2.0f
    // matching cardBorder's would not -- it would freeze the band at 2 the moment the
    // card moved, silently, for every theme that never mentioned the key.
    //
    // Read through titleBorder(), never directly.
    float titleBorderOverride = -1.0f;
    float titleBorder() const { return (titleBorderOverride >= 0.0f) ? titleBorderOverride : cardBorder; }

    // [panel] padding-x / padding-y -- the gap between a panel's BORDER and its first
    // glyph, in CELLS. -1 = follow the built-in (LayoutMetrics::panelPadding*Cells),
    // same sentinel-not-a-copied-default reasoning as titleBorderOverride above.
    //
    // These live here and not on LayoutMetrics because LayoutMetrics is GLOBAL -- one
    // lattice shared by every HUD, which is what lets HUDs align to each other -- while
    // this is a per-theme choice. BaseHud::basePadding{X,Y}() resolves the pair; nothing
    // else may read these directly, because the fallback is the whole contract.
    //
    // A KNOB THAT CAN GO DEAD, and that is inherent rather than an oversight: content
    // must clear the frame's border, so contentPadding{X,Y}() takes the WIDER of this
    // and (frame + inner). Set it below that and it has no effect -- a knob that
    // lies -- so the mock flags when the border is what is setting the padding.
    float panelPaddingXOverride = -1.0f;
    float panelPaddingYOverride = -1.0f;

    // `[content] gap` -- THE VISIBLE GAP between two carded boxes, in CELLS. -1 = follow
    // the built-in (LayoutMetrics::sectionGap). See that field for the three boundaries
    // this is spent at; read it through BaseHud::contentGapY(), never directly.
    float sectionGapOverride = -1.0f;
    float sectionGap(float builtIn) const {
        return (sectionGapOverride >= 0.0f) ? sectionGapOverride : builtIn;
    }
    bool hasCard() const { return cardCenterSprite > 0; }

    // ---- THE BOX-MODEL SURFACE — the eleven keys ---------------------------
    // [panel] border/padding, [title]/[content]/[button] margin/border/padding,
    // each in CSS shorthand (core/panel_box.h parseSides), stored RAW in
    // x-cells per side. `set` false = the theme never named the key; the
    // resolver falls back — border keys to the per-set scalars above
    // (frameBorder, cardBorder, titleBorder(), buttonBorder), air keys to the
    // built-ins — so nothing may read `.v` without checking `.set` (the same
    // sentinel-not-a-copied-default rule titleBorderOverride states).
    //
    // The parser also FEEDS the legacy scalars from these keys where a value is
    // uniform (see themeBoxTerm in asset_manager.cpp), so a theme written in
    // the new vocabulary drives the pre-box-model layout too. That bridge — and
    // the scalars themselves — go away when the box-model port completes.
    struct BoxTerm {
        PanelBox::Sides v{};
        bool set = false;
    };
    BoxTerm boxPanelBorder, boxPanelPadding;
    BoxTerm boxTitleMargin, boxTitleBorder, boxTitlePadding;
    BoxTerm boxContentMargin, boxContentBorder, boxContentPadding;
    BoxTerm boxButtonMargin, boxButtonBorder, boxButtonPadding;
    // Junction-only air between stacked children ([panel] gap). A SCALAR key
    // (one number, no shorthand); stored as a uniform Sides so it rides the
    // same table — read it as `.v.t`.
    BoxTerm boxPanelGap;

    // DOES THIS THEME DRAW ANYTHING. False only for the null theme below, which is what
    // "no panel theme" resolves to -- see nullTheme(). The frame set is mandatory
    // for a real theme (a directory missing any of its nine files is skipped rather
    // than half-registered), so its centre sprite is the whole test.
    bool hasArt() const { return centerSprite > 0; }

    // THE ABSENCE OF A THEME, AS A THEME. resolveActiveTheme() hands this back instead
    // of nullptr, so every layout helper reads one shape and no caller has to branch:
    // its insets are zero, so frameBorderX/Y() and cardBorderX/Y() come out
    // zero on their own, and hasCard()/hasArt() are false. The ONLY thing that still
    // branches is the painting -- a themed panel is nine slices, an unthemed one is a
    // single flat quad -- which is the difference a reader would expect to be there.
    static const ThemeAsset& nullTheme() {
        static const ThemeAsset kNull = [] {
            ThemeAsset t;
            t.frameBorder = 0.0f; t.cardBorder = 0.0f; t.buttonBorder = 0.0f;
            return t;
        }();
        return kNull;
    }

    // BUTTON slice set: the theme's shape for small interactive rectangles.
    //
    // Separate from the card set because a button's COLOUR is semantic and comes
    // from the caller -- green for save, muted for disabled, accent for hover -- so
    // these sprites are ALWAYS white + alpha and always tinted, even for a theme
    // whose other slices bake their own colours. A baked button would throw away
    // the state the colour is communicating.
    //
    // button_center.tga enables the set, plus the per-position button_corner_*.tga /
    // button_edge_*.tga, same fallback-to-center rule as the card set.
    int buttonCenterSprite = 0;
    int buttonCornerSprites[4] = {0, 0, 0, 0};
    int buttonEdgeSprites[4]   = {0, 0, 0, 0};
    float buttonBorder = 1.0f;  // Button corner, in cells
    bool hasButton() const { return buttonCenterSprite > 0; }

    // ICON OVERRIDES: themes/<name>/icons/<icon>.tga, keyed by the icon's filename
    // stem, valued with the sprite index the override was registered at.
    //
    // REPLACES ART, NEVER THE VOCABULARY. A theme may only override a name the base
    // set already has; an unknown filename is ignored with a warning. That rule is
    // what makes the feature free of consequences: a rider's marker persists as a
    // NAME (settings_serde.h's shapeIndexToFilename, and the tracked-riders JSON's
    // "icon"), so an override cannot orphan a saved choice, cannot move a shape
    // index, and cannot change what the shape picker offers. Letting a theme ADD
    // glyphs would break all three -- and the user-override directory already covers
    // that need, since it syncs new .tga into the base set before discovery.
    //
    // SPARSE: lookup falls back per NAME, not per theme, so a theme shipping three
    // icons inherits the other hundred-odd unchanged.
    std::map<std::string, int> iconOverrides;
    // The same mapping the other way -- override sprite index -> base shape index --
    // so a sprite can be turned back into the vocabulary position it stands for. See
    // AssetManager::shapeIndexForSprite for what needs that and why.
    std::map<int, int> iconOverrideShape;

    // Draw a themed card behind the HUD's title row (the theme ini's titleBand).
    // Unlike everything else here this is NOT part of the frame -- it is extra
    // geometry where the HUD drew nothing -- so a theme whose look does not want a
    // banded header turns it off. It reuses the theme's own nine slices at half the
    // inset; a theme wanting a genuinely different title treatment would need its
    // own sprite set, which is deliberately not built until one does.
    bool titleBand = true;

    // PER-FAMILY OVERRIDES of titleBand / contentCard, one slot per PanelKind, each
    // -1 = "no opinion, follow the value above". A theme can then say "bands
    // everywhere, but no body card on the gauges" without a key per widget.
    //
    // Tri-state rather than a bool pair, because a bool cannot distinguish "this
    // theme wants it off" from "this theme never mentioned it" -- and only the second
    // may inherit. That is the same distinction colorSet[] draws for the palette, for
    // the same reason.
    //
    // Indexed by PanelKind (Hud, Widget, Settings); kept as plain ints so this header
    // does not have to see base_hud.h.
    // Did the ini MENTION [card] at all? Discovery-only, and it exists because
    // titleBand DEFAULTS to true: without it, "you asked for a card and this theme
    // has no card slices to draw one with" could not be told apart from "this theme
    // never mentioned cards", and every plain-frame theme would warn on every load.
    bool cardKeysSet = false;

    int titleBandFor[3]   = { -1, -1, -1 };
    int contentCardFor[3] = { -1, -1, -1 };
    bool titleBandKind(int kind) const {
        return (kind >= 0 && kind < 3 && titleBandFor[kind] >= 0) ? titleBandFor[kind] != 0
                                                                  : titleBand;
    }
    bool contentCardKind(int kind) const {
        return (kind >= 0 && kind < 3 && contentCardFor[kind] >= 0) ? contentCardFor[kind] != 0
                                                                   : contentCard;
    }

    // Draw a themed card behind the HUD's CONTENT block -- the rows of a
    // standings table, an event log, a telemetry graph. The same idea as titleBand
    // one step down: a title band frames the header, this frames what the HUD is
    // actually showing, so a themed HUD reads as header + body rather than as text
    // floating inside a frame.
    //
    // Off by default, unlike titleBand. It is new geometry over the panel's whole
    // body, so a theme whose card slices are a subtle band can look heavy-handed
    // wrapped around a 22-row grid -- the theme opts in once it has looked at it.
    bool contentCard = false;

    bool valid() const {
        return centerSprite > 0 && cornerSprites[0] > 0 && edgeSprites[0] > 0;
    }
};

// ============================================================================
// GAMEPAD PACKS -- gamepads/<name>/gamepad.ini plus its .tga set.
//
// A pad is a PACK rather than a texture variant because the artwork alone was
// never enough: every button position is per-pad data (see PadGeometry), so the
// picture and the numbers that place things on it have to travel together. It
// also means the SETTING can name the pad instead of indexing discovery order --
// an index silently reassigns every user's selection the moment a pack is added,
// removed or renamed, which is the same rule icon overrides and themes follow.
// ============================================================================
namespace GamepadSprite {
    // Every sprite one pad pack contributes, in registration order.
    enum Part {
        BACKGROUND = 0,      // the controller photograph
        STICK,
        DPAD_BUTTON,
        MENU_BUTTON,
        TRIGGER_L, TRIGGER_R,
        BUMPER_L, BUMPER_R,
        FACE,                // generic face button; the menu button's fallback art
        FACE_1, FACE_2, FACE_3, FACE_4,
        FACE_1_PRESSED, FACE_2_PRESSED, FACE_3_PRESSED, FACE_4_PRESSED,
        COUNT
    };

    // THE REGISTRY. Discovery walks this table to hand out sprite indices and
    // HudManager walks the SAME table to register the files, so the two orders
    // cannot diverge -- the failure themes can still have (a pack drawing another
    // pack's sprites) is not expressible here, because there is only one order.
    //
    // Indexed by Part; the static_assert below is what keeps them in step.
    inline constexpr const char* kStems[] = {
        "background",
        "stick",
        "dpad_button",
        "menu_button",
        "trigger_button_l", "trigger_button_r",
        "bumper_button_l",  "bumper_button_r",
        "face_button",
        "face_button_1", "face_button_2", "face_button_3", "face_button_4",
        "face_button_1_pressed", "face_button_2_pressed",
        "face_button_3_pressed", "face_button_4_pressed",
    };
    static_assert(sizeof(kStems) / sizeof(kStems[0]) == COUNT,
                  "kStems must name exactly one file per GamepadSprite::Part -- adding a "
                  "Part without its stem would shift every later pack's sprite indices");

    // GamepadWidget::addFaceButton indexes these two runs arithmetically
    // (FACE_1 + n, FACE_1_PRESSED + n) from the button number, so reordering the
    // enum would silently swap A with X rather than fail to build.
    static_assert(FACE_2 == FACE_1 + 1 && FACE_3 == FACE_1 + 2 && FACE_4 == FACE_1 + 3,
                  "FACE_1..FACE_4 must stay contiguous and in order");
    static_assert(FACE_2_PRESSED == FACE_1_PRESSED + 1 && FACE_3_PRESSED == FACE_1_PRESSED + 2 &&
                  FACE_4_PRESSED == FACE_1_PRESSED + 3,
                  "FACE_1_PRESSED..FACE_4_PRESSED must stay contiguous and in order");
}

// PIT BOARD PACKS -- pitboards/<name>/pitboard.ini plus its art.
//
// Same shape and same reasoning as a gamepad pack, one sprite instead of
// seventeen. What it buys is different, though: the numbers that place text on
// the board travel WITH the picture, so a custom board can be handed to anyone.
// Art in a flat textures/ folder with its layout in the user's own settings file
// cannot -- the picture travels and its layout does not.
namespace PitboardSprite {
    enum Part {
        BACKGROUND = 0,   // the board artwork
        COUNT
    };

    // THE REGISTRY, exactly as GamepadSprite::kStems is: discovery walks it to
    // hand out sprite indices and HudManager walks the same table to register the
    // files. One order, so the two cannot diverge. One entry today; the mechanism
    // is the point, since a second sprite would otherwise need a hand-matched
    // list, which is the failure this shape exists to rule out.
    inline constexpr const char* kStems[] = {
        "background",
    };
    static_assert(sizeof(kStems) / sizeof(kStems[0]) == COUNT,
                  "kStems must name exactly one file per PitboardSprite::Part");
}

struct PitboardAsset {
    std::string name;          // directory name, e.g. "classic"
    std::string displayName;   // [board] name, else generated from the directory

    // [board] base -- the pack this one layers over, empty for a standalone
    // pack. The SPOTTER rule, made explicit because boards can have several
    // parents: what this pack's folder and ini state wins, what they leave out
    // is answered from the base. One level only -- a base must itself be
    // baseless -- which kills chains and cycles with a single check.
    std::string baseName;

    // The artwork's proportions and the per-row offsets on it. For a based
    // pack this starts as a COPY of the base's geometry, so an omitted ini key
    // means "same as the base" rather than "built-in default".
    PitboardLayout::BoardGeometry geometry;

    // Per Part: does the sprite file resolve from the BASE pack's folder
    // (true) or this pack's own (false)? Registration reads this so the file
    // list matches what discovery accepted.
    bool spriteFromBase[PitboardSprite::COUNT] = {};

    // Sprite index per PitboardSprite::Part. A pack is only accepted with its
    // whole RESOLVED set present (own file, else the base's), so these are all
    // non-zero or the pack is not in m_pitboards at all.
    int sprites[PitboardSprite::COUNT] = {};
};

// GAUGES PACKS -- gauges/<name>/gauge.ini plus its two dial faces.
//
// The third pack type. The ticks and figures are painted into the .tga, so the
// needle's range and sweep must travel with the face rather than be compiled in;
// see hud/gauge_geometry.h for why a compiled-in placement mis-draws any face but
// the shipped one.
//
// BOTH FACES IN ONE PACK, not gauges split across a tacho/ and a speedo/ root.
// They are drawn as a set, so splitting them would mean picking the same name
// twice for the matched case; and the mixed case is already answered twice over
// -- each widget selects its own pack, and `base =` makes "my tacho over the
// shipped speedo" a two-file pack.
namespace GaugeSprite {
    enum Part {
        TACHO = 0,   // the rev-counter face
        SPEEDO,      // the speedometer face
        COUNT
    };

    // THE REGISTRY, exactly as GamepadSprite::kStems and PitboardSprite::kStems
    // are: discovery walks it to hand out sprite indices and HudManager walks the
    // same table to register the files. One order, so the two cannot diverge.
    inline constexpr const char* kStems[] = {
        "tacho",
        "speedo",
    };
    static_assert(sizeof(kStems) / sizeof(kStems[0]) == COUNT,
                  "kStems must name exactly one file per GaugeSprite::Part");
}

struct GaugesAsset {
    std::string name;          // directory name, e.g. "classic"
    std::string displayName;   // [gauges] name, else generated from the directory

    // [gauges] base -- see PitboardAsset::baseName; same rule, same one-level
    // limit. Here it is also how a MIXED set is expressed: a pack with
    // `base = classic` and only its own tacho.tga is a custom rev-counter on the
    // shipped speedometer.
    std::string baseName;

    // What each face reads and what its needle looks like. For a based pack this
    // starts as a COPY of the base's, so an omitted key means "same as the base"
    // rather than "built-in default".
    GaugeLayout::GaugeGeometry geometry;

    // Per Part: file resolves from the base pack's folder (true) or this pack's
    // own (false). See PitboardAsset.
    bool spriteFromBase[GaugeSprite::COUNT] = {};

    // Sprite index per GaugeSprite::Part. A pack is only accepted with its whole
    // RESOLVED set present, so these are all non-zero or the pack is not in
    // m_gauges at all -- there is no half-registered set with one blank face.
    int sprites[GaugeSprite::COUNT] = {};
};

struct GamepadAsset {
    std::string name;          // directory name, e.g. "xbox"
    std::string displayName;   // [pad] name, else generated from the directory name

    // [pad] base -- see PitboardAsset::baseName; same rule, same one-level
    // limit. This is what makes a reskin two files (ini + background.tga)
    // instead of a copy of all seventeen sprites and the geometry.
    std::string baseName;

    // Where this pad's buttons sit on its artwork. Defaults until the ini is
    // read; for a based pack, a copy of the base's geometry first.
    GamepadLayout::PadGeometry geometry;

    // Per Part: file resolves from the base pack's folder (true) or this
    // pack's own (false). See PitboardAsset.
    bool spriteFromBase[GamepadSprite::COUNT] = {};

    // Sprite index per GamepadSprite::Part. A pack is only accepted with the whole
    // RESOLVED set present (own file, else the base's -- see discoverGamepads), so
    // these are all non-zero or the pack is not in m_gamepads at all -- there is no
    // half-registered pad drawing holes.
    int sprites[GamepadSprite::COUNT] = {};
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
    // The heavier cut of the face at this engine index, or 0. m_fonts is pushed
    // in index order (fontIndex == position + 1), so this is a subscript, not a
    // search -- getFont() calls it at every addString against a 2.08ms budget.
    int getEmphasisFontIndex(int fontIndex) const {
        const size_t pos = static_cast<size_t>(fontIndex - 1);
        return (fontIndex >= 1 && pos < m_fonts.size()) ? m_fonts[pos].emphasisIndex : 0;
    }
    int getFontIndexByName(const std::string& name) const;
    // const char* overload: FontConfig::getFont() runs at every addString, and
    // binding a const char* to the std::string parameter allocated on each call for
    // the four shipped font names that exceed SSO. Same lookup, no temporary.
    int getFontIndexByName(const char* name) const;

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

    // Get pixel aspect ratio (width/height) for a sprite index
    // Returns 0.0f if not found (caller should skip aspect ratio correction)
    float getTextureAspectRatio(int spriteIndex) const;

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
    // The sprite to DRAW for an icon name: the active theme's override if it has
    // one, else the base set's. Every draw site goes through here, which is how a
    // theme restyles HUD identity icons, settings-tab icons and rider markers alike
    // without any of them knowing themes exist.
    //
    // Resolved against the GLOBALLY selected theme, not a HUD's per-panel theme
    // override: that override exists to change one panel's frame, and icons are a
    // shared vocabulary -- the same glyph meaning two things on two panels is worse
    // than a pinned panel keeping the global icon set.
    int getIconSpriteIndex(const std::string& name) const;

    // Get icon filename by sprite index (returns empty string if not found)
    std::string getIconFilename(int spriteIndex) const;

    // Get icon display name by sprite index (returns empty string if not found)
    std::string getIconDisplayName(int spriteIndex) const;

    // THE BASE set's sprite for a name, ignoring any theme override.
    //
    // For IDENTITY, not for drawing: the name <-> shape-index mapping that settings
    // persistence and the shape pickers are built on. Those must answer the same way
    // whatever theme is on, or switching a theme would renumber every saved marker.
    int getBaseIconSpriteIndex(const std::string& name) const;

    // The base shape index a sprite belongs to -- the inverse of iconSpriteForShape,
    // and it has to be the inverse for an OVERRIDE sprite too.
    //
    // THE TRAP THIS EXISTS FOR: several marker paths pick a sprite by name and then
    // round-trip it back to a shape index to ask shouldRotate() whether the glyph is
    // directional. `sprite - firstIcon + 1` answers that correctly only while every
    // icon sprite lives in the base block -- a theme override sits past it, so the
    // subtraction returns a number off the end of the vocabulary and the rotation
    // check reads an empty filename. Returns 0 for a sprite that is not an icon.
    int shapeIndexForSprite(int spriteIndex) const;

    // The sprite to DRAW for a 1-based shape index, theme override applied. The
    // arithmetic (firstIcon + shape - 1) is here rather than at each call site
    // precisely because the override has to ride along with it.
    int iconSpriteForShape(int shapeIndex) const;

    // Get first icon sprite index (for calculating offsets)
    int getFirstIconSpriteIndex() const { return m_firstIconSpriteIndex; }

    // True if the 1-based shape index maps to a HUD identity icon (filename starts
    // with "hud-"). These are reserved for HUD titles/tabs and should be skipped by
    // rider/marker shape pickers so they only cycle "real" marker glyphs.
    bool isHudIdentityShape(int shapeIndex) const;

    // Step a shape index by +/-1 with wrap, skipping HUD identity icons. With
    // allowOff (the default) the range is [0..getIconCount()] where 0 is the
    // "Off"/default slot (always eligible); otherwise the range is [1..count] with
    // no off slot (radar shapes). Returns the nearest eligible index (or the input
    // unchanged if somehow none are eligible).
    int stepShapeIndexSkippingHud(int shapeIndex, bool forward, bool allowOff = true) const;

    // ========================================================================
    // Theme Access (9-slice panel themes)
    // ========================================================================

    // All discovered themes, sorted by name. May be empty (themes are optional).
    const std::vector<ThemeAsset>& getThemes() const { return m_themes; }

    // Bumped whenever anything a theme lookup depends on changes -- discovery, a
    // reload, or the selected theme name. BaseHud memoises its resolved ThemeAsset*
    // against this: getThemeByName() is a linear string scan, and one HUD rebuild
    // asks for it dozens of times (every themed* helper resolves it, and each calls
    // layout(), which resolves it again) -- unmemoised, a per-frame slide loop runs
    // hundreds of scans a frame against a 2.08ms budget.
    unsigned int themeGeneration() const { return mxbThemeGeneration(); }
    void bumpThemeGeneration() { mxbBumpThemeGeneration(); }

#if defined(MXBMRP3_TEST_BUILD)
    // TEST ONLY: give the CURRENTLY SELECTED theme an opinion about one colour slot,
    // so a test can distinguish "inherited from the theme" from "built-in default".
    void testSetActiveThemeColor(ColorSlot slot, unsigned long abgr) {
        const std::string& sel = UiConfig::getInstance().getThemeName();
        for (auto& t : m_themes) {
            if (t.name != sel) continue;
            t.colors[static_cast<size_t>(slot)] = abgr;
            t.colorSet[static_cast<size_t>(slot)] = true;
            mxbBumpThemeGeneration();
            return;
        }
    }

    // TEST ONLY: register a fully-formed theme without any files on disk.
    //
    // Every themed layout rule this project has -- the title band, the body card,
    // the section protocol, the frame clearances -- is arithmetic over a
    // ThemeAsset's insets and flags. discoverThemes() is what needs a directory of
    // .tga files; the geometry does not. So a headless test builds the asset
    // directly and exercises the real emit path, instead of the integration suite
    // needing the shipped themes staged next to it.
    //
    // Callers set the sprite indices themselves (see MXBMRP3_Test_InstallTheme,
    // which sets them nonzero so hasCard() is true); no test asserts WHICH texture
    // a slice drew, and the emitters never validate them. Removed from every
    // shipping target with the rest of the test surface (see mxbmrp3/CMakeLists.txt).
    void installSyntheticTheme(const ThemeAsset& theme) {
        // REPLACE BY NAME, don't append. getThemeByName() takes the FIRST match, so
        // appending a re-installed name adds a second entry that can never be
        // selected while the first one's geometry keeps answering -- a case that
        // sweeps a theme parameter under one name then reads the value it started
        // with, and passes.
        for (size_t i = 0; i < m_themes.size(); ++i) {
            if (m_themes[i].name == theme.name) {
                m_themes[i] = theme;          // a fresh install is FRESH, overrides included
                m_lastSyntheticTheme = i;
                mxbBumpThemeGeneration();
                return;
            }
        }
        m_lastSyntheticTheme = m_themes.size();
        m_themes.push_back(theme);
        mxbBumpThemeGeneration();   // invalidate every HUD's memoised activeTheme()
    }
    // Index of the theme installSyntheticTheme last touched. Not m_themes.back():
    // a re-installed name is replaced IN PLACE, so "the one just installed" and
    // "the last one in the vector" are not the same entry.
    size_t m_lastSyntheticTheme = 0;
    // TEST ONLY: add a pack without files, for the same reason installSyntheticTheme
    // exists -- discoverGamepads() wants a directory of 17 .tga, while the behaviour
    // under test (name resolution and the degrade-to-default rule) wants only a name.
    // See MXBMRP3_Test_InstallGamepad.
    void installSyntheticGamepad(const GamepadAsset& pad) {
        // REPLACE BY NAME, for the reason installSyntheticTheme does: the
        // by-name getters take the FIRST match, so an appended re-install could
        // never be selected while the old one kept answering.
        for (GamepadAsset& existing : m_gamepads) {
            if (existing.name == pad.name) { existing = pad; return; }
        }
        m_gamepads.push_back(pad);
    }
    // TEST ONLY: the singleton outlives an individual test case, so a case that
    // needs "no packs installed" has to be able to say so rather than depend on
    // running before every case that installs one.
    void clearSyntheticGamepads() { m_gamepads.clear(); }
    // TEST ONLY: board packs, same contract as the two gamepad calls above.
    void installSyntheticPitboard(const PitboardAsset& pack) {
        for (PitboardAsset& existing : m_pitboards) {   // by name, see the gamepad call
            if (existing.name == pack.name) { existing = pack; return; }
        }
        m_pitboards.push_back(pack);
    }
    void clearSyntheticPitboards() { m_pitboards.clear(); }
    // TEST ONLY: part the band's corner size from the card's on the most recently
    // installed synthetic theme; a negative value puts it back on `size`. Injected
    // afterwards rather than passed to MXBMRP3_Test_InstallTheme so that every
    // existing case keeps installing the shape a theme has by DEFAULT -- one size for
    // both -- and only a case about `band-size` mentions it.
    bool testSetThemeTitleBorder(float cells) {
        if (m_themes.empty() || m_lastSyntheticTheme >= m_themes.size()) return false;
        m_themes[m_lastSyntheticTheme].titleBorderOverride = cells;
        mxbBumpThemeGeneration();
        return true;
    }
    // TEST ONLY: set `[panel] padding-x/-y` on the most recently installed synthetic
    // theme; a negative value puts that axis back on the built-in. Same
    // inject-afterwards reasoning as testSetThemeTitleBorder above.
    bool testSetThemePanelPadding(float xCells, float yCells) {
        if (m_themes.empty() || m_lastSyntheticTheme >= m_themes.size()) return false;
        ThemeAsset& t = m_themes[m_lastSyntheticTheme];
        // Mirror what a real `[panel] padding` key applies — BOTH vocabularies,
        // exactly as the ini bridge does: the box term the plan panels read,
        // and the legacy overrides the own-geometry holdouts read. Negative =
        // the absent-key sentinel on both.
        t.panelPaddingXOverride = xCells;
        t.panelPaddingYOverride = yCells;
        if (xCells >= 0.0f && yCells >= 0.0f) {
            t.boxPanelPadding.v = PanelBox::Sides{
                static_cast<double>(yCells), static_cast<double>(xCells),
                static_cast<double>(yCells), static_cast<double>(xCells)};
            t.boxPanelPadding.set = true;
        } else {
            t.boxPanelPadding.set = false;
        }
        mxbBumpThemeGeneration();
        return true;
    }
    // TEST ONLY: set an ASYMMETRIC `[content] border` on the most recently installed
    // synthetic theme. The scalar cardBorder that InstallTheme takes can only be
    // uniform, and a uniform border hides a whole class of fault: the content band is
    // inset by border.t at the top and border.b at the bottom, so it shares a centre
    // with the card it sits in ONLY while the two are equal. Every shipped theme is
    // symmetric, so anything centred in the band instead of the card looked right
    // everywhere until a skinner wrote `border = 2 0 4 6`.
    bool testSetThemeContentBorder(float t_, float r_, float b_, float l_) {
        if (m_themes.empty() || m_lastSyntheticTheme >= m_themes.size()) return false;
        ThemeAsset& t = m_themes[m_lastSyntheticTheme];
        t.boxContentBorder.v = PanelBox::Sides{
            static_cast<double>(t_), static_cast<double>(r_),
            static_cast<double>(b_), static_cast<double>(l_)};
        t.boxContentBorder.set = true;
        mxbBumpThemeGeneration();
        return true;
    }
    // TEST ONLY: same for `[title] margin` -- moves the TITLE band inside the
    // panel the way the content margin moves the card.
    bool testSetThemeTitleMargin(float t_, float r_, float b_, float l_) {
        if (m_themes.empty() || m_lastSyntheticTheme >= m_themes.size()) return false;
        ThemeAsset& t = m_themes[m_lastSyntheticTheme];
        t.boxTitleMargin.v = PanelBox::Sides{
            static_cast<double>(t_), static_cast<double>(r_),
            static_cast<double>(b_), static_cast<double>(l_)};
        t.boxTitleMargin.set = true;
        mxbBumpThemeGeneration();
        return true;
    }
    // TEST ONLY: same for `[content] margin` -- the term that moves the CARD
    // inside the panel, which is what separates the card's centre from the
    // panel's. See testSetThemeContentBorder.
    bool testSetThemeContentMargin(float t_, float r_, float b_, float l_) {
        if (m_themes.empty() || m_lastSyntheticTheme >= m_themes.size()) return false;
        ThemeAsset& t = m_themes[m_lastSyntheticTheme];
        t.boxContentMargin.v = PanelBox::Sides{
            static_cast<double>(t_), static_cast<double>(r_),
            static_cast<double>(b_), static_cast<double>(l_)};
        t.boxContentMargin.set = true;
        mxbBumpThemeGeneration();
        return true;
    }
    // TEST ONLY: set `[panel] gap` on the most recently installed synthetic
    // theme; negative puts it back on the built-in. Gap has no legacy scalar,
    // so unlike the padding hook there is only the box term to write.
    bool testSetThemeGap(float cells) {
        if (m_themes.empty() || m_lastSyntheticTheme >= m_themes.size()) return false;
        ThemeAsset& t = m_themes[m_lastSyntheticTheme];
        if (cells >= 0.0f) {
            const double c = static_cast<double>(cells);
            t.boxPanelGap.v = PanelBox::Sides{c, c, c, c};
            t.boxPanelGap.set = true;
        } else {
            t.boxPanelGap.set = false;
        }
        mxbBumpThemeGeneration();
        return true;
    }
    // TEST ONLY: add an icon override to the most recently installed synthetic theme.
    // False if there is none. See MXBMRP3_Test_SetThemeIconOverride.
    bool testSetThemeIconOverride(const std::string& icon, int sprite, int shape) {
        if (m_themes.empty() || m_lastSyntheticTheme >= m_themes.size()) return false;
        ThemeAsset& t = m_themes[m_lastSyntheticTheme];
        t.iconOverrides[icon] = sprite;
        t.iconOverrideShape[sprite] = shape;
        mxbBumpThemeGeneration();
        return true;
    }
#endif
    size_t getThemeCount() const { return m_themes.size(); }

    // Look up a theme by directory name; nullptr if absent (e.g. the user removed
    // the folder but the setting still names it -- callers fall back to the plain
    // solid background, which is why this must stay a nullable lookup).
    const ThemeAsset* getThemeByName(const std::string& name) const;

    // Registration path for one of a theme's three sprites. `part` is
    // "corner" | "edge" | "center".
    std::string getThemePath(const std::string& themeName, const char* part) const;

    // ========================================================================
    // Gamepad Pack Access
    // ========================================================================

    const std::vector<GamepadAsset>& getGamepads() const { return m_gamepads; }
    size_t getGamepadCount() const { return m_gamepads.size(); }

    // Look up a pad pack by directory name; nullptr if absent. Nullable for the
    // same reason getThemeByName() is: the setting names a pack the user may have
    // deleted, and the caller falls back to the shipped default rather than to
    // "no pad at all" (see GamepadWidget::activePack).
    const GamepadAsset* getGamepadByName(const std::string& name) const;

    // The pack a setting should fall back TO: the shipped default if present, else
    // the first discovered pack, else nullptr when no packs are installed.
    const GamepadAsset* getDefaultGamepad() const;

    // Registration path for one of a pack's sprites. `stem` comes from
    // GamepadSprite::kStems.
    std::string getGamepadPath(const std::string& packName, const char* stem) const;

    // The pack name a fresh install selects.
    static constexpr const char* DEFAULT_GAMEPAD = "xbox";

    // ========================================================================
    // Pit Board Pack Access -- mirrors the gamepad pack calls above.
    // ========================================================================

    const std::vector<PitboardAsset>& getPitboards() const { return m_pitboards; }
    size_t getPitboardCount() const { return m_pitboards.size(); }

    // Nullable for the same reason getGamepadByName() is: the setting names a
    // pack the user may have deleted, and the caller degrades to the shipped
    // default rather than to "no board".
    const PitboardAsset* getPitboardByName(const std::string& name) const;
    const PitboardAsset* getDefaultPitboard() const;

    std::string getPitboardPath(const std::string& packName, const char* stem) const;

    static constexpr const char* DEFAULT_PITBOARD = "classic";

    // ========================================================================
    // Gauges Pack Access -- mirrors the two pack blocks above.
    // ========================================================================

    const std::vector<GaugesAsset>& getGauges() const { return m_gauges; }
    size_t getGaugesCount() const { return m_gauges.size(); }

    const GaugesAsset* getGaugesByName(const std::string& name) const;
    const GaugesAsset* getDefaultGauges() const;

    std::string getGaugesPath(const std::string& packName, const char* stem) const;

    static constexpr const char* DEFAULT_GAUGES = "classic";
    // The pack migrateLegacyGaugeArt() writes, and the one getDefaultGauges()
    // prefers when it exists. Named here so the writer and the reader cannot
    // disagree about the folder.
    static constexpr const char* LEGACY_GAUGES = "legacy";

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
    static constexpr const char* THEMES_SUBDIR = "themes";
    static constexpr const char* GAMEPADS_SUBDIR = "gamepads";
    static constexpr const char* PITBOARDS_SUBDIR = "pitboards";
    static constexpr const char* SPOTTERS_SUBDIR = "spotters";
    static constexpr const char* GAUGES_SUBDIR = "gauges";
    static constexpr const char* WEB_SUBDIR = "web";
    // User override directory (under savePath, e.g., Documents\PiBoSo\MX Bikes\mxbmrp3\)
    static constexpr const char* USER_OVERRIDE_DIR = "mxbmrp3";

    // The nested PACK types, in one table.
    //
    // A pack is a folder the user can author and hand to someone else:
    // <root>\<name>\ holding that type's payload plus a <type>.ini describing it
    // (theme.ini, gamepad.ini, ... -- see core/pack_ini_path.h).
    // All four share that shape, and all four need the same two things done for
    // them -- copied out of the user's Documents folder at startup, and copied
    // again on RELOAD_CONFIG so an author's edit-reload loop works.
    //
    // WHY A TABLE AND NOT FOUR PAIRS OF CALLS. Separate pairs drift, and a pack
    // type wired into neither copy is one a user cannot author in their own
    // Documents folder at all -- it has to be dropped into the game's plugins
    // tree, where an uninstall removes it. Both copy sites walk THIS list, so a
    // row is the whole job.
    //
    // `media` is the type's non-ini payload, stated per type rather than a blanket
    // "*.*": the copy is a trust boundary (see syncDirectory's reparse-point
    // guard), so each type declares exactly what it may carry.
    //
    // Public so tests/unit/test_pack_types.cpp can census it against the shipped
    // data directory -- a fifth pack type that ships without a row here fails
    // there rather than going quietly unsyncable.
    struct PackType {
        const char* subdir;
        // Names the type in the sync log line AND is the canonical stem of every
        // pack's ini (PackIni::k*, core/pack_ini_path.h) -- the same object, so
        // what the log calls a type and what it is called on disk cannot drift.
        const char* label;
        const char* media;   // FindFirstFile pattern for the payload files
    };
    static constexpr PackType PACK_TYPES[] = {
        { THEMES_SUBDIR,    PackIni::kTheme,    "*.tga" },
        { GAMEPADS_SUBDIR,  PackIni::kGamepad,  "*.tga" },
        { PITBOARDS_SUBDIR, PackIni::kPitboard, "*.tga" },
        { SPOTTERS_SUBDIR,  PackIni::kSpotter,  "*.wav" },
        { GAUGES_SUBDIR,    PackIni::kGauges,   "*.tga" },
    };

private:
    AssetManager() = default;
    ~AssetManager() = default;
    AssetManager(const AssetManager&) = delete;
    AssetManager& operator=(const AssetManager&) = delete;

    // User asset sync - copies user overrides from savePath to plugin data directory
    void syncUserAssets(const char* savePath);
    void syncDirectory(const std::string& sourceDir, const std::string& destDir, const char* extension);
    // Every pack type shares one nested shape -- <root>/<name>/<payload> plus
    // its <type>.ini -- which the flat syncDirectory cannot carry. `label` names the
    // asset type in the log line only; `mediaPattern` is that type's non-ini
    // payload (art for themes/pads/boards, audio for spotter voices).
    void syncPackDirectories(const std::string& sourceRoot, const std::string& destRoot,
                             const char* label, const char* mediaPattern);
    // Copy every pack type's user folder into the plugin data directory. Both the
    // startup sync and the RELOAD_CONFIG re-copy call THIS, so a pack type cannot
    // be wired into one and not the other.
    void syncAllPackTypes(const std::string& userBaseDir);

    // Discovery helpers
    // See the definition: the globally selected theme, memoised on themeGeneration().
    const ThemeAsset* activeIconTheme() const;
    mutable const ThemeAsset* m_iconThemeCache = nullptr;
    mutable unsigned int m_iconThemeCacheGen = 0xFFFFFFFFu;

    void discoverFonts();
    void discoverTextures();
    void discoverIcons();
    void discoverThemes();
    void discoverGamepads();
    void discoverPitboards();
    void discoverGauges();

    // ONE-TIME UPGRADE PATH for gauge art that predates gauges/<name>/ packs.
    //
    // Making the tacho and speedo pack HUDs means they stop consulting the flat
    // textures directory entirely (BaseHud::setTextureVariant returns early for a
    // pack HUD), so a user who had drawn their own tacho_widget_1.tga would have
    // watched it be replaced by the shipped face on upgrade, with no warning and
    // no way back.
    //
    // So the plugin migrates rather than the user: their file becomes a real pack
    // folder in their own Documents tree, which they can then rename, reskin or
    // delete. Reads from the USER's own textures directory only, never the
    // plugin's: an upgrade leaves the previously-shipped tacho_widget_1.tga
    // behind in the plugins tree (File /r copies, it never deletes), so that copy
    // cannot say whose art it is, while a file in Documents was put there by a
    // person.
    //
    // Runs before the pack sync, so the folder it writes is picked up by the
    // ordinary path with nothing special downstream.
    void migrateLegacyGaugeArt(const std::string& userBaseDir);

    // The half of that upgrade the migration cannot reach: gauge art in the
    // PLUGIN's own textures directory, which nothing reads any more. See the
    // definition for why one log line has to address two different readers.
    void warnAboutStrandedGaugeTextures() const;

    // Reset every pack of one type to its built-in geometry and re-read its ini.
    // Backs reloadThemeLayouts(); see there for why it is one generic helper rather
    // than a loop per type (a hand-written loop per type can simply be missing, so
    // the hotkey reloads pads and silently ignores boards).
    //
    // Defined in the .cpp, where the per-type ini readers live -- both instantiations
    // are in that same TU. `subdir` is the pack root under DISCOVERY_DIR.
    // `stem` is the type's PackIni::k* ini stem, passed rather than derived from
    // `subdir` because the two are not the same word ("gamepads" / "gamepad") and
    // pluralising by hand is how a reload ends up reading a file discovery does not.
    // `seed` runs after the reset and BEFORE the ini, for a pack type whose defaults
    // are not the struct's -- see seedPitboardArt(). nullptr when there are none.
    template <typename Pack>
    void reloadPackLayouts(std::vector<Pack>& packs, const char* subdir, const char* stem,
                           void (*readIni)(const std::string&, Pack&),
                           void (*seed)(const std::string&, Pack&) = nullptr);

    // A pit board's art proportions default to the ARTWORK's own, read from the .tga,
    // so a pack stating no [art] block still draws undistorted (the classic board's pitboard.ini documents
    // exactly that). Shared by discoverPitboards() and the reload, because a default
    // applied at discovery and not at reload is a default RELOAD_CONFIG deletes: the
    // reset restores the compiled 1920x1080 and only the ini is replayed, so a
    // 1024x1024 board comes back 16:9-stretched until a restart.
    // `dir` must end with a separator.
    static void seedPitboardArt(const std::string& dir, PitboardAsset& board);
public:
    // Re-read the defaults file and every theme's own ini, WITHOUT
    // re-discovering sprites. Backs the RELOAD_CONFIG hotkey.
    //
    // Numbers only, and that boundary is load-bearing: sprite indices are pushed to
    // the game once at init and held by number everywhere after, so renumbering them
    // live would repoint every quad in the UI at the wrong texture. A theme that
    // gained, lost or renamed a .tga needs a restart; its insets and spacing do not.
    void reloadThemeLayouts();

    // Where any pack's ini lives: <dir>\<stem>.ini, falling back to the pre-rename
    // <dir>\<packName>.ini and warning when a stale copy of the latter is being
    // shadowed. See core/pack_ini_path.h for the rule and why the fallback stays.
    // One fixed stem per type, not a per-pack name: copying a pack to start a new
    // one must not produce a folder the plugin silently ignores (editors show the
    // parent folder, so the name is not needed to tell tabs apart).
    // `dir` must end with a separator; `stem` is a PackIni::k* constant.
    //
    // NOT static: the shadow warning is only for a pack the USER owns (see
    // PackIni::resolve), and answering that needs m_userBaseDir.
    std::string packIniPath(const std::string& dir, const std::string& packName,
                            const char* stem) const;

    // The user's own asset tree (<savePath>\mxbmrp3), or empty before
    // discoverAssets has run. It is where a person's files are, which is the one
    // thing that separates a pack somebody authored from one an installer wrote
    // -- used by the shadow warning here and by SpotterManager's.
    const std::string& userAssetDir() const { return m_userBaseDir; }
private:

    // Parse texture filename to extract base name and variant number
    // e.g., "standings_hud_1.tga" -> ("standings_hud", 1)
    bool parseTextureFilename(const std::string& filename, std::string& baseName, int& variant) const;

    // Generate display name from filename
    // e.g., "RobotoMono-Regular" -> "Roboto Mono"
    std::string generateDisplayName(const std::string& filename) const;

    // Read TGA header to get pixel dimensions (returns false if file can't be read)
    static bool readTgaDimensions(const std::string& path, int& width, int& height);

    // Parse a theme's optional ini (see packIniPath) (inset, tintable) into `theme`. Never
    // throws: the file is user-editable, and an exception here would abort all
    // asset discovery.
    static void readThemeIni(const std::string& iniPath, ThemeAsset& theme);

    // Asset storage
    std::vector<FontAsset> m_fonts;
    std::vector<TextureAsset> m_textures;
    std::vector<IconAsset> m_icons;
    std::vector<ThemeAsset> m_themes;
    std::vector<GamepadAsset> m_gamepads;
    std::vector<PitboardAsset> m_pitboards;
    std::vector<GaugesAsset> m_gauges;

    // savePath\mxbmrp3, remembered from syncUserAssets so the RELOAD_CONFIG hotkey
    // can re-copy every pack type from the folder the user actually edits. Empty
    // when there is no savePath, which is the "nothing to re-copy" reading.
    std::string m_userBaseDir;

    // Quick lookup maps
    std::map<std::string, size_t> m_fontNameToIndex;
    std::map<std::string, size_t> m_textureNameToIndex;
    std::map<std::string, size_t> m_iconNameToIndex;

    // Sprite index tracking
    size_t m_totalTextureSprites = 0;
    int m_firstIconSpriteIndex = 0;

    bool m_initialized = false;
};
