// ============================================================================
// core/input_manager.cpp
// Manages keyboard, mouse, and XInput controller input state
// ============================================================================
#include "input_manager.h"
#include "../diagnostics/logger.h"
#include "../diagnostics/timer.h"
#include "../core/hud_manager.h"
#include "../hud/version_widget.h"
#include <windows.h>

InputManager& InputManager::getInstance() {
    static InputManager instance;
    return instance;
}

void InputManager::initialize() {
    if (m_bInitialized) return;

    DEBUG_INFO("InputManager initializing");

    m_gameWindow = nullptr;
    m_processId = GetCurrentProcessId();  // Cache once - never changes
    m_windowWidth = 0;
    m_windowHeight = 0;
    m_bCursorEnabled = false;
    m_bWasCursorEnabled = false;
    m_bShouldShowCursor = false;
    m_bWasCursorVisible = false;
    m_fLastMouseX = 0.0f;
    m_fLastMouseY = 0.0f;
    m_framesSinceLastMovement = 0;
    m_framesSinceFocusLost = 0;

    m_bInitialized = true;
    DEBUG_INFO("InputManager initialized");
}

void InputManager::shutdown() {
    if (!m_bInitialized) return;

    DEBUG_INFO("InputManager shutting down");

    m_gameWindow = nullptr;
    m_processId = 0;
    m_windowWidth = 0;
    m_windowHeight = 0;
    m_bCursorEnabled = false;
    m_bWasCursorEnabled = false;
    m_bShouldShowCursor = false;
    m_bWasCursorVisible = false;
    m_fLastMouseX = 0.0f;
    m_fLastMouseY = 0.0f;
    m_framesSinceLastMovement = 0;
    m_framesSinceFocusLost = 0;
    m_leftButton = MouseButton();
    m_rightButton = MouseButton();

    m_bInitialized = false;
    DEBUG_INFO("InputManager shutdown complete");
}

void InputManager::updateFrame() {
    if (!m_bInitialized) return;

    // Step 1: Store previous button and key states
    m_leftButton.wasPressed = m_leftButton.isPressed;
    m_rightButton.wasPressed = m_rightButton.isPressed;
    m_f1Key.wasPressed = m_f1Key.isPressed;
    m_f2Key.wasPressed = m_f2Key.isPressed;
    m_f3Key.wasPressed = m_f3Key.isPressed;
    m_f4Key.wasPressed = m_f4Key.isPressed;
    m_f5Key.wasPressed = m_f5Key.isPressed;
    m_f6Key.wasPressed = m_f6Key.isPressed;
    m_f7Key.wasPressed = m_f7Key.isPressed;
    m_f8Key.wasPressed = m_f8Key.isPressed;
    m_f9Key.wasPressed = m_f9Key.isPressed;
    m_oem3Key.wasPressed = m_oem3Key.isPressed;
    m_oem5Key.wasPressed = m_oem5Key.isPressed;

    // Step 2: Check if cursor should be enabled (game is foreground - always enabled)
    updateCursorEnabled();

    // Step 3: Refresh window information when cursor is first enabled
    if (m_bCursorEnabled && !m_bWasCursorEnabled) {
        DEBUG_INFO("Cursor enabled - refreshing window information");
        refreshWindowInformation();

        // Validate all HUD positions to ensure they fit within current window bounds
        DEBUG_INFO("Validating HUD positions after window refresh");
        HudManager::getInstance().validateAllHudPositions();
    }

    // Step 4: Update input state only if cursor is enabled
    if (m_bCursorEnabled) {
        updateMouseButtons();
        updateKeyboardKeys();
        updateCursorPosition();
        updateCursorVisibility();  // Track mouse movement for auto-hide
    }
    else {
        // Clear current button and key states but keep previous states for edge detection
        m_leftButton.isPressed = false;
        m_rightButton.isPressed = false;
        m_f1Key.isPressed = false;
        m_f2Key.isPressed = false;
        m_f3Key.isPressed = false;
        m_f4Key.isPressed = false;
        m_f5Key.isPressed = false;
        m_f6Key.isPressed = false;
        m_f7Key.isPressed = false;
        m_f8Key.isPressed = false;
        m_f9Key.isPressed = false;
        m_oem3Key.isPressed = false;
        m_oem5Key.isPressed = false;
        m_cursorPosition.isValid = false;
        m_bShouldShowCursor = false;
    }

    // Step 5: Refresh window information when cursor becomes visible after being hidden
    // This catches window resizes that happen while the game is running
    // Only do this when the actual game window is focused (not console or dialogs)
    if (m_bShouldShowCursor && !m_bWasCursorVisible && GetForegroundWindow() == m_gameWindow) {
        DEBUG_INFO("Cursor became visible - checking for window changes");
        refreshWindowInformation();

        // Validate all HUD positions to ensure they fit within current window bounds
        DEBUG_INFO("Validating HUD positions after cursor became visible");
        HudManager::getInstance().validateAllHudPositions();
    }

    // Step 6: Track cursor enable/disable and visibility transitions
    m_bWasCursorEnabled = m_bCursorEnabled;
    m_bWasCursorVisible = m_bShouldShowCursor;
}

