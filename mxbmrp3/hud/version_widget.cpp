// ============================================================================
// hud/version_widget.cpp
// Version widget - displays plugin name and version
// ============================================================================
#include "version_widget.h"

#include <cstdio>
#include <cstring>
#include <cmath>

#include "../diagnostics/logger.h"
#include "../core/plugin_utils.h"
#include "../core/input_manager.h"
#include "../handlers/draw_handler.h"

using namespace PluginConstants;

// Brick colors by row (from top: red, orange, yellow, green)
namespace BrickColors {
    constexpr unsigned long ROW_0 = PluginUtils::makeColor(220, 60, 60, 255);    // Red
    constexpr unsigned long ROW_1 = PluginUtils::makeColor(240, 150, 50, 255);   // Orange
    constexpr unsigned long ROW_2 = PluginUtils::makeColor(240, 220, 60, 255);   // Yellow
    constexpr unsigned long ROW_3 = PluginUtils::makeColor(80, 200, 80, 255);    // Green
}

// Game UI colors
namespace GameColors {
    constexpr unsigned long PADDLE = PluginUtils::makeColor(200, 200, 200, 255);
    constexpr unsigned long BALL = PluginUtils::makeColor(255, 255, 255, 255);
    constexpr unsigned long BORDER = PluginUtils::makeColor(100, 100, 100, 255);
    constexpr unsigned long BACKGROUND = PluginUtils::makeColor(20, 20, 30, 240);
}

VersionWidget::VersionWidget() {
    initializeWidget("VersionWidget", 1, 1.0f);  // One string, full opacity
    m_bShowTitle = false;  // No title
    m_bVisible = false;    // Hidden by default

    // Position at top center (0.5 is screen center horizontally)
    setPosition(0.5f, 0.01f);

    // Initialize brick array
    m_bricks.fill(true);
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
    // Build render data once on first update
    else if (m_strings.empty()) {
        rebuildRenderData();
    }
}

void VersionWidget::handleClickDetection() {
    if (!m_bVisible) return;

    const InputManager& input = InputManager::getInstance();
    if (!input.isCursorEnabled()) return;

    const MouseButton& leftButton = input.getLeftButton();
    const CursorPosition& cursor = input.getCursorPosition();

    // Detect click (transition from not pressed to pressed)
    bool isLeftPressed = leftButton.isPressed;
    bool isClick = isLeftPressed && !m_wasLeftPressed;
    m_wasLeftPressed = isLeftPressed;

    if (!isClick || !cursor.isValid) return;

    long long currentTimeUs = DrawHandler::getCurrentTimeUs();

    // If game is active, handle game clicks
    if (m_gameActive) {
        if (m_gameOver) {
            // Click to exit
            exitGame();
        } else if (!m_ballLaunched) {
            // Click to launch ball
            launchBall();
        }
        return;
    }

    // Check if click is within widget bounds
    if (!isPointInBounds(cursor.x, cursor.y)) {
        // Click outside - reset counter
        m_clickCount = 0;
        return;
    }

    // Check for timeout - reset counter if too slow
    if (m_clickCount > 0 && (currentTimeUs - m_lastClickTimeUs) > CLICK_TIMEOUT_US) {
        m_clickCount = 0;
    }

    // Increment click counter
    m_clickCount++;
    m_lastClickTimeUs = currentTimeUs;

    // Check if we've reached the activation threshold
    if (m_clickCount >= CLICKS_TO_ACTIVATE) {
        m_clickCount = 0;
        startGame();
    }
}

void VersionWidget::startGame() {
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
    m_clickCount = 0;

    // Restore cursor
    InputManager::getInstance().setCursorSuppressed(false);

    // Clear game render data so widget rebuilds properly
    m_strings.clear();
    m_quads.clear();

    // Force immediate rebuild of widget render data
    rebuildRenderData();
}

