// ============================================================================
// hud/base_hud.cpp
// Base class for all HUD display elements with common rendering and positioning logic
// ============================================================================
#include "base_hud.h"
#include "../core/layout_config.h"
#include "../core/plugin_constants.h"
#include "../core/plugin_manager.h"
#include "../core/plugin_utils.h"
#include "../core/color_config.h"
#include "../core/font_config.h"
#include "../core/ui_config.h"
#include "../core/settings_manager.h"
#include "../core/hud_manager.h"
#include "../core/asset_manager.h"
#include "../core/companion_window.h"
#include "../diagnostics/logger.h"
#include "../handlers/draw_handler.h"
#include "../diagnostics/timer.h"
#include <algorithm>
#include <cmath>
#include <limits>

bool BaseHud::handleMouseInput(bool allowInput) {
    if (!m_bDraggable) return false;

    const InputManager& input = InputManager::getInstance();

    // Only process if cursor is enabled
    if (!input.isCursorEnabled()) {
        if (m_bDragging) {
            m_bDragging = false;
            DEBUG_INFO("Drag cancelled - cursor disabled");
        }
        return false;
    }

    // If input is not allowed (another HUD is being dragged), skip input processing
    if (!allowInput) {
        return m_bDragging;  // Return current drag state but don't process new input
    }

    const MouseButton& rightButton = input.getRightButton();
    const CursorPosition& cursor = input.getCursorPosition();

    // A drag edits the FOCUSED surface's position: dragging in the companion window
    // moves the companion instance, in the game window the game instance. The HUD
    // sits at that surface's offset, so hit-test and initial offset use it too.
    bool companion = input.getActiveSurface() == InputManager::Surface::Companion;
    float effOffX = companion ? getCompanionOffsetX() : m_fOffsetX;
    float effOffY = companion ? getCompanionOffsetY() : m_fOffsetY;

    // Start dragging on RMB click within bounds
    if (rightButton.isClicked() && cursor.isValid && !m_bDragging) {
        if (isPointInBoundsAt(cursor.x, cursor.y, effOffX, effOffY)) {
            m_bDragging = true;
            m_bDragCompanion = companion;
            m_fDragStartX = cursor.x;
            m_fDragStartY = cursor.y;
            m_fInitialOffsetX = effOffX;
            m_fInitialOffsetY = effOffY;
            DEBUG_INFO_F("Started dragging HUD (RMB) on %s surface at cursor: (%.3f, %.3f)",
                companion ? "companion" : "game", cursor.x, cursor.y);
        }
    }

    // Update position while dragging
    if (m_bDragging && rightButton.isPressed && cursor.isValid) {
        float deltaX = cursor.x - m_fDragStartX;
        float deltaY = cursor.y - m_fDragStartY;

        float newOffsetX = m_fInitialOffsetX + deltaX;
        float newOffsetY = m_fInitialOffsetY + deltaY;

        // Get actual window bounds and clamp position (if enabled)
        const WindowBounds& windowBounds = input.getWindowBounds();
        if (UiConfig::getInstance().getScreenClamping()) {
            clampPositionToBounds(newOffsetX, newOffsetY, windowBounds);
        }

        // Snap to grid if enabled (use separate horizontal/vertical grids for perfect alignment)
        // grid-snap-exempt: this IS the drag path snapEdgeX/Y exists for -- it needs
        // the gate around the whole clamp+snap block, not per-axis inside it.
        if (UiConfig::getInstance().getGridSnapping()) {
            // Snap the panel's resulting top-LEFT EDGE, not the offset. Snapping the
            // offset quantises only the drag delta, so a HUD whose layout starts at an
            // off-grid x (most of them: a default position is a designed number, not a
            // multiple of 0.0055) would stay off-grid no matter where it was dropped --
            // the rows inside it on the lattice, the panel around them between two lines.
            //
            // Only the offset changes, so nothing moves until the user drags: a saved
            // position from an older build keeps its exact pixels until it is touched.
            newOffsetX += layout().snapDeltaX(m_fBoundsLeft + newOffsetX);
            newOffsetY += layout().snapDeltaY(m_fBoundsTop + newOffsetY);

            // Edge magnetism: snap to window edges if within one grid cell
            // This allows HUDs to be positioned flush against screen borders
            const float gridH = layoutDefaults().cellW;
            const float gridV = layoutDefaults().cellH;

            // Calculate where HUD edges would be with current offset
            float hudLeft = m_fBoundsLeft + newOffsetX;
            float hudRight = m_fBoundsRight + newOffsetX;
            float hudTop = m_fBoundsTop + newOffsetY;
            float hudBottom = m_fBoundsBottom + newOffsetY;

            // Snap to left/right edge if close
            if (std::abs(hudLeft - windowBounds.left) < gridH) {
                newOffsetX = windowBounds.left - m_fBoundsLeft;
            }
            else if (std::abs(hudRight - windowBounds.right) < gridH) {
                newOffsetX = windowBounds.right - m_fBoundsRight;
            }

            // Snap to top/bottom edge if close
            if (std::abs(hudTop - windowBounds.top) < gridV) {
                newOffsetY = windowBounds.top - m_fBoundsTop;
            }
            else if (std::abs(hudBottom - windowBounds.bottom) < gridV) {
                newOffsetY = windowBounds.bottom - m_fBoundsBottom;
            }
        }

        // Commit to the surface this drag started on. The companion frame is
        // re-translated from the HUD's live position every collect, so updating the
        // companion offset moves it without a HUD rebuild; the game path keeps its
        // existing layout-dirty behavior.
        if (m_bDragCompanion) {
            if (getCompanionOffsetX() != newOffsetX || getCompanionOffsetY() != newOffsetY) {
                setCompanionPosition(newOffsetX, newOffsetY);
            }
        } else if (m_fOffsetX != newOffsetX || m_fOffsetY != newOffsetY) {
            m_fOffsetX = newOffsetX;
            m_fOffsetY = newOffsetY;
            setLayoutDirty();  // Only layout dirty, not data
        }
    }

    // Stop dragging on RMB release
    if (m_bDragging && rightButton.isReleased()) {
        m_bDragging = false;
        DEBUG_INFO_F("Stopped dragging HUD on %s surface at offset: (%.3f, %.3f)",
            m_bDragCompanion ? "companion" : "game",
            m_bDragCompanion ? getCompanionOffsetX() : m_fOffsetX,
            m_bDragCompanion ? getCompanionOffsetY() : m_fOffsetY);

        // Mark settings dirty after a drag. The write is DEFERRED to a leave-track transition
        // (pit/exit) or the Save button, so it never spikes a gameplay frame; the moved position
        // is already applied live. Unconditional so the Save button tracks changes in manual mode.
        SettingsManager::getInstance().markDirty();
    }

    // Return true if we're currently dragging (tells HudManager to stop processing other HUDs)
    return m_bDragging;
}

