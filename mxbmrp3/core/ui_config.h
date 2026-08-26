// ============================================================================
// core/ui_config.h
// User-configurable UI behavior settings (grid snapping, screen clamping, etc.)
// ============================================================================
#pragma once

// THEME GENERATION -- bumped whenever anything a theme lookup depends on changes:
// discovery, a config reload, or the selected theme name. BaseHud memoises its
// resolved ThemeAsset* against this, because getThemeByName() is a linear string
// scan and one rebuild asks for it dozens of times.
//
// A free inline counter rather than a member of AssetManager, and that is not
// stylistic: ui_config.cpp is compiled into the UNIT suite and asset_manager.cpp is
// not (it is Windows-only), so a setter reaching into AssetManager fails to link
// there. An inline global has no TU to miss.
inline unsigned int& mxbThemeGenerationRef() {
    static unsigned int gen = 1;   // 0 is BaseHud's never-resolved sentinel
    return gen;
}
inline unsigned int mxbThemeGeneration() { return mxbThemeGenerationRef(); }
inline void mxbBumpThemeGeneration() { ++mxbThemeGenerationRef(); }


#include <algorithm>   // std::clamp (render-probe bounds)
#include <atomic>
#include <cstdint>
#include <cmath>
#include <string>

// Temperature unit options (used by SessionHud weather display)
enum class TemperatureUnit : uint8_t {
    CELSIUS = 0,
    FAHRENHEIT = 1
};

// Personal best scope (per-bike or per-category)
enum class PBScope : uint8_t {
    BIKE = 0,
    CATEGORY = 1
};

// Where the HUD is drawn: in the game, in the standalone companion window, or both.
// Companion = window only (the in-game HUD is hidden, except while the settings menu
// is open so you can always change this back).
enum class DisplayTarget : uint8_t {
    IN_GAME = 0,
    COMPANION = 1,
    BOTH = 2
};

class UiConfig {
public:
    static UiConfig& getInstance();

    // Grid snapping setting (for HUD positioning)
    bool getGridSnapping() const { return m_bGridSnapping; }
    void setGridSnapping(bool enabled) { m_bGridSnapping = enabled; }

    // Active 9-slice panel theme, by AssetManager theme name. Empty = no theme
    // (HUD backgrounds stay flat solid quads, the pre-theme behaviour and the
    // default). Stored as a NAME rather than an index so adding, removing or
    // reordering theme folders cannot silently repoint a saved setting at a
    // different theme -- and so an unknown name degrades to "no theme" instead of
    // to an arbitrary one.
    const std::string& getThemeName() const { return m_themeName; }
    // Bumps the theme generation so every HUD's memoised ThemeAsset* is refreshed.
    void setThemeName(const std::string& name);

    // Screen clamping setting (keeps HUDs within screen bounds when dragging)
    bool getScreenClamping() const { return m_bScreenClamping; }
    void setScreenClamping(bool enabled) { m_bScreenClamping = enabled; }

    // Menu-only cursor: when enabled, the mouse cursor + settings button only
    // appear while the settings menu is open (toggled via the TOGGLE_SETTINGS
    // hotkey). Movement no longer summons the cursor. Intended for controller
    // users whose analog stick registers as mouse movement, which would
    // otherwise pop the cursor in every time they steer.
    bool getMenuOnlyCursor() const { return m_bMenuOnlyCursor; }
    void setMenuOnlyCursor(bool enabled) { m_bMenuOnlyCursor = enabled; }

    // Auto-save setting (automatically save settings on every change)
    bool getAutoSave() const { return m_bAutoSave; }
    void setAutoSave(bool enabled) { m_bAutoSave = enabled; }

    // Temperature unit setting (used by SessionHud weather display)
    TemperatureUnit getTemperatureUnit() const { return m_temperatureUnit; }
    void setTemperatureUnit(TemperatureUnit unit) { m_temperatureUnit = unit; }

    // Hold-to-repeat max speed (ms between repeats at full acceleration)
    int getHoldRepeatFastMs() const { return m_holdRepeatFastMs; }
    void setHoldRepeatFastMs(int ms) { m_holdRepeatFastMs = (ms < 10) ? 10 : (ms > 500) ? 500 : ms; }

