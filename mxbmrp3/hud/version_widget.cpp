// ============================================================================
// hud/version_widget.cpp
// Version widget - displays plugin name and version
// ============================================================================
#include "version_widget.h"
#include "center_stack.h"

#include <cstdio>
#include <cstring>
#include <cmath>
#include <shellapi.h>
#pragma comment(lib, "shell32.lib")

#include "../diagnostics/logger.h"
#include "../core/plugin_utils.h"
#include "../core/input_manager.h"
#include "../core/color_config.h"
#include "../core/update_checker.h"
#include "../core/stats_manager.h"
#include "../core/settings_manager.h"
#include "../core/hud_manager.h"
#include "../core/plugin_manager.h"
#include "../core/system_messages.h"
#include "../core/hotkey_manager.h"
#include "../core/pixel_text.h"
#if GAME_HAS_ANALYTICS
#include "../core/analytics_manager.h"
#endif
#include "settings_hud.h"
#include "../handlers/draw_handler.h"

using namespace PluginConstants;

VersionWidget::VersionWidget() {
    m_panelKind = PanelKind::Widget;
    // Body card: this widget's content is a block the theme can frame -- exactly what
    // a themed body card is for, and what every other panel already opts into. Opt-in;
    // see BaseHud::m_bContentCard. Without it, under a theme with a card set the
    // version line and the update prompt sit on bare frame while every widget beside
    // them sits on a card.
    m_bContentCard = true;
    // One-time setup
    setDraggable(true);
    m_strings.reserve(1);

    // Initialize brick array (game state, not configurable)
    m_bricks.fill(true);

    // Set all configurable defaults
    resetToDefaults();

    rebuildRenderData();
}

bool VersionWidget::handlesDataType(DataChangeType /*dataType*/) const {
    return false;  // No data changes - version is constant
}

void VersionWidget::update() {
    // Popups first: which one is owed, and its countdown. Not during the game,
    // which owns the panel until it exits.
    if (!m_gameActive) {
        syncPopup();
        tickPopup();
    }

    // Handle click detection for Easter egg trigger
    handleClickDetection();

    // If game is active, run game logic
    if (m_gameActive) {
        // Calculate delta time
        long long currentTimeUs = DrawHandler::getCurrentTimeUs();
        float deltaTime = 0.0f;

        if (m_lastUpdateTimeUs > 0) {
            deltaTime = (currentTimeUs - m_lastUpdateTimeUs) / 1000000.0f;
            // Clamp to prevent huge jumps (e.g., after pause/tab-out)
            if (deltaTime > 0.1f) deltaTime = 0.1f;
        }
        m_lastUpdateTimeUs = currentTimeUs;

        // Update game state
        updateGame(deltaTime);

        // Always rebuild render data when game is active
        rebuildAndRecord();
        return;
    }

    // Normal widget update path
    if (isLayoutDirty()) {
        rebuildLayout();
        clearLayoutDirty();
    }

    // Rebuild render data when dirty or on first update
    if (isDataDirty() || m_strings.empty()) {
        rebuildAndRecord();
        clearDataDirty();
    }
}

