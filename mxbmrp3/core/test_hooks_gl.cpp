// ============================================================================
// core/test_hooks_gl.cpp
// The MXBMRP3_Test_* exports for the GL paths: the Phase 0 feasibility probe,
// the in-context renderer ([Advanced] glInGame) and its confirmation prompt.
// Split by family out of core/test_hooks.cpp when that file reached its size
// budget. Pinned by tests/integration/tests/gl_render_test.cpp and
// gl_probe_test.cpp.
//
// Same rules as core/test_hooks.cpp: gated on MXBMRP3_TEST_BUILD, and
// mxbmrp3/CMakeLists.txt removes this file from every shipping target.
// ============================================================================
#include "../game/game_config.h"

#if defined(MXBMRP3_TEST_BUILD)

#include "gl_probe.h"
#include "hud_manager.h"
#include "test_gl_render_probe.h"
#include "ui_config.h"
#include "../hud/gl_confirm_hud.h"

extern "C" {

// Phase 0 GL feasibility probe (core/gl_probe.h). Under Wine with no GL context
// current on the harness's Draw thread, these pin the inert path: the probe
// runs, finds no context, and the native handoff is untouched. That is real
// coverage of the branch a non-GL game would take, not a placeholder for the
// in-game answer -- which no headless test can reach.
// The GL render probe's body lives in core/test_gl_render_probe.cpp - it needs
// a synthetic frame, a GL readback and hudgl::Renderer, which is more than a
// registry file of thin exports should carry.
__declspec(dllexport) void MXBMRP3_Test_GlInGame(int on) {
    UiConfig::getInstance().setGlInGame(on != 0);
    HudManager::getInstance().clearGlFailLatch();
}

// The Direct GL confirmation prompt (hud/gl_confirm_hud.h). arm/cancel drive it,
// the two readers observe it. Exposed rather than driven through the settings
// click path because what needs pinning is the TIMER and the ENGINE ROUTING, not
// the mouse coordinates of a chip.
__declspec(dllexport) void MXBMRP3_Test_GlConfirmArm(int on) {
    GlConfirmHud& h = HudManager::getInstance().getGlConfirmHud();
    if (on) h.arm(); else h.cancel();
}

// The SETTING, not the backend: what the prompt's timeout has to turn off, and
// what must still be off after a restart.
__declspec(dllexport) int MXBMRP3_Test_GlInGameGet() {
    return UiConfig::getInstance().getGlInGame() ? 1 : 0;
}

__declspec(dllexport) int MXBMRP3_Test_GlConfirmActive() {
    return HudManager::getInstance().getGlConfirmHud().isActive() ? 1 : 0;
}

// Remaining time as a percentage, so a test can watch it fall without knowing
// the timeout. -1 when the prompt is not up.
__declspec(dllexport) int MXBMRP3_Test_GlConfirmRemainingPct() {
    const GlConfirmHud& h = HudManager::getInstance().getGlConfirmHud();
    if (!h.isActive()) return -1;
    return static_cast<int>(h.remainingFraction() * 100.0f);
}

// Advance the countdown by `ms` of DRAWN time, without needing that many real
// frames. The production path feeds this from the interval between GL-drawn
// frames; a test that had to render for twenty seconds to check the timeout
// would be a test nobody runs.
__declspec(dllexport) void MXBMRP3_Test_GlConfirmTick(int ms) {
    HudManager::getInstance().getGlConfirmHud().tickDrawn(static_cast<float>(ms) / 1000.0f);
}

__declspec(dllexport) int MXBMRP3_Test_GlDrewLastFrame() {
    return HudManager::getInstance().glDrewLastFrame() ? 1 : 0;
}

// 1 = the live GL backend has loaded this sprite (render name), 0 = not, -1 = no
// backend. See HudManager::testGlHasTexture.
__declspec(dllexport) int MXBMRP3_Test_GlHasTexture(const char* renderName) {
    return HudManager::getInstance().testGlHasTexture(renderName);
}

__declspec(dllexport) int MXBMRP3_Test_GlRenderProbe(int w, int h, int px, int py,
                                                     int scenario) {
    return mxbtest::glRenderProbe(w, h, px, py, scenario);
}

// kind: 0 = fonts, 1 = sprites; index is 0-based. See test_gl_render_probe.h.
__declspec(dllexport) int MXBMRP3_Test_GlFrameAssetName(int kind, int index,
                                                        char* out, int cap) {
    return mxbtest::glFrameAssetName(kind, index, out, cap);
}

__declspec(dllexport) int MXBMRP3_Test_GlFrameAssetCount(int kind) {
    return mxbtest::glFrameAssetCount(kind);
}

__declspec(dllexport) void MXBMRP3_Test_GlProbeConfig(int mode) {
    UiConfig::getInstance().setGlProbe(mode);
}

// The Phase 1 measurement load. Exposed so the batched client-vertex-array path
// is exercised against a real driver in CI-adjacent conditions rather than
// first running on Thomas's machine, where a fault would cost a game launch.
__declspec(dllexport) void MXBMRP3_Test_GlProbeLoad(int quads, int batch) {
    UiConfig::getInstance().setGlProbeQuads(quads);
    UiConfig::getInstance().setGlProbeBatch(batch);
}

// Packed so one hook serves every field without a struct crossing the DLL
// boundary (where a layout change is a silent skew, per the array-callback
// rule). Index order is fixed; unknown index -> 0.
__declspec(dllexport) int MXBMRP3_Test_GlProbeStatus(int field) {
    const GlProbe::Status st = GlProbe::status();
    switch (field) {
        case 0: return st.ran ? 1 : 0;
        case 1: return st.moduleResident ? 1 : 0;
        case 2: return st.entryPointsOk ? 1 : 0;
        case 3: return st.contextCurrent ? 1 : 0;
        case 4: return st.glVersion;
        case 5: return st.compatProfile ? 1 : 0;
        case 6: return st.drew ? 1 : 0;
        case 7: return st.readbackMatched ? 1 : 0;
        case 8: return st.stateDiffs;
        case 9: return st.glErrors;
        case 10: return st.drawGaps;
        case 11: return st.lastGapMs;
        case 12: return st.loadPainted;
        default: return 0;
    }
}

}  // extern "C"

#endif  // MXBMRP3_TEST_BUILD
