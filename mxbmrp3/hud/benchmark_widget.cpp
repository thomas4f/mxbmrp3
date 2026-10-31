// ============================================================================
// hud/benchmark_widget.cpp
// The Benchmark profiler: per-callback and per-HUD timing, frame sampling, the live panel
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
#include "../core/plugin_utils.h"
#include "../core/plugin_data.h"
#include "../core/plugin_thread.h"
#include "../core/color_config.h"
#include "../core/atomic_file_writer.h"
#include "../core/settings_manager.h"
#include "../core/system_messages.h"
#include "../core/hud_manager.h"
#include "../game/game_config.h"  // GAME_NAME
#include "../handlers/draw_handler.h"
#include "settings_hud.h"

using namespace PluginConstants;

BenchmarkWidget::BenchmarkWidget() {
    m_panelKind = PanelKind::Widget;
    // Body card: a title over a table of rows is exactly the shape the card exists
    // for; without one, under a theme this table sits straight on the frame's fill
    // while every other one sits in a well. The box-model plan (addPlanBackground)
    // emits the card.
    m_bContentCard = true;
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
// does for the telemetry history buffers. Latched from the GAME toggle only, a
// widget enabled on the companion window collects nothing and renders empty
// tables forever.
//
// Driven per frame rather than from the setters on purpose: any-surface visibility
// also changes when the COMPANION WINDOW itself opens or closes, and no setter runs
// then.
//
// It does NOT see every transition: it sees every transition A FRAME LATER, and a
// caller that toggles and then tears down without drawing gets none of them --
// bench_driver does exactly that (Benchmark(0) then Shutdown()), so
// MXBMRP3_Test_BenchmarkWidget calls this directly. Any caller needing the edge
// applied synchronously must do the same.
void BenchmarkWidget::syncCollectionState() {
    const bool visibleNow = isVisibleAnySurface();
    auto& bm = PluginData::getInstance().getBenchmarkMetrics();
    bm.active = visibleNow;

    if (visibleNow == m_bCollecting) return;
    m_bCollecting = visibleNow;

    if (!visibleNow) {
        // Export report when the widget leaves every surface, and say where it went
        // once the writer has landed it: the file otherwise just appears in a folder
        // the player has to know about. Only here, where the player switched it off:
        // the saves on leaving the track and at quit (saveStint) have no Draw after
        // them, so a card would turn up on the next ride or never.
        const std::string& savePath = SettingsManager::getInstance().getSavePath();
        if (!savePath.empty()) {
            if (exportReport(savePath.c_str(), &m_reportPath)) {
                m_reportCheckPending = true;
                m_reportCheckAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(1500);
            } else {
                m_reportPath.clear();
                m_reportCheckPending = true;
                m_reportCheckAt = std::chrono::steady_clock::now();
            }
        }
    }

    if (visibleNow) {
        // Reset snapshots when becoming visible
        m_snapshotCount = 0;
        m_hudSnapshotCount = 0;
        m_frameCounter = 0;
        m_callbackSnapshots.fill({});
        m_hudSnapshots.fill({});

        beginStint();
    }
}

// Zero every stint accumulator and restart the frame clock (the rising edge). The
// callback/HUD stint totals restart on the SAME edge as the frame record, so
// every figure in a report covers the same span.
void BenchmarkWidget::beginStint() {
    auto& bm = PluginData::getInstance().getBenchmarkMetrics();
    bm.sampleMaxCallUs = 0;
    bm.sampleMaxCallIdx = -1;
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
        bm.huds[i].stintUpdateTimeUs = 0;
        bm.huds[i].stintUpdateCount = 0;
    }
    // ON THE SAME EDGE as the callback and HUD stint totals, so the Draw split
    // and the Draw average it is compared against cover the same frames.
    bm.updateHudsTimeUs = 0;
    bm.collectRenderTimeUs = 0;
    bm.framePollTimeUs = 0;
    bm.frameHeadTimeUs = 0;
    bm.frameTailTimeUs = 0;
    bm.frameTimerSamples = 0;
    bm.frameHudInputTimeUs = 0;
    bm.planChainTimeUs = 0;
    bm.planPanelTimeUs = 0;
    bm.planChainCalls = 0;

    // Start a new full-session FPS / duration window, and name its report file.
    resetSessionStats();
    m_sessionStart = std::chrono::steady_clock::now();
    time_t now = time(nullptr);
    struct tm timeInfo;
    // cppcheck-suppress uninitvar
    localtime_s(&timeInfo, &now);
    strftime(m_reportStamp, sizeof(m_reportStamp), "%Y%m%d_%H%M%S", &timeInfo);
    // The game's memory now, so the report can say whether it grew over the run
    // (a steady climb fits "stutters after ten minutes" better than any CPU cost).
    m_startMemMB = processMemoryMB();
}