void BaseHud::validatePosition() {
    // If HUD is dirty (e.g., scale was just changed), update bounds before validating
    // This ensures we validate against the correct scaled dimensions
    if (isDataDirty() || isLayoutDirty()) {
        update();
    }

    // Skip clamping if disabled
    if (!UiConfig::getInstance().getScreenClamping()) {
        return;
    }

    // Skip clamping when HUD has zero-size bounds (no content to clamp).
    // HUDs like TimingHud in SPLITS mode have empty bounds when not frozen,
    // and clamping against zero bounds corrupts the saved offset (e.g., a
    // negative offsetY used to position the HUD near the top gets reset to 0).
    if (m_fBoundsLeft == m_fBoundsRight && m_fBoundsTop == m_fBoundsBottom) {
        return;
    }

    const WindowBounds& windowBounds = InputManager::getInstance().getWindowBounds();

    // Use helper to clamp position to window bounds
    if (clampPositionToBounds(m_fOffsetX, m_fOffsetY, windowBounds)) {
        setLayoutDirty();  // Only layout dirty, not data
        DEBUG_INFO_F("HUD position adjusted to fit window bounds: (%.3f, %.3f)",
            m_fOffsetX, m_fOffsetY);
    }
}

bool BaseHud::checkFrequentUpdates() {
    if (!needsFrequentUpdates()) {
        return false;
    }

    auto now = std::chrono::steady_clock::now();
    auto sinceLastTick = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - m_lastTickUpdate
    ).count();

    if (sinceLastTick >= getTickIntervalMs()) {
        m_lastTickUpdate = now;
        setDataDirty();
        return true;
    }

    return false;
}

void BaseHud::setBounds(float left, float top, float right, float bottom) {
    m_fBoundsLeft = left;
    m_fBoundsTop = top;
    m_fBoundsRight = right;
    m_fBoundsBottom = bottom;
}

bool BaseHud::isPointInBounds(float x, float y) const {
    return isPointInBoundsAt(x, y, m_fOffsetX, m_fOffsetY);
}