void VersionWidget::rebuildLayout() {
    if (m_gameActive) {
        // Game handles its own layout
        return;
    }

    // Fast path - only update positions
    auto dim = getScaledDimensions();

    // Use minimal padding (30% of normal padding)
    const float minPaddingH = dim.paddingH * 0.3f;
    const float minPaddingV = dim.paddingV * 0.3f;

    // Calculate text width for "MXBMRP3 v1.5.0.0" (16 characters)
    const float textWidth = PluginUtils::calculateMonospaceTextWidth(16, dim.fontSize);
    const float backgroundWidth = minPaddingH + textWidth + minPaddingH;
    const float backgroundHeight = minPaddingV + dim.lineHeightNormal + minPaddingV;

    // Base position centers widget at (0.5, 0.01) - offset applied automatically by BaseHud
    float startX = -backgroundWidth / 2.0f;  // Centers around X=0.5 when offset=0.5
    float startY = 0.01f;  // Top of screen

    // Set bounds for drag detection
    setBounds(startX, startY, startX + backgroundWidth, startY + backgroundHeight);

    // Update background quad position
    updateBackgroundQuadPosition(startX, startY, backgroundWidth, backgroundHeight);

    // Position string
    float contentStartX = startX + minPaddingH;
    float contentStartY = startY + minPaddingV;

    positionString(0, contentStartX, contentStartY);
}

void VersionWidget::rebuildRenderData() {
    // Clear existing data
    m_strings.clear();
    m_quads.clear();

    if (m_gameActive) {
        renderGame();
        return;
    }

    auto dim = getScaledDimensions();

    // Use minimal padding (30% of normal padding)
    const float minPaddingH = dim.paddingH * 0.3f;
    const float minPaddingV = dim.paddingV * 0.3f;

    // Calculate text width for "MXBMRP3 v1.5.0.0" (16 characters)
    const float textWidth = PluginUtils::calculateMonospaceTextWidth(16, dim.fontSize);
    const float backgroundWidth = minPaddingH + textWidth + minPaddingH;
    const float backgroundHeight = minPaddingV + dim.lineHeightNormal + minPaddingV;

    // Base position centers widget at (0.5, 0.01) - offset applied automatically by BaseHud
    float startX = -backgroundWidth / 2.0f;  // Centers around X=0.5 when offset=0.5
    float startY = 0.01f;  // Top of screen

    // Add background quad (opaque black)
    addBackgroundQuad(startX, startY, backgroundWidth, backgroundHeight);

    // Create version string
    char versionText[64];
    snprintf(versionText, sizeof(versionText), "MXBMRP3 v%s", PLUGIN_VERSION);

    // Add string
    float contentStartX = startX + minPaddingH;
    float contentStartY = startY + minPaddingV;

    addString(versionText, contentStartX, contentStartY, Justify::LEFT,
              Fonts::ROBOTO_MONO, TextColors::SECONDARY, dim.fontSize);

    // Set bounds for drag detection
    setBounds(startX, startY, startX + backgroundWidth, startY + backgroundHeight);
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
    strncpy_s(scoreString.m_szString, sizeof(scoreString.m_szString), scoreText,
              sizeof(scoreString.m_szString) - 1);
    scoreString.m_afPos[0] = m_gameLeft + 0.01f;
    scoreString.m_afPos[1] = m_gameTop + 0.01f;
    scoreString.m_iFont = Fonts::ROBOTO_MONO;
    scoreString.m_fSize = FontSizes::SMALL;
    scoreString.m_iJustify = Justify::LEFT;
    scoreString.m_ulColor = TextColors::PRIMARY;
    m_strings.push_back(scoreString);

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
        msgString.m_iFont = Fonts::ROBOTO_MONO;
        msgString.m_fSize = FontSizes::NORMAL;
        msgString.m_iJustify = Justify::CENTER;
        msgString.m_ulColor = TextColors::SECONDARY;
        m_strings.push_back(msgString);
    }

    // Set bounds to game area for potential interaction
    setBounds(m_gameLeft - 0.5f, m_gameTop, m_gameLeft + GAME_AREA_WIDTH - 0.5f, m_gameTop + GAME_AREA_HEIGHT);
}

void VersionWidget::resetToDefaults() {
    m_bVisible = false;    // Hidden by default
    m_bShowTitle = false;  // No title
    m_bShowBackgroundTexture = false;  // No texture by default
    m_fBackgroundOpacity = 1.0f;  // Full opacity
    m_fScale = 1.0f;
    setPosition(0.5f, 0.01f);  // Top center (0.5 is screen center)

    // Reset game state and restore cursor if game was active
    if (m_gameActive) {
        InputManager::getInstance().setCursorSuppressed(false);
    }
    m_gameActive = false;
    m_clickCount = 0;

    setDataDirty();
}
