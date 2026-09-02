// ============================================================================
// hud/nine_slice.h
// Pure geometry for a 9-slice themed panel: given a rect and a corner inset, the
// 9 quad windings that tile a corner/edge/center sprite trio across it.
//
// Header-only and free of any game/Windows dependency so the unit suite can
// compile it directly (tests/unit/nine_slice_test.cpp) -- the geometry is where
// the subtle bugs live, and it is cheap to pin.
//
// WHY NINE SPRITE FILES AND NOT ONE ATLAS. SPluginQuad_t is 4 corner positions +
// ONE whole sprite index; there is no UV / source-rect field, so a resizable panel
// cannot be cut out of a single atlas. It must be nine separate sprites, and the
// game loads each one by PATH, so they cannot be synthesised at load either. A
// spritesheet can only ever be an AUTHORING format that some tool splits up.
//
// EVERY SLICE IS DRAWN IN PLACE. All nine quads share one winding -- u runs +x, v
// runs +y -- and differ only in their rect, exactly like the center. So a slice
// file looks, in an image editor, like the thing it becomes on screen:
// frame_corner_tr.tga is a top-right corner, frame_edge_left.tga a left edge. That
// is what every other 9-slice system does (Android .9.png, CSS border-image, Qt
// BorderImage, Unity, Godot, Slate, KDE's FrameSvg), so importing art from one is
// cut-and-save with no orientation table in between.
//
// NOTHING HERE GENERATES ART, and nothing should: an image editor is the
// toolchain. tools/themeslice CUTS a master image into the 27 files and never
// draws a pixel, which is the whole difference -- a generator hides the source, a
// slicer keeps it (and copies the master in beside the slices, so it cannot go
// missing).
//
// TWO RULES THAT ARE EASY TO GET WRONG:
//
//   1. SQUARE MUST BE SQUARE. HUD coords are normalized over a 16:9 viewport, so
//      a delta of D covers D*vpW pixels in x but D*vpH in y. A corner authored
//      with equal x/y insets comes out 1.78x too wide -- visibly oval. The inset
//      is therefore specified in Y and converted: insetX = borderY / aspect.
//
//   2. CORNER CLAMP. When the panel is smaller than 2*inset on an axis the
//      corners would overlap and the frame inverts (small widgets hit this). The
//      inset is scaled down to fit -- by ONE factor applied to BOTH axes, so the
//      corner stays square while it shrinks. A fully clamped square panel
//      degrades to a circle, which is the correct 9-slice behaviour.
//
// A third rule lives in the ART, not here, and nothing asserts it: the edge
// sprite's inner value must equal the center sprite's value, or the
// independently-stretched slices show a hard seam inset from the panel edge.
// ============================================================================
#pragma once

#include <algorithm>