bool BaseHud::isVisibleAnySurface() const {
    if (m_bVisible.load()) return true;  // vis-gate: this IS the any-surface check
    // The companion is a second surface: a HUD enabled only there must still update.
    //
    // rendersOnCompanion() has to be asked HERE too, not only in collectSurface().
    // Otherwise a HUD excluded from the companion can still answer "visible on some
    // surface" off a companion flag that no longer reaches a renderer, and every
    // update() gated on this predicate rebuilds for a frame nobody draws. The helmet
    // reaches that state from ordinary use: on in game, open the companion (the
    // snapshot copies the game flag into the companion one), then switch it off in
    // game -- now the game flag is false, the companion flag is true, and the overlay
    // is drawn on neither surface while rebuilding full-screen every frame.
    // check_visibility_gates.sh polices the producer/consumer half of this.
    return rendersOnCompanion()
        && CompanionWindow::getInstance().isEnabled()
        && getCompanionVisible();
}

bool BaseHud::isPointInBoundsAt(float x, float y, float offX, float offY) const {
    float boundsLeft = m_fBoundsLeft + offX;
    float boundsTop = m_fBoundsTop + offY;
    float boundsRight = m_fBoundsRight + offX;
    float boundsBottom = m_fBoundsBottom + offY;
    return (x >= boundsLeft && x <= boundsRight && y >= boundsTop && y <= boundsBottom);
}

bool BaseHud::clampPositionToBounds(float& offsetX, float& offsetY, const WindowBounds& windowBounds) const {
    // Calculate HUD edges in screen space with proposed offset
    float hudLeft = m_fBoundsLeft + offsetX;
    float hudRight = m_fBoundsRight + offsetX;
    float hudTop = m_fBoundsTop + offsetY;
    float hudBottom = m_fBoundsBottom + offsetY;

    bool needsAdjustment = false;

    // Clamp horizontally to keep HUD within window bounds
    if (hudLeft < windowBounds.left) {
        offsetX = windowBounds.left - m_fBoundsLeft;
        needsAdjustment = true;
    }
    else if (hudRight > windowBounds.right) {
        offsetX = windowBounds.right - m_fBoundsRight;
        needsAdjustment = true;
    }

    // Clamp vertically to keep HUD within window bounds
    if (hudTop < windowBounds.top) {
        offsetY = windowBounds.top - m_fBoundsTop;
        needsAdjustment = true;
    }
    else if (hudBottom > windowBounds.bottom) {
        offsetY = windowBounds.bottom - m_fBoundsBottom;
        needsAdjustment = true;
    }

    return needsAdjustment;
}

void BaseHud::rebuildAndRecord() {
    // THE WHOLE DIRTY PATH IS THE REBUILD, not just rebuildRenderData(): the two
    // calls below run on exactly the same frames and are part of what a rebuild
    // costs. Timed only around the middle one, their cost would fall out of the
    // per-HUD table and into the Draw remainder instead.
    auto& bm = PluginData::getInstance().getBenchmarkMetrics();
    if (bm.active && m_benchmarkIndex >= 0) {
        long long start = DrawHandler::getCurrentTimeUs();
        rebuildRenderData();
        onAfterDataRebuild();
        finalizeThemedFill();   // see its declaration: one fill layer per pixel
        bm.recordHudRebuild(m_benchmarkIndex, DrawHandler::getCurrentTimeUs() - start);
    } else {
        rebuildRenderData();
        onAfterDataRebuild();
        finalizeThemedFill();
    }
}

void BaseHud::processDirtyFlags() {
    if (isDataDirty()) {
        // Time the rebuild if benchmark is active and this HUD is registered
        rebuildAndRecord();
        clearDataDirty();
        clearLayoutDirty();
    }
    else if (isLayoutDirty()) {
        rebuildLayout();
        // Keep the title icon glued to the (possibly repositioned) title string.
        // Idempotent, so it's a no-op when the fast path already placed it or didn't
        // move the title at all.
        positionTitleIcon();
        clearLayoutDirty();
    }
}

// ============================================================================
// Dynamic Texture Variant Support
// ============================================================================

void BaseHud::setTextureBaseName(const std::string& baseName) {
    m_textureBaseName = baseName;

    // If variant is set, update the background texture index
    if (m_textureVariant > 0) {
        int spriteIndex = AssetManager::getInstance().getSpriteIndex(baseName, m_textureVariant);
        if (spriteIndex > 0) {
            m_iBackgroundTextureIndex = spriteIndex;
            invalidateThemeCache();
        }
    }
}

