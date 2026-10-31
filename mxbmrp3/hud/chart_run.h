// ============================================================================
// hud/chart_run.h
// One graph line's open run: the quad BaseHud::addChartSegment is stretching,
// and how far its end may still go. A graph draws a line per sample step, one
// quad each, and the engine pays per quad; consecutive pieces that stay within
// TOL_PX of a single straight line are drawn as that one line instead.
//
// The test is a cone: every joint J passed so far allows the run's end only the
// slopes (from the run's start S) that keep J within TOL_PX of the line, as a
// vertical distance - never less than the true distance, so a merge is never
// looser than TOL_PX. Each joint narrows [lo, hi]; a new end fits while its
// slope stays inside. O(1) per piece, no stored points, so a level line (every
// joint exactly on it) runs on for the whole graph.
//
// Pixels are measured at 1080p: a quarter of a pixel there is half a pixel at
// 4K. Graph lines only run left to right, so a piece that does not (a vertical
// step, a backwards x) starts a new run.
// ============================================================================
#pragma once

#include <algorithm>
#include <limits>

struct ChartRun {
    static constexpr float TOL_PX = 0.25f;
    static constexpr float PX_W = 1920.0f, PX_H = 1080.0f;

    int quad = -1;                  // index into the host's m_quads, -1 = no run
    unsigned long color = 0;        // the quad's colour, as stored
    float thickness = 0.0f;
    float sx = 0.0f, sy = 0.0f;     // start, screen-normalized (offset applied)
    float ex = 0.0f, ey = 0.0f;     // end
    float lo = 0.0f, hi = 0.0f;     // end slopes from the start, 1080p px per px

    void reset() { quad = -1; }

    void start(int q, unsigned long c, float t, float x1, float y1, float x2, float y2) {
        quad = q; color = c; thickness = t;
        sx = x1; sy = y1; ex = x2; ey = y2;
        lo = -std::numeric_limits<float>::infinity();
        hi = std::numeric_limits<float>::infinity();
    }

    // Whether x1,y1 -> x2,y2 carries on from the end and the run can take it;
    // on true the run ends at x2,y2.
    bool extend(float x1, float y1, float x2, float y2) {
        if (quad < 0 || x1 != ex || y1 != ey || !(x2 > x1) || !(x1 > sx)) return false;
        const float jdx = (x1 - sx) * PX_W, jdy = (y1 - sy) * PX_H;
        const float nlo = std::max(lo, (jdy - TOL_PX) / jdx);
        const float nhi = std::min(hi, (jdy + TOL_PX) / jdx);
        const float m = (y2 - sy) * PX_H / ((x2 - sx) * PX_W);
        if (!(m >= nlo && m <= nhi)) return false;
        lo = nlo; hi = nhi; ex = x2; ey = y2;
        return true;
    }
};
