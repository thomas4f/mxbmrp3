// ============================================================================
// hud/settings/scroll_view.h
// A scrolling list inside a settings card (Achievements, Riders x2, Find,
// Widgets): the rows that used to be split into pages, in one column you move
// with a scrollbar. Pure geometry and timing - no HUD, no
// renderer; SettingsLayoutContext::beginScroll draws with it.
//
// A ROW IS THE UNIT at the list's edge, handed whole to the shared edge rule
// (hud/clip_marks.h; SettingsLayoutContext::closeScrollRow): our renderers
// cut it there, the game -- which cannot cut -- fades it out over a third of
// a line past it (FADE_LINES), so nothing visibly lands on the heading above
// or the footer below, and at rest does not draw a part row at all.
//
// AT REST THE LIST SITS ON A ROW. Every resting offset is a row's top (snap),
// so a resting list never shows a part row; the fade is only ever seen while
// the list moves. With Motion off it jumps a row at a time, like the game's own
// menus; with Motion on it glides there on Motion's clock (glideMs).
//
// OFFSETS ARE IN LINES (one settings row), not screen units, so a scale or
// theme change keeps the list where it was.
//
// ONE MECHANISM FOR EVERY LIST. The HUD lists (hud_scroll.h) hold a State
// too, and both answer the bar through press() / follow() here and draw it
// with HudScroll::drawBar: what differs is only how far a move goes, which
// each list's Moves says (settings: rowTops, at least STEP_LINES a step; a
// HUD: whole rows, one a step, a page keeping a row of context).
//
// Pinned by tests/unit/test_scroll_view.cpp; the panel side by
// tests/integration/tests/settings_scroll_test.cpp.
// ============================================================================
#pragma once

#include "../../core/hold_repeat.h"
#include "../../core/motion.h"
#include "../clip_marks.h"   // Edge, for every list's edge rule

#include <algorithm>
#include <cmath>
#include <vector>

namespace ScrollView {

// The lists that scroll. Each keeps its own position for as long as the
// plugin runs; one State per id lives on SettingsHud.
enum Id { SERVER_PLAYERS = 0, TRACKED_RIDERS, ACHIEVEMENTS, FIND_RESULTS, WIDGETS, RELEASE_NOTES, DECKS, COUNT };

// A row is gone - alpha 0 - once this much of a line hangs past the edge.
constexpr float FADE_LINES = 1.0f / 3.0f;
// One step moves at least this many lines, in whole rows: two rows of a
// one-line list, one two-line achievement. The track's shortest move.
constexpr float STEP_LINES = 2.0f;
constexpr float EPS = 1e-3f;
// The list a tab ends with, grown to the footer (SettingsLayoutContext::planFilled):
// which list (an Id, -1 = none) and by how many lines.
struct Fill { int list = -1; float lines = 0.0f; };
// Every scrollbar, the settings lists' and the HUDs', in character cells: the
// bar as drawn (thin, so it reads as a bar and not a column), and the strip
// the pointer can hit, centred on it (wider, so it is still easy to hit).
constexpr float BAR_CHARS = 0.75f;
constexpr float HIT_CHARS = 1.5f;

// THE GLIDE: where a list rests, where it is drawn this frame, and the move
// between on Motion's clock (glideTo, tick).
struct Glide {
    float target = 0.0f;   // where the list comes to rest: a row's top, in lines
    float shown = 0.0f;    // where it is drawn this frame, in lines
    float from = 0.0f;     // where the glide toward target started
    long long glideStartUs = 0;
    bool gliding = false;
};

struct State : Glide {
    // The scrollbar's thumb is held: the list follows `dragLines` (the thumb's
    // own position, continuous) snapped to a row, the thumb the pointer.
    bool dragging = false;
    float dragLines = 0.0f;
    float grabY = 0.0f;    // pointer to the thumb's top, in HUD units
    // An end arrow is held: -1 up, +1 down, 0 none. One step on the press,
    // then the panel's hold-to-repeat curve (core/hold_repeat.h).
    int arrowDir = 0;
    int arrowRepeats = 0;
    long long arrowStartUs = 0, arrowLastUs = 0;

