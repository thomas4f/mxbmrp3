#!/usr/bin/env python3
# ============================================================================
# tools/analytics_svg.py
# Tiny, dependency-free SVG chart helpers for tools/analytics_report.py.
#
# Everything here emits a self-contained <svg> string with deterministic output
# (no timestamps, no randomness) so the committed charts diff cleanly. Charts
# are theme-aware: a <style> block swaps text/grid colours via
# prefers-color-scheme, so they read on both the light and dark GitHub themes
# when embedded as <img>. Data-series colours are chosen to work on either.
#
# No matplotlib / no external chart lib on purpose -- these are simple, and the
# repo already hand-rolls its SVG assets (see tools/icon_gen.py).
# ============================================================================
import json
import math
from html import escape

# Series palette (readable on both light and dark backgrounds).
PALETTE = [
    "#3fb950",  # green
    "#58a6ff",  # blue
    "#d29922",  # amber
    "#bc8cff",  # purple
    "#f778ba",  # pink
    "#39c5cf",  # teal
    "#ff7b72",  # red
    "#a5d6ff",  # light blue
    "#7ee787",  # light green
    "#ffa657",  # orange
]

# ONE PALETTE, NO DARK-MODE BLOCK. GitHub shows these charts as <img>, and an
# image answers prefers-color-scheme from the OS, not from the GitHub theme the
# page is drawn in: with a dark OS and a light GitHub theme the old dark block
# painted near-white titles on a white page. So every neutral is #808080, the
# tint the README's icons carry, which reads on both surfaces (3.9:1 on white,
# 5.3:1 on GitHub's dark), and the bars keep their categorical hues, which do too.
#
# TIER RAMP (.t1-.t4). A tier is ORDINAL - swapping Bronze and Platinum would
# change the meaning - so it takes one hue stepped light-to-dark rather than four
# categorical hues, and the reader sees the order in the colour. Literal metal
# colours do not survive this report: silver and platinum land within a few
# percent of each other, and platinum is very nearly the light GitHub background.
# The ramp is chosen to pass the ordinal checks on both surfaces: monotone
# lightness, adjacent dL >= 0.06, every step >= 2:1 against white and dark alike.

# Stable colours for the supported games, so a game keeps its colour everywhere.
GAME_COLORS = {
    "MX Bikes": "#3fb950",
    "GP Bikes": "#58a6ff",
    "Kart Racing Pro": "#d29922",
    "WRS": "#bc8cff",
}

_STYLE = """<style>
  text{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,Helvetica,Arial,sans-serif}
  .grid{stroke:#808080;stroke-opacity:.3;stroke-width:1}
  .axis{stroke:#808080;stroke-width:1}
  .lbl{fill:#808080;font-size:12px}
  .val{fill:#808080;font-size:12px;font-weight:600}
  .title{fill:#808080;font-size:14px;font-weight:700}
  .sub{fill:#808080;font-size:11px}
  .gap{fill:#808080;opacity:.12}
  .gaplbl{fill:#808080;font-size:10px;opacity:.85}
  .ico{fill:#808080}
  .t1{fill:#e3b341}
  .t2{fill:#d29922}
  .t3{fill:#b7860b}
  .t4{fill:#946f0f}
</style>"""


def _svg(w, h, body, title="", data=None):
    # The chart's title is ALSO its accessible name: a <title> child and an
    # aria-label, so the image is not a nameless role="img" to a screen reader.
    # It is the one place the title lives -- Report.chart reads it back as the
    # Markdown alt text, so the two cannot say different things.
    #
    # HOVER DATA. `data` (a line chart's points) rides on the root as data-chart,
    # and bars carry their own data-tip. Both are inert where GitHub shows the
    # chart as an <img>; the Pages report (tools/analytics_page.py) inlines the
    # SVG and its script reads them to follow the cursor. Plain attributes, so a
    # chart stays one self-contained, deterministic file either way.
    t = escape(title)
    extra = ""
    if data is not None:
        # Single-quoted so the JSON's own quotes need no escaping (a third of the bytes).
        js = escape(json.dumps(data, separators=(",", ":"), sort_keys=True), quote=False)
        extra = " data-chart='{}'".format(js.replace("'", "&#x27;"))
    return (
        '<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" '
        'viewBox="0 0 {w} {h}" role="img" aria-label="{t}"{extra}><title>{t}</title>{style}{body}</svg>'
    ).format(w=w, h=h, t=t, extra=extra, style=_STYLE, body=body)


