// ============================================================================
// hud/benchmark_widget_report.cpp
// The Benchmark profiler's exported report: the plain-words SUMMARY at the top,
// then the full tables and the BENCH line tools/benchmark_report.py parses.
// Split from benchmark_widget.cpp (the widget, its sampling and its live panel)
// to keep both inside the source-file budget.
// ============================================================================
#include "benchmark_widget.h"
#include "../diagnostics/call_counters.h"

#include <cstdio>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
#include <ctime>
#include <windows.h>
#include <psapi.h>   // K32GetProcessMemoryInfo

#include "../diagnostics/logger.h"
#include "../core/atomic_file_writer.h"
#include "../core/plugin_data.h"
#include "../core/plugin_thread.h"
#include "../core/plugin_utils.h"
#include "../core/settings_manager.h"
#include "../core/ui_config.h"
#include "../core/hud_manager.h"
#include "../game/game_config.h"  // GAME_NAME

using namespace PluginConstants;

// The two lines a person reads first: how much was captured and how it ran, and
// what the plugin's own code cost. Plain numbers, deliberately no verdict:
//  * every timer here is WALL time, so a Windows pause inside a plugin callback
//    (a busy or few-core PC) reads as plugin time -- a sentence blaming or
//    clearing the plugin would be wrong exactly when it matters. The worst-frames
//    table's "Longest call" column is what tells a stuck callback from a busy PC;
//  * only GAME-THREAD plugin code is timed: the companion window draws on its
//    own thread, and the game's cost of drawing what we hand it is invisible
//    here (the primitive counts are in RENDER HANDOFF);
//  * the worst-frame figure is an upper bound (see sampleFrameTime).
void BenchmarkWidget::appendSummary(std::string& out, const FrameTimeStats& fs) const {
    char line[256];
    out += "\n=== SUMMARY ===\n";
    const long long frames = fs.count();
    if (frames <= 0) {
        out += "No frames captured.\n";
        return;
    }
    const double ridingSec = fs.meanUs() * static_cast<double>(frames) / 1.0e6;
    const int ridingS = static_cast<int>(ridingSec + 0.5);
    const double p99 = fs.percentileUs(0.99);
    snprintf(line, sizeof(line), "Captured: %d:%02d on track, %lld frames, avg %.0f fps, 1%% low %.0f fps\n",
             ridingS / 60, ridingS % 60, frames, 1.0e6 / fs.meanUs(), (p99 > 0.0) ? 1.0e6 / p99 : 0.0);
    out += line;

    const auto& bm = PluginData::getInstance().getBenchmarkMetrics();
    long long pluginTotalUs = 0;
    for (int i = 0; i < bm.callbackCount; ++i) pluginTotalUs += bm.callbacks[i].stintTotalTimeUs;
    const double pluginPerFrameUs = static_cast<double>(pluginTotalUs) / static_cast<double>(frames);
    snprintf(line, sizeof(line), "Plugin:   %.2f ms per frame on average, %.1f ms at most in any frame (wall time)\n",
             pluginPerFrameUs / 1000.0, static_cast<double>(fs.maxPluginUs()) / 1000.0);
    out += line;
}

// The machine the report came from, read once at save time: what a reader needs
// to weigh a hitch -- an old 4-core laptop pauses the game where a desktop does
// not, and a game whose memory climbed over the run (or a PC short of free RAM)
// fits "stutters after ten minutes" better than any CPU cost. Heat throttling is
// the other usual suspect and is deliberately absent: Windows exposes no reliable
// temperature or clock without drivers, and a guessed figure would mislead.
// The game's commit charge: PagefileUsage, which Windows documents as the same
// figure as PROCESS_MEMORY_COUNTERS_EX::PrivateUsage -- and which, unlike that
// one, Wine fills in, so the integration suite sees the line too.
double BenchmarkWidget::processMemoryMB() {
    PROCESS_MEMORY_COUNTERS pmc{};
    pmc.cb = sizeof(pmc);
    if (!K32GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) return 0.0;
    return static_cast<double>(pmc.PagefileUsage) / (1024.0 * 1024.0);
}

