// ============================================================================
// core/input_manager.h
// Handles keyboard and mouse input for HUD interaction and dragging
// ============================================================================
#pragma once

#include "plugin_constants.h"
#include <windows.h>  // For HWND type
#include <array>
#include <chrono>

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
    // Which of our windows currently has focus and receives cursor input. The
    // companion window is a second interactive surface; input maps to whichever
    // of our windows is foreground so the user can drag/click in either one.
    enum class Surface { Game, Companion };

    static InputManager& getInstance();

    // The surface (game window vs companion window) the cursor is currently
    // mapped into — i.e. the one the user is interacting with this frame.
    Surface getActiveSurface() const { return m_activeSurface; }
#ifdef MXBMRP3_TEST_BUILD
    // Pin the active surface for headless preview/tests of surface-scoped rendering
    // (the companion window never takes focus, so it's never the foreground-derived
    // active surface under Wine). -1 clears the override.
    void testForceActiveSurface(int companion) { m_testForceSurface = companion; }
    // Stand in for the mouse: while `on`, updateFrame() takes this cursor (UI
    // space) and button state (bit 0 left, bit 1 right) instead of polling
    // Win32, so a test can click or drag a HUD through the same frame path the
    // game drives. Edge detection (isClicked/isReleased) still comes from
    // consecutive frames.
    void testInjectMouse(bool on, float x, float y, int buttons) {
        m_testMouseOn = on; m_testMouseX = x; m_testMouseY = y; m_testButtons = buttons;
        m_testCursorIdle = false;   // a move: the cursor is active again
    }
    // The injected mouse stopped longer ago than the hide timeout, so the next
    // frame auto-hides the cursor unless something holds it (the Toggles strip's
    // hover). Without this the injected cursor always shows.
    void testCursorIdle() { m_testCursorIdle = true; }
    // Stand in for window focus: 0 = no game window focused, 1 = focused,
    // 2 = another program focused with the cursor over the companion window,
    // -1 = the real foreground check. Pinned by hotkey_capture_test.cpp.
    void testForceFocus(int focused) { m_testFocus = focused; }
    // How many times isKeyDown() read a keyboard key (not a mouse button) since
    // the last reset.
    int testKeyReads(bool reset) { const int n = m_testKeyReads; if (reset) m_testKeyReads = 0; return n; }
    // Stand in for the keyboard: while `on`, isKeyDown() answers from keys held
    // with testHoldKey() instead of asking Windows (the focus gate still applies).
    void testInjectKeys(bool on) { m_testKeysOn = on; m_testKeys.fill(false); }
    void testHoldKey(int vk, bool down) { if (vk >= 0 && vk < 256) m_testKeys[static_cast<size_t>(vk)] = down; }
#endif

    void initialize();
    void shutdown();

    // Call once per frame before any HUDs process input
    void updateFrame();

    // Query input state (fast, uses cached frame data)
    bool isCursorEnabled() const { return m_bCursorEnabled; }
    // One of the game's own windows is in front THIS frame (no debounce). The
    // cursor also works over the companion while another program is in front;
    // the keyboard never does, since the keys then belong to that program.
    bool hasKeyboardFocus() const { return m_bKeyboardFocus; }

    // Every keyboard and mouse-button read in the plugin goes through here: is
    // `vk` held right now? A mouse button is read while the cursor is enabled
    // (isCursorEnabled); a keyboard key only while one of the game's windows is
    // in front (hasKeyboardFocus). Otherwise it is false WITHOUT asking Windows,
    // so the plugin reads no keys while the player is in another program, even
    // with the mouse over the companion. Callers that track edges
    // (HotkeyManager) take a fresh baseline when focus returns. **Enforced** by
    // check_banned_constructs.sh (no GetAsyncKeyState anywhere else).
    bool isKeyDown(int vk) const;
    bool shouldShowCursor() const { return m_bShouldShowCursor && !m_bCursorSuppressed; }

    // Allow HUDs to suppress cursor rendering (e.g., during full-screen overlays)
    void setCursorSuppressed(bool suppressed) { m_bCursorSuppressed = suppressed; }
    const CursorPosition& getCursorPosition() const { return m_cursorPosition; }
    const MouseButton& getLeftButton() const { return m_leftButton; }
    const MouseButton& getRightButton() const { return m_rightButton; }
    const WindowBounds& getWindowBounds() const { return m_windowBounds; }
    HWND getGameWindow() const { return m_gameWindow; }  // null until the game window is found

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
    // The mouse was used without moving (a wheel tick a list kept): the idle
    // cursor stays up, as after a move or a click.
    void noteCursorActivity();

    // Force window information refresh (useful for detecting resizes at run start/stop)
    void forceWindowRefresh();