def _tip(*parts, note=None):
    """data-tip attribute for one hoverable mark: its parts joined by ": ", then
    an optional second line. The line break is written as &#10; because a raw
    newline in an attribute is folded to a space when the file is read as XML."""
    text = escape(": ".join(str(p) for p in parts if p != ""))
    if note:
        text += "&#10;" + "&#10;".join(escape(line) for line in note.split("\n"))
    return ' data-tip="{}"'.format(text)


def _fmt(v):
    """Compact human number: 12345 -> 12,345 ; 3.4 -> 3.4."""
    if isinstance(v, float) and not v.is_integer():
        return "{:,.1f}".format(v)
    return "{:,}".format(int(round(v)))


def pctstr(count, total):
    """'46%', '<1%' for a nonzero share below 1%, '>99%' for a share above 99%
    that isn't the whole (so '100%' only ever means literally all)."""
    p = (100.0 * count / total) if total else 0.0
    if count > 0 and p < 1.0:
        return "<1%"
    if count < total and p > 99.0:
        return ">99%"
    return "{:.0f}%".format(p)


def pc(count, total):
    """'46% (1,606)' - share, then its count: THE report's one shape for a count
    with its share, in charts, tables, text and hover readouts alike. (Count
    charts used to lead with the count, "1,606 (46%)", on the grounds that their
    bar is a count; the reader saw two orders for the same pair, not the reason.)
    When the base differs from row to row, the report's adoption_annot adds it:
    '46% (1,606 of 3,491)'."""
    return "{} ({:,})".format(pctstr(count, total), int(count))


def _value_pad(annotations):
    """Right margin for bar-end values: the widest one, at .val's 12px bold
    (~7.4px a character), plus the 6px gap before it. A fixed 96px cut the longer
    count-and-share labels off at the frame ("100% (2,868 of 2")."""
    return max(96, 18 + int(math.ceil(7.4 * max((len(a) for a in annotations), default=0))))


def hbar(title, rows, subtitle="", value_fmt=_fmt, width=760, label_w=210):
    """Horizontal bar chart.

    rows: (label, value[, color[, annotation]]). Drawn top-to-bottom in the
    given order (caller sorts). Bar length scales to the max value. The optional
    4th element is the text drawn at the bar end (e.g. "46% (1,606)"); it
    overrides value_fmt, so a chart can show count and percentage together.
    """
    pad_l, pad_t = label_w, 44 if subtitle else 34
    pad_r = _value_pad([r[3] if len(r) > 3 else value_fmt(r[1]) for r in rows])
    row_h, gap = 22, 8
    n = len(rows)
    h = pad_t + n * (row_h + gap) + 12
    plot_w = width - pad_l - pad_r
    vmax = max([r[1] for r in rows], default=0) or 1
    parts = ['<text x="12" y="20" class="title">{}</text>'.format(escape(title))]
    if subtitle:
        parts.append('<text x="12" y="37" class="sub">{}</text>'.format(escape(subtitle)))
    for i, r in enumerate(rows):
        label, val = r[0], r[1]
        color = r[2] if len(r) > 2 and r[2] else PALETTE[i % len(PALETTE)]
        annot = r[3] if len(r) > 3 else value_fmt(val)
        y = pad_t + i * (row_h + gap)
        bw = max(1, int(plot_w * (val / vmax)))
        parts.append(
            '<text x="{x}" y="{ty}" class="lbl" text-anchor="end">{lab}</text>'.format(
                x=pad_l - 10, ty=y + row_h - 6, lab=escape(str(label))
            )
        )
        parts.append(
            '<rect x="{x}" y="{y}" width="{bw}" height="{rh}" rx="3" fill="{c}"{t}/>'.format(
                x=pad_l, y=y, bw=bw, rh=row_h, c=color, t=_tip(label, annot)
            )
        )
        parts.append(
            '<text x="{x}" y="{ty}" class="val">{v}</text>'.format(
                x=pad_l + bw + 6, ty=y + row_h - 6, v=escape(annot)
            )
        )
    return _svg(width, h, "".join(parts), title)