bool BaseHud::hasBackgroundArtwork() const {
    // No default case, so adding a pack kind fails to compile here rather than
    // silently falling through to the texture-variant answer.
    switch (m_packKind) {
        case PackKind::Gamepad:  return AssetManager::getInstance().getGamepadCount() > 0;
        case PackKind::Pitboard: return AssetManager::getInstance().getPitboardCount() > 0;
        case PackKind::Gauges:   return AssetManager::getInstance().getGaugesCount() > 0;
        case PackKind::None:     break;
    }
    return !getAvailableTextureVariants().empty();
}

void BaseHud::setTextureVariant(int variant) {
    if (variant < 0) variant = 0;

    // A PACK HUD HAS NO TEXTURE VARIANTS -- its artwork is the selected pack's, set
    // through setBackgroundTextureIndex() on every rebuild -- so a variant request
    // here has nothing to resolve against and must change nothing.
    //
    // Without this it falls through to the no-base-name arm at the bottom, which
    // assumes a HUD that FORGOT to declare a stem and switches the background off to
    // say so. For a pack HUD that assumption is wrong twice over: the stem is absent
    // on purpose, and switching the background off is exactly the state
    // m_textureRequired exists to prevent.
    //
    // The path is live on every upgrade: an INI written before a HUD became a pack
    // still carries `textureVariant=1` from when it was texture-based, and
    // applyBaseSettings walks the section's keys in map (alphabetical) order -- so
    // `showBackgroundTexture=1` is applied first and this call would turn it straight
    // back off: the pack art vanishes and the panel draws its flat background colour
    // with the loose button/stick sprites still on top, a grey slab wearing half a
    // controller. Nothing fails, nothing logs, and a fresh install cannot reproduce
    // it, because only an upgraded file has the key. Pinned by
    // pack_texture_variant_test.cpp.
    //
    // Returning early also lets the stale key heal itself: m_textureVariant stays 0,
    // so the next save writes 0 and the file stops carrying the trap.
    if (m_packKind != PackKind::None) return;
    // Variant 0 IS "off" for a texture cycle, so a HUD whose artwork is mandatory
    // (see m_textureRequired) snaps a 0 up to its first real variant instead. This
    // is what makes a stale `textureVariant=0` in an existing INI self-heal rather
    // than stranding the HUD in a state its settings row no longer offers.
    if (variant == 0 && m_textureRequired) {
        const std::vector<int> avail = getAvailableTextureVariants();
        if (!avail.empty()) variant = avail.front();
    }

    if (m_textureVariant != variant) {
        m_textureVariant = variant;

        // ONCE, at the top, covering every branch below -- the same shape
        // setShowBackgroundTexture() uses. A background texture SUPERSEDES the theme
        // (resolveActiveTheme returns nullptr while one is on), so all four exits
        // from here can change which theme this HUD resolves to.
        //
        // On the success branch only, the asymmetry is easy to miss: turning a
        // texture ON would invalidate, turning it OFF would not. Cycling a HUD's
        // Texture control to "Off" under a global theme then leaves the memo holding
        // nullptr, so the rebuild that setDataDirty() triggers draws a FLAT
        // background -- and stays flat until something else bumps the generation,
        // because nothing does so on a normal frame.
        invalidateThemeCache();

        // Update background texture index based on variant
        if (variant == 0) {
            // Variant 0 = Off (solid color background)
            m_bShowBackgroundTexture = false;
        } else if (!m_textureBaseName.empty()) {
            int spriteIndex = AssetManager::getInstance().getSpriteIndex(m_textureBaseName, variant);
            if (spriteIndex > 0) {
                m_iBackgroundTextureIndex = spriteIndex;
                m_bShowBackgroundTexture = true;
            } else {
                // Variant not found, fall back to solid color
                m_bShowBackgroundTexture = false;
                DEBUG_WARN_F("Texture variant %d not found for %s", variant, m_textureBaseName.c_str());
            }
        } else {
            // A VARIANT WAS ASKED FOR AND THERE IS NO STEM TO RESOLVE IT AGAINST.
            // The constructor is the only place a stem comes from, so forgetting it
            // is a real mistake -- and dropped in silence it would be no sprite, no
            // warning, and a HUD whose artwork is mandatory (m_textureRequired)
            // drawing nothing where its dial should be. Say so.
            m_bShowBackgroundTexture = false;
            DEBUG_WARN_F("Texture variant %d requested with no texture base name "
                         "(declare one in the HUD's constructor, before resetToDefaults)",
                         variant);
        }

        setDataDirty();
    }
}

