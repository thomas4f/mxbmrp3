// ============================================================================
// core/plugin_thread.h
// Experimental "plugin worker thread" — full game-thread isolation.
//
// WHY: every PiBoSo plugin callback (Draw, RunTelemetry, RaceTrackPosition, ...)
// normally runs *on the game's own thread*. Any hiccup in our code — a slow HUD
// rebuild, a lock we wait on, a page fault, a degraded driver call — directly
// stalls the game's frame. This component moves ALL of the plugin's per-frame /
// per-event work onto a dedicated thread, so the game thread only ever does two
// O(1), allocation-free things: (1) hand a copy of the callback's data to the
// worker via a queue, and (2) in Draw, pick up the most recently finished,
// triple-buffered render frame and return. A block on our side can therefore
// never propagate into the game's frame time.
//
// The mode is OPT-IN and OFF BY DEFAULT ([Advanced] pluginThread=1). When off,
// enabled() returns false and every routing helper is a no-op — the plugin runs
// exactly as before (synchronous, on the game thread), so the shipping behavior
// and the whole test suite are unchanged.
//
// Thread-safety model when ON:
//   * Game thread   : enqueue() (push a closure), requestFrame()/takeFrame()
//                     (swap a frame slot). Never touches PluginData/HudManager.
//   * Worker thread : drains the queue (running the existing handlers, which own
//                     all PluginData/HudManager mutation) and builds render frames.
// So all plugin state lives on a single thread again — just not the game's.
//
// NOT routed through the worker (documented limitations, see plugin_manager.cpp):
//   * Startup / Shutdown / DrawInit — one-shot lifecycle, run synchronously.
//   * SpectateVehicles / SpectateCameras — must answer the game synchronously.
// ============================================================================
#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

#include "../game/game_config.h"   // SPluginQuad_t / SPluginString_t
#include "render_frame_buffer.h"

class PluginThread {
public:
    static PluginThread& getInstance();

    // Spawn the worker IFF [Advanced] pluginThread=1 (read from UiConfig). Idempotent;
    // a no-op (and enabled() stays false) when the flag is off. Call once, after
    // settings are loaded, at the end of PluginManager::initialize().
    void start();

    // Signal the worker to stop and join it, then run any still-queued commands
    // inline on the CALLING thread so no state mutation is lost before shutdown
    // saves run. Safe to call when never started. Idempotent.
    void stop();

    // GAME-THREAD ONLY. Bring the worker's running state in line with the [Advanced]
    // pluginThread flag, so a live INI reload (the RELOAD_CONFIG hotkey) can switch
    // between legacy and threaded mode WITHOUT a game restart. Called once per frame at
    // the top of the Draw callback. start()/stop() from the game thread are safe; the
    // worker must never call this (stop() would join the worker to itself). A
    // threaded->legacy switch joins the worker here, so that one frame may block for up
    // to a single build — acceptable for an explicit, rare mode change.
    void reconcileEnabled();

    // True only while the worker is running (i.e. the flag was on at start()).
    bool enabled() const { return m_enabled.load(std::memory_order_acquire); }

    // True when called from the worker thread itself — routing helpers use this so a
    // handler that re-enters another handler runs inline instead of self-deadlocking.
    bool onWorkerThread() const;

    // ---- Callback routing ---------------------------------------------------
    // Each returns true if it TOOK OVER (queued the work) — the caller must then
    // return immediately. Returns false when disabled or already on the worker, in
    // which case the caller runs the work synchronously as before.

    // Zero-arg member callback: (self->*fn)()
    template <class T>
    bool offload(T* self, void (T::*fn)()) {
        if (!enabled() || onWorkerThread()) return false;
        enqueue([self, fn]() { (self->*fn)(); });
        return true;
    }

    // Single pointer-arg member callback: (self->*fn)(&argCopy). The pointee is
    // copied by value into the closure so the game's buffer can be reused/freed the
    // instant the callback returns.
    template <class T, class Arg>
    bool offload(T* self, void (T::*fn)(Arg*), const Arg& arg) {
        if (!enabled() || onWorkerThread()) return false;
        enqueue([self, fn, a = arg]() mutable { (self->*fn)(&a); });
        return true;
    }

