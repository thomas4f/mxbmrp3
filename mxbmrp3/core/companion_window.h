// ============================================================================
// core/companion_window.h
// An in-process, standalone OS window that renders the plugin's own HUD outside
// the game — so the user can drag it to a second monitor. It is NOT a network
// mirror: it reads the plugin's live render primitives directly from memory and
// draws them with the software renderer (core/hud_sw_renderer), presenting via a
// plain Win32 window (works natively on Windows and under Proton/Wine).
//
// Threading: the game thread calls submit() once per Draw with a snapshot of the
// current quads/strings (POD, cheap to copy) under a mutex; a dedicated window
// thread owns the Win32 window + message loop and renders the latest snapshot on
// its own cadence — so the window stays live and interactive even in menus, when
// the game issues no Draw calls. Enable via the [CompanionWindow] INI setting.
// ============================================================================
#pragma once
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../game/game_config.h"   // SPluginQuad_t / SPluginString_t

class CompanionWindow {
public:
    static CompanionWindow& getInstance();

    // True if `hwnd` (a HWND, passed opaque to keep this header windows.h-free) is
    // the companion window — identified by its window class, so input handling can
    // tell the companion surface apart from the game window. Always false off-Win32.
    static bool isCompanionHwnd(void* hwnd);

    // Open (true) / close (false) the window. Idempotent; safe from any thread.
    void setEnabled(bool enabled);
    bool isEnabled() const { return m_enabled.load(std::memory_order_relaxed); }

    // Game-thread: publish this frame's primitives + the registration tables
    // (font/sprite paths, 1-based indices; firstIcon splits textures from icons).
    // Cheap POD copy under the mutex; a no-op when the window is closed.
    void submit(const std::vector<SPluginQuad_t>& quads,
                const std::vector<SPluginString_t>& strings,
                const std::vector<std::string>& fontPaths,
                const std::vector<std::string>& spritePaths,
                int firstIcon);

    // Where the .tga assets live (default: the game-relative plugin data dir).
    void setAssetRoot(const std::string& root);

    // Persisted window geometry (full-window rect, screen coords). The window
    // thread updates it as the user moves/resizes; the game thread reads it when
    // settings are saved and pushes the loaded values back before the window opens.
    // w<=0 means "unset" -> the window opens at the default size/position. The
    // maximized flag is stored/restored alongside the rect (the rect stays the
    // "normal"/restored geometry, so un-maximizing returns to the right place).
    void setSavedGeometry(int x, int y, int w, int h);
    void getSavedGeometry(int& x, int& y, int& w, int& h) const;
    void setSavedMaximized(bool m) { m_geomMax.store(m); }
    bool getSavedMaximized() const { return m_geomMax.load(); }

    // Render cadence of the window thread. 0 = V-Sync (wait for the compositor's next
    // frame via DwmFlush — matches the monitor, tear-free); N > 0 = a fixed N Hz cap.
    // INI-only ([Display] companionRefreshHz); default V-Sync. Clamp the top end: the
    // loop paces with Sleep(1000/hz), so hz > 1000 rounds to Sleep(0) and busy-spins the
    // thread at 100% CPU. No monitor exceeds ~1000 Hz, so cap there (negatives → V-Sync).
    static constexpr int MAX_REFRESH_HZ = 1000;
    void setRefreshHz(int hz) { m_refreshHz.store(hz < 0 ? 0 : (hz > MAX_REFRESH_HZ ? MAX_REFRESH_HZ : hz)); }
    int  getRefreshHz() const { return m_refreshHz.load(); }

    // Request the window to close from WITHIN the window thread (the WM_CLOSE
    // handler): just signals the loop to exit — it must NOT join itself. The thread
    // tears down its own window and finishes; a later stop()/setEnabled() reaps it.
    void requestClose();

    // Signal the window thread to stop and join it. Safe from any OTHER thread
    // (game thread / shutdown); never joins the window thread from itself.
    void stop();

    // True once if the window was closed by the user (its X button), consumed on
    // read. The game thread uses this to fall back to In-game display so the HUD
    // reappears in the game instead of vanishing. Set only by the WM_CLOSE path,
    // never by a setting-driven stop() — so it distinguishes an X-close from a
    // deliberate switch back to In-game.
    bool consumeUserClosed() { return m_userClosed.exchange(false); }

private:
    CompanionWindow() = default;
    ~CompanionWindow();
    CompanionWindow(const CompanionWindow&) = delete;
    CompanionWindow& operator=(const CompanionWindow&) = delete;

    void threadMain();

    std::atomic<bool> m_enabled{ false };
    std::atomic<bool> m_run{ false };
    std::atomic<bool> m_userClosed{ false };
    // Persisted geometry (full-window "normal"/restored rect + maximized flag).
    // w<=0 => unset (open at default). The rect is guarded by m_geomMutex (not four
    // atomics) so a cross-thread read at save time can't tear the rect (see the .cpp);
    // the maximized flag stays a lone atomic bool — a single value can't be torn.
    mutable std::mutex m_geomMutex;
    int m_geomX{ 0 }, m_geomY{ 0 }, m_geomW{ 0 }, m_geomH{ 0 };
    std::atomic<bool> m_geomMax{ false };
    std::atomic<int> m_refreshHz{ 0 };   // 0 = V-Sync; N = fixed N Hz cap
    std::thread m_thread;

    std::mutex m_mutex;
    std::vector<SPluginQuad_t> m_quads;
    std::vector<SPluginString_t> m_strings;
    std::vector<std::string> m_fontBases;    // basenames derived from paths (rebuilt on size change)
    std::vector<std::string> m_spriteBases;
    int m_firstIcon = 1 << 30;
    std::string m_assetRoot = "plugins/mxbmrp3_data";
    bool m_haveFrame = false;
};
