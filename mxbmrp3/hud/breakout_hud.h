// ============================================================================
// hud/breakout_hud.h
// The hidden easter-egg mini-game (Breakout), started by five clicks on the
// settings footer's About button (SettingsHud, ClickRegion::VERSION_CLICK).
//
// Not a panel: no settings row, no INI section, no position, no dragging,
// never on the companion window. Hidden until the game starts and hidden
// again when it ends, so it draws nothing the rest of the time. The high
// score is StatsManager's (Settings > Stats, the Brick Breaker achievement).
// HudManager::isHeldBack exempts it from the hide-all and widget toggles
// while it runs.
// ============================================================================
#pragma once

#include "base_hud.h"
#include "../core/plugin_constants.h"
#include <array>

class BreakoutHud : public BaseHud {
public:
    bool rendersOnCompanion() const override { return false; }
    MotionStyle motionStyle() const override { return MotionStyle::NONE; }

    BreakoutHud();
    virtual ~BreakoutHud() = default;

    void update() override;
    bool handlesDataType(DataChangeType dataType) const override;

    // Start the game (called from SettingsHud)
    void startGame();

    // Check if game is active (cursor handling, HudManager::isHeldBack)
    bool isGameActive() const { return m_gameActive; }

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

    // Click detection for game input (ball launch / exit)
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

    // Game area bounds (centred on screen at game start)
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
