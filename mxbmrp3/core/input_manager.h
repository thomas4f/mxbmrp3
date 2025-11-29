// ============================================================================
// core/input_manager.h
// Handles keyboard and mouse input for HUD interaction and dragging
// ============================================================================
#pragma once

#include "plugin_constants.h"
#include <windows.h>  // For HWND type

// Normalized UI coordinates where:
// - (0,0) = top-left of 16:9 UI area
// - (1,1) = bottom-right of 16:9 UI area
// - Values can extend beyond [0,1] range on ultrawide/superwide displays
//   Example on 21:9 display: x range is approximately [-0.17, 1.17]
//   Example on 32:9 display: x range is approximately [-0.44, 1.44]
struct CursorPosition {
    float x;
    float y;
    bool isValid;

    CursorPosition() : x(0.0f), y(0.0f), isValid(false) {}
};

struct MouseButton {
    bool isPressed;
    bool wasPressed;

    MouseButton() : isPressed(false), wasPressed(false) {}

    bool isClicked() const { return isPressed && !wasPressed; }
    bool isReleased() const { return !isPressed && wasPressed; }
};

struct KeyboardKey {
    bool isPressed;
    bool wasPressed;

    KeyboardKey() : isPressed(false), wasPressed(false) {}

    bool isClicked() const { return isPressed && !wasPressed; }
    bool isReleased() const { return !isPressed && wasPressed; }
};

// Window bounds in UI coordinate space
// On 16:9 displays: left=0, top=0, right=1, bottom=1
// On 21:9 displays (pillarboxed): left=-0.17, top=0, right=1.17, bottom=1
// On narrower displays (letterboxed): left=0, top<0, right=1, bottom>1
struct WindowBounds {
    float left;
    float top;
    float right;
    float bottom;

    WindowBounds() : left(0.0f), top(0.0f), right(1.0f), bottom(1.0f) {}
};

class InputManager {
public:
    static InputManager& getInstance();

    void initialize();
    void shutdown();

    // Call once per frame before any HUDs process input
    void updateFrame();

    // Query input state (fast, uses cached frame data)
    bool isCursorEnabled() const { return m_bCursorEnabled; }
    bool shouldShowCursor() const { return m_bShouldShowCursor && !m_bCursorSuppressed; }

    // Allow HUDs to suppress cursor rendering (e.g., during full-screen overlays)
    void setCursorSuppressed(bool suppressed) { m_bCursorSuppressed = suppressed; }
    const CursorPosition& getCursorPosition() const { return m_cursorPosition; }
    const MouseButton& getLeftButton() const { return m_leftButton; }
    const MouseButton& getRightButton() const { return m_rightButton; }
    const WindowBounds& getWindowBounds() const { return m_windowBounds; }

    // Query window dimensions (for resize detection)
    int getWindowWidth() const { return m_windowWidth; }
    int getWindowHeight() const { return m_windowHeight; }

    // Query keyboard state
    const KeyboardKey& getF1Key() const { return m_f1Key; }
    const KeyboardKey& getF2Key() const { return m_f2Key; }
    const KeyboardKey& getF3Key() const { return m_f3Key; }
    const KeyboardKey& getF4Key() const { return m_f4Key; }
    const KeyboardKey& getF5Key() const { return m_f5Key; }
    const KeyboardKey& getF6Key() const { return m_f6Key; }
    const KeyboardKey& getF7Key() const { return m_f7Key; }
    const KeyboardKey& getF8Key() const { return m_f8Key; }
    const KeyboardKey& getF9Key() const { return m_f9Key; }
    const KeyboardKey& getOem3Key() const { return m_oem3Key; }  // VK_OEM_3: `~ on US, § on some EU layouts
    const KeyboardKey& getOem5Key() const { return m_oem5Key; }  // VK_OEM_5: \| on US layout
    bool isAnyModifierKeyPressed() const;  // Check if Shift/Ctrl/Alt are pressed

    // Force window information refresh (useful for detecting resizes at run start/stop)
    void forceWindowRefresh();

private:
    InputManager() : m_bInitialized(false), m_gameWindow(nullptr),
        m_bCursorEnabled(false), m_bWasCursorEnabled(false),
        m_bShouldShowCursor(false), m_bWasCursorVisible(false), m_bCursorSuppressed(false),
        m_fLastMouseX(0.0f), m_fLastMouseY(0.0f),
        m_framesSinceLastMovement(0), m_windowWidth(0), m_windowHeight(0) {}
    ~InputManager() { shutdown(); }
    InputManager(const InputManager&) = delete;
    InputManager& operator=(const InputManager&) = delete;

    // Internal update helpers
    void updateCursorEnabled();
    void updateCursorPosition();
    void updateMouseButtons();
    void updateKeyboardKeys();
    void updateCursorVisibility();  // New: tracks mouse movement and auto-hide
    void refreshWindowInformation();

    HWND m_gameWindow;
    int m_windowWidth;
    int m_windowHeight;

    // Frame state
    bool m_bInitialized;
    bool m_bCursorEnabled;
    bool m_bWasCursorEnabled;  // Track cursor enable/disable transitions
    bool m_bShouldShowCursor;   // True if cursor should be visible (based on movement)
    bool m_bWasCursorVisible;   // Track cursor visibility transitions (for window refresh on show)
    bool m_bCursorSuppressed;   // True if HUD is suppressing cursor (e.g., full-screen overlay)
    CursorPosition m_cursorPosition;
    MouseButton m_leftButton;
    MouseButton m_rightButton;
    WindowBounds m_windowBounds;

    // Keyboard keys
    KeyboardKey m_f1Key;
    KeyboardKey m_f2Key;
    KeyboardKey m_f3Key;
    KeyboardKey m_f4Key;
    KeyboardKey m_f5Key;
    KeyboardKey m_f6Key;
    KeyboardKey m_f7Key;
    KeyboardKey m_f8Key;
    KeyboardKey m_f9Key;
    KeyboardKey m_oem3Key;  // VK_OEM_3: `~ on US, § on some EU layouts
    KeyboardKey m_oem5Key;  // VK_OEM_5: \| on US layout

    // Mouse movement tracking for auto-hide
    float m_fLastMouseX;
    float m_fLastMouseY;
    int m_framesSinceLastMovement;

    // Constants
    static constexpr float ASPECT_RATIO = PluginConstants::UI_ASPECT_RATIO;
    static constexpr int CURSOR_HIDE_FRAMES = 120;  // Frames of inactivity before hiding cursor (~2 seconds at 60fps)
    static constexpr float MOVEMENT_THRESHOLD = 0.001f;  // Minimum movement to count as "moved"
};
