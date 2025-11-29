// ============================================================================
// hud/version_widget.h
// Version widget - displays plugin name and version
// ============================================================================
#pragma once

#include "base_hud.h"
#include "../core/plugin_constants.h"
#include <array>

class VersionWidget : public BaseHud {
public:
    VersionWidget();
    virtual ~VersionWidget() = default;

    void update() override;
    bool handlesDataType(DataChangeType dataType) const override;
    void resetToDefaults();

    // Allow SettingsManager to access private members
    friend class SettingsManager;

protected:
    void rebuildLayout() override;

private:
    void rebuildRenderData() override;

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

    // Click detection for Easter egg trigger
    static constexpr int CLICKS_TO_ACTIVATE = 5;
    static constexpr long long CLICK_TIMEOUT_US = 2000000;  // 2 seconds in microseconds

    int m_clickCount = 0;
    long long m_lastClickTimeUs = 0;
    bool m_wasLeftPressed = false;

    // Game state
    bool m_gameActive = false;
    bool m_ballLaunched = false;
    bool m_gameOver = false;

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
    void startGame();
    void updateGame(float deltaTime);
    void resetBall();
    void launchBall();
    void advanceLevel();
    float getCurrentBallSpeed() const;
    bool checkBrickCollision(float newX, float newY);
    void renderGame();
    void exitGame();
};
