// ============================================================================
// core/render_probe_sweep.cpp
// See render_probe_sweep.h for why this drives itself rather than asking a human
// to step an INI file nineteen times.
// ============================================================================
#include "render_probe_sweep.h"

#include <algorithm>
#include <cstdio>
#include <ctime>

#include <windows.h>   // CreateDirectoryA

#include "../diagnostics/logger.h"
#include "atomic_file_writer.h"
#include "hud_manager.h"
#include "plugin_constants.h"
#include "settings_manager.h"
#include "ui_config.h"
#include "gl_probe.h"

namespace {

// THE MATRIX. Each row applies one probe configuration; the differences between rows
// are the measurements. See tools/probetheme/README.md.
//
// Every family starts from the SAME baseline row (probe off), which is measured once
// rather than per family: it is the same configuration each time, and re-measuring it
// would only add wall-clock during which the machine can drift.
//
// The fullscreen rows use tiny N on purpose -- a full-screen quad covers the whole
// viewport, so ten of them is ten screens of overdraw and already a heavy load. They
// answer a different question (cost per PIXEL) from the tiny-quad rows (cost per
// PRIMITIVE), and mixing the two N ranges would make both unreadable.
constexpr RenderProbeSweep::Step kSteps[] = {
    { "baseline (probe off)",        0, 0, false,    0, 15, 64 },

    // R1 -- per-quad submit, untextured, negligible fill.
    { "fill, tiny",                  0, 0, false,  500, 15, 64 },
    { "fill, tiny",                  0, 0, false, 1000, 15, 64 },
    { "fill, tiny",                  0, 0, false, 2000, 15, 64 },

    // R2 -- textured, ONE sprite for the whole frame: sampling with no switching.
    // Sprite 1 rather than a chosen one: any single sprite answers "does texturing
    // cost more than filling", and which sprite it was is recorded in the report.
    { "sprite pinned, tiny",         1, 1, false,  500, 15, 64 },
    { "sprite pinned, tiny",         1, 1, false, 1000, 15, 64 },
    { "sprite pinned, tiny",         1, 1, false, 2000, 15, 64 },

    // R3 -- textured, cycling every registered sprite: a texture switch per quad.
    // R3 minus R2 is the switch cost, and it is the row that decides whether the fix
    // is "draw fewer boxes" or "draw them from fewer sprites".
    { "sprite cycled, tiny",         1, 0, false,  500, 15, 64 },
    { "sprite cycled, tiny",         1, 0, false, 1000, 15, 64 },
    { "sprite cycled, tiny",         1, 0, false, 2000, 15, 64 },

    // R4/R5 -- fill rate, untextured then textured. A nine-slice's centre is a large
    // STRETCHED TEXTURED quad, so the textured rows are the case a themed panel is.
    { "fill, FULLSCREEN",            0, 0, true,     2, 15, 64 },
    { "fill, FULLSCREEN",            0, 0, true,     5, 15, 64 },
    { "fill, FULLSCREEN",            0, 0, true,    10, 15, 64 },
    { "sprite pinned, FULLSCREEN",   1, 1, true,     2, 15, 64 },
    { "sprite pinned, FULLSCREEN",   1, 1, true,     5, 15, 64 },
    { "sprite pinned, FULLSCREEN",   1, 1, true,    10, 15, 64 },

    // R6 -- the text path, which is a separate primitive array to the engine and
    // which we hand over ~930 of every frame. COUNT at a fixed length first...
    { "text, 15 chars",              2, 0, false,  500, 15, 64 },
    { "text, 15 chars",              2, 0, false, 1000, 15, 64 },
    { "text, 15 chars",              2, 0, false, 2000, 15, 64 },

    // ...then LENGTH at a fixed count, because the engine bills per GLYPH and the
    // count sweep alone cannot see that. Measuring only at 15 characters produced a
    // real wrong answer: applied to the plugin's own strings, which average about
    // nine, it overstated the cost of drop shadow by 1.7x. Four lengths give a slope
    // (us per character) and an intercept (what a string costs before its first
    // glyph), and those two together price any string the HUD actually draws.
    { "text length",                 2, 0, false, 1000,  4, 64 },
    { "text length",                 2, 0, false, 1000,  8, 64 },
    { "text length",                 2, 0, false, 1000, 16, 64 },
    { "text length",                 2, 0, false, 1000, 32, 64 },

    // R7 -- FULLY TRANSPARENT quads, against the identical rows at alpha 64 above.
    // HUDs emit their background quad even at zero opacity ("always add quad to keep
    // indices consistent" -- addBackgroundQuad), which is 27 invisible quads per
    // themed panel. Whether that is worth removing depends entirely on whether the
    // engine early-outs on alpha 0, and nothing in the plugin can see the answer.
    // Same N as the alpha-64 fill rows so the two are read straight across.
    { "fill, tiny, ALPHA 0",         0, 0, false, 1000, 15,  0 },
    { "fill, tiny, ALPHA 0",         0, 0, false, 2000, 15,  0 },

    // R8 -- DEGENERATE (zero-area) quads, chars=1 selecting the shape. Every themed
    // panel submits several of these: finalizeThemedFill degenerates its unused fill
    // strips in place rather than removing them. Read against the alpha-64 tiny fill
    // rows at the same N.
    { "fill, DEGENERATE",            0, 0, false, 1000,  1, 64 },
    { "fill, DEGENERATE",            0, 0, false, 2000,  1, 64 },

    // R9 -- THE IN-CONTEXT GL COMPARISON (spike Phase 1; needs glProbe=2, and
    // the rows read as zero-cost without it, which the report says out loud).
    //
    // Each row prices the SAME load as an "fill, tiny" row above, drawn by us
    // inside the game's GL context instead of handed to the engine. Same shape,
    // same alpha, same scene, seconds apart -- which is the comparison the
    // spike's pass/fail bar is stated in, and one no pair of hand-run sessions
    // can produce. (This file's header records what hand-stepping a sweep
    // actually produced: five internally-perfect reports of an experiment that
    // never happened.)
    //
    // 6000 is included on BOTH sides because the bar is stated at realistic HUD
    // loads and a themed frame reaches ~6k quads; 500/1000/2000 mirror the
    // engine rows exactly so the two matrices difference straight across.
    { "fill, tiny",                  0, 0, false, 6000, 15, 64 },
    { "GL fill, tiny, batched",      0, 0, false,    0, 15, 64,  500, 1 },
    { "GL fill, tiny, batched",      0, 0, false,    0, 15, 64, 1000, 1 },
    { "GL fill, tiny, batched",      0, 0, false,    0, 15, 64, 2000, 1 },
    { "GL fill, tiny, batched",      0, 0, false,    0, 15, 64, 6000, 1 },
    // Immediate mode is the slowest submission GL has, so these rows are a
    // FLOOR, not a prediction of a real backend. Kept because a batched-only
    // report would leave "how much of this is the batching?" unanswerable.
    { "GL fill, tiny, immediate",    0, 0, false,    0, 15, 64, 1000, 0 },
    { "GL fill, tiny, immediate",    0, 0, false,    0, 15, 64, 2000, 0 },
};
constexpr std::size_t kStepCount = sizeof(kSteps) / sizeof(kSteps[0]);

// Wall-clock, not a frame count. A frame count makes the heavy steps take
// proportionally longer in real time (a 10-fullscreen-quad step at 20fps would run
// twenty times as long as the baseline), and the whole point of an automatic sweep is
// that every step is measured close in time to every other one.
constexpr double kWarmupSec = 0.6;   // discard: the first frames after a config change
                                     // carry allocation and driver churn
constexpr double kSampleSec = 2.2;

double percentile(std::vector<double>& v, double frac) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    std::size_t i = static_cast<std::size_t>(frac * static_cast<double>(v.size() - 1) + 0.5);
    return v[std::min(i, v.size() - 1)];
}

}  // namespace

