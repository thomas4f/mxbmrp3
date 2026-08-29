// ============================================================================
// hud/base_hud.h
// Base class for all HUD display elements with common rendering and positioning logic
// ============================================================================
// file-budget: 2500 one class definition; the split bar (unchanged class definitions) forbids carving it
#pragma once
#include <vector>
#include "../core/small_vec.h"
#include "../core/layout_config.h"
#include <string>
#include <cmath>
#include <chrono>
#include <deque>
#include <optional>
#include <array>
#include <atomic>
#include "../game/game_config.h"
#include "../core/input_manager.h"
#include "../core/plugin_data.h"
#include "../core/color_config.h"
#include "../core/font_config.h"
#include "../core/asset_manager.h"   // ThemeAsset (panel themes)
#include "../core/plugin_utils.h"    // isColorDark (chipGlyphColor)
#include "nine_slice.h"

// Configuration for individual HUD strings with per-string padding and backgrounds
struct HudStringConfig {
    std::string text;
    float x = 0.0f;
    float y = 0.0f;

    // Text formatting
    int justify = PluginConstants::Justify::LEFT;
    int fontIndex = PluginConstants::Fonts::getNormal();
    unsigned long color = PluginUtils::makeColor(255, 255, 255);  // White default
    float fontSize = layoutDefaults().fontSizeNormal;

    // Layout padding (affects spacing and HUD bounds calculation)
    // This is "logical" padding that affects positioning
    float paddingLeft = 0.0f;
    float paddingRight = 0.0f;
    float paddingTop = 0.0f;
    float paddingBottom = 0.0f;

    // Optional background
    bool hasBackground = false;
    unsigned long backgroundColor = 0x000000;  // Black
    float backgroundOpacity = 0.85f;

    // Background padding (size of background quad around text)
    // Only used if hasBackground = true
    // Can be different from layout padding for visual effects
    float bgPaddingLeft = 0.0f;
    float bgPaddingRight = 0.0f;
    float bgPaddingTop = 0.0f;
    float bgPaddingBottom = 0.0f;

    // Cached text width (set to > 0 to skip recalculation in render)
    // PERFORMANCE: Caching this eliminates redundant calculateMonospaceTextWidth calls
    float cachedTextWidth = 0.0f;
};

// Which of the three panel FAMILIES a HUD belongs to, so a theme can style them
// separately ([card] widget-content, settings-title-band, ...).
//
// A family, not a per-HUD identity: the three differ in what they are FOR -- a table
// or graph, a gauge, and the menu -- which is the grain a skinner actually wants.
// Per-component control is the obvious next step and deliberately not this one; the
// forty-key layout surface that got deleted is what per-component looks like when it
// is offered before anyone has asked for it.
enum class PanelKind { Hud, Widget, Settings };

class BaseHud {
public:
    // Standard update interval for live timing displays (~167Hz for smooth ticking)
    static constexpr int TICK_UPDATE_INTERVAL_MS = 6;

    // Order here MUST match the member declaration order below — C++ initializes
    // in declaration order whatever this list says, so a mismatch is misleading
    // rather than wrong, and it produced all 18 of the build's warnings. Enforced
    // by -Werror on the tests/unit targets, which compile this header.
    BaseHud() :
        m_fScale(1.0f), m_bVisible(true), m_bShowTitle(true), m_fBackgroundOpacity(0.85f),
        m_bShowBackgroundTexture(false), m_iBackgroundTextureIndex(0),
        m_fOffsetX(0.0f), m_fOffsetY(0.0f),
        m_fBoundsLeft(0.0f), m_fBoundsTop(0.0f), m_fBoundsRight(0.0f), m_fBoundsBottom(0.0f),
        m_lastTickUpdate(), m_benchmarkIndex(-1),
        m_bDataDirty(true), m_bLayoutDirty(true), m_bDraggable(false), m_bDragging(false),
        m_fDragStartX(0.0f), m_fDragStartY(0.0f),
        m_fInitialOffsetX(0.0f), m_fInitialOffsetY(0.0f) {}

    virtual ~BaseHud() = default;

    virtual void update() = 0;
    virtual bool handlesDataType(DataChangeType dataType) const = 0;

    const std::vector<SPluginQuad_t>& getQuads() const { return m_quads; }
    const std::vector<SPluginString_t>& getStrings() const { return m_strings; }
    const std::vector<bool>& getStringSkipShadow() const { return m_stringSkipShadow; }

    // Visibility controls
    virtual void setVisible(bool visible) {
        if (m_bVisible != visible) {
            m_bVisible = visible;
            if (visible) setDataDirty();  // Rebuild when becoming visible
            onVisibilityChanged();
        }
    }

    // Called immediately after ANY change that may flip isVisibleAnySurface(), from
    // BOTH surface setters. A HUD whose show/hide edge has a SIDE EFFECT overrides
    // this; doing that work only on the next frame instead is a real bug, twice
    // measured: TelemetryHud clears its history on becoming visible and callers feed
    // samples between the toggle and the next draw (stripchart_parity_test does
    // exactly that, and a deferred clear wiped the samples it had just fed), while
    // BenchmarkWidget exports its report on hide and bench_driver tears down with no
    // draw at all.
    //
    // Overriders MUST be idempotent: the per-frame sync in update() calls the same
    // code to catch the one transition no setter can see -- the companion window
    // itself opening or closing. Edge-guard on your own remembered state, not on the
    // call.
    virtual void onVisibilityChanged() {}

    // Whether this HUD belongs on the companion window AT ALL, independent of its
    // on/off there. Default true: the companion is a second surface for the HUD set,
    // and per-HUD on/off is what decouple() is for.
    //
    // False is for a HUD that is an IN-GAME EFFECT rather than a panel -- one whose
    // meaning is tied to the player's view of the game, not to a place to put
    // information. Such a HUD has no position, scale or title to decouple, so the
    // per-surface model does not describe it, and drawing it on a second monitor
    // covers the very HUDs the companion exists to show. HelmetOverlayHud is the
    // case: a full-screen immersion overlay driven by lean angle and suspension
    // travel. Excluding it here also makes the settings UI honest -- the tab bar
    // already treats the helmet as a shared global toggle rather than a per-surface
    // one, which was only half-true while the renderer still drew it per-surface.
    virtual bool rendersOnCompanion() const { return true; }
    bool isVisible() const { return m_bVisible; }

    // True if this HUD is shown on ANY active surface — the game, or the companion
    // window when it's open. HUD update()s early-out on "not visible" to skip the
    // expensive rebuild; they must use THIS (not isVisible()) so a HUD enabled only
    // on the companion still rebuilds and doesn't render stale. Equals isVisible()
    // when the companion is disabled, so single-window behavior is unchanged.
    bool isVisibleAnySurface() const;

    // Temporary "show even if the mouse is idle" reveal, used by the corner status
    // buttons (settings / director) so they can flash into view on an event (entering
    // the track, toggling a mode) without waiting for cursor movement. A no-op for HUDs
    // that don't consult isRevealed() in their render gate. Time-based (not frame-based)
    // so the duration is FPS-independent.
    void reveal(int ms) {
        m_revealUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    }
    bool isRevealed() const {
        return std::chrono::steady_clock::now() < m_revealUntil;
    }

    void setShowTitle(bool showTitle) {
        if (!m_titleSupported) showTitle = false;
        if (m_bShowTitle != showTitle) {
            m_bShowTitle = showTitle;
            setDataDirty();
        }
    }
    bool getShowTitle() const { return m_bShowTitle; }

    // HUD identity icon: the name of an icon asset (from mxbmrp3_data/icons/, without
    // the .tga extension) used to represent this HUD. Drawn to the left of the title
    // (when the global "HUD Icons" setting is on) and as the settings-panel tab toggle.
    // Return "" (the default) to opt out — callers fall back to text rendering.
    virtual const char* getIconName() const { return ""; }

    void setBackgroundOpacity(float opacity) {
        // Clamp opacity to valid range [min, 1.0] - min is 0 for most HUDs, but some
        // (e.g. the settings button) floor it so the slider can't reach a misleading 0%
        if (opacity < m_fMinBackgroundOpacity) opacity = m_fMinBackgroundOpacity;
        if (opacity > 1.0f) opacity = 1.0f;

        // Round to nearest 1% increment to avoid floating point precision issues
        opacity = std::round(opacity * 100.0f) / 100.0f;

        if (m_fBackgroundOpacity != opacity) {
            m_fBackgroundOpacity = opacity;
            setDataDirty();
        }
    }
    float getBackgroundOpacity() const { return m_fBackgroundOpacity; }

    // Nothing this panel's background draws would be visible.
    //
    // MEASURED, not assumed: the render probe put a fully transparent quad at 0.969us
    // against an opaque one's 0.973 -- 100%. The engine charges for submission, not
    // for pixels, so an alpha-0 quad costs exactly as much as one you can see. A
    // themed panel at zero opacity was therefore paying for ~27 invisible quads every
    // frame, which at the measured switching rate is ~45us per panel.
    //
    // Gates the FRAME background only, not the title band or the content cards --
    // they are tinted by the same opacity and look like equally valid candidates, but
    // their emission is also where the card covers and spans are recorded, and that
    // geometry is read by content placement, the fill cut and the click rects. See
    // addBackgroundQuad.
    bool backgroundIsInvisible() const { return m_fBackgroundOpacity <= 0.0f; }

    // Background texture support
    // THE ARTWORK IS THE WIDGET, so it cannot be switched off.
    //
    // Set by the HUDs whose texture is not decoration but the thing itself -- the
    // gamepad's controller, the pit board, the radar's dial. Turning it off did not
    // give a cleaner panel, it gave the CONTENTS floating on an empty one: a
    // gamepad's buttons, sticks and triggers arranged in mid-air with no controller
    // under them. Rendered and looked at before removing it, which is the only way
    // that call could be made honestly.
    //
    // "Off" was also doing a second job by accident. A background texture SUPPRESSES
    // the theme (see resolveActiveTheme), so Off was the only route to a themed
    // panel on these three -- which is why it looked defensible in the code and
    // indefensible on screen. If a themed gamepad is ever wanted, it needs the pad
    // drawn ON the theme, not the pad deleted.
    //
    // Enforced in the SETTERS rather than only in the settings UI, so a stale
    // `showBackgroundTexture=0` or `textureVariant=0` already in someone's INI
    // self-heals on load. No migration, and no way back into the dead state.
    //
    // POINTERWIDGET SHIPS ARTWORK AND IS DELIBERATELY NOT ONE OF THESE. It looks like
    // the sixth member of the set and is not: it BRANCHES on the flag
    // (`m_bShowBackgroundTexture ? createPointerSprite() : createPointerQuads()`), so
    // Off draws the cursor as accent-coloured shapes instead of the .tga. Both spell a
    // cursor, so that Off is a style choice and has to stay. The test for membership is
    // therefore not "does it ship a texture" but "is there anything left when the
    // texture goes" -- and the answer is only visible by rendering it. The five here
    // were each looked at: a gamepad's buttons in mid-air, a board's rows on a blank
    // panel, and a gauge reduced to a red needle with no face, ticks or numbers.
    //
    // (Not to be confused with `[Display] menuOnlyCursor`, which decides WHEN the
    // cursor shows at all. Different setting, different question.)
    bool m_textureRequired = false;

    // WHICH ASSET-PACK CYCLE this HUD's Texture row drives, if any.
    //
    // A pack HUD has no texture BASE NAME (its art comes from the selected pack, not
    // from the textures/ folder), so getAvailableTextureVariants() is empty and the
    // settings row fell through to the per-HUD THEME control instead. That control is
    // dead on exactly these HUDs: m_textureRequired forces the artwork on, and artwork
    // makes resolveActiveTheme() return nullptr -- so the theme it offers can never
    // take effect. Radar, which does have a base name, shows a Texture cycle and is
    // the shape the other two should match.
    enum class PackKind { None, Gamepad, Pitboard, Gauges };
    PackKind m_packKind = PackKind::None;

    // CAN THIS PANEL CARRY A CAPTION AT ALL. False means there is no Title row in the
    // settings -- not a greyed one -- and setShowTitle() refuses, so a stale
    // `showTitle=1` in someone's INI self-heals on load rather than rendering a caption
    // the UI says is off. Same shape as m_textureRequired above, for the same reason:
    // enforced in the SETTER, so there is no way back into the mixed state.
    //
    // WHO IS FALSE, and it is one question with one answer per HUD rather than a bool
    // passed at each settings call site. It used to be exactly that -- an `enableTitle`
    // argument on addStandardHudControls AND another on addWidgetRow -- so the panel
    // and its settings row could disagree, and greying the row left the HUD's own
    // m_bShowTitle untouched underneath.
    //
    // The three CENTRE-STACK panels (Timing, Gap Bar, Notices) are false because they
    // are one-line readouts stacked against the top edge: a caption over each turns
    // three thin bars into three two-row panels and pushes the stack down the screen.
    // The rest are panels whose content IS the panel -- a pit board, a radar sweep, a
    // gauge face, the pad artwork, the cursor, the settings button.
    //
    // THE RENDER PATH IS UNAFFECTED and nothing was deleted from it: the caption path
    // handles the hidden case itself (it emits the body card and an empty string, which
    // is what keeps every layout fast path's string indices stable), so each HUD still
    // calls it and simply always takes the untitled arm.
    //
    // SET IT THROUGH disableTitle(), never by hand: the flag and m_bShowTitle are two
    // halves of one statement, and BaseHud's constructor starts m_bShowTitle TRUE. Most
    // HUDs put it back to false in a resetToDefaults() their constructor calls -- but
    // PointerWidget's and SettingsHud's constructors do not call theirs, so those two
    // read back getShowTitle() == true until something else resets them, and the
    // Widgets table prints that value. One call, both effects, nothing to forget.
    bool m_titleSupported = true;

    // "This panel has no caption." Both halves, so they cannot drift apart.
    void disableTitle() { m_titleSupported = false; m_bShowTitle = false; }

    void setShowBackgroundTexture(bool show) {
        // ...but only while there is artwork to show. THE SAME GUARD setTextureVariant
        // puts on its variant-0 snap, and the two have to ask one question: that one
        // leaves the flag FALSE when the lookup fails, so a mandatory-artwork HUD whose
        // .tga is missing ended up with a factory default of 0 and a forced live value
        // of 1. Nothing renders differently either way -- there is no texture -- but the
        // sparse save then wrote showBackgroundTexture=1 on every save AFTER a load and
        // not on the first one, so the settings file stopped round-tripping. Found by
        // settings_idempotency_test, which is exactly the shape it exists to catch, on
        // a harness that stages no textures/ at all.
        //
        // hasBackgroundArtwork(), NOT getAvailableTextureVariants(): the first version of
        // this guard asked the variant list, which is EMPTY for the two HUDs the rule is
        // actually about. The gamepad and the pit board carry no texture base name at all
        // -- their art comes from PACKS -- so the guard switched the enforcement off for
        // exactly them, and asset_pack_test failed 36 ways on the invariant it exists to
        // hold. Two art sources, one question.
        //
        // So this and setTextureVariant's snap no longer call the SAME function, and that
        // is the point rather than drift: the snap picks a variant NUMBER, which only a
        // texture HUD has, so the variant list is the whole answer there. Both still ask
        // "is there art to show" -- this one has to ask it of both sources because a pack
        // HUD can reach it.
        if (m_textureRequired && hasBackgroundArtwork()) show = true;
        invalidateThemeCache();   // a background texture SUPERSEDES the theme
        if (m_bShowBackgroundTexture != show) {
            m_bShowBackgroundTexture = show;
            setDataDirty();
        }
    }
    bool getShowBackgroundTexture() const { return m_bShowBackgroundTexture; }

    // Legacy texture index support (for compatibility)
    void setBackgroundTextureIndex(int index) { m_iBackgroundTextureIndex = index; invalidateThemeCache(); }
    int getBackgroundTextureIndex() const { return m_iBackgroundTextureIndex; }