    // Single by-value-arg member callback: (self->*fn)(v)  (e.g. RaceRemoveEntry(int)).
    template <class T, class V>
    bool offloadValue(T* self, void (T::*fn)(V), V v) {
        if (!enabled() || onWorkerThread()) return false;
        enqueue([self, fn, v]() { (self->*fn)(v); });
        return true;
    }

    // Escape hatch for callbacks whose arguments don't fit the helpers above (the
    // array callbacks capture a std::vector by value). Caller checks enabled() &&
    // !onWorkerThread() itself, builds the closure, and returns.
    void enqueue(std::function<void()> fn);

    // ---- Frame handoff ------------------------------------------------------
    // Game thread: ask the worker to build a frame for this draw state, then fetch
    // the latest finished frame. requestFrame() only records intent + wakes the
    // worker (non-blocking); takeFrame() publishes whatever is ready (never waits).
    void requestFrame(int iState);

    // Fill the out-params with the latest ready frame and return true; return false
    // (and leave params untouched) if the worker hasn't produced a frame yet. The
    // returned pointers stay valid until the NEXT takeFrame() call (triple-buffered),
    // which matches the game's "read the quads after Draw returns" contract.
    bool takeFrame(const SPluginQuad_t*& quads, int& numQuads,
                   const SPluginString_t*& strings, int& numStrings);

    // Most recent draw state the game reported (worker reads this while building).
    int drawState() const { return m_drawState.load(std::memory_order_relaxed); }

    // Block the CALLING thread until the worker has drained every command queued so
    // far (a FIFO sentinel round-trip). Test-only synchronization for the headless
    // harness — not used in the game, where the whole point is to never block. No-op
    // when disabled or already on the worker thread.
    void flush();

private:
    PluginThread() = default;
    ~PluginThread();
    PluginThread(const PluginThread&) = delete;
    PluginThread& operator=(const PluginThread&) = delete;

    void threadMain();
    void buildAndPublishFrame();

    std::atomic<bool> m_enabled{ false };
    std::atomic<bool> m_run{ false };
    // Set true when the worker has left its loop. The DESTRUCTOR spins on this (not
    // join()): it runs during static teardown under the Windows loader lock, where a
    // join()'s wait on the thread's OS-level exit would deadlock (that exit also needs
    // the loader lock). Spinning on an app-level flag needs no loader lock. stop() (the
    // normal path, not under the lock) still join()s cleanly.
    std::atomic<bool> m_workerFinished{ false };
    // True while the worker is parked on the CV with nothing left to do. flush()
    // waits for this AFTER its sentinel so a frame build triggered by a Draw can't
    // still be reading PluginData when the test reads a snapshot. (Test-path only.)
    std::atomic<bool> m_idle{ true };
    std::thread m_thread;
    std::thread::id m_workerId;

    // Command queue (closures wrapping the existing handlers).
    std::mutex m_qMutex;
    std::condition_variable m_cv;
    std::deque<std::function<void()>> m_queue;
    bool m_frameRequested = false;

    std::atomic<int> m_drawState{ 0 };

    // Performance-metrics support so the PerformanceHud / BenchmarkWidget stay live in
    // threaded mode. FPS is measured from the real Draw cadence on the GAME thread
    // (an EMA in requestFrame — m_fpsLastTp/m_fpsEma are game-thread-only); the worker
    // publishes it plus its own per-build plugin time into PluginData::updateDebugMetrics
    // (on the worker, the PluginData owner). Note the semantic shift: in threaded mode
    // "plugin time" is the WORKER's build cost — the game-thread cost is ~0 by design.
    std::atomic<float> m_fps{ 0.0f };
    std::chrono::steady_clock::time_point m_fpsLastTp{};
    float m_fpsEma = 0.0f;

    // Triple-buffered render frame: the worker fills writeSlot() + publish()es; the
    // game acquire()s the latest finished frame. The buffer guarantees the game's
    // display slot is never the worker's write slot, so a frame handed to the game
    // stays valid while the worker keeps producing. See render_frame_buffer.h.
    struct Frame {
        std::vector<SPluginQuad_t> quads;
        std::vector<SPluginString_t> strings;
    };
    RenderFrameBuffer<Frame> m_frameBuffer;
};