RenderProbeSweep& RenderProbeSweep::getInstance() {
    static RenderProbeSweep instance;
    return instance;
}

void RenderProbeSweep::applyProbe(const Step& s) const {
    UiConfig& ui = UiConfig::getInstance();
    ui.setRenderProbeType(s.type);
    ui.setRenderProbeSprite(s.sprite);
    ui.setRenderProbeFullscreen(s.fullscreen);
    ui.setRenderProbeTextChars(s.chars);
    ui.setRenderProbeAlpha(s.alpha);
    ui.setGlProbeBatch(s.glBatch);
    ui.setRenderProbeQuads(s.n);   // LAST: the count is the switch, so nothing is
                                   // emitted under a half-applied configuration
    ui.setGlProbeQuads(s.glQuads); // ...and the same for the GL side's own switch
}

void RenderProbeSweep::start() {
    if (m_running) return;

    UiConfig& ui = UiConfig::getInstance();
    m_savedQuads = ui.getRenderProbeQuads();
    m_savedType = ui.getRenderProbeType();
    m_savedFullscreen = ui.getRenderProbeFullscreen();
    m_savedSprite = ui.getRenderProbeSprite();
    m_savedTextChars = ui.getRenderProbeTextChars();
    m_savedAlpha = ui.getRenderProbeAlpha();
    m_savedGlQuads = ui.getGlProbeQuads();
    m_savedGlBatch = ui.getGlProbeBatch();

    m_results.clear();
    m_results.reserve(kStepCount);
    m_running = true;
    DEBUG_INFO_F("RenderProbeSweep: starting %zu steps (~%.0fs)", kStepCount,
                 kStepCount * (kWarmupSec + kSampleSec));
    beginStep(0);
}