void VersionWidget::handleClickDetection() {
    // Any-surface: the notification buttons are clickable on the companion too
    // (the hit-test below maps the cursor via mapCursorToHudSpace), so a widget
    // enabled only there must still process clicks.
    if (!isVisibleAnySurface() || isHeldBack()) return;

    const InputManager& input = InputManager::getInstance();
    if (!input.isCursorEnabled()) return;

    const MouseButton& leftButton = input.getLeftButton();

    // Detect left click (transition from not pressed to pressed)
    bool isLeftPressed = leftButton.isPressed;
    bool isLeftClick = isLeftPressed && !m_wasLeftPressed;
    m_wasLeftPressed = isLeftPressed;

    // Handle popup button hover and clicks (not during game)
    if (m_popup != Popup::None && !m_gameActive) {
        // Shift into build space so the buttons line up when the widget is dragged
        // to a different spot on the companion (no-op in-game).
        CursorPosition cursor = input.getCursorPosition();
        mapCursorToHudSpace(cursor.x, cursor.y);

        // Track which button is hovered (need to apply offset for comparison)
        NotificationButton oldHover = m_hoveredButton;
        m_hoveredButton = NotificationButton::NONE;

        // The welcome has no primary button (hasPrimary in the popup layout).
        if (cursor.isValid && m_popup != Popup::Welcome) {
            float primaryLeft = m_primaryButtonLeft + m_fOffsetX;
            float primaryTop = m_primaryButtonTop + m_fOffsetY;
            if (cursor.x >= primaryLeft && cursor.x <= primaryLeft + m_primaryButtonWidth &&
                cursor.y >= primaryTop && cursor.y <= primaryTop + m_primaryButtonHeight) {
                m_hoveredButton = NotificationButton::PRIMARY;
            }
        }
        if (cursor.isValid) {
            float dismissLeft = m_dismissButtonLeft + m_fOffsetX;
            float dismissTop = m_dismissButtonTop + m_fOffsetY;
            if (cursor.x >= dismissLeft && cursor.x <= dismissLeft + m_dismissButtonWidth &&
                cursor.y >= dismissTop && cursor.y <= dismissTop + m_dismissButtonHeight) {
                m_hoveredButton = NotificationButton::DISMISS;
            }
        }

        // Rebuild if hover state changed
        if (m_hoveredButton != oldHover) {
            setDataDirty();
        }

        if (isLeftClick) {
            if (m_hoveredButton == NotificationButton::PRIMARY) {
                onPrimaryClicked();
                endPopup(/*answered=*/true);
                return;
            }
            if (m_hoveredButton == NotificationButton::DISMISS) {
                endPopup(/*answered=*/true);
                return;
            }
        }
        return;  // Don't process game input while a popup is showing
    }

    // Only handle left clicks when game is active (for ball launch / exit)
    if (!m_gameActive) return;
    if (!isLeftClick) return;

    // Handle game clicks
    if (m_gameOver) {
        // Click to exit
        exitGame();
    } else if (!m_ballLaunched) {
        // Click to launch ball
        launchBall();
    }
}

void VersionWidget::showUpdateNotification() {
    // Don't show if already in notification mode
    if (m_showingUpdateNotification) return;

    UpdateChecker& checker = UpdateChecker::getInstance();
    if (!checker.shouldShowUpdateNotification()) return;

    DEBUG_INFO_F("VersionWidget: Showing update notification for version %s",
                checker.getLatestVersion().c_str());

    // Only the flag: this runs on the UpdateChecker worker, and the panel's
    // visibility is switched on the game thread by syncPopup().
    m_showingUpdateNotification = true;
    setDataDirty();
}

VersionWidget::Popup VersionWidget::choosePopup() const {
    if (m_showingUpdateNotification && UpdateChecker::getInstance().shouldShowUpdateNotification()) {
        return Popup::Update;
    }
    switch (SystemMessages::getInstance().pendingPopup()) {
        case SystemMessages::Popup::Updated: return Popup::Updated;
        case SystemMessages::Popup::Welcome: return Popup::Welcome;
        default:                             return Popup::None;
    }
}

void VersionWidget::syncPopup() {
    const Popup want = choosePopup();
    if (want == m_popup) return;
    // From none to a popup: remember the player's own setting. From one popup
    // straight to the next: the remembered setting still stands.
    if (m_popup == Popup::None) m_visibleBeforePopup = m_bVisible;  // vis-gate: save/restore around a popup
    m_popup = want;
    setVisible(want != Popup::None ? true : m_visibleBeforePopup);
    m_popupShownMs = 0.0f;
    m_popupLastTick = std::chrono::steady_clock::now();
    m_countdownShown = -1;
    m_hoveredButton = NotificationButton::NONE;
    setDataDirty();
}

