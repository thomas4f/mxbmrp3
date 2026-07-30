// ============================================================================
// hud/session_charts_hud.h
// Session Charts HUD - race-progression analysis charts (position / trace / gap /
// pace). A single configurable HUD, modeled on TelemetryHud/PerformanceHud,
// with a chart-type selector that switches between four views of one underlying
// data set: each rider's per-lap lap time (PluginData::m_riderLapLog).
//
//   Lap chart   - track position per lap (overtakes)
//   Race trace  - cumulative time vs a fixed reference pace (real gaps + pace)
//   Gap chart   - seconds behind the current leader per lap
//   Pace chart  - raw lap time per lap (drop-off / mistakes)
//
// All derivations (cumulative time, position, gap, reference pace) live in the
// dependency-free header session_charts_math.h so the logic is unit-tested
// headlessly; this class is the rendering adapter.
// ============================================================================
#pragma once

#include "base_hud.h"
#include "../core/plugin_constants.h"
#include "../core/widget_constants.h"
#include "session_charts_math.h"
#include <vector>
#include <cstdint>

class SessionChartsHud : public BaseHud {
public:
    SessionChartsHud();
    virtual ~SessionChartsHud() = default;

    void update() override;
    bool handlesDataType(DataChangeType dataType) const override;
    const char* getIconName() const override { return "hud-sessioncharts"; }
    void resetToDefaults();

    // Click-to-spectate on a rider's inline "#num" tag, matching Standings/Map/Event Log.
    // The tag is the only rider-identifying element on a chart; the lines overlap too much
    // to hit-test. Gated on PluginData::isRiderSpectatable, so a retired rider keeps their
    // label but it does nothing.
    void handleClick(float mouseX, float mouseY);

    // Test-only view of what collectField() settled on. The charts are render-only (no
    // /api/state), and the two things worth pinning are decisions, not pixels: the
    // resolution actually used (sector points are races-only, and fall back field-wide when
    // any rider's sector data has a hole) and how many samples the display rider's series
    // holds — which is what shows the live in-progress lap extending the line mid-lap.
    struct TestSeries { int pointsPerLap = 0; int points = 0; };
    TestSeries testSeries() const;

    // Set which charts are shown (bitmask of ChartFlags). Used by test hooks; the
    // settings tab toggles m_enabledCharts bits directly.
    void setEnabledCharts(uint32_t flags) { m_enabledCharts = flags; setDataDirty(); }

    // Optional chart elements (ElementFlags). Used by test hooks; the settings tab toggles
    // m_enabledElements bits directly.
    uint32_t getEnabledElements() const { return m_enabledElements; }
    void setEnabledElements(uint32_t flags) { m_enabledElements = flags; setDataDirty(); }

    // Chart kinds. Each is an independent checkbox (ChartFlags); any combination can
    // be shown, stacked vertically.
    enum class ChartType : uint8_t {
        LAP   = 0,   // position per lap
        TRACE = 1,   // cumulative time vs reference pace (race only)
        GAP   = 2,   // gap to leader (race) / to session-best lap (non-race)
        PACE  = 3,   // lap time per lap
        CHART_COUNT = 4
    };

    // Which charts are enabled (bitmask). Persisted as an int — don't renumber.
    enum ChartFlags : uint32_t {
        CHART_LAP   = 1 << 0,
        CHART_TRACE = 1 << 1,
        CHART_GAP   = 1 << 2,
        CHART_PACE  = 1 << 3,
        // Default: Lap (order flow) + Pace (lap times) — a compact two-chart view
        // that's meaningful in any session. Trace and Gap are opt-in.
        CHART_DEFAULT = CHART_LAP | CHART_PACE,
        CHART_ALLFLAGS = CHART_LAP | CHART_TRACE | CHART_GAP | CHART_PACE
    };

    // Optional chart elements (bitmask, TelemetryHud::ElementFlags pattern).
    enum ElementFlags : uint32_t {
        ELEM_GRID            = 1 << 0,  // horizontal grid lines
        ELEM_AXIS_LABELS     = 1 << 1,  // Y-axis + lap-number labels
        ELEM_LEGEND          = 1 << 2,  // inline "#num" tag at the end of each rider's line
        ELEM_ZERO_LINE       = 1 << 3,  // dashed reference line (trace chart)
        ELEM_DOTS            = 1 << 4,  // a dot at each lap point
        ELEM_FILTER_OUTLIERS = 1 << 5,  // pace chart: drop opening/slow laps
        // Sample the position/trace/gap charts once per SECTOR instead of once per lap.
        // Every rider's sector times are already known (RaceLap carries the whole lap's
        // splits; RaceSplit arrives live mid-lap), so this is 3-4x the resolution from data
        // the plugin already holds — it turns "the gap grew on lap 7" into "the gap grew in
        // sector 2". Off by default: it is a density choice, and a busy grid is already
        // dense at one point per lap (ELEM_DOTS is opt-in for the same reason).
        ELEM_SECTOR_POINTS   = 1 << 6,

