#!/usr/bin/env python3
# ============================================================================
# tools/fontgen/outlines2ttf.py - a sheet of vector glyph OUTLINES -> a .ttf
#
# Turns hand-drawn glyphs (an Illustrator .ai, a PDF, or an SVG: one filled
# shape per glyph, laid out in a row) into a TrueType font that
# mxbmrp3_fontgen then rasterises like any other font:
#
#   python3 tools/fontgen/outlines2ttf.py numbers.ai --chars 1234567890 -o NeonNumbers.ttf
#   tools/fontgen/mxbmrp3_fontgen NeonNumbers.ttf        # -> NeonNumbers.fnt, drop-in
#
# Why it exists: a user designed a number-plate digit set as ARTWORK, not as a
# font, and the plugin loads only .fnt. The .fnt pipeline starts at a .ttf, so
# this is the missing first step - and it stays a separate step on purpose:
# mxbmrp3_fontgen keeps doing one thing (rasterise a font), so the width and
# centring normalisation it applies to every shipped face applies to this one
# unchanged, and the artwork font lands the same size on the plate as the rest.
#
# What was evaluated: FontForge can script SVG imports, but it is a desktop app
# rather than a pip install and wants one SVG file per glyph, so the sheet would
# still need slicing first; Illustrator's font export is a paid plugin (and
# Windows-only, like the game). fontTools (the standard Python font library) plus
# PyMuPDF (reads .ai/PDF/SVG as vector paths) are two pip installs and cover it.
#
# How glyphs are found: every FILLED path on the first page is a candidate; the
# len(--chars) TALLEST are the glyphs (a title or caption on the same sheet is
# smaller), read left to right and assigned to --chars in order. A path's
# subpaths become the glyph's contours, so counters (the hole in a 0) just work,
# and winding is normalised to TrueType's convention (outer clockwise, holes
# anticlockwise) whatever direction they were drawn in.
#
# Metrics: the baseline is the highest bottom edge among the glyphs (a flat-
# bottomed one; round glyphs overshoot below it); the union ink height maps to
# CAP_UNITS of a 1000-unit em; each glyph's advance is the distance to the next
# glyph's left edge IN THE ARTWORK, i.e. the spacing the designer laid the row
# out with. --tracking adds a fraction of the ink height to every advance.
#
# --base <font.ttf|otf> fills every CP1252 character the artwork does not draw
# from another font, scaled so that font's digit height matches the artwork's,
# so a plate face that draws only 0-9 still renders "P1", "#12" or "1/24"
# instead of gaps. Without --base, undrawn characters are blank but keep a
# digit's advance, so a stray character leaves a visible hole rather than
# silently closing up ("1/24" reads as "1 24", never "124").
#
# Test: test_outlines2ttf.sh (the `outlines2ttf` gate) draws a synthetic sheet,
# converts it, rasterises it with mxbmrp3_fontgen and asserts the atlas.
# ============================================================================
import argparse
import os
import statistics
import sys


def die(msg):
    print(f"outlines2ttf: {msg}", file=sys.stderr)
    sys.exit(1)


try:
    import pymupdf
    from fontTools.fontBuilder import FontBuilder
    from fontTools.pens.boundsPen import BoundsPen
    from fontTools.pens.cu2quPen import Cu2QuPen
    from fontTools.pens.recordingPen import DecomposingRecordingPen, RecordingPen
    from fontTools.pens.transformPen import TransformPen
    from fontTools.pens.ttGlyphPen import TTGlyphPen
    from fontTools.ttLib import TTFont
except ImportError as e:  # pragma: no cover - the gate exits 3 before reaching this
    die(f"missing dependency '{e.name}' - python3 -m pip install -r tools/requirements.txt")

UPM = 1000          # em size of the generated font
CAP_UNITS = 700     # the artwork's union ink height, in em units (a typical cap height)
EPS = 1e-3          # point-equality tolerance in page units (pt)


# ---------------------------------------------------------------------------
# Page paths -> contours. A contour is (start, segs); a seg is ('l', p) or
# ('c', c1, c2, p) with every point an (x, y) tuple in PAGE space (y down).
# ---------------------------------------------------------------------------
def _same(a, b):
    return abs(a[0] - b[0]) < EPS and abs(a[1] - b[1]) < EPS


