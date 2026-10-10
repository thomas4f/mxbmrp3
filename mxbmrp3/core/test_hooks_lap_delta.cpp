// ============================================================================
// core/test_hooks_lap_delta.cpp
// The MXBMRP3_Test_* exports for the lap delta displays: the Map's lap delta
// (its mode, and a count of the tinted track quads) and the Delta Trace HUD
// (points plotted, the previous lap, quad winding and the rider's marker).
//
// SPLIT OUT OF core/test_hooks.cpp, which is at its file budget.
// Same rules as its parent: the whole file is gated on MXBMRP3_TEST_BUILD and
// mxbmrp3/CMakeLists.txt removes it from every shipping target's source list,
// so these exports cannot exist in a released DLL.
// ============================================================================
#include "../game/game_config.h"

#if defined(MXBMRP3_TEST_BUILD)

#include "color_config.h"
#include "hud_manager.h"
#include "plugin_constants.h"
#include "../hud/delta_trace_hud.h"
#include "../hud/map_hud.h"

#include <algorithm>
#include <cstdlib>
#include <vector>

// MapHud's lap delta: the track quads tinted away from the fill, how many
// distinct colours they use, and how many lean green (gaining) and red (losing).
extern long long g_mapWorldRibbonBuilds;   // map_hud_track.cpp

int MXBMRP3_Test_MapLapDeltaQuadsImpl(int* distinct, int* gaining, int* losing) {
    const MapHud& map = HudManager::getInstance().getMapHud();
    const ColorConfig& cc = ColorConfig::getInstance();
    const unsigned long fill = cc.getColor(ColorSlot::BACKGROUND);
    const unsigned long outline = cc.getColor(ColorSlot::PRIMARY);
    const unsigned long gain = cc.getColor(ColorSlot::POSITIVE);
    const unsigned long loss = cc.getColor(ColorSlot::NEGATIVE);
    auto dist = [](unsigned long a, unsigned long b) {
        int d = 0;
        for (int sh = 0; sh < 24; sh += 8) d += std::abs(static_cast<int>((a >> sh) & 0xFF) - static_cast<int>((b >> sh) & 0xFF));
        return d;
    };
    std::vector<unsigned long> seen;
    int tinted = 0, g = 0, l = 0;
    for (const auto& q : map.m_ribbonQuads) {
        if (q.m_iSprite != PluginConstants::SpriteIndex::SOLID_COLOR) continue;
        const unsigned long c = q.m_ulColor;
        if (c == fill || c == outline) continue;
        const int dg = dist(c, gain), dl = dist(c, loss), df = dist(c, fill);
        if (dg >= df && dl >= df) continue;        // not a blend toward either
        ++tinted;
        if (dg < dl) ++g; else ++l;
        if (std::find(seen.begin(), seen.end(), c) == seen.end()) seen.push_back(c);
    }
    if (distinct) *distinct = static_cast<int>(seen.size());
    if (gaining) *gaining = g;
    if (losing) *losing = l;
    return tinted;
}

extern "C" {

// The Map's lap delta (map_lap_delta_test): set the mode (0 off, 1 session PB,
// 2 all-time PB, 3 last lap), then count the track quads tinted away from the
// fill, how many distinct colours they use, and how many lean green (gaining)
// and red (losing).
__declspec(dllexport) void MXBMRP3_Test_MapSetLapDelta(int mode) {
    HudManager::getInstance().getMapHud().setLapDelta(static_cast<MapHud::LapDelta>(mode));
}
__declspec(dllexport) int MXBMRP3_Test_MapLapDeltaQuads(int* distinct, int* gaining, int* losing) {
    return MXBMRP3_Test_MapLapDeltaQuadsImpl(distinct, gaining, losing);
}
// How many times the Map has re-walked its world ribbon (map_lap_delta_test).
__declspec(dllexport) long long MXBMRP3_Test_MapWorldRibbonBuilds() {
    return g_mapWorldRibbonBuilds;
}
// The Delta Trace's plotted points at its last rebuild (delta_trace_test).
__declspec(dllexport) int MXBMRP3_Test_DeltaTracePoints() {
    return HudManager::getInstance().getDeltaTraceHud().plottedPoints();
}
// The previous lap's points drawn ahead of the rider, and how many of the HUD's
// quads are wound against the game's order (it culls those; the companion
// window does not, so only this sees them).
// Also whether the rider's marker is drawn, its centre x (the last quad), and
// how many rebuilds the HUD has done (a marker move in place is not one).
__declspec(dllexport) void MXBMRP3_Test_DeltaTraceShape(int* previousPoints, int* reversedQuads, int* marker,
                                                        float* markerX, int* rebuilds) {
    const DeltaTraceHud& hud = HudManager::getInstance().getDeltaTraceHud();
    if (previousPoints) *previousPoints = hud.previousPoints();
    if (rebuilds) *rebuilds = hud.rebuildCount();
    if (marker) *marker = hud.markerShown() ? 1 : 0;
    if (markerX) {
        const auto& quads = hud.getQuads();
        *markerX = (hud.markerShown() && !quads.empty())
            ? (quads.back().m_aafPos[0][0] + quads.back().m_aafPos[2][0]) * 0.5f : -1.0f;
    }
    if (reversedQuads) {
        int reversed = 0;
        for (const SPluginQuad_t& q : hud.getQuads()) {
            // Shoelace sum; the game's order (TL, BL, BR, TR with y down) is negative.
            float area = 0.0f;
            for (int i = 0; i < 4; ++i) {
                const int j = (i + 1) % 4;
                area += q.m_aafPos[i][0] * q.m_aafPos[j][1] - q.m_aafPos[j][0] * q.m_aafPos[i][1];
            }
            if (area > 1e-9f) ++reversed;
        }
        *reversedQuads = reversed;
    }
}

}  // extern "C"

#endif  // MXBMRP3_TEST_BUILD
