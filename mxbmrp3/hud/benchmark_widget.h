// ============================================================================
// hud/benchmark_widget.h
// Developer-only widget showing per-callback and per-HUD timing breakdown
// Requires developerMode=1 in INI to be accessible
// ============================================================================
#pragma once

#include "base_hud.h"
#include "../core/plugin_constants.h"
#include "../core/plugin_data_types.h"   // BenchmarkMetrics::MAX_* -- see the caps below
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
        // THE WHOLE update() CALL, rebuild and idle together. The live table used to
        // carry only rebuild figures, so a HUD that costs time WITHOUT rebuilding --
        // position_widget at 2.22us/frame, clock_widget at 1.19, both with zero
        // rebuilds -- was not merely mis-ranked, it was absent. Same data the
        // exported report grew for the same reason.
        float stintUpdateTimeUs;
        int   stintUpdateCount;
    };

    // DERIVED FROM THE REGISTRY'S OWN SIZE, because this was a second, independent
    // `32` that had to track BenchmarkMetrics::MAX_HUDS and silently didn't. Raising
    // MAX_HUDS to 64 left this at 32; takeSnapshot() then stored an unclamped
    // m_hudSnapshotCount of 40 while filling only 32 entries, and every reader -- the
    // live table, the exported report, the BENCH line -- walked 40 entries of a
    // 32-element array. Out-of-bounds READS, not writes: the copy loops were capped,
    // the COUNT was not. It reached a user as report rows with garbage names and
    // negative quad/string counts. Pinned by benchmark_registry_test.
    static constexpr int MAX_HUD_SNAPSHOTS = BenchmarkMetrics::MAX_HUDS;

    // How many HUD rows the LIVE panel shows. The exported report lists every panel
    // and should; this one has to fit on screen next to the game, and with all 40
    // enabled an uncapped list runs off the bottom. The two views answer different
    // questions -- "what should I look at" here, "what did everything cost" there --
    // so the cap belongs on this side only, with the remainder counted rather than
    // silently dropped.
    static constexpr int MAX_LIVE_HUD_ROWS = 12;
    std::array<HudRebuildSnapshot, MAX_HUD_SNAPSHOTS> m_hudSnapshots;
    // Allow SettingsManager to access private members
    friend class SettingsManager;

    // The snapshot array's capacity and the count currently stored in it. The
    // count must never exceed the capacity -- see MAX_HUD_SNAPSHOTS for the
    // report corruption that happens when it does.
    static constexpr int snapshotCapacity() { return MAX_HUD_SNAPSHOTS; }
    int snapshotCount() const { return m_hudSnapshotCount; }

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

    // DERIVED, never restated -- see MAX_HUD_SNAPSHOTS for what a second
    // hand-written copy of a registry's size costs.
    static constexpr int MAX_CALLBACKS = BenchmarkMetrics::MAX_CALLBACKS;
    std::array<CallbackSnapshot, MAX_CALLBACKS> m_callbackSnapshots;
    int m_snapshotCount = 0;

    int m_hudSnapshotCount = 0;

    // Aggregate metrics snapshot
    float m_totalCallbackTimeUs = 0.0f;
    // Draw's per-frame average, for the footer's attribution line and its shares.
    float m_drawAvgUs = 0.0f;
    float m_collectRenderTimeUs = 0.0f;
    // The other two thirds of Draw -- see BenchmarkMetrics for why it is split.
    float m_updateHudsTimeUs = 0.0f;
    float m_framePollTimeUs = 0.0f;
    float m_frameHeadTimeUs = 0.0f;
    float m_frameTailTimeUs = 0.0f;
    float m_frameHudInputTimeUs = 0.0f;
    float m_planChainTimeUs = 0.0f;
    float m_planPanelTimeUs = 0.0f;
    long long m_planChainCalls = 0;
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
