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

void SessionChartsHud::neighbourGaps(NeighbourGaps& out) const {
    out.ahead.clear();
    out.mine.clear();
    out.behind.clear();
    out.aheadNum = out.mineNum = out.behindNum = -1;
    FieldData field;
    collectField(field);
    out.pointsPerLap = field.pointsPerLap;
    const int displayRaceNum = PluginData::getInstance().getDisplayRaceNum();
    const size_t n = field.raceNums.size();
    for (size_t i = 0; i < n; ++i) {
        if (field.raceNums[i] != displayRaceNum) continue;
        out.mine = field.gaps[i];
        out.mineNum = field.raceNums[i];
        if (i > 0) {
            out.ahead = field.gaps[i - 1];
            out.aheadNum = field.raceNums[i - 1];
        }
        if (i + 1 < n) {
            out.behind = field.gaps[i + 1];
            out.behindNum = field.raceNums[i + 1];
        }
        break;
    }
}
