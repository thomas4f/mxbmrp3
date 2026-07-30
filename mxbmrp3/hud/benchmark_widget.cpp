// ============================================================================
// hud/benchmark_widget.cpp
// Developer-only widget showing per-callback and per-HUD timing breakdown
// ============================================================================
#include "benchmark_widget.h"

#include <cstdio>
#include <cstring>
#include <algorithm>
#include <string>
#include <vector>
#include <ctime>

#include <windows.h>  // CreateDirectoryA

#include "../diagnostics/logger.h"
#include "../core/atomic_file_writer.h"
#include "../core/plugin_utils.h"
#include "../core/plugin_data.h"
#include "../core/plugin_thread.h"
#include "../core/color_config.h"
#include "../core/settings_manager.h"
#include "../game/game_config.h"  // GAME_NAME
#include "../handlers/draw_handler.h"

using namespace PluginConstants;

BenchmarkWidget::BenchmarkWidget() {
    DEBUG_INFO("BenchmarkWidget created");
    setDraggable(true);

    m_quads.reserve(4);  // background + separators
    // Pre-allocate strings for typical display (header + ~16 callbacks + separator + ~10 HUDs + footer)
    m_strings.reserve(60);

    m_callbackSnapshots.fill({});
    m_hudSnapshots.fill({});

    resetToDefaults();
    rebuildRenderData();
}

bool BenchmarkWidget::handlesDataType(DataChangeType dataType) const {
    return (dataType == DataChangeType::DebugMetrics);
}

// Keep metrics collection in step with whether this widget is shown ANYWHERE.
//
// `bm.active` is a PRODUCER gate — it switches on the instrumentation that fills
// the tables this widget displays — so it has to ask the same question as the
// consumer gate (isVisibleAnySurface()), exactly like isTelemetryHistoryNeeded()
// does for the telemetry history buffers. It used to be latched from
// setVisible(), i.e. from the GAME toggle only, which meant a widget enabled on
// the companion window collected nothing and rendered empty tables forever; the
// only way to get data was to enable it in-game as well.
//
// Driven per frame rather than from the setters on purpose: any-surface visibility
// also changes when the COMPANION WINDOW itself opens or closes, and no setter runs
// then.
//
// It does NOT see every transition, which an earlier version of this comment
// claimed. It sees every transition A FRAME LATER, and a caller that toggles and
// then tears down without drawing gets none of them -- bench_driver does exactly
// that (Benchmark(0) then Shutdown()), which silently stopped its report being
// written until MXBMRP3_Test_BenchmarkWidget was made to call this directly. Any
// caller needing the edge applied synchronously must do the same.
void BenchmarkWidget::syncCollectionState() {
    const bool visibleNow = isVisibleAnySurface();
    auto& bm = PluginData::getInstance().getBenchmarkMetrics();
    bm.active = visibleNow;

    if (visibleNow == m_bCollecting) return;
    m_bCollecting = visibleNow;

    if (!visibleNow) {
        // Export report when the widget leaves every surface.
        const std::string& savePath = SettingsManager::getInstance().getSavePath();
        if (!savePath.empty()) {
            exportReport(savePath.c_str());
        }
    }

    if (visibleNow) {
        // Reset snapshots when becoming visible
        m_snapshotCount = 0;
        m_hudSnapshotCount = 0;
        m_frameCounter = 0;
        m_callbackSnapshots.fill({});
        m_hudSnapshots.fill({});

        // Reset peak values in benchmark metrics, and start a fresh STINT accumulation
        // (the exported report's whole-session tables) on the same edge as the frame
        // sample window below, so every figure in a report covers the same span.
        for (int i = 0; i < bm.callbackCount; ++i) {
            bm.callbacks[i].peakTimeUs = 0;
            bm.callbacks[i].stintTotalTimeUs = 0;
            bm.callbacks[i].stintPeakTimeUs = 0;
            bm.callbacks[i].stintCallCount = 0;
        }
        for (int i = 0; i < bm.hudCount; ++i) {
            bm.huds[i].rebuildCount = 0;
            bm.huds[i].stintTotalTimeUs = 0;
            bm.huds[i].stintPeakTimeUs = 0;
            bm.huds[i].stintRebuildCount = 0;
        }

        // Start a new full-session FPS / duration window
        resetSessionStats();
        m_sessionStart = std::chrono::steady_clock::now();
    }
}

void BenchmarkWidget::resetSessionStats() {
    m_haveLastFrameTime = false;
    m_frameSampleCount = 0;
    m_frameSampleWrite = 0;
    m_frameSamplesWrapped = false;
    m_peakQuadCount = 0;
    m_peakStringCount = 0;
}