        ELEM_DEFAULT = ELEM_GRID | ELEM_AXIS_LABELS | ELEM_LEGEND | ELEM_ZERO_LINE | ELEM_FILTER_OUTLIERS,
        ELEM_COUNT   = 7
    };

    // How rider lines are coloured. Persisted as an int — don't renumber.
    enum class RiderColorMode : uint8_t {
        POSITION_PALETTE = 0,  // distinct hue per classification position
        BRAND            = 1,  // bike brand color, lightened/darkened per same-brand rider
        COLOR_MODE_COUNT = 2
    };

    static constexpr int MIN_TOP_COUNT = 0;    // 0 = no pinned leaders
    static constexpr int MAX_TOP_COUNT = 10;
    static constexpr int MIN_ROW_COUNT = 2;    // minimum lines drawn
    static constexpr int MAX_ROW_COUNT = 16;   // matches palette size / readability

    // Allow SettingsHud and SettingsManager to access private members
    friend class SettingsHud;
    friend class SettingsManager;

protected:
    void rebuildLayout() override { rebuildRenderData(); }

private:
    void rebuildRenderData() override;

    // Full-field lap-time table and everything derived from it (positions and
    // gaps must be computed over the WHOLE field, not just the drawn subset).
    struct FieldData {
        std::vector<int> raceNums;                       // classification order
        std::vector<std::vector<int>> lapMs;             // per rider, oldest-first
        std::vector<std::vector<char>> lapValid;         // per rider per lap (1=valid); parallel to lapMs
        std::vector<std::vector<long long>> cumulative;  // per rider per lap (for trace)
        std::vector<std::vector<int>> positions;         // per rider per lap (1-based)
        std::vector<std::vector<long long>> gaps;        // per rider per lap (ms behind reference)
        long long refPaceMs = 0;                         // leader avg lap (trace baseline)
        int maxLap = 0;                                  // max completed laps in field
        bool isRace = false;                             // race vs practice/qualifying

        // Sample resolution of cumulative/positions/gaps: 1 = one point per completed lap,
        // GAME_SECTOR_COUNT = one per sector (ELEM_SECTOR_POINTS). lapMs/lapValid stay
        // per-lap either way — the pace chart plots lap TIMES, which have no sector analogue
        // on the same axis.
        //
        // Field-wide, never per rider: positionsPerLap()/gapToLeaderPerLap() rank riders by
        // comparing the same ARRAY INDEX across the field, so mixing resolutions would
        // compare one rider's sector against another's lap.
        int pointsPerLap = 1;
        // X-axis domain end, in laps completed. A float because at sector resolution the
        // furthest-along rider is usually part way through a lap (e.g. 3.33), and clamping
        // that back to maxLap would stack their last points on the right edge.
        float maxLaps = 0.0f;

        // Laps completed at a series index, and at the earliest sample the series can hold.
        // With pointsPerLap == 1 these are index+1 and 1.0 — the per-lap convention.
        float lapsAt(int index) const {
            return SessionChartsMath::lapsAtSectorIndex(index, pointsPerLap);
        }
        float firstLaps() const { return lapsAt(0); }
    };

    // Spectate click targets, one per drawn "#num" tag (so a rider enabled on several
    // charts gets several — all naming the same rider, so a hit is unambiguous).
    struct TagClickRegion {
        float x, y, width, height;
        int raceNum;
    };
    std::vector<TagClickRegion> m_tagClickRegions;

    // A rider selected for drawing (index into FieldData arrays + presentation).
    struct DrawnRider {
        int fieldIdx = 0;
        unsigned long color = 0;
        bool isPlayer = false;
    };

    // One rider's sector times, oldest-first, flattened with a GAME_SECTOR_COUNT stride:
    // every completed lap from the lap log, then the sectors of the lap in progress.
    void collectSectorTimes(int raceNum, std::vector<int>& out) const;

    void collectField(FieldData& field) const;
    void selectDrawn(const FieldData& field, std::vector<DrawnRider>& drawn) const;

