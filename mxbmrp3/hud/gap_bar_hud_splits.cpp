// ============================================================================
// hud/gap_bar_hud_splits.cpp
// The Gap Bar's split ticks: where the splits are (the centerline's marker data,
// else learned from the display rider's crossings) and the ticks drawn there.
// Split out of gap_bar_hud.cpp to keep it inside its file budget.
// ============================================================================
#include "gap_bar_hud.h"

#include <algorithm>

#include "../core/plugin_utils.h"
#include "../core/color_config.h"

static_assert(GapBarHud::SPLIT_SLOTS == Unified::MAX_SPLITS,
              "the gap bar keeps one slot per split the widest game has");

void GapBarHud::learnSplitPositions() {
    const PluginData& data = PluginData::getInstance();
    const CurrentLapData* lap = data.getCurrentLapData();
    if (!lap) return;
    // A split's time changes exactly when it is crossed (and resets to -1 at the
    // line), so a new positive value is the rider standing on that split now.
    const int splits[SPLIT_SLOTS] = { lap->split1, lap->split2, lap->split3 };
    const float pos = data.getDisplayRiderTrackPos();
    for (int i = 0; i < SPLIT_SLOTS; ++i) {
        if (splits[i] > 0 && splits[i] != m_learnSplitCache[i] && pos > 0.001f && pos < 0.999f) {
            m_learnedSplitPos[i] = pos;
            if (m_showSplits && isVisibleAnySurface()) setDataDirty();
        }
        m_learnSplitCache[i] = splits[i];
    }
}

int GapBarHud::collectSplitPositions(float (&out)[SPLIT_SLOTS]) const {
    int n = 0;
    // The centerline's first: exact, and there from the first frame on the track.
    // Entry 0 is the start/finish line itself (0 on this bar), not a split.
    const std::vector<float>& official = PluginData::getInstance().getSplitPositions();
    for (size_t i = 1; i < official.size() && n < SPLIT_SLOTS; ++i) {
        if (official[i] > 0.001f && official[i] < 0.999f) out[n++] = official[i];
    }
    if (n == 0) {
        for (int i = 0; i < SPLIT_SLOTS; ++i) {
            if (m_learnedSplitPos[i] > 0.0f) out[n++] = m_learnedSplitPos[i];
        }
    }
    std::sort(out, out + n);
    return n;
}

void GapBarHud::renderSplitTicks(float boxY, float boxH, float innerX, float innerW) {
    float pos[SPLIT_SLOTS];
    const int n = collectSplitPositions(pos);
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