void VersionWidget::onVisibilityApplied() {
    if (m_popup == Popup::None) return;
    m_visibleBeforePopup = m_bVisible;  // vis-gate: the profile's value, restored at the end
    if (!m_bVisible) setVisible(true);  // vis-gate: re-asserting the popup's own switch
}

void VersionWidget::tickPopup() {
    if (m_popup == Popup::None) return;
    // The widget switched off mid-popup some way other than a profile apply
    // (which onVisibilityApplied handles): the popup stays up, and off is what
    // it restores when it ends.
    if (!m_bVisible) {  // vis-gate: re-asserting the popup's own switch
        m_visibleBeforePopup = false;
        setVisible(true);
    }
    const auto now = std::chrono::steady_clock::now();
    float dtMs = std::chrono::duration<float, std::milli>(now - m_popupLastTick).count();
    m_popupLastTick = now;
    // A gap between Draws (a menu, a load) is not time on screen.
    if (dtMs > 100.0f) dtMs = 100.0f;

    const bool menuOpen = HudManager::getInstance().getSettingsHud().isVisible();  // vis-gate: the menu's own state
    // The welcome's goal is the menu opening: reached any way (the key or the
    // menu button), it is done.
    if (m_popup == Popup::Welcome && menuOpen) {
        endPopup(/*answered=*/true);
        return;
    }
    // The welcome waits for the player: no countdown, plain "Dismiss".
    if (m_popup == Popup::Welcome) return;
    // Counts only while drawn and the menu is shut (see the header).
    if (!menuOpen && isVisibleAnySurface() && !isHeldBack()) m_popupShownMs += dtMs;

    const int remaining = static_cast<int>(std::ceil((POPUP_COUNTDOWN_MS - m_popupShownMs) / 1000.0f));
    if (remaining <= 0) {
        endPopup(/*answered=*/false);
        return;
    }
    if (remaining != m_countdownShown) {
        m_countdownShown = remaining;
        setDataDirty();
    }
}

void VersionWidget::onPrimaryClicked() {
    SettingsHud& settings = HudManager::getInstance().getSettingsHud();
    switch (m_popup) {
        case Popup::Update:
            settings.showUpdatesTab();
            break;
        case Popup::Updated:
            settings.showWhatsNew();
            break;
        case Popup::Welcome:   // Dismiss only: the player learns the settings button
        case Popup::None:
            break;
    }
}

void VersionWidget::endPopup(bool answered) {
    switch (m_popup) {
        case Popup::Update:
            // Dismiss skips this version for good (the button's old meaning);
            // View and a timeout only end it for this session.
            if (answered && m_hoveredButton == NotificationButton::DISMISS) {
                UpdateChecker& checker = UpdateChecker::getInstance();
                checker.setDismissedVersion(checker.getLatestVersion());
                DEBUG_INFO_F("VersionWidget: Update notification dismissed for version %s",
                             checker.getLatestVersion().c_str());
                SettingsManager::getInstance().markDirty();
            }
            m_showingUpdateNotification = false;
            break;
        case Popup::Updated:
        case Popup::Welcome: {
            const SystemMessages::Popup p = m_popup == Popup::Updated ? SystemMessages::Popup::Updated
                                                                      : SystemMessages::Popup::Welcome;
            SystemMessages& msgs = SystemMessages::getInstance();
            if (answered) msgs.resolvePopup(p);
            else msgs.expirePopup(p);
            // What was told persists with the next save (leave-track or Save).
            SettingsManager::getInstance().markDirty();
            break;
        }
        case Popup::None:
            break;
    }
    m_hoveredButton = NotificationButton::NONE;
    syncPopup();   // the next one owed, or back to the player's own setting
}


void VersionWidget::rebuildLayout() {
    if (m_gameActive) return;   // game handles its own layout
    // BOX-MODEL: one source of geometry. The widget is a handful of strings;
    // rebuilding is cheaper than a second copy of every mode's sizing arithmetic
    // kept in step by comment.
    rebuildRenderData();
}

