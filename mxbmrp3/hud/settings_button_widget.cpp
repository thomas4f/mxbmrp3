// ============================================================================
// hud/settings_button_widget.cpp
// Settings button widget - draggable button to toggle settings menu
// Shows "[=]" when settings closed, "[x]" when settings open
// ============================================================================
#include "settings_button_widget.h"
#include "corner_buttons.h"
#include "../core/layout_config.h"
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
    // No caption on this panel -- see BaseHud::m_titleSupported.
    disableTitle();
    m_panelKind = PanelKind::Widget;
    // One-time setup
    DEBUG_INFO("SettingsButtonWidget created");
    setDraggable(true);
    m_strings.reserve(1);  // One string: button text
    m_quads.reserve(2);    // Two quads: HUD background + button background

    // Set all configurable defaults
    resetToDefaults();

    rebuildRenderData();
}

bool SettingsButtonWidget::handlesDataType(DataChangeType /*dataType*/) const {
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
    // Hidden by the user (settings opened via hotkey) - don't intercept clicks.
    // Surface-aware: the button may be hidden in-game but enabled on the
    // companion window, where it still renders and must accept clicks.
    if (!isVisibleOnActiveSurface()) {
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

    // Content + padding, like every other panel. This was a fixed 6 x 5 grid cells --
    // a true pixel-square that let a 2x2 block fill one square gauge widget -- which
    // also made panelPaddingXCells/panelPaddingYCells no-ops on the only two buttons in the UI. The glyph
    // is the content and the panel padding surrounds it, so the box now follows the
    // same rule every other panel does.
    //
    // The tiling property is the cost: the box is only pixel-square when the padding
    // makes it so (the grid cell is 10.56 x 12.672px, so equal cell counts are not
    // equal pixels). Uniform padding was the explicit trade.
    const float btnIcon = dim.fontSizeLarge * layout().titleIconSize;
    // BOTH AXES ON THE LATTICE. Unlike a dial, this box is not art -- it is a chip
    // (a themed button slice set, or a plain solid quad), which is MEANT to be sized to
    // the box, so growing it to whole cells distorts nothing and the glyph stays centred
    // in it. The trade the comment above describes is unchanged: the box is still not
    // pixel-square, because a cell is not. It is now a whole number of cells, which is
    // what tiling actually needs. See fitPanelToGrid.
    const GridFit btnFit = fitPanelToGrid(dim.paddingH + btnIcon + dim.paddingH,
                                          panelHeight(dim, btnIcon));
    const float backgroundWidth = btnFit.w;
    const float backgroundHeight = btnFit.h;
    // Menu/close glyph at the SAME size as a HUD title/identity icon. HUD titles draw at
    // the LARGE font, so the identity icon is fontSizeLarge * 0.63 (~20px at 1080p) - use
    // that exact size (not fontSize * 0.63, which is smaller) for a consistent glyph.
    float iconSize = dim.fontSizeLarge * layout().titleIconSize;

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

    // Accent "chip" (full on hover, dimmed otherwise). Holds its normal weight at/above
    // 10% opacity (unchanged), then fades with the slider below 10% so the whole box can
    // vanish at 0% - the glyph below stays fully opaque.
    const float chipScale = (m_fBackgroundOpacity < 0.1f) ? (m_fBackgroundOpacity / 0.1f) : 1.0f;
    const float chipAlpha = (isHovering ? 1.0f : 128.0f / 255.0f) * chipScale;
    const unsigned long chipColor =
        PluginUtils::applyOpacity(this->getColor(ColorSlot::ACCENT), chipAlpha);

    // This widget IS a button, so it takes the theme's BUTTON slices rather than the
    // panel frame. Drawing the frame here and then covering it with an opaque chip is
    // what made it look unthemed: the frame was emitted, then hidden.
    if (addThemedButton(startX, startY, backgroundWidth, backgroundHeight, chipColor)) {
        // The themed branch never reaches addBackgroundQuad, which is what ARMS the
        // panel rect and the fill strips -- so last rebuild's indices would survive
        // into this one and finalizeThemedFill would re-cut using them. Third caller
        // of this shape, after NoticesHud and RadarHud; see invalidatePanelRect().
        //
        // Harmless today only because this widget has no rebuildLayout() fast path
        // and its quad list happens to be rebuilt whole each time. That is not a
        // property worth relying on: adding a fast path here would turn it into the
        // stretched-strip bug the other two already hit.
        invalidatePanelRect();
    } else {
        addBackgroundQuad(startX, startY, backgroundWidth, backgroundHeight);
        SPluginQuad_t buttonBgQuad;
        float x = startX, y = startY;
        applyOffset(x, y);
        setQuadPositions(buttonBgQuad, x, y, backgroundWidth, backgroundHeight);
        buttonBgQuad.m_iSprite = SpriteIndex::SOLID_COLOR;
        buttonBgQuad.m_ulColor = chipColor;
        m_quads.push_back(buttonBgQuad);
    }

    // Whatever reads on the chip -- see BaseHud::chipGlyphColor() for why this is
    // derived from the chip rather than naming a slot. Hover is carried by the
    // chip's own alpha (50% -> 100%) rather than by a second glyph colour, so the
    // glyph no longer changes hue under the cursor.
    unsigned long textColor = this->chipGlyphColor(chipColor);

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
    // 100% scale -> a button sized icon + padding on each axis: an 8 x 6 grid-cell box
    // (0.044 x 0.0704, ~84 x 76 px at 1080p). Default position is grid-aligned
    // (x = 172 cells, y = 1 cell) so the RIGHT EDGE lands on 180 cells = 0.990, just
    // inside the corner. The camera button is the same box and defaults one cell to its
    // left; both are draggable and, with grid snapping on, tile flush beside/below any
    // widget.
    //
    // THE POSITION IS DERIVED FROM THE BOX, and both moved when the box did. This was
    // x = 174 for a 6-cell box, which put the right edge at 180 cells; when the box grew
    // to 8 cells (fitPanelToGrid rounding it onto the lattice) the two defaults stayed
    // put, so the right edge went to 182 cells = 1.001 -- 1.9px off the screen -- and the
    // camera button at 167 overlapped this one by a full cell. A click in that strip hit
    // BOTH: isClicked() is a const edge test that consumes nothing, and HudManager polls
    // each button unconditionally on the same frame, so one click toggled the auto
    // director AND opened the settings panel.
    m_fScale = 1.0f;
    setPosition(cellsX(CornerButtons::SETTINGS_X), cellsY(CornerButtons::BUTTON_Y));
    setDataDirty();
}