    // Cursor activation threshold (INI-only): how far the mouse must move from rest
    // before the cursor + settings button appear. Normalized UI units (0-1 across the
    // screen). Larger = ignores small bumps. Clamped to a tiny non-zero floor so the
    // cursor can never become impossible to summon.
    float getCursorActivationThreshold() const { return m_fCursorActivationThreshold; }
    void setCursorActivationThreshold(float threshold) {
        m_fCursorActivationThreshold = (threshold < 0.0001f) ? 0.0001f : (threshold > 0.5f) ? 0.5f : threshold;
    }

    // Personal best scope setting (Bike or Category)
    PBScope getPBScope() const { return m_pbScope; }
    void setPBScope(PBScope scope) { m_pbScope = scope; }

    DisplayTarget getDisplayTarget() const { return m_displayTarget; }
    void setDisplayTarget(DisplayTarget target) { m_displayTarget = target; }

    // Segment timer: snap a new boundary point to a nearby official split (INI-only).
    // On by default; threshold is in normalized trackPos units (0-1 across the lap).
    bool getSnapSegmentsToSplits() const { return m_bSnapSegmentsToSplits; }
    void setSnapSegmentsToSplits(bool enabled) { m_bSnapSegmentsToSplits = enabled; }
    float getSegmentSnapThreshold() const { return m_fSegmentSnapThreshold; }
    void setSegmentSnapThreshold(float threshold) {
        // Reject non-finite (NaN/Inf) from a hand-edited INI before the clamp - NaN slips
        // past both comparisons and would store NaN, silently disabling snapping.
        if (!std::isfinite(threshold)) { m_fSegmentSnapThreshold = 0.02f; return; }
        m_fSegmentSnapThreshold = (threshold < 0.0f) ? 0.0f : (threshold > 0.25f) ? 0.25f : threshold;
    }

    // HUD title icons: draw each HUD's identity icon to the left of its title text.
    // The same icon is used by the settings panel tab list. On by default.
    bool getTitleIcons() const { return m_bTitleIcons; }
    void setTitleIcons(bool enabled) { m_bTitleIcons = enabled; }

    // Grid overlay (INI-only, debug/alignment aid): draw the HUD snap grid across the
    // whole screen so you can see where each HUD's edges land. Off by default. Every
    // Nth line (majorEvery, default 10) is drawn thicker in the "major" color; the rest
    // in the "minor" color. Colors are 0xAARRGGBB (same format as dropShadowColor).
    bool getGridOverlay() const { return m_bGridOverlay; }
    void setGridOverlay(bool enabled) { m_bGridOverlay = enabled; }
    int getGridOverlayMajorEvery() const { return m_gridOverlayMajorEvery; }
    void setGridOverlayMajorEvery(int every) { m_gridOverlayMajorEvery = (every < 1) ? 1 : (every > 1000) ? 1000 : every; }
    unsigned long getGridOverlayColor() const { return m_ulGridOverlayColor; }
    void setGridOverlayColor(unsigned long color) { m_ulGridOverlayColor = color; }
    unsigned long getGridOverlayMajorColor() const { return m_ulGridOverlayMajorColor; }
    void setGridOverlayMajorColor(unsigned long color) { m_ulGridOverlayMajorColor = color; }

    // Plugin worker thread (INI-only, experimental, off by default). When on, the
    // plugin runs all game-state callbacks + the HUD render build on its OWN thread,
    // so a slow HUD rebuild or a blocking hiccup can NEVER stall the game's frame:
    // the game's Draw only hands over a pre-built, triple-buffered frame and returns.
    // See core/plugin_thread.{h,cpp}. Can be toggled live via the RELOAD_CONFIG hotkey:
    // PluginThread::reconcileEnabled() (game thread) starts/stops the worker to match.
    // Atomic because a RELOAD_CONFIG processed in threaded mode runs on the WORKER thread
    // (it writes this) while the game-thread reconcile reads it.
    bool getPluginThread() const { return m_bPluginThread.load(std::memory_order_relaxed); }
    void setPluginThread(bool enabled) { m_bPluginThread.store(enabled, std::memory_order_relaxed); }