void BenchmarkWidget::sampleFrameTime() {
    auto now = std::chrono::steady_clock::now();
    if (m_haveLastFrameTime) {
        double deltaUs = std::chrono::duration<double, std::micro>(now - m_lastFrameTime).count();
        // Guard against zero/negative deltas (clock anomalies) — would produce inf FPS.
        if (deltaUs > 0.0) {
            ++m_frameSampleCount;

            // Store the raw sample; every frame-rate figure is derived from this ring
            // on export (min/avg/max FPS and the percentiles), so they can't diverge.
            // First sample of the process pays for the ring; every later one is a
            // plain indexed store (the buffer is sized once, never grown).
            if (m_frameSamples.empty()) m_frameSamples.resize(MAX_FRAME_SAMPLES);
            m_frameSamples[m_frameSampleWrite] = deltaUs;
            m_frameSampleWrite = (m_frameSampleWrite + 1) % MAX_FRAME_SAMPLES;
            if (m_frameSampleWrite == 0) m_frameSamplesWrapped = true;
        }
    }
    m_lastFrameTime = now;
    m_haveLastFrameTime = true;
}

void BenchmarkWidget::update() {
    // Before the early-out: the falling edge (widget hidden everywhere) has to be
    // seen to stop collection and export the report.
    syncCollectionState();

    if (!isVisibleAnySurface()) {
        clearDataDirty();
        clearLayoutDirty();
        return;
    }

    // Sample frame interval for full-session FPS stats (min/avg/max)
    sampleFrameTime();

    // Take snapshot at interval to keep display readable
    m_frameCounter++;
    if (m_frameCounter >= SNAPSHOT_INTERVAL_FRAMES) {
        takeSnapshot();
        m_frameCounter = 0;
    }

    // Always rebuild (data changes every frame when visible)
    rebuildRenderData();
    clearDataDirty();
    clearLayoutDirty();
}

void BenchmarkWidget::takeSnapshot() {
    auto& bm = PluginData::getInstance().getBenchmarkMetrics();

    // Snapshot callback timing (accumulated across entire interval)
    m_snapshotCount = bm.callbackCount;
    for (int i = 0; i < m_snapshotCount && i < MAX_CALLBACKS; ++i) {
        strncpy_s(m_callbackSnapshots[i].name, sizeof(m_callbackSnapshots[i].name),
                  bm.callbacks[i].name, _TRUNCATE);
        m_callbackSnapshots[i].totalTimeUs = static_cast<float>(bm.callbacks[i].totalTimeUs);
        m_callbackSnapshots[i].peakTimeUs = static_cast<float>(bm.callbacks[i].peakTimeUs);
        m_callbackSnapshots[i].callCount = bm.callbacks[i].callCount;
    }

    // Snapshot HUD rebuild timing
    m_hudSnapshotCount = bm.hudCount;
    for (int i = 0; i < m_hudSnapshotCount && i < MAX_HUD_SNAPSHOTS; ++i) {
        strncpy_s(m_hudSnapshots[i].name, sizeof(m_hudSnapshots[i].name),
                  bm.huds[i].name, _TRUNCATE);
        m_hudSnapshots[i].lastRebuildTimeUs = static_cast<float>(bm.huds[i].lastRebuildTimeUs);
        m_hudSnapshots[i].rebuildsInInterval = bm.huds[i].rebuildCount;
        m_hudSnapshots[i].stintTotalTimeUs = static_cast<float>(bm.huds[i].stintTotalTimeUs);
        m_hudSnapshots[i].stintRebuilds = bm.huds[i].stintRebuildCount;
        m_hudSnapshots[i].quadCount = bm.huds[i].quadCount;
        m_hudSnapshots[i].stringCount = bm.huds[i].stringCount;
    }

    // Snapshot aggregate metrics
    m_totalCallbackTimeUs = 0;
    for (int i = 0; i < m_snapshotCount; ++i) {
        m_totalCallbackTimeUs += m_callbackSnapshots[i].totalTimeUs;
    }
    m_collectRenderTimeUs = static_cast<float>(bm.collectRenderTimeUs);
    m_totalQuadCount = bm.totalQuads;
    m_totalStringCount = bm.totalStrings;
    if (m_totalQuadCount > m_peakQuadCount) m_peakQuadCount = m_totalQuadCount;
    if (m_totalStringCount > m_peakStringCount) m_peakStringCount = m_totalStringCount;

    // Reset all counters for next interval
    for (int i = 0; i < bm.callbackCount; ++i) {
        bm.callbacks[i].totalTimeUs = 0;
        bm.callbacks[i].peakTimeUs = 0;
        bm.callbacks[i].callCount = 0;
    }
    for (int i = 0; i < bm.hudCount; ++i) {
        bm.huds[i].rebuildCount = 0;
    }
}

