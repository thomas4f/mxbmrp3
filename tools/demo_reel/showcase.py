#!/usr/bin/env python3
"""The README's showcase sheet: most HUDs, widgets and decks in one image, the
themes in diagonal bands. Driven by showcase.sh, which renders each piece with
the real plugin (make_demo.sh still mode); this file only lays out and composes.

  showcase.py ids                        harness ids, comma-separated (REEL_EDGES)
  showcase.py measure-ini THEME OUT      settings that show everything at offset 0
  showcase.py layout DIR THEME...        DIR/measure-THEME.log -> DIR/seed-*.ini, DIR/layout.json
  showcase.py compose DIR OUT THEME...   DIR/render-*-alpha.png -> OUT

LAYOUT. Every element keeps its standard size and sits on the plugin's own grid
(a column 0.0055, a row 0.011 of the UI rect), one cell apart, in a tree of
rows and columns (TREE below). Sizes come from the panel rects the plugin
reports (MXBMRP3_Test_HudScreenEdges); each element takes its largest rect over
the themes, so all themes share the same positions and the bands can cut
anywhere. The canvas is 1920 wide (the 1080p scale) and as tall as the content:
the companion window centres its 16:9 rect, so UI y runs past 0..1.

THEMES. The main sheet is cut into one diagonal band per theme, of equal area,
with a clear seam where the backdrop shows through. The pieces that look the
same in every theme (their art is their own) sit whole in a side column.

Why bespoke: the pieces are the plugin's own renders, placed by its own grid,
and the cut needs every theme's frame at the same positions; no image tool
lays out from a program's panel rects.
"""
import json
import os
import re
import sys

COL, ROW = 0.0055, 0.011          # one grid cell, in UI units
CELL_W, CELL_H = 10.56, 11.88     # the same, in 1080p px
HDR = 4                           # a section header's height, in rows
SEAM = 16                         # the cut between two bands, px
SLOPE = 0.6                       # the bands run along x + SLOPE * y
FONT = os.path.join(os.path.dirname(__file__), "..", "..", "mxbmrp3_data", "web", "fonts", "Inter-SemiBold.ttf")

# harness id -> INI section, for everything the sheet shows. Left out on
# purpose: Gamepad (draws "not connected" without a pad), Combo and FMX (FMX
# only), Toggles and Pointer (the mouse), the achievement toast and Prestige,
# Spotter, Benchmark, Rumble, stream chat, the helmet, and GP Bikes' own.
HUDS = [("standings_hud", "StandingsHud"), ("map_hud", "MapHud"), ("radar_hud", "RadarHud"),
        ("lap_log_hud", "LapLogHud"), ("friends_hud", "FriendsHud"), ("ideal_lap_hud", "IdealLapHud"),
        ("telemetry_hud", "TelemetryHud"), ("performance_hud", "PerformanceHud"),
        ("delta_trace_hud", "DeltaTraceHud"), ("pitboard_hud", "PitboardHud"), ("records_hud", "RecordsHud"),
        ("session_charts_hud", "SessionChartsHud"), ("stats_hud", "StatsHud"), ("event_log_hud", "EventLogHud"),
        ("session_widget", "SessionHud"), ("timing_hud", "TimingHud"), ("gap_bar_hud", "GapBarHud"),
        ("notices_hud", "NoticesHud"), ("dash_hud", "DashHud")]
WIDGETS = [("lap_widget", "LapWidget"), ("position_widget", "PositionWidget"), ("time_widget", "TimeWidget"),
           ("speed_widget", "SpeedWidget"), ("gear_widget", "GearWidget"), ("crash_widget", "CrashWidget"),
           ("rpm_widget", "RpmWidget"), ("speedo_widget", "SpeedoWidget"), ("tacho_widget", "TachoWidget"),
           ("bars_widget", "BarsWidget"), ("fuel_widget", "FuelWidget"), ("lean_widget", "LeanWidget"),
           ("slide_widget", "SlideWidget"), ("wheelie_widget", "WheelieWidget"), ("stance_widget", "StanceWidget"),
           ("gforce_widget", "GForceWidget"), ("compass_widget", "CompassWidget"), ("clock_widget", "ClockWidget"),
           ("now_playing_widget", "NowPlayingWidget")]
DECKS = [(f"{k}_deck", f"{s}Deck") for k, s in [
    ("speed", "Speed"), ("race", "Race"), ("timing", "Timing"), ("wheelie", "Wheelie"), ("slide", "Slide"),
    ("gap", "Gap"), ("rival", "Rival"), ("spin", "Spin"), ("inputs", "Inputs"), ("standings", "Standings"),
    ("session", "Session"), ("notice", "Notice"), ("fuel", "Fuel"), ("media", "Media"), ("laplog", "LapLog"),
    ("carousel", "Carousel")]]
ALL = HUDS + WIDGETS + DECKS


def H(*k, gap=1): return ("H", gap, list(k))
def V(*k, gap=1): return ("V", gap, list(k))
def S(name): return ("S", 10, HDR, name)


