// ============================================================================
// hud/base_hud.cpp
// Base class for all HUD display elements with common rendering and positioning logic
// ============================================================================
#include "base_hud.h"
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
        if (UiConfig::getInstance().getGridSnapping()) {
            newOffsetX = PluginConstants::HudGrid::SNAP_TO_GRID_X(newOffsetX);
            newOffsetY = PluginConstants::HudGrid::SNAP_TO_GRID_Y(newOffsetY);

            // Edge magnetism: snap to window edges if within one grid cell
            // This allows HUDs to be positioned flush against screen borders
            const float gridH = PluginConstants::HudGrid::GRID_SIZE_HORIZONTAL;
            const float gridV = PluginConstants::HudGrid::GRID_SIZE_VERTICAL;

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
    if (m_bVisible.load()) return true;
    // The companion is a second surface: a HUD enabled only there must still update.
    return CompanionWindow::getInstance().isEnabled() && getCompanionVisible();
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

void BaseHud::processDirtyFlags() {
    if (isDataDirty()) {
        // Time the rebuild if benchmark is active and this HUD is registered
        auto& bm = PluginData::getInstance().getBenchmarkMetrics();
        if (bm.active && m_benchmarkIndex >= 0) {
            long long start = DrawHandler::getCurrentTimeUs();
            rebuildRenderData();
            long long elapsed = DrawHandler::getCurrentTimeUs() - start;
            bm.recordHudRebuild(m_benchmarkIndex, elapsed);
        } else {
            rebuildRenderData();
        }
        onAfterDataRebuild();
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
        }
    }
}

void BaseHud::setTextureVariant(int variant) {
    if (variant < 0) variant = 0;

    if (m_textureVariant != variant) {
        m_textureVariant = variant;

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

    // Build cycle order: 0 (Off), then all variants
    std::vector<int> cycleOrder = {0};
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
