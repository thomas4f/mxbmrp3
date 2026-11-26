// ============================================================================
// core/mouse_wheel.h
// The mouse wheel for the plugin's scrolling lists: the settings panel's, and
// the HUDs' (Event Log, Lap Log, ...; hud/hud_scroll.h) while the pointer shows.
//
// WHY A SUBCLASS. Windows keeps no wheel state to poll (GetAsyncKeyState has
// no wheel key): the wheel only exists as WM_MOUSEWHEEL, sent to the window
// under the cursor. The game window is not ours, so the one way to see that
// message without a system-wide hook or raw input (which would REPLACE the
// game's own registration) is to put our window procedure in front of the
// game's (SetWindowLongPtr GWLP_WNDPROC) and forward everything else to it.
//
// THE GAME KEEPS ITS WHEEL. Each frame, every list the player could scroll
// right now asks for the wheel (take); the frame's end (endFrame, from
// HudManager after every HUD's update) installs us while any list asked and
// takes us out once none does. Even installed, a tick is kept only while the
// CLAIM is live: a list that can scroll was under the cursor on the last
// frame drawn, within CLAIM_MS. Every other message, and every other tick,
// goes straight on to the game's procedure. The claim is a deadline rather
// than a flag because the game makes no plugin calls in its own menus: a panel
// left open when the game takes the player off track stops renewing it, and
// the wheel is the game's again within CLAIM_MS.
//
// THREADS. The procedure runs on the game window's thread, which need not be
// the one that draws, so what it shares with the game thread is atomic.
//
// TEARDOWN. Removed once no list asks, at the orchestrated Shutdown(), and in
// the destructor for an unload without Shutdown(); the destructor touches only
// its own window (no other singleton). A procedure stacked on top of ours by
// someone else cannot be unlinked without breaking theirs; ours is then left
// forwarding everything, and the log says so.
//
// PINNED FROM THE FIRST ATTACH. The procedure lives in this DLL, and a window
// can still run it after any release: one stacked on ours keeps calling it,
// and a game that unloads plugins from inside its own window procedure (Alt+F4
// with the panel open) returns through our frame. So the first attach pins the
// module (GET_MODULE_HANDLE_EX_FLAG_PIN): from then on it stays mapped until
// the process exits, and none of those can land in unmapped code. The test
// build only records the pin -- a pinned DLL could not be reloaded fresh by
// the next test case -- so the harness can still unload it and watch the
// destructor restore the window.
//
// Pinned by tests/integration/tests/settings_scroll_test.cpp (the wheel cases).
// ============================================================================
#pragma once

#include <windows.h>

class MouseWheel {
public:
    static MouseWheel& getInstance();

    // How long a claim lasts without being renewed by a drawn frame.
    static constexpr unsigned long long CLAIM_MS = 100;

    // From a list the player could scroll this frame (its bar answers, it has
    // somewhere to go): ask for the wheel. `over`: the cursor is on it, on the
    // game window. Returns the whole notches kept for it since the last frame
    // (positive = away from the player, i.e. up the list; 0 when not over);
    // a fine-grained wheel's remainder carries over.
    int take(bool over);
    // Once a frame after every list has asked: in front of `window` (a new
    // window moves us) while any did, keeping ticks for CLAIM_MS when one was
    // under the cursor; out of it once none did.
    void endFrame(HWND window);
    // Out of the game window; every tick is the game's again.
    void release();

#if defined(MXBMRP3_TEST_BUILD)
    // Stand in for the game window (the harness has none): endFrame() uses this.
    void testSetWindow(HWND window) { m_testWindow = window; }
    HWND testAttached() const { return m_window; }
    bool testPinned() const { return m_pinned; }
#endif

private:
    MouseWheel() = default;
    ~MouseWheel() { release(); }
    MouseWheel(const MouseWheel&) = delete;
    MouseWheel& operator=(const MouseWheel&) = delete;

    void attach(HWND window);
    void pin();

    HWND m_window = nullptr;   // mt-plain: game thread only (the proc reads atomics)
    bool m_stuck = false;      // mt-plain: game thread only; another proc sits on ours
    bool m_pinned = false;     // mt-plain: game thread only; the module stays mapped
    int m_remainder = 0;       // mt-plain: game thread only; sub-notch wheel delta
    bool m_asked = false;      // mt-plain: game thread only; a list asked this frame
    bool m_over = false;       // mt-plain: game thread only; ...with the cursor on it
#if defined(MXBMRP3_TEST_BUILD)
    HWND m_testWindow = nullptr;
#endif
};