void BenchmarkWidget::rebuildRenderData() {
    clearStrings();
    m_quads.clear();

    auto dim = getScaledDimensions();

    // Calculate layout
    float backgroundWidth = PluginUtils::calculateMonospaceTextWidth(CONTENT_WIDTH_CHARS, dim.fontSize)
        + dim.paddingH + dim.paddingH;

    // Count rows: title + header + callbacks + separator + HUD header + HUDs with rebuilds + footer
    int rowCount = 1;  // Title
    rowCount += 1;     // Callback section header
    int activeCallbacks = 0;
    for (int i = 0; i < m_snapshotCount; ++i) {
        if (m_callbackSnapshots[i].callCount > 0) {
            activeCallbacks++;
        }
    }
    rowCount += (activeCallbacks > 0) ? activeCallbacks : 1;  // At least "(no data)" row
    rowCount += 1;     // Blank separator
    rowCount += 1;     // HUD rebuilds header
    int activeHuds = 0;
    for (int i = 0; i < m_hudSnapshotCount; ++i) {
        // Anything that rebuilt at all this stint stays listed — a HUD that rebuilds once
        // a lap must not vanish from the table between laps.
        if (m_hudSnapshots[i].stintRebuilds > 0) {
            activeHuds++;
        }
    }
    rowCount += (activeHuds > 0) ? activeHuds : 1;  // At least "(none)" row
    rowCount += 1;     // Blank separator
    rowCount += 3;     // Footer (collect time, quads, strings)

    float titleHeight = m_bShowTitle ? dim.lineHeightLarge : 0.0f;
    float backgroundHeight = dim.paddingV + titleHeight + (rowCount * dim.lineHeightNormal) + dim.paddingV;

    setBounds(START_X, START_Y, START_X + backgroundWidth, START_Y + backgroundHeight);
    addBackgroundQuad(START_X, START_Y, backgroundWidth, backgroundHeight);

    float contentStartX = START_X + dim.paddingH;
    float currentY = START_Y + dim.paddingV;

    // Title
    addTitleString("Benchmark", contentStartX, currentY, Justify::LEFT,
        this->getFont(FontCategory::TITLE), this->getColor(ColorSlot::PRIMARY), dim.fontSizeLarge);
    currentY += titleHeight;

    // Column right-edge X positions for the value columns (right-aligned). Time columns
    // are 7 chars, count columns 5 chars, with 2-char gaps. Headers right-align to the
    // same X as their values, so they line up regardless of header font size.
    float rightEdge = contentStartX + PluginUtils::calculateMonospaceTextWidth(CONTENT_WIDTH_CHARS, dim.fontSize);
    float charW = PluginUtils::calculateMonospaceTextWidth(1, dim.fontSize);
    float colCalls = rightEdge;
    float colPeak  = rightEdge - 7.0f * charW;
    float colTotal = rightEdge - 16.0f * charW;
    float colCount = rightEdge;
    float colLast  = rightEdge - 7.0f * charW;
    float colHudTotal = rightEdge - 16.0f * charW;   // same three-column grid as CALLBACKS
    int labelFont = this->getFont(FontCategory::STRONG);
    int valueFont = this->getFont(FontCategory::DIGITS);
    unsigned long labelColor = this->getColor(ColorSlot::TERTIARY);

    // === CALLBACK SECTION ===
    addString("Callbacks", contentStartX, currentY, Justify::LEFT,
        labelFont, this->getColor(ColorSlot::PRIMARY), dim.fontSize);
    addLabel("Total us", colTotal, currentY, Justify::RIGHT, labelFont, labelColor, dim);
    addLabel("Peak us", colPeak, currentY, Justify::RIGHT, labelFont, labelColor, dim);
    addLabel("Calls", colCalls, currentY, Justify::RIGHT, labelFont, labelColor, dim);
    currentY += dim.lineHeightNormal;

    if (activeCallbacks == 0) {
        addString("(no data)", contentStartX, currentY, Justify::LEFT,
            this->getFont(FontCategory::NORMAL), this->getColor(ColorSlot::TERTIARY), dim.fontSize);
        currentY += dim.lineHeightNormal;
    } else {
        for (int i = 0; i < m_snapshotCount; ++i) {
            if (m_callbackSnapshots[i].callCount <= 0) continue;

            // Callback name (left-aligned)
            addString(m_callbackSnapshots[i].name, contentStartX, currentY, Justify::LEFT,
                this->getFont(FontCategory::NORMAL), this->getColor(ColorSlot::SECONDARY), dim.fontSize);

            // Color based on peak single-call time vs frame budget (2083us at 480fps)
            // Green < 520us (< 25%), Yellow < 1250us (< 60%), Red > 1250us
            unsigned long color;
            if (m_callbackSnapshots[i].peakTimeUs < 520.0f) {
                color = this->getColor(ColorSlot::POSITIVE);
            } else if (m_callbackSnapshots[i].peakTimeUs < 1250.0f) {
                color = this->getColor(ColorSlot::WARNING);
            } else {
                color = this->getColor(ColorSlot::NEGATIVE);
            }

            char buf[16];
            snprintf(buf, sizeof(buf), "%.0f", m_callbackSnapshots[i].totalTimeUs);
            addString(buf, colTotal, currentY, Justify::RIGHT, valueFont, color, dim.fontSize);
            snprintf(buf, sizeof(buf), "%.0f", m_callbackSnapshots[i].peakTimeUs);
            addString(buf, colPeak, currentY, Justify::RIGHT, valueFont, color, dim.fontSize);
            snprintf(buf, sizeof(buf), "%d", m_callbackSnapshots[i].callCount);
            addString(buf, colCalls, currentY, Justify::RIGHT, valueFont, color, dim.fontSize);
            currentY += dim.lineHeightNormal;
        }
    }

    // Separator
    currentY += dim.lineHeightNormal;

    // === HUD REBUILD SECTION ===
    addString("HUD rebuilds", contentStartX, currentY, Justify::LEFT,
        labelFont, this->getColor(ColorSlot::PRIMARY), dim.fontSize);
    addLabel("Total ms", colHudTotal, currentY, Justify::RIGHT, labelFont, labelColor, dim);
    addLabel("Avg us", colLast, currentY, Justify::RIGHT, labelFont, labelColor, dim);
    addLabel("Builds", colCount, currentY, Justify::RIGHT, labelFont, labelColor, dim);
    currentY += dim.lineHeightNormal;

    if (activeHuds == 0) {
        addString("(none)", contentStartX, currentY, Justify::LEFT,
            this->getFont(FontCategory::NORMAL), this->getColor(ColorSlot::TERTIARY), dim.fontSize);
        currentY += dim.lineHeightNormal;
    } else {
        int hudOrder[MAX_HUD_SNAPSHOTS]; int nHudOrder = 0;
        for (int i = 0; i < m_hudSnapshotCount && nHudOrder < MAX_HUD_SNAPSHOTS; ++i)
            if (m_hudSnapshots[i].stintRebuilds > 0) hudOrder[nHudOrder++] = i;
        // Heaviest total first: the whole point of the stint view is that it reorders the
        // list by what is actually costing CPU, which the per-interval view cannot show.
        std::sort(hudOrder, hudOrder + nHudOrder, [&](int a2, int b2) {
            return m_hudSnapshots[a2].stintTotalTimeUs > m_hudSnapshots[b2].stintTotalTimeUs;
        });
        for (int k = 0; k < nHudOrder; ++k) {
            const auto& h = m_hudSnapshots[hudOrder[k]];

            // HUD name
            addString(h.name, contentStartX, currentY, Justify::LEFT,
                this->getFont(FontCategory::NORMAL), this->getColor(ColorSlot::SECONDARY), dim.fontSize);

            // Colour on the AVERAGE rebuild, not the last one: one expensive rebuild is
            // not a problem, one that happens every frame is. Same 100/500us bands.
            const float avgUs = (h.stintRebuilds > 0) ? h.stintTotalTimeUs / h.stintRebuilds : 0.0f;
            unsigned long color;
            if (avgUs < 100.0f) {
                color = this->getColor(ColorSlot::POSITIVE);
            } else if (avgUs < 500.0f) {
                color = this->getColor(ColorSlot::WARNING);
            } else {
                color = this->getColor(ColorSlot::NEGATIVE);
            }

            char buf[16];
            snprintf(buf, sizeof(buf), "%.1f", h.stintTotalTimeUs / 1000.0f);
            addString(buf, colHudTotal, currentY, Justify::RIGHT, valueFont, color, dim.fontSize);
            snprintf(buf, sizeof(buf), "%.0f", avgUs);
            addString(buf, colLast, currentY, Justify::RIGHT, valueFont, color, dim.fontSize);
            snprintf(buf, sizeof(buf), "%d", h.stintRebuilds);
            addString(buf, colCount, currentY, Justify::RIGHT, valueFont, color, dim.fontSize);
            currentY += dim.lineHeightNormal;
        }
    }

    // Separator
    currentY += dim.lineHeightNormal;

    // === FOOTER - Aggregate stats ===
    char footer[64];

    snprintf(footer, sizeof(footer), "Collect render: %.0f us", m_collectRenderTimeUs);
    addString(footer, contentStartX, currentY, Justify::LEFT,
        this->getFont(FontCategory::NORMAL), this->getColor(ColorSlot::SECONDARY), dim.fontSize);
    currentY += dim.lineHeightNormal;

    snprintf(footer, sizeof(footer), "Quads: %d", m_totalQuadCount);
    addString(footer, contentStartX, currentY, Justify::LEFT,
        this->getFont(FontCategory::NORMAL), this->getColor(ColorSlot::SECONDARY), dim.fontSize);

    snprintf(footer, sizeof(footer), "Strings: %d", m_totalStringCount);
    addString(footer, rightEdge, currentY, Justify::RIGHT,
        this->getFont(FontCategory::NORMAL), this->getColor(ColorSlot::SECONDARY), dim.fontSize);
    currentY += dim.lineHeightNormal;

    // Total callback time
    snprintf(footer, sizeof(footer), "Total callback: %.0f us (%.2f ms)",
             m_totalCallbackTimeUs, m_totalCallbackTimeUs / 1000.0f);
    addString(footer, contentStartX, currentY, Justify::LEFT,
        this->getFont(FontCategory::NORMAL), this->getColor(ColorSlot::PRIMARY), dim.fontSize);
}