    // Per-HUD panel-theme override:
    //   ""             follow the global Appearance > Panel Theme (the default)
    //   THEME_NONE     force NO theme on this HUD -- a flat background even when a
    //                  global theme is set
    //   <theme name>   use that theme regardless of the global one
    // An unknown name falls back to the global theme rather than to none, so a
    // deleted theme folder does not silently strip one HUD out of the set.
    //
    // "none" is a RESERVED value: a theme directory of that name is unreachable
    // through this setting. Cheap price for keeping the whole thing one string key.
    static constexpr const char* THEME_NONE = "none";
    void setThemeOverride(const std::string& v) {
        if (m_themeOverride != v) { m_themeOverride = v; invalidateThemeCache(); setDataDirty(); }
    }
    const std::string& getThemeOverride() const { return m_themeOverride; }
    // Back to following the global Appearance theme. The apply side calls this when
    // the settings carry no theme key -- see settings_serde.h for why absence has to
    // be authoritative rather than ignored.
    void clearThemeOverride() { setThemeOverride(std::string()); }

    // THE ONE STABLE NAME FOR THIS REGISTERED ELEMENT (e.g. "standings_hud"),
    // set at registration and never after -- see HudManager::registerHud, which
    // REQUIRES it, so an element cannot enter the list unnamed.
    //
    // It replaced two hand-maintained naming ladders that had already drifted
    // apart: the benchmark report picked the texture base name, else one of
    // eight `hud.get() == m_pX` special cases, else the literal "unknown" (three
    // rows shared that label until the special cases were added); the test
    // harness picked the ICON name, else the texture base name, else "#<index>",
    // so the same panel answered to `hud-timing` in one test and `crash_widget`
    // in another, and an element with neither was unaddressable. Both now read
    // this. A name here is load-bearing off the game thread too:
    // tools/benchmark_report.py keys its HUD-footprint table on it, so these
    // strings are the report's vocabulary -- rename one and the historical rows
    // stop lining up.
    //
    // Never null once registered; the accessor is used in string contexts.
    void setHarnessId(const char* id) { if (id) m_szHarnessId = id; }
    const char* getHarnessId() const { return m_szHarnessId; }

    // Dynamic texture variant support
    // THE STEM OF THIS HUD'S TEXTURE FILES (e.g. "standings_hud" for
    // mxbmrp3_data/textures/standings_hud_1.tga). Declared ONCE, by the HUD, in its
    // constructor and BEFORE resetToDefaults(), which is what consumes it:
    // setTextureVariant() resolves a sprite through it, and a HUD whose artwork is
    // mandatory (m_textureRequired) has no dial without it. HudManager deliberately
    // does not restate it at registration -- see createHud.
    //
    // No getter: it is nobody's name for this HUD. Three test hooks used to look
    // panels up by it, which made it a fourth naming scheme competing with the icon
    // name, the registration name and the benchmark report's; they all read
    // getHarnessId() now. This is an ASSET PATH, and the only code that should care
    // is the code that loads the asset.
    void setTextureBaseName(const std::string& baseName);

    // Texture variant: 0 = Off (solid color), 1+ = variant number
    void setTextureVariant(int variant);
    int getTextureVariant() const { return m_textureVariant; }

    // Cycle through available variants: Off -> 1 -> 2 -> ... -> Off
    void cycleTextureVariant(bool forward = true);

    // THE ONE SPELLING of a panel's padding, per axis: the base padding this HUD's
    // scale earns, widened to whatever the theme makes its content clear. These are
    // what getScaledDimensions() puts in ScaledDimensions::paddingH / paddingV, and
    // check_hud_helpers.sh rule 11 exists to ban synonyms of the vertical one.
    //
    // TOTALS, not the shortfall on top of the base. There used to be a second pair --
    // themedContentPadX/Y -- returning only the amount by which the theme exceeded the
    // base, so a caller wanting the padding had to add the two, and the word "padding"
    // named both the part and the whole. Y had this wrapper and X did not, so the X
    // total was spelled inline in getScaledDimensions, outside the reach of the very
    // lint that exists to keep it to one spelling.
    float contentPaddingX() const;
    float contentPaddingY() const;

    // The BASE padding on its own, before the theme's borders widen it -- the built-in,
    // or `[panel] padding-x/-y` when the active theme names one. contentPadding{X,Y}()
    // above is what a panel is normally built from; this is for the two places that
    // legitimately need the base alone (the settings panel's own vertical structure, and
    // a caption drawn by a HUD with no panel background).
    float basePaddingX() const;
    float basePaddingY() const;

    // THE VISIBLE GAP between two carded boxes, at this HUD's scale: the built-in, or the
    // active theme's `[content] gap`. The ONE reader of ThemeAsset::sectionGap.
    //
    // Spent at all three boundaries where two carded boxes meet -- the title band to the
    // body card (contentCardTop), one HUD section to the next (sectionGapY), one settings
    // card to the next (SettingsLayoutContext::addSectionHeading) -- because until this
    // existed each of the three answered differently and only the settings panel had a
    // visible term at all. See LayoutMetrics::sectionGap.
    float contentGapY() const;
    float contentGapCells() const;   // the same seam, unscaled cells (height budgets)
    float panelGapCells() const;     // the junction gap term alone, resolved (theme → built-in)

    // Get available variants for this HUD's texture (empty if no texture set)
    std::vector<int> getAvailableTextureVariants() const;

    // IS THERE ANY ARTWORK FOR THIS PANEL TO SHOW -- across BOTH sources a HUD can
    // draw one from. A pack HUD (gamepad, pit board) has no texture base name and
    // reads its art from mxbmrp3_data/gamepads|pitboards; every other HUD reads a
    // numbered variant out of textures/. The mandatory-artwork enforcement is the
    // caller that needs them answered together, and asking only the second is what
    // silently exempted the pack HUDs from it.
    bool hasBackgroundArtwork() const;

    // Drag and drop functionality
    void setDraggable(bool draggable) { m_bDraggable = draggable; }
    bool isDraggable() const { return m_bDraggable; }
    bool isDragging() const { return m_bDragging; }

    void setPosition(float offsetX, float offsetY) {
        if (m_fOffsetX != offsetX || m_fOffsetY != offsetY) {
            m_fOffsetX = offsetX;
            m_fOffsetY = offsetY;
            setLayoutDirty();
        }
    }
    float getOffsetX() const { return m_fOffsetX; }
    float getOffsetY() const { return m_fOffsetY; }

    // --- Companion-surface instance (decoupled on/off + position) ---------------
    // The companion window is a second HUD surface. Each HUD keeps an independent
    // visibility + position for it. While m_bCompanionConfigured is false the companion
    // values MIRROR the game (the accessors fall back to the game value) — the state for
    // a user who never opens the companion window, which keeps their save sparse. But
    // OPENING the window snapshots the current game layout into EVERY HUD's companion
    // instance: HudManager calls snapshotCompanionFromGame() on the first companion frame
    // (the "decoupled from the start" behavior, so the companion stops tracking the game
    // once opened). From then on each HUD's companion values are configured and persist —
    // so a user who has ever opened the companion saves the four companion* keys for all
    // HUDs (intended; the sparse-save property is for single-window users only). A later
    // setCompanion*() edit also snapshots-on-first-write as a fallback. Only the on/off +
    // position decouple; everything else (colors/size/columns) is shared.
    bool isCompanionConfigured() const { return m_bCompanionConfigured; }
    bool getCompanionVisible() const { return m_bCompanionConfigured ? m_bCompanionVisible.load() : m_bVisible.load(); }
    float getCompanionOffsetX() const { return m_bCompanionConfigured ? m_fCompanionOffsetX : m_fOffsetX; }
    float getCompanionOffsetY() const { return m_bCompanionConfigured ? m_fCompanionOffsetY : m_fOffsetY; }
    void setCompanionVisible(bool visible) {
        ensureCompanionConfigured();
        m_bCompanionVisible.store(visible);
        onVisibilityChanged();   // same edge, other surface — see the hook
    }
    void setCompanionPosition(float x, float y) { ensureCompanionConfigured(); m_fCompanionOffsetX = x; m_fCompanionOffsetY = y; }
    // Settings load applies the persisted companion instance verbatim (configured).
    void applyCompanionState(bool visible, float x, float y) {
        m_bCompanionConfigured = true;
        m_bCompanionVisible.store(visible);
        m_fCompanionOffsetX = x; m_fCompanionOffsetY = y;
    }
    void clearCompanionState() { m_bCompanionConfigured = false; }

    // Snapshot the game state into the companion instance now, so the companion is
    // independent from the moment it's enabled instead of mirroring until the first
    // edit (the "decoupled from the start" behavior). No-op once configured.
    void snapshotCompanionFromGame() { ensureCompanionConfigured(); }

    // Visibility on the surface the user is currently on: a widget hidden in-game
    // but enabled on the companion must still accept clicks there (and vice versa).
    // Pair this with isPointInActiveBounds() in click gates — testing m_bVisible
    // alone makes a companion-only widget render but ignore clicks.
    bool isVisibleOnActiveSurface() const {
        bool companion =
            InputManager::getInstance().getActiveSurface() == InputManager::Surface::Companion;
        return companion ? getCompanionVisible() : isVisible();
    }

    // Hit-test at the surface the user is currently on: a companion HUD sits at its
    // companion offset, so an interactive widget (settings/director button, drag)
    // must test there, not at the game offset. Mirrors to the game offset until the
    // companion instance diverges.
    // The panel rect the last rebuild produced. Public because it is the only way to
    // ask a HUD "how big did you come out", which is what a layout test asserts and
    // what the drag hit-test below already uses internally.
    void panelRect(float& l, float& t, float& r, float& b) const {
        l = m_fBoundsLeft; t = m_fBoundsTop; r = m_fBoundsRight; b = m_fBoundsBottom;
    }

    bool isPointInActiveBounds(float x, float y) const {
        bool companion =
            InputManager::getInstance().getActiveSurface() == InputManager::Surface::Companion;
        return isPointInBoundsAt(x, y, companion ? getCompanionOffsetX() : getOffsetX(),
                                       companion ? getCompanionOffsetY() : getOffsetY());
    }

    // A HUD builds its quads AND its interactive click/hover regions at the GAME
    // offset; on the companion the RENDER is translated by (companion - game) but the
    // regions are not. So an interactive HUD (settings menu, records/standings/map
    // click targets) must shift a companion cursor BACK by that delta before testing
    // it against its own regions — otherwise the hit-boxes sit where the HUD is drawn
    // in-game, not where it's drawn on the companion. No-op on the game surface or a
    // HUD still mirroring the game.
    void mapCursorToHudSpace(float& x, float& y) const {
        if (InputManager::getInstance().getActiveSurface() == InputManager::Surface::Companion) {
            x -= (getCompanionOffsetX() - getOffsetX());
            y -= (getCompanionOffsetY() - getOffsetY());
        }
    }

    virtual void setScale(float scale) {
        if (scale <= 0.0f) scale = 0.1f;
        if (m_fScale != scale) {
            m_fScale = scale;
            setDataDirty();
        }
    }
    float getScale() const { return m_fScale; }

    void validatePosition();

    // Benchmark profiling support
    void setBenchmarkIndex(int index) { m_benchmarkIndex = index; }
    int getBenchmarkIndex() const { return m_benchmarkIndex; }

    void setDataDirty() {
        m_bDataDirty = true;
        m_bLayoutDirty = true;
    }

    // The PANEL moved -- a drag, a scale, a changed default position.
    void setLayoutDirty() {
        m_bLayoutDirty = true;
        // The layout fast path repositions the background span, the title icon and
        // the strings -- everything it knew about before themes existed. It does NOT
        // know about the themed INNER cards (the title band, and the settings
        // panel's section cards), whose geometry is derived from content laid out
        // during a full rebuild. Left on the fast path they stay where they were
        // while the rest of the HUD moves, which is exactly what dragging a themed
        // HUD looked like.
        //
        // So a HUD carrying cards escalates a layout change to a FULL rebuild.
        // That is the honest fix rather than teaching the fast path to move a
        // variable number of cards it never sized: a rebuild is what a data change
        // already does many times a second, and dragging is a brief interaction.
        // BAND TOO, not just the card. The comment above names the title band as one
        // of the things the fast path cannot move, but the test only asked about the
        // card -- and titleBand defaults ON while contentCard defaults OFF, which is
        // precisely the combination a theme asks for with `widget-content = 0`. Under
        // it a drag took the fast path, so the band's nine quads stayed behind while the
        // panel moved, AND finalizeThemedFill cut a hole in the fill where the band was
        // supposed to be.
        if (hasThemedCard() || hasThemedTitleBand()) m_bDataDirty = true;
    }

    // Content moved INSIDE a stationary panel. The frame, the title band and the
    // body card are all exactly where they were, so the fast path is sufficient and
    // the escalation above must NOT apply.
    //
    // The distinction is a hot path, not a nicety. StandingsHud calls this every
    // frame for the length of a position-slide animation (500ms by default, and
    // frequent on a full grid). Routed through setLayoutDirty(), a theme with an
    // card slice set turned that into a full 22-row rebuild per frame on the most
    // expensive HUD in the plugin, against a 2.08ms budget -- and run_perf.sh could
    // not see it, because it runs unthemed.
    void setContentLayoutDirty() { m_bLayoutDirty = true; }

    // Process dirty flags immediately (without full update logic)
    // Only rebuilds if already marked dirty - use after batch settings changes
    void rebuildIfDirty() {
        processDirtyFlags();
    }

    // ========================================================================
    // Frequent Update Support (for live timing displays)
    // ========================================================================
    // Override needsFrequentUpdates() to return true when HUD should tick at high frequency.
    // Override getTickIntervalMs() to change the tick rate (default: 6ms / ~167Hz).
    // Call checkFrequentUpdates() in update() to apply the standard ticking logic.
    virtual bool needsFrequentUpdates() const { return false; }
    virtual int getTickIntervalMs() const { return TICK_UPDATE_INTERVAL_MS; }

    // Check if enough time has passed since last tick update; if so, marks data dirty.
    // Returns true if an update was triggered, false otherwise.
    // Use this in update() instead of duplicating the tick check logic.
    bool checkFrequentUpdates();

    virtual bool handleMouseInput(bool allowInput = true);
    bool isPointInBounds(float x, float y) const;
    // Hit test against the bounds translated by an explicit offset (used for
    // dragging on the companion surface, where the HUD sits at its companion offset).
    bool isPointInBoundsAt(float x, float y, float offX, float offY) const;

protected:
    bool clampPositionToBounds(float& offsetX, float& offsetY, const WindowBounds& windowBounds) const;
    virtual void rebuildRenderData() = 0;
    virtual void rebuildLayout() { rebuildRenderData(); }

    bool isDataDirty() const { return m_bDataDirty; }
    bool isLayoutDirty() const { return m_bLayoutDirty; }

    void clearDataDirty() { m_bDataDirty = false; }
    void clearLayoutDirty() { m_bLayoutDirty = false; }

    void setBounds(float left, float top, float right, float bottom);

    void applyOffset(float& x, float& y) const {
        x += m_fOffsetX;
        y += m_fOffsetY;
    }

    // ========================================================================
    // Standard Dirty Flag Handling
    // ========================================================================
    // Call processDirtyFlags() in update() implementations to handle the common pattern:
    //   - If data dirty: rebuild all, call onAfterDataRebuild(), clear both flags
    //   - Else if layout dirty: rebuild layout only, clear layout flag
    //
    // Override onAfterDataRebuild() if widget needs to update caches after rebuildRenderData().
    // rebuildRenderData() + the two calls that always follow it, TIMED and recorded
    // against this HUD. processDirtyFlags() runs it on the dirty edge; a HUD that
    // rebuilds unconditionally every frame (PerformanceHud, SpeedWidget, GearWidget --
    // live values, always stale) calls it directly instead of rebuildRenderData().
    //
    // WHY IT IS SHARED rather than left to each caller: those three bypassed the
    // recorded path, so the benchmark reported them as ZERO rebuilds while they
    // rebuilt every frame, and their cost -- 16.2, 7.7 and 5.8 us/frame, 87% of all
    // non-rebuild time in Draw -- was filed under the HUD table's idle column with no
    // way to tell it from a dirty-flag check.
    void rebuildAndRecord();

    void processDirtyFlags();
    virtual void onAfterDataRebuild() {}

    // Shared helper methods for HUD rendering (eliminates duplication across HUDs)
    // skipShadow: set true to exclude this string from drop shadow effect
    void addString(const char* text, float x, float y, int justify, int fontIndex,
                   unsigned long color, float fontSize, bool skipShadow = false);