def contours_of(items):
    contours = []
    cur = None
    last = None

    def start(p):
        nonlocal cur, last
        cur = [p, []]
        contours.append(cur)
        last = p

    for it in items:
        kind = it[0]
        if kind == "re":
            r = it[1]
            start((r.x0, r.y0))
            for p in ((r.x1, r.y0), (r.x1, r.y1), (r.x0, r.y1)):
                cur[1].append(("l", p))
            last = None  # a rect is self-contained; whatever follows starts anew
            continue
        if kind == "qu":
            q = it[1]
            pts = [(q.ul.x, q.ul.y), (q.ur.x, q.ur.y), (q.lr.x, q.lr.y), (q.ll.x, q.ll.y)]
            start(pts[0])
            for p in pts[1:]:
                cur[1].append(("l", p))
            last = None
            continue
        p0 = (it[1].x, it[1].y)
        if cur is None or last is None or not _same(p0, last):
            start(p0)
        if kind == "l":
            p = (it[2].x, it[2].y)
            cur[1].append(("l", p))
        elif kind == "c":
            p = (it[4].x, it[4].y)
            cur[1].append(("c", (it[2].x, it[2].y), (it[3].x, it[3].y), p))
        else:
            die(f"unsupported path item {kind!r}")
        last = p

    out = []
    for s, segs in contours:
        # A closing line back to the start is implied by closePath; drop it.
        if segs and segs[-1][0] == "l" and _same(segs[-1][1], s):
            segs = segs[:-1]
        if len(segs) >= 2:
            out.append((s, segs))
    return out


def transform_contour(contour, fx):
    s, segs = contour
    out = []
    for seg in segs:
        if seg[0] == "l":
            out.append(("l", fx(seg[1])))
        else:
            out.append(("c", fx(seg[1]), fx(seg[2]), fx(seg[3])))
    return (fx(s), out)


def flatten(contour, steps=8):
    """Polyline approximation, for area/containment tests only."""
    s, segs = contour
    pts = [s]
    prev = s
    for seg in segs:
        if seg[0] == "l":
            pts.append(seg[1])
            prev = seg[1]
        else:
            c1, c2, p = seg[1], seg[2], seg[3]
            for i in range(1, steps + 1):
                t = i / steps
                u = 1 - t
                pts.append((u * u * u * prev[0] + 3 * u * u * t * c1[0] + 3 * u * t * t * c2[0] + t * t * t * p[0],
                            u * u * u * prev[1] + 3 * u * u * t * c1[1] + 3 * u * t * t * c2[1] + t * t * t * p[1]))
            prev = p
    return pts


def signed_area(poly):
    a = 0.0
    for i in range(len(poly)):
        x0, y0 = poly[i]
        x1, y1 = poly[(i + 1) % len(poly)]
        a += x0 * y1 - x1 * y0
    return a / 2.0


def contains(poly, pt):
    """Ray-casting point-in-polygon."""
    x, y = pt
    inside = False
    n = len(poly)
    for i in range(n):
        x0, y0 = poly[i]
        x1, y1 = poly[(i + 1) % n]
        if (y0 > y) != (y1 > y):
            xi = x0 + (y - y0) * (x1 - x0) / (y1 - y0)
            if x < xi:
                inside = not inside
    return inside


def reverse_contour(contour):
    s, segs = contour
    pts = [s] + [seg[-1] for seg in segs]      # on-curve points, in order
    new_start = pts[-1]
    new_segs = []
    for i in range(len(segs) - 1, -1, -1):
        seg = segs[i]
        end = pts[i]                           # the point this seg started from
        if seg[0] == "l":
            new_segs.append(("l", end))
        else:
            new_segs.append(("c", seg[2], seg[1], end))
    return (new_start, new_segs)


def normalise_winding(contours):
    """TrueType: outer contours clockwise (negative shoelace area in y-up
    coordinates), holes anticlockwise. Nesting depth decides which is which."""
    polys = [flatten(c) for c in contours]
    out = []
    for i, c in enumerate(contours):
        depth = sum(1 for j, p in enumerate(polys) if j != i and contains(p, polys[i][0]))
        want_negative = (depth % 2 == 0)
        area = signed_area(polys[i])
        if (area < 0) != want_negative:
            c = reverse_contour(c)
        out.append(c)
    return out


