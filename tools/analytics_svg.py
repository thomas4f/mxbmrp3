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

# Stable colours for the supported games, so a game keeps its colour everywhere.
GAME_COLORS = {
    "MX Bikes": "#3fb950",
    "GP Bikes": "#58a6ff",
    "Kart Racing Pro": "#d29922",
    "WRS": "#bc8cff",
}

_STYLE = """<style>
  text{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,Helvetica,Arial,sans-serif}
  .grid{stroke:#d0d7de;stroke-width:1}
  .axis{stroke:#57606a;stroke-width:1}
  .lbl{fill:#57606a;font-size:12px}
  .val{fill:#24292f;font-size:12px;font-weight:600}
  .title{fill:#24292f;font-size:14px;font-weight:700}
  .sub{fill:#57606a;font-size:11px}
  .gap{fill:#57606a;opacity:.09}
  .gaplbl{fill:#57606a;font-size:10px;opacity:.85}
  @media (prefers-color-scheme: dark){
    .grid{stroke:#30363d}
    .axis{stroke:#8b949e}
    .lbl{fill:#8b949e}
    .val{fill:#e6edf3}
    .title{fill:#e6edf3}
    .sub{fill:#8b949e}
    .gap{fill:#8b949e;opacity:.13}
    .gaplbl{fill:#8b949e}
  }
</style>"""


def _svg(w, h, body):
    return (
        '<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" '
        'viewBox="0 0 {w} {h}" role="img">{style}{body}</svg>'
    ).format(w=w, h=h, style=_STYLE, body=body)


def _fmt(v):
    """Compact human number: 12345 -> 12,345 ; 3.4 -> 3.4."""
    if isinstance(v, float) and not v.is_integer():
        return "{:,.1f}".format(v)
    return "{:,}".format(int(round(v)))


def hbar(title, rows, subtitle="", value_fmt=_fmt, width=760, label_w=210):
    """Horizontal bar chart.

    rows: (label, value[, color[, annotation]]). Drawn top-to-bottom in the
    given order (caller sorts). Bar length scales to the max value. The optional
    4th element is the text drawn at the bar end (e.g. "46% (1,606)"); it
    overrides value_fmt, so a chart can show count and percentage together.
    """
    pad_l, pad_r, pad_t = label_w, 96, 44 if subtitle else 34
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
            '<rect x="{x}" y="{y}" width="{bw}" height="{rh}" rx="3" fill="{c}"/>'.format(
                x=pad_l, y=y, bw=bw, rh=row_h, c=color
            )
        )
        parts.append(
            '<text x="{x}" y="{ty}" class="val">{v}</text>'.format(
                x=pad_l + bw + 6, ty=y + row_h - 6, v=escape(annot)
            )
        )
    return _svg(width, h, "".join(parts))


def vbars(title, cats, subtitle="", value_fmt=_fmt, width=760, height=300):
    """Vertical bar chart / histogram. cats: list of (label, value)."""
    pad_l, pad_r, pad_t, pad_b = 46, 16, 44 if subtitle else 34, 40
    n = len(cats)
    plot_w = width - pad_l - pad_r
    plot_h = height - pad_t - pad_b
    vmax = max([c[1] for c in cats], default=0) or 1
    slot = plot_w / max(1, n)
    bw = max(3, slot * 0.72)
    parts = ['<text x="12" y="20" class="title">{}</text>'.format(escape(title))]
    if subtitle:
        parts.append('<text x="12" y="37" class="sub">{}</text>'.format(escape(subtitle)))
    base = pad_t + plot_h
    parts.append('<line x1="{a}" y1="{y}" x2="{b}" y2="{y}" class="axis"/>'.format(
        a=pad_l, b=width - pad_r, y=base))
    # gridlines at 0/50/100%
    for frac in (0.5, 1.0):
        gy = base - plot_h * frac
        parts.append('<line x1="{a}" y1="{y}" x2="{b}" y2="{y}" class="grid" stroke-dasharray="3 3"/>'.format(
            a=pad_l, b=width - pad_r, y=gy))
        parts.append('<text x="{x}" y="{y}" class="sub" text-anchor="end">{v}</text>'.format(
            x=pad_l - 6, y=gy + 4, v=escape(value_fmt(vmax * frac))))
    for i, (label, val) in enumerate(cats):
        x = pad_l + i * slot + (slot - bw) / 2
        bh = plot_h * (val / vmax)
        y = base - bh
        parts.append('<rect x="{x:.1f}" y="{y:.1f}" width="{bw:.1f}" height="{bh:.1f}" rx="2" fill="{c}"/>'.format(
            x=x, y=y, bw=bw, bh=bh, c=PALETTE[0]))
        parts.append('<text x="{x:.1f}" y="{y}" class="sub" text-anchor="middle">{v}</text>'.format(
            x=x + bw / 2, y=base + 14, v=escape(str(label))))
    return _svg(width, height, "".join(parts))


def _polyline(pts, color):
    return ('<polyline points="{p}" fill="none" stroke="{c}" stroke-width="2" '
            'stroke-linejoin="round" stroke-linecap="round"/>'
            .format(p=" ".join("{:.1f},{:.1f}".format(x, y) for x, y in pts), c=color))


def lines(title, x_labels, series, subtitle="", value_fmt=_fmt, width=760, height=320,
          x_tick_every=None, log=False, gaps=()):
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
    """
    pad_l, pad_r, pad_t, pad_b = 52, 16, (74 if subtitle else 56), 46
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
    # round vmax up to a nice-ish number
    def nice(v):
        if v <= 0:
            return 1
        mag = 10 ** math.floor(math.log10(v))
        for m in (1, 2, 2.5, 5, 10):
            if v <= m * mag:
                return m * mag
        return 10 * mag
    if log:
        # Round the top up to a whole decade so the highest gridline is a real tick.
        top = 10 ** int(math.ceil(math.log10(vmax))) if vmax > 0 else 1
        lmax = math.log10(1 + top) or 1.0
    else:
        vmax = nice(vmax)
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
        ticks = [vmax * frac for frac in (0, 0.25, 0.5, 0.75, 1.0)]
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
    return _svg(width, height, "".join(parts))


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
        parts.append('<rect x="{x:.1f}" y="{y}" width="{w:.1f}" height="{h}" fill="{c}"/>'.format(
            x=x, y=bar_y, w=w, h=bar_h, c=color))
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
    return _svg(width, legend_y + 12, "".join(parts))