    // Clear strings and associated shadow flags (use instead of m_strings.clear()).
    // Also invalidates the title-icon/string indices so a rebuild path that clears
    // strings without re-emitting the title can't leave them pointing at a stale quad.
    void clearStrings() { m_strings.clear(); m_stringSkipShadow.clear(); m_titleStringIndex = -1; m_titleIconQuadIndex = -1; }
    // Drop the remembered panel rect, for a rebuild that draws NO panel background.
    //
    // addBackgroundQuad() is what arms m_bgRectValid and m_contentCardEmitted, so a
    // HUD that calls it conditionally would otherwise leave the PREVIOUS rebuild's
    // rect visible to emitContentCard(). NoticesHud is the one such
    // caller (its panel exists only when captioned).
    void invalidatePanelRect() {
        m_bgRectValid = false;
        m_bgQuadFirst = -1;
        m_bgQuadCount = 0;
        m_fillFirst = -1;
        m_fillCount = 0;
        resetPanelDerivedState();
    }

    // EVERYTHING DERIVED FROM THE PANEL RECT, in one place, because there are two
    // entry points and they have to agree: addBackgroundQuad() arms this state for a
    // new rebuild, and invalidatePanelRect() drops it for a rebuild that draws no
    // panel at all. They were written out separately and diverged -- the section
    // fields below were reset only by the first -- and the divergence is not
    // cosmetic. finalizeThemedFill() and finishContentSections() both WRITE quads by
    // a remembered index, so a stale one points into whatever occupies that index now
    // and rewrites it. That is what happened to m_fillFirst on NoticesHud (the one HUD
    // that calls addBackgroundQuad conditionally): it erased the slab and took the
    // whole panel with it. m_lastSectionIndex is the same trap, still unsprung only
    // because no sectioned HUD skips its background yet.
    void resetPanelDerivedState() {
        m_contentCardEmitted = false;   // arm the body card for this rebuild
        m_bandValid = false;
        m_wholeCardValid = false;
        m_sectionCards.clear();
        m_sectionCount = 0;
        m_sectionCardIndex = -1;
        m_lastSectionIndex = -1;
        m_bodyCardValid = false;
    }

    // How far a glyph box of `fontSize` must drop to sit CENTRED in its row rather
    // than flush with the row's top.
    //
    // WHY: a render string's y is the glyph box's TOP (the engine and hud_sw_renderer
    // both draw downward from it), while addIcon's y is its CENTRE -- and eleven call
    // sites place row icons at `rowY + lineHeightNormal * 0.5f`. So text sat at the top
    // of its row while everything beside it sat in the middle, and all the leading fell
    // below the glyph. Invisible at the shipped ~1.9px, obvious once uiLineHeight is
    // raised, which is how it was spotted.
    //
    // ONE RULE, NO TIER LOOKUP: four of the five tiers pair EQUAL fontSize and row
    // multiples (normal 1.0/1.0, XL 2.0/2.0, XS and S likewise), so their row is exactly
    // `fontSize * lineHeightRatio` and the leading is a pure function of the string's own
    // size. That makes it scale-correct for free -- callers pass an already-scaled size,
    // and a ratio is scale-invariant. Large is the one exception (1.5x text in a 2x row);
    // it is slightly under-centred, which beats detecting a tier from a scaled float.
    //
    // APPLIED IN EXACTLY TWO PLACES -- addString() and positionString(). Those are the
    // only two ways a string's y is ever set, and check_hud_helpers.sh rule 8 keeps it
    // that way: a HUD that writes m_afPos[1] itself skips this and its text jumps by the
    // offset the moment the panel is dragged. That is not hypothetical, it is what the
    // first attempt at this shipped (standings_layout_test caught it).
    float rowCenterOffset(float fontSize) const {
        return fontSize * (layout().lineHeightRatio - 1.0f) * 0.5f;
    }

    void addBackgroundQuad(float x, float y, float width, float height);
    // Active panel theme, or nullptr when themes are off/unknown/superseded by a
    // background texture. See the definition for why each of those wins.
    const ThemeAsset* activeTheme() const;
    // The uncached lookup activeTheme() memoises. Call activeTheme(), not this.
    const ThemeAsset* resolveActiveTheme() const;
    // Drop the memo. Called by the per-HUD setters that can change which theme
    // this HUD resolves to; the global generation counter covers everything else.
    // The PLAN memo goes with it: the plan is computed from the resolved theme's
    // box terms and band/card flags, but its key carries only the GLOBAL theme
    // generation — a per-HUD override changes the resolved theme without moving
    // that key, so a kept plan would be served stale (borders leak in via
    // dim.padding, but two themes with equal borders and different margins/
    // padding/gap or [card] flags collide).
    void invalidateThemeCache() {
        m_themeCacheGen = 0;
        m_planCacheValid = false;
    }
    // Emit the 9 themed slices; firstIndex < 0 appends, else overwrites in place.
    void emitThemedBackground(const ThemeAsset& theme, float x, float y,
                              float width, float height, int firstIndex);

public:
    // Horizontal distance the identity icon takes to the LEFT of the title glyph:
    // the icon's own width (square in PIXELS, hence the aspect divide) plus the gap
    // to the caption. The gap is [title] icon-gap in CELLS -- it used to be
    // 0.35 * iconSize, which made resizing the icon silently move the text too.
    //
    // Not static, unlike its previous life: a grid distance needs the metrics.
    float titleIconAdvance(float iconSize) const {
        return iconSize / PluginConstants::UI_ASPECT_RATIO
             + layout().titleIconGap * layout().cellW;
    }

    // Height a caption ROW must reserve. Normally just `rowHeight`, but a themed
    // band is fontSize + 2 * [title] padding-y cells, and at the small tiers that
    // comes out TALLER than the 1.17-em row the caption sits in. The overhang lands
    // on the first thing the HUD draws (the compass's "N" sat inside its own title
    // band). Growing the row to hold the band is the fix; the panel grows with it.
    // The row a themed title BAND needs, ROUNDED UP TO THE GRID.
    //
    // The ceil is the whole point, not tidiness. Every other vertical term a panel is
    // built from is a whole number of cells -- panelPaddingY is 2, contentPaddingY
    // is itself a ceilY, lineHeightNormal is 2, lineHeightLarge is 3 -- so the raw
    // band was the ONLY thing that could take a panel off the lattice, and it always
    // did: it is the caption's font size plus twice [title] padding-y, 34.27px
    // against a 12.672px cell, or 2.70 cells.
    //
    // What that cost is not visible in one panel, which is why it survived: a widget
    // is internally consistent either way, and so is every other widget, because they
    // are all off by the same 0.30 of a cell. It shows the moment a widget has to line
    // up with something built the OTHER way -- a table HUD reserves lineHeightLarge
    // for its title, which is a whole 4 cells -- so butting Standings under a column
    // of widgets left a gap no amount of dragging could close.
    //
    // Ceiling puts this on the lattice too (3 cells). The two title rows are still
    // different HEIGHTS, and that is fine and intended -- what has to match is that
    // both are whole cells, so the panels around them tile. The BAND itself is still
    // drawn at its own height by the plan's band; this only reserves the row.
    float titleRowHeight(float fontSize, float rowHeight) const {
        if (!m_bShowTitle) return rowHeight;
        // WITH A BODY CARD the title row is exactly the caption's own row plus the gap
        // the card leaves below it -- no clearance term, because panelContentY() now
        // starts content at the card's interior and the card's top is
        // frameMarginY + this row. The two are one equation read from opposite ends,
        // which is the whole point of anchoring content to the card: there is nothing
        // left for a second chain to disagree with.
        // The band's own height is titleBandBoxHeight()'s, not a second spelling of it.
        // ONE LINE, CARD OR NO CARD. The no-card path used to add a clearance term and
        // ceil the result, and both are gone for reasons rather than for tidiness:
        //
        // The clearance was `frameMarginY + band - paddingV`, which can never exceed the
        // band now that paddingV >= frameMarginY is guaranteed (theme_panel_padding_test) --
        // so max(band, need) was always band and the term was unreachable.
        //
        // The ceil is what made hiding the body card save almost nothing while hiding the
        // BAND visibly shrank the panel: the row rounded back up and swallowed most of
        // what the card's border gave back. panelHeight() ceils the whole panel anyway,
        // which is what keeps panels tiling, so rounding this one term as well bought
        // nothing but the asymmetry.
        //
        // PLUS THE GAP THE CARD LEAVES BELOW IT, when there is a card to leave one. This
        // has to be reserved here as well as spent in contentCardTop(): placing the card a
        // gap lower without lengthening the row puts the panel a gap short and the last
        // row over its own bottom border -- the same "reserve it in the height AND add it
        // to the running y; one without the other just moves the bug" that sectionGapY()
        // documents. No card means no second box and no gap, which is also what
        // contentCardTop() decides.
        // THE CAPTION'S BOX EXISTS WHETHER OR NOT A BAND IS DRAWN OVER IT — the same
        // rule panel_box.h states for every box, and the rule this had backwards on
        // both terms. [title] margin was in NEITHER end (titleBandTop clamped the
        // band flush to the frame and this reserved nothing), and [title] padding
        // was reserved only through titleBandBoxHeight, i.e. only when there was a
        // band. With a theme whose band is off, both were dead.
        //
        // Banded, the padding is INSIDE titleBandBoxHeight (a border box); bandless
        // there is no box to be inside of, so it is spent here. The margin is air
        // OUTSIDE the border either way and is never in that height.
        const float gap = hasThemedContentCard() ? contentGapY() : 0.0f;
        const float box = hasThemedTitleBand()
                        ? titleBandBoxHeight(fontSize)
                        : rowHeight + titlePadY(false) + titlePadY(true);
        return box + titleMarginY(false) + titleMarginY(true) + gap;
    }

    // THE BAND'S BOX, and the only place its height is spelled.
    //
    // A BORDER BOX, like the body card: its own edge slice, then title.padding-y, then
    // the glyph, then the same back. Without the border term the box was shorter than
    // two of its own corners past a [card] size of ~1.6, so NineSlice::clampedBorder
    // scaled them down -- the header's border stopped thickening while the body card's
    // kept going, and the caption drifted off centre as the clamp ate the border under
    // it.
    //
    // ONE OWNER because there were three spellings of this and one of them was already
    // wrong. The plan DRAWS the band, titleRowHeight above RESERVES the row a HUD
    // puts it in, and SettingsHud::titleAdvance reserves it for the settings panel --
    // and when the border term was added, the third was missed.
    //
    // It survived because the settings panel LAYS OUT from the drawn band's bottom edge
    // and only SIZES itself from titleAdvance, so nothing was misplaced -- the panel was
    // 2 * border too short, and its last row simply ran out through the bottom. That is
    // silent until the border is thick enough to matter: 0.0196 of overflow at [card]
    // size 1, 0.0587 at size 3, against a bottom padding of 0.0234.
    //
    // A caller reserving room for a band must call THIS, not restate it.
    // The caption box is the normal tier's GLYPH CELL whatever the caller's tier —
    // one band height across the surface, so a legacy-chain HUD's band measures
    // the same as a plan panel's. It MIRRORS resolvePanelSpec's `captionH`, and
    // must keep mirroring it: that is the cell (dim.fontSize), NOT the
    // lineHeight row. Line height is the pitch between stacked rows and half of
    // it is leading meant for the next row; a caption has no next row, so
    // spending it here buys air at the panel's own top edge — the gap a user
    // sees after zeroing every box term. This said lineHeightNormal while the
    // plan said fontSize, and the two drew bands ~0.17 em apart on the same
    // screen, with this comment already claiming they agreed.
    // ...AND THE CAPTION BLOCK'S QUANTIZATION REMAINDER, which is the half of the
    // plan's band this used to leave out. PanelBox::layoutPanel ceils the caption
    // block's ADVANCE (margin + box + margin + gap) to a whole cell and stretches the
    // DRAWN band bottom by the remainder -- see titleSlack there for why the block is
    // quantized at all. This chain drew the box alone, so a legacy-chain HUD's and the
    // settings panel's band came out titleSlack SHORTER than a plan panel's on the same
    // screen with the same theme: 0.009335 of screen height, ~10px at 1080p, while the
    // paragraph above already claimed the two agreed. Reported as "the height of the
    // settings title does not line up with the height of the hud title to the left of
    // it". Pinned by settings_surface_test.
    float titleBandBoxHeight(float /*fontSize*/) const {
        const float core = titleBandCoreH();
        return core + titleBandSlackY(core);
    }

    // THE CAPTION'S BOX WITH NO BAND DRAWN OVER IT: the same box, minus the border
    // it is not drawing. A border needs art to draw it with, so a border that is not
    // drawn is not reserved -- the rule layoutPanel already applies to every box.
    //
    // The air stays: [title] padding is inside the box and [title] margin outside it,
    // and both are spent whether or not there is a band, because air owes a theme
    // nothing. So switching a band off shrinks the caption block by exactly the
    // band's border, which is what a reader expects from watching the border vanish.
    //
    // The slack is recomputed from the SHORTER core rather than carried over: it
    // quantizes the caption block's advance to a whole cell, so it is a function of
    // the core and subtracting the border afterwards would leave the block off the
    // lattice.
    float captionBoxNoBandH() const {
        const float core = titleBandCoreH() - 2.0f * titleBorderY();
        return core + titleBandSlackY(core);
    }

    // The border box alone. Split out because the slack is computed FROM it and
    // would otherwise recurse.
    float titleBandCoreH() const {
        return 2.0f * titleBorderY() + layout().fontSizeNormal * m_fScale
             + titlePadY(false) + titlePadY(true);
    }

    // PanelBox's `titleSlack`, spelled in absolute units instead of cells: the same
    // advance (the box plus its own margins plus the junction gap), ceiled to the same
    // y-cell lattice. The gap is [panel] gap alone, exactly as the plan folds it in --
    // NOT contentGapY(), whose bridged [content] margin the plan leaves outside the
    // quantized advance. Getting that wrong would put the two bands back out of step
    // by whatever the margin is.
    float titleBandSlackY(float core) const {
        const float cellY = layout().cellH * m_fScale;
        if (!(cellY > 0.0f)) return 0.0f;
        const float gapY = panelGapCells() * layout().cellW
                         * PluginConstants::UI_ASPECT_RATIO * m_fScale;
        const float adv = titleMarginY(false) + core + titleMarginY(true) + gapY;
        const float cells = adv / cellY;
        return (std::ceil(cells - 1e-6f) - cells) * cellY;
    }

    // [title] PADDING per side, resolved theme -> [Advanced] built-in and
    // converted square on screen, exactly as resolvePanelSpec resolves the same
    // key for a plan panel.
    //
    // There was a LayoutMetrics::titlePaddingY of half a cell doing this, on the
    // row lattice and reachable from no ini -- so a themed band's height, the
    // band emit and the settings caption were all built from the legacy spelling
    // while [Advanced] titlePadding fed the box term, and moving the key changed
    // what a panel RESERVED without moving the band or the glyph. Reported as
    // "titlePadding grows the space at the bottom rather than around the title".
    float titlePadY(bool bottom) const;

    // [title] MARGIN per side, the same resolution. The band's air OUTSIDE its
    // own border, which the legacy chain spent nowhere: titleRowHeight did not
    // reserve it and titleBandTop clamped the band flush to the frame, so
    // raising [title] margin on a themed HUD grew nothing and moved nothing
    // while the plan panels beside it honoured it. Both now ask this.
    float titleMarginY(bool bottom) const;

    // How far a LEFT-justified caption sits right of the content column so it clears
    // the title band's own left edge slice. Zero for a HUD with a body card, which
    // already pushes its content that far; non-zero only for a panel whose band is
    // its only inner geometry. (There was a titleStringX() beside it, the same sum
    // for a layout fast path to use -- unreferenced since the plan took over the
    // caption's x, so it went with the rest of the pre-plan geometry.)
    float titleGlyphInsetX(float contentX) const;


    bool hasThemedContentCard() const;
    void emitContentCard(float bandBottom);

    // contentRowInsetX() IS GONE. It answered "how far in does a row sit" as the
    // frame's border plus the card's -- true when it was written, and false once
    // [panel] padding started acting on a plan panel's card: the card moved in by the
    // padding and this did not, so every full-row BAND derived from it (a hover
    // highlight, the player-row highlight, a slide trail) drew OUTSIDE the card it
    // was supposed to sit in. Reported as "the highlight grows way beyond where the
    // text is".
    //
    // A band's span is the CONTENT COLUMN, and PanelPlan already owns that:
    // p.contentX() / p.contentW(). Eight call sites spelled this one inset by hand,
    // which is eight chances to spell it as it used to be; asking the plan is one.