def stacked_hbar(title, rows, legend, base, subtitle="", width=760, label_w=210,
                 icon_size=15):
    """Horizontal bars split into ordered segments, one row per category.

    rows:   (label, icon, segments, annotation[, about]), where `segments` is
            [(css_class, value[, note[, tier_name]])] in tier order -- the
            optional note is a second line on the segment's hover readout (an
            achievement tier's requirement), and tier_name replaces the legend's
            name for it ("" for none: a one-shot has no Bronze) -- `about` is
            the hover readout on the row's label, and `icon` is an (viewbox, path)
            pair or None. Row length is the segment TOTAL, so the bars still
            compare like an ordinary hbar; the split adds where inside that total
            each row sits.
    legend: [(css_class, name)] - always drawn, because identity must never rest
            on colour alone, and a static SVG has no hover to fall back on.
    base:   what a segment's value is a share OF. Values are counts; a segment's
            hover readout gives its share of base and the count, as pc() does
            ("Bronze 5% (217)"), the same shape as the row's annotation.

    Segments are separated by a 2px gap of surface rather than butted together,
    and the whole bar is clipped to one rounded rect so only the outer end is
    round - a rounded rect per segment reads as separate bars.
    """
    pad_l, pad_r = label_w, _value_pad([r[3] for r in rows])
    pad_t = (44 if subtitle else 34) + 22          # + legend row
    row_h, gap = 20, 8
    h = pad_t + len(rows) * (row_h + gap) + 12
    plot_w = width - pad_l - pad_r
    vmax = max([sum(seg[1] for seg in r[2]) for r in rows], default=0) or 1
    names = dict(legend)
    parts = ['<text x="12" y="20" class="title">{}</text>'.format(escape(title))]
    if subtitle:
        parts.append('<text x="12" y="37" class="sub">{}</text>'.format(escape(subtitle)))
    ly = (52 if subtitle else 42)
    lx = 12
    for cls, name in legend:
        parts.append('<rect x="{x}" y="{y}" width="11" height="11" rx="2" class="{c}"/>'.format(
            x=lx, y=ly - 9, c=cls))
        parts.append('<text x="{x}" y="{y}" class="lbl">{n}</text>'.format(
            x=lx + 16, y=ly, n=escape(name)))
        lx += 30 + 7.2 * len(name)
    for i, row in enumerate(rows):
        label, icon, segments, annot = row[:4]
        about = row[4] if len(row) > 4 else None
        y = pad_t + i * (row_h + gap)
        total = sum(seg[1] for seg in segments)
        # Icon and label LEFT-aligned as one unit, unlike the right-aligned labels on
        # the plain hbars. An icon pinned to the left margin beside a right-aligned
        # label leaves a gap the width of the longest name, and the pairing stops
        # reading; left-aligned, the row scans icon -> name -> bar.
        if icon:
            mx, my, box, path = icon
            sc = icon_size / float(box)
            parts.append(
                '<g transform="translate({x:.2f},{y:.2f}) scale({s:.5f})">'
                '<path d="{d}" class="ico"/></g>'.format(
                    x=12 - mx * sc, y=y + (row_h - icon_size) / 2.0 - my * sc, s=sc, d=path))
        parts.append(
            '<text x="{x}" y="{ty}" class="lbl"{t}>{lab}</text>'.format(
                x=12 + icon_size + 7, ty=y + row_h - 5, lab=escape(str(label)),
                t=_tip(label, note=about) if about else ""))
        bw = max(1, plot_w * (total / vmax))
        parts.append(
            '<clipPath id="b{i}"><rect x="{x}" y="{y}" width="{w:.1f}" height="{h}" '
            'rx="4"/></clipPath>'.format(i=i, x=pad_l, y=y, w=bw, h=row_h))
        parts.append('<g clip-path="url(#b{i})">'.format(i=i))
        sx = float(pad_l)
        for seg in segments:
            cls, val = seg[0], seg[1]
            if val <= 0:
                continue
            sw = plot_w * (val / vmax)
            parts.append(
                '<rect x="{x:.1f}" y="{y}" width="{w:.1f}" height="{h}" class="{c}"{t}/>'.format(
                    x=sx, y=y, w=max(0.0, sw - 2), h=row_h, c=cls,
                    t=_tip(label, "{} {}".format(seg[3] if len(seg) > 3 else names.get(cls, cls),
                                                 pc(val, base)).strip(),
                           note=seg[2] if len(seg) > 2 else None)))
            sx += sw
        parts.append("</g>")
        parts.append('<text x="{x:.1f}" y="{ty}" class="val">{v}</text>'.format(
            x=pad_l + bw + 6, ty=y + row_h - 5, v=escape(annot)))
    return _svg(width, h, "".join(parts), title)


