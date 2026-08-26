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
#if GAME_HAS_ANALYTICS
#include "../core/analytics_manager.h"
#endif
#include "settings_hud.h"
#include "../handlers/draw_handler.h"

using namespace PluginConstants;

namespace {
}  // namespace

namespace {
}  // namespace

static constexpr const char* KOFI_URL = "https://ko-fi.com/thomas4f";


VersionWidget::VersionWidget() {
    m_panelKind = PanelKind::Widget;
    // Body card: this widget's content is a block the theme can frame -- exactly what
    // a themed body card is for, and what every other panel already opts into. Opt-in;
    // see BaseHud::m_bContentCard. It was the one panel that drew a themed FRAME (via
    // addBackgroundQuad) and then set its text straight on it, so under a theme with a
    // card set the version line and the update prompt sat on bare frame while every
    // widget beside them sat on a card.
    m_bContentCard = true;
    // One-time setup
    DEBUG_INFO("VersionWidget created");
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
    if (!isVisibleAnySurface()) return;

    const InputManager& input = InputManager::getInstance();
    if (!input.isCursorEnabled()) return;

    const MouseButton& leftButton = input.getLeftButton();

    // Detect left click (transition from not pressed to pressed)
    bool isLeftPressed = leftButton.isPressed;
    bool isLeftClick = isLeftPressed && !m_wasLeftPressed;
    m_wasLeftPressed = isLeftPressed;

    // Handle notification button hover and clicks (not during game)
    if (m_showingUpdateNotification && !m_gameActive) {
        // Shift into build space so the View/Dismiss buttons line up when the widget is
        // dragged to a different spot on the companion (no-op in-game).
        CursorPosition cursor = input.getCursorPosition();
        mapCursorToHudSpace(cursor.x, cursor.y);

        // Track which button is hovered (need to apply offset for comparison)
        NotificationButton oldHover = m_hoveredButton;
        m_hoveredButton = NotificationButton::NONE;

        if (cursor.isValid) {
            // Check View button bounds (apply offset to button coords)
            float viewLeft = m_viewButtonLeft + m_fOffsetX;
            float viewTop = m_viewButtonTop + m_fOffsetY;
            if (cursor.x >= viewLeft && cursor.x <= viewLeft + m_viewButtonWidth &&
                cursor.y >= viewTop && cursor.y <= viewTop + m_viewButtonHeight) {
                m_hoveredButton = NotificationButton::VIEW;
            }

            // Check Dismiss button bounds
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

        // Handle button clicks
        if (isLeftClick) {
            if (m_hoveredButton == NotificationButton::VIEW) {
                // Clear notification mode since they're going to settings
                m_showingUpdateNotification = false;
                m_bVisible = false;
                m_hoveredButton = NotificationButton::NONE;
                setDataDirty();

                // Open settings panel to Updates tab
                HudManager::getInstance().getSettingsHud().showUpdatesTab();
                return;
            }

            if (m_hoveredButton == NotificationButton::DISMISS) {
                UpdateChecker& checker = UpdateChecker::getInstance();
                std::string latestVersion = checker.getLatestVersion();
                checker.setDismissedVersion(latestVersion);
                DEBUG_INFO_F("VersionWidget: Update notification dismissed for version %s", latestVersion.c_str());

                // Hide the widget and clear notification state
                m_showingUpdateNotification = false;
                m_bVisible = false;
                m_hoveredButton = NotificationButton::NONE;

                // Mark dirty to persist the dismissed version (deferred to leave-track / Save).
                SettingsManager::getInstance().markDirty();
                return;
            }
        }
        return;  // Don't process game input while notification is showing
    }

    // Handle donation nudge button hover and clicks (not during game)
    if (m_showingDonationNudge && !m_gameActive) {
        CursorPosition cursor = input.getCursorPosition();
        mapCursorToHudSpace(cursor.x, cursor.y);

        NotificationButton oldHover = m_hoveredButton;
        m_hoveredButton = NotificationButton::NONE;

        if (cursor.isValid) {
            float kofiLeft = m_viewButtonLeft + m_fOffsetX;
            float kofiTop = m_viewButtonTop + m_fOffsetY;
            if (cursor.x >= kofiLeft && cursor.x <= kofiLeft + m_viewButtonWidth &&
                cursor.y >= kofiTop && cursor.y <= kofiTop + m_viewButtonHeight) {
                m_hoveredButton = NotificationButton::KOFI;
            }

            float dismissLeft = m_dismissButtonLeft + m_fOffsetX;
            float dismissTop = m_dismissButtonTop + m_fOffsetY;
            if (cursor.x >= dismissLeft && cursor.x <= dismissLeft + m_dismissButtonWidth &&
                cursor.y >= dismissTop && cursor.y <= dismissTop + m_dismissButtonHeight) {
                m_hoveredButton = NotificationButton::NUDGE_DISMISS;
            }
        }

        if (m_hoveredButton != oldHover) {
            setDataDirty();
        }

        if (isLeftClick) {
            if (m_hoveredButton == NotificationButton::KOFI) {
#if GAME_HAS_ANALYTICS
                AnalyticsManager::getInstance().trackEvent("link_clicked", {{"target", "donate"}, {"source", "update_nudge"}});
#endif
                ShellExecuteA(nullptr, "open", KOFI_URL, nullptr, nullptr, SW_SHOWNORMAL);
            }
            // Both buttons dismiss the nudge
            if (m_hoveredButton == NotificationButton::KOFI ||
                m_hoveredButton == NotificationButton::NUDGE_DISMISS) {
                m_showingDonationNudge = false;
                m_bVisible = false;
                m_hoveredButton = NotificationButton::NONE;
                setDataDirty();
                return;
            }
        }
        return;  // Don't process game input while nudge is showing
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

    m_showingUpdateNotification = true;
    m_bVisible = true;
    setDataDirty();
}

void VersionWidget::showDonationNudge() {
    if (m_showingDonationNudge || m_showingUpdateNotification) return;
    m_showingDonationNudge = true;
    m_bVisible = true;
    setDataDirty();
}

void VersionWidget::rebuildLayout() {
    if (m_gameActive) return;   // game handles its own layout
    // BOX-MODEL: one source of geometry. This used to duplicate every mode's
    // sizing arithmetic to reposition in place, and the two copies were kept in
    // step by comment ("must match rebuildRenderData"). The widget is a handful
    // of strings; rebuilding is cheaper than the drift.
    rebuildRenderData();
}

BaseHud::PanelPlan VersionWidget::notifyPlan(const ScaledDimensions& dim,
                                             float contentWidth, int rows,
                                             float extraH, bool stackMember) const {
    PanelWant want;
    want.sectionH = { static_cast<float>(rows) * dim.lineHeightNormal + extraH };
    want.captionW = planTitleWidth(dim, "Version");
    if (stackMember) {
        // THE PLAIN VERSION ROW is a centre-stack panel and takes the stack's
        // width rule whole: the shared minimum owns the width and the string
        // does not compete for it. It used to pass its own text width HERE too,
        // which is invisible at shipped padding (the string is 19 normal chars
        // against 14 large ones of interior, so the minimum wins) and 78px of
        // divergence once [Advanced] padding grew, because a stated content
        // width carries the padding past the minimum while its neighbours,
        // which state none, sit on it. See BaseHud::wantCenterStackWidth.
        //
        // The string fitting that interior is therefore a CONSTRAINT now, not a
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

    // Check if we should show update notification
    bool showNotification = m_showingUpdateNotification &&
                           UpdateChecker::getInstance().shouldShowUpdateNotification();

    if (showNotification) {
        // ===== NOTIFICATION MODE: Show update message with buttons on separate row =====
        std::string latestVersion = UpdateChecker::getInstance().getLatestVersion();

        // Calculate text width for update message
        char displayText[64];
        snprintf(displayText, sizeof(displayText), "MXBMRP3 %s available!", latestVersion.c_str());
        const int textLength = static_cast<int>(strlen(displayText));
        const float textWidth = PluginUtils::calculateMonospaceTextWidth(textLength, dim.fontSize);

        // Button dimensions
        const float charWidth = PluginUtils::calculateMonospaceTextWidth(1, dim.fontSize);
        // The [button] terms, all four sides: box = insets around the label
        // row, gap = the SUM of facing margins (1 char at shipped defaults,
        // exactly the old hard-coded gap). The box was a bare text row, so
        // the vertical terms acted on nothing.
        const PlanButtonTerms bt = planButtonTerms(dim);
        const float buttonGap = bt.gap;
        const float viewButtonWidth = charWidth * VIEW_BUTTON_CHARS + bt.insetL + bt.insetR;
        const float dismissButtonWidth = charWidth * DISMISS_BUTTON_CHARS + bt.insetL + bt.insetR;
        const float buttonHeight = bt.insetT + dim.lineHeightNormal + bt.insetB;

        // Width is max of text row or button row
        const float buttonRowWidth = viewButtonWidth + buttonGap + dismissButtonWidth;
        const float contentWidth = std::fmax(textWidth, buttonRowWidth);
        // BOX-MODEL: the plan owns padding, chrome and the content origin. The
        // centre-stack width is a MINIMUM on the panel (widthSetBy 'min'), not
        // a padding sum folded into the content.
        // THE JUNCTION PLUS the box's own margin and insets. marginT alone was not
        // enough: it is the button BOX's margin and defaults to zero, so the row
        // still sat flush against the message. The seam above a button row is the
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
        addPlanTitle(placed, "Version", this->getFont(FontCategory::TITLE),
                     this->getColor(ColorSlot::PRIMARY));
        float currentY = placed.contentY();

        // Render update available text (centered on first row)
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
        m_viewButtonLeft = viewBtnX;
        m_viewButtonTop = viewBtnY;
        m_viewButtonWidth = viewButtonWidth;
        m_viewButtonHeight = buttonHeight;

        bool isViewHovered = (m_hoveredButton == NotificationButton::VIEW);

        // Hover is carried by the chip's alpha, not by a second label colour.
        addStateButton(viewBtnX, viewBtnY, viewButtonWidth, buttonHeight,
                       "View in Settings", viewBtnY + bt.insetT, dim.fontSize,
                       this->getColor(ColorSlot::ACCENT),
                       isViewHovered ? ButtonState::Hovered : ButtonState::Idle);

        // ===== Dismiss Button (negative color) =====
        float dismissBtnX = viewBtnX + viewButtonWidth + buttonGap;
        float dismissBtnY = row2Y;

        // Store button bounds for click detection (before offset)
        m_dismissButtonLeft = dismissBtnX;
        m_dismissButtonTop = dismissBtnY;
        m_dismissButtonWidth = dismissButtonWidth;
        m_dismissButtonHeight = buttonHeight;

        bool isDismissHovered = (m_hoveredButton == NotificationButton::DISMISS);

        addStateButton(dismissBtnX, dismissBtnY, dismissButtonWidth, buttonHeight,
                       "Dismiss", dismissBtnY + bt.insetT, dim.fontSize,
                       this->getColor(ColorSlot::NEGATIVE),
                       isDismissHovered ? ButtonState::Hovered : ButtonState::Idle);

        // Set bounds for the whole widget
        setBounds(startX, startY, startX + backgroundWidth, startY + backgroundHeight);

    } else if (m_showingDonationNudge) {
        // ===== DONATION NUDGE: shown once after a successful auto-update install =====
        const char* nudgeText = "MXBMRP3 updated successfully!";
        const int nudgeTextLen = static_cast<int>(strlen(nudgeText));
        const float nudgeTextWidth = PluginUtils::calculateMonospaceTextWidth(nudgeTextLen, dim.fontSize);

        const float charWidth = PluginUtils::calculateMonospaceTextWidth(1, dim.fontSize);
        const PlanButtonTerms bt = planButtonTerms(dim);
        const float buttonGap = bt.gap;
        const float kofiButtonWidth = charWidth * KOFI_BUTTON_CHARS + bt.insetL + bt.insetR;
        const float nudgeDismissButtonWidth = charWidth * NUDGE_DISMISS_BUTTON_CHARS + bt.insetL + bt.insetR;
        const float buttonHeight = bt.insetT + dim.lineHeightNormal + bt.insetB;
        const float buttonRowWidth = kofiButtonWidth + buttonGap + nudgeDismissButtonWidth;
        const float contentWidth = std::fmax(nudgeTextWidth, buttonRowWidth);
        // The junction, then the box's own terms -- same seam as the update
        // notification above; the reasoning is written out there.
        const float junctionY = panelGapY(dim);
        const PanelPlan p = notifyPlan(dim, contentWidth, /*rows=*/2,
                                       junctionY + bt.marginT + bt.insetT + bt.insetB);
        const float backgroundWidth = p.width();
        const float backgroundHeight = p.height();

        float startX = centerAnchoredPanelLeft(backgroundWidth);
        float startY = 0.01f;

        PanelPlan placed = p;
        addPlanBackground(placed, startX, startY);
        addPlanTitle(placed, "Version", this->getFont(FontCategory::TITLE),
                     this->getColor(ColorSlot::PRIMARY));
        float currentY = placed.contentY();

        float row1Y = currentY;
        // The CARD's centre, not the panel's (PanelPlan::sectionBoxCenterX).
        float centerX = placed.sectionBoxCenterX();
        addString(nudgeText, centerX, row1Y, Justify::CENTER,
                  this->getFont(FontCategory::NORMAL), this->getColor(ColorSlot::SECONDARY), dim.fontSize);

        float row2Y = row1Y + dim.lineHeightNormal + junctionY + bt.marginT;
        float buttonsStartX = centerX - buttonRowWidth / 2.0f;

        // Ko-fi button (accent color)
        float kofiBtnX = buttonsStartX;
        float kofiBtnY = row2Y;
        m_viewButtonLeft = kofiBtnX;
        m_viewButtonTop = kofiBtnY;
        m_viewButtonWidth = kofiButtonWidth;
        m_viewButtonHeight = buttonHeight;

        bool isKofiHovered = (m_hoveredButton == NotificationButton::KOFI);
        addStateButton(kofiBtnX, kofiBtnY, kofiButtonWidth, buttonHeight,
                       "Support thomas4f", kofiBtnY + bt.insetT, dim.fontSize,
                       this->getColor(ColorSlot::ACCENT),
                       isKofiHovered ? ButtonState::Hovered : ButtonState::Idle);

        // Dismiss button (muted)
        float nudgeDismissBtnX = kofiBtnX + kofiButtonWidth + buttonGap;
        float nudgeDismissBtnY = row2Y;
        m_dismissButtonLeft = nudgeDismissBtnX;
        m_dismissButtonTop = nudgeDismissBtnY;
        m_dismissButtonWidth = nudgeDismissButtonWidth;
        m_dismissButtonHeight = buttonHeight;

        bool isNudgeDismissHovered = (m_hoveredButton == NotificationButton::NUDGE_DISMISS);
        addStateButton(nudgeDismissBtnX, nudgeDismissBtnY, nudgeDismissButtonWidth, buttonHeight,
                       "Dismiss", nudgeDismissBtnY + bt.insetT, dim.fontSize,
                       this->getColor(ColorSlot::NEGATIVE),
                       isNudgeDismissHovered ? ButtonState::Hovered : ButtonState::Idle);

        setBounds(startX, startY, startX + backgroundWidth, startY + backgroundHeight);

    } else {
        // ===== NORMAL MODE: Show plugin version =====

        // Clear notification state if we're visible but notification no longer applies
        if (m_showingUpdateNotification) {
            m_showingUpdateNotification = false;
        }

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
        addPlanTitle(placed, "Version", this->getFont(FontCategory::TITLE),
                     this->getColor(ColorSlot::PRIMARY));
        // Add main text
        // INK-centred in the section's DRAWN BOX, like Timing's time. It used to centre
        // in the content ROW, which is the same place while the card border is
        // symmetric and a cell high when it is not (see PanelPlan::sectionBoxY). Before
        // that it passed the bare row top, leaving addString to centre the glyph CELL
        // -- fine for a row in a TABLE, where every row carries the same
        // 0.11-of-a-cell bias and it cancels, but this row IS the whole body of a
        // one-row panel, so the bias read as the text sitting high in its own box.
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

void VersionWidget::resetToDefaults() {
    m_bVisible = false;    // Hidden by default
    m_bShowTitle = false;  // No title
    setTextureVariant(0);  // No texture by default
    m_fBackgroundOpacity = 1.0f;  // Full opacity
    m_fScale = 1.0f;
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

    // Reset notification state
    m_showingUpdateNotification = false;
    m_showingDonationNudge = false;
    m_hoveredButton = NotificationButton::NONE;

    setDataDirty();
}