void RenderProbeSweep::beginStep(std::size_t index) {
    m_stepIndex = index;
    m_warmingUp = true;
    m_haveLastFrame = false;
    m_samplesUs.clear();
    m_stepStart = std::chrono::steady_clock::now();
    applyProbe(kSteps[index]);

    char buf[128];
    snprintf(buf, sizeof(buf), "PROBE SWEEP %zu/%zu  %s  N=%d",
             index + 1, kStepCount, kSteps[index].label, kSteps[index].n);
    m_status = buf;
}

void RenderProbeSweep::finishStep() {
    Result r;
    r.step = kSteps[m_stepIndex];
    r.frames = static_cast<int>(m_samplesUs.size());
    r.quadsHandedOver = static_cast<int>(HudManager::getInstance().gameFrameQuads().size());
    r.glPainted = GlProbe::status().loadPainted;
    double sum = 0.0;
    for (double v : m_samplesUs) sum += v;
    r.meanUs = m_samplesUs.empty() ? 0.0 : sum / static_cast<double>(m_samplesUs.size());
    r.p50Us = percentile(m_samplesUs, 0.5);
    m_results.push_back(r);
}

void RenderProbeSweep::tick() {
    if (!m_running) return;

    const auto now = std::chrono::steady_clock::now();
    const double stepElapsed = std::chrono::duration<double>(now - m_stepStart).count();

    if (m_warmingUp) {
        if (stepElapsed < kWarmupSec) { m_haveLastFrame = false; return; }
        m_warmingUp = false;
        m_haveLastFrame = false;   // the first post-warmup interval spans the boundary
        return;
    }

    if (m_haveLastFrame) {
        const double dtUs = std::chrono::duration<double, std::micro>(now - m_lastFrame).count();
        if (dtUs > 0.0) m_samplesUs.push_back(dtUs);
    }
    m_lastFrame = now;
    m_haveLastFrame = true;

    if (stepElapsed >= kWarmupSec + kSampleSec) {
        finishStep();
        if (m_stepIndex + 1 < kStepCount) {
            beginStep(m_stepIndex + 1);
        } else {
            finish(true);
        }
    }
}

void RenderProbeSweep::abort() {
    if (!m_running) return;
    if (!m_samplesUs.empty()) finishStep();   // keep the partial step; it is still data
    finish(false);
}