namespace NineSlice {

// Which edge/corner a slice occupies. A theme supplies a distinct sprite for each,
// drawn as authored -- so the position names below are literal, and the file named
// after a position is a picture of that position.
enum Side { TOP = 0, BOTTOM = 1, LEFT = 2, RIGHT = 3 };   // edges
enum Corner { TL = 0, TR = 1, BL = 2, BR = 3 };           // corners

// One emitted quad: which sprite to draw and the 4 corner positions, in the
// renderer's winding order (pos[0]=TL/u0v0, pos[1]=BL/u0v1, pos[2]=BR, pos[3]=TR).
struct Slice {
    enum Part { CENTER, EDGE, CORNER };
    Part part;
    int index = 0;      // Side for EDGE, Corner for CORNER, unused for CENTER
    float pos[4][2];
};

// The 9 slices are always emitted in this order, so callers that overwrite a
// previously emitted background in place (BaseHud's layout fast path) stay
// consistent with the initial emission.
constexpr int SLICE_COUNT = 9;

// Effective inset after the clamp, in (x, y). Exposed separately so tests can
// assert the square-in-pixels property without going through the quads.
struct Border { float x, y; };

inline Border clampedBorder(float w, float h, float borderY, float aspect) {
    float iy = borderY;
    float ix = borderY / aspect;
    // One scale for both axes keeps the corner square while shrinking it.
    const float scale = std::min({1.0f, (0.5f * w) / ix, (0.5f * h) / iy});
    return { ix * scale, iy * scale };
}

// ---------------------------------------------------------------------------
// Nesting metrics: where a CARD may sit inside the FRAME, and how far a panel's
// content must be pushed in to sit inside both.
//
// All three are derived from the theme's own two insets, so a skinner who
// thickens a frame gets the clearance, the card and the content indent to move
// with it -- there is no hardcoded margin anywhere in the layout code. Pure
// functions of (inset, aspect) and unit-pinned, because the failure mode is
// visual-only: a card that crosses the frame just looks wrong, nothing asserts.
// ---------------------------------------------------------------------------

// A SLICE SIZE IS STATED IN GRID CELLS, and this is the only place that converts.
//
// `cells` is how wide the corner art is, in cells of the horizontal lattice; the Y
// extent is that same distance in PIXELS, which is why the aspect appears. So a
// 3-cell frame is 3 cells across and a visually square corner down, and the margin
// it costs the layout is exactly 3 cells -- no rounding anywhere.
//
// Whole cells, not a normalized-Y fraction put through a ceil: a fraction that
// quantises to the same cell count over a range of values resizes the drawn
// corner without moving the spacing, so the headline theme knob lies.
inline float cellsToBorderX(float cells, float cellW) {
    return cells * cellW;
}
inline float cellsToBorderY(float cells, float cellW, float aspect) {
    return cells * cellW * aspect;
}

// Top edge of a title band, given the panel's top, the frame's vertical margin,
// and the top of the caption's own BOX (see `boxTop` below).
//
// The band's LEFT and RIGHT are flush with the frame's inner boundary (the caller
// insets them by frameMarginX), and this makes the TOP flush too, so one clearance
// governs all three sides. Whole cells on both axes, not equal pixels: the grid
// is 10.56 x 12.672px at 1080p, so the two can't be both, and every other themed
// margin quantises in cells.
//
// Two clamps, and they pull opposite ways on purpose:
//
//   min() with the glyph -- a band must never start BELOW its own title. The
//   settings panel is the case that matters: its heading sits well inside the
//   frame margin, so anything that pushes the band down past the text breaks it.
//
//   max() with the panel top -- a title closer to the top than its own pad would
//   otherwise put the band outside the panel. Overlapping the frame's top EDGE is
//   fine (the band is inset horizontally, so the corner motifs are untouched);
//   escaping the panel is not.
//
// `boxTop` is the top of the caption's BOX, not the glyph row: the band's own
// padding lives inside the box, so it must not enter this comparison. Don't take
// a pad here for the caller to add back -- a cancelling pair invites being
// "simplified" away, after which growing [title] padding walks the clamp up and
// drags the band into the frame's border. One argument, no cancellation.
inline float titleBandTop(float panelTop, float frameMarginY, float boxTop) {
    const float flush = panelTop + frameMarginY;
    return std::max(panelTop, std::min(flush, boxTop));
}

// Top edge of the BODY card -- the one behind a HUD's rows or graph.
//
// Two cases, and the branch is the whole reason this is a function: with a title
// band the card hangs off the band's bottom edge, so the seam between header and
// body is one number the theme sets. WITHOUT one -- a theme that turns the band
// off, or a HUD whose title the user hid -- there is no bottom edge to hang from,
// and the card takes the panel's own frame clearance instead, filling the interior.
// Writing that as `bandBottom + gap` with bandBottom defaulted to the panel top
// silently adds the seam gap to a card that has nothing above it.
//
// bandBottom is 0 when no band was drawn, matching what the band emitter returns.
inline float contentCardTop(float panelTop, float frameMarginY, float bandBottom,
                            float gapY) {
    // ONE VISIBLE GAP UNDER THE BAND, and flush inside the frame when there is no band.
    //
    // The gap is one term shared by all three boundaries -- see LayoutMetrics::
    // sectionGap. The settings panel separates its cards by that same gap, so a band
    // that hugged its card would make the plugin space its settings cards by a cell
    // and hug everywhere else.
    return (bandBottom > 0.0f) ? (bandBottom + gapY) : (panelTop + frameMarginY);
}

// ---------------------------------------------------------------------------
// Fill complement: the parts of the frame's CENTRE that no band or card covers,
// so a translucent panel carries exactly one fill layer per pixel (see
// BaseHud::finalizeThemedFill for why stacking is visible).
//
// Covers are general rects, not full-width strips: the settings panel draws its
// cards in TWO COLUMNS, and a cover the cut misses sits ON the panel fill and
// composites twice, reading a shade DARKER than the panel where the theme's card
// colour means it to read lighter.
//
// So this cuts against arbitrary cover rects with a SLAB sweep: an x-cut at
// every cover edge makes columns, and within a column every cover either spans
// it or misses it, so each column is a sorted y-interval sweep -- no general
// rectangle boolean. Full-width covers make one slab.
//
// OVERFLOW DEGRADES TO STACKING, NEVER TO A HOLE: if the rects needed exceed
// maxOut, or the covers exceed the internal cap, the whole centre is returned
// as ONE rect -- a uniform tone error -- because a skipped gap would be a hole
// straight through to the game.
// ---------------------------------------------------------------------------
struct FillRect { float l = 0.0f, t = 0.0f, r = 0.0f, b = 0.0f; };

inline int cutFill(const FillRect& centre, const FillRect* covers, int nCovers,
                   FillRect* out, int maxOut) {
    constexpr float EPS = 1e-6f;
    constexpr int MAX_COVERS = 32;
    if (!out || maxOut < 1) return 0;
    if (centre.r - centre.l <= EPS || centre.b - centre.t <= EPS) return 0;

    // Clamp to the centre and drop empties. More covers than the cap is
    // OVERFLOW (the fallback below), never truncation -- dropping a cover would
    // put fill under it, the exact double layer this exists to remove.
    FillRect cs[MAX_COVERS];
    int n = 0;
    bool overflow = false;
    for (int i = 0; i < nCovers; i++) {
        FillRect c = covers[i];
        if (c.l < centre.l) c.l = centre.l;
        if (c.r > centre.r) c.r = centre.r;
        if (c.t < centre.t) c.t = centre.t;
        if (c.b > centre.b) c.b = centre.b;
        if (c.r - c.l <= EPS || c.b - c.t <= EPS) continue;
        if (n == MAX_COVERS) { overflow = true; break; }
        cs[n++] = c;
    }

    int written = 0;
    if (!overflow) {
        // Slab boundaries: the centre's own edges plus every cover edge.
        float xs[2 * MAX_COVERS + 2];
        int nx = 0;
        xs[nx++] = centre.l;
        xs[nx++] = centre.r;
        for (int i = 0; i < n; i++) { xs[nx++] = cs[i].l; xs[nx++] = cs[i].r; }
        for (int i = 1; i < nx; i++) {   // insertion sort; nx is small
            const float v = xs[i];
            int j = i - 1;
            for (; j >= 0 && xs[j] > v; j--) xs[j + 1] = xs[j];
            xs[j + 1] = v;
        }
        for (int s = 0; s + 1 < nx && !overflow; s++) {
            const float x0 = xs[s], x1 = xs[s + 1];
            if (x1 - x0 <= EPS) continue;
            // Covers spanning this slab, as y-intervals sorted by top. Every
            // cover edge is a slab boundary, so a cover either spans the slab
            // or misses it -- there is no partial case to split.
            float lo[MAX_COVERS], hi[MAX_COVERS];
            int m = 0;
            for (int i = 0; i < n; i++) {
                if (cs[i].l > x0 + EPS || cs[i].r < x1 - EPS) continue;
                const float t = cs[i].t, b = cs[i].b;
                int j = m - 1;
                for (; j >= 0 && lo[j] > t; j--) { lo[j + 1] = lo[j]; hi[j + 1] = hi[j]; }
                lo[j + 1] = t;
                hi[j + 1] = b;
                m++;
            }
            // Sweep the gaps. Overlapping covers merge via the cursor max, so a
            // card overhanging the one above it never yields a negative gap.
            float cursor = centre.t;
            for (int i = 0; i <= m && !overflow; i++) {
                const float gapEnd = (i == m) ? centre.b : lo[i];
                if (gapEnd - cursor > EPS) {
                    if (written == maxOut) { overflow = true; break; }
                    out[written++] = { x0, cursor, x1, gapEnd };
                }
                if (i < m && hi[i] > cursor) cursor = hi[i];
            }
        }
    }
    if (overflow) {
        out[0] = centre;
        return 1;
    }
    return written;
}

// Fill `out` (must hold SLICE_COUNT entries) with the tiling of the rect.
inline void build(Slice* out, float x, float y, float w, float h,
                  float borderY, float aspect) {
    const Border in = clampedBorder(w, h, borderY, aspect);
    const float x0 = x, x1 = x + in.x, x2 = x + w - in.x, x3 = x + w;
    const float y0 = y, y1 = y + in.y, y2 = y + h - in.y, y3 = y + h;

    auto put = [&](int i, Slice::Part part, int index,
                   float ax, float ay, float bx, float by,
                   float cx, float cy, float dx, float dy) {
        out[i].part = part;
        out[i].index = index;
        out[i].pos[0][0] = ax; out[i].pos[0][1] = ay;
        out[i].pos[1][0] = bx; out[i].pos[1][1] = by;
        out[i].pos[2][0] = cx; out[i].pos[2][1] = cy;
        out[i].pos[3][0] = dx; out[i].pos[3][1] = dy;
    };

    // Center, stretched both ways. Degenerate (zero-area) when fully clamped,
    // which draws nothing -- deliberately still emitted so the slice count is
    // constant and the in-place update path can assume it.
    put(0, Slice::CENTER, 0, x1, y1, x1, y2, x2, y2, x2, y1);

    // Edges and corners, every one of them u=+x v=+y like the center above: pos[0]
    // is the rect's top-left, and only the rect differs from slice to slice. Read
    // the eight lines as eight rectangles, because that is all they are.
    //
    // THE WINDING RULE, which the uniformity above is what satisfies.
    // SPluginQuad_t's corners are documented counter-clockwise and the game CULLS a
    // quad wound the other way, while the software renderer behind the companion
    // window does not -- so a reversed quad is invisibly wrong on the very surface
    // most likely to be checked. Reorienting a sprite by permuting corners is
    // therefore only ever safe when the permutation is a ROTATION, never a
    // REFLECTION. Nine identical windings sidestep the question, and the handedness
    // case in tests/unit/nine_slice_test.cpp keeps them that way.
    put(1, Slice::EDGE, TOP,    x1, y0, x1, y1, x2, y1, x2, y0);
    put(2, Slice::EDGE, BOTTOM, x1, y2, x1, y3, x2, y3, x2, y2);
    put(3, Slice::EDGE, LEFT,   x0, y1, x0, y2, x1, y2, x1, y1);
    put(4, Slice::EDGE, RIGHT,  x2, y1, x2, y2, x3, y2, x3, y1);

    put(5, Slice::CORNER, TL, x0, y0, x0, y1, x1, y1, x1, y0);
    put(6, Slice::CORNER, TR, x2, y0, x2, y1, x3, y1, x3, y0);
    put(7, Slice::CORNER, BL, x0, y2, x0, y3, x1, y3, x1, y2);
    put(8, Slice::CORNER, BR, x2, y2, x2, y3, x3, y3, x3, y2);
}

}  // namespace NineSlice