// Leaving the track and Shutdown(): write the stint so far. The report used to
// be written only when the profiler was turned off, so quitting with it on threw
// the capture away -- and a player chasing a stutter in a race quits right after
// the race. The stint carries on: the same file is rewritten at each save (see
// exportReport), and the time spent in menus is a gap, not a frame
// (FrameTimeStats::GAP_US).
void BenchmarkWidget::saveStint() {
    if (!m_bCollecting || m_frameStats.count() == 0) return;
    const std::string& savePath = SettingsManager::getInstance().getSavePath();
    if (!savePath.empty()) exportReport(savePath.c_str());
}

void BenchmarkWidget::resetSessionStats() {
    m_haveLastFrameTime = false;
    m_frameStats.reset();
    m_lastPluginTotalUs = 0;   // the stint totals restart on the same edge
    m_havePendingFrame = false;
    m_peakQuadCount = 0;
    m_peakStringCount = 0;
}

void BenchmarkWidget::sampleFrameTime() {
    auto now = std::chrono::steady_clock::now();
    auto& bm = PluginData::getInstance().getBenchmarkMetrics();
    // The longest single callback since the last sample, then reset for the next.
    const long long maxCallUs = bm.sampleMaxCallUs;
    const int maxCallIdx = bm.sampleMaxCallIdx;
    bm.sampleMaxCallUs = 0;
    bm.sampleMaxCallIdx = -1;
    if (m_haveLastFrameTime) {
        double deltaUs = std::chrono::duration<double, std::micro>(now - m_lastFrameTime).count();
        // What the plugin spent between the two samples: every callback's stint
        // time, summed (a dozen adds, only while the profiler runs). Taken on EVERY
        // interval, so one the record drops (a gap, a clock anomaly) does not
        // bleed its plugin time into the next frame.
        long long pluginTotalUs = 0;
        for (int i = 0; i < bm.callbackCount; ++i) pluginTotalUs += bm.callbacks[i].stintTotalTimeUs;
        const long long pluginUs = pluginTotalUs - m_lastPluginTotalUs;
        m_lastPluginTotalUs = pluginTotalUs;

        const double atSec = std::chrono::duration<double>(now - m_sessionStart).count();
        const int riders = static_cast<int>(PluginData::getInstance().getRaceEntries().size());
        // RECORDED ONE SAMPLE LATE. The sample sits inside Draw, so each Draw is
        // split across two intervals, but its time is only booked when it ENDS --
        // in the delta of the interval after the one it started in. A slow start
        // to a Draw would land on the wrong frame. So a frame carries its own
        // delta plus the next one (and the longer of the two longest calls): an
        // upper bound on what the plugin spent in it.
        if (m_havePendingFrame) {
            FrameTimeStats::Spike f = m_pendingFrame;
            FrameTimeStats::bookNextInterval(f, deltaUs, pluginUs, maxCallUs, maxCallIdx);
            m_frameStats.add(f);
        }
        m_pendingFrame = { deltaUs, atSec, pluginUs, riders, maxCallUs, maxCallIdx };
        m_havePendingFrame = true;
    }
    m_lastFrameTime = now;
    m_haveLastFrameTime = true;
}

