// ============================================================================
// core/mouse_wheel.cpp
// The settings lists' mouse wheel; mouse_wheel.h has the why.
// ============================================================================
#include "mouse_wheel.h"
#include "../diagnostics/logger.h"

#include <atomic>

namespace {
// Shared with the window procedure, which runs on the game window's thread.
std::atomic<LONG_PTR> g_prevProc{ 0 };              // the game's procedure we forward to
std::atomic<bool> g_unicode{ true };                // the window's message character set
std::atomic<unsigned long long> g_claimUntil{ 0 };  // GetTickCount64 deadline; 0 = no claim
std::atomic<int> g_pending{ 0 };                    // kept wheel delta, WHEEL_DELTA a notch

LRESULT CALLBACK wheelProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) noexcept {
    if (msg == WM_MOUSEWHEEL && GetTickCount64() < g_claimUntil.load(std::memory_order_acquire)) {
        g_pending.fetch_add(GET_WHEEL_DELTA_WPARAM(wp), std::memory_order_acq_rel);
        return 0;   // handled: the game never sees this tick
    }
    const WNDPROC prev = reinterpret_cast<WNDPROC>(g_prevProc.load(std::memory_order_acquire));
    if (!prev) return g_unicode.load() ? DefWindowProcW(hwnd, msg, wp, lp) : DefWindowProcA(hwnd, msg, wp, lp);
    return g_unicode.load() ? CallWindowProcW(prev, hwnd, msg, wp, lp) : CallWindowProcA(prev, hwnd, msg, wp, lp);
}

LONG_PTR getProc(HWND w) {
    return IsWindowUnicode(w) ? GetWindowLongPtrW(w, GWLP_WNDPROC) : GetWindowLongPtrA(w, GWLP_WNDPROC);
}

LONG_PTR setProc(HWND w, LONG_PTR proc) {
    return IsWindowUnicode(w) ? SetWindowLongPtrW(w, GWLP_WNDPROC, proc) : SetWindowLongPtrA(w, GWLP_WNDPROC, proc);
}
}  // namespace

MouseWheel& MouseWheel::getInstance() {
    static MouseWheel instance;
    return instance;
}

void MouseWheel::attach(HWND window) {
    if (m_stuck) return;   // ours is still in some window's chain: never a second copy
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    if (pid != GetCurrentProcessId()) return;
    g_unicode.store(IsWindowUnicode(window) != FALSE);
    g_claimUntil.store(0);
    // Publish the procedure we forward to BEFORE ours can be called.
    g_prevProc.store(getProc(window), std::memory_order_release);
    if (!g_prevProc.load()) return;
    SetLastError(0);
    const LONG_PTR was = setProc(window, reinterpret_cast<LONG_PTR>(&wheelProc));
    if (was == 0 && GetLastError() != 0) {
        g_prevProc.store(0);
        DEBUG_WARN_F("MouseWheel: could not attach to the game window (error %lu)", GetLastError());
        return;
    }
    g_prevProc.store(was, std::memory_order_release);   // what was really there at the swap
    m_window = window;
    pin();
    DEBUG_INFO_F("MouseWheel: attached to the game window (HWND=%p)", static_cast<void*>(window));
}

// Keep this DLL mapped until the process exits: the window may run wheelProc
// after any release (mouse_wheel.h, PINNED FROM THE FIRST ATTACH).
void MouseWheel::pin() {
    if (m_pinned) return;
    m_pinned = true;
#if !defined(MXBMRP3_TEST_BUILD)
    HMODULE self = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                            reinterpret_cast<LPCWSTR>(&wheelProc), &self)) {
        DEBUG_WARN_F("MouseWheel: could not pin the plugin (error %lu)", GetLastError());
    }
#endif
}

int MouseWheel::take(bool over) {
    m_asked = true;
    if (!over) return 0;
    m_over = true;
    m_remainder += g_pending.exchange(0, std::memory_order_acq_rel);
    const int notches = m_remainder / WHEEL_DELTA;
    m_remainder -= notches * WHEEL_DELTA;
    return notches;
}

void MouseWheel::endFrame(HWND window) {
#if defined(MXBMRP3_TEST_BUILD)
    if (m_testWindow) window = m_testWindow;
#endif
    const bool asked = m_asked, over = m_over;
    m_asked = m_over = false;
    if (!asked || !window || !IsWindow(window)) {
        release();
        return;
    }
    if (window != m_window) {
        release();
        attach(window);
    }
    g_claimUntil.store(m_window && over ? GetTickCount64() + CLAIM_MS : 0, std::memory_order_release);
    if (!over) {   // ticks kept for a list the cursor has left are nobody's
        g_pending.store(0);
        m_remainder = 0;
    }
}

void MouseWheel::release() {
    g_claimUntil.store(0, std::memory_order_release);
    g_pending.store(0);
    m_remainder = 0;
    if (!m_window) return;
    const HWND window = m_window;
    m_window = nullptr;
    if (!IsWindow(window)) {   // the window went first: nothing points at us
        g_prevProc.store(0);
        return;
    }
    if (getProc(window) == reinterpret_cast<LONG_PTR>(&wheelProc)) {
        setProc(window, g_prevProc.load());
        DEBUG_INFO("MouseWheel: detached from the game window");
    } else {
        // Someone stacked a procedure on ours: unlinking ours would cut theirs
        // off. Ours stays, forwarding everything (no claim, ever again).
        m_stuck = true;
        DEBUG_WARN("MouseWheel: another procedure sits on ours; left in place, forwarding only");
    }
}