void InputManager::updateCursorEnabled() {
    // Cursor is enabled when a window belonging to our process is foreground
    // This is more robust than hardcoding the window title "MX Bikes"

    HWND foreground = GetForegroundWindow();
    if (!foreground) {
        // No foreground window - apply debouncing before disabling
        m_framesSinceFocusLost++;
        if (m_framesSinceFocusLost >= FOCUS_DEBOUNCE_FRAMES) {
            if (m_bCursorEnabled) {
                DEBUG_INFO("Cursor disabled: no foreground window");
            }
            m_bCursorEnabled = false;
        }
        return;
    }

    // Check if the foreground window belongs to our process
    DWORD foregroundPid = 0;
    GetWindowThreadProcessId(foreground, &foregroundPid);

    if (foregroundPid == m_processId) {
        // Foreground window is ours - enable cursor
        if (!m_bCursorEnabled) {
            DEBUG_INFO_F("Cursor enabled: process window focused (HWND=%p, PID=%lu)",
                         static_cast<void*>(foreground), foregroundPid);
        }
        m_framesSinceFocusLost = 0;
        m_bCursorEnabled = true;

        // Only update cached game window if this is the actual game window
        // (skip console windows, small dialogs, etc.)
        if (m_gameWindow != foreground) {
            // Skip console window (debug builds)
            HWND consoleWindow = GetConsoleWindow();
            if (foreground == consoleWindow) {
                // Console is focused - keep using existing game window (don't log every frame)
                return;
            }

            RECT rect;
            if (GetClientRect(foreground, &rect)) {
                int w = rect.right - rect.left;
                int h = rect.bottom - rect.top;
                // Accept if window is reasonably large (likely the game window)
                // or if we don't have a valid window yet
                if ((w >= 640 && h >= 480) || !m_gameWindow || !IsWindow(m_gameWindow)) {
                    m_gameWindow = foreground;
                    DEBUG_INFO_F("Game window updated: HWND=%p, size=%dx%d",
                                 static_cast<void*>(foreground), w, h);
                }
                // Don't log small window skips every frame - too noisy
            }
        }
    }
    else {
        // Foreground window is not ours - debounce before disabling
        // This prevents flicker during alt-tab transitions
        m_framesSinceFocusLost++;
        if (m_framesSinceFocusLost >= FOCUS_DEBOUNCE_FRAMES) {
            if (m_bCursorEnabled) {
                DEBUG_INFO_F("Cursor disabled: foreign window focused (HWND=%p, PID=%lu, ours=%lu)",
                             static_cast<void*>(foreground), foregroundPid, m_processId);
            }
            m_bCursorEnabled = false;
        }
    }
}