# The sheet. HUD columns end level (79 rows) but the last, clearly shorter.
TREE = V(
    S("HUDs"),
    H(V("standings_hud", "ideal_lap_hud", "stats_hud"),
      V(H("timing_hud", V("gap_bar_hud", "notices_hud")), "dash_hud", "friends_hud", "records_hud"),
      V("lap_log_hud", "delta_trace_hud", "telemetry_hud", "event_log_hud"),
      V("session_widget", "session_charts_hud", "performance_hud")),
    S("Widgets"),
    V(H("position_widget", "lap_widget", "time_widget", "clock_widget", "crash_widget", "speed_widget",
        "gear_widget", "fuel_widget", "stance_widget"),
      H("rpm_widget", "bars_widget", "lean_widget", "slide_widget", "wheelie_widget", "gforce_widget",
        "compass_widget", "now_playing_widget")),
    S("Decks"),
    V(H("race_deck", "timing_deck", "gap_deck", "rival_deck", "laplog_deck"),
      H("session_deck", "notice_deck", "carousel_deck", "inputs_deck"),
      H("standings_deck", "speed_deck", "fuel_deck", "wheelie_deck", "slide_deck", "spin_deck", "media_deck")))
# The same in every theme: rendered once, composed beside the sheet.
SIDE = ["map_hud", "radar_hud", "pitboard_hud", "speedo_widget", "tacho_widget"]
SIDE_TREE = V(*SIDE)
SIDE_HEADER = "Same in every theme"


def settings(theme, pos, rects):
    """The seed INI: the placed elements opaque at their spots, the rest hidden."""
    out = ["[Global]", "widgetsEnabled=1", "decksEnabled=1", "", "[Display]", f"panelTheme={theme}", "",
           "[Messages]", "dashTried=1", "", "[General]", "nowPlaying=1", "", "[Decks]", "opacity=1.000000", ""]
    for n, sec in ALL:
        if n not in pos:
            out += [f"[{sec}]", "visible=0", ""]
            continue
        c, r = pos[n]
        if n == "map_hud":   # the map places by its anchor, not offsetX/Y
            out += [f"[{sec}]", "visible=1", "backgroundOpacity=1.000000", "anchorPoint=TOP_LEFT",
                    f"anchorX={c * COL:.6f}", f"anchorY={r * ROW:.6f}", "rotateToPlayer=0", ""]
            continue
        extra = ["noticeFocus=0"] if n == "carousel_deck" else []   # its turn, not the PB the Notice deck shows
        left, top = rects[n][0], rects[n][1]
        out += [f"[{sec}]", "visible=1", "backgroundOpacity=1.000000", *extra,
                f"offsetX={c * COL - left:.6f}", f"offsetY={r * ROW - top:.6f}", ""]
    return "\n".join(out)


def read_edges(path):
    rects = {}
    for ln in open(path):
        m = re.match(r"edges (\S+) (-?\d+) (-?\d+) (-?\d+) (-?\d+)", ln)
        if m:
            rects[m.group(1)] = tuple(int(x) / 1e6 for x in m.groups()[1:])
    return rects


def layout(d, themes):
    measured = {t: read_edges(f"{d}/measure-{t}.log") for t in themes}
    cells = {n: (max(round((r[2] - r[0]) / COL) for r in (m[n] for m in measured.values())),
                 max(round((r[3] - r[1]) / ROW) for r in (m[n] for m in measured.values())))
             for n, _ in ALL}

    def size(node):
        if isinstance(node, str): return cells[node]
        if node[0] == "S": return node[1], node[2]
        kind, gap, kids = node
        s = [size(k) for k in kids]
        if kind == "H": return sum(w for w, _ in s) + gap * (len(s) - 1), max(h for _, h in s)
        return max(w for w, _ in s), sum(h for _, h in s) + gap * (len(s) - 1)

    def place(node, c, r, pos, heads):
        if isinstance(node, str): pos[node] = (c, r); return
        if node[0] == "S": heads[node[3]] = (c, r); return
        kind, gap, kids = node
        for k in kids:
            place(k, c, r, pos, heads)
            w, h = size(k)
            if kind == "H": c += w + gap
            else: r += h + gap

    def sheet(tree, canvas_h=None):
        w, h = size(tree)
        canvas_h = canvas_h or round((h + 4) * CELL_H / 2) * 2
        top_ui = -(canvas_h - 1080) / 2 / 1080     # the UI y of the canvas' top edge
        pos, heads = {}, {}
        place(tree, 2, -int(-top_ui / ROW) + 2, pos, heads)
        def px(c, r): return c * CELL_W, (r * ROW - top_ui) * 1080
        return canvas_h, pos, heads, px

    out = {"themes": themes}
    canvas_h, pos, heads, px = sheet(TREE)
    out["main"] = {"h": canvas_h, "rects": {n: [*px(*pos[n]), cells[n][0] * CELL_W, cells[n][1] * CELL_H] for n in pos},
                   "heads": {k: [*px(*v), 10 * CELL_W, HDR * CELL_H] for k, v in heads.items()}}
    for t in themes:
        open(f"{d}/seed-{t}.ini", "w").write(settings(t, pos, measured[t]))
    side_h, spos, _, spx = sheet(SIDE_TREE)
    out["side"] = {"h": side_h, "rects": {n: [*spx(*spos[n]), cells[n][0] * CELL_W, cells[n][1] * CELL_H] for n in spos}}
    open(f"{d}/seed-side.ini", "w").write(settings(themes[0], spos, measured[themes[0]]))
    json.dump(out, open(f"{d}/layout.json", "w"), indent=1)
    print(f"main 1920x{canvas_h}, side 1920x{side_h}")


