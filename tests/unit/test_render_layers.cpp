// ============================================================================
// tests/unit/test_render_layers.cpp
// core/render_layers.h -- HUD stacking for the renderers that can interleave
// quads and strings (Direct GL, the companion window).
//
// What it pins: the layer rule (an earlier HUD's text goes under a later HUD's
// panel only where the two overlap), that a layout with no overlap stays ONE
// layer (the flat order, so those frames batch exactly as before), and that
// spans which do not tile the arrays fall back to flat instead of dropping
// primitives.
// ============================================================================
#include "doctest.h"
#include "core/render_layers.h"

#include <utility>
#include <vector>

using hudlayers::Layers;
using hudlayers::Rect;
using hudlayers::Span;

namespace {

Span span(uint32_t q0, uint32_t q1, uint32_t s0, uint32_t s1) {
    Span s; s.q0 = q0; s.q1 = q1; s.s0 = s0; s.s1 = s1;
    return s;
}

Rect rect(float l, float t, float r, float b) {
    Rect x; x.l = l; x.t = t; x.r = r; x.b = b;
    return x;
}

// The draw order walkLayers produces, as ('q'|'s', index) pairs.
std::vector<std::pair<char, int>> order(const Layers& ls, int quads, int strings) {
    std::vector<std::pair<char, int>> out;
    hudlayers::walkLayers(ls, quads, strings,
        [&](int b, int e) { for (int i = b; i < e; ++i) out.push_back({ 'q', i }); },
        [&](int b, int e) { for (int i = b; i < e; ++i) out.push_back({ 's', i }); });
    return out;
}

}  // namespace

TEST_CASE("render layers: HUDs that do not overlap stay in one layer") {
    std::vector<Span> spans = { span(0, 2, 0, 3), span(2, 4, 3, 5) };
    std::vector<Rect> rects = { rect(0.0f, 0.0f, 0.2f, 0.2f), rect(0.5f, 0.5f, 0.7f, 0.7f) };
    CHECK(hudlayers::assignLayers(spans.data(), rects.data(), 2) == 1);
    CHECK(spans[1].layer == 0);
}

TEST_CASE("render layers: HUDs snapped edge to edge do not count as overlapping") {
    std::vector<Span> spans = { span(0, 1, 0, 1), span(1, 2, 1, 2) };
    std::vector<Rect> rects = { rect(0.0f, 0.0f, 0.2f, 0.2f), rect(0.2f, 0.0f, 0.4f, 0.2f) };
    CHECK(hudlayers::assignLayers(spans.data(), rects.data(), 2) == 1);
}

TEST_CASE("render layers: a later panel over earlier text goes up a layer") {
    std::vector<Span> spans = { span(0, 1, 0, 1), span(1, 2, 1, 2) };
    std::vector<Rect> rects = { rect(0.0f, 0.0f, 0.3f, 0.3f), rect(0.2f, 0.2f, 0.5f, 0.5f) };
    CHECK(hudlayers::assignLayers(spans.data(), rects.data(), 2) == 2);
    CHECK(spans[1].layer == 1);
    // Back HUD complete (panel, text), then the front one: its panel covers the text.
    Layers ls{ spans.data(), 2, 2 };
    const std::vector<std::pair<char, int>> want = { { 'q', 0 }, { 's', 0 }, { 'q', 1 }, { 's', 1 } };
    CHECK(order(ls, 2, 2) == want);
}

TEST_CASE("render layers: quads-only HUDs never push the next one up") {
    // The helmet and the vignette are full-screen quads with no text: every HUD
    // overlaps them, and none of them needs a layer of its own for it.
    std::vector<Span> spans = { span(0, 1, 0, 0), span(1, 3, 0, 2) };
    std::vector<Rect> rects = { rect(0.0f, 0.0f, 1.0f, 1.0f), rect(0.2f, 0.2f, 0.5f, 0.5f) };
    CHECK(hudlayers::assignLayers(spans.data(), rects.data(), 2) == 1);
}

TEST_CASE("render layers: text-only HUDs over earlier text keep the same layer") {
    // No panel to hide anything with: drawing its text after the earlier text
    // in the same strings pass is already the right order.
    std::vector<Span> spans = { span(0, 1, 0, 1), span(1, 1, 1, 2) };
    std::vector<Rect> rects = { rect(0.0f, 0.0f, 0.3f, 0.3f), rect(0.1f, 0.1f, 0.2f, 0.2f) };
    CHECK(hudlayers::assignLayers(spans.data(), rects.data(), 2) == 1);
}

TEST_CASE("render layers: a HUD never drops below one it overlaps") {
    // A (text) < B (panel over A, layer 1) < C (quads only, over B and not A):
    // C must stay at B's layer even though it needs no bump of its own.
    std::vector<Span> spans = { span(0, 1, 0, 1), span(1, 2, 1, 2), span(2, 3, 2, 2) };
    std::vector<Rect> rects = { rect(0.0f, 0.0f, 0.3f, 0.3f), rect(0.2f, 0.2f, 0.5f, 0.5f),
                                rect(0.4f, 0.4f, 0.6f, 0.6f) };
    hudlayers::assignLayers(spans.data(), rects.data(), 3);
    CHECK(spans[2].layer >= spans[1].layer);
}

TEST_CASE("render layers: an everywhere span overlaps every other one") {
    std::vector<Span> spans = { span(0, 1, 0, 1), span(1, 2, 1, 2) };
    std::vector<Rect> rects = { rect(0.0f, 0.0f, 0.1f, 0.1f), rect(0.8f, 0.8f, 0.9f, 0.9f) };
    rects[0].everywhere = true;
    CHECK(hudlayers::assignLayers(spans.data(), rects.data(), 2) == 2);
}

TEST_CASE("render layers: one layer walks the flat order") {
    std::vector<Span> spans = { span(0, 1, 0, 1), span(1, 2, 1, 2) };
    Layers ls{ spans.data(), 2, 1 };
    const std::vector<std::pair<char, int>> want = { { 'q', 0 }, { 'q', 1 }, { 's', 0 }, { 's', 1 } };
    CHECK(order(ls, 2, 2) == want);
    CHECK(order(Layers{}, 2, 2) == want);
}

TEST_CASE("render layers: primitives past the last span draw on top") {
    // The render probe and the sweep status line are appended after collect.
    std::vector<Span> spans = { span(0, 1, 0, 1), span(1, 2, 1, 2) };
    spans[1].layer = 1;
    Layers ls{ spans.data(), 2, 2 };
    const std::vector<std::pair<char, int>> want = {
        { 'q', 0 }, { 's', 0 }, { 'q', 1 }, { 's', 1 }, { 'q', 2 }, { 's', 2 } };
    CHECK(order(ls, 3, 3) == want);
}

TEST_CASE("render layers: spans that do not tile the arrays fall back to flat") {
    // A gap at the front (something spliced into the arrays), or spans that run
    // past the arrays (a stale table): draw everything, in the old order.
    std::vector<Span> gap = { span(1, 2, 0, 1), span(2, 3, 1, 2) };
    gap[1].layer = 1;
    const std::vector<std::pair<char, int>> flat = {
        { 'q', 0 }, { 'q', 1 }, { 'q', 2 }, { 's', 0 }, { 's', 1 } };
    CHECK(order(Layers{ gap.data(), 2, 2 }, 3, 2) == flat);

    std::vector<Span> stale = { span(0, 1, 0, 1), span(1, 5, 1, 2) };
    stale[1].layer = 1;
    CHECK(order(Layers{ stale.data(), 2, 2 }, 3, 2) == flat);
}
