// ============================================================================
// hud/crash_widget.cpp
// Crash counter widget - see crash_widget.h for who it is for.
// ============================================================================
#include "crash_widget.h"

#include <cstdio>

#include "../core/plugin_data.h"
#include "../core/plugin_utils.h"
#include "../core/color_config.h"
#include "../core/input_manager.h"
#include "../core/stats_manager.h"
#include "../diagnostics/logger.h"

using namespace PluginConstants;

CrashWidget::CrashWidget() {
    m_panelKind = PanelKind::Widget;
    m_bContentCard = true;
    DEBUG_INFO("CrashWidget created");
    setDraggable(true);
    m_quads.reserve(2);    // background + the reset chip
    m_strings.reserve(3);  // title (optional) + count + button label

    setTextureBaseName("crash_widget");
    resetToDefaults();
    rebuildRenderData();
}

bool CrashWidget::handlesDataType(DataChangeType dataType) const {
    // The tally moves on a crash, which reaches StatsManager through telemetry.
    return dataType == DataChangeType::InputTelemetry;
}

void CrashWidget::update() {
    // The button is live wherever the widget is, so the hit-test runs before the
    // visibility gate below can return.
    handleClickDetection();

    if (!isVisibleAnySurface()) {
        clearDataDirty();
        clearLayoutDirty();
        return;
    }

    // GATED ON THE NUMBER, not on the telemetry tick that carried it. Gear rebuilds
    // every tick because its value changes every tick; this one moves a handful of
    // times a session, and the crash-detection edge it follows is rarer still.
    const int count = StatsManager::getInstance().getCrashTally();
    if (count != m_cachedCount) {
        m_cachedCount = count;
        setDataDirty();
    }

    if (isDataDirty() || isLayoutDirty()) rebuildAndRecord();
}

void CrashWidget::resetCounter() {
    StatsManager::getInstance().resetCrashTally();
    setDataDirty();
}

void CrashWidget::rebuildLayout() {
    rebuildRenderData();
}

void CrashWidget::rebuildRenderData() {
    clearStrings();
    m_quads.clear();

    auto dim = getScaledDimensions();

    // THE SAME CONTENT BOX AS SPEED AND GEAR: a two-row value over a one-row extra.
    // There the extra is "mph" or nothing; here it is the Reset chip, and the chip is
    // sized to THE ROW rather than the row to the chip -- so the three tile.
    //
    // That costs the button its vertical insets (a chip elsewhere is insetT + a text
    // row + insetB, which is a fifth taller than the row alone). It is the right
    // trade here: the widget exists to sit beside Speed and Gear in a streamer's
    // corner, three boxes of one height, and a row that is a fifth taller than its
    // neighbours reads as a mistake at a glance where a slightly snug chip does not.
    //
    // So no planButtonTerms() at all: the chip takes the ROW for its height and the
    // CONTENT BOX for its width (both below), leaving the theme's [button] insets
    // nothing to size. The theme still draws the chip's own artwork.
    const float buttonHeight = dim.lineHeightNormal;
    const bool withButton = m_bShowResetButton;

    BaseHud::PanelWant want;
    want.contentW = PluginUtils::calculateMonospaceTextWidth(WidgetDimensions::CRASH_WIDTH, dim.fontSize);
    // NO junction gap above the chip, unlike VersionWidget's hand-laid button row:
    // this is not a row appended below the content, it is the SAME second row Speed
    // spends on "MPH" and Gear leaves empty. A seam here would break the tiling for
    // air the other two do not pay for.
    float sectionH = dim.lineHeightLarge;
    if (withButton) sectionH += buttonHeight;
    want.sectionH = { sectionH };
    want.captionW = planTitleWidth(dim, "Crashes");
    PanelPlan& p = planPanel(dim, want);

    const float backgroundWidth = p.width();
    const float backgroundHeight = p.height();
    const unsigned long textColor = this->getColor(ColorSlot::PRIMARY);

    addPlanBackground(p, 0.0f, 0.0f);
    addPlanTitle(p, "Crashes", this->getFont(FontCategory::TITLE), textColor);
    const float currentY = p.contentY();
    // The CARD's centre, not the panel's (PanelPlan::sectionBoxCenterX): the two
    // agree only while the [content] terms are left/right symmetric.
    const float centerX = p.sectionBoxCenterX();

    char countBuffer[12];
    snprintf(countBuffer, sizeof(countBuffer), "%d", m_cachedCount < 0 ? 0 : m_cachedCount);

    // TITLE at fontSizeExtraLarge, matching Speed / Lap / Position / Gear rather than
    // the DIGITS category the name would suggest: those five are the plugin's big-value
    // widgets and they are meant to read as one family when tiled. A FIXED size, not one
    // driven by the box the way Gear's is -- Gear draws exactly one character, this draws
    // one to three, and a number that shrank as it crossed 9 would be the one thing on
    // screen that moved while a viewer was watching it.
    //
    // INK-CENTRED in the value's own two-row band, the solve every big value in the
    // plugin uses -- not a hand-tuned nudge. See BaseHud::inkCenteredY.
    addString(countBuffer, centerX, inkCenteredY(currentY, dim.lineHeightLarge, dim.fontSizeExtraLarge),
        Justify::CENTER, this->getFont(FontCategory::TITLE), textColor, dim.fontSizeExtraLarge);

    if (withButton) {
        // THE CONTENT WIDTH, not the label's. Sizing a button from its label is
        // right where the panel is sized from something else (RecordsHud's Compare
        // under a wide table, VersionWidget's chips) -- here the panel is eight
        // characters wide and the label plus the theme's button insets came out
        // WIDER than that, so the chip hung over the panel padding by half its
        // width on each side while the caption and the count sat inside it.
        //
        // Spanning the content box is also what a lone action under a value reads
        // as, and it cannot overhang whatever insets a theme sets.
        const float buttonWidth = p.contentW();
        const float rowY = currentY + dim.lineHeightLarge;

        // CENTRED ON THE CARD, like RecordsHud's Compare chip under its table -- and
        // like the count above it. Anchoring at contentX() put it flush against
        // whichever side had the smaller [content] border (`border = 2 0 4 6` made the
        // content box the card's right edge, so the chip hugged the corner); centring
        // on the PANEL put it outside the card when a [content] margin was asymmetric
        // (`margin = 4 6 8 0`). The card is the box the player sees it in.
        m_resetLeft = centerX - buttonWidth / 2.0f;
        m_resetTop = rowY;
        m_resetWidth = buttonWidth;
        m_resetHeight = buttonHeight;

        // Ink-centred in the chip rather than offset by insetT, since the chip no
        // longer carries one -- the same solve the big count above uses.
        addStateButton(m_resetLeft, rowY, buttonWidth, buttonHeight,
                       "Reset", inkCenteredY(rowY, buttonHeight, dim.fontSize), dim.fontSize,
                       // ACCENT, not NEGATIVE: the same slot RecordsHud's Compare chip
                       // uses, and this is the same kind of control. Red would read as
                       // "careful" for an action whose whole point is to be pressed
                       // casually, between runs, on camera.
                       this->getColor(ColorSlot::ACCENT),
                       m_resetHovered ? ButtonState::Hovered : ButtonState::Idle);
    } else {
        m_resetWidth = 0.0f;
        m_resetHeight = 0.0f;
    }

    setBounds(0.0f, 0.0f, backgroundWidth, backgroundHeight);
}

