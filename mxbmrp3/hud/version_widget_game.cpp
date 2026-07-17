// ============================================================================
// hud/version_widget_game.cpp
// VersionWidget's hidden easter-egg mini-game (breakout): state machine,
// physics, collision, and rendering. Split out of version_widget.cpp — the
// game is ~70% of the widget's code but unrelated to version display (same
// class, second TU; the standings/map HUDs use the same split pattern).
// ============================================================================
#include "version_widget.h"

#include <cmath>

#include "../diagnostics/logger.h"
#include "../core/plugin_utils.h"
#include "../core/input_manager.h"
#include "../core/color_config.h"
#include "../core/stats_manager.h"
#include "../core/hud_manager.h"
#if GAME_HAS_ANALYTICS
#include "../core/analytics_manager.h"
#endif
#include "../handlers/draw_handler.h"

using namespace PluginConstants;

// Brick colors by row (from top: red, orange, yellow, green)
namespace BrickColors {
    constexpr unsigned long ROW_0 = ColorPalette::RED;
    constexpr unsigned long ROW_1 = ColorPalette::ORANGE;
    constexpr unsigned long ROW_2 = ColorPalette::YELLOW;
    constexpr unsigned long ROW_3 = ColorPalette::GREEN;
}

// Game UI colors
namespace GameColors {
    constexpr unsigned long PADDLE = ColorPalette::LIGHT_GRAY;
    constexpr unsigned long BALL = ColorPalette::WHITE;
    constexpr unsigned long BORDER = ColorPalette::GRAY;
    constexpr unsigned long BACKGROUND = PluginUtils::makeColor(20, 20, 30, 240);  // Dark blue-gray with transparency
}

void VersionWidget::startGame() {
    // Save original visibility state and ensure widget is visible for game
    m_wasVisibleBeforeGame = m_bVisible;
    m_bVisible = true;

    // Suppress cursor during game
    InputManager::getInstance().setCursorSuppressed(true);

    m_gameActive = true;
    m_gameOver = false;
    m_ballLaunched = false;
    m_score = 0;
    m_level = 1;
    m_bricksRemaining = TOTAL_BRICKS;
    m_lastUpdateTimeUs = DrawHandler::getCurrentTimeUs();

    // Reset all bricks
    m_bricks.fill(true);

    // Calculate game area position (centered on screen)
    m_gameLeft = 0.5f - (GAME_AREA_WIDTH / 2.0f);
    m_gameTop = 0.5f - (GAME_AREA_HEIGHT / 2.0f);

    // Position paddle at center bottom
    m_paddleX = 0.5f;

    // Reset ball
    resetBall();

    // Force rebuild
    setDataDirty();
}

void VersionWidget::resetBall() {
    // Position ball on top of paddle
    m_ballX = m_paddleX;
    m_ballY = m_gameTop + GAME_AREA_HEIGHT - PADDLE_HEIGHT - BALL_SIZE - 0.005f;
    m_ballVelX = 0.0f;
    m_ballVelY = 0.0f;
    m_ballLaunched = false;
}

void VersionWidget::launchBall() {
    // Launch ball straight up - player controls angle via paddle position on return
    m_ballVelX = 0.0f;
    m_ballVelY = -1.0f;  // Negative Y = upward
    m_ballLaunched = true;
}

void VersionWidget::advanceLevel() {
    m_level++;
    m_bricks.fill(true);
    m_bricksRemaining = TOTAL_BRICKS;
    resetBall();
}

float VersionWidget::getCurrentBallSpeed() const {
    float speed = BALL_SPEED_BASE + (m_level - 1) * BALL_SPEED_INCREMENT;
    return (speed < BALL_SPEED_MAX) ? speed : BALL_SPEED_MAX;
}

