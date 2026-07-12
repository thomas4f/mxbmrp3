// ============================================================================
// hud/settings_button_widget.cpp
// Settings button widget - draggable button to toggle settings menu
// Shows "[=]" when settings closed, "[x]" when settings open
// ============================================================================
#include "settings_button_widget.h"
#include "../core/hud_manager.h"
#include "../core/input_manager.h"
#include "../core/plugin_utils.h"
#include "../core/color_config.h"
#include "../core/ui_config.h"
#include "../core/asset_manager.h"
#include "../diagnostics/logger.h"
#include "../diagnostics/timer.h"

using namespace PluginConstants;

SettingsButtonWidget::SettingsButtonWidget()
{
    // One-time setup
    DEBUG_INFO("SettingsButtonWidget created");
    setDraggable(true);
    m_strings.reserve(1);  // One string: button text
    m_quads.reserve(2);    // Two quads: HUD background + button background

    // Set all configurable defaults
    resetToDefaults();

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

    // Handle dirty flags using base class helper
    processDirtyFlags();
}

bool SettingsButtonWidget::isClicked() const {
    // Hidden by the user (settings opened via hotkey) - don't intercept clicks
    if (!m_bVisible) {
        return false;
    }

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

    // Check if click is within button bounds — on the surface the cursor is on
    // (the button may sit at its companion position on the companion window).
    return isPointInActiveBounds(cursor.x, cursor.y);
}

