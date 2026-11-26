// ============================================================================
// hud/gap_bar_hud_splits.cpp
// The Gap Bar's split ticks, drawn where PluginData says the splits are (the
// centerline's marker data, else learned from the crossings: split_learner.h).
// Split out of gap_bar_hud.cpp to keep it inside its file budget.
// ============================================================================
#include "gap_bar_hud.h"

#include <algorithm>

#include "../core/plugin_utils.h"
#include "../core/color_config.h"

void GapBarHud::renderSplitTicks(float boxY, float boxH, float innerX, float innerW) {
    float pos[SplitLearner::SLOTS];
    const int n = PluginData::getInstance().collectSplitPositions(pos);
    if (n == 0) return;
    // On the markers' scale (the inner rect), so the rider icon meets the tick
    // exactly as it crosses the split. An eighth of the bar at each edge.
    const float thickness = stripChartLineThickness();
    const unsigned long color = PluginUtils::applyOpacity(getColor(ColorSlot::SECONDARY), 0.8f);
    const float tick = boxH * 0.125f;
    for (int i = 0; i < n; ++i) {
        const float x = innerX + innerW * pos[i];
        addLineSegment(x, boxY, x, boxY + tick, color, thickness);
        addLineSegment(x, boxY + boxH - tick, x, boxY + boxH, color, thickness);
    }
}