// Saved (with the folder) or not saved, once per report. An empty m_reportPath is
// a report that never reached the writer.
void BenchmarkWidget::pollReportResult() {
    if (std::chrono::steady_clock::now() < m_reportCheckAt) return;
    m_reportCheckPending = false;
    const bool ok = !m_reportPath.empty() && !AtomicFileWriter::needsRetry(m_reportPath);
    SystemMessages::Toast t;
    t.key = SystemMessages::KEY_PROFILER_REPORT;
    t.tab = SettingsHud::TAB_PERFORMANCE;
    if (ok) {
        snprintf(t.title, sizeof(t.title), "Profiler report saved");
        const std::string dir = m_reportPath.substr(0, m_reportPath.find_last_of("\\/"));
        SystemMessages::formatFolder(dir.c_str(), t.detail, sizeof(t.detail));
        snprintf(t.icon, sizeof(t.icon), "floppy-disk");
        t.durationMs = SystemMessages::WARNING_DURATION_MS;   // a folder to read, not a glance
    } else {
        snprintf(t.title, sizeof(t.title), "Profiler report not saved");
        snprintf(t.detail, sizeof(t.detail), "Could not write to mxbmrp3\\benchmarks");
        snprintf(t.icon, sizeof(t.icon), "triangle-exclamation");
        t.severity = SystemMessages::Severity::Warning;
        t.durationMs = SystemMessages::WARNING_DURATION_MS;
    }
    SystemMessages::getInstance().post(t);
}

void BenchmarkWidget::update() {
    // Before the early-out: the falling edge (widget hidden everywhere) has to be
    // seen to stop collection and export the report.
    syncCollectionState();
    if (m_reportCheckPending) pollReportResult();

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
    rebuildAndRecord();
    clearDataDirty();
    clearLayoutDirty();
}

