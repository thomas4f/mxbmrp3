// ============================================================================
// core/plugin_thread.cpp
// See plugin_thread.h for the design rationale.
// ============================================================================
#include "plugin_thread.h"

#include "ui_config.h"
#include "hud_manager.h"
#include "plugin_data.h"
#include "../handlers/draw_handler.h"
#include "../diagnostics/logger.h"

#include <future>

PluginThread& PluginThread::getInstance() {
    static PluginThread instance;
    return instance;
}

PluginThread::~PluginThread() {
    // Static-teardown backstop. The orchestrated shutdown path calls stop() (a clean
    // join) while every singleton is still alive; this only fires if that was skipped
    // (the DLL unloaded WITHOUT the Shutdown() export). That runs under the Windows
    // loader lock, where join() would deadlock (see stop() vs here in plugin_thread.h),
    // so DON'T join: signal stop, wake the worker off its CV, spin until it has left
    // the loop (app-level flag, no loader lock), then detach.
    if (m_thread.joinable()) {
        m_run.store(false, std::memory_order_release);
        { std::lock_guard<std::mutex> lk(m_qMutex); m_cv.notify_all(); }
        while (!m_workerFinished.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        m_thread.detach();
    }
}

bool PluginThread::onWorkerThread() const {
    return m_enabled.load(std::memory_order_acquire) &&
           std::this_thread::get_id() == m_workerId;
}

void PluginThread::start() {
    if (m_enabled.load(std::memory_order_acquire)) return;   // already running
    if (!UiConfig::getInstance().getPluginThread()) return;  // opt-in only

    m_run.store(true, std::memory_order_release);
    m_workerFinished.store(false, std::memory_order_release);
    m_thread = std::thread([this]() {
        // Top-level guard: an uncaught throw in a std::thread body calls
        // std::terminate() and takes the host game down with it.
        try {
            threadMain();
        } catch (...) {
            DEBUG_ERROR("PluginThread worker terminated by exception");
        }
        // LAST: signal the destructor's spin-wait that we've left our code.
        m_workerFinished.store(true, std::memory_order_release);
    });
    m_workerId = m_thread.get_id();
    m_enabled.store(true, std::memory_order_release);
    DEBUG_INFO("PluginThread: worker started (game-thread isolation ON)");
}

void PluginThread::stop() {
    if (!m_thread.joinable()) return;

    // Stop accepting new async work on the game thread. Clearing enabled() first
    // makes every routing helper fall back to inline execution during teardown.
    m_enabled.store(false, std::memory_order_release);
    m_run.store(false, std::memory_order_release);
    { std::lock_guard<std::mutex> lk(m_qMutex); m_cv.notify_all(); }
    try { m_thread.join(); } catch (...) {}

    // Drain whatever the worker didn't get to, inline on the calling (game) thread —
    // the worker is joined, so this is single-threaded and safe. Keeps PluginData
    // consistent for the shutdown-time stats/settings saves.
    std::deque<std::function<void()>> leftover;
    { std::lock_guard<std::mutex> lk(m_qMutex); leftover.swap(m_queue); }
    for (auto& cmd : leftover) {
        try { if (cmd) cmd(); } catch (...) {}
    }
    DEBUG_INFO("PluginThread: worker stopped");
}

void PluginThread::reconcileEnabled() {
    bool desired = UiConfig::getInstance().getPluginThread();
    bool running = m_enabled.load(std::memory_order_acquire);
    if (desired == running) return;
    if (desired) start();
    else         stop();
}

void PluginThread::enqueue(std::function<void()> fn) {
    if (!fn) return;
    {
        std::lock_guard<std::mutex> lk(m_qMutex);
        m_queue.push_back(std::move(fn));
    }
    m_cv.notify_one();
}

void PluginThread::flush() {
    if (!enabled() || onWorkerThread()) return;
    // 1) FIFO sentinel: guarantees every command queued before now has run.
    std::promise<void> done;
    std::future<void> fut = done.get_future();
    enqueue([&done]() { done.set_value(); });
    fut.wait();
    // 2) Wait for the worker to finish any frame build that a Draw requested and go
    //    back to idle — otherwise produceFrame() could still be touching PluginData
    //    while the caller reads a snapshot on this thread.
    while (m_run.load(std::memory_order_acquire) &&
           !m_idle.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
}

void PluginThread::requestFrame(int iState) {
    m_drawState.store(iState, std::memory_order_relaxed);

    // Measure FPS from the real Draw cadence on the game thread (this is the true
    // frame rate regardless of how often the worker actually rebuilds). EMA-smoothed;
    // m_fpsLastTp / m_fpsEma are touched only here (game thread), m_fps is the atomic
    // the worker reads. steady_clock is monotonic, so dt is always >= 0.
    auto now = std::chrono::steady_clock::now();
    if (m_fpsLastTp.time_since_epoch().count() != 0) {
        double dtMs = std::chrono::duration<double, std::milli>(now - m_fpsLastTp).count();
        if (dtMs > 0.0) {
            double inst = 1000.0 / dtMs;
            m_fpsEma = (m_fpsEma > 0.0f) ? (m_fpsEma * 0.9f + static_cast<float>(inst) * 0.1f)
                                         : static_cast<float>(inst);
            m_fps.store(m_fpsEma, std::memory_order_relaxed);
        }
    }
    m_fpsLastTp = now;

    {
        std::lock_guard<std::mutex> lk(m_qMutex);
        m_frameRequested = true;
    }
    m_cv.notify_one();
}

bool PluginThread::takeFrame(const SPluginQuad_t*& quads, int& numQuads,
                             const SPluginString_t*& strings, int& numStrings) {
    if (!m_frameBuffer.everProduced()) return false;
    // Adopt the latest finished frame. The returned reference stays valid until the
    // next takeFrame() (the buffer won't reuse this slot before then) — matching the
    // game's "read the quads after Draw returns" contract.
    const Frame& f = m_frameBuffer.acquire();
    quads = f.quads.empty() ? nullptr : f.quads.data();
    numQuads = static_cast<int>(f.quads.size());
    strings = f.strings.empty() ? nullptr : f.strings.data();
    numStrings = static_cast<int>(f.strings.size());
    return true;
}

void PluginThread::buildAndPublishFrame() {
    HudManager& hud = HudManager::getInstance();

    // Run the full update + collect on THIS (worker) thread. produceFrame() owns the
    // input poll, hotkeys, HUD rebuilds, companion submit and display-target gating —
    // everything the synchronous draw() used to do on the game thread. Time it so the
    // PerformanceHud / BenchmarkWidget stay live off-thread.
    long long buildStart = DrawHandler::getCurrentTimeUs();
    hud.produceFrame(m_drawState.load(std::memory_order_relaxed));
    long long buildUs = DrawHandler::getCurrentTimeUs() - buildStart;

    // Publish the plugin's per-frame metrics on the worker (the PluginData owner), so
    // the PerformanceHud reflects real numbers instead of a frozen last value. Plugin
    // time = this build + the event-callback time accumulated on the worker since the
    // last build (mirrors the sync-mode "total plugin time this frame", now off the
    // game thread). FPS comes from the true Draw cadence measured in requestFrame.
    {
        long long cbUs = DrawHandler::consumeAccumulatedCallbackTime();
        float fps = m_fps.load(std::memory_order_relaxed);
        float pluginMs = static_cast<float>(buildUs + cbUs) / 1000.0f;
        float budgetMs = (fps > 0.0f) ? (1000.0f / fps) : 16.67f;
        float pct = (budgetMs > 0.0f) ? (pluginMs / budgetMs) * 100.0f : 0.0f;
        if (fps > 0.0f) {
            PluginData::getInstance().updateDebugMetrics(fps, pluginMs, pct);
        }
        // Keep the benchmark widget's render-count row correct too (it's set in
        // DrawHandler in sync mode, which threaded Draw bypasses).
        auto& bm = PluginData::getInstance().getBenchmarkMetrics();
        if (bm.active) {
            bm.totalQuads = static_cast<int>(hud.gameFrameQuads().size());
            bm.totalStrings = static_cast<int>(hud.gameFrameStrings().size());
        }
    }

    // Copy the freshly built in-game frame into our write slot (the game reads a
    // stable copy, decoupled from HudManager's members which we overwrite next build).
    Frame& w = m_frameBuffer.writeSlot();
    if (hud.inGameFrameSuppressed()) {
        w.quads.clear();
        w.strings.clear();
    } else {
        w.quads = hud.gameFrameQuads();
        w.strings = hud.gameFrameStrings();
    }

    // Publish: the write slot becomes the latest ready frame; a recycled slot is taken
    // for the next build. Never touches the game's display slot.
    m_frameBuffer.publish();
}

void PluginThread::threadMain() {
    while (m_run.load(std::memory_order_acquire)) {
        std::deque<std::function<void()>> batch;
        bool doFrame = false;
        {
            std::unique_lock<std::mutex> lk(m_qMutex);
            m_idle.store(true, std::memory_order_release);
            m_cv.wait(lk, [this]() {
                return !m_run.load(std::memory_order_acquire) ||
                       !m_queue.empty() || m_frameRequested;
            });
            m_idle.store(false, std::memory_order_release);
            batch.swap(m_queue);
            doFrame = m_frameRequested;
            m_frameRequested = false;
        }

        // Execute queued callbacks in FIFO order (preserves the game's callback
        // ordering). Always finish the whole batch — even if a stop was signalled
        // mid-batch — so already-dequeued commands aren't silently dropped (stop()'s
        // drain only covers what's still in m_queue). Each is individually guarded so
        // one bad command can't kill the worker or skip the rest.
        for (auto& cmd : batch) {
            try { if (cmd) cmd(); } catch (...) {
                DEBUG_ERROR("PluginThread: queued callback threw");
            }
        }

        if (doFrame && m_run.load(std::memory_order_acquire)) {
            try { buildAndPublishFrame(); } catch (...) {
                DEBUG_ERROR("PluginThread: frame build threw");
            }
        }
    }
}
