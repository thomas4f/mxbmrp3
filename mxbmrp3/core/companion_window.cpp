// ============================================================================
// core/companion_window.cpp  — see companion_window.h
// ============================================================================
#include "companion_window.h"

#include "hud_sw_renderer.h"
#include "../diagnostics/logger.h"

#include <algorithm>
#include <chrono>
#include <thread>

namespace {
// Reduce a registered resource path (".../fonts/RobotoMono-Regular.fnt") to its
// base name ("RobotoMono-Regular"), which the renderer maps to a .ttf / .tga.
std::string baseName(const std::string& path) {
    size_t slash = path.find_last_of("/\\");
    std::string n = (slash == std::string::npos) ? path : path.substr(slash + 1);
    size_t dot = n.find_last_of('.');
    if (dot != std::string::npos) n.resize(dot);
    return n;
}

// Force the process's one-time GDI subsystem init to complete on the CALLER
// thread before the render thread is spawned. The render loop's first paint does
// GetDC -> CreateCompatibleDC -> CreateCompatibleBitmap (device-caps lookup); the
// very first such call in a process triggers a lazy, one-time GDI init. If the
// game/main thread and this new render thread both hit that first-init
// concurrently, Wine's win32u serializes it behind a pthread_once + internal
// mutex whose lock ordering can deadlock (both threads wedge inside
// NtGdiOpenDCW/CreateCompatibleBitmap) — an intermittent hang seen only under
// Wine (headless CI), where GDI is a userspace lazy-init. Real Windows GDI is
// thread-safe with no such bootstrap, so this is harmless there and belt-and-
// suspenders. Running the same primitive sequence once, single-threaded, before
// the thread exists makes the render thread's later GDI a no-op re-entry.
void warmUpGdiOnce() {
    static bool done = false;
    if (done) return;
    done = true;
    HDC screen = GetDC(nullptr);
    if (!screen) return;
    HDC mem = CreateCompatibleDC(screen);
    HBITMAP bmp = CreateCompatibleBitmap(screen, 1, 1);
    if (mem && bmp) {
        HGDIOBJ old = SelectObject(mem, bmp);
        SelectObject(mem, old);
    }
    if (bmp) DeleteObject(bmp);
    if (mem) DeleteDC(mem);
    ReleaseDC(nullptr, screen);
}
}  // namespace

CompanionWindow& CompanionWindow::getInstance() {
    static CompanionWindow instance;
    return instance;
}

CompanionWindow::~CompanionWindow() {
    // Static-teardown backstop: only fires if the orchestrated shutdown (stop())
    // was skipped — e.g. the DLL is unloaded WITHOUT the Shutdown() export being
    // called. That path runs under the Windows loader lock (FreeLibrary -> static
    // dtors), and a std::thread::join() waits for the thread's OS-level exit, which
    // ALSO needs the loader lock -> deadlock. So DON'T join: signal stop, spin until
    // the thread has left our loop (an app-level flag, no loader lock involved), then
    // detach so its CRT/OS teardown finishes without us blocking on it. Same shape
    // (and same known residual window) as ~XInputReader / ~PluginThread.
    if (m_thread.joinable()) {
        m_enabled.store(false);
        m_run.store(false);
        // BOUNDED spin: on an ExitProcess-without-Shutdown() teardown the OS has
        // already TERMINATED the thread - the finished flag will never be stored,
        // and an unbounded spin would hang process exit forever. ~2s covers any
        // legitimately slow exit; past it, detach regardless (a terminated thread
        // makes the detach trivially safe; a pathologically still-live one lands
        // in the same known residual window documented above).
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (!m_threadFinished.load(std::memory_order_acquire) &&
               std::chrono::steady_clock::now() < deadline) {
            std::this_thread::yield();
        }
        m_thread.detach();
    }
}

void CompanionWindow::setAssetRoot(const std::string& root) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_assetRoot = root;
}