void RenderProbeSweep::finish(bool completed) {
    m_running = false;
    m_status.clear();

    // Restore BEFORE writing: if the report write throws or the path is bad, the
    // user's settings are still put back rather than left mid-sweep.
    UiConfig& ui = UiConfig::getInstance();
    ui.setRenderProbeType(m_savedType);
    ui.setRenderProbeSprite(m_savedSprite);
    ui.setRenderProbeFullscreen(m_savedFullscreen);
    ui.setRenderProbeTextChars(m_savedTextChars);
    ui.setRenderProbeAlpha(m_savedAlpha);
    ui.setRenderProbeQuads(m_savedQuads);
    ui.setGlProbeQuads(m_savedGlQuads);
    ui.setGlProbeBatch(m_savedGlBatch);

    writeReport(completed);
    DEBUG_INFO_F("RenderProbeSweep: %s after %zu steps",
                 completed ? "completed" : "aborted", m_results.size());
}

void RenderProbeSweep::writeReport(bool completed) const {
    // The SAME folder every other export uses -- see SettingsManager::getBenchmarksDir.
    // Built by hand here once, and it dropped the `mxbmrp3` segment, so sweeps landed a
    // folder above the benchmark reports they are meant to sit beside.
    const std::string dir = SettingsManager::getInstance().getBenchmarksDir();
    if (dir.empty()) {
        DEBUG_WARN("RenderProbeSweep: no save path - report not written");
        return;
    }

    std::time_t t = std::time(nullptr);
    std::tm tmv{};
    localtime_s(&tmv, &t);
    char stamp[32];
    std::strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", &tmv);
    const std::string path = dir + "\\probe_sweep_" + stamp + ".txt";

    const std::string out = buildReport(completed);
    if (!AtomicFileWriter::writeFileAtomic(path, out)) {
        DEBUG_WARN_F("RenderProbeSweep: failed to write %s", path.c_str());
        return;
    }
    DEBUG_INFO_F("RenderProbeSweep: report written to %s", path.c_str());
}