PanelPlan VersionWidget::notifyPlan(const ScaledDimensions& dim,
                                             float contentWidth, int rows,
                                             float extraH, bool stackMember) const {
    PanelWant want;
    want.sectionH = { static_cast<float>(rows) * dim.lineHeightNormal + extraH };
    want.captionW = planTitleWidth(dim, "Version");
    if (stackMember) {
        // THE PLAIN VERSION ROW is a centre-stack panel and takes the stack's
        // width rule whole: the shared minimum owns the width and the string
        // does not compete for it. Passing its own text width HERE too would be
        // invisible at shipped padding (the string is 19 normal chars against 14
        // large ones of interior, so the minimum wins) and 78px of divergence
        // once [Advanced] padding grows, because a stated content width carries
        // the padding past the minimum while its neighbours, which state none,
        // sit on it. See BaseHud::wantCenterStackWidth.
        //
        // The string fitting that interior is therefore a CONSTRAINT, not a
        // coincidence -- version_fit_test pins it, so a longer version number
        // fails a test instead of quietly clipping.
        wantCenterStackWidth(want, dim);
    } else {
        // THE UPDATE POPUP is not a stack member: it has a message and a button
        // row that the stack width cannot hold, so it sizes to them and is
        // deliberately wider. Same panel machinery, different job.
        want.contentW = contentWidth;
        want.minPanelW = CenterStack::boxWidth(dim.fontSizeLarge, centerStackPaddingX());
    }
    return planPanel(dim, want);
}

