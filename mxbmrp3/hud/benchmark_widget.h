// ============================================================================
// hud/benchmark_widget.h
// Developer-only widget showing per-callback and per-HUD timing breakdown
// Requires developerMode=1 in INI to be accessible
// ============================================================================
#pragma once

#include "base_hud.h"
#include "../core/plugin_constants.h"
#include <array>

class BenchmarkWidget : public BaseHud {
public:
    BenchmarkWidget();
    virtual ~BenchmarkWidget() = default;

    void update() override;
    bool handlesDataType(DataChangeType dataType) const override;
    void setVisible(bool visible) override;
    void resetToDefaults();

    // Export current snapshot data to a text file in the save path
    // Returns true if export succeeded
    bool exportReport(const char* savePath) const;

    // Allow SettingsManager to access private members
    friend class SettingsManager;

private:
    void rebuildRenderData() override;

    // Layout constants
    static constexpr float START_X = 0.0f;
    static constexpr float START_Y = 0.0f;
    static constexpr int CONTENT_WIDTH_CHARS = 40;  // Wide enough for "CallbackName    0.123ms  Peak 0.456ms"

    // Snapshot interval - update displayed values at a readable rate
    static constexpr int SNAPSHOT_INTERVAL_FRAMES = 30;  // Update ~8x per second at 240fps
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
    };

    static constexpr int MAX_HUD_SNAPSHOTS = 32;
    std::array<HudRebuildSnapshot, MAX_HUD_SNAPSHOTS> m_hudSnapshots;
    int m_hudSnapshotCount = 0;

    // Aggregate metrics snapshot
    float m_totalCallbackTimeUs = 0.0f;
    float m_collectRenderTimeUs = 0.0f;
    int m_totalQuadCount = 0;
    int m_totalStringCount = 0;

    void takeSnapshot();
};