void InputManager::updateCursorPosition() {
    if (!m_gameWindow) {
        m_cursorPosition.isValid = false;
        return;
    }

    // Get cursor position in screen coordinates
    POINT screenPos;
    if (!GetCursorPos(&screenPos)) {
        m_cursorPosition.isValid = false;
        return;
    }

    // Convert to client coordinates
    POINT clientPos = screenPos;
    if (!ScreenToClient(m_gameWindow, &clientPos)) {
        // ScreenToClient failed - window might be invalid, refresh window info
        DEBUG_INFO("ScreenToClient failed - refreshing window information");
        refreshWindowInformation();
        m_cursorPosition.isValid = false;
        return;
    }

    // Validate window dimensions (should have been set by refreshWindowInformation)
    if (m_windowWidth <= 0 || m_windowHeight <= 0) {
        m_cursorPosition.isValid = false;
        return;
    }

    // Calculate UI area dimensions and convert cursor to normalized UI coordinates
    float windowAspect = static_cast<float>(m_windowWidth) / static_cast<float>(m_windowHeight);
    int uiWidth, uiHeight, uiOffsetX, uiOffsetY;

    if (windowAspect > ASPECT_RATIO) {
        // Pillarboxed (black bars on sides) - ultrawide/superwide displays
        uiHeight = m_windowHeight;
        uiWidth = static_cast<int>(m_windowHeight * ASPECT_RATIO);
        uiOffsetX = (m_windowWidth - uiWidth) / 2;
        uiOffsetY = 0;
    }
    else {
        // Letterboxed (black bars on top/bottom) - narrow displays
        uiWidth = m_windowWidth;
        uiHeight = static_cast<int>(m_windowWidth / ASPECT_RATIO);
        uiOffsetX = 0;
        uiOffsetY = (m_windowHeight - uiHeight) / 2;
    }

    // Safety: Validate calculated UI dimensions to prevent division by zero
    // Integer truncation could result in zero dimensions with very small window sizes
    if (uiWidth <= 0 || uiHeight <= 0) {
        DEBUG_WARN_F("Invalid UI dimensions (%d x %d) calculated from window (%d x %d), cannot update cursor",
                     uiWidth, uiHeight, m_windowWidth, m_windowHeight);
        m_cursorPosition.isValid = false;
        return;
    }

    // Convert cursor to normalized UI coordinates
    // Note: Coordinates naturally extend beyond [0, 1] when cursor is outside UI area
    m_cursorPosition.x = static_cast<float>(clientPos.x - uiOffsetX) / static_cast<float>(uiWidth);
    m_cursorPosition.y = static_cast<float>(clientPos.y - uiOffsetY) / static_cast<float>(uiHeight);
    m_cursorPosition.isValid = true;
}

void InputManager::updateMouseButtons() {
    m_leftButton.isPressed = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    m_rightButton.isPressed = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
}

void InputManager::updateKeyboardKeys() {
    m_f1Key.isPressed = (GetAsyncKeyState(VK_F1) & 0x8000) != 0;
    m_f2Key.isPressed = (GetAsyncKeyState(VK_F2) & 0x8000) != 0;
    m_f3Key.isPressed = (GetAsyncKeyState(VK_F3) & 0x8000) != 0;
    m_f4Key.isPressed = (GetAsyncKeyState(VK_F4) & 0x8000) != 0;
    m_f5Key.isPressed = (GetAsyncKeyState(VK_F5) & 0x8000) != 0;
    m_f6Key.isPressed = (GetAsyncKeyState(VK_F6) & 0x8000) != 0;
    m_f7Key.isPressed = (GetAsyncKeyState(VK_F7) & 0x8000) != 0;
    m_f8Key.isPressed = (GetAsyncKeyState(VK_F8) & 0x8000) != 0;
    m_f9Key.isPressed = (GetAsyncKeyState(VK_F9) & 0x8000) != 0;
    m_oem3Key.isPressed = (GetAsyncKeyState(VK_OEM_3) & 0x8000) != 0;  // `~ on US, § on some EU
    m_oem5Key.isPressed = (GetAsyncKeyState(VK_OEM_5) & 0x8000) != 0;  // \| on US
}

