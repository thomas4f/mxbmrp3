// ============================================================================
// hud/benchmark_widget.h
// Developer-only widget showing per-callback and per-HUD timing breakdown
// Requires developerMode=1 in INI to be accessible
// ============================================================================
#pragma once

#include "base_hud.h"
#include "../core/plugin_constants.h"
#include <array>
#include <vector>
#include <chrono>

class BenchmarkWidget : public BaseHud {
public:
    BenchmarkWidget();
    virtual ~BenchmarkWidget() = default;

    void update() override;
    bool handlesDataType(DataChangeType dataType) const override;
    void resetToDefaults();

    // Export current snapshot data to a text file in the save path
    // Returns true if export succeeded
    bool exportReport(const char* savePath) const;

    // Allow SettingsManager to access private members
    friend class SettingsManager;

    // Drives bm.active from isVisibleAnySurface() and fires the show/hide edges.
    // See the definition for why this is per-frame rather than in setVisible().
    //
    // PUBLIC because the collection switch is no longer synchronous with setVisible():
    // a caller that toggles the widget and immediately tears down (the headless bench
    // driver does exactly this -- Benchmark(0) then Shutdown(), no Draw between) would
    // otherwise never reach the falling edge and never get its report. Callers that
    // need the transition applied NOW call this; in game the next Draw does it.
    void syncCollectionState();
    void onVisibilityChanged() override { syncCollectionState(); }

private:
    void rebuildRenderData() override;

    // Whether collection is currently on, i.e. the widget was visible on SOME
    // surface last frame. Edge-detection state for syncCollectionState(); not the
    // same as m_bVisible, which is the game surface alone.
    bool m_bCollecting = false;

    // Layout constants
    static constexpr float START_X = 0.0f;
    static constexpr float START_Y = 0.0f;
    static constexpr int CONTENT_WIDTH_CHARS = 40;  // Wide enough for "CallbackName    0.123ms  Peak 0.456ms"

    // Snapshot interval - update displayed values at a readable rate
    static constexpr int SNAPSHOT_INTERVAL_FRAMES = 30;  // Update ~16x per second at 480fps
    int m_frameCounter = 0;

    // Snapshot of timing data (updated every SNAPSHOT_INTERVAL_FRAMES)
    struct CallbackSnapshot {
        char name[24];
        float totalTimeUs;     // Total accumulated time over snapshot interval
        float peakTimeUs;    // Peak time over snapshot interval
        int callCount;       // Calls during snapshot interval
    };

    static constexpr int MAX_CALLBACKS = 32;
    std::array<CallbackSnapshot, MAX_CALLBACKS> m_callbackSnapshots;
    int m_snapshotCount = 0;

    // HUD rebuild snapshots
    struct HudRebuildSnapshot {
        char name[24];
        float lastRebuildTimeUs;
        int rebuildsInInterval;
        // Stint totals (since the widget was shown). The live table shows THESE, not the
        // per-interval pair above: "Last us" never decays, so a HUD that rebuilds once a
        // lap reads on screen as if it cost that every frame. Total + count is what says
        // whether a HUD is actually eating CPU.
        float stintTotalTimeUs;
        int stintRebuilds;
        int quadCount;      // primitives this HUD emits into the frame (render handoff)
        int stringCount;
    };

    static constexpr int MAX_HUD_SNAPSHOTS = 32;
    std::array<HudRebuildSnapshot, MAX_HUD_SNAPSHOTS> m_hudSnapshots;
    int m_hudSnapshotCount = 0;

    // Aggregate metrics snapshot
    float m_totalCallbackTimeUs = 0.0f;
    float m_collectRenderTimeUs = 0.0f;
    int m_totalQuadCount = 0;
    int m_totalStringCount = 0;

    // Benchmark session FPS / duration tracking (full-session, not per-snapshot)
    std::chrono::steady_clock::time_point m_sessionStart{};
    std::chrono::steady_clock::time_point m_lastFrameTime{};
    bool m_haveLastFrameTime = false;
    long long m_frameSampleCount = 0;   // total frames sampled this session

    // Rolling frame-time samples for ALL frame-rate stats (min/avg/max FPS AND the
    // percentiles / 1% low). A single average hides stutter — at high refresh the
    // "feel" lives in the tail, so the headline metric is p99 frame time (=1% low
    // FPS). Deriving every frame-rate figure from THIS one window keeps them
    // mutually consistent (no all-time vs windowed "max" mismatch). Ring buffer of
    // the most recent MAX_FRAME_SAMPLES intervals; older ones age out (bounded mem).
    static constexpr int MAX_FRAME_SAMPLES = 16384;
    // Allocated on first sample, not at construction. As a std::array this was
    // 128 KB resident in every player's process for a dev tool almost nobody
    // enables; as a vector it costs one pointer until the widget actually runs.
    // Sized once to MAX_FRAME_SAMPLES (never grown), so the hot path below is
    // still a plain indexed store into a fixed buffer.
    std::vector<double> m_frameSamples;
    int m_frameSampleWrite = 0;
    bool m_frameSamplesWrapped = false;

    // Session high-water mark of the primitive counts handed to the engine (the
    // render-handoff cost the CPU times don't capture — see the perf drivers).
    int m_peakQuadCount = 0;
    int m_peakStringCount = 0;

    void resetSessionStats();
    void sampleFrameTime();

    void takeSnapshot();
};