static void appendMachine(std::string& out, double startMemMB, double nowMemMB) {
    char line[256];
    char cpu[128] = "unknown";
    DWORD cpuSize = sizeof(cpu);
    if (RegGetValueA(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                     "ProcessorNameString", RRF_RT_REG_SZ, nullptr, cpu, &cpuSize) != ERROR_SUCCESS) {
        snprintf(cpu, sizeof(cpu), "unknown");
    }
    // Trim the padding some vendors put around the name (AMD pads the end).
    const char* cpuName = PluginUtils::trimInPlace(cpu);
    snprintf(line, sizeof(line), "CPU: %s, %lu logical cores\n", cpuName,
             static_cast<unsigned long>(GetActiveProcessorCount(ALL_PROCESSOR_GROUPS)));
    out += line;

    MEMORYSTATUSEX mem{};
    mem.dwLength = sizeof(mem);
    if (GlobalMemoryStatusEx(&mem)) {
        snprintf(line, sizeof(line), "RAM: %.1f GB, %.1f GB free\n",
                 static_cast<double>(mem.ullTotalPhys) / (1024.0 * 1024.0 * 1024.0),
                 static_cast<double>(mem.ullAvailPhys) / (1024.0 * 1024.0 * 1024.0));
        out += line;
    }
    if (nowMemMB > 0.0) {
        if (startMemMB > 0.0) {
            snprintf(line, sizeof(line), "Game memory: %.0f MB at the start, %.0f MB now\n", startMemMB, nowMemMB);
        } else {
            snprintf(line, sizeof(line), "Game memory: %.0f MB\n", nowMemMB);
        }
        out += line;
    }
}

