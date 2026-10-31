// ============================================================================
// core/engine_cull.h
// Drop the primitives the game engine would draw as nothing, at the handoff.
//
// The engine pays per primitive it is handed, visible or not (tools/probetheme
// priced a quad at ~1 us, roughly 40x the plugin's own cost to build it). The
// HUDs emit some that cannot put a pixel on screen: a background at 0% opacity
// is still a full nine-slice of alpha-0 quads, and a theme slice with no
// border collapses to a quad whose four corners coincide. A busy everything-on
// frame carried ~11% of these (the perf ledger's primitive audit).
//
// They are dropped HERE, on the copy handed to the engine, not at the HUDs:
// HUDs, the fast paths and Motion address their quads by fixed index (a
// nine-slice is always SLICE_COUNT quads, a background is patched in place),
// so the frame the plugin keeps - HudManager::getGameQuads(), the frame hash,
// every test - is the same frame as before, and only the engine sees less.
// When nothing is invisible the frame is handed over as is, with no copy.
//
// Templates over the vendor structs so the unit test can drive them with its
// own (tests/unit/test_engine_cull.cpp).
// ============================================================================
#pragma once

#include <cstddef>
#include <vector>

namespace EngineCull {

// No pixel: alpha 0 (the engine modulates by the quad colour's alpha, as
// hud_sw_renderer reproduces), or all four corners at one point.
template <typename Quad>
inline bool invisibleQuad(const Quad& q) {
    if ((q.m_ulColor >> 24) == 0) return true;
    for (int c = 1; c < 4; ++c)
        if (q.m_aafPos[c][0] != q.m_aafPos[0][0] || q.m_aafPos[c][1] != q.m_aafPos[0][1]) return false;
    return true;
}

// No glyph: empty text, or alpha 0.
template <typename Str>
inline bool invisibleString(const Str& s) {
    return s.m_szString[0] == '\0' || (s.m_ulColor >> 24) == 0;
}

// The primitives of `in` the engine should get: `in` itself when every one is
// visible (no copy), else `scratch` refilled with the visible ones in order.
// `scratch` keeps its capacity, so a steady frame allocates nothing. The
// visible stretches between invisible ones are copied a run at a time: most
// frames carry a few dozen invisible strings (a hidden title's placeholder in
// nearly every HUD) among a thousand, and a per-element copy cost half again.
template <typename T, typename Pred>
inline const std::vector<T>& visibleOnly(const std::vector<T>& in, std::vector<T>& scratch, Pred invisible) {
    const size_t n = in.size();
    size_t i = 0;
    while (i < n && !invisible(in[i])) ++i;
    if (i == n) return in;
    scratch.clear();
    scratch.reserve(n);
    size_t runStart = 0;
    for (;;) {
        scratch.insert(scratch.end(), in.data() + runStart, in.data() + i);   // visible run [runStart, i)
        while (i < n && invisible(in[i])) ++i;
        if (i == n) break;
        runStart = i;
        while (i < n && !invisible(in[i])) ++i;
    }
    return scratch;
}

}  // namespace EngineCull