    // Render-load probe (INI-only debug aid, off by default). Emit N extra synthetic
    // quads each frame for the ENGINE to draw so its per-primitive render cost — which
    // no in-plugin timer can see — can be measured differentially (sweep N, watch the
    // frame time rise; the slope is the engine cost). Fullscreen mode measures
    // fill-rate; tiny mode measures per-quad submit cost. Atomic: in plugin-thread
    // mode the worker reads these in produceFrame() while settings-load writes them.
    int  getRenderProbeQuads() const { return m_renderProbeQuads.load(std::memory_order_relaxed); }
    // Clamped at BOTH ends: this count becomes an insert() of that many primitives
    // EVERY frame, so a fat-fingered INI value (an extra zero) would allocate its way
    // out of the frame budget the probe exists to measure. The cap is far above any
    // useful sweep — the heaviest real handoff is ~1300 quads.
    static constexpr int MAX_RENDER_PROBE_QUADS = 100000;
    void setRenderProbeQuads(int n) {
        m_renderProbeQuads.store(std::clamp(n, 0, MAX_RENDER_PROBE_QUADS), std::memory_order_relaxed);
    }
    bool getRenderProbeFullscreen() const { return m_renderProbeFullscreen.load(std::memory_order_relaxed); }
    void setRenderProbeFullscreen(bool on) { m_renderProbeFullscreen.store(on, std::memory_order_relaxed); }
    // 0 = solid-fill quad, 1 = sprite (textured) quad, 2 = text string.
    int  getRenderProbeType() const { return m_renderProbeType.load(std::memory_order_relaxed); }
    void setRenderProbeType(int t) { m_renderProbeType.store((t < 0 || t > 2) ? 0 : t, std::memory_order_relaxed); }
    // WHICH sprite type 1 draws: 0 = cycle every registered sprite, k>0 = that one,
    // every quad, for the whole frame.
    //
    // This is the knob that separates two costs the probe used to charge together.
    // Type 1 always cycled, so its result was "textured AND switching texture on
    // every quad" -- the worst case, and not the one a themed panel is: a panel's 27
    // quads come from ~9 distinct theme sprites and the engine may well batch them.
    // Pinning one sprite gives texture SAMPLING with zero switches; the difference
    // between the two runs is what switching costs. An out-of-range index falls back
    // to cycling rather than drawing nothing, so a typo cannot silently produce a
    // run that measures the untextured path while claiming to measure the textured
    // one.
    int  getRenderProbeSprite() const { return m_renderProbeSprite.load(std::memory_order_relaxed); }
    void setRenderProbeSprite(int s) {
        m_renderProbeSprite.store(s < 0 ? 0 : s, std::memory_order_relaxed);
    }
    // How many characters renderProbeType=2 draws per string.
    //
    // THIS IS NOT COSMETIC, and its absence produced a wrong answer. The text step
    // used a hardcoded 15-character string, so its result was "2.66 us per
    // FIFTEEN-CHARACTER string" -- and applying that to the plugin's own strings,
    // which average about nine, overstated the cost of drop shadow by 1.7x. The
    // engine bills per glyph; a per-string number is only meaningful next to the
    // length it was measured at. Sweeping the length separates the two: the slope is
    // us per character, the intercept is what a string costs before its first glyph.
    // Alpha the probe's quads carry (0-255, default 64).
    //
    // Exists to answer one question the plugin cannot answer about itself: HUDs emit
    // their background quad even when it is FULLY TRANSPARENT ("always add quad to
    // keep indices consistent" -- addBackgroundQuad), which is 27 invisible quads per
    // themed panel at zero opacity. Whether that costs anything depends on whether the
    // engine early-outs on alpha 0, and no in-plugin timer can see the answer. Sweep
    // alpha 64 against alpha 0 at the same N: same cost means transparent quads are
    // charged in full and skipping them is worth the surgery, cheaper means it is not.
    int  getRenderProbeAlpha() const { return m_renderProbeAlpha.load(std::memory_order_relaxed); }
    void setRenderProbeAlpha(int a) {
        m_renderProbeAlpha.store(std::clamp(a, 0, 255), std::memory_order_relaxed);
    }

