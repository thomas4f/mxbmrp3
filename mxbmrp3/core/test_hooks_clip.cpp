// ============================================================================
// core/test_hooks_clip.cpp
// The MXBMRP3_Test_* exports for CLIPPING (core/render_clip.h,
// core/hud_manager_clip.cpp): what each surface's frame last held, so a test
// can compare the engine's frame with the one our renderers cut. Pinned by
// tests/integration/tests/clip_marks_test.cpp.
//
// Same rules as core/test_hooks.cpp: gated on MXBMRP3_TEST_BUILD, and
// mxbmrp3/CMakeLists.txt removes this file from every shipping target.
// ============================================================================
#include "../game/game_config.h"

#if defined(MXBMRP3_TEST_BUILD)

#include <algorithm>
#include <cstdio>

#include "hud_manager.h"

extern "C" {

// Clip i of a surface's last frame: its ranges (q0, q1, s0, s1) and rect
// (l, t, r, b). Returns how many clips the frame had; i out of range fills nothing.
__declspec(dllexport) int MXBMRP3_Test_FrameClip(int companion, int i, unsigned* range, float* rect) {
    const auto& clips = HudManager::getInstance().frameClips(companion != 0);
    if (i >= 0 && static_cast<size_t>(i) < clips.size()) {
        const hudclip::Clip& c = clips[static_cast<size_t>(i)];
        if (range) { range[0] = c.q0; range[1] = c.q1; range[2] = c.s0; range[3] = c.s1; }
        if (rect) { rect[0] = c.l; rect[1] = c.t; rect[2] = c.r; rect[3] = c.b; }
    }
    return static_cast<int>(clips.size());
}

// String i of a surface's last frame: text, top, size and colour. Returns how
// many strings the frame had.
__declspec(dllexport) int MXBMRP3_Test_FrameString(int companion, int i, char* text, int cap, float* y,
                                                   float* size, unsigned* color) {
    const HudManager& hm = HudManager::getInstance();
    const auto& strings = companion ? hm.getCompanionStrings() : hm.getGameStrings();
    if (i >= 0 && static_cast<size_t>(i) < strings.size()) {
        const SPluginString_t& s = strings[static_cast<size_t>(i)];
        if (text && cap > 0) snprintf(text, static_cast<size_t>(cap), "%s", s.m_szString);
        if (y) *y = s.m_afPos[1];
        if (size) *size = s.m_fSize;
        if (color) *color = static_cast<unsigned>(s.m_ulColor);
    }
    return static_cast<int>(strings.size());
}

// Quad i's colour in a surface's last frame. Returns how many quads the frame had.
__declspec(dllexport) int MXBMRP3_Test_FrameQuadColor(int companion, int i, unsigned* color) {
    const HudManager& hm = HudManager::getInstance();
    const auto& quads = companion ? hm.getCompanionQuads() : hm.getGameQuads();
    if (color && i >= 0 && static_cast<size_t>(i) < quads.size())
        *color = static_cast<unsigned>(quads[static_cast<size_t>(i)].m_ulColor);
    return static_cast<int>(quads.size());
}

// Quad i's vertical extent in a surface's last frame. Returns how many quads
// the frame had.
__declspec(dllexport) int MXBMRP3_Test_FrameQuadY(int companion, int i, float* top, float* bot) {
    const HudManager& hm = HudManager::getInstance();
    const auto& quads = companion ? hm.getCompanionQuads() : hm.getGameQuads();
    if (top && bot && i >= 0 && static_cast<size_t>(i) < quads.size()) {
        const SPluginQuad_t& q = quads[static_cast<size_t>(i)];
        *top = std::min(std::min(q.m_aafPos[0][1], q.m_aafPos[1][1]), std::min(q.m_aafPos[2][1], q.m_aafPos[3][1]));
        *bot = std::max(std::max(q.m_aafPos[0][1], q.m_aafPos[1][1]), std::max(q.m_aafPos[2][1], q.m_aafPos[3][1]));
    }
    return static_cast<int>(quads.size());
}

}  // extern "C"

#endif  // MXBMRP3_TEST_BUILD