void CompanionWindow::setSavedGeometry(int x, int y, int w, int h) {
    // Guard the four fields as ONE rect: the window thread writes them from
    // WM_MOVE/WM_SIZE while the game thread reads them at save time, and four
    // independent atomics could be read torn mid-update (a new x/y paired with an
    // old w/h), persisting a bogus rect. A tiny mutex keeps the rect self-consistent.
    std::lock_guard<std::mutex> lock(m_geomMutex);
    m_geomX = x; m_geomY = y; m_geomW = w; m_geomH = h;
}

void CompanionWindow::getSavedGeometry(int& x, int& y, int& w, int& h) const {
    std::lock_guard<std::mutex> lock(m_geomMutex);
    x = m_geomX; y = m_geomY; w = m_geomW; h = m_geomH;
}

void CompanionWindow::setEnabled(bool enabled) {
    if (!enabled) { stop(); return; }
    if (m_enabled.exchange(true)) return;   // already open
    // Reap a previous thread that closed itself (e.g. via the window's X button)
    // before starting a new one — assigning over a joinable std::thread terminates.
    if (m_thread.joinable() && m_thread.get_id() != std::this_thread::get_id())
        m_thread.join();
    // Re-assert the open state AFTER the reap: if the previous thread was mid-
    // exception-unwind, its catch block stores m_enabled=false / m_userClosed=true
    // during the join above — which would leave this freshly-requested window
    // permanently suppressed (submit no-ops) and immediately fall the display
    // target back to In-game. The join is a full barrier, so these stores win.
    m_enabled.store(true);
    m_userClosed.store(false);
    // Complete the one-time GDI init on this (caller) thread before the render
    // thread exists, so the two can't race Wine's win32u GDI bootstrap. See
    // warmUpGdiOnce().
    warmUpGdiOnce();
    m_threadFinished.store(false, std::memory_order_release);
    m_run.store(true);
    m_thread = std::thread([this] {
        try { threadMain(); }
        catch (...) {
            DEBUG_WARN("CompanionWindow: thread terminated by exception");
            // Self-heal: leaving m_enabled true would keep the game frame
            // suppressed (displayTarget=COMPANION) with no live window — the
            // user would have no HUD anywhere. Flag the close so HudManager's
            // existing user-closed fallback restores the In-game display.
            // (The window itself can't be destroyed from here — threadMain owns
            // the HWND — but the render loop catches its own failures and
            // destroys the window cleanly; this is the outer backstop.)
            m_enabled.store(false);
            m_run.store(false);
            m_userClosed.store(true);
        }
        // LAST: signal the destructor's spin-wait that we've left our code. Keep
        // this the final statement so no more of our (potentially-unmapped-soon)
        // code runs. See ~CompanionWindow.
        m_threadFinished.store(true, std::memory_order_release);
    });
}

void CompanionWindow::requestClose() {
    // Called on the window thread (WM_CLOSE). Only signal — the thread exits its
    // own loop and destroys its window. NEVER join here: a thread can't join itself,
    // and throwing out of a WndProc would terminate the process.
    m_enabled.store(false);
    m_run.store(false);
    m_userClosed.store(true);   // game thread falls back to In-game display
}

void CompanionWindow::stop() {
    m_enabled.store(false);
    m_run.store(false);
    // Join only from another thread — never the window thread itself.
    if (m_thread.joinable() && m_thread.get_id() != std::this_thread::get_id())
        m_thread.join();
}

void CompanionWindow::submit(const std::vector<SPluginQuad_t>& quads,
                             const std::vector<SPluginString_t>& strings,
                             const std::vector<std::string>& fontPaths,
                             const std::vector<std::string>& spritePaths,
                             int firstIcon) {
    if (!m_enabled.load(std::memory_order_relaxed)) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    m_quads = quads;
    m_strings = strings;
    m_firstIcon = firstIcon;
    // The registration tables are stable after init — rebuild basenames only when
    // their size changes (i.e. once), not every frame. ASSUMPTION: font/sprite
    // registration is one-time and append-only, so a table can only GROW, never be
    // repopulated with the same count but different entries. If that ever changes
    // (dynamic re-registration), key this on content instead or the companion renders
    // stale basenames. (AssetManager registers once at startup, so it holds today.)
    if (m_fontBases.size() != fontPaths.size()) {
        m_fontBases.clear();
        for (const auto& p : fontPaths) m_fontBases.push_back(baseName(p));
    }
    if (m_spriteBases.size() != spritePaths.size()) {
        m_spriteBases.clear();
        for (const auto& p : spritePaths) m_spriteBases.push_back(baseName(p));
    }
    m_haveFrame = true;
}