    // WHO GETS ONE: any panel that opts in -- the table and graph HUDs (Standings,
    // Event Log, Lap Log, Telemetry, Performance) and most widgets.
    //
    // A WIDGET USED TO BE BANNED from opting in, by a lint. The ban was written when a
    // widget's card was a shrink-wrap mode that hugged the quads a gauge drew, so the
    // card round a compass wrapped the dial rather than the panel's bounding box: the
    // widget then carried two cards of DIFFERENT widths -- a panel-flush header over a
    // content-hugging body -- where a HUD carries two flush ones. That mode is gone.
    // emitContentCard() draws at the panel's frame margin, the same X span as the title
    // band, so a widget now gets exactly the HUD treatment and the lint rule was lifted
    // (check_hud_helpers.sh still documents it, at the rule-4 slot, with the render that
    // justified lifting it).
    //
    // What replaced the ban is a CHOICE: a theme sets [card] widget-content = 0 when its
    // inner slices are too much furniture around a single gauge. That was the ban's real
    // second objection, and it belongs to the theme rather than to the code.
    //
    // OPT-IN even among the HUDs that could take one, and not because the feature is
    // unfinished: emitting the card inserts 9 quads immediately after the title, and
    // a HUD that indexes m_quads by a HARDCODED position rather than one recorded at
    // build time reads the wrong quad afterwards. GearWidget indexed `m_quads[1]` for
    // its gear circle and was already wrong under any theme (a themed background is 9
    // quads, not 1); it records the index now (m_circleQuadIndex), and SessionHud's
    // row-icon walk was a second instance found the same way.
    // Set in the constructor by every widget and by SettingsHud; the default is the
    // table/graph HUDs, which are the majority.
    PanelKind m_panelKind = PanelKind::Hud;
    bool m_bContentCard = false;
    bool m_contentCardEmitted = false;   // per-rebuild; armed by addBackgroundQuad

    // SECTIONS instead of one card, for a HUD whose content is more than one block:
    // Performance's Frame Rate and CPU Time graphs, Session Charts' per-chart panels.
    // One whole-body card round both of them says they are one thing, and they are
    // not -- which is the same reason the settings panel gives each block its own
    // card rather than one card round the tab.
    //
    // Set this AND m_bContentCard: the pair still means "this HUD's content is
    // carded" (so rows clear the card's border and full-row bands inset past it),
    // and this says the HUD emits the cards itself via beginContentSection().
    bool m_bContentSections = false;

    // A body SECTION card, wrapping ONE block of content and the heading that opens
    // it. Reserve-then-rewrite, because a section's height is only known once it
    // ends: begin() pushes nine zero-size slices so the card lands BEHIND the content
    // (quads draw in emission order), end() sizes them.
    //
    // The same protocol the settings panel's section cards use, and for the same
    // reason -- there was no way to size a card round content that has not been laid
    // out yet, and drawing it afterwards puts it on top of the rows it should sit
    // behind.
    //
    // x/y/width are PRE-offset, like every other add* helper. Pass the heading's own
    // row as `y`: the card then contains its heading, which is the whole point.
    void beginContentSection(float x, float y, float width);
    void endContentSection(float bottomY);
    // Closes the last section AND stretches it to the body's bottom edge, so a
    // sectioned HUD's outermost card edges land exactly where a single whole-body
    // card's would. Call once, at the end of rebuildRenderData().
    void finishContentSections(float bottomY);
    float sectionCardPaddingY() const;
    // The top edge the CURRENT section's card will actually get. NOT the y passed to
    // beginContentSection: the FIRST section's top is pulled up to the body's top, so
    // a caller centring one tall glyph has to centre it in the CARD or the glyph sits
    // low by however far those two differ. A HUD whose section opens with a heading
    // never notices (the heading hugs the top either way); one whose section IS a
    // single centred value does.
    float sectionCardTop() const;
    // THE ADVANCE BETWEEN TWO SECTIONS was sectionGapY() here, ceiled to whole
    // cells so a boundary could not drag the panel off the lattice. Gone with the
    // rest of the pre-plan chain: PanelBox::layoutPanel spends the seam (the
    // facing margins plus the junction gap) and ceils the PANEL once instead of
    // every boundary, so there is nothing left for a caller to reserve by hand.
    // A panel whose size is DERIVED -- from art, a dial diameter, an aspect correction
    // -- rather than composed from rows and columns lands between grid cells, and then
    // it cannot tile with what is around it. The error is invisible ON that panel; it
    // shows up as the NEXT one down sitting a fraction of a cell out, so it is found by
    // whoever stacks them rather than by whoever built it.
    //
    // Round the box UP to whole cells and split the slack. The CONTENT keeps its size
    // and its position relative to the panel's centre -- a dial drawn circular by
    // dividing by UI_ASPECT_RATIO stays exactly that circle; the box gains margin, it
    // does not stretch the art. Callers setBounds with {w,h} and shift their content
    // anchor by {padX,padY}.
    //
    // Pinned by THE GRID SWEEP in theme_geometry_test, which measures every registered
    // panel -- the check that found the six panels this exists for.
    struct GridFit { float w = 0.0f, h = 0.0f, padX = 0.0f, padY = 0.0f; };
    GridFit fitPanelToGrid(float width, float height) const {
        const LayoutMetrics& L = layout();
        const float sw = L.ceilX(width);
        const float sh = L.ceilY(height);
        return { sw, sh, (sw - width) * 0.5f, (sh - height) * 0.5f };
    }

    // ONE FILL LAYER PER PIXEL. The frame's CENTRE slice fills the whole panel interior
    // and the title band and body card(s) are then drawn ON it, so a translucent panel
    // is twice as opaque wherever something is stacked: at background opacity a, the
    // margin passes (1-a) of the game and the card passes (1-a)^2. Identical art on both
    // layers still lands on two different tones -- a*V + (1-a)*B against a*V*(2-a) +
    // (1-a)^2*B, equal only at a = 1 -- so a translucent theme cannot have a uniform
    // interior no matter how it is drawn.
    //
    // This cuts the centre into the rects nothing covers instead. The band and the
    // whole-body card span exactly the centre's width; inner cards carry their real
    // rects -- the settings panel draws its cards in two COLUMNS, so its complement is
    // gutters plus per-column gaps, not full-width strips. The geometry is
    // NineSlice::cutFill (a slab sweep, unit-pinned); this collects the covers and
    // writes the result over the reserved quads.
    //
    // Runs after rebuildRenderData from processDirtyFlags, because the covering rects
    // are only all known once the HUD has finished building. The strips are RESERVED
    // next to the frame's own quads, so rewriting them later cannot change draw order.
    void finalizeThemedFill();
    // The band's rect, recorded by the band emitter for the sweep above. Post-offset.
    //
    // ALL FOUR EDGES, including the horizontal pair, which readers used to substitute
    // the frame's centre slice for. That was true while every band sat on the frame's
    // inner boundary and stopped being true when panelSurfaceInsetX() gained an
    // override: the fill cut then covered a strip the band does not actually reach,
    // so the panel's own fill was removed from beside its header. Two readers
    // (finalizeThemedFill and MXBMRP3_Test_HudFillCut); both take these now.
    float m_bandLeft = 0.0f, m_bandRight = 0.0f;
    float m_bandTop = 0.0f, m_bandBottom = 0.0f;
    bool m_bandValid = false;
    // The whole-body card's, when the HUD draws one instead of sections. Post-offset.
    float m_wholeCardTop = 0.0f, m_wholeCardBottom = 0.0f;
    bool m_wholeCardValid = false;
    // THE POOL IS USUALLY JUST THE CENTRE SLICE, so a themed panel costs no extra quads.
    // Strip 0 IS that slice: a panel with a title band and a body card has exactly ONE
    // uncovered gap (both reach the centre's top and bottom edges), so repurposing the
    // slice covers it with nothing reserved. Only a SECTIONED panel has more gaps -- one
    // per seam between its cards -- and only that shape pays for extra quads.
    //
    // Reserving six for everything cost 5 zero-area quads on every themed HUD to solve a
    // case only three HUDs have. If the rects ever outrun the pool (or the covers outrun
    // cutFill's cap) the WHOLE centre comes back as one strip: stacking under every card,
    // a uniform tone error, where running out mid-sweep would leave a HOLE straight
    // through to the game.
    static constexpr int SECTION_FILL_STRIPS = 6;
    int m_fillFirst = -1;
    int m_fillCount = 0;      // strips available: 1, SECTION_FILL_STRIPS, or m_fillReserve
    // Floor for the pool, for a panel whose cards are NOT full-width spans (the settings
    // panel's two card columns): the slab sweep spends a strip per gap per column plus
    // one per gutter between and beside the columns. 0 = the default rule above.
    int m_fillReserve = 0;

    // Every inner card this rebuild emitted, as its full rect pre-offset, keyed by its
    // first quad so a reserve-then-rewrite card updates its record in place. These are
    // the covers finalizeThemedFill cuts the fill against -- a card NOT recorded here
    // sits on the fill and composites twice (the settings-cards-read-darker bug) --
    // and what the no-overlap test hook reads. Cleared per rebuild.
    struct SectionCardSpan {
        float top = 0.0f, bottom = 0.0f;
        float left = 0.0f, right = 0.0f;
        int firstQuad = -1;
    };
    const std::vector<SectionCardSpan>& sectionCardSpans() const { return m_sectionCards; }

    // THE BOX A HUD'S GRAPHICAL CONTENT MUST STAY INSIDE: the body card's inner area
    // when the theme draws one, the panel's padded interior when it does not. Returns
    // false when there is no panel rect to derive it from.
    //
    // Exists because a HUD that DRAWS rather than lays out rows -- the map's track, the
    // radar's sweep -- has to clip itself, and clipping to the panel is wrong the moment
    // a theme adds a frame and a card: the panel's edge is the outside of the frame, so
    // a rotating map drew over its own border and out into the frame margin. Post-offset
    // (absolute screen space), matching what those clippers work in.
    bool contentClipRect(float& left, float& top, float& right, float& bottom) const;

    // THE BODY CARD'S INTERIOR IN Y, PRE-OFFSET -- what a one-row panel whose content is
    // a coloured FILL has to size that fill to. contentClipRect() answers the same
    // question post-offset, for HUDs that clip a drawing; the add* helpers take
    // pre-offset coords, so a HUD placing a quad needs this form.
    //
    // WHY A HUD CANNOT JUST USE ITS OWN ROW. The content row is
    // (paddingV + titleRowHeight) from the panel's top; the card is derived from the
    // PANEL instead -- the band's bottom down to panelBottom - frameMarginY --
    // and the two do not meet. Measured on a captioned Gap Bar under a 2-cell frame /
    // 1-cell card: the row is 6.000..8.000 cells while the card's interior is
    // 6.204..8.500, so the fill overhung the card's top border and stopped half a cell
    // short of its bottom, and the markers -- centred on the row -- sat 0.35 cells above
    // the centre of the box they appear to be in.
    //
    // The two cannot be reconciled by making them equal: paddingV would have to be
    // exactly frameBorderY + cardBorderY (2.5 cells there), and contentPaddingY() ceils
    // it to a whole cell precisely so rows stay on the lattice. The slack is real and
    // has to go somewhere; for a panel whose content IS its card, it goes into the fill.
    //
    // False when there is no card (unthemed, or a theme without card slices), where the
    // caller's own row geometry is already right.
    bool contentCardSpanY(float& top, float& bottom) const;

    // The frame's CENTRE -- the region finalizeThemedFill cuts into strips.
    // Shared with the fill-cut test hook so the check and the thing it checks
    // cannot disagree about where the centre is. See the definition.
    bool themedCentreRect(float& left, float& top, float& right, float& bottom) const;
    // Quad index of fill strip `i`, or -1. Strip 0 IS the frame's centre slice; the
    // rest are the pool reserved after the nine. For MXBMRP3_Test_HudFillCut.
    int fillStripQuad(int i) const {
        if (m_bgQuadFirst < 0 || i < 0 || i >= m_fillCount) return -1;
        return (i == 0) ? m_bgQuadFirst : m_bgQuadFirst + NineSlice::SLICE_COUNT + i - 1;
    }

    // True when a band would be drawn, so a caller can reserve its
    // height before laying anything out. See the definition.
    bool hasThemedTitleBand() const;
    // See the definition: the kind flag alone, without the caption toggle.
    bool themeDrawsTitleBandKind() const;
private:
    // Remember (or update -- reserve-then-rewrite lands on the same first quad
    // twice) a card's rect as a cover for finalizeThemedFill. PRE-offset, like
    // the coords its callers take.
    void recordCardCover(int firstQuad, float x, float y, float width, float height);
    // Shared emitter for both slice sets; useCard picks the quieter card set.
    void emitThemedSlices(const ThemeAsset& theme, float x, float y,
                          float width, float height, int firstIndex, bool useCard);
    // BAND is INNER's sprites at `[card] band-size` rather than `[card] size` -- one
    // art set, two scales. It is a member of this enum and not a bool beside it so
    // that every emit names its set once, at the call, and cannot pick the box from
    // one and the corner from the other.
    enum class SliceSet { OUTER, INNER, BAND, BUTTON };
    void emitThemedSliceSet(const ThemeAsset& theme, float x, float y,
                            float width, float height, int firstIndex,
                            SliceSet set, unsigned long colorOverride);
public:
    // Themed card, for elements drawn INSIDE a panel: settings sections and
    // anything else that wants the theme's secondary surface. Coords are pre-offset
    // (the caller is already laying out in offset space). No-op without a theme,
    // without an card slice set, or when the rect is degenerate.
    // reserveOnly emits the nine slices at a degenerate rect, positioned later by
    // rewriteThemedCard -- for cards that must sit behind content whose height
    // is not known until that content is laid out (settings sections).
    void addThemedCard(float x, float y, float width, float height,
                            bool reserveOnly = false);
    void rewriteThemedCard(int firstIndex, float x, float y, float width, float height);
    // True when a themed card would actually draw; lets a caller skip
    // reserving quads it would never use.
    bool hasThemedCard() const;
    // Themed button background in `color` (the caller's state colour). Returns
    // false when the theme has no button slices, so callers keep their plain quad
    // as the fallback. Coords are PRE-offset.
    bool addThemedButton(float x, float y, float width, float height, unsigned long color,
                         bool opaque = true);
    bool hasThemedButton() const;
    // In-place variant of addThemedButton for index-coordinated layout fast paths.
    // A button background anywhere in a HUD: themed button slices when the theme has
    // them, a plain solid quad otherwise. Coords are PRE-offset. The settings panel's
    // equivalent is SettingsLayoutContext::addButtonBackground, which additionally
    // insets the button inside its row; a HUD sizes its own buttons, so this does not.
    //
    // Exists so a HUD button cannot be left unthemed by omission -- every one that
    // hand-rolled its quad (the version popup's four, the notices slabs) simply
    // missed the theme when it arrived, silently.
    // A button's colour, made fully opaque without losing the state its alpha encodes.
    // See the definition -- applied inside addButtonQuad/addThemedButton, so callers keep
    // expressing disabled / idle / hovered as alphas and get an opaque control anyway.
    unsigned long opaqueButtonColor(unsigned long color) const;

    // How an UNTHEMED button fills itself. A themed one is unaffected either way: its
    // slice art already reads as a raised chip and the colour only tints it.
    enum class ButtonFill {
        // A SURFACE: the panel's own background lifted a step toward the text colour,
        // then tinted a bounded amount toward the state colour. Without slice art the
        // fill is the only thing that says "control", and the state colour at full
        // strength says "highlight" instead -- a solid block of accent with dark text
        // punched out of it. The state moves to the LABEL (see buttonGlyphColor), which
        // is what keeps Save green and Reset red while every button reads as the same
        // kind of object. Hover raises the tint but never past the surface, so the
        // label stays legible -- the property this exists to protect.
        Surface,
        // The state colour itself, flattened onto the panel (the older treatment).
        // For a SELECTION band rather than a control: the settings panel's active tab
        // is a row that happens to be drawn with the button slices, and it has to read
        // as strongly as a selection, not as politely as a button.
        State,
    };
    void addButtonQuad(float x, float y, float width, float height, unsigned long color,
                       bool opaque = true, ButtonFill fill = ButtonFill::Surface);
    // The fill addButtonQuad would draw for this state colour. Exposed so a caller can
    // colour a label against the real fill instead of guessing.
    unsigned long buttonFillColor(unsigned long stateColor) const;
    // The LABEL colour for a button carrying `stateColor`. Themed, the fill is the
    // state colour so the glyph is punched out of it (chipGlyphColor); unthemed, the
    // fill is a neutral surface and the state rides the glyph instead.
    unsigned long buttonGlyphColor(unsigned long stateColor) const;