void SettingsButtonWidget::rebuildRenderData() {
    // Clear render data
    clearStrings();
    m_quads.clear();

    // Don't render button when cursor is hidden (auto-hide after timeout), unless it's
    // briefly revealed (entering the track) so users can find it without moving the mouse.
    const InputManager& input = InputManager::getInstance();
    if (!input.shouldShowCursor() && !isRevealed()) {
        setBounds(0.0f, 0.0f, 0.0f, 0.0f);  // Clear bounds
        return;
    }

    auto dim = getScaledDimensions();

    float startX = 0.0f;  // Base position (upper left)
    float startY = 0.0f;

    // PIXEL-SQUARE grid-aligned box, sized so a 2x2 block fills one square gauge widget
    // (e.g. the compass = 12 x 10 grid cells): each button is HALF that, 6 x 5 cells. On
    // the current grid 6*gridH == 5*gridV in pixels, so the box is a true pixel-square;
    // whole cells in both axes keep every edge on a grid line, so four buttons snap
    // perfectly inside a gauge widget and any pair tiles flush. Cells scale with m_fScale
    // (default 1.0). Identical to DirectorWidget's camera button.
    const float gridH = HudGrid::GRID_SIZE_HORIZONTAL;
    const float gridV = HudGrid::GRID_SIZE_VERTICAL;
    int hCells = static_cast<int>(6.0f * m_fScale + 0.5f); if (hCells < 1) hCells = 1;
    int vCells = static_cast<int>(5.0f * m_fScale + 0.5f); if (vCells < 1) vCells = 1;
    float backgroundWidth = hCells * gridH;
    float backgroundHeight = vCells * gridV;
    // Menu/close glyph at the SAME size as a HUD title/identity icon. HUD titles draw at
    // the LARGE font, so the identity icon is fontSizeLarge * 0.63 (~20px at 1080p) - use
    // that exact size (not fontSize * 0.63, which is smaller) for a consistent glyph.
    float iconSize = dim.fontSizeLarge * TITLE_ICON_SCALE;

    // Vertically center the "[=]"/"[x]" text fallback (icons off) in the box.
    float contentStartY = startY + (backgroundHeight - dim.fontSize) * 0.5f;

    // Determine button text based on whether settings menu is visible
    bool settingsVisible = HudManager::getInstance().isSettingsVisible();
    const char* buttonText = settingsVisible ? TEXT_OPEN : TEXT_CLOSED;

    // Check if cursor is hovering for color change
    // (Cursor is guaranteed to be visible at this point)
    bool isHovering = false;
    const CursorPosition& cursor = input.getCursorPosition();
    if (cursor.isValid) {
        isHovering = isPointInActiveBounds(cursor.x, cursor.y);
    }

    // Add HUD background
    addBackgroundQuad(startX, startY, backgroundWidth, backgroundHeight);

    // Add button-style background (accent with opacity, full on hover). Holds its normal
    // weight at/above 10% opacity (unchanged), then fades with the slider below 10% so the
    // whole box can vanish at 0% - the glyph below stays fully opaque.
    {
        SPluginQuad_t buttonBgQuad;
        float x = startX, y = startY;
        applyOffset(x, y);
        setQuadPositions(buttonBgQuad, x, y, backgroundWidth, backgroundHeight);
        buttonBgQuad.m_iSprite = SpriteIndex::SOLID_COLOR;
        const float chipScale = (m_fBackgroundOpacity < 0.1f) ? (m_fBackgroundOpacity / 0.1f) : 1.0f;
        const float chipAlpha = (isHovering ? 1.0f : 128.0f / 255.0f) * chipScale;
        buttonBgQuad.m_ulColor = PluginUtils::applyOpacity(this->getColor(ColorSlot::ACCENT), chipAlpha);
        m_quads.push_back(buttonBgQuad);
    }

    // Use PRIMARY color when hovering, ACCENT when not (accent on accent)
    unsigned long textColor = isHovering ? this->getColor(ColorSlot::PRIMARY) : this->getColor(ColorSlot::ACCENT);

    // Button content: when UI icons are enabled, a flat "menu" icon while closed and a
    // flat "close" icon while open; otherwise the legacy "[=]"/"[x]" text. The flat icons
    // get the (togglable) drop shadow via the title-icon path, so they read like the text
    // they replace. addIcon centers on the point and applies the offset itself.
    int iconSprite = UiConfig::getInstance().getTitleIcons()
        ? AssetManager::getInstance().getIconSpriteIndex(settingsVisible ? "hud-close" : "hud-menu")
        : 0;
    m_titleIconQuadIndex = -1;   // reset each rebuild; set below so the icon is shadowed
    m_titleStringIndex = -1;
    if (iconSprite > 0) {
        m_titleIconQuadIndex = static_cast<int>(m_quads.size());
        addIcon(startX + backgroundWidth * 0.5f, startY + backgroundHeight * 0.5f,
            iconSprite, textColor, iconSize);
    } else {
        addString(buttonText, startX + backgroundWidth * 0.5f, contentStartY, Justify::CENTER,
            this->getFont(FontCategory::NORMAL), textColor, dim.fontSize);
    }

    // Set bounds for drag detection
    setBounds(startX, startY, startX + backgroundWidth, startY + backgroundHeight);
}

void SettingsButtonWidget::resetToDefaults() {
    m_bVisible = true;
    m_bShowTitle = false;  // Hardcoded off (like the cursor) - title makes no sense for a button
    setTextureVariant(0);  // No texture by default
    // The accent "chip" now scales with the opacity slider (see rebuildRenderData),
    // so 0% fades the whole box away leaving just the opaque glyph - a valid look, so
    // the slider floors at 0%.
    m_fMinBackgroundOpacity = 0.0f;
    m_fBackgroundOpacity = 0.1f;  // Match TimingHud opacity (default; slider reaches 0)
    // 100% scale -> a pixel-square 6x5 grid-cell button (6*gridH x 5*gridV ~= 0.033 x
    // 0.0587, ~63x63 px; a 2x2 block = one square gauge widget). Default position is
    // grid-aligned (x = 174 cells, y = 1 cell) with the right edge near the corner
    // (~0.990). The camera button is the same box and defaults flush to its left, so out
    // of the box the pair is a clean grid-aligned row; both are draggable and, with grid
    // snapping on, tile flush beside/below any widget.
    m_fScale = 1.0f;
    setPosition(0.9570f, 0.01173f);
    setDataDirty();
}