bool BenchmarkWidget::exportReport(const char* savePath, std::string* writtenPath) const {
    if (!savePath || savePath[0] == '\0') return false;

    // savePath/mxbmrp3/benchmarks/ -- reports get their own subfolder so they don't
    // clutter the plugin config root (which holds settings/stats/log). Built by
    // SettingsManager, which owns the save path; the render-probe sweep writes here too,
    // through the same accessor rather than spelling the path itself.
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
    // ONE FILE PER STINT, named when the stint began: the saves on leaving the
    // track and at quit rewrite it with the stint so far, and turning the profiler
    // off writes it a last time -- never a pile of partial copies.
    const char* stamp = (m_reportStamp[0] != '\0') ? m_reportStamp : fileTimestamp;
    std::string filePath = dir + "\\benchmark_" + stamp + ".txt";

    // Build the whole report into a string, then hand it to the shared atomic writer (temp
    // file + MoveFileExA replace) — the same path every persisted file uses. snprintf per line
    // keeps the exact column formatting the analyzer parses.
    std::string out;
    char line[256];

    out += "MXBMRP3 Benchmark Report\n";
    snprintf(line, sizeof(line), "Version: %s\n", PluginConstants::PLUGIN_VERSION); out += line;
    snprintf(line, sizeof(line), "Date: %s\n", timestamp); out += line;
    snprintf(line, sizeof(line), "Snapshot interval: %d frames\n", SNAPSHOT_INTERVAL_FRAMES); out += line;

    // A copy, so the frame still waiting for its next sample can be counted (with
    // the plugin time known so far) without the record itself changing.
    FrameTimeStats fs = m_frameStats;
    if (m_havePendingFrame) {
        fs.add(m_pendingFrame);
    }
    appendSummary(out, fs);

    // 480fps frame budget. Cost is expressed as a share of this so the report is
    // self-describing — an analyzer doesn't have to know the target out of band.
    // The shared target -- see PluginConstants::FRAME_BUDGET_US -- so this report, the
    // graph, run_perf.sh and the docs cannot disagree. NOT re-declared under the same
    // names: this file has `using namespace PluginConstants`, so a local TARGET_FPS
    // shadows the global one and MSVC's C4459 is an error here (/WX).

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
    appendMachine(out, m_startMemMB, processMemoryMB());

    // --- RENDER PROBE, only when it is on. A probe run's numbers are meaningless
    // without its configuration, and the configuration lives in the INI, not in the
    // report -- so without it a sweep produces a stack of files that cannot be told
    // apart afterwards. Since the whole point of the probe is to compare runs, the run has
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
    // Every frame-rate figure below comes from the ONE whole-stint record, so the
    // FPS min/max and the frame-time max can't disagree (they're the same frames).
    const long long nSamples = fs.count();
    double ftP50 = 0.0, ftP99 = 0.0, ftMax = 0.0, low1Fps = 0.0;
    double fpsMin = 0.0, fpsAvg = 0.0, fpsMax = 0.0;
    if (nSamples > 0) {
        ftP50 = fs.percentileUs(0.50); ftP99 = fs.percentileUs(0.99); ftMax = fs.maxUs();
        const double ftMin = fs.minUs(), ftMean = fs.meanUs();
        fpsMax = (ftMin  > 0.0) ? 1.0e6 / ftMin  : 0.0;   // shortest frame = highest FPS
        fpsMin = (ftMax  > 0.0) ? 1.0e6 / ftMax  : 0.0;   // longest frame  = lowest FPS
        fpsAvg = (ftMean > 0.0) ? 1.0e6 / ftMean : 0.0;
        low1Fps = (ftP99 > 0.0) ? 1.0e6 / ftP99  : 0.0;   // 1% low = FPS at the p99 frame
    }
    // A frame past 50 ms (under 20 fps for that one frame) is what a player calls a
    // freeze; 100 ms is a visible skip.
    const long long hitches50 = fs.hitchCount();   // exact, unlike countAbove
    const long long hitches100 = fs.countAbove(100000.0);
    out += "\n=== FRAME RATE (whole stint) ===\n";
    snprintf(line, sizeof(line), "Duration: %.2f s (%lld frames)\n", durationSec, nSamples); out += line;
    if (nSamples > 0) {
        snprintf(line, sizeof(line), "FPS: min %.1f, avg %.1f, max %.1f\n", fpsMin, fpsAvg, fpsMax); out += line;
        snprintf(line, sizeof(line), "Frame time: p50 %.0f us, p99 %.0f us, max %.0f us\n", ftP50, ftP99, ftMax); out += line;
        snprintf(line, sizeof(line), "1%% low: %.1f fps  (FPS at the p99 frame time -- the stutter metric)\n", low1Fps); out += line;
        snprintf(line, sizeof(line), "Hitches: %lld frames over 50 ms, %lld over 100 ms\n", hitches50, hitches100); out += line;
        if (fs.gapCount() > 0) {
            snprintf(line, sizeof(line), "Gaps (no frames: menus/loading): %d, %.1f s -- left out of every figure\n",
                     fs.gapCount(), fs.gapSec()); out += line;
        }

        // Each of the longest frames, when it happened (match it against a video of
        // the run), at most what the plugin spent inside it (an upper bound, see
        // sampleFrameTime -- hence "<="), and its longest single callback. A long
        // frame with a small plugin figure was not the plugin; a large figure made
        // of one long call points at that callback, while the same figure spread
        // over many short calls points at the PC pausing the game thread.
        const auto& bmw = PluginData::getInstance().getBenchmarkMetrics();
        FrameTimeStats::Spike worst[FrameTimeStats::WORST_COUNT];
        const int nWorst = fs.worst(worst, FrameTimeStats::WORST_COUNT);
        out += "\n=== WORST FRAMES (longest first) ===\n";
        snprintf(line, sizeof(line), "%10s %10s %10s %7s  %s\n", "At m:ss.s", "Frame ms", "Plugin<=ms", "Riders", "Longest call"); out += line;
        snprintf(line, sizeof(line), "%10s %10s %10s %7s  %s\n", "----------", "----------", "----------", "-------", "------------"); out += line;
        for (int i = 0; i < nWorst; ++i) {
            const auto& w = worst[i];
            // Rounded to tenths FIRST, so 59.96 s reads 1:00.0, never 0:60.0.
            const long long tenths = std::llround(w.atSec * 10.0);
            char atText[24];
            snprintf(atText, sizeof(atText), "%lld:%02lld.%lld", tenths / 600, (tenths % 600) / 10, tenths % 10);
            char callText[48] = "-";
            if (w.maxCallIdx >= 0 && w.maxCallIdx < bmw.callbackCount) {
                snprintf(callText, sizeof(callText), "%s %.2f ms",
                         bmw.callbacks[w.maxCallIdx].name, static_cast<double>(w.maxCallUs) / 1000.0);
            }
            snprintf(line, sizeof(line), "%10s %10.1f %10.2f %7d  %s\n",
                     atText, w.frameUs / 1000.0, static_cast<double>(w.pluginUs) / 1000.0, w.riders, callText); out += line;
        }
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
    // the engine); timing is the stint table's job.
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
        // frames it is NOT dirty. Summed it can exceed the rebuilds themselves, and
        // only a per-HUD figure says where it goes.
        out += "=== HUD REBUILDS (STINT TOTALS) ===\n";
        snprintf(line, sizeof(line), "%-24s %12s %9s %10s %10s %10s %7s\n",
                 "Name", "Total us", "Avg us", "Peak us", "Rebuilds", "Idle us/f", "% upd"); out += line;
        snprintf(line, sizeof(line), "%-24s %12s %9s %10s %10s %10s %7s\n",
                 "------------------------", "------------", "---------", "----------", "----------", "----------", "-------"); out += line;
        int ord[MAX_HUD_SNAPSHOTS]; int nOrd = 0;
        // Every HUD with EITHER a rebuild or a timed update: a HUD that never
        // rebuilds and still costs time per frame is exactly what this is hunting.
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
    // them. Without the split a Draw regression has to be bisected instead of read.
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
    // EMITTED IN SEGMENTS, not one snprintf into a fixed buffer, and that is
    // correctness rather than style: the line is longer than a comfortable buffer,
    // and an overrun fails silently -- snprintf truncates and returns, the report
    // still parses, and every key that survives is correct, so the trailing keys
    // (cb_total_us, the four probe_* keys) simply vanish. A bigger buffer would only
    // move the cliff; segments remove it, because `out` is a std::string and each
    // piece below is bounded by its own format.
    //
    // The last segment is therefore load-bearing as a completeness check: if a report
    // ends mid-line, the line was cut, and benchmark_report.py requires the terminal
    // keys for exactly that reason.
    //
    // gl_ingame / gl_drew: a report must say WHICH RENDERER IT MEASURED. Two
    // reports taken minutes apart are otherwise indistinguishable, so the whole
    // comparison rests on someone remembering which file was which - and a
    // mislabelled pair does not look wrong, it looks like a result. gl_drew is
    // the honest half: the setting can be on while the backend declined (a
    // failed init), and what was measured is what DREW.
    {
        char seg[256];
        snprintf(seg, sizeof(seg),
            "BENCH game=\"%s\" track=\"%s\" riders=%d huds=%d threaded=%d "
            "gl_ingame=%d gl_drew=%d target_fps=%d budget_us=%.0f ",
            GAME_NAME, trackName, riders, m_hudSnapshotCount, threaded ? 1 : 0,
            UiConfig::getInstance().getGlInGame() ? 1 : 0,
            HudManager::getInstance().glDrewLastFrame() ? 1 : 0,
            PluginConstants::TARGET_FPS, PluginConstants::FRAME_BUDGET_US);
        out += seg;
        snprintf(seg, sizeof(seg),
            "dur_s=%.2f frames=%lld frames_sampled=%lld fps_min=%.1f fps_avg=%.1f fps_max=%.1f "
            "ft_p50_us=%.0f ft_p99_us=%.0f ft_max_us=%.0f lowfps_1pct=%.1f "
            "hitches_50ms=%lld hitches_100ms=%lld ",
            durationSec, nSamples, nSamples, fpsMin, fpsAvg, fpsMax,
            ftP50, ftP99, ftMax, low1Fps, hitches50, hitches100);
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

    if (writtenPath) *writtenPath = filePath;
    if (!AtomicFileWriter::submit(filePath, std::move(out))) {
        DEBUG_WARN_F("BenchmarkWidget: Failed to write %s", filePath.c_str());
        return false;
    }
    DEBUG_INFO_F("BenchmarkWidget: Report exported to %s", filePath.c_str());
    return true;
}