std::string RenderProbeSweep::buildReport(bool completed) const {
    std::time_t t = std::time(nullptr);
    std::tm tmv{};
    localtime_s(&tmv, &t);
    char stamp[32];

    std::string out;
    char line[320];

    out += "MXBMRP3 Render Probe Sweep\n";
    snprintf(line, sizeof(line), "Version: %s\n", PluginConstants::PLUGIN_VERSION); out += line;
    std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &tmv);
    snprintf(line, sizeof(line), "Date: %s\n", stamp); out += line;
    snprintf(line, sizeof(line), "Steps: %zu of %zu%s\n", m_results.size(), kStepCount,
             completed ? "" : "  *** ABORTED - partial ***"); out += line;
    snprintf(line, sizeof(line), "Registered sprites: %d\n",
             HudManager::getInstance().registeredSpriteCount()); out += line;
    snprintf(line, sizeof(line), "Per step: %.1fs warm-up discarded, %.1fs sampled\n",
             kWarmupSec, kSampleSec); out += line;

    // The engine's cost is what the frame time does when N moves, so the table is
    // frame time and the delta against the baseline -- never FPS, which is not
    // additive and would have to be converted back before anything could be
    // differenced.
    out += "\n=== STEPS (frame time; the engine's cost is the RISE, not the level) ===\n";
    out += "Step                            Type   Sprite  N      Chars  Frames   p50 us   mean us  d p50   us/prim   Quads\n";
    out += "------------------------------- ------ ------- ------ ------ -------- -------- -------- ------- --------- --------\n";

    const double baseP50 = m_results.empty() ? 0.0 : m_results[0].p50Us;
    for (const Result& r : m_results) {
        const char* typeName = (r.step.type == 2) ? "text"
                             : (r.step.type == 1) ? "sprite" : "fill";
        char spriteCol[16];
        if (r.step.type != 1)          snprintf(spriteCol, sizeof(spriteCol), "-");
        else if (r.step.sprite > 0)    snprintf(spriteCol, sizeof(spriteCol), "pin %d", r.step.sprite);
        else                           snprintf(spriteCol, sizeof(spriteCol), "cycle");

        const double delta = r.p50Us - baseP50;
        // Per-primitive cost, the number the whole sweep exists to produce. Undefined
        // for the baseline (N=0), and printed as blank rather than as 0.0 -- a zero
        // there would read as "a primitive is free".
        char perPrim[16];
        if (r.step.n > 0) snprintf(perPrim, sizeof(perPrim), "%9.3f", delta / r.step.n);
        else              snprintf(perPrim, sizeof(perPrim), "%9s", "-");

        // Chars only means something for text; a number here on a quad row would
        // invite reading it as part of the quad's cost.
        char charsCol[8];
        if (r.step.type == 2) snprintf(charsCol, sizeof(charsCol), "%d", r.step.chars);
        else                  snprintf(charsCol, sizeof(charsCol), "-");
        snprintf(line, sizeof(line),
                 "%-31s %-6s %-7s %-6d %-6s %-8d %8.0f %8.0f %7.0f %s %8d\n",
                 r.step.label, typeName, spriteCol, r.step.n, charsCol, r.frames,
                 r.p50Us, r.meanUs, delta, perPrim, r.quadsHandedOver);
        out += line;
    }

    // The four derived answers, computed here rather than left to be eyeballed: the
    // whole failure mode this sweep exists to prevent is a person doing arithmetic
    // across files by hand.
    out += "\n=== DERIVED (us per primitive, from the largest N of each family) ===\n";
    auto perPrimOf = [&](int type, int sprite, bool fs) -> double {
        double best = 0.0;
        int bestN = 0;
        for (const Result& r : m_results) {
            if (r.step.type != type || r.step.fullscreen != fs || r.step.n <= 0) continue;
            if (type == 1 && r.step.sprite != sprite) continue;
            // The alpha-0 and degenerate (chars==1) fill rows are their own
            // families, read against this one below — matching them here would
            // hand the baseline to whichever iterates last at the same N.
            if (type == 0 && (r.step.alpha == 0 || r.step.chars == 1)) continue;
            if (r.step.n > bestN) { bestN = r.step.n; best = (r.p50Us - baseP50) / r.step.n; }
        }
        return best;
    };
    const double fillTiny   = perPrimOf(0, 0, false);
    const double sprPinned  = perPrimOf(1, 1, false);
    const double sprCycled  = perPrimOf(1, 0, false);
    const double fillFs     = perPrimOf(0, 0, true);
    const double sprFs      = perPrimOf(1, 1, true);
    const double textCost   = perPrimOf(2, 0, false);

    snprintf(line, sizeof(line), "  quad, untextured, tiny        %8.3f us  (what a quad costs to exist)\n", fillTiny); out += line;
    snprintf(line, sizeof(line), "  quad, textured, one sprite    %8.3f us  (+%.3f = the cost of TEXTURING)\n", sprPinned, sprPinned - fillTiny); out += line;
    snprintf(line, sizeof(line), "  quad, textured, cycling       %8.3f us  (+%.3f = the cost of SWITCHING)\n", sprCycled, sprCycled - sprPinned); out += line;
    snprintf(line, sizeof(line), "  quad, fullscreen, untextured  %8.3f us  (cost per screen of FILL)\n", fillFs); out += line;
    snprintf(line, sizeof(line), "  quad, fullscreen, textured    %8.3f us  (+%.3f = textured fill premium)\n", sprFs, sprFs - fillFs); out += line;
    snprintf(line, sizeof(line), "  text string (15 chars)        %8.3f us\n", textCost); out += line;

    // Is a fully transparent quad free? Read straight across from the alpha-64 fill
    // rows at the same N, so nothing else differs.
    {
        double opaque = 0.0, clear = 0.0;
        for (const Result& r : m_results) {
            if (r.step.type != 0 || r.step.fullscreen || r.step.n != 2000) continue;
            if (r.step.chars == 1) continue;  // DEGENERATE row: its own comparison below
            if (r.step.alpha == 0) clear  = (r.p50Us - baseP50) / r.step.n;
            else                   opaque = (r.p50Us - baseP50) / r.step.n;
        }
        if (opaque > 0.0 && clear > 0.0) {
            snprintf(line, sizeof(line),
                     "  quad, ALPHA 0                 %8.3f us  (%.0f%% of an opaque quad -- %s)\n",
                     clear, 100.0 * clear / opaque,
                     (clear > 0.6 * opaque) ? "transparent quads are charged in full"
                                            : "the engine short-circuits them");
            out += line;
        }
        double degen = 0.0;
        for (const Result& r : m_results)
            if (r.step.type == 0 && !r.step.fullscreen && r.step.n == 2000 && r.step.chars == 1)
                degen = (r.p50Us - baseP50) / r.step.n;
        if (opaque > 0.0 && degen > 0.0) {
            snprintf(line, sizeof(line),
                     "  quad, ZERO-AREA               %8.3f us  (%.0f%% of a drawn quad -- %s)\n",
                     degen, 100.0 * degen / opaque,
                     (degen > 0.6 * opaque)
                         ? "degenerate quads are charged in full; the reserved fill strips are waste"
                         : "the engine rejects them early; the reserved strips are near-free");
            out += line;
        }
    }

    // THE TEXT COST IS A LINE, NOT A NUMBER, and reporting it as a number was a real
    // error: measured only at 15 characters and applied to the plugin's own strings,
    // which average about nine, it overstated drop shadow by 1.7x. Two points from the
    // length sweep give slope (per glyph) and intercept (per string before its first
    // glyph); price a real string as intercept + slope * length.
    {
        double loUs = 0.0, hiUs = 0.0;
        int loC = 0, hiC = 0;
        for (const Result& r : m_results) {
            if (r.step.type != 2 || r.step.n <= 0) continue;
            const double per = (r.p50Us - baseP50) / r.step.n;
            if (loC == 0 || r.step.chars < loC) { loC = r.step.chars; loUs = per; }
            if (r.step.chars > hiC)             { hiC = r.step.chars; hiUs = per; }
        }
        if (hiC > loC && loC > 0) {
            const double perChar = (hiUs - loUs) / (hiC - loC);
            const double perString = loUs - perChar * loC;
            snprintf(line, sizeof(line),
                     "  text: %.4f us per glyph + %.4f us per string\n", perChar, perString);
            out += line;
            snprintf(line, sizeof(line),
                     "        (a 9-char string, the HUD's rough average: %.3f us)\n",
                     perString + perChar * 9.0); out += line;
        }
    }

    // ---- The in-context GL comparison (spike Phase 1) --------------------
    // Stated as numbers on both sides and then judged, because the plan's bar is
    // explicit that "faster" is not a result.
    //
    // ESTIMATOR: the SLOPE between the smallest and largest N of a family, not
    // delta/N of one row. The baseline drifts tens of microseconds between steps
    // (visible in the fullscreen rows, which cost nearly nothing yet came in at
    // -29 us in one field run and -1 us in the next), and that offset is charged
    // in full to a single-row estimate. Differencing two rows cancels it - which
    // is what this file's own header says about the engine cost being the RISE
    // and not the level, applied to itself. It matters: on the field data the
    // single-row estimate put the engine at 0.71-0.74 us/quad while the slope put
    // it at 0.9393 and 0.9364 across two runs, agreeing to 0.3%.
    {
        struct Fam { double firstDelta = 0, lastDelta = 0; int firstN = 0, lastN = 0; int rows = 0; };
        auto family = [&](const char* label, int batch, bool glSide) {
            Fam f;
            for (const Result& r : m_results) {
                const int n = glSide ? r.step.glQuads : r.step.n;
                if (n <= 0 || std::string(r.step.label) != label) continue;
                if (glSide && r.step.glBatch != batch) continue;
                const double d = r.p50Us - baseP50;
                if (f.rows == 0) { f.firstDelta = d; f.firstN = n; }
                f.lastDelta = d; f.lastN = n;
                ++f.rows;
            }
            return f;
        };
        auto slopeOf = [](const Fam& f) {
            return (f.rows >= 2 && f.lastN > f.firstN)
                 ? (f.lastDelta - f.firstDelta) / (f.lastN - f.firstN) : 0.0;
        };

        const Fam engineF = family("fill, tiny", 1, false);
        const Fam batchF  = family("GL fill, tiny, batched", 1, true);
        const Fam immedF  = family("GL fill, tiny, immediate", 0, true);
        const double enginePer = slopeOf(engineF);
        const double glBatched = slopeOf(batchF);
        const double glImmed   = slopeOf(immedF);

        // Noise, derived from THIS run rather than hardcoded: the fullscreen rows
        // draw 2-10 quads and so cost almost nothing, making their spread a direct
        // reading of the baseline's wander during this sweep. An earlier version
        // hardcoded 20 us and was wrong on a machine that swings +-32 us.
        double fsMin = 0.0, fsMax = 0.0; bool haveFs = false;
        for (const Result& r : m_results) {
            if (!r.step.fullscreen || r.step.glQuads > 0) continue;
            const double d = r.p50Us - baseP50;
            if (!haveFs) { fsMin = fsMax = d; haveFs = true; }
            if (d < fsMin) fsMin = d;
            if (d > fsMax) fsMax = d;
        }
        const double noiseUs = haveFs ? (fsMax - fsMin) : 0.0;
        const double glRise = batchF.lastDelta - batchF.firstDelta;

        bool anyGlRow = false, anyPaintConfirmed = false, anyPaintFailed = false;
        for (const Result& r : m_results) {
            if (r.step.glQuads <= 0) continue;
            anyGlRow = true;
            if (r.glPainted == 1) anyPaintConfirmed = true;
            if (r.glPainted == 0) anyPaintFailed = true;
        }

        if (anyGlRow) {
            out += "\nIn-context GL vs engine (per untextured tiny quad, from the SLOPE\n";
            out += "across N so the baseline's drift between steps cancels):\n";
            const GlProbe::Status gp = GlProbe::status();
            if (!gp.contextCurrent || !gp.drew) {
                out += "  *** GL ROWS MEASURED NOTHING. ";
                out += (!gp.contextCurrent ? "No GL context was current on the Draw thread"
                                           : "The probe never drew (is [Advanced] glProbe=2?)");
                out += ".\n      Every number below is the cost of drawing zero quads. Ignore\n";
                out += "      them, fix the setting, and re-run.\n";
            } else if (anyPaintFailed) {
                out += "  *** GL ROWS DREW NOTHING (readback found no pixels). Their timing\n";
                out += "      is the cost of drawing zero quads. No verdict.\n";
            } else if (!anyPaintConfirmed) {
                out += "  *** ENGAGEMENT UNVERIFIED: no GL row confirmed it painted (the\n";
                out += "      readback never ran). The timings below may be the cost of\n";
                out += "      drawing nothing, which is indistinguishable from being fast.\n";
                out += "      No verdict. Check the log for 'measurement load ... readback'.\n";
            } else {
                // The rise must clear the baseline's own wander before it counts.
                const bool resolved = (glRise > 0.0) && (glRise > 2.0 * noiseUs);
                const double glCost = resolved ? glBatched
                                    : (batchF.lastN > 0 ? (2.0 * noiseUs) / batchF.lastN : 0.0);
                snprintf(line, sizeof(line), "  engine:              %.5f us/quad\n", enginePer);
                out += line;
                if (resolved) {
                    snprintf(line, sizeof(line), "  in-context, batched: %.5f us/quad"
                             "  (rise %.0f us over N=%d..%d, vs %.0f us of baseline noise)\n",
                             glBatched, glRise, batchF.firstN, batchF.lastN, noiseUs);
                } else {
                    // Split appends, never one long snprintf: the previous version
                    // built a 319-char message into a 320-char buffer and lost its
                    // trailing newline, running two report lines together.
                    out += "  in-context, batched: BELOW THIS INSTRUMENT'S RESOLUTION -\n";
                    snprintf(line, sizeof(line),
                             "                       the rise across N (%.0f us) does not clear the\n",
                             glRise); out += line;
                    snprintf(line, sizeof(line),
                             "                       baseline's own wander (%.0f us), so only a bound\n",
                             noiseUs); out += line;
                    snprintf(line, sizeof(line),
                             "                       is defensible: < %.5f us/quad\n", glCost);
                }
                out += line;
                // The immediate row does not feed the verdict, but a negative
                // us/quad is meaningless wherever it appears - and printing one
                // is exactly the habit that made the first field report look
                // authoritative while being nonsense.
                if (glImmed > 0.0) {
                    snprintf(line, sizeof(line), "  in-context, immediate (a FLOOR, not a backend):"
                                                 " %.5f us/quad\n", glImmed); out += line;
                } else {
                    out += "  in-context, immediate (a FLOOR, not a backend):"
                           " also below resolution\n";
                }
                if (enginePer > 0.0) {
                    double recovered = (enginePer - glCost) / enginePer * 100.0;
                    if (recovered > 100.0) recovered = 100.0;
                    if (recovered < 0.0) recovered = 0.0;
                    snprintf(line, sizeof(line),
                             "  batched GL recovers %s%.1f%% of the engine's per-quad cost"
                             " (%.0fx cheaper)\n",
                             resolved ? "" : "at least ", recovered,
                             glCost > 0.0 ? enginePer / glCost : 0.0); out += line;
                    snprintf(line, sizeof(line),
                             "  at 6000 quads: engine %.3f ms, in-context %s%.3f ms"
                             " (budget 2.08 ms)\n",
                             enginePer * 6.0, resolved ? "" : "<", glCost * 6.0);
                    out += line;
                    const bool majority = recovered > 50.0;
                    const bool inBudget = (glCost * 6.0) < 2.08;
                    out += (majority && inBudget)
                        ? "  => PASS by the plan's bar (majority recovered, inside budget).\n"
                          "     Confirm it reproduces across runs before acting on it.\n"
                        : "  => FAILS the plan's bar. A marginal win does not justify a\n"
                          "     second renderer's permanent maintenance -- see the plan.\n";
                }
            }
        }
    }

    out += "\nWhat these imply is in tools/probetheme/README.md. Texture SIZE is\n";
    out += "the one variable this cannot reach -- that needs probetheme.py's themes.\n";

    // Machine-readable, one line per step, for tools that want the raw sweep.
    out += "\n";
    for (const Result& r : m_results) {
        snprintf(line, sizeof(line),
                 "SWEEP step=\"%s\" type=%d sprite=%d fs=%d n=%d gl_n=%d gl_batch=%d "
                 "frames=%d p50_us=%.0f mean_us=%.0f delta_us=%.0f quads=%d\n",
                 r.step.label, r.step.type, r.step.sprite, r.step.fullscreen ? 1 : 0,
                 r.step.n, r.step.glQuads, r.step.glBatch,
                 r.frames, r.p50Us, r.meanUs, r.p50Us - baseP50,
                 r.quadsHandedOver);
        out += line;
    }

    return out;
}

