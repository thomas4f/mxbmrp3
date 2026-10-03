#!/usr/bin/env python3
# ============================================================================
# tools/analytics_page.py
# The usage survey as an interactive web page: index.html, beside REPORT.md.
#
# REPORT.md stays the source and the github.com view. GitHub shows its charts as
# <img>, and an SVG loaded as an image is a flat picture - no script, not even
# CSS :hover - and GitHub strips <script>/<object>/<iframe> from rendered
# Markdown, so github.com can never be interactive. GitHub Pages can: this module
# renders the same Markdown to one self-contained HTML file with every chart
# INLINED, so one small script (analytics_page.js) can follow the cursor - a
# guide line and a readout of every series on the day under it on the line
# charts, the exact value of the mark under it on the bar charts. The data it
# reads is written into each SVG by analytics_svg.py (data-chart / data-tip),
# so the page never re-derives a number the chart did not draw.
#
# Pages serves an .html with no front matter byte-for-byte (no Jekyll layout,
# no kramdown), so what this writes is exactly what visitors get, and it is
# previewed by opening the file. The Markdown is rendered with markdown-it-py
# (CommonMark + GFM tables, the dialect GitHub uses) rather than a hand-rolled
# converter.
#
#   python3 tools/analytics_page.py --demo OUT_DIR   # sample page, no data needed
# ============================================================================
import argparse
import os
import re
import sys
from html import escape, unescape

HERE = os.path.dirname(os.path.abspath(__file__))
PAGE_JS = os.path.join(HERE, "analytics_page.js")
PAGE_CSS = os.path.join(HERE, "analytics_page.css")
# Where the page points readers for the plain-Markdown version.
REPO_URL = "https://github.com/thomas4f/mxbmrp3"

_CHART_IMG = r'<img src="charts/([\w.-]+\.svg)" alt="[^"]*"\s*/?>'


def _markdown(md_text):
    try:
        from markdown_it import MarkdownIt
    except ImportError:
        sys.exit("error: markdown-it-py is required for the web page "
                 "(pip install -r tools/requirements.txt)")
    # html=True: the report carries raw <details>/<sub>/<img> blocks on purpose.
    return MarkdownIt("commonmark", {"html": True}).enable(["table", "strikethrough"]).render(md_text)


def _slug(text, used):
    base = re.sub(r"[^a-z0-9]+", "-", unescape(re.sub(r"<[^>]+>", "", text)).lower()).strip("-") or "section"
    slug, k = base, 2
    while slug in used:
        slug, k = "{}-{}".format(base, k), k + 1
    used.add(slug)
    return slug


def _inline_chart(svg_text, k):
    """One chart's SVG, made safe to sit beside the others in one document: ids
    (the stacked bars' clip paths) are per-file, so they are prefixed per chart -
    two charts both defining #b0 would clip each other's bars.

    The <title> child goes too. As an <img> it is only the image's name, but
    inline the browser shows it as a native tooltip over every mark, on top of
    the page's own readout. The aria-label carries the same name, so nothing is
    lost to a screen reader."""
    svg_text = re.sub(r"<title>.*?</title>", "", svg_text, count=1, flags=re.S)
    svg_text = re.sub(r'\bid="([^"]+)"', r'id="c{}-\1"'.format(k), svg_text)
    return svg_text.replace("url(#", "url(#c{}-".format(k))


# The report's headline table: "| At a glance | |", one figure per row.
_GLANCE = re.compile(r"<table>\s*<thead>\s*<tr>\s*<th>At a glance</th>.*?</thead>\s*<tbody>(.*?)</tbody>\s*</table>",
                     re.S)
_GLANCE_ROW = re.compile(r"<tr>\s*<td>(.*?)</td>\s*<td[^>]*><strong>(.*?)</strong></td>\s*</tr>", re.S)


def _glance_cards(m):
    """The headline table as a row of cards: the number big, its name under it.

    Only on this page - REPORT.md keeps the table, since github.com renders no
    styling. A value is a bare number; any qualifier belongs in its name
    ("Releases since Nov 2025"), so every card reads the same way."""
    cards = []
    for name, value in _GLANCE_ROW.findall(m.group(1)):
        cards.append('<div class="card"><div class="num">{}</div><div class="name">{}</div></div>'.format(
            value, name))
    return '<div class="glance">{}</div>'.format("".join(cards))