private:
    // Order matches the member declaration order below (see BaseHud's note).
    InputManager() :
        m_gameWindow(nullptr), m_processId(0),
        m_windowWidth(0), m_windowHeight(0),
        m_activeWindow(nullptr), m_activeSurface(Surface::Game),
        m_bInitialized(false), m_bCursorEnabled(false), m_bKeyboardFocus(false), m_bWasCursorEnabled(false),
        m_bShouldShowCursor(false), m_bWasCursorVisible(false), m_bCursorSuppressed(false),
        m_fLastMouseX(0.0f), m_fLastMouseY(0.0f),
        m_framesSinceFocusLost(0) {}
    ~InputManager() { shutdown(); }
    InputManager(const InputManager&) = delete;
    InputManager& operator=(const InputManager&) = delete;

    // Internal update helpers
    void updateCursorEnabled();
    void updateCursorPosition();
    void updateMouseButtons();
    void updateKeyboardKeys();
    void updateCursorVisibility();  // New: tracks mouse movement and auto-hide
    // Something keeps the idle cursor up: it rests on the Toggles strip.
    bool isCursorHeld() const;
    void refreshWindowInformation();

    // The active surface follows the MOUSE, not keyboard focus: HUD editing is
    // mouse-driven, and the game often keeps foreground while the cursor is over
    // the companion window (e.g. a second monitor). Returns the root window under
    // the cursor when it belongs to our process, else `fallback` (the foreground
    // window, already known to be ours).
    HWND surfaceWindowUnderCursor(HWND fallback) const;

    HWND m_gameWindow;
    DWORD m_processId;  // Cached process ID (never changes)
    int m_windowWidth;
    int m_windowHeight;
    HWND m_activeWindow;       // Foreground window the cursor maps into (game or companion)
    Surface m_activeSurface;   // Which surface m_activeWindow represents
#ifdef MXBMRP3_TEST_BUILD
    int m_testForceSurface = -1;   // -1 = off, 0 = force Game, 1 = force Companion
    bool m_testMouseOn = false;    // testInjectMouse
    float m_testMouseX = 0.0f, m_testMouseY = 0.0f;
    int m_testButtons = 0;
    bool m_testCursorIdle = false; // testCursorIdle
    int m_testFocus = -1;          // testForceFocus
    mutable int m_testKeyReads = 0; // testKeyReads
    bool m_testKeysOn = false;      // testInjectKeys
    std::array<bool, 256> m_testKeys{};
#endif

    // Frame state
    bool m_bInitialized;
    bool m_bCursorEnabled;
    bool m_bKeyboardFocus;     // hasKeyboardFocus(): one of our windows is in front this frame
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
    // Last movement or click: the cursor hides CursorIdle::HIDE_MS after it.
    std::chrono::steady_clock::time_point m_lastCursorActivity;

    // Focus debouncing - prevents flicker during alt-tab transitions
    int m_framesSinceFocusLost;

    // Constants (the 16:9 UI-rect math itself lives in ui_viewport.h — one
    // function shared with the companion paint loop; see input_manager.cpp's
    // static_assert tying it to PluginConstants::UI_ASPECT_RATIO)
    static constexpr float MOVEMENT_THRESHOLD = 0.001f;  // Minimum movement to count as "moved"
    static constexpr int FOCUS_DEBOUNCE_FRAMES = 5;  // Frames to wait before disabling cursor on focus loss
};