void VersionWidget::rebuildRenderData() {
    // Clear existing data
    clearStrings();
    m_quads.clear();

    if (m_gameActive) {
        renderGame();
        return;
    }

    auto dim = getScaledDimensions();

    if (m_popup != Popup::None) {
        // ===== POPUP MODE: the message, with its two buttons on a separate row =====
        // Formatted per rebuild, which is per change (a popup, a hover, a second
        // of countdown), never per frame.
        char displayText[112];
        // The welcome has Dismiss only: a settings button inside it would teach
        // a new player that button, not the real one in the top right.
        const char* primaryLabel = nullptr;
        switch (m_popup) {
            case Popup::Update:
                snprintf(displayText, sizeof(displayText), "MXBMRP3 %s available!",
                         UpdateChecker::getInstance().getLatestVersion().c_str());
                primaryLabel = "View in Settings";
                break;
            case Popup::Updated:
                // One fixed line: the news itself is the "New" markers in the menu.
                snprintf(displayText, sizeof(displayText), "Updated to %s. Open settings to see what's new",
                         SystemMessages::getInstance().updatedLine());
                primaryLabel = "What's New";
                break;
            case Popup::Welcome:
            default: {
                // The key that opens the menu, as bound; the menu button when none is.
                char hint[80];
                HotkeyManager::getInstance().formatOpenSettingsHint(hint, sizeof(hint));
                snprintf(displayText, sizeof(displayText), "Welcome! %s", hint);
                break;
            }
        }
        char dismissLabel[16];
        if (m_countdownShown > 0) snprintf(dismissLabel, sizeof(dismissLabel), "Dismiss (%d)", m_countdownShown);
        else snprintf(dismissLabel, sizeof(dismissLabel), "Dismiss");

        const int textLength = static_cast<int>(strlen(displayText));
        const float textWidth = PluginUtils::calculateMonospaceTextWidth(textLength, dim.fontSize);

        // Button dimensions
        const float charWidth = PluginUtils::calculateMonospaceTextWidth(1, dim.fontSize);
        // The [button] terms, all four sides: box = insets around the label
        // row, gap = the SUM of facing margins (1 char at shipped defaults).
        const PlanButtonTerms bt = planButtonTerms(dim);
        const float buttonGap = bt.gap;
        const bool hasPrimary = primaryLabel != nullptr;
        const int primaryChars = hasPrimary ? static_cast<int>(strlen(primaryLabel)) + BUTTON_PAD_CHARS : 0;
        const float viewButtonWidth = hasPrimary ? charWidth * primaryChars + bt.insetL + bt.insetR : 0.0f;
        const float dismissButtonWidth = charWidth * DISMISS_BUTTON_CHARS + bt.insetL + bt.insetR;
        const float buttonHeight = bt.insetT + dim.lineHeightNormal + bt.insetB;

        // Width is max of text row or button row
        const float buttonRowWidth = hasPrimary ? viewButtonWidth + buttonGap + dismissButtonWidth
                                                : dismissButtonWidth;
        const float contentWidth = std::fmax(textWidth, buttonRowWidth);
        // BOX-MODEL: the plan owns padding, chrome and the content origin. The
        // centre-stack width is a MINIMUM on the panel (widthSetBy 'min'), not
        // a padding sum folded into the content.
        // THE JUNCTION PLUS the box's own margin and insets. marginT alone is not
        // enough: it is the button BOX's margin and defaults to zero, so the row
        // would still sit flush against the message. The seam above a button row is the
        // [panel] junction gap -- what panel_box.h spends as `y += gapY` for a
        // PLANNED row, and what the settings tabs spend as addSpacing() for a
        // hand-laid one. This row is hand-laid, so it owes the same gap.
        const float junctionY = panelGapY(dim);
        const PanelPlan p = notifyPlan(dim, contentWidth, /*rows=*/2,
                                       junctionY + bt.marginT + bt.insetT + bt.insetB);
        const float backgroundWidth = p.width();
        const float backgroundHeight = p.height();

        // Center widget at top of screen
        float startX = centerAnchoredPanelLeft(backgroundWidth);
        float startY = 0.01f;

        PanelPlan placed = p;
        addPlanBackground(placed, startX, startY);
        addPlanTitle(placed, "Version",
                     this->getColor(ColorSlot::PRIMARY));
        float currentY = placed.contentY();

        // Render the message (centered on first row)
        float row1Y = currentY;
        // The CARD's centre, not the panel's (PanelPlan::sectionBoxCenterX).
        float centerX = placed.sectionBoxCenterX();
        addString(displayText, centerX, row1Y, Justify::CENTER,
                  this->getFont(FontCategory::NORMAL), this->getColor(ColorSlot::SECONDARY), dim.fontSize);

        // Second row: buttons centered
        float row2Y = row1Y + dim.lineHeightNormal + junctionY + bt.marginT;
        float buttonsStartX = centerX - buttonRowWidth / 2.0f;

        // ===== View in Settings Button (accent color) =====
        float viewBtnX = buttonsStartX;
        float viewBtnY = row2Y;

        // Store button bounds for click detection (before offset)
        m_primaryButtonLeft = viewBtnX;
        m_primaryButtonTop = viewBtnY;
        m_primaryButtonWidth = viewButtonWidth;
        m_primaryButtonHeight = buttonHeight;

        bool isViewHovered = (m_hoveredButton == NotificationButton::PRIMARY);

        // Hover is carried by the chip's alpha, not by a second label colour.
        if (hasPrimary) {
            addStateButton(viewBtnX, viewBtnY, viewButtonWidth, buttonHeight,
                           primaryLabel, viewBtnY + bt.insetT, dim.fontSize,
                           this->getColor(ColorSlot::ACCENT),
                           isViewHovered ? ButtonState::Hovered : ButtonState::Idle);
        }

        // ===== Dismiss Button (negative color) =====
        float dismissBtnX = hasPrimary ? viewBtnX + viewButtonWidth + buttonGap : buttonsStartX;
        float dismissBtnY = row2Y;

        // Store button bounds for click detection (before offset)
        m_dismissButtonLeft = dismissBtnX;
        m_dismissButtonTop = dismissBtnY;
        m_dismissButtonWidth = dismissButtonWidth;
        m_dismissButtonHeight = buttonHeight;

        bool isDismissHovered = (m_hoveredButton == NotificationButton::DISMISS);

        addStateButton(dismissBtnX, dismissBtnY, dismissButtonWidth, buttonHeight,
                       dismissLabel, dismissBtnY + bt.insetT, dim.fontSize,
                       this->getColor(ColorSlot::NEGATIVE),
                       isDismissHovered ? ButtonState::Hovered : ButtonState::Idle);

        // Set bounds for the whole widget
        setBounds(startX, startY, startX + backgroundWidth, startY + backgroundHeight);

    } else {
        // ===== NORMAL MODE: Show plugin version =====

        char displayText[64];
        snprintf(displayText, sizeof(displayText), "MXBMRP3 v%s", PLUGIN_VERSION);

        // Calculate text width based on actual string length
        const int textLength = static_cast<int>(strlen(displayText));
        const float textWidth = PluginUtils::calculateMonospaceTextWidth(textLength, dim.fontSize);
        const PanelPlan p = notifyPlan(dim, textWidth, /*rows=*/1, /*extraH=*/0.0f,
                                       /*stackMember=*/true);
        const float backgroundWidth = p.width();
        const float backgroundHeight = p.height();

        // Base position centers widget at (0.5, 0.01) - offset applied automatically by BaseHud
        // CENTRE-ANCHORED, like the centre stack: offsetX is this widget's centre.
        float startX = centerAnchoredPanelLeft(backgroundWidth);
        float startY = 0.01f;  // Top of screen

        PanelPlan placed = p;
        addPlanBackground(placed, startX, startY);
        addPlanTitle(placed, "Version",
                     this->getColor(ColorSlot::PRIMARY));
        // Add main text
        // INK-centred in the section's DRAWN BOX, like Timing's time. Centring in the
        // content ROW is the same place while the card border is symmetric and a cell
        // high when it is not (see PanelPlan::sectionBoxY); passing the bare row top
        // leaves addString to centre the glyph CELL -- fine for a row in a TABLE,
        // where every row carries the same 0.11-of-a-cell bias and it cancels, but
        // this row IS the whole body of a one-row panel, so the bias reads as the
        // text sitting high in its own box.
        // CENTRED, like the notification message this panel turns into: the
        // string is the entire body of a one-row panel, and the panel is sized
        // to it, so left-justifying it only showed when a theme's padding made
        // the panel wider than the text. On the CARD, not the panel
        // (PanelPlan::sectionBoxCenterX), matching the ink-centring below.
        float contentStartX = placed.sectionBoxCenterX();
        float contentStartY = inkCenteredY(placed.sectionBoxY(), placed.sectionBoxH(),
                                           dim.fontSize);

        addString(displayText, contentStartX, contentStartY, Justify::CENTER,
                  this->getFont(FontCategory::NORMAL), this->getColor(ColorSlot::SECONDARY), dim.fontSize);

        // Set bounds for drag detection
        setBounds(startX, startY, startX + backgroundWidth, startY + backgroundHeight);
    }
}

