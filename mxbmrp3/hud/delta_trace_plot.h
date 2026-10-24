// ============================================================================
// hud/delta_trace_plot.h
// The Delta Trace's chart, drawn into whichever HUD hosts it: the Delta Trace
// HUD's panel and the Dash's Delta Trace page. One plot, two hosts, so the page
// is the HUD's own picture (the sweep, the fills, the split lines, the peaks and
// the rider's marker) rather than a second copy of it. Each host keeps its own
// plot, so each holds its own scale through a lap.
//
// What it draws, and why, is documented at DeltaTraceHud (delta_trace_hud.h).
// BaseHud names this class a friend: it emits into the host's primitives with
// the host's own helpers, colours and offset.
// ============================================================================
#pragma once

#include "../core/lap_delta_profile.h"

#include <cstddef>

class BaseHud;
struct ScaledDimensions;

class DeltaTracePlot {
public:
    using Reference = PbGapTracker::Ref;

    // Whether the chart needs drawing again: the rider reached the next profile
    // point, the gap's validity flipped, a lap was committed or the reference
    // changed (or `force`). Track position has no change notification of its
    // own, so this runs a few times a second, not every frame. A new lap (or
    // live again after the pits) or another reference refits the scale.
    bool due(Reference reference, bool force);

    // The chart in the box x, y, w, h of `host`. Axis labels are the HUD's; the
    // Dash's page has no room for them.
    void draw(BaseHud& host, Reference reference, float x, float y, float w, float h,
              bool showPeaks, bool axisLabels, const ScaledDimensions& dims);

    // Between profile points only the rider's marker moves: replace its quad in
    // place (keeping its colour, alpha included) and leave the trace alone.
    // Returns whether it moved.
    bool moveMarker(BaseHud& host, Reference reference);

    // Points on the line at the last draw (0 = nothing plotted).
    int plottedPoints() const { return m_plotted; }
    // Points of the previous lap drawn ahead of the rider (0 when none).
    int previousPoints() const { return m_plottedPrevious; }
    // Whether the rider's marker was drawn at the last draw.
    bool markerShown() const { return m_markerShown; }
    // Draws so far (the marker's in-place move is not one).
    int drawCount() const { return m_draws; }
    // Peak markers drawn at the last draw, and their gaps (0 when that side has none).
    int peaksShown() const { return m_peaksShown; }
    int peakAheadMs() const { return m_peakAhead.point >= 0 ? m_peakAhead.gapMs : 0; }
    int peakBehindMs() const { return m_peakBehind.point >= 0 ? m_peakBehind.gapMs : 0; }

private:
    void addSplitLines(BaseHud& host, float x, float y, float width, float height);
    void addRiderMarker(BaseHud& host, Reference reference, float x, float y, float width, float height);
    // Find the lap's two peaks (formatting a label only when its value changed)
    // and draw them, before the rider's marker.
    void findPeaks();
    void addPeakMarkers(BaseHud& host, float x, float y, float width, float height,
                        bool axisLabels, const ScaledDimensions& dims);
    void addTrace(BaseHud& host, float x, float y, float width, float height);
    // One lap of the profile: fills, then the line. `from`..`to` are points.
    void addSeries(BaseHud& host, const float* gapMs, const bool* has, int from, int to, float opacity,
                   float x, float y, float width, float height);
    // Pre-format the axis labels for m_scaleMs (only when it changes).
    void formatScaleLabels();

    LapDeltaProfile m_profile;
    int m_lastRef = -1;           // the reference of the last draw
    int m_scaleMs = 0;            // half-height of the plot: the round step above m_scalePeakMs
    int m_scalePeakMs = 0;        // the largest |gap| shown this lap; held, so the picture does
                                  // not rescale with every swing (as the Gap Bar's Auto range)
    char m_scaleTop[16] = {};     // axis labels for m_scaleMs: ahead at the top,
    char m_scaleBottom[16] = {};  // behind at the bottom
    int m_lastPoint = -1;         // the rider's profile point at the last draw
    bool m_lastLive = false;      // whether the gap was live at the last draw
    unsigned m_lastLapStamp = 0;  // PbGapTracker::lastLapStamp() at the last draw
    int m_plotted = 0;
    int m_plottedPrevious = 0;
    int m_draws = 0;
    // One peak: the profile point it is at (-1 = none) and its gap, with the
    // label pre-formatted for the value and time format it was formatted with.
    struct Peak {
        int point = -1;
        int gapMs = 0;
        int formattedMs = 0;
        bool formattedShort = false;
        bool formatted = false;
        int textLen = 0;
        char text[16] = {};
    };
    Peak m_peakAhead;              // most negative gap
    Peak m_peakBehind;             // most positive gap
    int m_peaksShown = 0;
    bool m_markerShown = false;
    size_t m_markerQuad = 0;       // its index in the host's quads, while shown
    float m_markerPos = -1.0f;     // the track position it was drawn at
    float m_plotX = 0.0f, m_plotY = 0.0f, m_plotW = 0.0f, m_plotH = 0.0f;   // the chart, as last laid out
};