bool BenchmarkWidget::exportReport(const char* savePath) const {
    if (!savePath || savePath[0] == '\0') return false;

    // Build directory path: savePath/mxbmrp3/benchmarks/ — reports get their own subfolder
    // so they don't clutter the plugin config root (which holds settings/stats/log).
    std::string dir = std::string(savePath);
    if (!dir.empty() && dir.back() != '/' && dir.back() != '\\') {
        dir += '\\';
    }
    dir += "mxbmrp3";
    // Ensure the parent (usually already there via logger/settings) and the benchmarks
    // subfolder exist. CreateDirectoryA is idempotent (ignores ERROR_ALREADY_EXISTS) and
    // AtomicFileWriter won't create parents on its own.
    CreateDirectoryA(dir.c_str(), nullptr);
    dir += "\\benchmarks";
    CreateDirectoryA(dir.c_str(), nullptr);

    // Build file path with timestamp: savePath/mxbmrp3/benchmarks/benchmark_YYYYMMDD_HHMMSS.txt
    time_t now = time(nullptr);
    struct tm timeInfo;
    // localtime_s writes timeInfo (output param); cppcheck can't model that.
    // cppcheck-suppress uninitvar
    localtime_s(&timeInfo, &now);
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &timeInfo);
    char fileTimestamp[20];
    strftime(fileTimestamp, sizeof(fileTimestamp), "%Y%m%d_%H%M%S", &timeInfo);
    std::string filePath = dir + "\\benchmark_" + fileTimestamp + ".txt";

    // Build the whole report into a string, then hand it to the shared atomic writer (temp
    // file + MoveFileExA replace) — the same path every persisted file uses. snprintf per line
    // preserves the exact column formatting the old fprintf calls produced.
    std::string out;
    char line[256];

    out += "MXBMRP3 Benchmark Report\n";
    snprintf(line, sizeof(line), "Version: %s\n", PluginConstants::PLUGIN_VERSION); out += line;
    snprintf(line, sizeof(line), "Date: %s\n", timestamp); out += line;
    snprintf(line, sizeof(line), "Snapshot interval: %d frames\n", SNAPSHOT_INTERVAL_FRAMES); out += line;

    // 480fps frame budget. Cost is expressed as a share of this so the report is
    // self-describing — an analyzer doesn't have to know the target out of band.
    const double BUDGET_US = 2083.0;
    const int    TARGET_FPS = 480;

    // --- CONTEXT: makes two reports comparable and attributable to a scenario ----
    const auto& sd = PluginData::getInstance().getSessionData();
    int riders = static_cast<int>(PluginData::getInstance().getRaceEntries().size());
    bool threaded = PluginThread::getInstance().enabled();
    const char* trackName = (sd.trackName[0] != '\0') ? sd.trackName : "(unknown)";
    out += "\n=== CONTEXT ===\n";
    snprintf(line, sizeof(line), "Game: %s\n", GAME_NAME); out += line;
    snprintf(line, sizeof(line), "Track: %s (%.0f m)\n", trackName, sd.trackLength); out += line;
    snprintf(line, sizeof(line), "Riders: %d\n", riders); out += line;
    snprintf(line, sizeof(line), "HUDs profiled: %d\n", m_hudSnapshotCount); out += line;
    snprintf(line, sizeof(line), "Render mode: %s\n", threaded ? "threaded" : "synchronous"); out += line;
    snprintf(line, sizeof(line), "Target: %d fps (%.0f us/frame budget)\n", TARGET_FPS, BUDGET_US); out += line;

    // --- FRAME RATE: min/avg/max PLUS the distribution tail that actually matters
    // at high refresh (a single average hides stutter). Guard against
    // m_sessionStart being default-constructed (export before any setVisible).
    double durationSec = 0.0;
    if (m_sessionStart != std::chrono::steady_clock::time_point{}) {
        durationSec = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - m_sessionStart).count();
    }
    // Every frame-rate figure below comes from THIS one sorted window, so the FPS
    // min/max and the frame-time max can't disagree (they're the same samples).
    double ftP50 = 0.0, ftP99 = 0.0, ftMax = 0.0, low1Fps = 0.0;
    double fpsMin = 0.0, fpsAvg = 0.0, fpsMax = 0.0;
    int nSamples = m_frameSamplesWrapped ? MAX_FRAME_SAMPLES : m_frameSampleWrite;
    if (m_frameSamples.empty()) nSamples = 0;   // never sampled: the ring was never allocated
    if (nSamples > 0) {
        std::vector<double> s(m_frameSamples.begin(), m_frameSamples.begin() + nSamples);
        std::sort(s.begin(), s.end());
        auto at = [&](double p){ return s[static_cast<size_t>(p * (s.size() - 1))]; };
        ftP50 = at(0.50); ftP99 = at(0.99); ftMax = s.back();
        double ftMin = s.front();
        double sum = 0.0; for (double v : s) sum += v;
        double ftMean = (sum > 0.0) ? sum / s.size() : 0.0;
        fpsMax = (ftMin  > 0.0) ? 1.0e6 / ftMin  : 0.0;   // shortest frame = highest FPS
        fpsMin = (ftMax  > 0.0) ? 1.0e6 / ftMax  : 0.0;   // longest frame  = lowest FPS
        fpsAvg = (ftMean > 0.0) ? 1.0e6 / ftMean : 0.0;
        low1Fps = (ftP99 > 0.0) ? 1.0e6 / ftP99  : 0.0;   // 1% low = FPS at the p99 frame
    }
    out += "\n=== FRAME RATE ===\n";
    // Two DIFFERENT counts, deliberately both reported: m_frameSampleCount is every
    // frame of the session, but every figure below derives from the ring window
    // (last <= MAX_FRAME_SAMPLES frames). Labelling the session total as "sampled"
    // read as if a 10-minute capture's 288k frames backed the p99.
    snprintf(line, sizeof(line), "Duration: %.2f s (%lld frames total; %d in the sampled window)\n",
             durationSec, m_frameSampleCount, nSamples); out += line;
    if (m_frameSampleCount > 0) {
        // "(window)" is load-bearing: these derive from the ring, not the
        // session, so a 10-minute capture prints Duration: 600s next to an FPS
        // minimum drawn from the last ~34s. The Duration line already says how
        // many frames are in the window; these say they are the ones using it.
        snprintf(line, sizeof(line), "FPS (window): min %.1f, avg %.1f, max %.1f\n", fpsMin, fpsAvg, fpsMax); out += line;
        snprintf(line, sizeof(line), "Frame time (window): p50 %.0f us, p99 %.0f us, max %.0f us\n", ftP50, ftP99, ftMax); out += line;
        snprintf(line, sizeof(line), "1%% low: %.1f fps  (FPS at the p99 frame time -- the stutter metric)\n", low1Fps); out += line;
    } else {
        out += "FPS: (no samples)\n";
    }

    // Callback section. Avg = Total/Calls (per-call cost); %bud = peak call vs the
    // 480fps budget (how close a single call comes to blowing a frame).
    // Whole-stint, deliberately: this file is written when the widget is HIDDEN, so a
    // last-interval table would report whichever ~0.25s you happened to stop on. The
    // live HUD keeps the rolling interval — that is the view that makes sense on screen.
    out += "\n=== CALLBACKS (whole stint) ===\n";
    snprintf(line, sizeof(line), "%-24s %12s %9s %10s %10s %6s\n", "Name", "Total us", "Avg us", "Peak us", "Calls", "%bud"); out += line;
    snprintf(line, sizeof(line), "%-24s %12s %9s %10s %10s %6s\n", "------------------------", "------------", "---------", "----------", "----------", "------"); out += line;
    {
        const auto& bm = PluginData::getInstance().getBenchmarkMetrics();
        for (int i = 0; i < bm.callbackCount; ++i) {
            const auto& c = bm.callbacks[i];
            if (c.stintCallCount <= 0) continue;   // never fired this stint
            const double avgUs = static_cast<double>(c.stintTotalTimeUs) / c.stintCallCount;
            const double pctBud = 100.0 * static_cast<double>(c.stintPeakTimeUs) / BUDGET_US;
            snprintf(line, sizeof(line), "%-24s %12lld %9.1f %10lld %10d %5.1f%%\n",
                    c.name, c.stintTotalTimeUs, avgUs, c.stintPeakTimeUs,
                    c.stintCallCount, pctBud); out += line;
        }
    }
    out += "\n";

    // HUD rebuilds section
    // Per-HUD: rebuild cost (CPU build) AND primitives emitted (the render handoff —
    // which HUD hands the engine the most to draw). Sorted by strings desc, since text
    // is the priciest primitive, so the top row is the heaviest render-handoff HUD.
    // Primitive counts only. These ARE a snapshot by nature (what each HUD last handed
    // the engine); the timing that used to share this table is the stint table's job.
    out += "=== HUD RENDER FOOTPRINT (primitives emitted) ===\n";
    snprintf(line, sizeof(line), "%-24s %8s %8s\n", "Name", "Quads", "Strings"); out += line;
    snprintf(line, sizeof(line), "%-24s %8s %8s\n", "------------------------", "--------", "--------"); out += line;
    int order[MAX_HUD_SNAPSHOTS]; int nOrder = 0;
    for (int i = 0; i < m_hudSnapshotCount; ++i) {
        const auto& h = m_hudSnapshots[i];
        if (h.rebuildsInInterval > 0 || h.lastRebuildTimeUs > 0 || h.quadCount > 0 || h.stringCount > 0)
            order[nOrder++] = i;
    }
    std::sort(order, order + nOrder, [&](int a, int b){
        if (m_hudSnapshots[a].stringCount != m_hudSnapshots[b].stringCount)
            return m_hudSnapshots[a].stringCount > m_hudSnapshots[b].stringCount;
        return m_hudSnapshots[a].quadCount > m_hudSnapshots[b].quadCount;
    });
    for (int k = 0; k < nOrder; ++k) {
        const auto& h = m_hudSnapshots[order[k]];
        snprintf(line, sizeof(line), "%-24s %8d %8d\n",
                h.name, h.quadCount, h.stringCount); out += line;
    }
    out += "\n";

    // Per-HUD rebuild cost over the whole stint, heaviest first. This is the table that
    // answers "which HUD cost me CPU?" — the FOOTPRINT table above counts primitives, and
    // the live HUD's own view is a rolling interval. Read from the live metrics rather
    // than the widget's per-interval snapshot copies.
    //
    // FORMAT: keep in step with parse_stint() in tools/benchmark_report.py, which matches
    // these rows with an anchored regex — a column change silently stops it matching.
    // run_perf.sh gates that coupling.
    {
        const auto& bm = PluginData::getInstance().getBenchmarkMetrics();
        out += "=== HUD REBUILDS (STINT TOTALS) ===\n";
        snprintf(line, sizeof(line), "%-24s %12s %9s %10s %10s\n",
                 "Name", "Total us", "Avg us", "Peak us", "Rebuilds"); out += line;
        snprintf(line, sizeof(line), "%-24s %12s %9s %10s %10s\n",
                 "------------------------", "------------", "---------", "----------", "----------"); out += line;
        int ord[MAX_HUD_SNAPSHOTS]; int nOrd = 0;
        for (int i = 0; i < bm.hudCount && nOrd < MAX_HUD_SNAPSHOTS; ++i)
            if (bm.huds[i].stintRebuildCount > 0) ord[nOrd++] = i;
        // Heaviest total first — that is the "what should I look at" ordering, and it is
        // the one the per-interval table cannot give you.
        std::sort(ord, ord + nOrd, [&](int a2, int b2) {
            return bm.huds[a2].stintTotalTimeUs > bm.huds[b2].stintTotalTimeUs;
        });
        for (int k = 0; k < nOrd; ++k) {
            const auto& h = bm.huds[ord[k]];
            const double avg = static_cast<double>(h.stintTotalTimeUs) / h.stintRebuildCount;
            snprintf(line, sizeof(line), "%-24s %12lld %9.1f %10lld %10d\n",
                     h.name, h.stintTotalTimeUs, avg, h.stintPeakTimeUs, h.stintRebuildCount); out += line;
        }
        out += "\n";
    }

    // Render handoff: the primitives the ENGINE draws each frame — a cost the CPU
    // times above do NOT include (the engine renders after Draw() returns). Peak is
    // the session high-water mark. Count is the proxy; fill-rate (full-screen
    // overlays) is not captured by count. See the perf drivers for the same numbers.
    out += "=== RENDER HANDOFF (engine draws these; not in the CPU times above) ===\n";
    snprintf(line, sizeof(line), "Quads:   %d now, %d peak\n", m_totalQuadCount, m_peakQuadCount); out += line;
    snprintf(line, sizeof(line), "Strings: %d now, %d peak\n", m_totalStringCount, m_peakStringCount); out += line;
    out += "\n";

    // Aggregate + budget attribution. NOTE: callback "total" accumulates over the
    // snapshot interval, so it is NOT a single-frame figure — per-frame attribution
    // lives in the %bud column of the CALLBACKS table. collectRenderData() IS
    // per-frame, so it gets a budget share here.
    double collectPct = 100.0 * m_collectRenderTimeUs / BUDGET_US;
    out += "=== AGGREGATE ===\n";
    snprintf(line, sizeof(line), "Total callback time: %.0f us (%.2f ms over the %d-frame interval)\n",
             m_totalCallbackTimeUs, m_totalCallbackTimeUs / 1000.0f, SNAPSHOT_INTERVAL_FRAMES); out += line;
    snprintf(line, sizeof(line), "Collect render time: %.0f us/frame (%.1f%% of the 480fps budget)\n",
             m_collectRenderTimeUs, collectPct); out += line;
    out += "\n";

    // Machine-readable one-liner for tools/benchmark_report.py (stable key=value;
    // parse this rather than the human tables). Wider buffer — the track name alone
    // can be 100 chars.
    char bench[512];
    snprintf(bench, sizeof(bench),
        "BENCH game=\"%s\" track=\"%s\" riders=%d huds=%d threaded=%d target_fps=%d budget_us=%.0f "
        "dur_s=%.2f frames=%lld frames_sampled=%d fps_min=%.1f fps_avg=%.1f fps_max=%.1f "
        "ft_p50_us=%.0f ft_p99_us=%.0f ft_max_us=%.0f lowfps_1pct=%.1f "
        "quads=%d quads_peak=%d strings=%d strings_peak=%d collect_us=%.0f cb_total_us=%.0f\n",
        GAME_NAME, trackName, riders, m_hudSnapshotCount, threaded ? 1 : 0, TARGET_FPS, BUDGET_US,
        durationSec, m_frameSampleCount, nSamples, fpsMin, fpsAvg, fpsMax,
        ftP50, ftP99, ftMax, low1Fps,
        m_totalQuadCount, m_peakQuadCount, m_totalStringCount, m_peakStringCount,
        static_cast<double>(m_collectRenderTimeUs), static_cast<double>(m_totalCallbackTimeUs));
    out += bench;

    if (!AtomicFileWriter::writeFileAtomic(filePath, out)) {
        DEBUG_WARN_F("BenchmarkWidget: Failed to write %s", filePath.c_str());
        return false;
    }
    DEBUG_INFO_F("BenchmarkWidget: Report exported to %s", filePath.c_str());
    return true;
}

void BenchmarkWidget::resetToDefaults() {
    m_bVisible = false;    // Hidden by default
    m_bShowTitle = true;
    setTextureVariant(0);
    m_fBackgroundOpacity = 0.90f;
    m_fScale = 1.0f;
    setPosition(0.01f, 0.30507f);  // Left side of screen

    m_frameCounter = 0;
    m_snapshotCount = 0;
    m_hudSnapshotCount = 0;
    m_totalCallbackTimeUs = 0.0f;
    m_collectRenderTimeUs = 0.0f;
    m_totalQuadCount = 0;
    m_totalStringCount = 0;

    m_callbackSnapshots.fill({});
    m_hudSnapshots.fill({});

    resetSessionStats();
    m_sessionStart = std::chrono::steady_clock::time_point{};

    setDataDirty();
}