// The shipped NORMAL font's capital, in its own cell (fontSize high, one
// charWidth wide), measured off the real string in the comparison capture
// (companion_demo "brokeninstall real"). Ratios rather than pixels, so the
// blocks follow the scale like the font does.
namespace {
    constexpr float PIXEL_CAP_TOP = 0.29f;   // cell top to cap top, of fontSize
    constexpr float PIXEL_CAP_H   = 0.555f;  // cap height, of fontSize
    constexpr float PIXEL_INK_W   = 0.80f;   // capital width, of charWidth
}

void VersionWidget::addPixelText(const char* text, float centerX, float rowTop,
                                 float fontSize, unsigned long color) {
    const float charW = PluginUtils::calculateMonospaceTextWidth(1, fontSize);
    const float pw = charW * PIXEL_INK_W / static_cast<float>(PixelText::GLYPH_W);
    const float ph = fontSize * PIXEL_CAP_H / static_cast<float>(PixelText::CAP_H);
    // The string's cells, centred like Justify::CENTER; each glyph centred in its cell.
    const float left = centerX - PluginUtils::calculateMonospaceTextWidth(
                                     static_cast<int>(strlen(text)), fontSize) * 0.5f
                     + (charW - pw * static_cast<float>(PixelText::GLYPH_W)) * 0.5f;
    const float top = rowTop + fontSize * PIXEL_CAP_TOP;
    PixelText::forEachRun(text, [&](int glyph, int col, int row, int len) {
        float x = left + charW * static_cast<float>(glyph) + pw * static_cast<float>(col);
        float y = top + ph * static_cast<float>(row);
        applyOffset(x, y);
        const float w = pw * static_cast<float>(len);
        SPluginQuad_t q;
        q.m_aafPos[0][0] = x;      q.m_aafPos[0][1] = y;
        q.m_aafPos[1][0] = x;      q.m_aafPos[1][1] = y + ph;
        q.m_aafPos[2][0] = x + w;  q.m_aafPos[2][1] = y + ph;
        q.m_aafPos[3][0] = x + w;  q.m_aafPos[3][1] = y;
        q.m_iSprite = SpriteIndex::SOLID_COLOR;
        q.m_ulColor = color;
        m_quads.push_back(q);
    });
}

