// ============================================================================
// hud/benchmark_widget.cpp
// Developer-only widget showing per-callback and per-HUD timing breakdown
// ============================================================================
#include "benchmark_widget.h"
#include "../diagnostics/call_counters.h"

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
#include "../core/ui_config.h"
#include "../core/hud_manager.h"
#include "../game/game_config.h"  // GAME_NAME
#include "../handlers/draw_handler.h"

using namespace PluginConstants;

BenchmarkWidget::BenchmarkWidget() {
    m_panelKind = PanelKind::Widget;
    // Body card: a title over a table of rows is exactly the shape the card exists
    // for, and this was the last titled panel in the tree without one -- so under a
    // theme it was the one table sitting straight on the frame's fill while every
    // other one sat in a well. The box-model plan (addPlanBackground) emits the card.
    m_bContentCard = true;
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
    rebuildAndRecord();
    clearDataDirty();
    clearLayoutDirty();
}

void BenchmarkWidget::takeSnapshot() {
    auto& bm = PluginData::getInstance().getBenchmarkMetrics();

    // Snapshot callback timing (accumulated across entire interval)
    // CLAMPED, not just the copy loops below. Storing a count larger than the array
    // is what sent every reader off the end -- see MAX_HUD_SNAPSHOTS.
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
    // BenchmarkMetrics::frameTimerSamples for why these are no longer one sample.
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
    int rowCount = 1;  // Bottom slack row, kept for geometry parity: the pre-plan sizing
                       // counted a "Title" row here ON TOP of reserving the title height
                       // separately (the plan's band owns the title now), so the panel has
                       // always ended one blank row below the footer.
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
        // ...AND anything that costs time without rebuilding. This listed only
        // rebuilders, so a HUD burning microseconds per frame to decide NOT to draw
        // was absent rather than mis-ranked: position_widget (2.22us/frame) and
        // clock_widget (1.19), both at zero rebuilds, were invisible here while the
        // exported report named them.
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
    BaseHud::PanelWant want;
    want.contentW = PluginUtils::calculateMonospaceTextWidth(CONTENT_WIDTH_CHARS, dim.fontSize);
    want.sectionH = { rowCount * dim.lineHeightNormal };
    want.captionW = planTitleWidth(dim, "Benchmark", TitleTier::Large);
    want.tier = TitleTier::Large;
    PanelPlan& p = planPanel(dim, want);
    const float backgroundWidth = p.width();
    const float backgroundHeight = p.height();

    setBounds(START_X, START_Y, START_X + backgroundWidth, START_Y + backgroundHeight);
    addPlanBackground(p, START_X, START_Y);
    addPlanTitle(p, "Benchmark", this->getFont(FontCategory::TITLE),
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
    // us/f, not the stint total: the per-frame cost is what a player feels and what
    // the exported report ranks by, and it is the only one of the three that a
    // non-rebuilding HUD can express at all.
    addLabel("us/f", colHudTotal, currentY, Justify::RIGHT, labelFont, labelColor, dim);
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
            if (hudIsActive(m_hudSnapshots[i])) hudOrder[nHudOrder++] = i;
        // Heaviest total first: the whole point of the stint view is that it reorders the
        // list by what is actually costing CPU, which the per-interval view cannot show.
        // Heaviest PER FRAME first. Sorting by stint total ranked by how long the
        // widget had been open rather than by what each HUD costs a frame, and it
        // cannot rank a non-rebuilder at all -- its total is zero however much time
        // it burns.
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

    // DRAW, ATTRIBUTED -- the same split the exported report leads with. This line
    // used to read "Collect render: N us" alone, which is the SMALLEST of Draw's
    // three parts (about a tenth of it) while updateHuds (half) and the frame poll
    // (a third) had no line at all. A profiler whose live view names its smallest
    // component and hides its largest sends you looking in the wrong place.
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
    // up as every row rising together -- which is exactly how it was missed before.
    if (m_planChainTimeUs >= 1.0f) {
        snprintf(footer, sizeof(footer), "Plan chain: %.0f us/f", m_planChainTimeUs);
        addString(footer, contentStartX, currentY, Justify::LEFT,
            this->getFont(FontCategory::NORMAL), this->getColor(ColorSlot::SECONDARY), dim.fontSize);
    }
}

bool BenchmarkWidget::exportReport(const char* savePath) const {
    if (!savePath || savePath[0] == '\0') return false;

    // savePath/mxbmrp3/benchmarks/ -- reports get their own subfolder so they don't
    // clutter the plugin config root (which holds settings/stats/log). Built by
    // SettingsManager, which owns the save path; the render-probe sweep writes here too
    // and got the folder wrong the one time it spelled the path itself.
    const std::string dir = SettingsManager::getInstance().getBenchmarksDir();
    if (dir.empty()) return false;

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
    // The shared target -- see PluginConstants::FRAME_BUDGET_US. These were a local
    // 2083.0 and a local 480 that happened to agree with the graph, run_perf.sh and
    // the docs; now they cannot disagree. NOT re-declared under the same names: this
    // file has `using namespace PluginConstants`, so a local TARGET_FPS shadows the
    // global one and MSVC's C4459 is an error here (/WX).

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
    snprintf(line, sizeof(line), "Target: %d fps (%.0f us/frame budget)\n", PluginConstants::TARGET_FPS, PluginConstants::FRAME_BUDGET_US); out += line;

    // --- RENDER PROBE, only when it is on. A probe run's numbers are meaningless
    // without its configuration, and the configuration lives in the INI, not in the
    // report -- so a sweep produced a stack of files that could not be told apart
    // afterwards. Since the whole point of the probe is to compare runs, the run has
    // to carry its own settings. Off by default, so this block is absent from every
    // ordinary report.
    {
        UiConfig& ui = UiConfig::getInstance();
        const int probeN = ui.getRenderProbeQuads();
        if (probeN > 0) {
            const int type = ui.getRenderProbeType();
            const char* typeName = (type == 2) ? "text string (glyph atlas)"
                                 : (type == 1) ? "sprite quad (textured)"
                                               : "solid-fill quad (untextured)";
            out += "\n=== RENDER PROBE (synthetic engine load -- NOT an ordinary run) ===\n";
            snprintf(line, sizeof(line), "Extra primitives/frame: %d\n", probeN); out += line;
            snprintf(line, sizeof(line), "Type: %d = %s\n", type, typeName); out += line;
            if (type != 2) {
                snprintf(line, sizeof(line), "Geometry: %s\n",
                         ui.getRenderProbeFullscreen() ? "FULL-SCREEN (fill-rate bound)"
                                                       : "tiny 0.01 square (submit bound)");
                out += line;
            }
            if (type == 1) {
                HudManager& hm = HudManager::getInstance();
                const int pin = ui.getRenderProbeSprite();
                const int have = hm.registeredSpriteCount();
                if (pin > 0 && pin <= have) {
                    snprintf(line, sizeof(line), "Sprite: PINNED %d (%s) -- no texture switching\n",
                             pin, hm.spriteName(pin));
                } else if (pin > 0) {
                    snprintf(line, sizeof(line),
                             "Sprite: %d OUT OF RANGE (%d registered) -- fell back to cycling\n",
                             pin, have);
                } else {
                    snprintf(line, sizeof(line),
                             "Sprite: cycling all %d registered -- a texture switch per quad\n", have);
                }
                out += line;
            }
        }
    }

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
    // Draw's own average, kept for the SHARE columns below. A share is what makes
    // two reports comparable: absolute microseconds move with whatever the machine
    // was doing -- thermals, boost state, background load -- while a part and its
    // whole move together, so the ratio holds. Measured across two of this
    // profiler's own reports, updateHuds moved 33% in microseconds and 1.4% as a
    // share of Draw.
    double drawAvgUs = 0.0;
    {
        const auto& bm = PluginData::getInstance().getBenchmarkMetrics();
        for (int i = 0; i < bm.callbackCount; ++i) {
            const auto& c = bm.callbacks[i];
            if (c.stintCallCount <= 0) continue;   // never fired this stint
            const double avgUs = static_cast<double>(c.stintTotalTimeUs) / c.stintCallCount;
            if (std::strcmp(c.name, "Draw") == 0) drawAvgUs = avgUs;
            const double pctBud = 100.0 * static_cast<double>(c.stintPeakTimeUs) / PluginConstants::FRAME_BUDGET_US;
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
        // IDLE us/frame is the column that matters for a Draw regression: a HUD's
        // update() minus its rebuilds, over every frame -- what it costs on the
        // frames it is NOT dirty. Summed, that was 58.6us/frame against 36.4us of
        // rebuilds, and no per-HUD figure existed to say where it went.
        out += "=== HUD REBUILDS (STINT TOTALS) ===\n";
        snprintf(line, sizeof(line), "%-24s %12s %9s %10s %10s %10s %7s\n",
                 "Name", "Total us", "Avg us", "Peak us", "Rebuilds", "Idle us/f", "% upd"); out += line;
        snprintf(line, sizeof(line), "%-24s %12s %9s %10s %10s %10s %7s\n",
                 "------------------------", "------------", "---------", "----------", "----------", "----------", "-------"); out += line;
        int ord[MAX_HUD_SNAPSHOTS]; int nOrd = 0;
        // Every HUD with EITHER a rebuild or a timed update: a HUD that never
        // rebuilds and still costs time per frame is exactly what this is hunting,
        // and the old filter hid it.
        for (int i = 0; i < bm.hudCount && nOrd < MAX_HUD_SNAPSHOTS; ++i)
            if (bm.huds[i].stintRebuildCount > 0 || bm.huds[i].stintUpdateTimeUs > 0) ord[nOrd++] = i;
        // Heaviest total first — that is the "what should I look at" ordering, and it is
        // the one the per-interval table cannot give you.
        std::sort(ord, ord + nOrd, [&](int a2, int b2) {
            return bm.huds[a2].stintTotalTimeUs > bm.huds[b2].stintTotalTimeUs;
        });
        for (int k = 0; k < nOrd; ++k) {
            const auto& h = bm.huds[ord[k]];
            const double avg = h.stintRebuildCount
                             ? static_cast<double>(h.stintTotalTimeUs) / h.stintRebuildCount : 0.0;
            const double idle = h.stintUpdateCount
                              ? static_cast<double>(h.stintUpdateTimeUs - h.stintTotalTimeUs)
                                / h.stintUpdateCount : 0.0;
            // THE WHOLE update() call per frame -- rebuild and idle together -- as a
            // share of updateHuds. This is the column to compare between reports:
            // "telemetry_hud is 11% of the HUD pass" survives a run on a hot machine,
            // "17.3us" does not.
            const double perFrame = h.stintUpdateCount
                                  ? static_cast<double>(h.stintUpdateTimeUs) / h.stintUpdateCount : 0.0;
            const double pctUpd = (m_updateHudsTimeUs > 0.0f)
                                ? 100.0 * perFrame / m_updateHudsTimeUs : 0.0;
            snprintf(line, sizeof(line), "%-24s %12lld %9.1f %10lld %10d %10.2f %6.1f%%\n",
                     h.name, h.stintTotalTimeUs, avg, h.stintPeakTimeUs, h.stintRebuildCount,
                     idle, pctUpd); out += line;
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
    double collectPct = 100.0 * m_collectRenderTimeUs / PluginConstants::FRAME_BUDGET_US;
    double updatePct  = 100.0 * m_updateHudsTimeUs   / PluginConstants::FRAME_BUDGET_US;
    double pollPct    = 100.0 * m_framePollTimeUs    / PluginConstants::FRAME_BUDGET_US;
    // SHARE OF DRAW, alongside the budget percentages above. The two answer
    // different questions and only one of them survives a machine in a different
    // state: % of budget is a rescaled microsecond and moves with thermals, boost
    // and background load; % of Draw is a part over its own whole, so it holds.
    // Comparing two reports from this profiler, updateHuds moved 33% in absolute
    // terms between them and 1.4% as a share.
    const auto pctDraw = [&](float v) { return (drawAvgUs > 0.0) ? 100.0 * v / drawAvgUs : 0.0; };
    out += "=== AGGREGATE ===\n";
    snprintf(line, sizeof(line), "Total callback time: %.0f us (%.2f ms over the %d-frame interval)\n",
             m_totalCallbackTimeUs, m_totalCallbackTimeUs / 1000.0f, SNAPSHOT_INTERVAL_FRAMES); out += line;
    // DRAW, ATTRIBUTED. These three are per-frame and together account for the Draw
    // callback: the HUDs' own update() (rebuilds included), the collect that bundles
    // their primitives for the engine, and the input/hotkey/director polls around
    // them. Draw was one lump with only the collect broken out, and the collect is
    // the small half -- so a Draw regression had to be bisected instead of read.
    snprintf(line, sizeof(line), "Update HUDs time:    %.0f us/frame (%.1f%% of the 480fps budget, %.1f%% of Draw)\n",
             m_updateHudsTimeUs, updatePct, pctDraw(m_updateHudsTimeUs)); out += line;
    snprintf(line, sizeof(line), "  of which HUD input: %.0f us/frame (handleMouseInput + the drag-target search)\n",
             m_frameHudInputTimeUs); out += line;
    snprintf(line, sizeof(line), "  of which plan chain: %.0f us/frame over %lld calls (planPanel + addPlanBackground + addPlanTitle)\n",
             m_planChainTimeUs, m_planChainCalls); out += line;
    snprintf(line, sizeof(line), "     planPanel %.0f / emit %.0f us/frame (compute vs quads+strings)\n",
             m_planPanelTimeUs, m_planChainTimeUs - m_planPanelTimeUs); out += line;
    snprintf(line, sizeof(line), "Collect render time: %.0f us/frame (%.1f%% of the 480fps budget, %.1f%% of Draw)\n",
             m_collectRenderTimeUs, collectPct, pctDraw(m_collectRenderTimeUs)); out += line;
    snprintf(line, sizeof(line), "Frame poll time:     %.0f us/frame (%.1f%% of the 480fps budget, %.1f%% of Draw)\n",
             m_framePollTimeUs, pollPct, pctDraw(m_framePollTimeUs)); out += line;
    snprintf(line, sizeof(line), "Frame head/tail:     %.0f / %.0f us/frame (draw-state+reveal / probe+display-target+companion)\n",
             m_frameHeadTimeUs, m_frameTailTimeUs); out += line;

    out += "\n";

#if defined(MXBMRP3_TEST_BUILD)
    // The call census, normalised by the rebuilds it was gathered over -- see
    // call_counters.h for why a COUNT is the figure this harness can trust.
    {
        const auto& bmc = PluginData::getInstance().getBenchmarkMetrics();
        long long rebuilds = 0;
        for (int i = 0; i < bmc.hudCount; ++i) rebuilds += bmc.huds[i].stintRebuildCount;
        out += CallCounters::report(rebuilds);
        out += "\n";
        CallCounters::reset();
    }
#endif

    // Machine-readable one-liner for tools/benchmark_report.py (stable key=value;
    // parse this rather than the human tables).
    //
    // EMITTED IN SEGMENTS, and that is the fix for a real defect rather than a style
    // choice. This was ONE snprintf into a char[512], and the line had outgrown it:
    // every exported report came out truncated at exactly 512 bytes, silently losing
    // cb_total_us and -- the moment they were added -- all four probe_* keys. So five
    // probe sweeps in a row could not be told apart by the file that was supposed to
    // describe them, and nothing failed: snprintf truncates and returns, the report
    // still parses, and every key that survived is correct. A bigger buffer would only
    // move the cliff; segments remove it, because `out` is a std::string and each
    // piece below is bounded by its own format.
    //
    // The last segment is therefore load-bearing as a completeness check: if a report
    // ends mid-line, the line was cut, and benchmark_report.py requires the terminal
    // keys for exactly that reason.
    {
        char seg[256];
        snprintf(seg, sizeof(seg),
            "BENCH game=\"%s\" track=\"%s\" riders=%d huds=%d threaded=%d "
            "target_fps=%d budget_us=%.0f ",
            GAME_NAME, trackName, riders, m_hudSnapshotCount, threaded ? 1 : 0,
            PluginConstants::TARGET_FPS, PluginConstants::FRAME_BUDGET_US);
        out += seg;
        snprintf(seg, sizeof(seg),
            "dur_s=%.2f frames=%lld frames_sampled=%d fps_min=%.1f fps_avg=%.1f fps_max=%.1f "
            "ft_p50_us=%.0f ft_p99_us=%.0f ft_max_us=%.0f lowfps_1pct=%.1f ",
            durationSec, m_frameSampleCount, nSamples, fpsMin, fpsAvg, fpsMax,
            ftP50, ftP99, ftMax, low1Fps);
        out += seg;
        snprintf(seg, sizeof(seg),
            "quads=%d quads_peak=%d strings=%d strings_peak=%d collect_us=%.0f "
            "update_huds_us=%.0f frame_poll_us=%.0f frame_head_us=%.0f frame_tail_us=%.0f ",
            m_totalQuadCount, m_peakQuadCount, m_totalStringCount, m_peakStringCount,
            static_cast<double>(m_collectRenderTimeUs),
            static_cast<double>(m_updateHudsTimeUs), static_cast<double>(m_framePollTimeUs),
            static_cast<double>(m_frameHeadTimeUs), static_cast<double>(m_frameTailTimeUs));
        out += seg;
        snprintf(seg, sizeof(seg),
            "hud_input_us=%.0f plan_chain_us=%.0f plan_panel_us=%.0f "
            "draw_avg_us=%.0f upd_pct_draw=%.1f collect_pct_draw=%.1f poll_pct_draw=%.1f "
            "cb_total_us=%.0f ",
            static_cast<double>(m_frameHudInputTimeUs), static_cast<double>(m_planChainTimeUs),
            static_cast<double>(m_planPanelTimeUs),
            drawAvgUs, pctDraw(m_updateHudsTimeUs), pctDraw(m_collectRenderTimeUs),
            pctDraw(m_framePollTimeUs),
            static_cast<double>(m_totalCallbackTimeUs));
        out += seg;
        // The probe's configuration, ALWAYS emitted (0 0 0 0 on an ordinary run).
        // These are what make a sweep comparable: every other key here describes the
        // RESULT, and without these four the file cannot say which experiment produced
        // it. Unconditional so the key set never varies between reports.
        snprintf(seg, sizeof(seg), "probe_n=%d probe_type=%d probe_fs=%d probe_sprite=%d\n",
            UiConfig::getInstance().getRenderProbeQuads(),
            UiConfig::getInstance().getRenderProbeType(),
            UiConfig::getInstance().getRenderProbeFullscreen() ? 1 : 0,
            UiConfig::getInstance().getRenderProbeSprite());
        out += seg;
    }

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
