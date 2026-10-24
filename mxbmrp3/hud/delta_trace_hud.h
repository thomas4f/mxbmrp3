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
// Peaks (on by default): a dot with the gap beside it, as the Charts HUD marks
// its points, where the lap was furthest ahead and where it was furthest
// behind - the lap in progress while the gap is live, else the whole lap
// shown. Found at a rebuild (a few times a second), formatted only when a
// peak's value changes; a lap never more than a hair ahead (or behind) has no
// marker for that side.
//
// The chart is a DeltaTracePlot (delta_trace_plot.h), the same one the Dash's
// Delta Trace page draws.
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
#include "delta_trace_plot.h"
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
    int plottedPoints() const { return m_plot.plottedPoints(); }
    // Points of the previous lap drawn ahead of the rider (0 when none).
    int previousPoints() const { return m_plot.previousPoints(); }
    // Whether the rider's marker was drawn at the last rebuild.
    bool markerShown() const { return m_plot.markerShown(); }
    // Rebuilds so far (the marker's in-place move is not one).
    int rebuildCount() const { return m_plot.drawCount(); }
    void setShowPeaks(bool on) { m_showPeaks = on; setDataDirty(); }
    bool showsPeaks() const { return m_showPeaks; }
    // Peak markers drawn at the last rebuild, and their gaps (0 when that side has none).
    int peaksShown() const { return m_plot.peaksShown(); }
    int peakAheadMs() const { return m_plot.peakAheadMs(); }
    int peakBehindMs() const { return m_plot.peakBehindMs(); }

    friend class SettingsHud;
    friend class SettingsManager;

private:
    void rebuildRenderData() override;

    static constexpr float START_X = 0.0f;
    static constexpr float START_Y = 0.0f;
    // Telemetry's graph plus its values column, so the panel lines up with it.
    static constexpr int GRAPH_WIDTH_CHARS = 43;
    static constexpr int MIN_GRAPH_ROWS = 3;
    static constexpr int MAX_GRAPH_ROWS = 30;
    static constexpr int DEFAULT_GRAPH_ROWS = 6;

    Reference m_reference = Reference::SESSION_PB;   // when not Default
    bool m_referenceDefault = true;                  // follow General's reference
    int m_graphRows = DEFAULT_GRAPH_ROWS;
    bool m_showPeaks = true;

    // The chart itself, shared with the Dash's Delta Trace page (delta_trace_plot.h).
    DeltaTracePlot m_plot;
};