void BenchmarkWidget::takeSnapshot() {
    auto& bm = PluginData::getInstance().getBenchmarkMetrics();

    // Snapshot callback timing (accumulated across entire interval)
    // CLAMPED, not just the copy loops below. Storing a count larger than the array
    // sends every reader off the end -- see MAX_HUD_SNAPSHOTS.
    m_snapshotCount = (bm.callbackCount < MAX_CALLBACKS) ? bm.callbackCount : MAX_CALLBACKS;
    for (int i = 0; i < m_snapshotCount && i < MAX_CALLBACKS; ++i) {
        strncpy_s(m_callbackSnapshots[i].name, sizeof(m_callbackSnapshots[i].name),
                  bm.callbacks[i].name, _TRUNCATE);
        m_callbackSnapshots[i].totalTimeUs = static_cast<float>(bm.callbacks[i].totalTimeUs);
        m_callbackSnapshots[i].peakTimeUs = static_cast<float>(bm.callbacks[i].peakTimeUs);
        m_callbackSnapshots[i].callCount = bm.callbacks[i].callCount;
    }

    // Snapshot HUD rebuild timing
    m_hudSnapshotCount = (bm.hudCount < MAX_HUD_SNAPSHOTS) ? bm.hudCount : MAX_HUD_SNAPSHOTS;
    for (int i = 0; i < m_hudSnapshotCount && i < MAX_HUD_SNAPSHOTS; ++i) {
        strncpy_s(m_hudSnapshots[i].name, sizeof(m_hudSnapshots[i].name),
                  bm.huds[i].name, _TRUNCATE);
        m_hudSnapshots[i].lastRebuildTimeUs = static_cast<float>(bm.huds[i].lastRebuildTimeUs);
        m_hudSnapshots[i].rebuildsInInterval = bm.huds[i].rebuildCount;
        m_hudSnapshots[i].stintTotalTimeUs = static_cast<float>(bm.huds[i].stintTotalTimeUs);
        m_hudSnapshots[i].stintRebuilds = bm.huds[i].stintRebuildCount;
        m_hudSnapshots[i].quadCount = bm.huds[i].quadCount;
        m_hudSnapshots[i].stringCount = bm.huds[i].stringCount;
        m_hudSnapshots[i].stintUpdateTimeUs = static_cast<float>(bm.huds[i].stintUpdateTimeUs);
        m_hudSnapshots[i].stintUpdateCount = bm.huds[i].stintUpdateCount;
    }

    // Snapshot aggregate metrics
    m_totalCallbackTimeUs = 0;
    for (int i = 0; i < m_snapshotCount; ++i) {
        m_totalCallbackTimeUs += m_callbackSnapshots[i].totalTimeUs;
    }
    // Draw's own per-frame average, so the footer can say what the callback is MADE
    // OF rather than just how big it is.
    m_drawAvgUs = 0.0f;
    for (int i = 0; i < bm.callbackCount; ++i) {
        if (std::strcmp(bm.callbacks[i].name, "Draw") == 0 && bm.callbacks[i].stintCallCount > 0) {
            m_drawAvgUs = static_cast<float>(
                static_cast<double>(bm.callbacks[i].stintTotalTimeUs) / bm.callbacks[i].stintCallCount);
            break;
        }
    }
    // PER FRAME, over the same window Draw's average is taken across -- see
    // BenchmarkMetrics::frameTimerSamples for why these are averaged, not one sample.
    {
        const double n = bm.frameTimerSamples > 0
                       ? static_cast<double>(bm.frameTimerSamples) : 1.0;
        m_collectRenderTimeUs = static_cast<float>(bm.collectRenderTimeUs / n);
        m_updateHudsTimeUs = static_cast<float>(bm.updateHudsTimeUs / n);
        m_framePollTimeUs = static_cast<float>(bm.framePollTimeUs / n);
        m_frameHeadTimeUs = static_cast<float>(bm.frameHeadTimeUs / n);
        m_frameTailTimeUs = static_cast<float>(bm.frameTailTimeUs / n);
        m_frameHudInputTimeUs = static_cast<float>(bm.frameHudInputTimeUs / n);
        m_planChainTimeUs = static_cast<float>(bm.planChainTimeUs / n);
        m_planPanelTimeUs = static_cast<float>(bm.planPanelTimeUs / n);
        m_planChainCalls = bm.planChainCalls;
    }
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

namespace {
// The whole update() call per frame -- rebuild AND the frames it decided not to.
// Both live-table figures derive from this: it is the number a player feels, and
// the only one a HUD that never rebuilds can express.
float hudPerFrameUs(const BenchmarkWidget::HudRebuildSnapshot& h) {
    return h.stintUpdateCount ? h.stintUpdateTimeUs / h.stintUpdateCount : 0.0f;
}
// Listed if it rebuilt at all this stint (so a once-a-lap HUD does not blink out
// between laps) OR if it costs time without rebuilding.
bool hudIsActive(const BenchmarkWidget::HudRebuildSnapshot& h) {
    return h.stintRebuilds > 0 || h.stintUpdateTimeUs > 0.0f;
}
}  // namespace

void BenchmarkWidget::rebuildRenderData() {
    clearStrings();
    m_quads.clear();

    auto dim = getScaledDimensions();

    // Count rows: slack + header + callbacks + separator + HUD header + HUDs with rebuilds + footer
    int rowCount = 1;  // Bottom slack row: the panel ends one blank row below the footer.
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
        // ...AND anything that costs time without rebuilding: listing only
        // rebuilders leaves a HUD burning microseconds per frame to decide NOT to
        // draw (a widget at zero rebuilds and ~2us/frame) absent rather than
        // mis-ranked, while the exported report names it.
        if (hudIsActive(m_hudSnapshots[i])) {
            activeHuds++;
        }
    }
    const int shownHuds = (activeHuds < MAX_LIVE_HUD_ROWS) ? activeHuds : MAX_LIVE_HUD_ROWS;
    rowCount += (shownHuds > 0) ? shownHuds : 1;          // At least "(none)" row
    if (activeHuds > shownHuds) rowCount += 1;            // "+N more" line
    rowCount += 1;     // Blank separator
    // Footer: Draw's attribution, its shares, quads/strings, and the plan chain.
    rowCount += 4;     // must match what the footer block below actually emits

    // BOX-MODEL: the plan owns padding, the Large title band and the panel's
    // rounding; the fixed monospace table width is the content ask.
    PanelWant want;
    want.contentW = PluginUtils::calculateMonospaceTextWidth(CONTENT_WIDTH_CHARS, dim.fontSize);
    want.sectionH = { rowCount * dim.lineHeightNormal };
    want.captionW = planTitleWidth(dim, "Benchmark", TitleTier::Large);
    want.tier = TitleTier::Large;
    PanelPlan& p = planPanel(dim, want);
    const float backgroundWidth = p.width();
    const float backgroundHeight = p.height();

    setBounds(START_X, START_Y, START_X + backgroundWidth, START_Y + backgroundHeight);
    addPlanBackground(p, START_X, START_Y);
    addPlanTitle(p, "Benchmark",
        this->getColor(ColorSlot::PRIMARY));

    float contentStartX = p.contentX();
    float currentY = p.contentY();

    // Column right-edge X positions for the value columns (right-aligned). Time columns
    // are 7 chars, count columns 5 chars, with 2-char gaps. Headers right-align to the
    // same X as their values, so they line up regardless of header font size.
    // Right edge = the content inset mirrored on the panel's right side (the box is
    // symmetric unless a theme sets per-side terms).
    float rightEdge = START_X + backgroundWidth - (contentStartX - START_X);
    float charW = PluginUtils::calculateMonospaceTextWidth(1, dim.fontSize);
    float colCalls = rightEdge;
    float colPeak  = rightEdge - 7.0f * charW;
    float colTotal = rightEdge - 16.0f * charW;
    float colCount = rightEdge;
    float colLast  = rightEdge - 7.0f * charW;
    float colHudTotal = rightEdge - 16.0f * charW;   // same three-column grid as CALLBACKS
    int valueFont = this->getFont(FontCategory::DIGITS);
    unsigned long labelColor = this->getColor(ColorSlot::TERTIARY);

    // === CALLBACK SECTION ===
    addSectionHeading("Callbacks", contentStartX, currentY, dim);
    addLabel("Total us", colTotal, currentY, Justify::RIGHT, labelColor, dim);
    addLabel("Peak us", colPeak, currentY, Justify::RIGHT, labelColor, dim);
    addLabel("Calls", colCalls, currentY, Justify::RIGHT, labelColor, dim);
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
    addSectionHeading("HUD rebuilds", contentStartX, currentY, dim);
    // us/f, not the stint total: the per-frame cost is what a player feels and what
    // the exported report ranks by, and it is the only one of the three that a
    // non-rebuilding HUD can express at all.
    addLabel("us/f", colHudTotal, currentY, Justify::RIGHT, labelColor, dim);
    addLabel("Avg us", colLast, currentY, Justify::RIGHT, labelColor, dim);
    addLabel("Builds", colCount, currentY, Justify::RIGHT, labelColor, dim);
    currentY += dim.lineHeightNormal;

    if (activeHuds == 0) {
        addString("(none)", contentStartX, currentY, Justify::LEFT,
            this->getFont(FontCategory::NORMAL), this->getColor(ColorSlot::TERTIARY), dim.fontSize);
        currentY += dim.lineHeightNormal;
    } else {
        int hudOrder[MAX_HUD_SNAPSHOTS]; int nHudOrder = 0;
        for (int i = 0; i < m_hudSnapshotCount && nHudOrder < MAX_HUD_SNAPSHOTS; ++i)
            if (hudIsActive(m_hudSnapshots[i])) hudOrder[nHudOrder++] = i;
        // Heaviest PER FRAME first: the whole point of the stint view is that it
        // reorders the list by what is actually costing CPU, which the per-interval
        // view cannot show. Sorting by stint total would rank by how long the widget
        // has been open rather than by what each HUD costs a frame, and cannot rank
        // a non-rebuilder at all -- its total is zero however much time it burns.
        std::sort(hudOrder, hudOrder + nHudOrder, [&](int a2, int b2) {
            return hudPerFrameUs(m_hudSnapshots[a2]) > hudPerFrameUs(m_hudSnapshots[b2]);
        });
        const int nShow = (nHudOrder < MAX_LIVE_HUD_ROWS) ? nHudOrder : MAX_LIVE_HUD_ROWS;
        for (int k = 0; k < nShow; ++k) {
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
            snprintf(buf, sizeof(buf), "%.1f", hudPerFrameUs(h));
            addString(buf, colHudTotal, currentY, Justify::RIGHT, valueFont, color, dim.fontSize);
            snprintf(buf, sizeof(buf), "%.0f", avgUs);
            addString(buf, colLast, currentY, Justify::RIGHT, valueFont, color, dim.fontSize);
            snprintf(buf, sizeof(buf), "%d", h.stintRebuilds);
            addString(buf, colCount, currentY, Justify::RIGHT, valueFont, color, dim.fontSize);
            currentY += dim.lineHeightNormal;
        }
        // COUNTED, never silently dropped: a truncated list that does not say so
        // reads as "these are all of them", which is the same failure the registry
        // cap had -- see MAX_HUDS.
        if (nHudOrder > nShow) {
            char moreBuf[48];
            snprintf(moreBuf, sizeof(moreBuf), "+%d more (full list in the report)",
                     nHudOrder - nShow);
            addString(moreBuf, contentStartX, currentY, Justify::LEFT,
                this->getFont(FontCategory::NORMAL), this->getColor(ColorSlot::TERTIARY),
                dim.fontSize);
            currentY += dim.lineHeightNormal;
        }
    }

    // Separator
    currentY += dim.lineHeightNormal;

    // === FOOTER - Aggregate stats ===
    char footer[64];

    // DRAW, ATTRIBUTED -- the same split the exported report leads with. Collect
    // render is the SMALLEST of Draw's three parts (about a tenth of it), updateHuds
    // about half and the frame poll a third; a profiler whose live view names its
    // smallest component and hides its largest sends you looking in the wrong place.
    snprintf(footer, sizeof(footer), "Draw %.0f = upd %.0f + col %.0f + poll %.0f",
             m_drawAvgUs, m_updateHudsTimeUs, m_collectRenderTimeUs, m_framePollTimeUs);
    addString(footer, contentStartX, currentY, Justify::LEFT,
        this->getFont(FontCategory::NORMAL), this->getColor(ColorSlot::PRIMARY), dim.fontSize);
    currentY += dim.lineHeightNormal;

    // ...and as SHARES, which is what survives a comparison. Absolute microseconds
    // move with thermals and background load; a part over its own whole does not.
    if (m_drawAvgUs > 0.0f) {
        const float pu = 100.0f * m_updateHudsTimeUs   / m_drawAvgUs;
        const float pc = 100.0f * m_collectRenderTimeUs / m_drawAvgUs;
        const float pp = 100.0f * m_framePollTimeUs    / m_drawAvgUs;
        snprintf(footer, sizeof(footer), "  of Draw: %.0f%% / %.0f%% / %.0f%%", pu, pc, pp);
        addString(footer, contentStartX, currentY, Justify::LEFT,
            this->getFont(FontCategory::NORMAL), this->getColor(ColorSlot::TERTIARY), dim.fontSize);
    }
    currentY += dim.lineHeightNormal;

    snprintf(footer, sizeof(footer), "Quads: %d", m_totalQuadCount);
    addString(footer, contentStartX, currentY, Justify::LEFT,
        this->getFont(FontCategory::NORMAL), this->getColor(ColorSlot::SECONDARY), dim.fontSize);

    snprintf(footer, sizeof(footer), "Strings: %d", m_totalStringCount);
    addString(footer, rightEdge, currentY, Justify::RIGHT,
        this->getFont(FontCategory::NORMAL), this->getColor(ColorSlot::SECONDARY), dim.fontSize);
    currentY += dim.lineHeightNormal;

    // The plan chain, when it is costing anything worth naming. It is the one
    // shared cost that every ported panel pays at once, so a regression there shows
    // up as every row rising together rather than as one row standing out.
    if (m_planChainTimeUs >= 1.0f) {
        snprintf(footer, sizeof(footer), "Plan chain: %.0f us/f", m_planChainTimeUs);
        addString(footer, contentStartX, currentY, Justify::LEFT,
            this->getFont(FontCategory::NORMAL), this->getColor(ColorSlot::SECONDARY), dim.fontSize);
    }
}

void BenchmarkWidget::resetToDefaults() {
    m_bVisible = false;    // Hidden by default
    m_bShowTitle = true;
    setTextureVariant(0);
    m_fBackgroundOpacity = 1.0f;
    setScale(1.0f);
    setPosition(0.01f, cellsY(26));  // Left side of screen

    m_frameCounter = 0;
    m_snapshotCount = 0;
    m_hudSnapshotCount = 0;
    m_totalCallbackTimeUs = 0.0f;
    m_collectRenderTimeUs = 0.0f;
    m_updateHudsTimeUs = 0.0f;
    m_framePollTimeUs = 0.0f;
    m_frameHeadTimeUs = 0.0f;
    m_frameTailTimeUs = 0.0f;
    m_frameHudInputTimeUs = 0.0f;
    m_planChainTimeUs = 0.0f;
    m_planPanelTimeUs = 0.0f;
    m_planChainCalls = 0;
    m_totalQuadCount = 0;
    m_totalStringCount = 0;

    m_callbackSnapshots.fill({});
    m_hudSnapshots.fill({});

    resetSessionStats();
    m_sessionStart = std::chrono::steady_clock::time_point{};

    setDataDirty();
}