void VersionWidget::buildInstallNotice(const char* const* rows, int count, bool realText,
                                       std::vector<SPluginQuad_t>& outQuads,
                                       std::vector<SPluginString_t>& outStrings) {
    // Once, at startup: the popup's layout with the message on `count` rows and
    // no button row, at the widget's place.
    clearStrings();
    m_quads.clear();
    const auto dim = getScaledDimensions();
    int widest = 0;
    for (int i = 0; i < count; ++i) widest = std::max(widest, static_cast<int>(strlen(rows[i])));
    const float textWidth = PluginUtils::calculateMonospaceTextWidth(widest, dim.fontSize);
    PanelPlan placed = notifyPlan(dim, textWidth, count);
    const float startX = centerAnchoredPanelLeft(placed.width());
    const float startY = 0.01f;
    addPlanBackground(placed, startX, startY);
    const float centerX = placed.sectionBoxCenterX();
    float y = placed.contentY();
    for (int i = 0; i < count; ++i) {
        const unsigned long color = this->getColor(i == 0 ? ColorSlot::NEGATIVE : ColorSlot::SECONDARY);
        if (realText)
            addString(rows[i], centerX, y, Justify::CENTER,
                      this->getFont(FontCategory::NORMAL), color, dim.fontSize);
        else
            addPixelText(rows[i], centerX, y, dim.fontSize, color);
        y += dim.lineHeightNormal;
    }
    setBounds(startX, startY, startX + placed.width(), startY + placed.height());
    outQuads = m_quads;
    outStrings = m_strings;
    setDataDirty();   // back to the widget's own view on its next update
}

void VersionWidget::resetToDefaults() {
    m_bVisible = false;    // Hidden by default
    m_bShowTitle = false;  // No title
    setTextureVariant(0);  // No texture by default
    m_fBackgroundOpacity = 1.0f;  // Full opacity
    setScale(1.0f);
    setPosition(CENTER_ANCHOR_X, cellsY(1));  // Top centre

    // Reset game state and restore cursor if game was active
    if (m_gameActive) {
        InputManager::getInstance().setCursorSuppressed(false);
    }
    m_gameActive = false;
    m_ballLaunched = false;
    m_gameOver = false;
    m_wasVisibleBeforeGame = false;
    m_ballX = 0.0f;
    m_ballY = 0.0f;
    m_ballVelX = 0.0f;
    m_ballVelY = 0.0f;
    m_paddleX = 0.0f;
    m_bricks.fill(true);
    m_bricksRemaining = TOTAL_BRICKS;
    m_score = 0;
    m_level = 1;
    m_lastUpdateTimeUs = 0;

    // Reset notification state. A popup owed by SystemMessages comes back on
    // the next update(); the update notice is UpdateChecker's to raise again.
    m_showingUpdateNotification = false;
    m_popup = Popup::None;
    m_hoveredButton = NotificationButton::NONE;

    setDataDirty();
}
