// ============================================================================
// hud/settings_button_widget.cpp
// Settings button widget - draggable button to toggle settings menu
// Shows "[=]" when settings closed, "[x]" when settings open
// ============================================================================
#include "settings_button_widget.h"
#include "../core/hud_manager.h"
#include "../core/input_manager.h"
#include "../core/plugin_utils.h"
#include "../diagnostics/logger.h"
#include "../diagnostics/timer.h"

using namespace PluginConstants;

SettingsButtonWidget::SettingsButtonWidget()
    : m_bCachedSettingsVisible(false)
{
    DEBUG_INFO("SettingsButtonWidget created");
    setDraggable(true);

    // Set defaults - small compact button (match TimingWidget opacity)
    m_fBackgroundOpacity = 0.1f;  // Low opacity background like TimingWidget
    setPosition(0.957f, 0.0111f);  // Top-right corner

    // Pre-allocate vectors
    m_strings.reserve(1);  // One string: button text
    m_quads.reserve(1);    // One quad: background

    rebuildRenderData();
}

bool SettingsButtonWidget::handlesDataType(DataChangeType dataType) const {
    // Settings button doesn't depend on any game data
    return false;
}

void SettingsButtonWidget::update() {
    // Always rebuild to update hover state and settings visibility
    // Button is lightweight (1 quad, 1 string) so rebuilding every frame is fine
    setDataDirty();

    // Check data dirty first (takes precedence)
    if (isDataDirty()) {
        rebuildRenderData();
        clearDataDirty();
        clearLayoutDirty();
    }
    else if (isLayoutDirty()) {
        rebuildLayout();
        clearLayoutDirty();
    }
}

bool SettingsButtonWidget::isClicked() const {
    const InputManager& input = InputManager::getInstance();

    // Button is only clickable if cursor is visible
    if (!input.shouldShowCursor()) {
        return false;
    }

    const CursorPosition& cursor = input.getCursorPosition();
    if (!cursor.isValid) {
        return false;
    }

    const MouseButton& leftButton = input.getLeftButton();
    if (!leftButton.isClicked()) {
        return false;
    }

    // Check if click is within button bounds
    return isPointInBounds(cursor.x, cursor.y);
}

void SettingsButtonWidget::rebuildLayout() {
    // Fast path - only update positions
    auto dim = getScaledDimensions();

    float startX = 0.0f;  // Base position (upper left)
    float startY = 0.0f;

    // Calculate dimensions
    float backgroundWidth = dim.paddingH +
                           PluginUtils::calculateMonospaceTextWidth(BUTTON_WIDTH_CHARS, dim.fontSize) +
                           dim.paddingH;
    float backgroundHeight = dim.paddingV + dim.lineHeightNormal + dim.paddingV;

    setBounds(startX, startY, startX + backgroundWidth, startY + backgroundHeight);

    // Update background quad position (applies offset internally)
    updateBackgroundQuadPosition(startX, startY, backgroundWidth, backgroundHeight);

    float contentStartX = startX + dim.paddingH;
    float contentStartY = startY + dim.paddingV;

    // Position button text
    if (!m_strings.empty()) {
        float x = contentStartX;
        float y = contentStartY;
        applyOffset(x, y);
        m_strings[0].m_afPos[0] = x;
        m_strings[0].m_afPos[1] = y;
    }
}

void SettingsButtonWidget::rebuildRenderData() {
    // Clear render data
    m_strings.clear();
    m_quads.clear();

    // Don't render button when cursor is hidden (auto-hide after timeout)
    const InputManager& input = InputManager::getInstance();
    if (!input.shouldShowCursor()) {
        setBounds(0.0f, 0.0f, 0.0f, 0.0f);  // Clear bounds
        return;
    }

    auto dim = getScaledDimensions();

    float startX = 0.0f;  // Base position (upper left)
    float startY = 0.0f;

    // Calculate dimensions
    float backgroundWidth = dim.paddingH +
                           PluginUtils::calculateMonospaceTextWidth(BUTTON_WIDTH_CHARS, dim.fontSize) +
                           dim.paddingH;
    float backgroundHeight = dim.paddingV + dim.lineHeightNormal + dim.paddingV;

    float contentStartX = startX + dim.paddingH;
    float contentStartY = startY + dim.paddingV;

    // Determine button text based on whether settings menu is visible
    bool settingsVisible = HudManager::getInstance().isSettingsVisible();
    const char* buttonText = settingsVisible ? TEXT_OPEN : TEXT_CLOSED;

    // Check if cursor is hovering for color change
    // (Cursor is guaranteed to be visible at this point)
    bool isHovering = false;
    const CursorPosition& cursor = input.getCursorPosition();
    if (cursor.isValid) {
        isHovering = isPointInBounds(cursor.x, cursor.y);
    }

    // Add background quad with hover-based color
    if (isHovering) {
        // Green when closed (can open), Red when open (can close)
        unsigned long hoverBgColor = settingsVisible ? Colors::RED : Colors::GREEN;
        SPluginQuad_t backgroundQuad;
        float x = startX, y = startY;
        applyOffset(x, y);
        setQuadPositions(backgroundQuad, x, y, backgroundWidth, backgroundHeight);
        backgroundQuad.m_iSprite = SpriteIndex::SOLID_COLOR;
        backgroundQuad.m_ulColor = PluginUtils::applyOpacity(hoverBgColor, m_fBackgroundOpacity);
        m_quads.push_back(backgroundQuad);
    } else {
        // Default background when not hovering
        addBackgroundQuad(startX, startY, backgroundWidth, backgroundHeight);
    }

    // Use PRIMARY color when hovering, MUTED when not
    unsigned long textColor = isHovering ? TextColors::PRIMARY : TextColors::MUTED;

    // Add button text
    addString(buttonText, contentStartX, contentStartY, Justify::LEFT,
        Fonts::ROBOTO_MONO, textColor, dim.fontSize);

    // Set bounds for drag detection
    setBounds(startX, startY, startX + backgroundWidth, startY + backgroundHeight);
}

void SettingsButtonWidget::resetToDefaults() {
    m_bVisible = true;
    m_bShowTitle = true;
    m_bShowBackgroundTexture = false;  // No texture by default
    m_fBackgroundOpacity = 0.1f;  // Match TimingWidget opacity
    m_fScale = 1.0f;
    setPosition(0.957f, 0.0111f);
    setDataDirty();
}