void CrashWidget::handleClickDetection() {
    // Any-surface, like VersionWidget's chips: a widget enabled only on the
    // companion still has a clickable button there.
    if (!isVisibleAnySurface() || !m_bShowResetButton) return;

    const InputManager& input = InputManager::getInstance();
    if (!input.isCursorEnabled()) return;

    const MouseButton& leftButton = input.getLeftButton();
    const bool isLeftPressed = leftButton.isPressed;
    const bool isLeftClick = isLeftPressed && !m_wasLeftPressed;
    m_wasLeftPressed = isLeftPressed;

    // Into build space, so the target follows the widget when it is dragged on
    // the companion surface (a no-op in-game).
    CursorPosition cursor = input.getCursorPosition();
    mapCursorToHudSpace(cursor.x, cursor.y);

    const bool wasHovered = m_resetHovered;
    m_resetHovered = false;
    if (cursor.isValid && m_resetWidth > 0.0f) {
        const float left = m_resetLeft + m_fOffsetX;
        const float top = m_resetTop + m_fOffsetY;
        m_resetHovered = cursor.x >= left && cursor.x <= left + m_resetWidth &&
                         cursor.y >= top && cursor.y <= top + m_resetHeight;
    }
    if (m_resetHovered != wasHovered) setDataDirty();

    if (isLeftClick && m_resetHovered) {
        DEBUG_INFO("CrashWidget: reset clicked");
        resetCounter();
    }
}

void CrashWidget::resetToDefaults() {
    m_bVisible = false;          // opt-in: this is a streaming tool, not a default HUD
    m_bShowTitle = false;        // title off, like the rest of the widget rail
    setTextureVariant(0);
    // Opaque, like the gauges it tiles with on the rail (Lean and Fuel sit
    // either side of its slot). Speed and Gear are the transparent ones --
    // they are bare numerals with no box to speak of.
    m_fBackgroundOpacity = 1.0f;
    m_fScale = 1.0f;
    m_bShowResetButton = true;
    // Lean's old slot on the widget rail (13-cell pitch, shared y); everything
    // from Lean leftward moved one slot down to make room.
    setPosition(cellsX(135), cellsY(74));
    setDataDirty();
}
