// ============================================================================
// hud/session_charts_hud_series.cpp
// Session Charts HUD - its series handed to others: the test view and the
// Dash's Gap chart page. Split from session_charts_hud.cpp for its line budget.
// ============================================================================
#include "session_charts_hud.h"
#include "../core/plugin_data.h"

SessionChartsHud::TestSeries SessionChartsHud::testSeries() const {
    FieldData field;
    collectField(field);
    TestSeries out;
    out.pointsPerLap = field.pointsPerLap;
    const int displayRaceNum = PluginData::getInstance().getDisplayRaceNum();
    for (size_t i = 0; i < field.raceNums.size(); ++i) {
        if (field.raceNums[i] == displayRaceNum) {
            out.points = static_cast<int>(field.cumulative[i].size());
            break;
        }
    }
    return out;
}

void SessionChartsHud::gapChartLines(GapChartLines& out) const {
    // Each line's vector keeps its capacity across rebuilds (the Dash holds one
    // GapChartLines), so a redraw with the same riders allocates nothing new.
    for (GapChartLine& l : out.lines) {
        l.gaps.clear();
        l.raceNum = -1;
    }
    out.count = 0;
    FieldData field;
    collectField(field);
    out.pointsPerLap = field.pointsPerLap;
    GapChartPicks::Pick picks[GapChartPicks::MAX_LINES];
    const int count = GapChartPicks::pick(field.raceNums.data(), field.raceNums.size(),
                                          PluginData::getInstance().getDisplayRaceNum(), picks);
    for (int k = 0; k < count; ++k) {
        const size_t i = static_cast<size_t>(picks[k].index);
        GapChartLine& l = out.lines[k];
        l.gaps = field.gaps[i];
        l.raceNum = field.raceNums[i];
        l.role = picks[k].role;
    }
    out.count = count;
}