    static constexpr int MAX_PROBE_TEXT_CHARS = 90;   // SPluginString_t holds 100
    int  getRenderProbeTextChars() const { return m_renderProbeTextChars.load(std::memory_order_relaxed); }
    void setRenderProbeTextChars(int n) {
        m_renderProbeTextChars.store(std::clamp(n, 1, MAX_PROBE_TEXT_CHARS), std::memory_order_relaxed);
    }

    // Drop shadow settings (for text rendering)
    bool getDropShadow() const { return m_bDropShadow; }
    void setDropShadow(bool enabled) { m_bDropShadow = enabled; }
    float getDropShadowOffsetX() const { return m_fDropShadowOffsetX; }
    float getDropShadowOffsetY() const { return m_fDropShadowOffsetY; }
    unsigned long getDropShadowColor() const { return m_ulDropShadowColor; }
    void setDropShadowOffsetX(float offset) { m_fDropShadowOffsetX = offset; }
    void setDropShadowOffsetY(float offset) { m_fDropShadowOffsetY = offset; }
    void setDropShadowColor(unsigned long color) { m_ulDropShadowColor = color; }

    // Reset all settings to defaults
    void resetToDefaults();

private:
    UiConfig();
    ~UiConfig() = default;
    UiConfig(const UiConfig&) = delete;
    UiConfig& operator=(const UiConfig&) = delete;

    bool m_bGridSnapping = true;    // Grid snapping enabled by default
    std::string m_themeName;         // Empty = no panel theme (flat backgrounds)
    bool m_bScreenClamping = false;  // Screen clamping disabled by default
    bool m_bMenuOnlyCursor = false;  // Cursor follows mouse movement by default
    bool m_bAutoSave = true;         // Auto-save enabled by default
    TemperatureUnit m_temperatureUnit = TemperatureUnit::CELSIUS;  // Celsius by default
    PBScope m_pbScope = PBScope::CATEGORY;  // Per-category PB tracking by default
    DisplayTarget m_displayTarget = DisplayTarget::IN_GAME;  // HUD in the game by default
    bool m_bSnapSegmentsToSplits = true;    // Snap segment boundaries to nearby splits by default
    float m_fSegmentSnapThreshold = 0.02f;  // Snap distance: 2% of the lap
    int m_holdRepeatFastMs = 50;     // Max repeat speed: 50ms (~20/sec)
    float m_fCursorActivationThreshold = 0.015f;  // Mouse travel from rest before cursor appears (~29px horiz on 1080p)
    bool m_bTitleIcons = true;       // HUD title identity icons enabled by default
    std::atomic<bool> m_bPluginThread{ false };  // Experimental plugin worker thread (INI-only, off by default; live-toggle via reconcileEnabled)
    std::atomic<int>  m_renderProbeQuads{ 0 };       // DEBUG: extra synthetic quads/frame for engine render-cost measurement (INI-only, off by default)
    std::atomic<bool> m_renderProbeFullscreen{ false };  // DEBUG: probe quads full-screen (fill-rate) vs tiny (submit cost)
    std::atomic<int>  m_renderProbeType{ 0 };        // DEBUG: 0=fill quad, 1=sprite quad, 2=text string
    std::atomic<int>  m_renderProbeSprite{ 0 };      // DEBUG: 0=cycle all sprites, k=pin sprite k (isolates texture SWITCH cost from sample cost)
    std::atomic<int>  m_renderProbeAlpha{ 64 };      // DEBUG: probe quad alpha; 0 tests whether the engine early-outs
    std::atomic<int>  m_renderProbeTextChars{ 15 };  // DEBUG: glyphs per probe string (the engine bills per glyph)

    // Grid overlay (INI-only debug aid)
    bool m_bGridOverlay = false;                       // Off by default
    int m_gridOverlayMajorEvery = 10;                  // Emphasize every 10th line
    unsigned long m_ulGridOverlayColor = 0x22FFFFFF;   // Minor lines (subtle white)
    unsigned long m_ulGridOverlayMajorColor = 0x9933CCFF;  // Major lines (light blue)

    // Drop shadow settings
    bool m_bDropShadow = false;                      // Off by default: every shadowed string is a second draw
    float m_fDropShadowOffsetX = 0.03f;              // 3% of font size
    float m_fDropShadowOffsetY = 0.04f;              // 4% of font size
    unsigned long m_ulDropShadowColor = 0xAA000000;  // Semi-transparent black
};