    // A BUTTON'S STATE, in the one place that turns it into an alpha. Ten sites
    // spelled this ternary out by hand and picked three different idle alphas
    // (128/255 in the settings footer and its Copy/Reset, 0.5f in VersionWidget's
    // four, 0.3f on Check Now's disabled), so the same control was a different
    // shade depending on which file drew it.
    enum class ButtonState { Idle, Hovered, Disabled };
    unsigned long buttonStateColor(unsigned long stateColor, ButtonState state) const;

    // A BUTTON, WHOLE: the state fill (themed slices or the surface treatment) and
    // its label, horizontally centred in the box. The label colour is DERIVED from
    // the fill -- naming a slot beside it is how a button ends up drawn in its own
    // colour, invisible under any theme with button art -- except when Disabled,
    // where it stays MUTED: a greyed control must not read as an enabled one, and
    // deriving from the grey fill is exactly what makes it look live.
    //
    // Geometry and click regions stay the caller's, `labelY` included: a footer
    // button, an in-card action row and VersionWidget's notification chips place
    // themselves very differently, and only the drawing was ever the same.
    void addStateButton(float x, float y, float width, float height,
                        const char* label, float labelY, float fontSize,
                        unsigned long stateColor, ButtonState state,
                        unsigned long glyphColorOverride = 0);
    // Smallest BT.601 luma gap a coloured label may have against its own fill.
    // 45 keeps disabled (muted, ~48) on its own colour and pushes red (~25) onto the
    // panel's text colour. Public so a test can assert the property against the LIVE
    // threshold instead of freezing 45 into an expectation -- the mistake that made a
    // batch of layout tests go red the moment a metric root was retuned.
    static constexpr unsigned int MIN_GLYPH_LUMA_GAP = 45u;

    // Legible ink for `ink` drawn on `fill`: keeps the hue while it clears
    // MIN_GLYPH_LUMA_GAP, lifts it toward the palette end away from the fill when it
    // does not, and only falls to a punched glyph as the floor. Shared by the button
    // labels and by any caption drawn on a coloured slab.
    unsigned long legibleOnFill(unsigned long ink, unsigned long fill) const;
    // The ink for a caption on a slab of its OWN slot colour, drawn at `fillOpacity`
    // -- the Notices slabs. See the definition for why "NEGATIVE on NEGATIVE"
    // survived this long and what it looks like on an opaque theme.
    unsigned long captionOnSlabColor(unsigned long slot, float fillOpacity) const;
    // The same, where the ink and the SLAB are not the same slot -- the Gap Bar, whose
    // fill tracks the LIVE gap while its text may be showing a FROZEN one, so the two
    // can legitimately disagree in sign (red text over a green fill) and correcting
    // against the ink's own colour would answer the wrong question. Both forms flatten
    // the slab the same way, because the pixel the caption competes with is the slab
    // after its alpha is spent, not the slot colour.
    unsigned long inkOnSlabColor(unsigned long ink, unsigned long slot,
                                 float fillOpacity) const;

    // A ROW BAND: the strip behind a selected or hovered row. Six HUDs drew one by
    // hand -- standings (player row and spectator hover), records, event log, and the
    // settings panel's tab list and control rows -- each with its own alpha and its
    // own idea of which palette slot to use. Three spellings of 60/255, two of 80/255,
    // and no rule about ACCENT versus MUTED.
    //
    // TAKES THE THEME'S BUTTON SLICES, the same shape the settings panel's selected
    // tab has always had (it goes through addButtonQuad). A band and a selected tab
    // are the same kind of object -- "this row is the one" -- so a theme that rounds
    // one and leaves the other a hard rectangle looks like a miss, which is what it
    // was: this used to be deliberately flat, on the grounds that a band is
    // repositioned by cached index during the standings row-slide animation and a
    // nine-slice is not one quad. That is now handled by repositionRowHighlight()
    // rewriting the whole span, which is the same reserve-then-rewrite the section
    // cards already use.
    //
    // TRANSLUCENT, unlike a button: the alpha is NOT flattened onto the background
    // (addThemedButton's opaque=false). A control must stay legible and so is made
    // opaque; a row band is a tint over whatever the row sits on, and flattening it
    // would turn every hover into a solid box.
    //
    // Returns the FIRST quad index of the band (-1 if nothing was drawn), for a
    // caller that repositions it later. The span is 1 quad unthemed, SLICE_COUNT
    // themed -- never assume which; pass the index back to repositionRowHighlight.
    //
    // Callers pass the slot and the alpha, because which STATE a band means is the
    // caller's to say; the two canonical alphas are below so a change is one edit.
    int addRowHighlight(float x, float y, float width, float height, unsigned long color);

    // Move a band emitted by addRowHighlight, in place. Rewrites the whole span --
    // one quad or nine -- so a themed band survives the standings slide animation and
    // the event log's drag, both of which move a band without a rebuild. Colour is
    // taken from the band itself, so this cannot drift from what was emitted.
    //
    // Safe against a theme switch because one marks every HUD dirty, so the next
    // reposition always runs against a band emitted under the current theme; the
    // bounds check is what covers the ordering if that ever stops being true.
    void repositionRowHighlight(int firstIndex, float x, float y, float width, float height);

    // SELECTED (this row is the subject) versus HOVERED (the pointer is over it).
    // Two steps apart on purpose: on a themed panel both sit over a card, and a hover
    // that reads as strongly as a selection makes a table look like it has two
    // selected rows whenever the mouse is inside it.
    static constexpr float ROW_SELECT_ALPHA = 80.0f / 255.0f;
    static constexpr float ROW_HOVER_ALPHA  = 60.0f / 255.0f;

    // Nesting metrics for the ACTIVE theme, all zero when there is none. Thin
    // wrappers over NineSlice's pure helpers -- see nine_slice.h for what each
    // band means and why they are derived from the theme rather than tuned.
    //
    // Callers that draw a card around content use the pair:
    //     card.x = content.x - cardBorderX()
    //     card.w = content.w + 2 * cardBorderX()
    // and get a card whose edge slice wraps the content instead of sitting on it.
    // A panel makes room for that by taking contentPaddingX() as its horizontal
    // padding (and 2x to its width), which is what keeps the card clear of the
    // outer frame at the same time.
    // This HUD's layout vocabulary: its active theme's, or the global defaults when
    // it has no theme. Every metric a HUD lays out with comes from here, so a theme
    // that adjusts its own spacing reaches every HUD drawing with it -- and a
    // per-HUD theme override changes that HUD's rhythm along with its sprites.
    //
    // Returns a reference to storage owned by AssetManager / LayoutConfig, both of
    // which outlive every HUD, and is a plain member read (no parse, no lookup) so
    // it is safe in a rebuild path. See layout_metrics.h for roots vs derived.
    const LayoutMetrics& layout() const;

    float frameBorderX() const;
    // Same band vertically. The insets are authored in Y units, so this IS the
    // inset -- it exists so call sites read symmetrically instead of one axis
    // going through a helper and the other reaching into ThemeAsset.
    float frameBorderY() const;

    // WHERE THIS PANEL'S OUTER SURFACES SIT: the inset from the panel background's
    // edge at which the title band's edge slice starts. PanelBox's panelInnerLeft /
    // panelInner, for the two chains that do not go through it.
    //
    // The frame's border ALONE here, and it is not an oversight: this chain anchors
    // its content with contentPaddingX()'s max(), which leaves the first glyph at
    // [panel] padding. Move the surfaces in by that same padding and the content --
    // which did not move -- ends up level with or outside its own card's border.
    // Closing that means the additive model contentPaddingY()'s header describes,
    // which is blocked on the centre stack's stored offsets.
    //
    // SettingsHud overrides both. It is already additive on both axes (its content
    // starts at frame border + [panel] padding), so its surfaces can sit where a
    // plan panel's do -- which is what "the settings panel should respect [panel]
    // padding like the HUDs do" asks for.
    virtual float panelSurfaceInsetX() const { return frameBorderX(); }
    virtual float panelSurfaceInsetY() const { return frameBorderY(); }
    // The SIDE margin as a vertical distance covering the same pixels -- what auto
    // border-y resolves to. See the definition.
    float cardBorderX() const;
    float cardBorderY() const;
    // The same, at `[card] band-size` -- equal to the card pair unless a theme sets
    // that key. Band geometry asks these; card geometry asks the pair above.
    float titleBorderX() const;
    float titleBorderY() const;

    // The horizontal padding a CENTER-STACK panel uses (GapBar / Notices / Timing).
    //
    // contentPaddingX() carries the body card's clearance only for a panel that
    // CARRIES one -- correct for a HUD sized from its own content, wrong for these
    // three, which are all one fixed character count wide and exist to line up with
    // each other. Timing and GapBar have a body card and Notices does not (its
    // coloured slab IS its content), so with a theme on, the same character count
    // came out a cell narrower per side on Notices while the unthemed stack was
    // flush -- reported as "notices is one cell narrower once I pick a theme".
    //
    // So the stack pays the WIDEST member's clearance, whether the panel asking
    // carries a card or not. Unthemed both terms are zero and this is the plain
    // panel padding, so the shipped stack is unchanged.
    // Used via CenterStack::boxWidth(); pinned by theme_geometry_test.
    float centerStackPaddingX() const;

    // Put a panel EDGE on the snap lattice, when the user has grid snapping on.
    // Returns the edge unchanged when it is off, so this is safe to apply
    // unconditionally at the call site.
    //
    // Static and public because the callers are not all inside a HUD's own drag
    // path: a centred panel snaps its anchor during rebuild, and the settings
    // panel snaps an origin it recomputes from scratch every frame. Those had
    // grown three near-identical private copies of this gate, which is how the
    // Notices and Timing panels came to agree with each other but not with the
    // GapBar above them. One helper, one lattice.
    static float snapEdgeX(float edge);
    static float snapEdgeY(float edge);

    // LAYOUT-SPACE LEFT FOR A CENTRE-ANCHORED ELEMENT: one whose stored offsetX
    // means "where my CENTRE sits" (default 0.5) rather than "where my left edge
    // sits" like every other element. Gap Bar, Notices, Timing and Version are
    // the four -- they live mid-screen, so a width change from scale, from the
    // bar-width setting or from content must grow them symmetrically about the
    // stored centre instead of walking one edge out. Calling THIS is what
    // declares the convention, and each of the four also stores 0.5 as its
    // default offsetX; settings v7 shifts an older Notices/Timing offset, which
    // meant a left edge, onto the new meaning.
    //
    // DELIBERATELY UNSNAPPED, and that is the whole point: snapping the left edge
    // and holding the centre are different quantizations, and a panel cannot do
    // both -- with the snap in here the centre walked by up to a cell every time
    // the width changed, which is exactly the drift this anchor exists to stop.
    // Snapping belongs to the DRAG path, which snaps the resulting absolute left
    // edge (see base_hud.cpp) and so still lands a dragged panel on the lattice.
    //
    // A centre-anchored element also needs NO scale compensation:
    // setScaleKeepingCenter on top of this recentring double-compensates and
    // walks the centre by half the growth per step (the Gap Bar shipped that).
    static float centerAnchoredPanelLeft(float panelW);

    // The default offsetX for such an element: the middle of the screen. The four
    // wrote it four ways (two local `constexpr float CENTER_X = 0.5f` in anonymous
    // namespaces, two bare 0.5f literals), which is how Notices and Timing came to
    // store a DELTA while GapBar and Version stored a CENTRE without anything
    // saying so. One constant, next to the function that consumes it: a defaults
    // site that writes THIS is declaring which convention it means.
    static constexpr float CENTER_ANCHOR_X = 0.5f;
protected:
    // A ring or ring-slice as a fan of quads. Shared by every HUD that draws one;
    // see the definition for why the angles are rotated rather than recomputed.
    void addArcSegment(float centerX, float centerY, float innerRadius, float outerRadius,
                       float startAngleRad, float endAngleRad, unsigned long color,
                       int numSegments);

    void addDot(float x, float y, unsigned long color, float size);
    // Centered, aspect-corrected icon sprite (like addDot, but textured). size is
    // the height in normalized units; the sprite is tinted by color.
    void addIcon(float x, float y, int spriteIndex, unsigned long color, float size);
    void addLineSegment(float x1, float y1, float x2, float y2, unsigned long color, float thickness);
    void addHorizontalGridLine(float x, float y, float width, unsigned long color, float thickness);
    static void setQuadPositions(SPluginQuad_t& quad, float x, float y, float width, float height);

    // Right-pointing triangle in the same box setQuadPositions would fill: the two
    // right-hand vertices collapse onto a single tip at the vertical middle. The
    // draw primitive is four arbitrary corners (m_aafPos[4][2]), not a rect, so a
    // degenerate edge is a legal quad -- addNeedleQuad already relies on that.
    //
    // Used for the standings brand indicator, which real broadcast graphics draw as
    // an arrow rather than a bar.
    static void setQuadPositionsArrowRight(SPluginQuad_t& quad, float x, float y,
                                           float width, float height);

    // The same rect setQuadPositions() would fill, with the SPRITE turned 90
    // degrees clockwise inside it -- an up-caret comes out pointing right.
    //
    // Done by cycling which rect corner each vertex takes, not by swapping a pair:
    // a swap mirrors, mirroring reverses the winding, and the engine back-face-culls
    // a non-CCW quad so the sprite silently never draws. Same trap the pos-gain
    // caret's 180-degree case documents (it negates both axes rather than flipping).
    //
    // For callers that need an arbitrary angle rather than a right angle, use
    // addRotatedSpriteQuad() -- it rotates in uniform space and aspect-corrects
    // after, which is what a map marker needs. This one preserves the caller's rect
    // exactly, which is what a sprite replacing a hand-built quad needs.
    static void setQuadPositionsRotatedCW(SPluginQuad_t& quad, float x, float y,
                                          float width, float height);

    // Add a gauge needle (trapezoid: flat tip, wider base) pointing outward at angleRad.
    // Shared by SpeedoWidget and TachoWidget.
    void addNeedleQuad(float centerX, float centerY, float angleRad,
                       float needleLength, float needleWidth, unsigned long color);

    // Add a square sprite quad centered on (screenX, screenY), rotated by the
    // given yaw in UNIFORM (square) space and aspect-corrected on X only after
    // rotation, then translated by the HUD offset. Shared by RadarHud and
    // MapHud rider markers; pass cos=1/sin=0 for non-directional icons.
    void addRotatedSpriteQuad(float screenX, float screenY, float halfSize,
                              float cosYaw, float sinYaw, int spriteIndex,
                              unsigned long color);

    // NO setScaleKeepingCenter. It was the center-preserving variant of setScale,
    // used by the Radar and the Gap Bar: it grew the bounds proportionally and
    // SHIFTED THE STORED OFFSET so the visual centre stayed put. Both are now
    // centre-anchored instead (see centerAnchoredPanelLeft), which gets the same
    // result from the layout, without editing a persisted setting as a side effect
    // of a scale change and without depending on bounds left over from the previous
    // render. On the Gap Bar the two mechanisms were stacked and double-compensated,
    // walking the centre by half the width change per scale step.

    // Temperature color gradient: blue (cold) -> green (optimal) -> yellow -> red (hot).
    // Shared by BarsWidget and TyreTempWidget. optTemp is the optimal/midpoint temperature.
    static unsigned long calculateTemperatureColor(float temp, float optTemp,
                                                   float alarmLow, float alarmHigh);

    // Helper to update background quad position during rebuildLayout (reduces duplication)
    void updateBackgroundQuadPosition(float startX, float startY, float width, float height);

    // Expand quad dimensions to match background texture aspect ratio (prevents stretching)
    void applyTextureAspectCorrection(float& x, float& y, float& width, float& height) const;

    // Styled string rendering with per-string padding and backgrounds
    void addStyledString(const HudStringConfig& config);
    void renderStyledStrings();

    // Calculate bounds for all styled strings (for HUD sizing)
    struct StyledStringBounds {
        float minX, minY, maxX, maxY;
        float width() const { return maxX - minX; }
        float height() const { return maxY - minY; }
    };
    StyledStringBounds calculateStyledStringBounds() const;