def draw_contours(contours, pen):
    for s, segs in contours:
        pen.moveTo(s)
        for seg in segs:
            if seg[0] == "l":
                pen.lineTo(seg[1])
            else:
                pen.curveTo(seg[1], seg[2], seg[3])
        pen.closePath()


def bounds_of(rec):
    bp = BoundsPen(None)
    rec.replay(bp)
    return bp.bounds


def to_ttglyph(rec):
    ttpen = TTGlyphPen(None)
    rec.replay(Cu2QuPen(ttpen, max_err=1.0))
    return ttpen.glyph()


# ---------------------------------------------------------------------------
def pick_shapes(page, count):
    cands = []
    for d in page.get_drawings():
        if d.get("fill") is None:
            continue
        r = d["rect"]
        if r.width < EPS or r.height < EPS:
            continue
        cands.append(d)
    if len(cands) < count:
        die(f"found {len(cands)} filled shapes on the page but --chars names {count}")
    cands.sort(key=lambda d: -d["rect"].height)
    picked = cands[:count]
    if len(cands) > count and cands[count]["rect"].height > 0.85 * picked[-1]["rect"].height:
        print(f"outlines2ttf: warning: the {count + 1}th tallest shape is nearly as tall as the "
              f"{count}th ({cands[count]['rect'].height:.1f} vs {picked[-1]['rect'].height:.1f} pt) - "
              "check the glyph assignment", file=sys.stderr)
    picked.sort(key=lambda d: d["rect"].x0)
    return picked


def cp1252_chars():
    out = []
    for b in range(32, 256):
        try:
            out.append(bytes([b]).decode("cp1252"))
        except UnicodeDecodeError:
            pass
    return out