def vbars(title, cats, subtitle="", value_fmt=_fmt, width=760, height=300):
    """Vertical bar chart / histogram. cats: list of (label, count). The buckets
    partition one population, so a bar's hover readout gives its share of the
    total and its count, as pc() does."""
    # The axis top is rounded and the ticks are exact, like the line chart's:
    # the raw maximum used to be the top, so the top label collided with the
    # subtitle and the half-way label read "26,309.5". The left pad grows with
    # the widest label, so a five-digit axis is not cut off at the edge.
    vmax = _nice_max(max([c[1] for c in cats], default=0) or 1)
    ticks = [t for t in _linear_ticks(vmax) if t > 0]
    pad_l = max(46, 12 + 7 * max(len(value_fmt(t)) for t in ticks))
    # The top tick's label sits half a line above the top gridline, so the plot
    # starts a line below the subtitle rather than where the label runs into it.
    pad_r, pad_t, pad_b = 16, 58 if subtitle else 40, 40
    n = len(cats)
    plot_w = width - pad_l - pad_r
    plot_h = height - pad_t - pad_b
    total = sum(c[1] for c in cats)
    slot = plot_w / max(1, n)
    bw = max(3, slot * 0.72)
    parts = ['<text x="12" y="20" class="title">{}</text>'.format(escape(title))]
    if subtitle:
        parts.append('<text x="12" y="37" class="sub">{}</text>'.format(escape(subtitle)))
    base = pad_t + plot_h
    parts.append('<line x1="{a}" y1="{y}" x2="{b}" y2="{y}" class="axis"/>'.format(
        a=pad_l, b=width - pad_r, y=base))
    for t in ticks:
        gy = base - plot_h * (t / vmax)
        parts.append('<line x1="{a}" y1="{y}" x2="{b}" y2="{y}" class="grid" stroke-dasharray="3 3"/>'.format(
            a=pad_l, b=width - pad_r, y=gy))
        parts.append('<text x="{x}" y="{y}" class="sub" text-anchor="end">{v}</text>'.format(
            x=pad_l - 6, y=gy + 4, v=escape(value_fmt(t))))
    for i, (label, val) in enumerate(cats):
        x = pad_l + i * slot + (slot - bw) / 2
        bh = plot_h * (val / vmax)
        y = base - bh
        parts.append('<rect x="{x:.1f}" y="{y:.1f}" width="{bw:.1f}" height="{bh:.1f}" rx="2" fill="{c}"{t}/>'.format(
            x=x, y=y, bw=bw, bh=bh, c=PALETTE[0], t=_tip(label, pc(val, total))))
        parts.append('<text x="{x:.1f}" y="{y}" class="sub" text-anchor="middle">{v}</text>'.format(
            x=x + bw / 2, y=base + 14, v=escape(str(label))))
    return _svg(width, height, "".join(parts), title)


def _nice_max(v):
    """Round an axis top up to a nice-ish number, shared by the line and bar
    charts. The ladder is dense enough that no series is drawn against a top
    nearly twice its peak: 11,348 installs draw to 15,000 rather than 20,000,
    and 52,619 sessions to 60,000 rather than 100,000, which left the upper
    half of the plot empty. Every step quarters to an exact tick label."""
    if v <= 0:
        return 1
    mag = 10 ** math.floor(math.log10(v))
    for m in (1, 1.5, 2, 2.5, 3, 4, 5, 6, 8, 10):
        if v <= m * mag:
            return m * mag
    return 10 * mag


def _polyline(pts, color):
    return ('<polyline points="{p}" fill="none" stroke="{c}" stroke-width="2" '
            'stroke-linejoin="round" stroke-linecap="round"/>'
            .format(p=" ".join("{:.1f},{:.1f}".format(x, y) for x, y in pts), c=color))


def _linear_ticks(vmax):
    """Evenly spaced gridlines whose labels are EXACT at the one decimal _fmt prints.

    Quartering the top is right for a round vmax and wrong otherwise: on the
    launches-per-active axis (vmax 5) it put gridlines at 1.25 and 3.75, which
    _fmt rendered "1.2" and "3.8" -- two labels naming no line on the chart, and
    disagreeing about which way they round. Picking the interval COUNT instead of
    fixing it at four keeps the same look where it already worked (100 -> 25s,
    10 -> 2.5s, both unchanged) and lands 5 on whole numbers.
    """
    if not (vmax > 0):
        return [0]
    for n in (4, 5, 3, 6, 8):
        step = vmax / float(n)
        if abs(step - round(step, 1)) < 1e-9:
            return [vmax * i / float(n) for i in range(n + 1)]
    return [vmax * frac for frac in (0, 0.25, 0.5, 0.75, 1.0)]