    // Scaled dimensions helper (eliminates repeated calculations in rebuildLayout/rebuildRenderData).
    // Public TYPE (the members stay protected): SettingsLayoutContext — a standalone
    // struct, not a BaseHud — carries a ScaledDimensions between the settings tabs.
    // gcc/MSVC let the protected nested-type access slide; clang (which fronts the
    // thread-safety analysis pass) correctly rejects it.
public:
    struct ScaledDimensions {
        float fontSize;
        float fontSizeExtraSmall;
        float fontSizeSmall;
        float fontSizeLarge;
        float fontSizeExtraLarge;
        float paddingH;
        float paddingV;
        float lineHeightExtraSmall;
        float lineHeightSmall;
        float lineHeightLarge;
        float lineHeightNormal;
        float lineHeightExtraLarge;
        // The snap grid at THIS HUD's scale. Every distance a layout file states is
        // in cells, so this is what spends them -- one named conversion instead of
        // each call site remembering whether its value was in lines, cells or
        // characters (which is what the old *Lines / *Chars / *Cells name soup was
        // trying and failing to encode).
        float cellW;
        float cellH;
        float scale;

        // Grid-aligned spacing, in cells. cellW/cellH are already scaled, so these
        // are just named multiplication -- kept because the call sites read better
        // as "one cell across" than as a bare product.
        //
        // These held their OWN copies of the grid (0.0111 and 0.0055) until the
        // lattice became live. The horizontal one happened to match; the vertical
        // one was 5.4% short of the real cell, so anything spaced with gridV() sat
        // slightly off the lattice every other part of the UI snapped to.
        float gridV(float units) const { return cellH * units; }
        float gridH(float units) const { return cellW * units; }
    };

    // Declared AFTER ScaledDimensions, which these take by reference: a member
    // function's parameter TYPES must be complete at its declaration (only the
    // body is compiled as if the class were), so up with the other layout helpers
    // this failed to parse on MSVC and gcc alike.
    // A BIG VALUE'S ROW -- what Position / Lap / Time / Clock give their one XL number.
    //
    // It was lineHeightLarge, a row sized for the glyph CELL. The normalized .fnt
    // digits ink ~63% of their cell, so an XL value's visible height is about 0.025
    // where the row reserved 0.047: the widgets stood two rows tall to show one row of
    // ink, which is why they read as tall next to the Version widget's single row.
    // The row is now a normal one, and the value is placed by centring its INK in it
    // (bigValueTextY) rather than its cell -- the same distinction the gear digit needs.
    // Which type tier a panel captions at. A panel titles at ONE of two sizes -- the
    // full HUDs at Large, the widgets at Normal -- and the tier picks BOTH the font and
    // the row together, which is the whole reason it is an enum and not two arguments.
    enum class TitleTier { Normal, Large };

    // The reserved title row, or nothing when the caption is off -- the form every
    // panel actually wants, and which was written out longhand at a dozen call sites.
    //
    // ONE HELPER, TIER AS A PARAMETER, because two helpers is two spellings and two
    // spellings is what drifted. This used to serve the Normal tier only, and its own
    // comment claimed the Large-tier panels "keep calling titleRowHeight directly".
    // They did not: thirteen of them wrote `m_bShowTitle ? dim.lineHeightLarge : 0.0f`,
    // reserving the bare ROW and never reaching titleRowHeight at all.
    //
    // THAT WAS NOT COSMETIC, and the exemption in check_hud_helpers.sh rule 7 said why
    // it looked safe: "lineHeightLarge already exceeds the band, so those panels
    // over-reserve rather than clip". Measured, that holds only while a BODY CARD is
    // drawn. Without one the Large tier needs 5 cells and the bare row is 4 -- a 12.67px
    // shortfall at 1080p, so the first content row starts inside the bottom of its own
    // title band.
    //
    // And the config that produces it is a theme's DEFAULT, not an exotic one:
    // ThemeAsset::titleBand defaults true while contentCard defaults FALSE, so any theme
    // that ships card slices and does not explicitly write `[card] hud-content = 1` gets
    // a band with no card -- exactly the case that clips. All three SHIPPED themes do
    // write it, which is why nothing looked wrong and why the grid sweep passed.
    //
    // titleRowHeight() reserves a plain `rowHeight` for a caption the theme draws no
    // band for -- the same row an untitled site would have used -- so routing a site
    // through here is pixel-identical unthemed and on every shipped theme. (It USED to
    // early-out for that case; the early-out is gone, because skipping the arithmetic
    // also skipped the frame's clearance and the caption then ignored the frame.)
    float reservedTitleHeight(const ScaledDimensions& dim,
                              TitleTier tier = TitleTier::Normal) const {
        if (!m_bShowTitle) { m_reservedTitleRow = 0.0f; return 0.0f; }
        // MEMOISED because the caption path needs this exact number and cannot derive it:
        // it is handed a font size, not a tier, and the two tiers reserve different rows.
        // Only the caller knows which it asked for. Safe to read there because every HUD
        // calls this to SIZE ITS PANEL, which it must do before addBackgroundQuad, which
        // must precede the caption -- that ordering is structural, not a convention.
        // What it is for: a caption with no band still reserves a
        // row, and the body card has to start under it.
        m_reservedTitleRow = (tier == TitleTier::Large)
            ? titleRowHeight(dim.fontSizeLarge, dim.lineHeightLarge)
            : titleRowHeight(dim.fontSize, dim.lineHeightNormal);
        return m_reservedTitleRow;
    }

    // THE PANEL BOX: a content block plus the padding spent at BOTH ends, which is
    // every panel's total height. Its partner panelContentY() answers where that
    // content starts, from the same term -- so a panel's bottom edge and its first
    // row cannot be derived from two different numbers, which is exactly how the
    // title row came apart (StandingsHud reserved five cells and advanced four).
    //
    // WHY A HELPER FOR ONE ADDITION. Not to shorten it -- to name WHICH padding.
    // dim.paddingV is theme-aware (contentPaddingY(), a max of base and borders), and
    // that second term is what pushes content clear of the frame's edge slices. Any
    // other spelling silently opts out of it, which is not hypothetical: PitboardHud
    // spent dim.lineHeightNormal instead. The two are EQUAL unthemed -- a row is two
    // cells and so is the padding -- so it read as a harmless synonym for years, and
    // diverges by 12.67px per side under the shipped themes and 50.69px under Debug,
    // putting its rows inside the frame. check_hud_helpers.sh rule 11 is what stops
    // the next synonym.
    //
    // Panels whose height is NOT composed this way keep computing their own and are
    // annotated: a panel sized from ART (GamepadWidget, from the pad photograph's
    // aspect), from a CONTROL (SettingsButtonWidget), or with its own vertical
    // structure (SettingsHud, whose top is a title band and whose body is section
    // cards -- see titleAdvance()).
    // CEILED TO A CELL, so panels still tile with each other. The terms inside are no
    // longer all integral -- panelContentY() below anchors content to the body card,
    // whose interior sits at a fractional row -- and the remainder is spent at the
    // BOTTOM, inside the card's own border where nothing reads it.
    float panelHeight(const ScaledDimensions& dim, float contentHeight) const {
        return layout().ceilY(dim.paddingV + contentHeight + dim.paddingV);
    }
    // WHERE CONTENT BEGINS, and when a body card is drawn that is the CARD'S INTERIOR
    // rather than the panel's padding.
    //
    // The two used to be separate chains that met nowhere: the row was
    // (paddingV + titleRowHeight) from the panel's top while the card was derived from
    // the panel, and measured across four panels and three frame sizes the gap between
    // them ran from -0.08 to +1.17 cells -- twice NEGATIVE, meaning content drawn on the
    // card's own top border. contentCardSpanY() exists because of that mismatch and
    // three HUDs re-anchored to the card by hand to work around it.
    //
    // One rule instead: a card is a border box, and content starts inside its border.
    // Toggling `[card] hud-content` now moves content by exactly the card's border, on
    // both axes, which is what the horizontal rule already did.
    // ONE INSET, BOTH ENDS -- dim.paddingV, carded or not, and that is what makes content
    // vertically CENTRED in its box instead of merely inside it.
    //
    // THE BUG THIS FIXES, and it was visible rather than theoretical: the top spent
    // frameBorderY + cardBorderY (the card's interior, a raw sum) while panelHeight() spent
    // paddingV at BOTH ends -- and paddingV is that same sum with its shortfall CEILED to a
    // whole cell. So the bottom inset exceeded the top by the ceil remainder and content sat
    // high in its card. At [frame] 2 the gap runs 6.3px at [card] 1, 8.4 at 2, 10.6 at 3 --
    // and then 0.0 at 4, because a card border is 0.833 of a row-cell per unit while the
    // ceil only ever adds whole ones, so they realign every third step. A sawtooth, which is
    // why the amount looked arbitrary and why nobody found the rule by staring at one panel.
    //
    // SAFE BECAUSE paddingV >= frameBorderY + cardBorderY ALWAYS, and it is always a whole
    // number of cells -- both verified across every frame x card combination in 0..6, not
    // argued. So content still starts at or below the card's interior and can never be drawn
    // on the card's own top border, which is the property the carded branch existed for.
    // With both ends equal and on the lattice, panelHeight()'s outer ceil finds nothing left
    // to round for a panel whose content is whole rows.
    //
    // WHAT IT COSTS, because it is a real trade and not a free win: toggling
    // `[card] hud-content` no longer moves content by EXACTLY the card's border. paddingV
    // only changes when the sum crosses a cell boundary, so the flip now moves content by a
    // whole cell or by nothing. That exactness was deliberate -- see the git history of this
    // function -- but it was bought with an asymmetry a user can see, and centred content is
    // worth more than a tidy flip. The additive box model (BOX-MODEL-PORT) gets both.
    float panelContentY(const ScaledDimensions& dim, float panelTop) const {
        return panelTop + dim.paddingV;
    }

    float bigValueRowHeight(const ScaledDimensions& dim) const { return dim.lineHeightNormal; }
    // Y to pass addString for a value drawn in that row, at ANY size: the row is fixed
    // (one normal row) while the glyph cell is taller than it, so this cannot be a
    // single ratio of the font -- it solves for the placement that puts the INK in the
    // middle of the row.
    //
    // Three terms, and each cancels something specific: centre the ink in the row, back
    // off the ink's own inset inside its cell, then undo addString's row centring, which
    // would otherwise compound with this one (the same subtraction TimingHud and GapBar
    // already make).
    //
    // Where the ink sits in the cell is a MEASUREMENT of the font, so it lives with
    // the other one (layout().inkCenterRatio, beside charWidthRatio) and is checked
    // against the shipped atlases by test_font_metrics.cpp rather than against a
    // screenshot.
    //
    // The general form: ink centred in an ARBITRARY box. Timing centres its time in its
    // CARD rather than its row, so it needs the box spelled out.
    float inkCenteredY(float boxTop, float boxHeight, float fontSize) const {
        return boxTop + boxHeight * 0.5f
                      - layout().inkCenterRatio * fontSize
                      - rowCenterOffset(fontSize);
    }
    float bigValueTextY(float rowTop, const ScaledDimensions& dim, float fontSize) const {
        return inkCenteredY(rowTop, bigValueRowHeight(dim), fontSize);
    }
    float bigValueTextY(float rowTop, const ScaledDimensions& dim) const {
        return bigValueTextY(rowTop, dim, dim.fontSizeExtraLarge);
    }

    // ==== THE BOX-MODEL PLAN (BOX-MODEL-PORT) ===============================
    // The additive panel geometry, computed ONCE per rebuild by the engine in
    // core/panel_box.h, and pinned to it by the golden vectors in
    // tests/fixtures/panel_box_parity.json. A migrated HUD builds:
    //
    //     auto dim = getScaledDimensions();
    //     BaseHud::PanelWant want;
    //     want.contentW = <widest row, normalized>;
    //     want.sectionH = { <section heights, normalized> };
    //     want.captionW = <title text width, normalized>;   // caption can win the ask
    //     PanelPlan& p = planPanel(dim, want);
    //     addPlanBackground(p, x, y);       // frame + band + section cards
    //     addPlanTitle(p, "Name", font, color);
    //     // rows at p.contentX(), p.contentY(section) + k * rowH
    //
    // EVERYTHING IS ON THE MODEL EXCEPT FOUR DELIBERATE HOLDOUTS, each annotated
    // at its own box: the corner buttons (Director/SettingsButton — themed BUTTON
    // slices, not panels); the dial gauges (Speedo/Tacho — the box IS the dial
    // art); the shape-driven panels (Map/Radar/Pitboard/Gamepad — track shape,
    // radar circle, board and pad art drive the geometry); and the settings
    // panel's OUTER box (screen-ceiling constrained and content-anchored in X,
    // see the BOX-MODEL NOTE in settings_hud_render.cpp — every term it spends
    // still resolves from the box-model surface). The legacy chain below survives
    // solely as those four's vocabulary; anything else reaching for it is a HUD
    // that has not been migrated.
    //
    // Every derivation the old chain needed (titleRowHeight, panelContentY,
    // contentCardTop, the reserve/rewrite section dance, the caption row's three
    // branches) collapses: geometry is fully known before the first quad, and
    // every box's position is the engine's, not re-derived at the emit site.
    //
    // The plan is in CELLS (x-cells across, y-cells down); X()/Y() convert to
    // normalized units at this HUD's scale. contentPaddingX/Y() and the helpers
    // above remain for unmigrated panels; a HUD uses one path or the other,
    // never both in one rebuild.
    struct PanelWant {
        float contentW = 0.0f;             // widest section row, normalized units
        // INLINE up to 8 sections (see small_vec.h): this struct is built and
        // destroyed once per HUD per rebuild, and a std::vector here cost one
        // heap round-trip -- 1.57us in the game process -- per rebuild.
        SmallVec<float, 8> sectionH;       // per-section content height, normalized
        float captionW = 0.0f;             // caption text width (0 = never wins the ask)
        TitleTier tier = TitleTier::Normal;
        int buttons = 0;                   // footer button count
        float buttonW = 0.0f;              // per-button content width, normalized
        float buttonH = 0.0f;              // button row content height, normalized
        float minPanelW = 0.0f;            // minimum panel width, normalized (0 = none)
        // THE CONTENT IS A SLAB, NOT ROWS -- so the panel's own padding becomes part
        // of it instead of a margin around it: full-bleed to the sides and the
        // bottom, and to the top too when no title is shown. A shown title keeps
        // the top padding as its own air (the caption is rows, not slab -- flush
        // against the panel's top edge it read as a defect, and was reported as
        // one). UNTHEMED ONLY: with a theme the frame art needs that ring, and
        // the padding is the theme's to spend.
        //
        // The panel does NOT change size. The engine moves the padding into the
        // content band rather than dropping it, so the outer rect is identical with
        // the flag on or off -- which is the whole reason this is a flag the engine
        // honours and not two edits in the caller that could drift apart.
        //
        // Why only some panels: the Gap Bar's coloured fill and the Notices slab ARE
        // the panel; a cell of air around them reads as a border nobody asked for.
        // A panel of text rows wants that air, which is why this is opt-in.
        //
        // Applies to the sectionH path only -- the vertical share lands on the LAST
        // section, the one that already absorbs the panel's ceil remainder. A `bands`
        // caller is left alone (no current one is a slab).
        bool contentFillsPanel = false;
        // THE BODY AS COLUMNS, for a panel whose body is a horizontal split. Same
        // shape as PanelBox::BandAsk, in NORMALIZED units like every field above --
        // the engine has carried columns since the box model landed (panel_box.h
        // names the settings panel's sidebar as the reason), and this is the plan
        // layer catching up so a caller can reach them.
        //
        // Set `bands` OR contentW/sectionH, never both: bands wins, exactly as
        // PanelBox::Spec resolves the same pair.
        struct ColumnWant {
            float contentW = 0.0f;         // this column's content width, normalized
            // Plain vector, unlike PanelWant::sectionH above: only the settings
            // panel states columns, it rebuilds only while open, and its lists are
            // long enough that inline storage would spill anyway.
            std::vector<float> sectionH;   // per-section content height, normalized
        };
        struct BandWant { std::vector<ColumnWant> columns; };
        std::vector<BandWant> bands;
        // A floor under the body, normalized (0 = none) -- see Spec::minBodyH.
        float minBodyH = 0.0f;
    };
    struct PanelPlan {
        PanelBox::Geom g;                  // the engine's geometry, in cells
        float cellW = 0.0f, cellH = 0.0f;  // normalized units per cell (scaled)
        float x0 = 0.0f, y0 = 0.0f;        // panel origin, PRE-offset normalized
        float capFontSize = 0.0f;          // the tier's caption size, normalized
        float X(double cells) const { return x0 + static_cast<float>(cells) * cellW; }
        float Y(double cells) const { return y0 + static_cast<float>(cells) * cellH; }
        float W(double cells) const { return static_cast<float>(cells) * cellW; }
        float H(double cells) const { return static_cast<float>(cells) * cellH; }
        float width() const { return W(g.panelCols); }
        float height() const { return H(g.panelH); }
        // A section's content origin — where its first row starts, both axes.
        float contentX() const { return X(g.rowsX); }
        float contentY(size_t section = 0) const {
            return Y(g.sections[section < g.sections.size() ? section : 0].rowsTop);
        }
        float contentW() const { return W(g.cols); }
        // The content box's RIGHT edge -- where a right-aligned value ends. Three
        // panels derived this by mirroring the LEFT inset onto the right edge
        // (`panelLeft + width - (contentX - panelLeft)`), which is the same number
        // only while [content] border and padding are horizontally symmetric. Write
        // `border = 2 0 4 6` and the mirror pulls right-aligned values a whole left
        // border inward, into the labels beside them -- the Fuel widget's value over
        // its own label, reported from exactly that theme.
        float contentRight() const { return contentX() + contentW(); }
        // A section's DRAWN BOX -- the card as the player sees it, border included,
        // which is NOT the content band above when [content] border is asymmetric:
        // the band is inset by border.t at the top and border.b at the bottom, so the
        // two share a centre only while those are equal.
        //
        // Centre a single big value in THIS, not in the content band. Every shipped
        // theme has a symmetric card border, so the two agree and this changes
        // nothing; write `border = 2 0 4 6` and the value drawn from the band sits a
        // cell above the middle of the card it is drawn on. Timing centred in the box
        // and looked right; the Version widget, the Gap Bar, Notices and the Bars
        // widget centred in the band and did not.
        //
        // Rows still start at contentY(): a LIST belongs inside the border, and only
        // a lone value centred in its card has a reason to ask where the card is.
        float sectionBoxY(size_t section = 0) const {
            return Y(g.sections[section < g.sections.size() ? section : 0].top);
        }
        float sectionBoxH(size_t section = 0) const {
            const PanelBox::SectionGeom& s =
                g.sections[section < g.sections.size() ? section : 0];
            return H(s.bot - s.top);
        }
        // The horizontal half of the same box: the card's drawn extent along X
        // (g.cardLeft/cardW -- one column, shared by every section). CENTRED
        // content anchors HERE, never at the panel's centre: the two are the same
        // number only while the [content] terms are left/right symmetric, and a
        // skinner's `margin = 4 6 8 0` moved the panel's centre outside the card
        // -- every big value, gauge and chip that centred on `startX +
        // backgroundWidth / 2` slid off its own card while the card stayed put.
        // ALIGNED content keeps contentX()/contentRight(): a column respects the
        // card's border and padding; only centring answers to the drawn box.
        float sectionBoxX() const { return X(g.cardLeft); }
        float sectionBoxW() const { return W(g.cardW); }
        float sectionBoxCenterX() const { return sectionBoxX() + sectionBoxW() / 2.0f; }
        // A COLUMN of a split body, by band and column index. The one-column
        // accessors above are `col(0, 0)` with the leftover-width rule applied,
        // so a caller that has a split reads its columns the same way.
        const PanelBox::ColumnGeom& col(size_t band, size_t column) const {
            static const PanelBox::ColumnGeom kNone;
            if (band >= g.bands.size()) return kNone;
            const PanelBox::BandGeom& b = g.bands[band];
            return column < b.columns.size() ? b.columns[column] : kNone;
        }
        // A column section's content origin and width -- the engine's own row box
        // for that column, which for one column IS g.rowsX / g.cols.
        float colContentX(const PanelBox::ColumnGeom& c) const { return X(c.rowsLeft); }
        float colContentW(const PanelBox::ColumnGeom& c) const { return W(c.rowsW); }
        float colContentY(const PanelBox::ColumnGeom& c, size_t section = 0) const {
            if (c.sections.empty()) return Y(g.panelInner);
            return Y(c.sections[section < c.sections.size() ? section : 0].rowsTop);
        }