void VersionWidget::updateGame(float deltaTime) {
    if (m_gameOver) return;

    const InputManager& input = InputManager::getInstance();
    const CursorPosition& cursor = input.getCursorPosition();

    // Update paddle position to follow mouse
    if (cursor.isValid) {
        m_paddleX = cursor.x;

        // Clamp paddle to game area
        float halfPaddle = PADDLE_WIDTH / 2.0f;
        float minX = m_gameLeft + halfPaddle;
        float maxX = m_gameLeft + GAME_AREA_WIDTH - halfPaddle;
        m_paddleX = std::fmax(minX, std::fmin(maxX, m_paddleX));
    }

    // If ball not launched, keep it on paddle
    if (!m_ballLaunched) {
        m_ballX = m_paddleX;
        m_ballY = m_gameTop + GAME_AREA_HEIGHT - PADDLE_HEIGHT - BALL_SIZE - 0.005f;
        return;
    }

    // Move ball - divide X by aspect ratio so visual speed is consistent in all directions
    // (In normalized coords, 1 unit of X spans more visual distance than 1 unit of Y)
    float speed = getCurrentBallSpeed();
    float newX = m_ballX + (m_ballVelX / UI_ASPECT_RATIO) * speed * deltaTime;
    float newY = m_ballY + m_ballVelY * speed * deltaTime;

    // Ball collision sizes - X is aspect-corrected to match visual appearance
    float ballHalfWidth = (BALL_SIZE / UI_ASPECT_RATIO) / 2.0f;
    float ballHalfHeight = BALL_SIZE / 2.0f;

    // Wall collision (left/right)
    if (newX - ballHalfWidth < m_gameLeft) {
        newX = m_gameLeft + ballHalfWidth;
        m_ballVelX = std::abs(m_ballVelX);
    } else if (newX + ballHalfWidth > m_gameLeft + GAME_AREA_WIDTH) {
        newX = m_gameLeft + GAME_AREA_WIDTH - ballHalfWidth;
        m_ballVelX = -std::abs(m_ballVelX);
    }

    // Wall collision (top)
    if (newY - ballHalfHeight < m_gameTop) {
        newY = m_gameTop + ballHalfHeight;
        m_ballVelY = std::abs(m_ballVelY);
    }

    // Paddle collision - check ball bounds, not just center
    float paddleTop = m_gameTop + GAME_AREA_HEIGHT - PADDLE_HEIGHT;
    float paddleLeft = m_paddleX - PADDLE_WIDTH / 2.0f;
    float paddleRight = m_paddleX + PADDLE_WIDTH / 2.0f;

    if (newY + ballHalfHeight >= paddleTop &&
        m_ballY + ballHalfHeight < paddleTop &&  // Was above paddle last frame
        newX + ballHalfWidth >= paddleLeft && newX - ballHalfWidth <= paddleRight) {

        newY = paddleTop - ballHalfHeight;

        // Calculate hit position: -1 (left edge) to +1 (right edge)
        float hitPos = (newX - m_paddleX) / (PADDLE_WIDTH / 2.0f);
        hitPos = std::fmax(-1.0f, std::fmin(1.0f, hitPos));  // Clamp to [-1, 1]

        // Set ball angle based on hit position
        // Edge hits = steep angle (up to 60°), center hits = mostly vertical
        float maxAngle = 1.05f;  // ~60 degrees in radians
        float angle = hitPos * maxAngle;

        // Convert angle to velocity components (negative Y = upward)
        m_ballVelX = std::sin(angle);
        m_ballVelY = -std::cos(angle);
    }

    // Ball fell below paddle - game over
    if (newY > m_gameTop + GAME_AREA_HEIGHT + 0.02f) {
        m_gameOver = true;
        StatsManager::getInstance().updateBreakoutHighScore(m_score);
        return;
    }

    // Brick collision
    if (checkBrickCollision(newX, newY)) {
        // Collision handled inside function
    }

    // Update ball position
    m_ballX = newX;
    m_ballY = newY;
}