void InputManager::refreshWindowInformation() {
    // Use cached window if valid, otherwise find a window belonging to our process
    HWND gameWindow = m_gameWindow;

    if (!gameWindow || !IsWindow(gameWindow)) {
        // Try to find a visible top-level window belonging to our process
        gameWindow = nullptr;

        // Check foreground window first (most likely candidate)
        HWND foreground = GetForegroundWindow();
        if (foreground) {
            DWORD foregroundPid = 0;
            GetWindowThreadProcessId(foreground, &foregroundPid);
            if (foregroundPid == m_processId) {
                gameWindow = foreground;
            }
        }

        if (!gameWindow) {
            DEBUG_WARN("No valid game window found for refresh");
            m_gameWindow = nullptr;
            m_windowWidth = 0;
            m_windowHeight = 0;
            return;
        }
    }

    // Get window dimensions
    RECT clientRect;
    if (!GetClientRect(gameWindow, &clientRect)) {
        DEBUG_WARN("Failed to get client rect");
        m_gameWindow = nullptr;
        m_windowWidth = 0;
        m_windowHeight = 0;
        return;
    }

    int width = clientRect.right - clientRect.left;
    int height = clientRect.bottom - clientRect.top;

    if (width <= 0 || height <= 0) {
        DEBUG_WARN("Invalid window dimensions");
        m_gameWindow = nullptr;
        m_windowWidth = 0;
        m_windowHeight = 0;
        return;
    }

    // Update cached window information
    m_gameWindow = gameWindow;
    m_windowWidth = width;
    m_windowHeight = height;

    // Calculate window bounds in UI coordinate space whenever dimensions change
    // This allows HUD position validation to work without requiring cursor to be enabled
    float windowAspect = static_cast<float>(m_windowWidth) / static_cast<float>(m_windowHeight);
    int uiWidth, uiHeight, uiOffsetX, uiOffsetY;

    if (windowAspect > ASPECT_RATIO) {
        // Pillarboxed (black bars on sides) - ultrawide/superwide displays
        uiHeight = m_windowHeight;
        uiWidth = static_cast<int>(m_windowHeight * ASPECT_RATIO);
        uiOffsetX = (m_windowWidth - uiWidth) / 2;
        uiOffsetY = 0;
    }
    else {
        // Letterboxed (black bars on top/bottom) - narrow displays
        uiWidth = m_windowWidth;
        uiHeight = static_cast<int>(m_windowWidth / ASPECT_RATIO);
        uiOffsetX = 0;
        uiOffsetY = (m_windowHeight - uiHeight) / 2;
    }

    // Safety: Validate calculated UI dimensions to prevent division by zero
    // Integer truncation could result in zero dimensions with very small window sizes
    if (uiWidth <= 0 || uiHeight <= 0) {
        DEBUG_WARN_F("Invalid UI dimensions (%d x %d) calculated from window (%d x %d), cannot update bounds",
                     uiWidth, uiHeight, m_windowWidth, m_windowHeight);
        // Keep previous bounds or use safe defaults
        return;
    }

    // Update window bounds
    m_windowBounds.left = static_cast<float>(-uiOffsetX) / static_cast<float>(uiWidth);
    m_windowBounds.top = static_cast<float>(-uiOffsetY) / static_cast<float>(uiHeight);
    m_windowBounds.right = static_cast<float>(m_windowWidth - uiOffsetX) / static_cast<float>(uiWidth);
    m_windowBounds.bottom = static_cast<float>(m_windowHeight - uiOffsetY) / static_cast<float>(uiHeight);
}

void InputManager::updateCursorVisibility() {
    // Track mouse movement and clicks to auto-hide cursor after inactivity
    if (!m_cursorPosition.isValid) {
        m_bShouldShowCursor = false;
        return;
    }

    // Always show cursor when settings menu is open or easter egg game is active
    if (HudManager::getInstance().isSettingsVisible() ||
        HudManager::getInstance().getVersionWidget().isGameActive()) {
        m_framesSinceLastMovement = 0;
        m_bShouldShowCursor = true;
        m_fLastMouseX = m_cursorPosition.x;
        m_fLastMouseY = m_cursorPosition.y;
        return;
    }

    // Check if mouse has moved
    float deltaX = m_cursorPosition.x - m_fLastMouseX;
    float deltaY = m_cursorPosition.y - m_fLastMouseY;
    float distanceSq = deltaX * deltaX + deltaY * deltaY;
    bool hasMoved = distanceSq > (MOVEMENT_THRESHOLD * MOVEMENT_THRESHOLD);

    // Check if either mouse button was clicked (extends cursor visibility)
    bool hasClicked = m_leftButton.isClicked() || m_rightButton.isClicked();

    if (hasMoved || hasClicked) {
        // Mouse moved or clicked - reset timer and show cursor
        m_framesSinceLastMovement = 0;
        m_bShouldShowCursor = true;
        m_fLastMouseX = m_cursorPosition.x;
        m_fLastMouseY = m_cursorPosition.y;
    }
    else {
        // Mouse hasn't moved or clicked - increment timer
        m_framesSinceLastMovement++;

        // Hide cursor after timeout
        if (m_framesSinceLastMovement >= CURSOR_HIDE_FRAMES) {
            m_bShouldShowCursor = false;
        }
    }
}

void InputManager::forceWindowRefresh() {
    DEBUG_INFO("Force window refresh requested");
    refreshWindowInformation();

    // Validate all HUD positions after window refresh
    if (HudManager::getInstance().isInitialized()) {
        HudManager::getInstance().validateAllHudPositions();
        DEBUG_INFO("HUD positions validated after forced window refresh");
    }
}

bool InputManager::isAnyModifierKeyPressed() const {
    // Check if Shift, Ctrl, or Alt are currently pressed
    bool shiftPressed = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    bool ctrlPressed = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    bool altPressed = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;

    return shiftPressed || ctrlPressed || altPressed;
}