#if defined(_WIN32)
#include <windows.h>
#include <dwmapi.h>   // DwmFlush (V-Sync pacing)

#if defined(_MSC_VER)
#pragma comment(lib, "gdi32.lib")   // StretchDIBits / CreateSolidBrush / FillRect
#pragma comment(lib, "user32.lib")  // window + DC APIs
#pragma comment(lib, "dwmapi.lib")  // DwmFlush
#endif

namespace {
const wchar_t* kClassName = L"MXBMRP3CompanionWindow";

// The bounding rect of all monitors (screen coords), used to reject a saved
// off-screen position (e.g. after a monitor was disconnected).
RECT virtualScreenRect() {
    int x = GetSystemMetrics(SM_XVIRTUALSCREEN), y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    return { x, y, x + GetSystemMetrics(SM_CXVIRTUALSCREEN), y + GetSystemMetrics(SM_CYVIRTUALSCREEN) };
}
}  // namespace

bool CompanionWindow::isCompanionHwnd(void* hwnd) {
    if (!hwnd) return false;
    wchar_t cls[64];
    int n = GetClassNameW(static_cast<HWND>(hwnd), cls, 64);
    return n > 0 && wcscmp(cls, kClassName) == 0;
}

namespace {

LRESULT CALLBACK companionWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CLOSE:
            // We're on the window thread — only SIGNAL the close (no join, which would
            // be a self-join and terminate the process). The thread loop sees the flag,
            // exits, and destroys this window itself.
            CompanionWindow::getInstance().requestClose();
            return 0;
        case WM_MOVE:
        case WM_SIZE:
            // Remember the user's window state so it persists across sessions. Track
            // the maximized flag whenever not minimized; capture the "normal" rect
            // only when not maximized (so we keep the restored geometry, and never
            // save GetWindowRect's bogus -32000 minimized coords).
            if (!IsIconic(hwnd)) {
                bool zoomed = IsZoomed(hwnd) != FALSE;
                CompanionWindow::getInstance().setSavedMaximized(zoomed);
                RECT rc;
                if (!zoomed && GetWindowRect(hwnd, &rc))
                    CompanionWindow::getInstance().setSavedGeometry(
                        rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top);
            }
            return DefWindowProcW(hwnd, msg, wp, lp);
        case WM_MOUSEACTIVATE:
            // Click-raise WITHOUT taking focus. WS_EX_NOACTIVATE keeps the window from
            // ever stealing foreground, but that also means a window sitting BEHIND the
            // game on the SAME monitor can't be brought forward by clicking it. Raise
            // it to the top of the z-order here (SWP_NOACTIVATE preserves our never-focus
            // contract), then tell Windows not to activate on this click either. On a
            // second monitor this is a harmless no-op (already unobscured).
            SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            return MA_NOACTIVATE;
        case WM_ERASEBKGND:
            return 1;  // we paint the whole client ourselves; skip flicker
        case WM_SETCURSOR:
            // Hide the OS cursor over the client area — the plugin draws its own
            // cursor there (same as the game window does). Defer the non-client
            // area (borders/title bar) to DefWindowProc so resize/move cursors stay.
            if (LOWORD(lp) == HTCLIENT) { SetCursor(nullptr); return TRUE; }
            return DefWindowProcW(hwnd, msg, wp, lp);
        default:
            return DefWindowProcW(hwnd, msg, wp, lp);
    }
}
}  // namespace

