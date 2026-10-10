// ============================================================================
// hud/version_widget.h
// Version widget - displays plugin name and version
//
// POPUPS. The same panel turns into a one-line message with its buttons for the
// messages that wait for the player, highest first:
//   update available  "MXBMRP3 1.33 available!"     View in Settings / Dismiss
//   updated           "Updated to 1.32. Open ..."   What's New / Dismiss
//   welcome           "Welcome! The key under ..."  Dismiss
// The first is UpdateChecker's; the other two are owed by SystemMessages
// (core/system_messages.h), which also remembers which were answered.
//
// THE COUNTDOWN: Dismiss reads "Dismiss (5)" and counts down; at zero the
// popup goes on its own. It counts only while the popup is actually drawn and
// the settings menu is shut, so a popup that appears in a menu or behind the
// panel is not spent unseen. Running out is NOT a Dismiss: an update notice
// comes back at the next launch, while "Updated to" is told once either way.
// The welcome has no countdown and no settings button: it stays up, on track
// too, until the player answers it (Dismiss, or the settings menu opened the
// way they will open it from then on - its button or its key). The button is sized to its widest label so it
// does not shrink as the number falls.
// ============================================================================
#pragma once

#include "base_hud.h"
#include "../core/plugin_constants.h"
#include <array>
#include <chrono>

class VersionWidget : public BaseHud {
public:
    VersionWidget();
    virtual ~VersionWidget() = default;

    void update() override;
    bool handlesDataType(DataChangeType dataType) const override;
    void resetToDefaults();

    // Start the easter egg game (called from SettingsHud)
    void startGame();

    // Check if game is active (used by HudManager to bypass widgets toggle)
    bool isGameActive() const { return m_gameActive; }
#ifdef MXBMRP3_TEST_BUILD
    // The two terms the notification button row is laid out from, read from the
    // same source rebuildRenderData() reads them from, so a test can assert the
    // RULE (a text row, then the junction) instead of a pixel count that moves
    // with the font. See MXBMRP3_Test_VersionRowTerms.
    float testRowHeight() const { return getScaledDimensions().lineHeightNormal; }
    float testJunctionY() const { return panelGapY(getScaledDimensions()); }
    // The popup countdown: the second the Dismiss label shows (-1 before the
    // first tick), time on screen added without waiting it out, and a Dismiss
    // click without a cursor to aim.
    int testCountdown() const { return m_countdownShown; }
    void testAdvancePopup(float ms) { m_popupShownMs += ms; }
    void testDismiss() { m_hoveredButton = NotificationButton::DISMISS; endPopup(/*answered=*/true); }
#endif

    // THE BROKEN-INSTALL NOTICE (HudManager::buildInstallWarning): `count` rows
    // laid out as this panel lays out its popup message, first row in the
    // NEGATIVE colour, into `outQuads`. Without fonts no string can draw, so the
    // text is core/pixel_text.h blocks placed on the real font's character cell:
    // the same width per character, the cap height of the shipped NORMAL font.
    // `realText` writes the rows as strings into `outStrings` instead, so the
    // two can be compared (companion_demo "brokeninstall real"). The widget's
    // own view is rebuilt on its next update; panelRect() holds the notice's
    // until then.
    void buildInstallNotice(const char* const* rows, int count, bool realText,
                            std::vector<SPluginQuad_t>& outQuads,
                            std::vector<SPluginString_t>& outStrings);

    // Update notification mode - auto-enables widget when update is available
    void showUpdateNotification();
    // A popup is up (for the tests): 0 none, 1 update, 2 updated, 3 welcome.
    int popupKind() const { return static_cast<int>(m_popup); }
    // The player's own on/off for this widget. A popup forces the widget on;
    // this is the value it goes back to, and what a profile captures, so a save
    // or a profile switch mid-popup never stores the popup's switch.
    bool visibleSetting() const {  // vis-gate: the setting itself, popup aside
        return m_popup != Popup::None ? m_visibleBeforePopup : m_bVisible.load();
    }
    // A profile applied (a switch, a reset, a load) while a popup is up: its
    // value is what the popup restores, and the popup stays on screen.
    void onVisibilityApplied();

    // Allow SettingsManager to access private members
    friend class SettingsManager;

protected:
    void rebuildLayout() override;

private:
    void rebuildRenderData() override;
    // The shared box-model plan for all three modes: content width + row count
    // in. `stackMember` picks which width rule applies -- the centre-stack
    // contract for the plain version row (the stack minimum owns the width), or
    // content-sized for the update popup, which carries a button row the stack
    // width cannot hold. See BaseHud::wantCenterStackWidth.
    PanelPlan notifyPlan(const ScaledDimensions& dim, float contentWidth,
                         int rows, float extraH = 0.0f,
                         bool stackMember = false) const;

