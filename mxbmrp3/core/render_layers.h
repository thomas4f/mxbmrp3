// ============================================================================
// core/render_layers.h
// HUD STACKING for the renderers that can interleave quads and strings: Direct
// GL, the companion window's D3D11 backend and its software renderer.
//
// THE PROBLEM. The game's Draw export takes one quad array and one string
// array and draws every quad before any string, so a HUD dragged over another
// cannot hide the back HUD's text - it bleeds through the front panel. The
// engine path cannot change that. The plugin's own renderers copied the rule
// for parity (hudbatch::build, hudsw::Renderer::render), but nothing forces
// them to: they can draw one HUD's quads and strings before the next HUD's.
//
// THE SHAPE. collectSurface records one Span per HUD (and per Motion ghost,
// and the debug grid): its contiguous range in both arrays and a screen rect.
// assignLayers() then puts each span in the lowest layer that keeps every
// overlapping pair in order, and walkLayers() draws layer by layer: that
// layer's quads, then its strings. Spans in one layer never need ordering
// against each other beyond their array order, which is kept.
//
// WHY LAYERS AND NOT "INTERLEAVE EVERY HUD". Each quads-to-strings switch is a
// texture change, so a new GL run. Interleaving all ~15-49 drawn HUDs would add
// ~2 draw calls per HUD every frame for a problem that exists only where HUDs
// overlap. With layers, a layout where nothing overlaps is ONE layer, and one
// layer takes the flat path: byte-identical batches to before this file.
//
// THE RULE assignLayers enforces, for an earlier span A overlapping a later B:
//   - layer(B) >= layer(A), so A is never drawn after B, and
//   - layer(B) >  layer(A) when A has strings AND B has quads - the bleed case.
// A without strings (the helmet, the vignette: full-screen, quads only) never
// pushes anything up, so they do not turn every frame into two layers.
//
// FAIL-SOFT. A span whose rect is too small (a HUD drawing outside its bounds)
// can only miss an overlap, which draws exactly as it did before: text over the
// front panel in that spot. Spans that do not tile both arrays from index 0
// (a table stale against its arrays) fall back to the flat path.
// Primitives past the last span (the render probe, the sweep status line) draw
// after every layer, on top - which is what both are for.
//
// Pinned by tests/unit/test_render_layers.cpp (the rule, the flat fallback) and
// tests/unit/test_render_batch.cpp (a layered frame's run order).
// ============================================================================
#pragma once

#include <cstdint>

namespace hudlayers {

// One HUD's (or ghost's) contiguous primitive ranges, [q0,q1) and [s0,s1).
struct Span {
    uint32_t q0 = 0, q1 = 0;
    uint32_t s0 = 0, s1 = 0;
    int layer = 0;
};

// Normalized screen rect; `everywhere` overlaps every other span (a Motion
// ghost or a HUD mid-slide, whose drawn position is not its bounds).
struct Rect {
    float l = 0.0f, t = 0.0f, r = 0.0f, b = 0.0f;
    bool everywhere = false;
};

// What a frame carries: null spans / layerCount <= 1 = the flat path.
struct Layers {
    const Span* spans = nullptr;
    int spanCount = 0;
    int layerCount = 0;
};

// Strict overlap: HUDs snapped edge to edge on the grid share an edge and must
// NOT count, or every tidy layout would pay for a second layer.
inline bool overlaps(const Rect& a, const Rect& b) {
    if (a.everywhere || b.everywhere) return true;
    return a.l < b.r && b.l < a.r && a.t < b.b && b.t < a.b;
}

// Assign spans[i].layer for n spans in draw order; returns the layer count
// (1 when nothing needs reordering, 0 for no spans). O(n^2) rect tests on a
// few dozen spans: microseconds.
inline int assignLayers(Span* spans, const Rect* rects, int n) {
    int count = 0;
    for (int i = 0; i < n; ++i) {
        Span& b = spans[i];
        const bool bHasQuads = b.q1 > b.q0;
        int layer = 0;
        for (int j = 0; j < i; ++j) {
            if (!overlaps(rects[j], rects[i])) continue;
            const Span& a = spans[j];
            const int need = a.layer + ((a.s1 > a.s0 && bHasQuads) ? 1 : 0);
            if (need > layer) layer = need;
        }
        b.layer = layer;
        if (layer + 1 > count) count = layer + 1;
    }
    return count;
}

// Spans usable for this frame: they start at 0, are contiguous in both arrays
// and stay inside them, and every layer index is in range.
inline bool tiles(const Layers& ls, int quadCount, int stringCount) {
    if (!ls.spans || ls.spanCount <= 0) return false;
    uint32_t q = 0, s = 0;
    for (int i = 0; i < ls.spanCount; ++i) {
        const Span& sp = ls.spans[i];
        if (sp.q0 != q || sp.s0 != s || sp.q1 < sp.q0 || sp.s1 < sp.s0) return false;
        if (sp.layer < 0 || sp.layer >= ls.layerCount) return false;
        q = sp.q1; s = sp.s1;
    }
    return q <= static_cast<uint32_t>(quadCount) && s <= static_cast<uint32_t>(stringCount);
}

// Call quads(begin, end) and strings(begin, end) in draw order. The flat path
// is exactly the old order: all quads, then all strings.
template <class QuadFn, class StringFn>
void walkLayers(const Layers& ls, int quadCount, int stringCount, QuadFn&& quads,
                StringFn&& strings) {
    if (ls.layerCount <= 1 || !tiles(ls, quadCount, stringCount)) {
        quads(0, quadCount);
        strings(0, stringCount);
        return;
    }
    for (int layer = 0; layer < ls.layerCount; ++layer) {
        for (int i = 0; i < ls.spanCount; ++i)
            if (ls.spans[i].layer == layer)
                quads(static_cast<int>(ls.spans[i].q0), static_cast<int>(ls.spans[i].q1));
        for (int i = 0; i < ls.spanCount; ++i)
            if (ls.spans[i].layer == layer)
                strings(static_cast<int>(ls.spans[i].s0), static_cast<int>(ls.spans[i].s1));
    }
    const Span& last = ls.spans[ls.spanCount - 1];
    quads(static_cast<int>(last.q1), quadCount);
    strings(static_cast<int>(last.s1), stringCount);
}

}  // namespace hudlayers