def lines(title, x_labels, series, subtitle="", value_fmt=_fmt, width=760, height=320,
          x_tick_every=None, log=False, gaps=(), annotate=True):
    """Multi-series line chart.

    x_labels: list of tick labels (one per x index).
    series: list of (name, [y values], color). All y-lists share the x axis.
             A y value of None means NO OBSERVATION, which is not zero: the line
             breaks there rather than diving to the axis and climbing back out.

    gaps: [(i0, i1, label)] index ranges with no data, shaded and labelled. The x
          axis keeps those positions rather than closing them up, so a ten-day
          outage reads as ten days of silence instead of two adjacent days that
          happen to sit either side of it. See analytics_report.collection_gaps.

    log=True puts the y axis on a base-10 scale, for series whose magnitudes differ
    by orders of magnitude -- without it the small ones are pinned to the axis and
    unreadable (MX Bikes runs ~5,000 launches/day against Kart Racing Pro's ~20).

    It scales log10(1 + v), NOT log10(v), because these are COUNTS and counts reach
    zero: Kart Racing Pro has days with no launches at all, and a plain log axis
    cannot place them. log1p maps 0 to the axis floor honestly, is monotonic, and
    needs no special case in the series loop -- the alternative (dropping or
    clamping zeros) either breaks the line or draws a zero as if it were a one.
    Gridlines are decades, so the labels stay in real units.

    annotate=True writes each series' LAST observed value at the end of its line,
    and marks its PEAK with a dot and the value when the peak is not the end. On a
    log axis a curve's level and slope are unreadable by eye -- a cumulative count
    ending at 5,500 and one ending at 9,000 sit a few pixels apart -- and the
    reader was left to guess the one number the chart exists to show. End labels
    that would overprint are nudged apart down the right margin. annotate="ends"
    writes the end values only: a retention curve starts at its peak by
    definition, and a dot saying "100%" at day 0 is noise. annotate="legend"
    keeps the peaks but puts each END value in the legend ("1.30.3 · 49%") instead
    of the margin: with many series ending near each other (the version chart) the
    margin became a column of small numbers matched to lines by colour alone.
    """
    pad_l, pad_r, pad_t, pad_b = 52, 16, (74 if subtitle else 56), 46
    plain_names = [ser[0] for ser in series]  # before annotate="legend" suffixes them
    # Room on the right for the end labels: a text column as wide as the widest one.
    ends = []   # (series index, last observed x index, value)
    peaks = []  # (series index, x index of the max, value)
    if annotate:
        for si, (_name, ys, _color) in enumerate(series):
            obs = [(i, v) for i, v in enumerate(ys or []) if v is not None]
            if not obs:
                continue
            ends.append((si, obs[-1][0], obs[-1][1]))
            pi, pv = max(obs, key=lambda iv: iv[1])
            if annotate in (True, "legend") and pv > 0 and pi < obs[-1][0] - 1:
                peaks.append((si, pi, pv))
        if annotate == "legend":
            series = [(name + next((" · " + value_fmt(v) for si, _i, v in ends if si == k), ""), ys, c)
                      for k, (name, ys, c) in enumerate(series)]
            ends = []
        elif ends:
            pad_r += 6 + 7 * max(len(value_fmt(v)) for _si, _i, v in ends)
    # LEGEND FIRST, because it decides how much room the plot has. One row was fine at
    # four series and ran off the right edge at seven (the version chart, once a second
    # month of data brought more releases into view) - the last entry simply vanished
    # past the frame, with nothing to say a series was missing from the key.
    legend_rows, row, row_w = [], [], 0.0
    for name, _ys, color in series:
        w = 22 + 8 * len(name) + 20
        if row and row_w + w > width - pad_l - pad_r:
            legend_rows.append(row)
            row, row_w = [], 0.0
        row.append((name, color))
        row_w += w
    if row:
        legend_rows.append(row)
    # Grow the CANVAS by what the extra legend rows take, so the plot keeps its height
    # instead of being squeezed by its own key.
    extra = 18 * max(0, len(legend_rows) - 1)
    pad_t += extra
    height += extra
    plot_w = width - pad_l - pad_r
    plot_h = height - pad_t - pad_b
    n = max((len(s[1]) for s in series), default=0)
    vmax = max((max(v for v in s[1] if v is not None) for s in series
                if any(v is not None for v in s[1])), default=0) or 1
    if log:
        # Round the top up to a whole decade so the highest gridline is a real tick.
        top = 10 ** int(math.ceil(math.log10(vmax))) if vmax > 0 else 1
        lmax = math.log10(1 + top) or 1.0
    else:
        vmax = _nice_max(vmax)
    parts = ['<text x="12" y="20" class="title">{}</text>'.format(escape(title))]
    if subtitle:
        parts.append('<text x="12" y="37" class="sub">{}</text>'.format(escape(subtitle)))
    base = pad_t + plot_h

    def px(i):
        return pad_l + (plot_w * (i / max(1, n - 1)) if n > 1 else plot_w / 2)

    def py(v):
        if log:
            return base - plot_h * (math.log10(1 + max(0, v)) / lmax)
        return base - plot_h * (v / vmax)

    # NO-DATA BANDS, behind everything else. Half a step either side so the band
    # covers the missing days themselves rather than only the ticks.
    half = (plot_w / max(1, n - 1)) / 2 if n > 1 else plot_w / 2
    for g in gaps or ():
        i0, i1 = g[0], g[1]
        label = g[2] if len(g) > 2 else "no data"
        x0, x1 = px(i0) - half, px(i1) + half
        parts.append('<rect x="{x:.1f}" y="{y}" width="{w:.1f}" height="{h}" class="gap"/>'.format(
            x=x0, y=pad_t, w=max(1.0, x1 - x0), h=plot_h))
        if x1 - x0 > 7 * len(label):
            parts.append('<text x="{x:.1f}" y="{y}" class="gaplbl" text-anchor="middle">{t}</text>'
                         .format(x=(x0 + x1) / 2, y=pad_t + 12, t=escape(label)))

    # horizontal gridlines + y labels
    if log:
        ticks = [0] + [10 ** k for k in range(0, int(round(math.log10(top))) + 1)]
    else:
        ticks = _linear_ticks(vmax)
    for tv in ticks:
        gy = py(tv)
        parts.append('<line x1="{a}" y1="{y:.1f}" x2="{b}" y2="{y:.1f}" class="grid" stroke-dasharray="3 3"/>'.format(
            a=pad_l, b=width - pad_r, y=gy))
        parts.append('<text x="{x}" y="{y:.1f}" class="sub" text-anchor="end">{v}</text>'.format(
            x=pad_l - 6, y=gy + 4, v=escape(value_fmt(tv))))
    # x tick labels
    if x_tick_every is None:
        x_tick_every = max(1, n // 8)
    # The last label is forced so the axis states where it ends - but only when it is
    # far enough from the previous tick to be read: at 68 days it landed on top of it
    # and the two dates printed through each other ("0891-01").
    last_ok = n > 1 and ((n - 1) % x_tick_every) > x_tick_every / 2
    for i, lab in enumerate(x_labels):
        if i % x_tick_every == 0 or (i == n - 1 and last_ok):
            parts.append('<text x="{x:.1f}" y="{y}" class="sub" text-anchor="middle">{v}</text>'.format(
                x=px(i), y=base + 16, v=escape(str(lab))))
    # series polylines, one per RUN of observed points: a None breaks the line.
    for _name, ys, color in series:
        if not ys:
            continue
        run = []
        for i, v in enumerate(ys):
            if v is None:
                if run:
                    parts.append(_polyline(run, color))
                    run = []
                continue
            run.append((px(i), py(v)))
        if run:
            parts.append(_polyline(run, color))
    # END VALUES, down the right margin in the series' colour, nudged apart so two
    # lines ending together (a log axis makes that the norm) both stay legible.
    if ends:
        # Two passes, so a label moves only when its neighbours leave it no room.
        # The old single pass stacked downward and then slid the WHOLE column up
        # when it overflowed: on the version chart, eight lines ending near 0%
        # pushed the 41% and 27% labels forty points above their own lines. The
        # first pass pushes down, the second pushes back up from the bottom; a
        # label that still ends up displaced gets a leader line to its point.
        placed = sorted(((py(v), si, i, v) for si, i, v in ends), key=lambda t: t[0])
        ys_out, lo = [], pad_t + 8
        for y, si, i, v in placed:
            y = max(y, lo)
            ys_out.append([y, si, i, v])
            lo = y + 12
        hi = base + 4
        for entry in reversed(ys_out):
            entry[0] = min(entry[0], hi)
            hi = entry[0] - 12
        for y, si, i, v in ys_out:
            if abs(y - py(v)) > 5:
                parts.append('<line x1="{x0:.1f}" y1="{y0:.1f}" x2="{x1:.1f}" y2="{y1:.1f}" stroke="{c}" '
                             'stroke-width="1" stroke-opacity="0.5"/>'
                             .format(x0=px(i) + 2, y0=py(v), x1=px(i) + 5, y1=y, c=series[si][2]))
            parts.append('<text x="{x:.1f}" y="{y:.1f}" style="fill:{c};font-size:11px;font-weight:600">{t}</text>'
                         .format(x=px(i) + 6, y=y + 4, c=series[si][2], t=escape(value_fmt(v))))
    # PEAKS: a dot on the point and the value above it, kept inside the plot. Peaks
    # that share an x (every series peaking in the same first cohort) stack upward
    # rather than print through each other.
    placed_peaks = []
    for si, i, v in sorted(peaks, key=lambda t: (px(t[1]), py(t[2]))):
        x, y = px(i), py(v)
        parts.append('<circle cx="{x:.1f}" cy="{y:.1f}" r="3" fill="{c}"/>'.format(x=x, y=y, c=series[si][2]))
        anchor = "middle"
        if x < pad_l + 24: anchor = "start"
        if x > pad_l + plot_w - 24: anchor = "end"
        ly_ = max(pad_t + 10, y - 7)
        for _attempt in range(6):
            clash = [oy for ox, oy in placed_peaks if abs(ox - x) < 24 and abs(oy - ly_) < 11]
            if not clash:
                break
            up = min(clash) - 11
            ly_ = up if up >= pad_t + 10 else max(clash) + 11   # no room above: go below
        placed_peaks.append((x, ly_))
        parts.append('<text x="{x:.1f}" y="{y:.1f}" text-anchor="{a}" style="fill:{c};font-size:10px">{t}</text>'
                     .format(x=x, y=ly_, a=anchor, c=series[si][2], t=escape(value_fmt(v))))
    # legend (its own rows, below the title/subtitle so nothing overlaps)
    ly = 54 if subtitle else 40
    for r_i, r_entries in enumerate(legend_rows):
        lx = pad_l
        y = ly + 18 * r_i
        for name, color in r_entries:
            parts.append('<rect x="{x}" y="{y}" width="11" height="11" rx="2" fill="{c}"/>'.format(
                x=lx, y=y - 10, c=color))
            parts.append('<text x="{x}" y="{y}" class="lbl">{n}</text>'.format(
                x=lx + 16, y=y, n=escape(name)))
            lx += 22 + 8 * len(name) + 20
    # What the Pages report's cursor readout needs, in the chart's own coordinates:
    # where each day sits, and each series' point and formatted value there (None
    # where the line breaks). The script never re-derives a scale, so it cannot
    # disagree with the drawing.
    data = {"type": "lines", "top": pad_t, "bottom": base,
            "x": [round(px(i), 1) for i in range(n)],
            "labels": [str(x_labels[i]) if i < len(x_labels) else "" for i in range(n)],
            "series": [{"name": plain_names[k], "color": color,
                        "values": [None if i >= len(ys) or ys[i] is None else value_fmt(ys[i]) for i in range(n)],
                        "y": [None if i >= len(ys) or ys[i] is None else round(py(ys[i]), 1) for i in range(n)]}
                       for k, (_name, ys, color) in enumerate(series)]}
    return _svg(width, height, "".join(parts), title, data)


def stacked_bar(title, segments, subtitle="", width=760, value_fmt=_fmt):
    """Single 100%-stacked horizontal bar. segments: [(label, value, color?)]."""
    pad_l, pad_r, pad_t = 12, 12, 44 if subtitle else 30
    bar_y, bar_h = pad_t, 30
    total = sum(s[1] for s in segments) or 1
    plot_w = width - pad_l - pad_r
    parts = ['<text x="12" y="20" class="title">{}</text>'.format(escape(title))]
    if subtitle:
        parts.append('<text x="12" y="37" class="sub">{}</text>'.format(escape(subtitle)))
    x = pad_l
    legend_y = bar_y + bar_h + 24
    lx = pad_l
    for i, seg in enumerate(segments):
        label, val = seg[0], seg[1]
        color = seg[2] if len(seg) > 2 else PALETTE[i % len(PALETTE)]
        w = plot_w * (val / total)
        parts.append('<rect x="{x:.1f}" y="{y}" width="{w:.1f}" height="{h}" fill="{c}"{t}/>'.format(
            x=x, y=bar_y, w=w, h=bar_h, c=color,
            t=_tip(label, pc(val, total))))
        if w > 44:
            pct = 100.0 * val / total
            parts.append('<text x="{x:.1f}" y="{y}" class="val" text-anchor="middle" '
                         'style="fill:#0d1117">{v}%</text>'.format(
                             x=x + w / 2, y=bar_y + 20, v=("%.0f" % pct)))
        x += w
        # legend chip
        lab = "{} ({})".format(label, value_fmt(val))
        parts.append('<rect x="{x}" y="{y}" width="11" height="11" rx="2" fill="{c}"/>'.format(
            x=lx, y=legend_y - 10, c=color))
        parts.append('<text x="{x}" y="{y}" class="lbl">{n}</text>'.format(
            x=lx + 16, y=legend_y, n=escape(lab)))
        lx += 30 + 7.2 * len(lab)
    return _svg(width, legend_y + 12, "".join(parts), title)


def heatmap(title, row_labels, col_labels, values, subtitle="", value_fmt=_fmt, width=760,
            color=None, col_tick_every=None):
    """Grid of cells shaded on ONE hue by value: values[row][col], None = no data (a
    gap cell). One hue rather than a rainbow because the reader's question is "where is
    it busy", which is a lightness question; the max cell carries its value so the
    scale has a stated top. Used for launches by weekday and hour."""
    color = color or PALETTE[0]
    pad_l, pad_r, pad_t, pad_b = 52, 16, (44 if subtitle else 34), 28
    n_r, n_c = len(row_labels), len(col_labels)
    plot_w = width - pad_l - pad_r
    cw, ch = plot_w / max(1, n_c), 22
    height = pad_t + n_r * ch + pad_b
    vmax = max((v for row in values for v in row if v is not None), default=0) or 1
    parts = ['<text x="12" y="20" class="title">{}</text>'.format(escape(title))]
    if subtitle:
        parts.append('<text x="12" y="37" class="sub">{}</text>'.format(escape(subtitle)))
    for ri, lab in enumerate(row_labels):
        parts.append('<text x="{x}" y="{y:.1f}" class="lbl" text-anchor="end">{t}</text>'.format(
            x=pad_l - 8, y=pad_t + ri * ch + 15, t=escape(str(lab))))
    if col_tick_every is None:
        col_tick_every = max(1, n_c // 12)
    for ci, lab in enumerate(col_labels):
        if ci % col_tick_every == 0:
            parts.append('<text x="{x:.1f}" y="{y}" class="sub" text-anchor="middle">{t}</text>'.format(
                x=pad_l + ci * cw + cw / 2, y=pad_t + n_r * ch + 16, t=escape(str(lab))))
    best = None
    for ri, row in enumerate(values):
        for ci, v in enumerate(row):
            x, y = pad_l + ci * cw, pad_t + ri * ch
            if v is None:
                parts.append('<rect x="{x:.1f}" y="{y}" width="{w:.1f}" height="{h}" class="gap"/>'.format(
                    x=x + 0.5, y=y + 0.5, w=cw - 1, h=ch - 1))
                continue
            op = 0.08 + 0.92 * (v / vmax)
            parts.append('<rect x="{x:.1f}" y="{y}" width="{w:.1f}" height="{h}" rx="2" fill="{c}" '
                         'fill-opacity="{o:.3f}"{t}/>'.format(x=x + 0.5, y=y + 0.5, w=cw - 1, h=ch - 1, c=color, o=op,
                                                            t=_tip("{} {}".format(row_labels[ri], col_labels[ci]),
                                                                   value_fmt(v))))
            if best is None or v > best[0]:
                best = (v, x + cw / 2, y + ch / 2)
    if best is not None:
        parts.append('<text x="{x:.1f}" y="{y:.1f}" text-anchor="middle" '
                     'style="fill:#ffffff;font-size:10px;font-weight:600">{t}</text>'.format(
                         x=best[1], y=best[2] + 4, t=escape(value_fmt(best[0]))))
    return _svg(width, height, "".join(parts), title)