    // What the last build laid out, for the input side (drag, track) and the
    // bar. Pre-offset HUD units, like a click region. The view is the bar's
    // height: an arrow cell (lineH) at each end, the track between.
    bool laidOut = false;
    float viewX = 0.0f, viewY = 0.0f, viewW = 0.0f, viewH = 0.0f;
    float barX = 0.0f, barW = 0.0f;
    float trackY = 0.0f, trackH = 0.0f;  // between the end arrows: the thumb's travel
    float lineH = 0.0f;
    float contentLines = 0.0f, viewLines = 0.0f, maxRest = 0.0f;
    float thumbY = 0.0f, thumbH = 0.0f;   // pre-offset, from the build
    std::vector<float> rowTops;          // every row's top, in lines, in order (the settings lists')
};

// The furthest a list can rest: the first row whose top leaves everything from
// there on inside the view. 0 when the whole list fits.
inline float maxRest(const std::vector<float>& tops, float contentLines, float viewLines) {
    if (contentLines <= viewLines + EPS) return 0.0f;
    for (float t : tops) {
        if (contentLines - t <= viewLines + EPS) return t;
    }
    return tops.empty() ? 0.0f : tops.back();
}

// The resting row nearest `lines`, within [0, maxRest].
inline float snap(const std::vector<float>& tops, float limit, float lines) {
    float best = 0.0f, bestDist = 1e30f;
    for (float t : tops) {
        if (t > limit + EPS) break;
        const float d = std::fabs(t - lines);
        if (d < bestDist) { bestDist = d; best = t; }
    }
    return best;
}

// `steps` steps on from the resting row `from` (positive = down the list):
// row by row until STEP_LINES have passed per step, clamped to the ends.
inline float step(const std::vector<float>& tops, float limit, float from, int steps) {
    if (tops.empty() || steps == 0) return from;
    int i = 0;
    while (i + 1 < static_cast<int>(tops.size()) && tops[static_cast<size_t>(i) + 1] <= from + EPS) ++i;
    const int dir = steps > 0 ? 1 : -1;
    for (int n = steps > 0 ? steps : -steps; n > 0; --n) {
        const float start = tops[static_cast<size_t>(i)];
        while (true) {
            const int next = i + dir;
            if (next < 0 || next >= static_cast<int>(tops.size())) break;
            if (tops[static_cast<size_t>(next)] > limit + EPS) break;
            i = next;
            if (std::fabs(tops[static_cast<size_t>(i)] - start) >= STEP_LINES - EPS) break;
        }
    }
    return std::min(tops[static_cast<size_t>(i)], limit);
}

// A view's worth on from `from` (the scrollbar's track): the furthest row
// still inside the next view down, or the nearest above that keeps the old
// top row in sight going up -- always at least one row.
inline float page(const std::vector<float>& tops, float limit, float from, float viewLines, int dir) {
    if (tops.empty()) return from;
    float best = from;
    if (dir > 0) {
        for (float t : tops) {
            if (t > limit + EPS) break;
            if (t > from + EPS && t <= from + viewLines + EPS) best = t;
        }
        if (best == from) best = step(tops, limit, from, 1);
    } else {
        for (float t : tops) {
            if (t >= from - EPS) break;
            if (t >= from - viewLines - EPS) { best = t; break; }
        }
        if (best == from) best = step(tops, limit, from, -1);
    }
    return best;
}

// EVERY LIST'S EDGE RULE (hud/clip_marks.h), the settings lists' and the HUD
// lists' alike: a row crossing the view fades out for the game over
// FADE_LINES past it while the list moves, and at rest a part row is hidden.
inline Edge listEdge(float lineH, bool moving) { return Edge{ 0.0f, FADE_LINES * lineH, !moving }; }

// The glide's length on Motion's levels: none with Motion off.
inline float glideMs(Motion::Level level) {
    return level == Motion::Level::NORMAL ? 220.0f : level == Motion::Level::SUBTLE ? 140.0f : 0.0f;
}

// Head for `to`: from wherever the list is now, so a second move mid-glide
// carries on from the drawn position rather than jumping back to the old rest.
inline void glideTo(Glide& s, float to, long long nowUs, Motion::Level level) {
    s.target = to;
    if (glideMs(level) <= 0.0f || std::fabs(s.shown - to) < EPS) {
        s.shown = to;
        s.gliding = false;
        return;
    }
    s.from = s.shown;
    s.glideStartUs = nowUs;
    s.gliding = true;
}

// Advance a glide to `nowUs`; true while the drawn position moved.
inline bool tick(Glide& s, long long nowUs, Motion::Level level) {
    if (!s.gliding) return false;
    const float ms = glideMs(level);
    const float t = ms > 0.0f ? static_cast<float>(nowUs - s.glideStartUs) / (ms * 1000.0f) : 1.0f;
    if (t >= 1.0f) {
        s.shown = s.target;
        s.gliding = false;
        return true;
    }
    const float u = t < 0.0f ? 0.0f : t;
    const float e = 1.0f - (1.0f - u) * (1.0f - u) * (1.0f - u);   // ease-out cubic
    s.shown = s.from + (s.target - s.from) * e;
    return true;
}

// The thumb on a track of `viewH`: as tall as the share of the list in view
// (never under minH), placed by how far the list is toward its last rest.
inline void thumb(float viewH, float contentLines, float viewLines, float offsetLines, float limit,
                  float minH, float& y, float& h) {
    const float share = contentLines > 0.0f ? std::min(1.0f, viewLines / contentLines) : 1.0f;
    h = std::min(viewH, std::max(minH, viewH * share));
    const float t = limit > EPS ? std::clamp(offsetLines / limit, 0.0f, 1.0f) : 0.0f;
    y = (viewH - h) * t;
}

// The inverse, for a drag: the offset (lines, continuous) a thumb whose top
// sits `thumbTop` down the track stands for.
inline float offsetForThumb(float viewH, float thumbH, float thumbTop, float limit) {
    const float travel = viewH - thumbH;
    if (travel <= 0.0f) return 0.0f;
    return std::clamp(thumbTop / travel, 0.0f, 1.0f) * limit;
}

// ---- The bar, for every list ------------------------------------------------

// Where the bar sits: centred in the padding between the card's right edge and
// the panel's, so it never covers content that runs to the card's edge; a
// padding thinner than the bar takes it flush with the panel's edge. Its left.
inline float barLeft(float cardRight, float panelRight, float barW) {
    return panelRight - std::max(0.0f, (panelRight - cardRight - barW) * 0.5f) - barW;
}

// The bar's parts: an arrow cell at each end, the track (and its thumb) between.
enum Part { PART_NONE = -1, PART_UP = 0, PART_TRACK = 1, PART_DOWN = 2 };

// The part a height on the bar is in (the caller has the pointer on the bar).
inline int partAtY(const State& s, float y) {
    return y < s.trackY ? PART_UP : (y >= s.trackY + s.trackH ? PART_DOWN : PART_TRACK);
}

// The part under the pointer: the hit strip (HIT_CHARS) is wider than the drawn
// bar on both sides, so the thin bar is easy to hit. PART_NONE off it.
inline int partAt(const State& s, float x, float y) {
    const float slop = s.barW * ((HIT_CHARS - BAR_CHARS) * 0.5f / BAR_CHARS);
    if (x < s.barX - slop || x > s.barX + s.barW + slop || y < s.viewY || y > s.viewY + s.viewH) return PART_NONE;
    return partAtY(s, y);
}

// A list's moves, which only the list knows -- how far a step and a page go,
// where a dragged thumb rests. Each heads the State for its new rest (glideTo)
// and returns true when it moved:
//   bool step(int dir); bool page(int dir); bool dragTo(float lines); void dragEnd();

// A press on the bar's `part`, the pointer at `y`: an end arrow steps the list
// and starts its hold-to-repeat (follow); the thumb is held; the track either
// side moves a view's worth toward the press.
template <class Moves>
void press(State& s, int part, float y, long long nowUs, Moves& moves) {
    if (part == PART_UP || part == PART_DOWN) {
        s.arrowDir = part == PART_UP ? -1 : 1;
        s.arrowRepeats = 0;
        s.arrowStartUs = s.arrowLastUs = nowUs;
        moves.step(s.arrowDir);
    } else if (y >= s.thumbY && y <= s.thumbY + s.thumbH) {
        s.dragging = true;
        s.grabY = y - s.thumbY;
        s.dragLines = s.shown;
    } else {
        moves.page(y < s.thumbY ? -1 : 1);
    }
}

// Once a frame: a held thumb follows the pointer (and lets go on release); a
// held arrow repeats on the panel's hold curve (core/hold_repeat.h) and stops
// on release or once the pointer leaves its cell; the glide advances. True
// when anything drawn changed.
template <class Moves>
bool follow(State& s, float x, float y, bool valid, bool held, long long nowUs, Motion::Level level,
            long long fastMs, Moves& moves) {
    bool moved = false;
    if (s.dragging) {
        if (!held) {
            s.dragging = false;
            moves.dragEnd();
            moved = true;
        } else if (valid) {
            const float lines = offsetForThumb(s.trackH, s.thumbH, y - s.grabY - s.trackY, s.maxRest);
            if (std::fabs(lines - s.dragLines) > EPS) {
                s.dragLines = lines;
                moves.dragTo(lines);
                moved = true;
            }
        }
    }
    if (s.arrowDir != 0) {
        const int cell = s.arrowDir < 0 ? PART_UP : PART_DOWN;
        if (!held || !valid || partAt(s, x, y) != cell) {
            s.arrowDir = 0;
            moved = true;   // the arrow's lit state
        } else if ((nowUs - s.arrowStartUs) / 1000 >= HoldRepeat::INITIAL_DELAY_MS &&
                   (nowUs - s.arrowLastUs) / 1000 >= HoldRepeat::intervalMs(s.arrowRepeats, fastMs)) {
            s.arrowLastUs = nowUs;
            ++s.arrowRepeats;
            moved |= moves.step(s.arrowDir);
        }
    }
    moved |= tick(s, nowUs, level);
    return moved;
}

}  // namespace ScrollView
