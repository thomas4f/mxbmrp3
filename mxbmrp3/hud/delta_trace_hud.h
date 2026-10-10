// ============================================================================
// hud/delta_trace_hud.h
// Delta Trace: the gap to a reference lap across the WHOLE lap, not just at the
// rider's current position - a line running left to right as the lap
// progresses, filled green above zero (ahead) and red below (behind), so where
// the time was won and lost reads at a glance. The new lap draws over the
// previous one like a sweep: ahead of the rider, past a short gap, the lap just
// completed stays up (fainter) until it is overwritten, and in the pits all of
// it shows. Faint vertical lines mark the splits, and a square marks the rider.
// Riding back never erases what the lap has already drawn.
//
// READ-ONLY. It samples PluginData's PbGapTracker through LapDeltaProfile and
// computes each point's gap with the tracker's own gapAt(), so it is the Gap
// Bar's number drawn as a line rather than a second implementation of it. The
// Map's lap delta reads the same profile.
//
// Laid out like the Telemetry and Performance graphs - title, a strip chart
// whose height is a row count - but the chart spans the whole panel: the live
// gap as a number is the Gap Bar's and the Timing HUD's job.
// ============================================================================
#pragma once

#include "base_hud.h"
#include "../core/lap_delta_profile.h"
#include "hud_defaults.h"

class DeltaTraceHud : public BaseHud {
public:
    DeltaTraceHud();
    virtual ~DeltaTraceHud() = default;

    void update() override;
    bool handlesDataType(DataChangeType dataType) const override;
    const char* getIconName() const override { return "hud-deltatrace"; }
    void resetToDefaults();

    // Which lap the trace compares against - the Gap Bar's references, saved by
    // value like the Gap Bar's, or Default: General's (hud_defaults.h). A change
    // of the reference in use, either way, refits the scale (update()).
    using Reference = PbGapTracker::Ref;
    static constexpr int REFERENCE_COUNT = PbGapTracker::REF_COUNT;
    Reference getReference() const { return HudDefaults::reference(m_referenceDefault, m_reference); }

    // Points on the line at the last rebuild (0 = nothing plotted).
    int plottedPoints() const { return m_plotted; }
    // Points of the previous lap drawn ahead of the rider (0 when none).
    int previousPoints() const { return m_plottedPrevious; }
    // Whether the rider's marker was drawn at the last rebuild.
    bool markerShown() const { return m_markerShown; }
    // Rebuilds so far (the marker's in-place move is not one).
    int rebuildCount() const { return m_rebuilds; }

    friend class SettingsHud;
    friend class SettingsManager;

private:
    void rebuildRenderData() override;
    void addSplitLines(float x, float y, float width, float height);
    void addRiderMarker(float x, float y, float width, float height);
    void addTrace(float x, float y, float width, float height);
    // One lap of the profile: fills, then the line. `from`..`to` are points.
    void addSeries(const float* gapMs, const bool* has, int from, int to, float opacity,
                   float x, float y, float width, float height);
    // Pre-format the axis labels for m_scaleMs (only when it changes).
    void formatScaleLabels();

    static constexpr float START_X = 0.0f;
    static constexpr float START_Y = 0.0f;
    // Telemetry's graph plus its values column, so the panel lines up with it.
    static constexpr int GRAPH_WIDTH_CHARS = 43;
    static constexpr int MIN_GRAPH_ROWS = 3;
    static constexpr int MAX_GRAPH_ROWS = 30;
    static constexpr int DEFAULT_GRAPH_ROWS = 6;

    Reference m_reference = Reference::SESSION_PB;   // when not Default
    bool m_referenceDefault = true;                  // follow General's reference
    int m_lastRef = -1;                              // the reference of the last rebuild
    int m_graphRows = DEFAULT_GRAPH_ROWS;

    LapDeltaProfile m_profile;
    int m_scaleMs = 0;            // half-height of the plot: the round step above m_scalePeakMs
    int m_scalePeakMs = 0;        // the largest |gap| shown this lap; held, so the picture does
                                  // not rescale with every swing (as the Gap Bar's Auto range)
    char m_scaleTop[16] = {};     // axis labels for m_scaleMs: ahead at the top,
    char m_scaleBottom[16] = {};  // behind at the bottom
    int m_lastPoint = -1;         // the rider's profile point at the last rebuild
    bool m_lastLive = false;      // whether the gap was live at the last rebuild
    unsigned m_lastLapStamp = 0;  // PbGapTracker::lastLapStamp() at the last rebuild
    int m_plotted = 0;
    int m_plottedPrevious = 0;
    int m_rebuilds = 0;
    bool m_markerShown = false;    // and, when it is, it is the last quad
    float m_markerPos = -1.0f;     // the track position it was drawn at
    float m_plotX = 0.0f, m_plotY = 0.0f, m_plotW = 0.0f, m_plotH = 0.0f;   // the chart, as last laid out
};