def main():
    ap = argparse.ArgumentParser(description="vector glyph outlines (.ai/.pdf/.svg) -> .ttf")
    ap.add_argument("sheet", help="the artwork: .ai, .pdf or .svg, one filled shape per glyph")
    ap.add_argument("--chars", required=True,
                    help="the characters the shapes draw, in left-to-right order, e.g. 1234567890")
    ap.add_argument("-o", "--out", help="output .ttf (default: <sheet stem>.ttf)")
    ap.add_argument("--name", help="font family name (default: the output stem)")
    ap.add_argument("--base", help="font to fill the rest of CP1252 from, scaled to the artwork's digit height")
    ap.add_argument("--tracking", type=float, default=0.0,
                    help="extra advance on every glyph, as a fraction of the ink height (default 0)")
    args = ap.parse_args()

    chars = args.chars
    if len(set(chars)) != len(chars):
        die("--chars repeats a character")
    for ch in chars:
        try:
            ch.encode("cp1252")
        except UnicodeEncodeError:
            die(f"--chars: {ch!r} is not a CP1252 character, which is all the .fnt format holds")

    out_path = args.out or os.path.splitext(os.path.basename(args.sheet))[0] + ".ttf"
    name = args.name or os.path.splitext(os.path.basename(out_path))[0]

    doc = pymupdf.open(args.sheet)
    page = doc[0]
    shapes = pick_shapes(page, len(chars))

    # Scale: union ink height -> CAP_UNITS. Baseline: the highest bottom edge.
    top = min(d["rect"].y0 for d in shapes)
    bottom = max(d["rect"].y1 for d in shapes)
    baseline = min(d["rect"].y1 for d in shapes)
    scale = CAP_UNITS / (bottom - top)

    # Advance = distance to the next glyph's left edge in the artwork; the last
    # glyph gets its width plus the median gap the others were given.
    x0s = [d["rect"].x0 for d in shapes]
    widths = [d["rect"].width * scale for d in shapes]
    advances = [(x0s[i + 1] - x0s[i]) * scale for i in range(len(shapes) - 1)]
    if advances:
        gap = statistics.median(advances[i] - widths[i] for i in range(len(advances)))
        advances.append(widths[-1] + gap)
    else:
        advances.append(widths[0] * 1.1)
    tracking = args.tracking * CAP_UNITS

    glyphs, metrics, cmap = {}, {}, {}
    order = [".notdef"]
    recs = {}
    for ch, d, adv in zip(chars, shapes, advances):
        gx0 = d["rect"].x0

        def fx(p, gx0=gx0):
            return ((p[0] - gx0) * scale, (baseline - p[1]) * scale)

        contours = normalise_winding([transform_contour(c, fx) for c in contours_of(d["items"])])
        rec = RecordingPen()
        draw_contours(contours, rec)
        gname = f"uni{ord(ch):04X}"
        recs[gname] = rec
        b = bounds_of(rec)
        glyphs[gname] = to_ttglyph(rec)
        metrics[gname] = (int(round(adv + tracking)), int(round(b[0])) if b else 0)
        cmap[ord(ch)] = gname
        order.append(gname)
        print(f"outlines2ttf: {ch!r}: {len(contours)} contour(s), advance {metrics[gname][0]}", file=sys.stderr)

    digit_adv = statistics.median(m[0] for m in metrics.values())

    if args.base:
        bf = TTFont(args.base)
        gs = bf.getGlyphSet()
        bcmap = bf.getBestCmap()
        btop, bbot = -1e9, 1e9
        for dch in "0123456789":
            gn = bcmap.get(ord(dch))
            if not gn:
                continue
            bp = BoundsPen(gs)
            gs[gn].draw(bp)
            if bp.bounds:
                bbot = min(bbot, bp.bounds[1])
                btop = max(btop, bp.bounds[3])
        if btop <= bbot:
            die(f"--base {args.base}: no digits to match the height by")
        bscale = CAP_UNITS / (btop - bbot)
        filled = 0
        for ch in cp1252_chars():
            if ch in chars or ord(ch) in cmap:
                continue
            gn = bcmap.get(ord(ch))
            if not gn:
                continue
            drec = DecomposingRecordingPen(gs)
            gs[gn].draw(drec)
            rec = RecordingPen()
            drec.replay(TransformPen(rec, (bscale, 0, 0, bscale, 0, 0)))
            gname = f"uni{ord(ch):04X}"
            b = bounds_of(rec)
            glyphs[gname] = to_ttglyph(rec)
            metrics[gname] = (int(round(gs[gn].width * bscale)), int(round(b[0])) if b else 0)
            cmap[ord(ch)] = gname
            order.append(gname)
            recs[gname] = rec
            filled += 1
        print(f"outlines2ttf: filled {filled} characters from {os.path.basename(args.base)} "
              f"(scaled x{bscale:.3f} to match the digit height)", file=sys.stderr)

    if 32 not in cmap:
        glyphs["space"] = TTGlyphPen(None).glyph()
        metrics["space"] = (int(round(digit_adv * 0.5)), 0)
        cmap[32] = "space"
        order.append("space")

    # .notdef: blank, a digit's advance - so an undrawn character leaves a hole.
    glyphs[".notdef"] = TTGlyphPen(None).glyph()
    metrics[".notdef"] = (int(round(digit_adv)), 0)

    all_bounds = [bounds_of(r) for r in recs.values()]
    all_bounds = [b for b in all_bounds if b]
    ascent = int(round(max(b[3] for b in all_bounds))) + 50
    descent = int(round(min(b[1] for b in all_bounds))) - 50
    descent = min(descent, -50)

    fb = FontBuilder(UPM, isTTF=True)
    fb.setupGlyphOrder(order)
    fb.setupCharacterMap(cmap)
    fb.setupGlyf(glyphs)
    fb.setupHorizontalMetrics(metrics)
    fb.setupHorizontalHeader(ascent=ascent, descent=descent)
    fb.setupNameTable({"familyName": name, "styleName": "Regular"})
    fb.setupOS2(sTypoAscender=ascent, sTypoDescender=descent, sTypoLineGap=0,
                usWinAscent=ascent, usWinDescent=-descent)
    fb.setupPost()
    fb.save(out_path)
    print(f"outlines2ttf: wrote {out_path} ({len(order)} glyphs)", file=sys.stderr)


if __name__ == "__main__":
    main()