bool VersionWidget::checkBrickCollision(float newX, float newY) {
    // Calculate brick grid starting position
    float brickAreaWidth = BRICK_COLS * (BRICK_WIDTH + BRICK_GAP) - BRICK_GAP;
    float brickStartX = m_gameLeft + (GAME_AREA_WIDTH - brickAreaWidth) / 2.0f;
    float brickStartY = m_gameTop + 0.04f;  // Small margin from top

    // Ball collision sizes - X is aspect-corrected to match visual appearance
    float ballHalfWidth = (BALL_SIZE / UI_ASPECT_RATIO) / 2.0f;
    float ballHalfHeight = BALL_SIZE / 2.0f;

    for (int row = 0; row < BRICK_ROWS; row++) {
        for (int col = 0; col < BRICK_COLS; col++) {
            int index = row * BRICK_COLS + col;
            if (!m_bricks[index]) continue;  // Already destroyed

            float brickX = brickStartX + col * (BRICK_WIDTH + BRICK_GAP);
            float brickY = brickStartY + row * (BRICK_HEIGHT + BRICK_GAP);

            // AABB collision check with aspect-corrected ball width
            if (newX + ballHalfWidth >= brickX &&
                newX - ballHalfWidth <= brickX + BRICK_WIDTH &&
                newY + ballHalfHeight >= brickY &&
                newY - ballHalfHeight <= brickY + BRICK_HEIGHT) {

                // Destroy brick
                m_bricks[index] = false;
                m_bricksRemaining--;
                m_score += (BRICK_ROWS - row) * 10;  // Top rows worth more

                // Determine bounce direction
                float ballCenterX = newX;
                float ballCenterY = newY;
                float brickCenterX = brickX + BRICK_WIDTH / 2.0f;
                float brickCenterY = brickY + BRICK_HEIGHT / 2.0f;

                float dx = ballCenterX - brickCenterX;
                float dy = ballCenterY - brickCenterY;

                // Determine if collision is more horizontal or vertical
                // Use aspect-corrected dimensions for accurate comparison
                float overlapX = (BRICK_WIDTH / 2.0f + ballHalfWidth) - std::abs(dx);
                float overlapY = (BRICK_HEIGHT / 2.0f + ballHalfHeight) - std::abs(dy);

                if (overlapX < overlapY) {
                    m_ballVelX = -m_ballVelX;
                } else {
                    m_ballVelY = -m_ballVelY;
                }

                // Check level complete - advance to next level
                if (m_bricksRemaining <= 0) {
                    advanceLevel();
                }

                return true;  // Only handle one brick collision per frame
            }
        }
    }

    return false;
}

void VersionWidget::exitGame() {
    m_gameActive = false;

    // Restore cursor
    InputManager::getInstance().setCursorSuppressed(false);

    // Restore original visibility state
    m_bVisible = m_wasVisibleBeforeGame;

    // Clear game render data so widget rebuilds properly
    clearStrings();
    m_quads.clear();

    // Force immediate rebuild of widget render data (if still visible)
    if (m_bVisible) {
        rebuildRenderData();
    }
}