        // THE BAND A ROW HIGHLIGHT SPANS: the ROWS box, the same box the text sits
        // in. At the shipped default ([content] padding 0) that IS the card's
        // interior, so a band reads flush to the card; where a theme asks for
        // padding, the padding is air around the rows and the highlight is one of
        // the things it is air around.
        //
        // NOT THE CARD'S INTERIOR, which was tried and reverted: it makes the band
        // absorb [content] padding, i.e. "the highlight grows outside the content of
        // the card way beyond where the text is" -- the reported bug that
        // standings_row_band_test exists to pin. That test asserts the card-to-band
        // clearance GROWS with the padding, which only the rows box does.
        //
        // Four emitters spanned three different boxes before this owner existed:
        // StandingsHud, RecordsHud and the settings SIDEBAR took the rows box, while
        // the settings panel's content rows took the card interior (a hand-rolled
        // planCardRightX - cardBorderX(), with a second spelling for the unthemed
        // case) -- so one panel highlighted two ways at once, a column apart. The
        // rows box is what the other three already agreed on.
        float rowBandX(const PanelBox::ColumnGeom& c) const { return X(c.rowsLeft); }
        float rowBandW(const PanelBox::ColumnGeom& c) const { return W(c.rowsW); }
        // The one-column form, for a panel with no split body.
        float rowBandX() const { return X(g.rowsX); }
        float rowBandW() const { return W(g.cols); }
    };
    // Geometry only — no quads, no state; safe to call while merely measuring.
    //
    // MEMOISED against the last call's inputs. A plan is a pure function of the
    // ask, the scaled metrics and the theme, and for most panels none of those
    // move between frames -- SpeedWidget asks for the same box every frame and
    // only the NUMBER inside it changes. Measured in-game it was 5.8us per
    // invocation and 53% of all rebuild time, on panels emitting as little as one
    // quad and three strings.
    //
    // The key is the whole input set (want + dim + theme generation) rather than a
    // hand-picked subset: dim carries scale AND the layout metrics, so an
    // [Advanced] retune invalidates without needing its own counter, and the
    // generation covers discovery, a config reload and a change of theme.
    // THE CENTRE-STACK WIDTH CONTRACT, the other half of centerAnchoredPanelLeft above.
    //
    // A member states the stack width as its panel MINIMUM and states NO content
    // width of its own: the minimum then owns the width, so every member lands on
    // it whatever it happens to draw. State BOTH and the wider wins -- which is
    // how VersionWidget came to outgrow its neighbours by 78px the moment
    // [Advanced] padding grew, while the three that leave contentW at 0 sat on
    // the shared minimum. The same trap gap_bar_hud.cpp documents at its own
    // minPanelW ("setting both would make the wider of the two win").
    //
    // A panel that must fit content the stack width cannot hold is not a stack
    // member: VersionWidget's update popup sizes to its message and button row
    // through the ordinary PanelWant fields, and is deliberately wider.
    //
    // The dim overload computes the shared width; the float one takes it, for the
    // Gap Bar, whose user width setting scales the panel.
    void wantCenterStackWidth(PanelWant& want, const ScaledDimensions& dim) const;
    void wantCenterStackWidth(PanelWant& want, float panelW) const;

    PanelPlan& planPanel(const ScaledDimensions& dim, const PanelWant& want) const;

    // TEST ONLY (test_hooks.cpp): the memoized plan as last stamped by
    // addPlanBackground -- the card-anchoring sweep measures every drawn string
    // and quad against its card rect. Null while no plan has been computed.
    // Only meaningful for HUDs that bind planPanel() BY REFERENCE (all but
    // VersionWidget, which copies and places a local).
    const PanelPlan* testPlacedPlan() const {
        return m_planCacheValid ? &m_planCachePlan : nullptr;
    }
    // Background + chrome in one call: the frame (via addBackgroundQuad, so the
    // fill-strip and drag fast-path records are the same as ever), then the
    // title band and one card PER SECTION at the plan's coordinates. Call before
    // any content — cards must sit behind the rows (quads draw in order).
    void addPlanBackground(PanelPlan& p, float x, float y);
    // The caption glyph at the plan's column. No-op when the title is off.
    void addPlanTitle(const PanelPlan& p, const char* text, int fontIndex,
                      unsigned long color);
    // What PanelWant::captionW should carry: the caption text's width plus the
    // identity icon's advance when one will draw — so a long title widens its
    // panel (the widest-ask rule) instead of overhanging it.
    float planTitleWidth(const ScaledDimensions& dim, const char* text,
                         TitleTier tier = TitleTier::Normal) const;
    // The y addPlanTitle hands addString for the caption — band-centred with the
    // row-centring compensation, or the bare row top. Exposed for layout fast
    // paths that repositionString the caption without re-emitting it.
    float planTitleY(const PanelPlan& p) const;
    // (planStandardPanel lived here: a four-field shorthand for "chars across, one
    // section down, caption on top". It was written to replicate what
    // calculateBackgroundWidth/Height composed on the LEGACY chain, and it outlived
    // that chain's retirement by about an hour. Seven panels used it and twenty-seven
    // did not -- sixteen of those for real reasons (a gauge's width is geometric, not
    // a character count; several stack sections or carry button rows), so the
    // shorthand served a minority shape while making "which entry point?" a question
    // at every new HUD. Its callers now state the four fields, which is what the
    // other twenty-seven already did and reads better besides: named fields instead
    // of four positional arguments. One way to plan a panel.)
    // The resolved [button] box terms at this HUD's scale, for a panel laying
    // out its own NON-UNIFORM button row (the settings footer: differing
    // labels, so PanelWant's uniform buttons don't fit). insetL/R are the
    // border+padding a button's content sits inside; gap is the seam two
    // sibling buttons keep — the SUM of the facing margins, the model's rule.
    // The [button] box terms resolved to pixels for a HUD laying its own button
    // row: insets are border+padding per side (the box around the label), gap is
    // the SUM of facing margins, marginT/B the row's own vertical margins. The
    // vertical half was missing — buttons drew as one bare text row however the
    // terms were set, and no panel grew with them.
    // The [panel] junction gap, resolved to pixels and drawn square on screen --
    // the seam the box model spends between a panel's stacked children (band->card,
    // card->card, card->buttons; layout_metrics.h names all three).
    //
    // A JUNCTION BELONGS TO THE STACK, A MARGIN BELONGS TO THE BOX, which is why
    // this is not folded into planButtonTerms().marginT: that is the button box's
    // own margin and defaults to zero, and a HUD laying out its own button row owes
    // the junction above it exactly as the settings tabs do with addSpacing().
    // Omitting it is what has a hand-laid button row sitting flush against the text
    // above it while a PLANNED row (PanelWant::buttons, where panel_box.h spends
    // `y += gapY` itself) keeps its air.
    //
    // Exists because the conversion was spelled out by hand in the one place that
    // needed it (titleBandSlackY, just below), so the second caller would have been
    // a second spelling of the same three terms.
    float panelGapY(const ScaledDimensions& dim) const {
        return panelGapCells() * dim.cellW * PluginConstants::UI_ASPECT_RATIO;
    }
    struct PlanButtonTerms { float insetL, insetR, insetT, insetB, gap, marginT, marginB; };
    PlanButtonTerms planButtonTerms(const ScaledDimensions& dim) const;

    // The BODY height this want would lay out to -- the bands only, without the
    // title band or the button row. Answers "how tall would this content be" without
    // committing to it, and without the caller re-deriving the stacking.
    //
    // Exists for the settings panel, which must size itself to its TALLEST tab and
    // therefore has to price a layout it is not going to draw. The alternative -- and
    // what was there before -- was to compare content FLOW (a cursor's end position),
    // which is a different quantity: a column's body is sum(section heights) plus,
    // per section, the card's margin/border/padding, plus a seam between each pair.
    // Two of those three scale with the SECTION COUNT and not with the cursor, so the
    // floor was systematically short for tabs with many sections and the panel
    // changed height as you switched tabs.
    //
    // Deliberately NOT by exposing resolvePanelSpec: that is documented as the only
    // reader of the theme's box terms, and a caller summing those terms itself is the
    // second-spelling failure this panel's history is made of.
    float planBodyHeight(const ScaledDimensions& dim, const PanelWant& want) const;
private:
    // The eleven theme terms + switches for THIS panel, resolved: a set box key
    // wins, else the legacy scalar/sentinel chain (frameBorder, cardBorder,
    // titleBorder(), buttonBorder, panelPadding*Override, sectionGap). The ONLY
    // reader of ThemeAsset's box terms.
    PanelBox::Spec resolvePanelSpec(const ScaledDimensions& dim,
                                    const PanelWant& want) const;
    // ==== end box-model plan ================================
protected:
    ScaledDimensions getScaledDimensions() const;

    // A DEFAULT POSITION, stated in grid cells.
    //
    // Every shipped default was a decimal -- setPosition(0.0055f, 0.30507f) -- and
    // every one of them was a whole number of cells wearing a decimal costume: 0.0055
    // is one cellW, 0.30507 is twenty-six cellH, 0.73150 is a hundred and thirty-three
    // cellW. 33 of the 35 shipped defaults were exact.
    //
    // That is the grid written down twice, and the second copy cannot follow the
    // first: uiFontSize moves the lattice, and a frozen decimal stays where it was, so
    // a user who resizes the UI gets defaults that no longer land on the grid every
    // panel snaps to. Stating them in cells makes them follow, and makes the number
    // readable -- "one cell in, twenty-six down" instead of 0.30507f.
    //
    // layoutDefaults(), not layout(): a default position is not a themed quantity, and
    // these are called from resetToDefaults() where there is no theme context anyway.
    static float cellsX(float cells) { return cells * layoutDefaults().cellW; }
    static float cellsY(float cells) { return cells * layoutDefaults().cellH; }

    // Vertical offset to center Small-size text within a normal-height row band.
    static float labelRowYOffset(const ScaledDimensions& dim) {
        return (dim.lineHeightNormal - dim.lineHeightSmall) * 0.5f;
    }
    // Render a header/row label: Small font size, vertically centered in the row at rowY.
    // Keeps labels visually distinct from the full-size data values beside them.
    void addLabel(const char* text, float x, float rowY, int justify, int fontIndex,
                  unsigned long color, const ScaledDimensions& dim) {
        addString(text, x, rowY + labelRowYOffset(dim), justify, fontIndex, color, dim.fontSizeSmall);
    }

    // ========================================================================
    // Section headings
    // ========================================================================
    // A SECTION HEADING opens one block of a panel's content: the session line above
    // the standings table, "Frame Rate" and "CPU Time" on the performance graphs,
    // each chart's name on Session Charts, the server line on Session.
    //
    // THE SAME CONCEPT the settings panel calls a section heading, and deliberately
    // the same NAME (SettingsLayoutContext::addSectionHeading). They were briefly
    // split -- "heading" in the settings panel, "subtitle" in a HUD -- and the split
    // did not survive being measured: same size, same colour, same one-row advance,
    // same job of opening a card and sitting inside it. The only difference is the
    // FACE, and that is a property of the surface rather than of the idea: the
    // settings panel's body is monospace so its heading is the STRONG (bold mono)
    // face, a HUD's identity is the TITLE face so its heading is that. Two renderings
    // of one thing do not need two words.
    //
    // The vocabulary, borrowed from CSS along with the ini's shape: the panel's title
    // band is the h1, a section heading is the h2, addLabel() is the small text that
    // captions a column or an axis. "Subtitle" would have meant a second title under
    // the first, which is not what any of these are.
    //
    // ONE STYLE, defined here rather than at five call sites. Four HUDs each spelled
    // it out and three agreed; SessionHud drew its server line at fontSizeExtraLarge
    // -- an h1's size -- and since that HUD also shipped with its real title off, the
    // data WAS the title.
    //
    // A heading belongs INSIDE the card it opens. See beginContentSection().
    void addSectionHeading(const char* text, float x, float y, const ScaledDimensions& dim,
                           int justify = PluginConstants::Justify::LEFT) {
        addString(text, x, y, justify, getFont(FontCategory::TITLE),
                  getColor(ColorSlot::PRIMARY), dim.fontSize);
    }
    // Row height a section heading occupies. Its own function so a HUD reserving space
    // and a HUD advancing past one cannot disagree.
    static float sectionHeadingRowHeight(const ScaledDimensions& dim) { return dim.lineHeightNormal; }