#if defined(MXBMRP3_TEST_BUILD)
std::string RenderProbeSweep::testReportSynthetic(double fillUs, double alpha0Us,
                                                  double degenUs, double glPerQuadUs,
                                                  int glPaintState) {
    // Baseline frame time, arbitrary: the derived block differences against it,
    // so each row's delta is exactly the injected per-quad cost times its N.
    constexpr double kBase = 1000.0;
    m_results.clear();
    for (std::size_t i = 0; i < kStepCount; ++i) {
        const Step& s = kSteps[i];
        double per;
        if (s.type == 0) {
            per = (s.alpha == 0) ? alpha0Us
                : (s.chars == 1) ? degenUs
                                 : fillUs;
        } else {
            per = fillUs;  // sprite/text rows: not what this seam is for
        }
        Result r;
        r.step = s;
        r.frames = 100;
        // A GL row carries its load in glQuads, not n.
        r.p50Us = (s.glQuads > 0) ? kBase + glPerQuadUs * s.glQuads
                                  : kBase + per * s.n;
        // A synthetic GL row with zero cost IS the drew-nothing case - that is
        // what "free" and "absent" have in common and why timing cannot separate
        // them. So the seam models it, and the refusal branch gets covered.
        if (s.glQuads > 0) {
            r.glPainted = (glPaintState != -2) ? glPaintState
                                              : ((glPerQuadUs != 0.0) ? 1 : 0);
        }
        r.meanUs = r.p50Us;
        r.quadsHandedOver = s.n;
        m_results.push_back(r);
    }
    const std::string report = buildReport(true);
    m_results.clear();
    return report;
}
#endif
