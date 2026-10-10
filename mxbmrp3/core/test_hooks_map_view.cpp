// ============================================================================
// core/test_hooks_map_view.cpp
// The MXBMRP3_Test_* exports for the Map's views (map_view_test): set the
// View, and read back the track ribbon - how many quads are faded, how many
// sit outside the map's clip rect, and how wide the ribbon is in the map's top
// and bottom thirds (the tilt makes the far track narrower).
//
// Same rules as core/test_hooks.cpp: the whole file is gated on
// MXBMRP3_TEST_BUILD and mxbmrp3/CMakeLists.txt removes it from every shipping
// target's source list, so these exports cannot exist in a released DLL.
// ============================================================================
#include "../game/game_config.h"

#if defined(MXBMRP3_TEST_BUILD)

#include "hud_manager.h"
#include "plugin_constants.h"
#include "../hud/map_hud.h"

#include <algorithm>
#include <cmath>

// The ribbon cache's quads, against the clip rect of the last rebuild. A quad
// is faded when its opacity is below the most opaque ribbon quad's; outside
// when its centre lies past the clip rect (by more than float noise). width[0]
// and width[1] are the mean quad width (corner 0 to corner 3, across the
// ribbon) in the top and bottom thirds of the clip rect.
int MXBMRP3_Test_MapViewQuadsImpl(float* clip, int* faded, int* outside, float* width) {
    const MapHud& map = HudManager::getInstance().getMapHud();
    const float cl = map.m_fadeClip[0], ct = map.m_fadeClip[1];
    const float cr = map.m_fadeClip[2], cb = map.m_fadeClip[3];
    if (clip) { clip[0] = cl; clip[1] = ct; clip[2] = cr; clip[3] = cb; }
    unsigned long maxA = 0;
    for (const auto& q : map.m_ribbonQuads) maxA = std::max(maxA, (q.m_ulColor >> 24) & 0xFF);
    constexpr float EPS = 1e-4f;
    const float third = (cb - ct) / 3.0f;
    int f = 0, o = 0, n[2] = { 0, 0 };
    double w[2] = { 0.0, 0.0 };
    for (const auto& q : map.m_ribbonQuads) {
        if (((q.m_ulColor >> 24) & 0xFF) < maxA) ++f;
        float mx = 0.0f, my = 0.0f;
        for (int c = 0; c < 4; ++c) { mx += q.m_aafPos[c][0] * 0.25f; my += q.m_aafPos[c][1] * 0.25f; }
        if (mx < cl - EPS || mx > cr + EPS || my < ct - EPS || my > cb + EPS) ++o;
        const int band = my < ct + third ? 0 : (my > cb - third ? 1 : -1);
        if (band >= 0) {
            const float dx = (q.m_aafPos[0][0] - q.m_aafPos[3][0]) * PluginConstants::UI_ASPECT_RATIO;
            const float dy = q.m_aafPos[0][1] - q.m_aafPos[3][1];
            w[band] += std::sqrt(dx * dx + dy * dy);
            ++n[band];
        }
    }
    if (faded) *faded = f;
    if (outside) *outside = o;
    if (width) {
        width[0] = n[0] ? static_cast<float>(w[0] / n[0]) : 0.0f;
        width[1] = n[1] ? static_cast<float>(w[1] / n[1]) : 0.0f;
    }
    return static_cast<int>(map.m_ribbonQuads.size());
}

extern "C" {

// The Map's tilt in degrees (0 flat).
__declspec(dllexport) void MXBMRP3_Test_MapSetTilt(int degrees) {
    HudManager::getInstance().getMapHud().setTilt(degrees);
}
// Map > Adaptive range (0 off, 1 on).
__declspec(dllexport) void MXBMRP3_Test_MapSetAdaptiveRange(int on) {
    HudManager::getInstance().getMapHud().setAdaptiveRange(on != 0);
}
__declspec(dllexport) int MXBMRP3_Test_MapViewQuads(float* clip, int* faded, int* outside, float* width) {
    return MXBMRP3_Test_MapViewQuadsImpl(clip, faded, outside, width);
}

}  // extern "C"

#endif  // MXBMRP3_TEST_BUILD
