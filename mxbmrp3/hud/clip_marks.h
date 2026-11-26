// ============================================================================
// hud/clip_marks.h
// THE EDGE, for every HUD whose content moves past a box: a scrolling settings
// list, a HUD list's scrollback (hud_scroll.h), the gear and odometer roll,
// the Dash page strip, Standings rows changing places. Each lays its content out ONCE, the way the plugin's own
// renderers (Direct GL, the companion window) draw it -- whole, where it is --
// and hands each piece with its box to edge() (or a card to card()). From
// that one layout both pictures are made here, by one rule:
//  - our renderers CUT whatever crosses the box at it (core/render_clip.h);
//  - the game, which draws whole quads and strings and cannot cut, gets the
//    piece FADED by how far it crosses (Edge, below), and a card's slices
//    clamped to the box, the one past it dropped (an open edge).
// Nothing inside its box is touched or marked, so a still HUD costs nothing.
//
// The marks underneath, applied by collectSurface on our surfaces only
// (core/hud_manager_clip.cpp): a CUT (the range cut at a rect in layout
// coordinates, pre-offset like every add* helper, so a drag moves it with the
// primitives; cut ranges never overlap) and OURS (the range as our renderers
// draw it, kept before the game's copy was faded or clamped). One ClipMarks
// per HUD (BaseHud::m_clip), filled at rebuild time, reset by clearStrings().
//
// Pinned by tests/unit/test_clip_marks.cpp (the rule) and
// tests/integration/tests/clip_marks_test.cpp (the HUDs).
// ============================================================================
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "../core/motion.h"         // Motion::scaleAlpha
#include "../game/game_config.h"    // SPluginQuad_t / SPluginString_t

// How the game fades a piece crossing its box. `over` is how far the piece
// reaches past the box on its worst side, negative while it is inside by that
// much. The piece is whole until `over` reaches `start`, gone `ramp` past it
// (Motion::fadePast): start 0 lets it cross a little before it is gone (a
// settings row, a digit), a negative start has it gone by the time it reaches
// the edge (a Dash icon, which must never sit on the card's border). hidePart:
// a piece not wholly inside is not drawn at all (a list at rest shows no part
// row in game).
struct Edge {
    float start = 0.0f;
    float ramp = 1.0f;
    bool hidePart = false;
};

struct ClipMarks {
    struct Rect { float l, t, r, b; };
    struct Mark {
        enum Kind : uint8_t { CUT, OURS };
        Kind kind = CUT;
        int q0 = 0, q1 = 0, s0 = 0, s1 = 0;   // [q0,q1) quads, [s0,s1) strings
        float l = 0.0f, t = 0.0f, r = 0.0f, b = 0.0f;   // CUT
        int quad0 = 0, color0 = 0;   // OURS: its quads in `quads`, its strings' colours in `colors`
    };
    std::vector<Mark> marks;
    std::vector<SPluginQuad_t> quads;
    std::vector<unsigned long> colors;

    bool empty() const { return marks.empty(); }
    void clear() { marks.clear(); quads.clear(); colors.clear(); }

    // Below this a piece is inside (a float hair, far below a pixel).
    static constexpr float INSIDE_EPS = 1e-5f;

    // Far past any HUD (they live in about 0..1): a box reaching this far on
    // one axis only ever cuts on the other.
    static constexpr float REACH = 10.0f;
    // A box that cuts only at its top and bottom: a list's view, a roll's row.
    static Rect rows(float t, float b) { return Rect{ -REACH, t, REACH, b }; }
    // One that cuts only at its left and right: the Dash strip across a row.
    static Rect columns(float l, float r) { return Rect{ l, -REACH, r, REACH }; }

    // A range's primitives moved `dy` up the screen, as a scrolled list moves
    // its rows before handing them to edge().
    static void shiftUp(std::vector<SPluginQuad_t>& q, std::vector<SPluginString_t>& strings, size_t q0,
                        size_t q1, size_t s0, size_t s1, float dy) {
        for (size_t i = q0; i < q1 && i < q.size(); ++i)
            for (auto& c : q[i].m_aafPos) c[1] -= dy;
        for (size_t i = s0; i < s1 && i < strings.size(); ++i) strings[i].m_afPos[1] -= dy;
    }

    // How far `piece` reaches past `box` on its worst side (negative: inside).
    static float overhang(const Rect& piece, const Rect& box) {
        return std::max(std::max(box.l - piece.l, piece.r - box.r), std::max(box.t - piece.t, piece.b - box.b));
    }
    // Whether `piece` lies wholly past `box` on some side, `eps` short of
    // touching it: nothing of it would show.
    static bool outside(const Rect& piece, const Rect& box, float eps = 0.0f) {
        return piece.b <= box.t + eps || piece.t >= box.b - eps || piece.r <= box.l + eps || piece.l >= box.r - eps;
    }
    // The game's alpha for a piece `over` past its box.
    static float gameAlpha(float over, const Edge& e) {
        if (e.hidePart) return over > INSIDE_EPS ? 0.0f : 1.0f;
        return Motion::fadePast(over - e.start, e.ramp);
    }