void BaseHud::cycleTextureVariant(bool forward) {
    if (m_textureBaseName.empty()) {
        return;
    }

    std::vector<int> variants = getAvailableTextureVariants();
    if (variants.empty()) {
        return;
    }

    // Off, then the variants -- EXCEPT where the artwork is the widget, which
    // cycles the variants alone (see m_textureRequired for what Off looked like).
    std::vector<int> cycleOrder;
    if (!m_textureRequired) cycleOrder.push_back(0);
    cycleOrder.insert(cycleOrder.end(), variants.begin(), variants.end());

    // Find current position in cycle
    int currentIndex = 0;
    for (size_t i = 0; i < cycleOrder.size(); ++i) {
        if (cycleOrder[i] == m_textureVariant) {
            currentIndex = static_cast<int>(i);
            break;
        }
    }

    // Calculate next position
    int cycleSize = static_cast<int>(cycleOrder.size());
    int newIndex;
    if (forward) {
        newIndex = (currentIndex + 1) % cycleSize;
    } else {
        newIndex = (currentIndex - 1 + cycleSize) % cycleSize;
    }

    setTextureVariant(cycleOrder[newIndex]);
}

std::vector<int> BaseHud::getAvailableTextureVariants() const {
    if (m_textureBaseName.empty()) {
        return {};
    }

    return AssetManager::getInstance().getAvailableVariants(m_textureBaseName);
}

// ============================================================================
// Per-HUD Color/Font Overrides
// ============================================================================

unsigned long BaseHud::getColor(ColorSlot slot) const {
    size_t index = static_cast<size_t>(slot);
    if (index < m_colorOverrides.size() && m_colorOverrides[index].has_value()) {
        return m_colorOverrides[index].value();
    }
    return ColorConfig::getInstance().getColor(slot);
}

int BaseHud::getFont(FontCategory category) const {
    size_t index = static_cast<size_t>(category);
    if (index < m_fontOverrides.size() && m_fontOverrides[index].has_value()) {
        int fontIndex = m_fontOverrides[index].value().resolvedIndex;
        if (fontIndex > 0) return fontIndex;
        // Font not resolved, fall back to global
    }
    return FontConfig::getInstance().getFont(category);
}

void BaseHud::setColorOverride(ColorSlot slot, unsigned long color) {
    size_t index = static_cast<size_t>(slot);
    if (index < m_colorOverrides.size()) {
        m_colorOverrides[index] = color;
        setDataDirty();
    }
}

void BaseHud::clearColorOverride(ColorSlot slot) {
    size_t index = static_cast<size_t>(slot);
    if (index < m_colorOverrides.size()) {
        m_colorOverrides[index].reset();
        setDataDirty();
    }
}

bool BaseHud::hasColorOverride(ColorSlot slot) const {
    size_t index = static_cast<size_t>(slot);
    return index < m_colorOverrides.size() && m_colorOverrides[index].has_value();
}

unsigned long BaseHud::getColorOverrideValue(ColorSlot slot) const {
    size_t index = static_cast<size_t>(slot);
    if (index < m_colorOverrides.size() && m_colorOverrides[index].has_value()) {
        return m_colorOverrides[index].value();
    }
    return 0;
}

void BaseHud::setFontOverride(FontCategory category, const std::string& fontName) {
    size_t index = static_cast<size_t>(category);
    if (index < m_fontOverrides.size()) {
        FontOverride override;
        override.name = fontName;
        override.resolvedIndex = AssetManager::getInstance().getFontIndexByName(fontName);
        m_fontOverrides[index] = std::move(override);
        setDataDirty();
    }
}

void BaseHud::clearFontOverride(FontCategory category) {
    size_t index = static_cast<size_t>(category);
    if (index < m_fontOverrides.size()) {
        m_fontOverrides[index].reset();
        setDataDirty();
    }
}

bool BaseHud::hasFontOverride(FontCategory category) const {
    size_t index = static_cast<size_t>(category);
    return index < m_fontOverrides.size() && m_fontOverrides[index].has_value();
}

const std::string& BaseHud::getFontOverrideName(FontCategory category) const {
    size_t index = static_cast<size_t>(category);
    if (index < m_fontOverrides.size() && m_fontOverrides[index].has_value()) {
        return m_fontOverrides[index].value().name;
    }
    static const std::string empty;
    return empty;
}