def build(md_text, charts_dir, title="MXBMRP3 - Usage Survey"):
    """The page for `md_text`, whose chart images (charts/<name>.svg) are read
    from `charts_dir` and inlined. Deterministic: same input, same bytes."""
    body = _markdown(md_text)
    body = _GLANCE.sub(_glance_cards, body, count=1)
    counter = [0]

    def chart(m):
        path = os.path.join(charts_dir, m.group(1))
        with open(path, encoding="utf-8") as f:
            svg_text = f.read()
        counter[0] += 1
        return '<figure class="chart">{}</figure>'.format(_inline_chart(svg_text, counter[0]))

    # A chart alone in its paragraph becomes a figure (a figure inside <p> is
    # invalid, and the browser would split the paragraph around it).
    body = re.sub(r"<p>\s*" + _CHART_IMG + r"\s*</p>", chart, body)
    body = re.sub(_CHART_IMG, chart, body)
    # The report's own links point at Markdown files next to it; Pages serves
    # those rendered as .html.
    body = re.sub(r'href="((?!https?:|#)[^"]+?)\.md(#[^"]*)?"',
                  lambda m: 'href="{}.html{}"'.format(m.group(1), m.group(2) or ""), body)
    # The top heading is the page header; every section heading gets an anchor
    # and a place in the section list.
    body = re.sub(r"<h1>.*?</h1>\s*", "", body, count=1, flags=re.S)
    used, nav = set(), []

    def section(m):
        slug = _slug(m.group(1), used)
        nav.append('<a href="#{}">{}</a>'.format(slug, re.sub(r"<[^>]+>", "", m.group(1))))
        return '<h2 id="{}">{}</h2>'.format(slug, m.group(1))

    body = re.sub(r"<h2>(.*?)</h2>", section, body)
    with open(PAGE_CSS, encoding="utf-8") as f:
        css = f.read()
    with open(PAGE_JS, encoding="utf-8") as f:
        js = f.read()
    return (
        "<!doctype html>\n"
        '<html lang="en">\n<head>\n<meta charset="utf-8">\n'
        '<meta name="viewport" content="width=device-width, initial-scale=1">\n'
        "<title>{title}</title>\n"
        '<meta name="description" content="What the MXBMRP3 plugin\'s anonymous usage pings add up to.">\n'
        "<style>\n{css}</style>\n</head>\n<body>\n"
        '<header class="top"><div class="wrap">\n'
        "<h1>{title}</h1>\n"
        '<p class="alt">Also on GitHub as <a href="{repo}/blob/main/usage_survey/REPORT.md">REPORT.md</a>'
        ' - hover a chart here for the numbers.</p>\n'
        '<nav>{nav}</nav>\n</div></header>\n'
        '<main class="wrap">\n{body}</main>\n'
        "<script>\n{js}</script>\n</body>\n</html>\n"
    ).format(title=escape(title), css=css, repo=REPO_URL, nav="".join(nav), body=body, js=js)


def write(out_dir, md_text):
    path = os.path.join(out_dir, "index.html")
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(build(md_text, os.path.join(out_dir, "charts")))
    return path


def demo(out_dir):
    """A sample page from synthetic data through the real chart helpers, for
    previewing the page and for the Playwright hover test (tests/web)."""
    import analytics_svg as svg
    os.makedirs(os.path.join(out_dir, "charts"), exist_ok=True)
    days = ["09-{:02d}".format(d) for d in range(1, 11)]
    charts = {
        "lines.svg": svg.lines(
            "Version migration", days,
            [("1.30.3", [None, None, 5, 12, 20, 31, 40, 44, 47, 49], svg.PALETTE[1]),
             ("1.29.5", [60, 58, 52, 45, 40, 33, 28, 25, 23, 21], svg.PALETTE[0]),
             ("1.28.0", [5, 3, 1, 0, 0, 0, 0, 0, 0, 0], svg.PALETTE[2]),
             ("Other", [35, 39, 42, 43, 40, 36, 32, 31, 30, 30], "#57606a")],
            subtitle="share of each day's active installs", annotate="legend",
            value_fmt=lambda v: "{:.0f}%".format(v)),
        "bars.svg": svg.hbar("Operating system", [("Windows 11", 78, None, "10,116 (78%)"),
                                                  ("Windows 10", 21, None, "2,779 (21%)")]),
        "stacked.svg": svg.stacked_hbar(
            "Achievements by tier reached",
            [("Racer", None, [("t1", 40, "Finish a race"), ("t2", 20, "Finish 10 races"),
                              ("t3", 0.5, "Finish 100 races")], "60%",
              "Bronze: Finish a race\nSilver: Finish 10 races\nGold: Finish 100 races"),
             ("Metronome", None, [("t1", 3, "Five laps in a row, all within a tenth", "")], "3%",
              "Five laps in a row, all within a tenth")],
            [("t1", "Bronze"), ("t2", "Silver"), ("t3", "Gold")]),
    }
    for name, text in charts.items():
        with open(os.path.join(out_dir, "charts", name), "w", encoding="utf-8") as f:
            f.write(text)
    md = "\n".join(["# Demo", "", "| At a glance | |", "|---|--:|",
                    "| Unique installs | **14,782** |", "| Releases since Nov 2025 | **39** |", "",
                    "## Adoption", "", "![a](charts/lines.svg)", "",
                    "## Installs", "", "| OS | Installs |", "|---|--:|", "| Windows 11 | 10,116 |", "",
                    "![b](charts/bars.svg)", "", '<img src="charts/stacked.svg" alt="c">', ""])
    return write(out_dir, md)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--demo", metavar="OUT_DIR", required=True,
                    help="write a sample page (and its charts) into OUT_DIR")
    args = ap.parse_args()
    sys.path.insert(0, HERE)
    print(demo(args.demo))


if __name__ == "__main__":
    main()