    // THE ONE CALL. A piece (the range, extending over `piece`) laid out as
    // our renderers draw it, in `box`: crossing it, our renderers cut it there
    // and the game gets it at gameAlpha. Returns the game's alpha (1 inside).
    float edge(std::vector<SPluginQuad_t>& q, std::vector<SPluginString_t>& strings, size_t q0, size_t q1,
               size_t s0, size_t s1, const Rect& piece, const Rect& box, const Edge& rule) {
        const float over = overhang(piece, box);
        const float alpha = gameAlpha(over, rule);
        if (over <= INSIDE_EPS && alpha >= 1.0f) return 1.0f;
        if (over > INSIDE_EPS) cut(q0, q1, s0, s1, box);
        if (alpha < 1.0f) {
            ours(q, strings, q0, q1, s0, s1);
            for (size_t i = q0; i < q1 && i < q.size(); ++i) q[i].m_ulColor = Motion::scaleAlpha(q[i].m_ulColor, alpha);
            for (size_t i = s0; i < s1 && i < strings.size(); ++i)
                strings[i].m_ulColor = Motion::scaleAlpha(strings[i].m_ulColor, alpha);
        }
        return alpha;
    }

    // A themed card's slices [q0,q1) (no strings: its range of them is the
    // empty one at sAt), laid out whole, in `box`: our renderers cut it there;
    // for the game each slice is clamped to the box (a stretched side or middle
    // just stops) and one wholly past it dropped, so the card reads as going on
    // past the edge, open on that side. The slices are where they are drawn
    // (offset by ox, oy), the box and the result in layout coordinates.
    // Returns the part of the card the game draws, for its fill cover; empty
    // (zero size) when it draws none.
    Rect card(std::vector<SPluginQuad_t>& q, size_t q0, size_t q1, size_t sAt, const Rect& box, float ox,
              float oy) {
        q1 = std::min(q1, q.size());
        Rect ext{ 1e9f, 1e9f, -1e9f, -1e9f };
        for (size_t i = q0; i < q1; ++i)
            for (const auto& c : q[i].m_aafPos) {
                ext.l = std::min(ext.l, c[0] - ox); ext.r = std::max(ext.r, c[0] - ox);
                ext.t = std::min(ext.t, c[1] - oy); ext.b = std::max(ext.b, c[1] - oy);
            }
        if (q0 >= q1 || ext.r <= ext.l || ext.b <= ext.t) return Rect{ 0.0f, 0.0f, 0.0f, 0.0f };
        if (overhang(ext, box) <= INSIDE_EPS) return ext;
        static const std::vector<SPluginString_t> kNoStrings;
        cut(q0, q1, sAt, sAt, box);
        ours(q, kNoStrings, q0, q1, 0, 0);
        const Rect in{ std::max(ext.l, box.l) + ox, std::max(ext.t, box.t) + oy, std::min(ext.r, box.r) + ox,
                       std::min(ext.b, box.b) + oy };
        for (size_t i = q0; i < q1; ++i) {
            float lo = 1e9f, hi = -1e9f, le = 1e9f, ri = -1e9f;
            for (const auto& c : q[i].m_aafPos) {
                lo = std::min(lo, c[1]); hi = std::max(hi, c[1]);
                le = std::min(le, c[0]); ri = std::max(ri, c[0]);
            }
            const bool past = outside(Rect{ le, lo, ri, hi }, in);
            if (past) q[i].m_ulColor = 0;
            for (auto& c : q[i].m_aafPos) {
                c[0] = past ? in.l : std::clamp(c[0], in.l, in.r);
                c[1] = past ? in.t : std::clamp(c[1], in.t, in.b);
            }
        }
        if (in.b <= in.t || in.r <= in.l) return Rect{ 0.0f, 0.0f, 0.0f, 0.0f };
        return Rect{ in.l - ox, in.t - oy, in.r - ox, in.b - oy };
    }

    // A range whose primitives move WITHOUT a rebuild (a HUD that repositions
    // them by index every frame, as Standings slides its rows), so where each
    // stands is not known here and edge() cannot weigh it: our renderers cut
    // the whole range at `box`; the game draws it as laid out.
    void frame(size_t q0, size_t q1, size_t s0, size_t s1, const Rect& box) { cut(q0, q1, s0, s1, box); }

private:
    // Kept in range order, which is how they are applied (a card is placed
    // after the rows it holds, but its slices come before theirs).
    void cut(size_t q0, size_t q1, size_t s0, size_t s1, const Rect& box) {
        if (q0 >= q1 && s0 >= s1) return;
        Mark m = range(Mark::CUT, q0, q1, s0, s1);
        m.l = box.l; m.t = box.t; m.r = box.r; m.b = box.b;
        // Among the cuts, that is: OURS marks are applied by range, in any
        // order, so the search steps over them.
        auto at = marks.end();
        while (at != marks.begin() && ((at - 1)->kind != Mark::CUT || (at - 1)->q0 > m.q0)) --at;
        marks.insert(at, m);
    }

    void ours(const std::vector<SPluginQuad_t>& from, const std::vector<SPluginString_t>& strings, size_t q0,
              size_t q1, size_t s0, size_t s1) {
        q1 = std::min(q1, from.size());
        s1 = std::min(s1, strings.size());
        q0 = std::min(q0, q1);
        s0 = std::min(s0, s1);
        if (q0 >= q1 && s0 >= s1) return;
        Mark m = range(Mark::OURS, q0, q1, s0, s1);
        m.quad0 = static_cast<int>(quads.size());
        m.color0 = static_cast<int>(colors.size());
        quads.insert(quads.end(), from.begin() + static_cast<std::ptrdiff_t>(q0),
                     from.begin() + static_cast<std::ptrdiff_t>(q1));
        for (size_t i = s0; i < s1; ++i) colors.push_back(strings[i].m_ulColor);
        marks.push_back(m);
    }

    static Mark range(Mark::Kind kind, size_t q0, size_t q1, size_t s0, size_t s1) {
        Mark m;
        m.kind = kind;
        m.q0 = static_cast<int>(std::min(q0, q1)); m.q1 = static_cast<int>(q1);
        m.s0 = static_cast<int>(std::min(s0, s1)); m.s1 = static_cast<int>(s1);
        return m;
    }
};