    // ========================================================================
    // History Strip Charts (Telemetry / Performance / Rumble HUDs)
    // ========================================================================
    // Shared styling for the scrolling history graphs, so the graph HUDs stay
    // visually identical. Thicknesses are in normalized units at 100% HUD scale
    // (multiply by getScale(); the inline helpers below do it).
    static constexpr float STRIP_CHART_GRID_THICKNESS = 0.001f;  // ~1px at 1080p, subtle grid lines
    static constexpr float STRIP_CHART_LINE_THICKNESS = 0.002f;  // history trace thickness
    static constexpr float STRIP_CHART_LABEL_INSET = 0.2f;       // axis-label inset, in paddingH units

    float stripChartGridThickness() const { return STRIP_CHART_GRID_THICKNESS * getScale(); }
    float stripChartLineThickness() const { return STRIP_CHART_LINE_THICKNESS * getScale(); }

    // The shared strip-chart "frame": grid lines at 100%/50%/0% of the value range
    // (drawn first so the data traces render on top) plus the matching axis-label
    // triple down the left inside edge (SMALL font, TERTIARY color, top/middle/
    // bottom). Label text stays at the call site (snprintf into a stack buffer for
    // dynamic ranges, string literals for the fixed 0-100% charts).
    void addStripChartFrame(float x, float y, float width, float height,
                            const char* topLabel, const char* midLabel, const char* botLabel,
                            const ScaledDimensions& dims);

    // One scrolling history trace: a polyline over a deque of 0..1 samples, with
    // the newest sample pinned to the right edge (the graph scrolls left as the
    // deque fills). Segments where both endpoints are near zero are skipped so an
    // idle channel costs nothing. maxHistory fixes the point spacing so the graph
    // width is constant regardless of how full the deque is.
    // One strip-chart polyline. Templated on the container so telemetry's and
    // rumble's histories (different capacities, same shape) share one emitter --
    // it took a std::deque<float>&, which is the storage that made those two the
    // most expensive panels in the plugin. See core/history_ring.h.
    template <typename Hist>
    void addStripChartHistoryLine(const Hist& history, unsigned long color,
                                  float x, float y, float width, float height,
                                  float lineThickness, size_t maxHistory) {
        if (history.size() < 2) return;
        const float pointSpacing = width / (maxHistory - 1);
        // Offset so the newest sample is always at the right edge.
        const size_t offset = maxHistory - history.size();
        for (size_t i = 0; i < history.size() - 1; ++i) {
            const float value1 = std::max(0.0f, std::min(1.0f, history[i]));
            const float value2 = std::max(0.0f, std::min(1.0f, history[i + 1]));
            if (value1 < 0.01f && value2 < 0.01f) continue;   // both flat: nothing to draw
            const float x1 = x + (offset + i) * pointSpacing;
            const float x2 = x + (offset + i + 1) * pointSpacing;
            const float y1 = y + height - (value1 * height);
            const float y2 = y + height - (value2 * height);
            addLineSegment(x1, y1, x2, y2, color, lineThickness);
        }
    }

    // Helper method to calculate text color with opacity (eliminates duplication in widgets)
    unsigned long getTextColorWithOpacity(uint8_t r = 255, uint8_t g = 255, uint8_t b = 255) const;

    // Helper methods to calculate background dimensions consistently (eliminates duplication in HUDs)
    float calculateBackgroundWidth(int charWidth) const;
    float calculateBackgroundHeight(int rowCount, bool includeTitle = true) const;

    // Helper method to position a string at (x, y) with offset applied (eliminates duplication in widget layouts)
    // Returns true if string was positioned, false if stringIndex >= m_strings.size()
    bool positionString(size_t stringIndex, float x, float y);

    // Title icon helpers. finalizeTitleIcon() places the icon quad to the left of the
    // (un-advanced) title at base and shifts the title text right past it; positionTitleIcon()
    // re-derives that from the title string's current position after a layout-only rebuild.
    // Both are no-ops when there is no title icon.
    void finalizeTitleIcon(float baseX, float baseY);
    void positionTitleIcon();

    // Helper for click detection - checks if point (x,y) is inside rectangle
    // Shared by StandingsHud, RecordsHud, MapHud for click region testing
    static bool isPointInRect(float x, float y, float rectX, float rectY, float width, float height) {
        return x >= rectX && x <= rectX + width && y >= rectY && y <= rectY + height;
    }

    // ========================================================================
    // Per-HUD Color/Font Overrides (ini-only, power user feature)
    // ========================================================================
    // If set, these override the global ColorConfig/FontConfig values for this HUD.
    // Usage: getColor(ColorSlot::PRIMARY) instead of ColorConfig::getInstance().getPrimary()
    // Usage: getFont(FontCategory::NORMAL) instead of Fonts::getNormal()
    unsigned long getColor(ColorSlot slot) const;
    int getFont(FontCategory category) const;

    // The legible glyph colour to draw ON a coloured chip (a button, a badge).
    //
    // Picks the palette's LIGHT or DARK end by the chip's own luma, rather than
    // naming a slot -- because which slot is "the light one" is not fixed. On a
    // dark theme it is PRIMARY and BACKGROUND is near-black; on a light one those
    // are the other way round, so any hardcoded slot is wrong for half the themes.
    //
    // Replaces "accent on accent", which the two chip widgets both used. That only
    // ever worked because every theme was dark AND the chip is half-alpha: a
    // full-strength accent glyph read against its own dimmed wash. On the light
    // a light theme the same pair is navy on navy over a dark track, which is
    // invisible -- and on the dark themes this is an improvement anyway, since
    // white on purple beats purple on purple.
    unsigned long chipGlyphColor(unsigned long chip) const {
        const unsigned long bg = getColor(ColorSlot::BACKGROUND);
        const unsigned long fg = getColor(ColorSlot::PRIMARY);
        const bool bgIsDark = PluginUtils::isColorDark(bg);
        return PluginUtils::isColorDark(chip) ? (bgIsDark ? fg : bg)
                                              : (bgIsDark ? bg : fg);
    }

public:
    // Per-HUD override setters (used by SettingsManager during INI load/save)
    void setColorOverride(ColorSlot slot, unsigned long color);
    void clearColorOverride(ColorSlot slot);
    bool hasColorOverride(ColorSlot slot) const;
    unsigned long getColorOverrideValue(ColorSlot slot) const;  // Raw override value (for save)

    void setFontOverride(FontCategory category, const std::string& fontName);
    void clearFontOverride(FontCategory category);
    bool hasFontOverride(FontCategory category) const;
    const std::string& getFontOverrideName(FontCategory category) const;  // Raw font name (for save)

    // Per-HUD drop-shadow override (ini-only). Empty = inherit the global [Display] dropShadow;
    // set = force on/off for this HUD only (gates both text and icon-sprite shadows).
    void setDropShadowOverride(bool enabled) { m_dropShadowOverride = enabled; setDataDirty(); }
    void clearDropShadowOverride() { m_dropShadowOverride.reset(); setDataDirty(); }
    bool hasDropShadowOverride() const { return m_dropShadowOverride.has_value(); }
    bool getDropShadowOverrideValue() const { return m_dropShadowOverride.value_or(false); }  // raw value (for save)
    // A panel that NEVER wants drop shadows, whatever the user set.
    //
    // A POLICY, not a preference, and that distinction is why this is a virtual
    // rather than a setDropShadowOverride() call in a constructor. The override is a
    // stored user setting: it is captured into the INI and, being sparse-saved,
    // cleared on the next load by the authoritative apply when the file does not
    // mention it (see settings_serde.h). A panel setting it on itself would lose the
    // behaviour the first time settings round-tripped.
    //
    // The shadow exists to keep text legible against the GAME, where there is no
    // panel behind it. A surface that always draws its own opaque background does not
    // need it, and pays a full second string for every string it draws.
    virtual bool alwaysSkipDropShadow() const { return false; }

    // Centre this panel's caption in its title band instead of starting it at the
    // caption column. Off for every HUD, on for the settings menu.
    //
    // A HUD's caption is a LABEL on a small panel that sits in a corner, and labels
    // read left. The settings menu is a page: it is centred on screen, it is the only
    // thing you are looking at, and its heading is a heading. Same reason its title
    // uses the Large tier when nothing else does.
    //
    // Applies only to a caption WITHOUT a title icon, which is every caller there
    // will be: an icon is an identity marker for a panel you pick out of a cluttered
    // screen, the opposite need from a centred page heading -- and the settings menu
    // has no icon. With one, the caption stays left and addPlanTitle says so.
    virtual bool centreTitle() const { return false; }

    // Effective drop-shadow flag given the global default (policy beats override
    // beats the global default).
    bool getEffectiveDropShadow(bool globalDefault) const {
        if (alwaysSkipDropShadow()) return false;
        return m_dropShadowOverride.value_or(globalDefault);
    }

    std::vector<SPluginQuad_t> m_quads;
    std::vector<SPluginString_t> m_strings;
    std::vector<bool> m_stringSkipShadow;  // Parallel to m_strings: true = skip drop shadow for this string
    std::vector<HudStringConfig> m_styledStringConfigs;  // Storage for styled string configurations
    float m_fScale;

    // Title icon tracking (set by addPlanTitle). The icon is a quad whose position is
    // derived from the title string, so the layout fast path (rebuildLayout) can keep it
    // in sync during drag/scale without each HUD knowing about it. -1 = no title icon.
    int m_titleStringIndex = -1;       // index into m_strings of the title text
    // What reservedTitleHeight() last returned; see it.
    mutable float m_reservedTitleRow = 0.0f;
    // Per-HUD panel-theme override; see setThemeOverride(). Empty = follow the
    // global Appearance theme, which is what every HUD ships as.
    std::string m_themeOverride;
    int m_titleIconQuadIndex = -1;     // index into m_quads of the title icon
    // Span of m_quads written by the last addBackgroundQuad: 1 quad normally, 9
    // when a panel theme is active. updateBackgroundQuadPosition rewrites exactly
    // this span (it previously hardcoded m_quads[0], which a themed panel breaks).
    int m_bgQuadFirst = -1;
    int m_bgQuadCount = 0;
    // The panel rect that span covers, offset already applied. Recorded so the
    // title band (emitted later, from the plan) can span the same width
    // without every HUD having to pass its panel rect twice.
    float m_bgRectX = 0.0f, m_bgRectY = 0.0f, m_bgRectW = 0.0f, m_bgRectH = 0.0f;
    bool m_bgRectValid = false;

    // The section card in flight: where its nine reserved slices start, its left
    // edge, width and top. -1 when no section is open.
    int m_sectionCardIndex = -1;
    float m_sectionX = 0.0f, m_sectionW = 0.0f, m_sectionTop = 0.0f;
    int m_sectionCount = 0;              // sections opened this rebuild
    int m_lastSectionIndex = -1;         // the card finishContentSections() stretches
    float m_lastSectionTop = 0.0f;
    std::vector<SectionCardSpan> m_sectionCards;   // see sectionCardSpans()
    // Where a single whole-body card WOULD have sat, recorded even when the HUD draws
    // sections instead -- see emitContentCard(). Pre-offset.
    float m_bodyCardTop = 0.0f, m_bodyCardBottom = 0.0f;
    bool m_bodyCardValid = false;
    // activeTheme()'s memo. Generation 0 means "never resolved / invalidated".
    mutable const ThemeAsset* m_themeCache = nullptr;
    // The plan memo — see planPanel(). x0/y0 are NOT part of the key: they are the
    // panel's on-screen origin, stamped by addPlanBackground long after the plan is
    // computed, so a hit hands back a plan with them cleared exactly as a fresh one
    // would (MapHud reads contentX() before addPlanBackground and relies on that).
    mutable bool m_planCacheValid = false;
    mutable unsigned int m_planCacheGen = 0;
    mutable ScaledDimensions m_planCacheDim{};
    mutable PanelWant m_planCacheWant{};
    mutable PanelPlan m_planCachePlan{};
    mutable unsigned int m_themeCacheGen = 0;
    float m_titleIconSize = 0.0f;      // icon draw height (a fraction of the title font)
    float m_titleFontSize = 0.0f;      // title font size; icon is vertically centred on this

    // Visibility and display options (protected so derived classes can access)
    // Atomic because background workers legitimately poke these (RecordsHud's
    // fetch thread marks HUDs dirty on completion; update-checker callbacks
    // call VersionWidget::showUpdateNotification) while the game thread reads
    // them every frame. All access is plain load/store - no RMW needed.
    std::atomic<bool> m_bVisible;
    bool m_bShowTitle;
    float m_fBackgroundOpacity;  // 0.0 (fully transparent) to 1.0 (fully opaque)
    float m_fMinBackgroundOpacity = 0.0f;  // Lower bound for the opacity slider (most HUDs allow full transparency; raised for widgets that always render foreground content, e.g. the settings button)
    bool m_bShowBackgroundTexture;  // If true and texture exists, render sprite background
    int m_iBackgroundTextureIndex;  // 1-based sprite index (0 = no texture)

    // Registration name; see setHarnessId. A static literal owned by the
    // registration site, so this stores the pointer rather than a copy. The
    // placeholder only survives an element built outside HudManager (tests that
    // construct a HUD directly) -- registerHud always overwrites it.
    const char* m_szHarnessId = "unregistered";

    // Dynamic texture support
    std::string m_textureBaseName;  // Base texture name (e.g., "standings_hud")
    int m_textureVariant = 0;       // Selected variant: 0 = Off, 1+ = variant number

    // Position and bounds (protected so derived classes can access for advanced positioning)
    float m_fOffsetX, m_fOffsetY;
    float m_fBoundsLeft, m_fBoundsTop, m_fBoundsRight, m_fBoundsBottom;

    // Companion-surface instance (see the accessors above). Independent on/off +
    // position, mirroring the game while m_bCompanionConfigured is false.
    bool m_bCompanionConfigured = false;
    std::atomic<bool> m_bCompanionVisible{ true };
    float m_fCompanionOffsetX = 0.0f, m_fCompanionOffsetY = 0.0f;
    // Snapshot the current game state into the companion instance on first divergence.
    void ensureCompanionConfigured() {
        if (!m_bCompanionConfigured) {
            m_bCompanionVisible.store(m_bVisible.load());
            m_fCompanionOffsetX = m_fOffsetX; m_fCompanionOffsetY = m_fOffsetY;
            m_bCompanionConfigured = true;
        }
    }

    // Frequent update timing (for live timing displays)
    std::chrono::steady_clock::time_point m_lastTickUpdate;

    // Temporary reveal window (see reveal()/isRevealed()). Default-constructed to the
    // clock epoch, so isRevealed() is false until the first reveal().
    std::chrono::steady_clock::time_point m_revealUntil;

    // Benchmark profiling index (registered in BenchmarkMetrics, -1 = not registered)
    int m_benchmarkIndex;

private:
    // Atomic for the same cross-thread reason as m_bVisible (see comment
    // there). Note setDataDirty() writes BOTH flags, so the background
    // callers that mark HUDs dirty reach m_bLayoutDirty too.
    std::atomic<bool> m_bDataDirty;
    std::atomic<bool> m_bLayoutDirty;

    bool m_bDraggable;
    bool m_bDragging;
    bool m_bDragCompanion = false;   // the surface this drag edits (companion vs game)
    float m_fDragStartX, m_fDragStartY;
    float m_fInitialOffsetX, m_fInitialOffsetY;

    // Per-HUD color/font overrides (ini-only, power user feature)
    // When set, these override the corresponding global ColorConfig/FontConfig values
    std::array<std::optional<unsigned long>, static_cast<size_t>(ColorSlot::COUNT)> m_colorOverrides;

    struct FontOverride {
        std::string name;       // Font filename (for INI save/load)
        int resolvedIndex;      // Cached font index (0 = not resolved or not found)
    };
    std::array<std::optional<FontOverride>, static_cast<size_t>(FontCategory::COUNT)> m_fontOverrides;

    // Per-HUD drop-shadow override (ini-only). Empty = inherit the global setting.
    std::optional<bool> m_dropShadowOverride;
};