    // One row of block text, centred on centerX with its ink centred in the row
    // the font would give the same string. See buildInstallNotice.
    void addPixelText(const char* text, float centerX, float rowTop,
                      float fontSize, unsigned long color);

    // Mini-game constants
    static constexpr int BRICK_COLS = 8;
    static constexpr int BRICK_ROWS = 4;
    static constexpr int TOTAL_BRICKS = BRICK_COLS * BRICK_ROWS;

    // Game constants (in normalized screen coordinates per second)
    static constexpr float BALL_SPEED_BASE = 0.35f;
    static constexpr float BALL_SPEED_INCREMENT = 0.05f;  // Speed increase per level
    static constexpr float BALL_SPEED_MAX = 0.80f;        // Cap to keep game playable
    static constexpr float PADDLE_WIDTH = 0.08f;
    static constexpr float PADDLE_HEIGHT = 0.012f;
    static constexpr float BALL_SIZE = 0.010f;
    static constexpr float BRICK_WIDTH = 0.04f;
    static constexpr float BRICK_HEIGHT = 0.015f;
    static constexpr float BRICK_GAP = 0.004f;
    static constexpr float GAME_AREA_WIDTH = 0.40f;
    static constexpr float GAME_AREA_HEIGHT = 0.35f;

    // Popup buttons: each sized to its label plus a character of padding each
    // side; Dismiss to its widest countdown label, "Dismiss (5)".
    static constexpr int BUTTON_PAD_CHARS = 2;
    static constexpr int DISMISS_BUTTON_CHARS = 11 + BUTTON_PAD_CHARS;
    static constexpr int POPUP_COUNTDOWN_MS = 5000;

    // Click detection for game input (ball launch / exit)
    bool m_wasLeftPressed = false;

    // Update notification state. Atomic: showUpdateNotification() runs on the
    // UpdateChecker worker thread while the game thread reads it every frame.
    std::atomic<bool> m_showingUpdateNotification = false;  // True when auto-enabled for update notification

    // The popup showing (see the header comment), and the visibility to restore
    // when the last one ends: the panel is switched on for a popup.
    enum class Popup : uint8_t { None, Update, Updated, Welcome };
    Popup m_popup = Popup::None;
    bool m_visibleBeforePopup = false;   // mt-plain: game thread (update/sync) only
    // Countdown: drawn time spent, the clock of the last tick, and the second
    // last drawn on the Dismiss label (a rebuild only when it changes).
    float m_popupShownMs = 0.0f;
    std::chrono::steady_clock::time_point m_popupLastTick{};
    int m_countdownShown = -1;

    Popup choosePopup() const;
    // Move to the popup now owed (or none): visibility and countdown follow.
    void syncPopup();
    // The countdown tick, and the welcome's goal (the menu opened).
    void tickPopup();
    // The popup ends: `answered` (a button, or the welcome's goal) records it for
    // good; a timeout records only what is told once.
    void endPopup(bool answered);
    void onPrimaryClicked();

    // Notification button state
    enum class NotificationButton { NONE, PRIMARY, DISMISS };
    NotificationButton m_hoveredButton = NotificationButton::NONE;

    // Button bounds (screen coordinates, before offset applied)
    float m_primaryButtonLeft = 0.0f;
    float m_primaryButtonTop = 0.0f;
    float m_primaryButtonWidth = 0.0f;
    float m_primaryButtonHeight = 0.0f;
    float m_dismissButtonLeft = 0.0f;
    float m_dismissButtonTop = 0.0f;
    float m_dismissButtonWidth = 0.0f;
    float m_dismissButtonHeight = 0.0f;

    // Game state
    bool m_gameActive = false;
    bool m_ballLaunched = false;
    bool m_gameOver = false;
    bool m_wasVisibleBeforeGame = false;  // Restore visibility after game ends

    // Ball state
    float m_ballX = 0.0f;
    float m_ballY = 0.0f;
    float m_ballVelX = 0.0f;
    float m_ballVelY = 0.0f;

    // Paddle state (X position follows mouse)
    float m_paddleX = 0.0f;

    // Bricks (true = alive, false = destroyed)
    std::array<bool, TOTAL_BRICKS> m_bricks;
    int m_bricksRemaining = TOTAL_BRICKS;

    // Score, level, and timing
    int m_score = 0;
    int m_level = 1;
    long long m_lastUpdateTimeUs = 0;

    // Game area bounds (calculated from widget position)
    float m_gameLeft = 0.0f;
    float m_gameTop = 0.0f;

    // Game methods
    void handleClickDetection();
    void updateGame(float deltaTime);
    void resetBall();
    void launchBall();
    void advanceLevel();
    float getCurrentBallSpeed() const;
    bool checkBrickCollision(float newX, float newY);
    void renderGame();
    void exitGame();
};
