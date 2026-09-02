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
#include "thread_safety.h"
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

    // Game-thread: ask the window thread to re-read every theme .tga on its next
    // frame. Reload Config calls this, and it is the ONLY surface where changed art
    // can appear without restarting the game -- see hudsw::Renderer::dropTextureCache.
    // A request rather than a direct call because the renderer lives on the window
    // thread; the flag is atomic because the two threads are the whole point.
    void requestArtReload() { m_artReload.store(true, std::memory_order_relaxed); }
    // See m_forceRepaint. Called from the window thread's WM_PAINT.
    void requestRepaint() { m_forceRepaint.store(true, std::memory_order_relaxed); }

    // Render cadence of the window thread. 0 = V-Sync (wait for the compositor's next
    // frame via DwmFlush — matches the monitor, tear-free); N > 0 = a fixed N Hz cap.
    // INI-only ([Display] companionRefreshHz); default V-Sync. Clamp the top end: the
    // loop paces with Sleep(1000/hz), so hz > 1000 rounds to Sleep(0) and busy-spins the
    // thread at 100% CPU. No monitor exceeds ~1000 Hz, so cap there (negatives → V-Sync).
    static constexpr int MAX_REFRESH_HZ = 1000;
    void setRefreshHz(int hz) { m_refreshHz.store(hz < 0 ? 0 : (hz > MAX_REFRESH_HZ ? MAX_REFRESH_HZ : hz)); }
    int  getRefreshHz() const { return m_refreshHz.load(); }

    // GPU (D3D11) rendering, [Advanced] hwAccel, INI-only, default on. The window
    // thread tries the GPU backend once per window life and falls back to the
    // software rasterizer on ANY failure (init or runtime), so worst case equals
    // the pre-GPU behavior; a changed setting takes effect when the window next
    // opens. See core/hud_gpu_renderer.h.
    void setHwAccel(bool on) { m_hwAccel.store(on, std::memory_order_relaxed); }
    bool getHwAccel() const { return m_hwAccel.load(std::memory_order_relaxed); }

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
    mutable Mutex m_geomMutex;
    int m_geomX MXB_GUARDED_BY(m_geomMutex) { 0 }, m_geomY MXB_GUARDED_BY(m_geomMutex) { 0 },
        m_geomW MXB_GUARDED_BY(m_geomMutex) { 0 }, m_geomH MXB_GUARDED_BY(m_geomMutex) { 0 };
    std::atomic<bool> m_geomMax{ false };
    std::atomic<int> m_refreshHz{ 0 };   // 0 = V-Sync; N = fixed N Hz cap
    std::atomic<bool> m_hwAccel{ true }; // GPU backend wanted (falls back live)
    // joined-by: stop() (HudManager shutdown path); the destructor's
    // spinThenDetach is only the no-Shutdown() unload backstop.
    std::thread m_thread;
    // Signals the destructor's spin-wait that the window thread has left our code
    // (loader-lock-safe teardown — see ~CompanionWindow). Starts true: no thread yet.
    std::atomic<bool> m_threadFinished{ true };

    Mutex m_mutex;
    std::vector<SPluginQuad_t> m_quads MXB_GUARDED_BY(m_mutex);
    std::vector<SPluginString_t> m_strings MXB_GUARDED_BY(m_mutex);
    // These five ride the same lock as m_quads/m_strings above — submit() writes
    // them under m_mutex on the game thread, the window thread copies them under
    // m_mutex before rendering — but they had no MXB_GUARDED_BY, so the
    // thread-safety analysis was not actually checking the very members the lock
    // exists for. Annotated, they are.
    std::vector<std::string> m_fontBases MXB_GUARDED_BY(m_mutex);   // basenames derived from paths (rebuilt on size change)
    std::vector<std::string> m_spriteBases MXB_GUARDED_BY(m_mutex);
    int m_firstIcon MXB_GUARDED_BY(m_mutex) = 1 << 30;
    // Constant in practice — every asset root in the plugin is the same
    // hardcoded path (AssetManager::DISCOVERY_DIR, HttpServer's web root). The
    // annotation stays because the window thread reads it under the lock, so a
    // setter added later is checked rather than trusted.
    std::string m_assetRoot MXB_GUARDED_BY(m_mutex) = "plugins/mxbmrp3_data";
    bool m_haveFrame MXB_GUARDED_BY(m_mutex) = false;
    // Set by the game thread on Reload Config, consumed once by the window thread.
    std::atomic<bool> m_artReload{false};
    // FORCE THE NEXT TICK TO RASTERISE, whatever the unchanged-frame check thinks. The
    // render loop skips a frame whose content is byte-identical to the last, which is
    // the dominant case (the plugin gets no callbacks in menus) -- so anything that
    // changes what should be on screen WITHOUT changing the frame's identity has to say
    // so here, or the skip strands it indefinitely. Two do: a theme-art reload (same
    // quads, different pixels) and exposure damage (same everything, but the OS threw
    // our pixels away). Set from the window thread AND from the render loop, hence
    // atomic. See the two setters.
    std::atomic<bool> m_forceRepaint{false};
};
