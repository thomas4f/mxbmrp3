// ============================================================================
// core/render_probe_sweep.h
// Run the whole render-probe matrix automatically, from one hotkey, in one
// session -- and write a single report with the answers already differenced.
//
// WHY THIS EXISTS. The engine's cost to draw our primitives is spent after Draw()
// returns, so no in-plugin timer can see it; the only way to measure it is
// differentially, by handing over N extra synthetic primitives and watching frame
// time rise. That means a SWEEP -- several N, several primitive kinds -- and doing
// it by hand meant: edit the INI, reload, start the benchmark, stop it, save the
// file, repeat. Nineteen times.
//
// That process failed on its first outing, and it failed SILENTLY, which is the
// part worth designing against: five reports came back that were internally perfect
// and all measured the same thing, because the probe had never engaged. Nothing
// errors when an experiment does not happen. Worse, hand-stepping spreads the runs
// over minutes of wall-clock, during which the machine is free to change state
// underneath the comparison -- and every step invites the natural mistake of
// changing N *during* a benchmark window, which averages the Ns together rather
// than comparing them.
//
// So the sweep drives itself: each step applies its own probe configuration, waits
// out a warm-up, samples frame time for a fixed slice of wall-clock, and moves on.
// Nineteen steps land inside a minute, seconds apart, in one scene -- which is a
// far better-controlled comparison than a human with an INI file can produce, and
// cannot be half-performed.
//
// WHAT IT IS NOT. Not a replacement for BenchmarkWidget: that profiles OUR CPU, per
// callback and per panel, and this deliberately measures the thing that is invisible
// to it. They can run together; the sweep reads frame time only.
//
// See tools/probetheme/README.md for what each step isolates and what each
// outcome implies. Texture SIZE is the one variable this cannot reach -- it needs
// art that differs only in resolution, which is what probetheme.py generates.
// ============================================================================
#pragma once

#include <chrono>
#include <cstddef>
#include <string>
#include <vector>

class RenderProbeSweep {
public:
    static RenderProbeSweep& getInstance();

    // Begin the sweep (no-op if already running). Captures the user's current probe
    // settings and restores them at the end -- including on abort, so a stopped
    // sweep cannot leave the plugin quietly drawing 2000 synthetic quads forever.
    void start();
    // Stop early. The report is still written, marked as incomplete: a partial sweep
    // is data, and discarding it would punish the user for stopping.
    void abort();
    bool isRunning() const { return m_running; }

    // Per-frame, from produceFrame BEFORE the probe emits, so the step's settings are
    // the ones this frame draws with.
    void tick();

    // Progress for the on-screen status line: "step 7/19 - sprite pinned, N=1000".
    // Empty when not running.
    const std::string& statusLine() const { return m_status; }

    // One row of the matrix.
    struct Step {
        const char* label;
        int type;        // 0 = solid fill, 1 = sprite, 2 = text
        int sprite;      // type 1 only: 0 = cycle every sprite, k = pin sprite k
        bool fullscreen;
        int n;           // primitives per frame
        int chars;       // type 2 only: glyphs per string (see below)
        int alpha;       // quad alpha; 0 answers "is a transparent quad free?"
    };

    // What a step measured. Frame time, not FPS: the cost of N primitives is additive
    // in time and hyperbolic in rate, so a rate would have to be converted back before
    // any of it could be differenced.
    struct Result {
        Step step{};
        int frames = 0;
        double p50Us = 0.0;
        double meanUs = 0.0;
        int quadsHandedOver = 0;   // the frame's total, as a did-it-engage check
    };

#if defined(MXBMRP3_TEST_BUILD)
    // Report text for a synthetic sweep whose fill / alpha-0 / degenerate
    // per-quad costs are the given values — the seam that lets the DERIVED
    // block's arithmetic be asserted headlessly. A real sweep is ~40s of
    // wall-clock and its numbers are the machine's, so nothing else can pin
    // that the alpha-0 and zero-area verdicts difference against the OPAQUE
    // fill baseline rather than each other.
    std::string testReportSynthetic(double fillUs, double alpha0Us, double degenUs);
#endif

private:
    RenderProbeSweep() = default;

    void beginStep(std::size_t index);
    void finishStep();
    void finish(bool completed);
    void writeReport(bool completed) const;
    std::string buildReport(bool completed) const;
    void applyProbe(const Step& s) const;

    bool m_running = false;
    std::size_t m_stepIndex = 0;
    bool m_warmingUp = true;
    std::chrono::steady_clock::time_point m_stepStart{};
    std::chrono::steady_clock::time_point m_lastFrame{};
    bool m_haveLastFrame = false;
    std::vector<double> m_samplesUs;    // this step's frame intervals
    std::vector<Result> m_results;
    std::string m_status;

    // The user's probe settings, restored when the sweep ends by any route.
    int m_savedQuads = 0;
    int m_savedType = 0;
    bool m_savedFullscreen = false;
    int m_savedSprite = 0;
    int m_savedTextChars = 15;
    int m_savedAlpha = 64;
};
