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
  @media (prefers-color-scheme: dark){
    .grid{stroke:#30363d}
    .axis{stroke:#8b949e}
    .lbl{fill:#8b949e}
    .val{fill:#e6edf3}
    .title{fill:#e6edf3}
    .sub{fill:#8b949e}
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


def lines(title, x_labels, series, subtitle="", value_fmt=_fmt, width=760, height=320,
          x_tick_every=None):
    """Multi-series line chart.

    x_labels: list of tick labels (one per x index).
    series: list of (name, [y values], color). All y-lists share the x axis.
    """
    pad_l, pad_r, pad_t, pad_b = 52, 16, (74 if subtitle else 56), 46
    plot_w = width - pad_l - pad_r
    plot_h = height - pad_t - pad_b
    n = max((len(s[1]) for s in series), default=0)
    vmax = max((max(s[1]) for s in series if s[1]), default=0) or 1
    # round vmax up to a nice-ish number
    def nice(v):
        import math
        if v <= 0:
            return 1
        mag = 10 ** math.floor(math.log10(v))
        for m in (1, 2, 2.5, 5, 10):
            if v <= m * mag:
                return m * mag
        return 10 * mag
    vmax = nice(vmax)
    parts = ['<text x="12" y="20" class="title">{}</text>'.format(escape(title))]
    if subtitle:
        parts.append('<text x="12" y="37" class="sub">{}</text>'.format(escape(subtitle)))
    base = pad_t + plot_h

    def px(i):
        return pad_l + (plot_w * (i / max(1, n - 1)) if n > 1 else plot_w / 2)

    def py(v):
        return base - plot_h * (v / vmax)

    # horizontal gridlines + y labels
    for frac in (0, 0.25, 0.5, 0.75, 1.0):
        gy = base - plot_h * frac
        parts.append('<line x1="{a}" y1="{y:.1f}" x2="{b}" y2="{y:.1f}" class="grid" stroke-dasharray="3 3"/>'.format(
            a=pad_l, b=width - pad_r, y=gy))
        parts.append('<text x="{x}" y="{y:.1f}" class="sub" text-anchor="end">{v}</text>'.format(
            x=pad_l - 6, y=gy + 4, v=escape(value_fmt(vmax * frac))))
    # x tick labels
    if x_tick_every is None:
        x_tick_every = max(1, n // 8)
    for i, lab in enumerate(x_labels):
        if i % x_tick_every == 0 or i == n - 1:
            parts.append('<text x="{x:.1f}" y="{y}" class="sub" text-anchor="middle">{v}</text>'.format(
                x=px(i), y=base + 16, v=escape(str(lab))))
    # series polylines
    for si, (name, ys, color) in enumerate(series):
        if not ys:
            continue
        pts = " ".join("{:.1f},{:.1f}".format(px(i), py(v)) for i, v in enumerate(ys))
        parts.append('<polyline points="{p}" fill="none" stroke="{c}" stroke-width="2" '
                     'stroke-linejoin="round" stroke-linecap="round"/>'.format(p=pts, c=color))
    # legend (own row, below the title/subtitle so nothing overlaps)
    lx = pad_l
    ly = 54 if subtitle else 40
    for si, (name, ys, color) in enumerate(series):
        parts.append('<rect x="{x}" y="{y}" width="11" height="11" rx="2" fill="{c}"/>'.format(
            x=lx, y=ly - 10, c=color))
        parts.append('<text x="{x}" y="{y}" class="lbl">{n}</text>'.format(
            x=lx + 16, y=ly, n=escape(name)))
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