void CompanionWindow::threadMain() {
    HINSTANCE hInst = GetModuleHandleW(nullptr);
    WNDCLASSW wc{};
    wc.lpfnWndProc = companionWndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kClassName;
    RegisterClassW(&wc);  // ignore "already registered" on re-open

    // Restore the saved size/position if we have one AND it still intersects a
    // monitor (a disconnected second screen shouldn't strand the window off-view);
    // otherwise open at the default size, system-placed.
    int gx, gy, gw, gh; getSavedGeometry(gx, gy, gw, gh);
    bool haveGeom = gw > 0 && gh > 0;
    if (haveGeom) {
        RECT wr{ gx, gy, gx + gw, gy + gh }, vs = virtualScreenRect(), dummy;
        if (!IntersectRect(&dummy, &wr, &vs)) haveGeom = false;
    }
    int cx = haveGeom ? gx : CW_USEDEFAULT;
    int cy = haveGeom ? gy : CW_USEDEFAULT;
    int cw = haveGeom ? gw : 980;
    int ch = haveGeom ? gh : 560;

    // WS_EX_NOACTIVATE so the window NEVER takes foreground — not on open (incl. the
    // maximized show; there's no no-activate variant of SW_SHOWMAXIMIZED, so the
    // ex-style is what keeps it from taking focus) and not on click. We keep it for
    // the window's whole life — it is deliberately NOT cleared: input is routed by the
    // window under the cursor (InputManager::surfaceWindowUnderCursor), so the
    // companion never needs to be activated to drag/click HUDs in it, and clearing the
    // style was what let opening it steal focus from the game on launch. WS_EX_APPWINDOW
    // keeps a taskbar button despite NOACTIVATE. Title is plain ASCII (a non-ASCII dash
    // in an L"" literal is mis-decoded by MSVC on a UTF-8 source without /utf-8).
    HWND hwnd = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_APPWINDOW, kClassName, L"MXBMRP3",
                                WS_OVERLAPPEDWINDOW, cx, cy,
                                cw, ch, nullptr, nullptr, hInst, nullptr);
    if (!hwnd) {
        DEBUG_WARN("CompanionWindow: CreateWindowEx failed");
        m_run.store(false);
        return;
    }
    ShowWindow(hwnd, getSavedMaximized() ? SW_SHOWMAXIMIZED : SW_SHOWNOACTIVATE);
    DEBUG_INFO("CompanionWindow: opened");

    hudsw::Renderer renderer;
    hudsw::Image img;
    std::vector<uint8_t> bgra;  // BGRA scratch for the DIB (Win32 wants blue-first)

    // Back buffer: we compose each frame off-screen and blit it to the window in a
    // single BitBlt, so the window never shows a half-drawn (fill-then-image) frame.
    // Without this, drawing straight to the window DC flickers badly.
    HDC memDC = nullptr;
    HBITMAP memBmp = nullptr, oldBmp = nullptr;
    int bbW = 0, bbH = 0;

    // Snapshot scratch, hoisted out of the loop: vector/string ASSIGNMENT reuses
    // existing capacity, so the per-frame copy under m_mutex is a memcpy-grade fill
    // instead of fresh allocations — the game thread's submit() blocks on this same
    // mutex, so keeping the critical section short bounds game-thread stalls at
    // high companionRefreshHz.
    std::vector<SPluginQuad_t> quads;
    std::vector<SPluginString_t> strings;
    std::vector<std::string> fontBases, spriteBases;
    std::string root;

    while (m_run.load()) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!m_run.load()) break;

        // Snapshot the latest frame under the lock, then render outside it.
        int firstIcon; bool have;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            quads = m_quads; strings = m_strings;
            fontBases = m_fontBases; spriteBases = m_spriteBases;
            firstIcon = m_firstIcon; root = m_assetRoot; have = m_haveFrame;
        }

        RECT rc; GetClientRect(hwnd, &rc);
        int cw = std::max(1, (int)(rc.right - rc.left)), ch = std::max(1, (int)(rc.bottom - rc.top));
        // A 16:9 content rect centered in the client sets the HUD's SCALE (so it never
        // distorts), but we render into the FULL client — elements positioned outside
        // [0,1] (negative / past 1, exactly as the in-game HUD allows) then land in the
        // surrounding area instead of being clipped off by a letterbox. The window is
        // freely resizable to any shape; the extra space is usable, not dead bars.
        int rw = cw, rh = cw * 9 / 16;
        if (rh > ch) { rh = ch; rw = ch * 16 / 9; }
        rw = std::max(1, rw); rh = std::max(1, rh);
        int dx = (cw - rw) / 2, dy = (ch - rh) / 2;
        if (img.w != cw || img.h != ch) img.resize(cw, ch);
        img.setViewport((float)dx, (float)dy, (float)rw, (float)rh);

        if (have) {
            hudsw::Frame f;
            f.quads = quads.data(); f.quadCount = (int)quads.size();
            f.strings = strings.data(); f.stringCount = (int)strings.size();
            f.fontNames = &fontBases; f.spriteNames = &spriteBases;
            f.firstIcon = firstIcon; f.assetRoot = root;
            try {
                renderer.render(img, f, 12, 15, 20);  // dark backdrop for legibility (fills the whole client)
            } catch (...) {
                // A throwing render (e.g. bad_alloc from a corrupt user-supplied
                // asset) would otherwise repeat every frame. Close the window
                // cleanly and engage the user-closed fallback so the in-game
                // HUD comes back instead of leaving the user HUD-less.
                DEBUG_WARN("CompanionWindow: render failed - closing window, "
                           "falling back to In-game display");
                m_enabled.store(false);
                m_userClosed.store(true);
                m_run.store(false);
                break;  // cleanup below destroys the window
            }
        } else {
            img.fill(12, 15, 20, 255);
        }

        // Present: convert RGBA -> BGRA. The image already covers the whole client, so
        // one blit paints everything (no separate letterbox fill needed).
        bgra.resize(img.px.size());
        for (size_t i = 0; i < img.px.size(); i += 4) {
            bgra[i] = img.px[i + 2]; bgra[i + 1] = img.px[i + 1]; bgra[i + 2] = img.px[i]; bgra[i + 3] = img.px[i + 3];
        }
        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = cw;
        bmi.bmiHeader.biHeight = -ch;  // top-down
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        HDC dc = GetDC(hwnd);
        // (Re)create the back buffer to match the client size.
        if (!memDC) memDC = CreateCompatibleDC(dc);
        if (cw != bbW || ch != bbH) {
            HBITMAP nb = CreateCompatibleBitmap(dc, cw, ch);
            HBITMAP prev = (HBITMAP)SelectObject(memDC, nb);
            if (!oldBmp) oldBmp = prev;         // stash the DC's original bitmap for cleanup
            if (memBmp) DeleteObject(memBmp);   // free the previous back buffer
            memBmp = nb; bbW = cw; bbH = ch;
        }
        // Compose off-screen (one full-client image), then one blit to the window.
        StretchDIBits(memDC, 0, 0, cw, ch, 0, 0, cw, ch, bgra.data(), &bmi, DIB_RGB_COLORS, SRCCOPY);
        BitBlt(dc, 0, 0, cw, ch, memDC, 0, 0, SRCCOPY);
        ReleaseDC(hwnd, dc);

        // Pace the thread: V-Sync (DwmFlush blocks until the compositor's next present —
        // tear-free and matched to the monitor) or a fixed Hz cap. DwmFlush fails if the
        // DWM is off (rare on modern Windows; also under a bare Wine prefix), so fall
        // back to ~60 Hz so the loop still paces instead of spinning.
        int hz = m_refreshHz.load(std::memory_order_relaxed);
        if (hz > 0) Sleep(1000 / hz);
        else if (FAILED(DwmFlush())) Sleep(16);
    }

    if (memDC) {
        if (oldBmp) SelectObject(memDC, oldBmp);
        if (memBmp) DeleteObject(memBmp);
        DeleteDC(memDC);
    }
    DestroyWindow(hwnd);
    DEBUG_INFO("CompanionWindow: closed");
}

#else  // non-Windows: the plugin is Windows-only, so this is just a link stub.
void CompanionWindow::threadMain() { m_run.store(false); }
#endif
