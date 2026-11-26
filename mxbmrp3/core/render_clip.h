// ============================================================================
// core/render_clip.h
// CLIPPING for the plugin's own renderers: Direct GL, the companion window's
// D3D11 backend and its software renderer. A HUD marks a range of its
// primitives with a rect; those renderers cut everything in the range at it.
//
// THE PROBLEM. The game's Draw export takes whole quads (no UVs) and whole
// strings, and draws them whole: nothing can be cut at a list's edge. So the
// settings lists fade a row out before it overhangs the edge, and the gear
// roll fades a digit out before it leaves its box. Our own renderers build
// every vertex themselves, so they can cut a row or a digit exactly where the
// box ends, the way any scrolling view on a desktop does.
//
// THE SHAPE. A HUD records marks in its own indices (hud/clip_marks.h).
// collectSurface maps them to the frame's indices on OUR surfaces only (the
// same surfaces that get HUD stacking, core/render_layers.h) and
// hands the renderers a sorted list of Clips. The engine's surface never sees
// a mark: its frame is the one the HUD built, byte for byte.
//
// WHY THE CUT IS ON THE CPU AND NOT A SCISSOR. Each scissor change splits a
// draw call in both GPU backends, and a clip is a handful of rows. Cutting an
// axis-aligned quad is four clamps and an affine UV fix-up, done once per
// primitive in the shared batcher, so both GPU backends cut identically and
// the batches coalesce exactly as before.
//
// A QUAD THAT IS NOT AXIS-ALIGNED (a needle, a rotated sprite at an odd
// angle) is not cut by the GPU backends: it draws whole when it touches the
// rect and is dropped when it lies wholly outside (the software renderer,
// which cuts per pixel, cuts it anyway). Every clipped range today holds only
// boxes and text, so this costs nothing; it is stated because it is the one
// thing the cut does not do.
//
// THE MARKS, what each surface draws, and which HUDs make them, are
// hud/clip_marks.h's.
//
// Pinned by tests/unit/test_render_clip.cpp (the cut, the UVs, the cursor),
// tests/unit/test_render_batch.cpp (a clipped frame's vertices) and
// tests/unit/test_hud_sw_renderer.cpp (the software renderer's pixels).
// ============================================================================
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace hudclip {

// One clipped range of the frame, [q0,q1) and [s0,s1), cut at a rect in
// normalized HUD coordinates.
struct Clip {
    uint32_t q0 = 0, q1 = 0;
    uint32_t s0 = 0, s1 = 0;
    float l = 0.0f, t = 0.0f, r = 0.0f, b = 0.0f;
};

// The clip covering primitive i, for a list sorted by start (collect order).
// The walk may revisit earlier indices (layers draw out of array order), so a
// step back restarts the search; within a range it is one compare.
class Cursor {
public:
    Cursor(const Clip* clips, int count) : m_clips(clips), m_count(clips ? count : 0) {}
    const Clip* quad(uint32_t i) { return find(i, &Clip::q0, &Clip::q1, m_q); }
    const Clip* string(uint32_t i) { return find(i, &Clip::s0, &Clip::s1, m_s); }

private:
    const Clip* find(uint32_t i, uint32_t Clip::*first, uint32_t Clip::*last, int& k) {
        if (m_count <= 0) return nullptr;
        if (k >= m_count || m_clips[k].*first > i) k = 0;
        while (k < m_count && m_clips[k].*last <= i) ++k;
        return (k < m_count && m_clips[k].*first <= i) ? &m_clips[k] : nullptr;
    }
    const Clip* m_clips;
    int m_count;
    int m_q = 0, m_s = 0;
};

// Cut a quad (corners TL,BL,BR,TR, as the game orders them) at [l,r] x [t,b],
// all in one coordinate space. uv may be null (an untextured fill). Returns
// false when nothing is left to draw. A box with its edges on the axes, in any
// orientation, is cut exactly, its UVs following the cut so the texture is
// cropped rather than squeezed; any other quad is kept whole if it touches the
// rect (see the header).
inline bool cutQuad(float p[4][2], float (*uv)[2], float l, float t, float r, float b) {
    float minX = p[0][0], maxX = p[0][0], minY = p[0][1], maxY = p[0][1];
    for (int c = 1; c < 4; ++c) {
        minX = std::min(minX, p[c][0]); maxX = std::max(maxX, p[c][0]);
        minY = std::min(minY, p[c][1]); maxY = std::max(maxY, p[c][1]);
    }
    if (maxX <= l || minX >= r || maxY <= t || minY >= b) return false;
    if (minX >= l && maxX <= r && minY >= t && maxY <= b) return true;   // wholly inside

    // Edges on the axes: each of TL->TR and TL->BL runs along x or along y.
    const float ex = std::max(maxX - minX, maxY - minY) * 1e-4f;
    const float ux = p[3][0] - p[0][0], uy = p[3][1] - p[0][1];   // TL -> TR
    const float vx = p[1][0] - p[0][0], vy = p[1][1] - p[0][1];   // TL -> BL
    const bool boxed = (std::fabs(uy) <= ex && std::fabs(vx) <= ex) ||
                       (std::fabs(ux) <= ex && std::fabs(vy) <= ex);
    const float det = ux * vy - uy * vx;
    if (!boxed || std::fabs(det) < 1e-12f) return true;

    // Corner c's (s,t) along TL->TR and TL->BL, so its UVs come from the
    // quad's own: uv = uv(TL) + s*(uv(TR)-uv(TL)) + t*(uv(BL)-uv(TL)).
    float uv0[2] = { 0.0f, 0.0f }, du[2] = { 0.0f, 0.0f }, dv[2] = { 0.0f, 0.0f };
    if (uv) {
        uv0[0] = uv[0][0]; uv0[1] = uv[0][1];
        du[0] = uv[3][0] - uv0[0]; du[1] = uv[3][1] - uv0[1];
        dv[0] = uv[1][0] - uv0[0]; dv[1] = uv[1][1] - uv0[1];
    }
    const float inv = 1.0f / det;
    const float ox = p[0][0], oy = p[0][1];
    for (int c = 0; c < 4; ++c) {
        p[c][0] = std::clamp(p[c][0], l, r);
        p[c][1] = std::clamp(p[c][1], t, b);
        if (!uv) continue;
        const float rx = p[c][0] - ox, ry = p[c][1] - oy;
        const float s = (rx * vy - ry * vx) * inv;
        const float tt = (ux * ry - uy * rx) * inv;
        uv[c][0] = uv0[0] + s * du[0] + tt * dv[0];
        uv[c][1] = uv0[1] + s * du[1] + tt * dv[1];
    }
    return true;
}

}  // namespace hudclip
