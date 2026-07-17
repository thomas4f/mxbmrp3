// ============================================================================
// hud/version_widget.cpp
// Version widget - displays plugin name and version
// ============================================================================
#include "version_widget.h"

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

static constexpr const char* KOFI_URL = "https://ko-fi.com/thomas4f";


VersionWidget::VersionWidget() {
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

bool VersionWidget::handlesDataType(DataChangeType dataType) const {
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
        rebuildRenderData();
        return;
    }

    // Normal widget update path
    if (isLayoutDirty()) {
        rebuildLayout();
        clearLayoutDirty();
    }

    // Rebuild render data when dirty or on first update
    if (isDataDirty() || m_strings.empty()) {
        rebuildRenderData();
        clearDataDirty();
    }
}

void VersionWidget::handleClickDetection() {
    if (!m_bVisible) return;

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
    if (m_gameActive) {
        // Game handles its own layout
        return;
    }

    // Fast path - only update positions
    auto dim = getScaledDimensions();

    // Check if we should show update notification
    bool showNotification = m_showingUpdateNotification &&
                           UpdateChecker::getInstance().shouldShowUpdateNotification();

    float backgroundWidth, backgroundHeight;
    float contentPaddingH, contentPaddingV;

    if (showNotification) {
        // Notification mode uses full padding
        contentPaddingH = dim.paddingH;
        contentPaddingV = dim.paddingV;

        // "MXBMRP3 " (8 chars) + version string + " available!" (11 chars)
        std::string latestVersion = UpdateChecker::getInstance().getLatestVersion();
        int textLength = 8 + static_cast<int>(latestVersion.length()) + 11;
        const float textWidth = PluginUtils::calculateMonospaceTextWidth(textLength, dim.fontSize);

        // Button dimensions (must match rebuildRenderData)
        const float charWidth = PluginUtils::calculateMonospaceTextWidth(1, dim.fontSize);
        const float buttonGap = charWidth * 1.0f;
        const float viewButtonWidth = charWidth * VIEW_BUTTON_CHARS;
        const float dismissButtonWidth = charWidth * DISMISS_BUTTON_CHARS;

        // Width is max of text row or button row
        const float buttonRowWidth = viewButtonWidth + buttonGap + dismissButtonWidth;
        const float contentWidth = std::fmax(textWidth, buttonRowWidth);
        backgroundWidth = contentPaddingH + contentWidth + contentPaddingH;
        // Two rows: text + buttons
        backgroundHeight = contentPaddingV + dim.lineHeightNormal + dim.lineHeightNormal + contentPaddingV;
    } else if (m_showingDonationNudge) {
        contentPaddingH = dim.paddingH;
        contentPaddingV = dim.paddingV;

        // "MXBMRP3 updated successfully!" (29 chars)
        const float nudgeTextWidth = PluginUtils::calculateMonospaceTextWidth(29, dim.fontSize);
        const float charWidth = PluginUtils::calculateMonospaceTextWidth(1, dim.fontSize);
        const float buttonGap = charWidth * 1.0f;
        const float kofiButtonWidth = charWidth * KOFI_BUTTON_CHARS;
        const float nudgeDismissButtonWidth = charWidth * NUDGE_DISMISS_BUTTON_CHARS;
        const float buttonRowWidth = kofiButtonWidth + buttonGap + nudgeDismissButtonWidth;
        const float contentWidth = std::fmax(nudgeTextWidth, buttonRowWidth);
        backgroundWidth = contentPaddingH + contentWidth + contentPaddingH;
        backgroundHeight = contentPaddingV + dim.lineHeightNormal + dim.lineHeightNormal + contentPaddingV;
    } else {
        // Normal mode uses full padding for consistency
        contentPaddingH = dim.paddingH;
        contentPaddingV = dim.paddingV;

        // "MXBMRP3 v" (9 chars) + version string
        int textLength = 9 + static_cast<int>(strlen(PLUGIN_VERSION));
        const float textWidth = PluginUtils::calculateMonospaceTextWidth(textLength, dim.fontSize);
        backgroundWidth = contentPaddingH + textWidth + contentPaddingH;
        backgroundHeight = contentPaddingV + dim.lineHeightNormal + contentPaddingV;
    }

    // Base position centers widget at (0.5, 0.01) - offset applied automatically by BaseHud
    float startX = -backgroundWidth / 2.0f;  // Centers around X=0.5 when offset=0.5
    float startY = 0.01f;  // Top of screen

    // Set bounds for drag detection
    setBounds(startX, startY, startX + backgroundWidth, startY + backgroundHeight);

    // Update background quad position
    updateBackgroundQuadPosition(startX, startY, backgroundWidth, backgroundHeight);

    // Position first string (only used in normal mode; notification/nudge rebuilds all strings)
    if (!showNotification && !m_showingDonationNudge) {
        float contentStartX = startX + contentPaddingH;
        float contentStartY = startY + contentPaddingV;
        positionString(0, contentStartX, contentStartY);
    }
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
        const float buttonGap = charWidth * 1.0f;  // Gap between buttons
        const float viewButtonWidth = charWidth * VIEW_BUTTON_CHARS;
        const float dismissButtonWidth = charWidth * DISMISS_BUTTON_CHARS;
        const float buttonHeight = dim.lineHeightNormal;

        // Width is max of text row or button row
        const float buttonRowWidth = viewButtonWidth + buttonGap + dismissButtonWidth;
        const float contentWidth = std::fmax(textWidth, buttonRowWidth);
        const float backgroundWidth = dim.paddingH + contentWidth + dim.paddingH;
        // Two rows: text + buttons
        const float backgroundHeight = dim.paddingV + dim.lineHeightNormal + dim.lineHeightNormal + dim.paddingV;

        // Center widget at top of screen
        float startX = -backgroundWidth / 2.0f;
        float startY = 0.01f;

        // Add background quad
        addBackgroundQuad(startX, startY, backgroundWidth, backgroundHeight);

        // Render update available text (centered on first row)
        float row1Y = startY + dim.paddingV;
        float centerX = startX + backgroundWidth / 2.0f;
        addString(displayText, centerX, row1Y, Justify::CENTER,
                  this->getFont(FontCategory::NORMAL), this->getColor(ColorSlot::SECONDARY), dim.fontSize);

        // Second row: buttons centered
        float row2Y = row1Y + dim.lineHeightNormal;
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

        // View button background
        SPluginQuad_t viewBgQuad;
        float viewBgX = viewBtnX, viewBgY = viewBtnY;
        applyOffset(viewBgX, viewBgY);
        setQuadPositions(viewBgQuad, viewBgX, viewBgY, viewButtonWidth, buttonHeight);
        viewBgQuad.m_iSprite = SpriteIndex::SOLID_COLOR;
        viewBgQuad.m_ulColor = isViewHovered ? this->getColor(ColorSlot::ACCENT)
            : PluginUtils::applyOpacity(this->getColor(ColorSlot::ACCENT), 0.5f);
        m_quads.push_back(viewBgQuad);

        // View button text (center-aligned on button)
        unsigned long viewTextColor = isViewHovered ? this->getColor(ColorSlot::PRIMARY) : this->getColor(ColorSlot::ACCENT);
        addString("View in Settings", viewBtnX + viewButtonWidth / 2.0f, viewBtnY, Justify::CENTER,
                  this->getFont(FontCategory::NORMAL), viewTextColor, dim.fontSize);

        // ===== Dismiss Button (negative color) =====
        float dismissBtnX = viewBtnX + viewButtonWidth + buttonGap;
        float dismissBtnY = row2Y;

        // Store button bounds for click detection (before offset)
        m_dismissButtonLeft = dismissBtnX;
        m_dismissButtonTop = dismissBtnY;
        m_dismissButtonWidth = dismissButtonWidth;
        m_dismissButtonHeight = buttonHeight;

        bool isDismissHovered = (m_hoveredButton == NotificationButton::DISMISS);

        // Dismiss button background
        SPluginQuad_t dismissBgQuad;
        float dismissBgX = dismissBtnX, dismissBgY = dismissBtnY;
        applyOffset(dismissBgX, dismissBgY);
        setQuadPositions(dismissBgQuad, dismissBgX, dismissBgY, dismissButtonWidth, buttonHeight);
        dismissBgQuad.m_iSprite = SpriteIndex::SOLID_COLOR;
        dismissBgQuad.m_ulColor = isDismissHovered ? this->getColor(ColorSlot::NEGATIVE)
            : PluginUtils::applyOpacity(this->getColor(ColorSlot::NEGATIVE), 0.5f);
        m_quads.push_back(dismissBgQuad);

        // Dismiss button text (center-aligned on button)
        unsigned long dismissTextColor = isDismissHovered ? this->getColor(ColorSlot::PRIMARY) : this->getColor(ColorSlot::NEGATIVE);
        addString("Dismiss", dismissBtnX + dismissButtonWidth / 2.0f, dismissBtnY, Justify::CENTER,
                  this->getFont(FontCategory::NORMAL), dismissTextColor, dim.fontSize);

        // Set bounds for the whole widget
        setBounds(startX, startY, startX + backgroundWidth, startY + backgroundHeight);

    } else if (m_showingDonationNudge) {
        // ===== DONATION NUDGE: shown once after a successful auto-update install =====
        const char* nudgeText = "MXBMRP3 updated successfully!";
        const int nudgeTextLen = static_cast<int>(strlen(nudgeText));
        const float nudgeTextWidth = PluginUtils::calculateMonospaceTextWidth(nudgeTextLen, dim.fontSize);

        const float charWidth = PluginUtils::calculateMonospaceTextWidth(1, dim.fontSize);
        const float buttonGap = charWidth * 1.0f;
        const float kofiButtonWidth = charWidth * KOFI_BUTTON_CHARS;
        const float nudgeDismissButtonWidth = charWidth * NUDGE_DISMISS_BUTTON_CHARS;
        const float buttonHeight = dim.lineHeightNormal;
        const float buttonRowWidth = kofiButtonWidth + buttonGap + nudgeDismissButtonWidth;
        const float contentWidth = std::fmax(nudgeTextWidth, buttonRowWidth);
        const float backgroundWidth = dim.paddingH + contentWidth + dim.paddingH;
        const float backgroundHeight = dim.paddingV + dim.lineHeightNormal + dim.lineHeightNormal + dim.paddingV;

        float startX = -backgroundWidth / 2.0f;
        float startY = 0.01f;

        addBackgroundQuad(startX, startY, backgroundWidth, backgroundHeight);

        float row1Y = startY + dim.paddingV;
        float centerX = startX + backgroundWidth / 2.0f;
        addString(nudgeText, centerX, row1Y, Justify::CENTER,
                  this->getFont(FontCategory::NORMAL), this->getColor(ColorSlot::SECONDARY), dim.fontSize);

        float row2Y = row1Y + dim.lineHeightNormal;
        float buttonsStartX = centerX - buttonRowWidth / 2.0f;

        // Ko-fi button (accent color)
        float kofiBtnX = buttonsStartX;
        float kofiBtnY = row2Y;
        m_viewButtonLeft = kofiBtnX;
        m_viewButtonTop = kofiBtnY;
        m_viewButtonWidth = kofiButtonWidth;
        m_viewButtonHeight = buttonHeight;

        bool isKofiHovered = (m_hoveredButton == NotificationButton::KOFI);
        SPluginQuad_t kofiBgQuad;
        float kofiBgX = kofiBtnX, kofiBgY = kofiBtnY;
        applyOffset(kofiBgX, kofiBgY);
        setQuadPositions(kofiBgQuad, kofiBgX, kofiBgY, kofiButtonWidth, buttonHeight);
        kofiBgQuad.m_iSprite = SpriteIndex::SOLID_COLOR;
        kofiBgQuad.m_ulColor = isKofiHovered ? this->getColor(ColorSlot::ACCENT)
            : PluginUtils::applyOpacity(this->getColor(ColorSlot::ACCENT), 0.5f);
        m_quads.push_back(kofiBgQuad);

        unsigned long kofiTextColor = isKofiHovered ? this->getColor(ColorSlot::PRIMARY) : this->getColor(ColorSlot::ACCENT);
        addString("Support thomas4f", kofiBtnX + kofiButtonWidth / 2.0f, kofiBtnY, Justify::CENTER,
                  this->getFont(FontCategory::NORMAL), kofiTextColor, dim.fontSize);

        // Dismiss button (muted)
        float nudgeDismissBtnX = kofiBtnX + kofiButtonWidth + buttonGap;
        float nudgeDismissBtnY = row2Y;
        m_dismissButtonLeft = nudgeDismissBtnX;
        m_dismissButtonTop = nudgeDismissBtnY;
        m_dismissButtonWidth = nudgeDismissButtonWidth;
        m_dismissButtonHeight = buttonHeight;

        bool isNudgeDismissHovered = (m_hoveredButton == NotificationButton::NUDGE_DISMISS);
        SPluginQuad_t nudgeDismissBgQuad;
        float nudgeDismissBgX = nudgeDismissBtnX, nudgeDismissBgY = nudgeDismissBtnY;
        applyOffset(nudgeDismissBgX, nudgeDismissBgY);
        setQuadPositions(nudgeDismissBgQuad, nudgeDismissBgX, nudgeDismissBgY, nudgeDismissButtonWidth, buttonHeight);
        nudgeDismissBgQuad.m_iSprite = SpriteIndex::SOLID_COLOR;
        nudgeDismissBgQuad.m_ulColor = isNudgeDismissHovered ? this->getColor(ColorSlot::NEGATIVE)
            : PluginUtils::applyOpacity(this->getColor(ColorSlot::NEGATIVE), 0.5f);
        m_quads.push_back(nudgeDismissBgQuad);

        unsigned long nudgeDismissTextColor = isNudgeDismissHovered ? this->getColor(ColorSlot::PRIMARY) : this->getColor(ColorSlot::NEGATIVE);
        addString("Dismiss", nudgeDismissBtnX + nudgeDismissButtonWidth / 2.0f, nudgeDismissBtnY, Justify::CENTER,
                  this->getFont(FontCategory::NORMAL), nudgeDismissTextColor, dim.fontSize);

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
        const float backgroundWidth = dim.paddingH + textWidth + dim.paddingH;
        const float backgroundHeight = dim.paddingV + dim.lineHeightNormal + dim.paddingV;

        // Base position centers widget at (0.5, 0.01) - offset applied automatically by BaseHud
        float startX = -backgroundWidth / 2.0f;  // Centers around X=0.5 when offset=0.5
        float startY = 0.01f;  // Top of screen

        // Add background quad (opaque black)
        addBackgroundQuad(startX, startY, backgroundWidth, backgroundHeight);

        // Add main text
        float contentStartX = startX + dim.paddingH;
        float contentStartY = startY + dim.paddingV;

        addString(displayText, contentStartX, contentStartY, Justify::LEFT,
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
    setPosition(0.5f, 0.01173f);  // Top center (0.5 is screen center)

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
