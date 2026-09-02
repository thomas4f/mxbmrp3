// ============================================================================
// core/render_batch.cpp
// Moved VERBATIM out of hud_gpu_renderer.cpp's Impl::buildBatch. The only
// changes are mechanical: D3D resource pointers become opaque handles, the
// pixel-shader pointer becomes a Shader enum, and the cache lookups become
// Resolver calls. No arithmetic, no ordering and no early-out was altered, so
// the D3D backend's output is unchanged by the move - which is what makes the
// extraction reviewable by inspection, the same discipline the
// render_asset_decode extraction used.
// ============================================================================
#include "render_batch.h"

namespace hudbatch {

void build(const hudsw::Frame& frame, int w, int h,
           float vx, float vy, float vw, float vh,
           Resolver& res, std::vector<Vertex>& verts, std::vector<Run>& runs) {
    verts.clear();
    runs.clear();
    const float sw = 2.0f / w, sh = 2.0f / h;
    auto ndcX = [&](float px) { return px * sw - 1.0f; };
    auto ndcY = [&](float py) { return 1.0f - py * sh; };
    auto mapX = [&](float n) { return vx + n * vw; };
    auto mapY = [&](float n) { return vy + n * vh; };

    auto beginRun = [&](const void* tex, Shader sh2) {
        if (runs.empty() || runs.back().tex != tex || runs.back().shader != sh2)
            runs.push_back(Run{ tex, sh2, static_cast<uint32_t>(verts.size()), 0 });
    };
    auto emitQuad = [&](const float p[4][2], const float uv[4][2], uint32_t rgba) {
        // Corners arrive TL,BL,BR,TR (the game's order); two triangles.
        static const int tri[6] = { 0, 1, 2, 0, 2, 3 };
        for (int i = 0; i < 6; ++i) {
            const int c = tri[i];
            verts.push_back(Vertex{ ndcX(p[c][0]), ndcY(p[c][1]), uv[c][0], uv[c][1], rgba });
        }
        runs.back().count += 6;
    };

    // Quads, in submission order (z-order), matching hud_sw_renderer::render.
    for (int i = 0; i < frame.quadCount; ++i) {
        const SPluginQuad_t& q = frame.quads[i];
        float p[4][2];
        for (int c = 0; c < 4; ++c) { p[c][0] = mapX(q.m_aafPos[c][0]); p[c][1] = mapY(q.m_aafPos[c][1]); }
        const uint32_t rgba = static_cast<uint32_t>(q.m_ulColor);
        if (q.m_iSprite == 0) {
            static const float uvZero[4][2] = { {0,0}, {0,0}, {0,0}, {0,0} };
            beginRun(res.white(), Shader::Sprite);
            emitQuad(p, uvZero, rgba);
            continue;
        }
        const auto& names = *frame.spriteNames;
        int idx = q.m_iSprite - 1;
        if (idx < 0 || idx >= static_cast<int>(names.size())) continue;
        const void* t = res.texture(names[idx], q.m_iSprite >= frame.firstIcon, frame.assetRoot);
        if (!t) continue;
        // U runs TL->TR, V runs TL->BL — the same basis the software affine
        // blit derives; the GPU interpolates it (and bilinear-samples, which
        // the point-sampled software blit does not).
        static const float uvFull[4][2] = { {0,0}, {0,1}, {1,1}, {1,0} };
        beginRun(t, Shader::Sprite);
        emitQuad(p, uvFull, rgba);
    }

    // Strings after quads, matching the software path's draw order.
    for (int i = 0; i < frame.stringCount; ++i) {
        const SPluginString_t& s = frame.strings[i];
        if (s.m_szString[0] == '\0') continue;
        int idx = s.m_iFont - 1;
        if (idx < 0 || idx >= static_cast<int>(frame.fontNames->size())) continue;
        const hudassets::FntFont* fp = nullptr;
        const void* fh = res.font((*frame.fontNames)[idx], frame.assetRoot, &fp);
        if (!fh || !fp) continue;
        const hudassets::FntFont& f = *fp;
        const float scale = s.m_fSize * vh / static_cast<float>(f.cellH);
        float penX = mapX(s.m_afPos[0]);
        const float total = hudassets::measureFnt(f, s.m_szString, scale);
        if (s.m_iJustify == 1) penX -= total / 2; else if (s.m_iJustify == 2) penX -= total;
        const float top = mapY(s.m_afPos[1]);
        const uint32_t rgba = static_cast<uint32_t>(s.m_ulColor);
        const float iw = 1.0f / f.aw, ih = 1.0f / f.ah;
        beginRun(fh, Shader::Text);
        hudassets::layoutFnt(f, s.m_szString, penX, top, scale,
                             [&](const hudassets::GlyphQuad& g) {
            const float p[4][2] = { { g.dx0, g.dy0 }, { g.dx0, g.dy1 },
                                    { g.dx1, g.dy1 }, { g.dx1, g.dy0 } };
            const float uv[4][2] = { { g.ax0 * iw, g.ay0 * ih }, { g.ax0 * iw, g.ay1 * ih },
                                     { g.ax1 * iw, g.ay1 * ih }, { g.ax1 * iw, g.ay0 * ih } };
            emitQuad(p, uv, rgba);
        });
    }
}

}  // namespace hudbatch
