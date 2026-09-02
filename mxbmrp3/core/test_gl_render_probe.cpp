// ============================================================================
// core/test_gl_render_probe.cpp
// Body of the GL render probe - see test_gl_render_probe.h for why it is not
// in test_hooks.cpp. Moved verbatim; only the export became a plain function.
// ============================================================================
#include "test_gl_render_probe.h"

#if defined(MXBMRP3_TEST_BUILD)

#include "hud_gl_renderer.h"
#include "hud_manager.h"
#include "hud_sw_renderer.h"
#include "render_asset_decode.h"
#include "../game/game_config.h"

#include <windows.h>
#include <cstring>
#include <string>
#include <vector>

namespace mxbtest {

namespace {
// One pixel out of the current GL framebuffer, in TOP-DOWN coordinates so the
// test can name positions the same way the HUD does. Returns 0xRRGGBBAA.
int glReadPixelTopDown(int x, int yTop, int h) {
    HMODULE gl = GetModuleHandleA("opengl32.dll");
    if (!gl) return -1;
    auto readPixels = reinterpret_cast<void (WINAPI*)(int,int,int,int,unsigned,unsigned,void*)>(
        reinterpret_cast<void*>(GetProcAddress(gl, "glReadPixels")));
    auto pixelStore = reinterpret_cast<void (WINAPI*)(unsigned,int)>(
        reinterpret_cast<void*>(GetProcAddress(gl, "glPixelStorei")));
    if (!readPixels) return -1;
    if (pixelStore) pixelStore(0x0D05 /*GL_PACK_ALIGNMENT*/, 1);
    unsigned char p[4] = { 0, 0, 0, 0 };
    readPixels(x, h - 1 - yTop, 1, 1, 0x1908 /*GL_RGBA*/, 0x1401 /*GL_UNSIGNED_BYTE*/, p);
    return (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
}
}  // namespace

int glFrameAssetCount(int kind) {
    const HudManager& hm = HudManager::getInstance();
    return static_cast<int>((kind == 0 ? hm.testGlFrameFontNames()
                                       : hm.testGlFrameSpriteNames()).size());
}

int glFrameAssetName(int kind, int index, char* out, int cap) {
    if (!out || cap <= 0) return -1;
    const HudManager& hm = HudManager::getInstance();
    const std::vector<std::string>& names =
        kind == 0 ? hm.testGlFrameFontNames() : hm.testGlFrameSpriteNames();
    if (index < 0 || index >= static_cast<int>(names.size())) return -1;
    const std::string& n = names[static_cast<size_t>(index)];
    const int len = static_cast<int>(n.size());
    if (len + 1 > cap) return -1;
    memcpy(out, n.c_str(), static_cast<size_t>(len) + 1);
    return len;
}

// thread and read one pixel back. The harness makes that context (see
// PluginHost::glMakeContext), so gl_render_test.cpp can assert the backend's
// actual rendered OUTPUT - colour, position, z-order, text - which is coverage
// no GPU backend in this repo has ever had.
//
// scenario: 0 = one white-through-colour quad covering the left half;
//           1 = two overlapping quads, second on top (z-order);
//           2 = a string;
//           3 = a NESTED PACK SPRITE, the case a hand-rolled path join missed.
// Returns packed 0xRRGGBBAA, or -1 if the render failed.
int glRenderProbe(int wIgnored, int hIgnored, int pctX, int pctY, int scenario) {
    // The render must match the framebuffer it lands in, so the size comes from
    // the CURRENT GL viewport rather than from the caller - a mismatch renders
    // to one coordinate space and reads back from another, which is exactly the
    // false "the quad bled past its edge" this test first produced. px/py are
    // PERCENTAGES so the test never has to know the harness window's size.
    (void)wIgnored; (void)hIgnored;
    int vp[4] = { 0, 0, 0, 0 };
    if (HMODULE glLib = GetModuleHandleA("opengl32.dll")) {
        if (auto getIv = reinterpret_cast<void (WINAPI*)(unsigned, int*)>(
                reinterpret_cast<void*>(GetProcAddress(glLib, "glGetIntegerv"))))
            getIv(0x0BA2 /*GL_VIEWPORT*/, vp);
    }
    const int w = vp[2] > 0 ? vp[2] : 320;
    const int h = vp[3] > 0 ? vp[3] : 240;
    const int px = pctX * w / 100;
    const int py = pctY * h / 100;
    static hudgl::Renderer* r = nullptr;
    if (!r) { r = new hudgl::Renderer(); if (!r->init()) return -1; }
    if (!r->ok()) return -1;

    std::vector<SPluginQuad_t> quads;
    std::vector<SPluginString_t> strings;
    auto quad = [&](float x0, float y0, float x1, float y1, unsigned long c) {
        SPluginQuad_t q{};
        q.m_iSprite = 0; q.m_ulColor = c;
        q.m_aafPos[0][0]=x0; q.m_aafPos[0][1]=y0; q.m_aafPos[1][0]=x0; q.m_aafPos[1][1]=y1;
        q.m_aafPos[2][0]=x1; q.m_aafPos[2][1]=y1; q.m_aafPos[3][0]=x1; q.m_aafPos[3][1]=y0;
        quads.push_back(q);
    };
    if (scenario == 0) {
        quad(0.0f, 0.0f, 0.5f, 1.0f, 0xFF0000FFul);          // ABGR: opaque red
    } else if (scenario == 1) {
        quad(0.0f, 0.0f, 1.0f, 1.0f, 0xFF0000FFul);          // red underneath
        quad(0.0f, 0.0f, 1.0f, 1.0f, 0xFFFF0000ul);          // opaque blue on top
    } else if (scenario == 3) {
        // Sprite 1 is the nested pack asset named below; full screen so any
        // sample point lands on it.
        SPluginQuad_t q{};
        q.m_iSprite = 1; q.m_ulColor = 0xFFFFFFFFul;         // white: texel passes through
        q.m_aafPos[0][0]=0.0f; q.m_aafPos[0][1]=0.0f; q.m_aafPos[1][0]=0.0f; q.m_aafPos[1][1]=1.0f;
        q.m_aafPos[2][0]=1.0f; q.m_aafPos[2][1]=1.0f; q.m_aafPos[3][0]=1.0f; q.m_aafPos[3][1]=0.0f;
        quads.push_back(q);
    } else {
        SPluginString_t st{};
        strncpy_s(st.m_szString, sizeof(st.m_szString), "III", _TRUNCATE);
        st.m_afPos[0] = 0.0f; st.m_afPos[1] = 0.0f;
        st.m_iFont = 1; st.m_fSize = 0.9f; st.m_iJustify = 0;
        st.m_ulColor = 0xFF00FF00ul;                          // opaque green
        strings.push_back(st);
    }

    hudsw::Frame f;
    f.quads = quads.empty() ? nullptr : quads.data();
    f.quadCount = static_cast<int>(quads.size());
    f.strings = strings.empty() ? nullptr : strings.data();
    f.stringCount = static_cast<int>(strings.size());
    // A shipped font, named directly rather than read out of HudManager's
    // private registration table: this hook only needs SOME real .fnt to prove
    // the GL_ALPHA/MODULATE text path, and widening HudManager's API for a test
    // would be the wrong trade.
    //
    // The sprite is a NESTED pack asset, and that is the whole point of it being
    // here: a nested render name keeps its relative path ("gauges/classic/tacho")
    // and resolves against the root, where a flat one gets /textures/ or /icons/.
    // Every other scenario draws untextured quads through the 1x1 white texture,
    // so none of them touches path resolution at all - which is how a join that
    // only implemented the flat half shipped, and took every pack's art with it.
    static const std::vector<std::string> kFonts{ "IBMPlexMono-Regular" };
    static const std::vector<std::string> kSprites{ "gauges/classic/tacho" };
    f.fontNames = &kFonts;
    f.spriteNames = &kSprites;
    f.firstIcon = 1 << 30;
    // plugins/mxbmrp3_data is what the plugin uses at runtime, but the harness
    // syncs only user-overridable asset types into it - no fonts. Fall back to
    // the repo's shipped data so the text path is actually exercised.
    // Root chosen against the asset THIS scenario needs, not against the font.
    // plugins/mxbmrp3_data is what the plugin uses at runtime, but the harness
    // syncs only user-overridable types into it - a test may have staged a font
    // there while no pack exists, so probing with the font would pick a root that
    // has no gauges/ at all and report the nested sprite as unpainted for a
    // reason that has nothing to do with path resolution.
    const std::string need = (scenario == 3)
        ? hudassets::spritePath("", kSprites[0], false)
        : hudassets::fntPath("", kFonts[0]);
    f.assetRoot = "plugins/mxbmrp3_data";
    if (!hudsw::readFile(f.assetRoot + need).size())
        f.assetRoot = MXB_REPO_DATA_DIR;
    if (!r->render(f, w, h, 0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h)))
        return -1;
    return glReadPixelTopDown(px, py, h);
}

}  // namespace mxbtest

#endif