    // Draw one chart into the cell rect (x,y,w,h). Reserves room inside the cell
    // for axis labels, then dispatches to the per-type renderer. The frame (grid +
    // axes) is always drawn — lines fill in as laps arrive, no "waiting" message.
    void drawChart(ChartType type, float x, float y, float w, float h,
                   const FieldData&, const std::vector<DrawnRider>&);

    // Chart renderers operate on the inner plotting rectangle.
    void drawLapChart  (float px, float py, float pw, float ph, const FieldData&, const std::vector<DrawnRider>&);
    void drawTraceChart(float px, float py, float pw, float ph, const FieldData&, const std::vector<DrawnRider>&);
    void drawGapChart  (float px, float py, float pw, float ph, const FieldData&, const std::vector<DrawnRider>&);
    void drawPaceChart (float px, float py, float pw, float ph, const FieldData&, const std::vector<DrawnRider>&);
    void drawRaceOnlyNote(float x, float y, float w, float h);

    // Draw a rider's "#num" tag in small font at (x,y) — the end of its line — in
    // the rider's colour, so lines are labelled inline instead of via a legend
    // column. Always at the line's endpoint (line and label stay vertically aligned);
    // on dense charts tags may overlap. The Lap chart's bump-chart Y keeps each line
    // on its own row, so its tags never overlap. Gated on ELEM_LEGEND.
    void addRiderTag(float x, float y, int raceNum, unsigned long color);

    // The axis-label set every chart shares: the Y-range pair right-justified
    // against the plot's left edge (top value at py, bottom value at the last
    // small-text row), then the X-range pair below the plot ("L1" left, "L<max>"
    // right — the latter only when there is more than one lap). Pass nullptr for
    // topLabel/botLabel to skip the Y pair (charts suppress it until data exists).
    // domainEndLaps MUST be the same value the chart placed its points with — the label is
    // ceil() of it. Passing the field domain to a chart that plots per-lap (pace) put "L4"
    // under a chart whose last point is lap 3.
    void addChartAxisLabels(float px, float py, float pw, float ph, float domainEndLaps,
                            const char* topLabel, const char* botLabel,
                            const ScaledDimensions& dims);

    // X position for a 0-based lap index across the plotting width. Kept for the per-lap
    // charts (pace) and expressed through the shared fractional-lap mapping, which reduces
    // to the original lapIndex0/(maxLap-1) — see xFracForLaps.
    float xForLap(float px, float pw, int lapIndex0, float domainEndLaps) const {
        return px + SessionChartsMath::xFracForLaps(static_cast<float>(lapIndex0 + 1), 1.0f,
                                                    domainEndLaps) * pw;
    }

    // X position for an index into the resolution-aware series (cumulative/positions/gaps).
    // Identical to xForLap when the field is at per-lap resolution.
    float xForPoint(float px, float pw, int index, const FieldData& field) const {
        return px + SessionChartsMath::xFracForLaps(field.lapsAt(index), field.firstLaps(),
                                                    field.maxLaps) * pw;
    }

    static constexpr float START_X = 0.0f;
    static constexpr float START_Y = 0.0f;
    static constexpr int GRAPH_WIDTH_CHARS = 43;   // match Telemetry/Performance outer width (33+1+9)
    static constexpr int TAG_WIDTH_CHARS = 5;      // right margin reserved for the "#999" line tag
    // Every chart is 10 rows tall, so a single-chart HUD is title(2) + subheading(1)
    // + 10 + padding(2) = 15 rows (matching the StandingsHud default), and the HUD
    // grows taller as charts are added (each adds a subheading + 10 rows). Multi-chart
    // stacks can exceed the screen — position/scale is left to the user.
    static constexpr float GRAPH_HEIGHT_LINES = 10.0f;

    // Configuration (saved to INI)
    uint32_t m_enabledCharts = CHART_DEFAULT;   // which charts are shown (checkboxes)
    uint32_t m_enabledElements = ELEM_DEFAULT;
    RiderColorMode m_riderColorMode = RiderColorMode::POSITION_PALETTE;
    int m_topPositionsCount = 3;   // top-N pinned leaders (StandingsHud style)
    int m_displayRowCount = 10;    // total rider lines drawn (top-N + player window); ~fills a 10-row chart's tags
    // Last seen SessionData::sessionGeneration. The charts do NOT subscribe to
    // DataChangeType::SessionData (it doubles as the 1 Hz session-clock heartbeat, which
    // would recompute the whole session once a second); update() compares this instead so
    // only a real session transition rebuilds. Same pattern as TimingHud / GapBarHud.
    int m_cachedSessionGeneration = -1;

    // Advanced tuning (INI-only, not in the UI): pace-chart outlier threshold.
    float m_outlierFactor = 1.4f;
};