def compose(d, out_path, themes):
    import numpy as np
    from PIL import Image, ImageDraw, ImageFont
    L = json.load(open(f"{d}/layout.json"))
    def load(p): return np.asarray(Image.open(p).convert("RGBA")).astype(np.float32) / 255
    main = [load(f"{d}/render-{t}-alpha.png") for t in themes]
    side = load(f"{d}/render-side-alpha.png")
    MH, MW = main[0].shape[:2]
    PAD, GAP, G = 4, 40, 12
    rects, heads = L["main"]["rects"], L["main"]["heads"]
    right = int(max(x + w for x, y, w, h in rects.values()))
    top, hdr_h = int(heads["HUDs"][1]), int(heads["HUDs"][3])

    def piece(n):   # a side element's panel rect, padded for its edge
        x, y, w, h = L["side"]["rects"][n]
        return side[int(y) - PAD:int(y + h) + PAD, int(x) - PAD:int(x + w) + PAD]
    m, r, p, s, t = (piece(n) for n in SIDE)
    sx = right + GAP
    colw = max(m.shape[1], r.shape[1] + G + s.shape[1], p.shape[1] + G + t.shape[1])
    W, H = sx + colw + 21, MH
    # The backdrop: the grey spotlight the stills use (make_demo.sh), ordered-dithered.
    Y, X = np.mgrid[0:H, 0:W].astype(np.float32)
    lum = 16 + 30 * np.exp(-((X / W - 0.5) / (0.12 + 0.45 * Y / H)) ** 2) * (1 - 0.5 * Y / H) \
        + (np.mod(X + 2 * Y, 4) - 1.5) * 0.6
    out = np.repeat(lum[..., None], 3, axis=2) / 255
    backdrop = out.copy()
    # One band per theme, of equal area, along x + SLOPE * y.
    yy, xx = np.mgrid[0:MH, 0:MW]
    diag = xx + SLOPE * yy
    q = np.quantile(diag.ravel(), [i / len(themes) for i in range(1, len(themes))])
    bands = np.searchsorted(q, diag)
    for i, img in enumerate(main):
        a = img[..., 3:4] * (bands == i)[..., None]
        out[:MH, :MW] = img[..., :3] * a + out[:MH, :MW] * (1 - a)
    seam = np.min([np.abs(diag - b) for b in q], axis=0) / np.hypot(1, SLOPE) < SEAM / 2
    seam &= xx < right + GAP // 2
    out[:MH, :MW][seam] = backdrop[:MH, :MW][seam]

    def over(src, x, y):
        h, w = src.shape[:2]
        a = src[..., 3:4]
        out[y:y + h, x:x + w] = src[..., :3] * a + out[y:y + h, x:x + w] * (1 - a)
    y = top + hdr_h + G   # the side: the map, then radar + speedo, pitboard + tacho
    over(m, sx, y)
    y += m.shape[0] + G
    for a_, b_ in ((r, s), (p, t)):
        over(a_, sx, y)
        over(b_, sx + a_.shape[1] + G, y)
        y += max(a_.shape[0], b_.shape[0]) + G
    img = Image.fromarray((np.clip(out, 0, 1) * 255).astype(np.uint8))
    draw = ImageDraw.Draw(img)
    font = ImageFont.truetype(FONT, 30)

    def header(text, x, y, h, x_end):
        tb = draw.textbbox((0, 0), text, font=font)
        draw.text((x, y + (h - (tb[3] - tb[1])) / 2 - tb[1]), text, font=font, fill=(205, 205, 205))
        draw.line([(x + tb[2] + 16, y + h / 2), (x_end, y + h / 2)], fill=(80, 80, 80), width=2)
    for k, (x, y_, _w, h) in heads.items():
        header(k, int(x), int(y_), int(h), right)
    header(SIDE_HEADER, sx, top, hdr_h, W - 21)
    img.save(out_path, optimize=True)
    print(f"wrote {out_path} {img.size[0]}x{img.size[1]}")


if __name__ == "__main__":
    cmd, args = sys.argv[1], sys.argv[2:]
    if cmd == "ids":
        print(",".join(n for n, _ in ALL))
    elif cmd == "measure-ini":
        open(args[1], "w").write(settings(args[0], {n: (0, 0) for n, _ in ALL}, {n: (0, 0) for n, _ in ALL}))
    elif cmd == "layout":
        layout(args[0], args[1:])
    elif cmd == "compose":
        compose(args[0], args[1], args[2:])
    else:
        sys.exit(__doc__)