void VersionWidget::renderGame() {
    // Game area background
    SPluginQuad_t bgQuad;
    BaseHud::setQuadPositions(bgQuad, m_gameLeft, m_gameTop, GAME_AREA_WIDTH, GAME_AREA_HEIGHT);
    bgQuad.m_iSprite = SpriteIndex::SOLID_COLOR;
    bgQuad.m_ulColor = GameColors::BACKGROUND;
    m_quads.push_back(bgQuad);

    // Border (4 thin rectangles)
    const float borderThickness = 0.003f;

    // Top border
    SPluginQuad_t topBorder;
    BaseHud::setQuadPositions(topBorder, m_gameLeft, m_gameTop, GAME_AREA_WIDTH, borderThickness);
    topBorder.m_iSprite = SpriteIndex::SOLID_COLOR;
    topBorder.m_ulColor = GameColors::BORDER;
    m_quads.push_back(topBorder);

    // Left border
    SPluginQuad_t leftBorder;
    BaseHud::setQuadPositions(leftBorder, m_gameLeft, m_gameTop, borderThickness, GAME_AREA_HEIGHT);
    leftBorder.m_iSprite = SpriteIndex::SOLID_COLOR;
    leftBorder.m_ulColor = GameColors::BORDER;
    m_quads.push_back(leftBorder);

    // Right border
    SPluginQuad_t rightBorder;
    BaseHud::setQuadPositions(rightBorder, m_gameLeft + GAME_AREA_WIDTH - borderThickness, m_gameTop,
                               borderThickness, GAME_AREA_HEIGHT);
    rightBorder.m_iSprite = SpriteIndex::SOLID_COLOR;
    rightBorder.m_ulColor = GameColors::BORDER;
    m_quads.push_back(rightBorder);

    // Render bricks
    float brickAreaWidth = BRICK_COLS * (BRICK_WIDTH + BRICK_GAP) - BRICK_GAP;
    float brickStartX = m_gameLeft + (GAME_AREA_WIDTH - brickAreaWidth) / 2.0f;
    float brickStartY = m_gameTop + 0.04f;

    for (int row = 0; row < BRICK_ROWS; row++) {
        for (int col = 0; col < BRICK_COLS; col++) {
            int index = row * BRICK_COLS + col;
            if (!m_bricks[index]) continue;

            float brickX = brickStartX + col * (BRICK_WIDTH + BRICK_GAP);
            float brickY = brickStartY + row * (BRICK_HEIGHT + BRICK_GAP);

            SPluginQuad_t brickQuad;
            BaseHud::setQuadPositions(brickQuad, brickX, brickY, BRICK_WIDTH, BRICK_HEIGHT);
            brickQuad.m_iSprite = SpriteIndex::SOLID_COLOR;

            // Color by row
            switch (row) {
                case 0: brickQuad.m_ulColor = BrickColors::ROW_0; break;
                case 1: brickQuad.m_ulColor = BrickColors::ROW_1; break;
                case 2: brickQuad.m_ulColor = BrickColors::ROW_2; break;
                default: brickQuad.m_ulColor = BrickColors::ROW_3; break;
            }

            m_quads.push_back(brickQuad);
        }
    }

    // Render paddle
    float paddleLeft = m_paddleX - PADDLE_WIDTH / 2.0f;
    float paddleTop = m_gameTop + GAME_AREA_HEIGHT - PADDLE_HEIGHT - 0.005f;

    SPluginQuad_t paddleQuad;
    BaseHud::setQuadPositions(paddleQuad, paddleLeft, paddleTop, PADDLE_WIDTH, PADDLE_HEIGHT);
    paddleQuad.m_iSprite = SpriteIndex::SOLID_COLOR;
    paddleQuad.m_ulColor = GameColors::PADDLE;
    m_quads.push_back(paddleQuad);

    // Render ball - apply aspect ratio correction to width so it appears square
    float ballWidth = BALL_SIZE / UI_ASPECT_RATIO;
    float ballLeft = m_ballX - ballWidth / 2.0f;
    float ballTop = m_ballY - BALL_SIZE / 2.0f;

    SPluginQuad_t ballQuad;
    BaseHud::setQuadPositions(ballQuad, ballLeft, ballTop, ballWidth, BALL_SIZE);
    ballQuad.m_iSprite = SpriteIndex::SOLID_COLOR;
    ballQuad.m_ulColor = GameColors::BALL;
    m_quads.push_back(ballQuad);

    // Level and score display
    char scoreText[32];
    snprintf(scoreText, sizeof(scoreText), "L%d  SCORE: %d", m_level, m_score);

    SPluginString_t scoreString;
    strncpy_s(scoreString.m_szString, sizeof(scoreString.m_szString), scoreText, _TRUNCATE);
    scoreString.m_afPos[0] = m_gameLeft + 0.01f;
    scoreString.m_afPos[1] = m_gameTop + 0.01f;
    scoreString.m_iFont = this->getFont(FontCategory::NORMAL);
    scoreString.m_fSize = FontSizes::SMALL;
    scoreString.m_iJustify = Justify::LEFT;
    scoreString.m_ulColor = this->getColor(ColorSlot::PRIMARY);
    m_strings.push_back(scoreString);
    m_stringSkipShadow.push_back(false);  // Game text should have shadows

    // Instructions or game state message
    const char* message = nullptr;
    if (m_gameOver) {
        message = "GAME OVER - Click to exit";
    } else if (!m_ballLaunched) {
        message = "Click to launch";
    }

    if (message) {
        SPluginString_t msgString;
        strncpy_s(msgString.m_szString, sizeof(msgString.m_szString), message,
                  sizeof(msgString.m_szString) - 1);
        msgString.m_afPos[0] = m_gameLeft + GAME_AREA_WIDTH / 2.0f;
        msgString.m_afPos[1] = m_gameTop + GAME_AREA_HEIGHT - 0.04f;
        msgString.m_iFont = this->getFont(FontCategory::NORMAL);
        msgString.m_fSize = FontSizes::NORMAL;
        msgString.m_iJustify = Justify::CENTER;
        msgString.m_ulColor = this->getColor(ColorSlot::SECONDARY);
        m_strings.push_back(msgString);
        m_stringSkipShadow.push_back(false);  // Game text should have shadows
    }

    // Set bounds to game area for potential interaction
    setBounds(m_gameLeft - 0.5f, m_gameTop, m_gameLeft + GAME_AREA_WIDTH - 0.5f, m_gameTop + GAME_AREA_HEIGHT);
}

