#!/usr/bin/env python3
"""Cut a theme's MASTER IMAGE into its .tga files, by the .cells file beside it.

WHY THIS EXISTS, given the theme system deliberately has no art tooling. A theme
is dozens of files that must look like each other -- a card is a quieter frame, a
button a smaller card, a deck picture the same family again -- and that many
separate files is the worst possible shape for keeping them consistent, whether
the hand is yours or an agent's. Drawn together in one image they stay together
by construction.

WHAT IT DOES NOT DO, which is the line the old generator crossed: it never draws
art. It cuts the image you gave it, losslessly, at positions its .cells file
states. Every pixel out is a pixel in.

ONE MASTER PER THEME, holding every top-level .tga of the theme folder: the
nine-slice sets, the slanted ends, the deck pictures. Each set is a 3x3
arrangement in the order the slices actually appear on screen:

    corner_tl   edge_top      corner_tr
    edge_left   center        edge_right
    corner_bl   edge_bottom   corner_br

so you are drawing a panel, not filling in a sprite sheet. The sets -- frame,
card, button, and the optional title -- stack down the image in that order; the
theme's other pictures stack below them, one under another, each whole.

THE .cells FILE IS THE CONTRACT. One line per picture, `<name> x y w h`, plus an
`out = <folder>` line naming where the pictures go. --template and --from-theme
write it with the sheet, and cutting reads it back, so the cuts cannot drift from
the sheet. To add a picture: grow the sheet, paint it, add its line. (Cutting
used to INFER the layout from the image's size, which only worked while a master
held nothing but the sets; the deck pictures come in any size, so the file says
where everything is instead.)

GUTTERS ARE NOT ART. Cells are separated by a gutter that no picture ever reads,
so the sheet can caption each set and picture without any of that reaching a .tga.
Paint inside the cells; whatever is left in the gutter is ignored.

    themeslice.py --template master.png          # start a new theme
    themeslice.py master.png                     # cut it (into its `out`, or --out)
    themeslice.py --from-theme <theme dir>       # the inverse: a folder -> master
    themeslice.py --selftest                     # round-trip check (the CI gate)

Stdlib only -- zlib for PNG, everything else by hand -- so it runs anywhere the
repo's other python gates do, with nothing to install.
"""
import argparse
import math
import os
import struct
import sys
import zlib

# ---------------------------------------------------------------------------
# Geometry of a NEW sheet (--template, --from-theme). Cutting never needs it:
# the .cells file says where every picture is.
# ---------------------------------------------------------------------------
CORNER = 64      # corner art is CORNER x CORNER; also each edge's THICKNESS
#
# EVERY SET CELL IS THE SAME SQUARE, which is an AUTHORING decision and not a
# rendering one -- the renderer stretches the edges and centre to the panel
# either way, so their resolution is free. It used to be 16px on exactly that
# reasoning ("detail belongs in the corners"), and free-but-small turned out to
# cost more than the pixels it saved: a 3x3 of unequal cells does not read as a
# panel, so the master stopped looking like the thing it becomes. You cannot draw
# a stroke ACROSS a boundary and have it land, because the two sides are at
# different scales; a corner motif cannot be carried a little way along its
# edges; and every art tool's grid, guides and symmetry are set up for equal
# cells. Square costs a few .tga bytes (they compress to nearly nothing in the
# installer) and nothing at all on screen.


# The gutter is DERIVED from the corner, not fixed, so a sheet scales UNIFORMLY:
# --corner 128 lands on exactly 2x the pixels of the default, which is what
# someone making a hi-dpi version of their art needs.
def gutter_for(corner):
    return max(2, corner // 8)


SETS = ("frame", "card", "button")
# The optional title band art (ThemeAsset::titleCenterSprite). LAST, so a
# three-set sheet's cells sit where they always did.
SETS_WITH_TITLE = SETS + ("title",)

# Row-major, matching the 3x3 picture above.
PARTS = (
    ("corner_tl", "edge_top", "corner_tr"),
    ("edge_left", "center", "edge_right"),
    ("corner_bl", "edge_bottom", "corner_br"),
)

# SLANTED ENDS (ThemeAsset::slantLeftSprite): a set's two ends leaning, for the
# slanted decks, `<set>_slant_left/right.tga`. Each is one picture, as tall as
# the deck piece it ends, so --template sizes it from the corner: `rows` deck
# rows tall (a row is 11.88/10.56 of a cell) and its lean (0.2 columns per row)
# plus one corner wide, at HALF the set's scale (a corner is one border's worth
# of cells). The plugin draws it by its shape alone, and a 9-row card stands
# ~107 px at 1080p: at the set's own scale (648 px tall at a 64 px corner) each
# end cost 466 KB for detail no screen shows.
END_ROWS = {"frame": 9}
END_SCALE = 0.5


def end_size(s, corner):
    rows, c = END_ROWS[s], corner * END_SCALE
    return math.ceil(0.2 * rows * c + c), round(rows * 11.88 / 10.56 * c)


GUIDE = (150, 160, 190, 48)    # --template gutter: SEMI-transparent, so cells
                               # are visible to work in but nothing reads as art.
                               # An assembled master gets a fully clear gutter.
CLEAR = (0, 0, 0, 0)
LABEL = (120, 130, 150, 255)   # caption glyphs, drawn in the gutter only
GLYPH_H = 5


def plan(corner=CORNER, stretch=None, sets=SETS, pictures=()):
    """(sheet_w, sheet_h, [(name, x, y, w, h)], [(x, y, caption)]) for a new sheet.

    The sets first, each a 3x3 block captioned with its name, then `pictures`
    ([(name, w, h)]) one under another, each captioned with its own. `stretch`
    (the edges' length and the centre) defaults to the corner: square cells.

    A caption sits in the gutter ABOVE its block, so it needs a gutter of
    GLYPH_H + 1; a smaller corner gets no captions rather than captions inside
    the previous block's cells (a --corner 32 template once baked label pixels
    into corner_bl that way). The self-test holds this: a cut template is clear.
    """
    stretch = corner if stretch is None else stretch
    g = gutter_for(corner)
    size = (corner, stretch, corner)
    col_x, x = [], g
    for w in size:
        col_x.append(x)
        x += w + g
    sheet_w = max([x] + [pw + 2 * g for _, pw, _ in pictures])
    cells, captions, y = [], [], g

    def caption(text):
        if g >= GLYPH_H + 1:
            captions.append((g, y - GLYPH_H - 1, text))

    for s in sets:
        caption(s)
        row_y = []
        for h in size:
            row_y.append(y)
            y += h + g
        for r, row in enumerate(PARTS):
            for c, part in enumerate(row):
                cells.append((f"{s}_{part}", col_x[c], row_y[r], size[c], size[r]))
    for name, pw, ph in pictures:
        caption(name)
        cells.append((name, g, y, pw, ph))
        y += ph + g
    return sheet_w, y, cells, captions


# ---------------------------------------------------------------------------
# PNG. Read enough of the spec to load anything an editor or an image model
# emits at 8 bits; write the minimum needed for a template.
# ---------------------------------------------------------------------------
def read_png(path):
    data = open(path, "rb").read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path}: not a PNG")
    idat, w, h, depth, ctype, pal, trns = b"", 0, 0, 0, 0, None, None
    off = 8
    while off < len(data):
        (ln,) = struct.unpack(">I", data[off:off + 4])
        tag = data[off + 4:off + 8]
        body = data[off + 8:off + 8 + ln]
        off += 12 + ln
        if tag == b"IHDR":
            w, h, depth, ctype = struct.unpack(">IIBB", body[:10])[:4]
            if body[12] != 0:
                raise ValueError(f"{path}: interlaced PNG is not supported")
        elif tag == b"PLTE":
            pal = body
        elif tag == b"tRNS":
            trns = body
        elif tag == b"IDAT":
            idat += body
        elif tag == b"IEND":
            break
    if depth != 8:
        raise ValueError(f"{path}: only 8-bit PNG is supported (got {depth})")
    chans = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}.get(ctype)
    if chans is None:
        raise ValueError(f"{path}: unsupported colour type {ctype}")

    raw = zlib.decompress(idat)
    stride = w * chans
    out, prev, pos = [], bytearray(stride), 0
    for _ in range(h):
        f = raw[pos]
        line = bytearray(raw[pos + 1:pos + 1 + stride])
        pos += 1 + stride
        # Undo the per-scanline filter (PNG spec 9.2). bytearray in place: each
        # byte may depend on one decoded earlier in the same line.
        if f == 1:
            for i in range(chans, stride):
                line[i] = (line[i] + line[i - chans]) & 0xFF
        elif f == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 0xFF
        elif f == 3:
            for i in range(stride):
                left = line[i - chans] if i >= chans else 0
                line[i] = (line[i] + ((left + prev[i]) >> 1)) & 0xFF
        elif f == 4:
            for i in range(stride):
                a = line[i - chans] if i >= chans else 0
                b = prev[i]
                c = prev[i - chans] if i >= chans else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 0xFF
        elif f != 0:
            raise ValueError(f"{path}: bad filter {f}")
        prev = line

        row = []
        for x in range(w):
            px = line[x * chans:(x + 1) * chans]
            if ctype == 6:
                row.append(tuple(px))
            elif ctype == 2:
                row.append((px[0], px[1], px[2], 255))
            elif ctype == 0:
                row.append((px[0], px[0], px[0], 255))
            elif ctype == 4:
                row.append((px[0], px[0], px[0], px[1]))
            else:                      # 3: palette
                i = px[0]
                r, g, b = pal[i * 3:i * 3 + 3]
                a = trns[i] if trns and i < len(trns) else 255
                row.append((r, g, b, a))
        out.append(row)
    return w, h, out


def write_png(path, rows):
    h, w = len(rows), len(rows[0])
    raw = bytearray()
    for row in rows:
        raw.append(0)                       # filter 0; a template is tiny
        for (r, g, b, a) in row:
            raw += bytes((r, g, b, a))

    def chunk(tag, body):
        c = struct.pack(">I", len(body)) + tag + body
        return c + struct.pack(">I", zlib.crc32(tag + body) & 0xFFFFFFFF)

    open(path, "wb").write(
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
        + chunk(b"IEND", b""))


# ---------------------------------------------------------------------------
# TGA. 32-bit uncompressed, top-origin -- the one shape the plugin reads.
# ---------------------------------------------------------------------------
def read_tga(path):
    b = open(path, "rb").read()
    idlen, cmap, imgtype = b[0], b[1], b[2]
    w, h, bpp, desc = struct.unpack("<HHBB", b[12:18])
    if (idlen, cmap, imgtype, bpp) != (0, 0, 2, 32):
        raise ValueError(f"{path}: expected a 32-bit uncompressed TGA")
    rows, off = [], 18
    for _ in range(h):
        row = []
        for _ in range(w):
            bl, g, r, a = b[off:off + 4]
            row.append((r, g, bl, a))
            off += 4
        rows.append(row)
    if not desc & 0x20:                     # bottom-origin: flip to top
        rows.reverse()
    return w, h, rows


def write_tga(path, rows):
    h, w = len(rows), len(rows[0])
    body = bytearray()
    for row in rows:
        for (r, g, b, a) in row:
            body += bytes((b, g, r, a))
    open(path, "wb").write(
        struct.pack("<BBBHHBHHHHBB", 0, 0, 2, 0, 0, 0, 0, 0, w, h, 32, 0x20)
        + bytes(body))


def read_image(path):
    if path.lower().endswith(".tga"):
        return read_tga(path)
    return read_png(path)


# ---------------------------------------------------------------------------
# Captions
# ---------------------------------------------------------------------------
# A 3x5 dot font, enough for the slice names. Labels are drawn in the GUTTER, so
# nothing here can end up inside a slice.
GLYPHS = {
    "A": ("010", "101", "111", "101", "101"), "B": ("110", "101", "110", "101", "110"),
    "C": ("011", "100", "100", "100", "011"), "D": ("110", "101", "101", "101", "110"),
    "E": ("111", "100", "110", "100", "111"), "F": ("111", "100", "110", "100", "100"),
    "G": ("011", "100", "101", "101", "011"), "H": ("101", "101", "111", "101", "101"),
    "I": ("111", "010", "010", "010", "111"), "L": ("100", "100", "100", "100", "111"),
    "M": ("101", "111", "111", "101", "101"), "N": ("101", "111", "111", "111", "101"),
    "O": ("010", "101", "101", "101", "010"), "R": ("110", "101", "110", "101", "101"),
    "T": ("111", "010", "010", "010", "010"), "U": ("101", "101", "101", "101", "011"),
    "_": ("000", "000", "000", "000", "111"), "-": ("000", "000", "111", "000", "000"),
    " ": ("000", "000", "000", "000", "000"),
    # The deck pieces' names: the rest of their letters, and digits for a size
    "K": ("101", "101", "110", "101", "101"), "P": ("110", "101", "110", "100", "100"),
    "S": ("011", "100", "010", "001", "110"), "V": ("101", "101", "101", "101", "010"),
    "W": ("101", "101", "111", "111", "101"), "X": ("101", "101", "010", "101", "101"),
    "Y": ("101", "101", "010", "010", "010"),
    "0": ("111", "101", "101", "101", "111"), "1": ("010", "110", "010", "010", "111"),
    "2": ("110", "001", "010", "100", "111"), "3": ("110", "001", "010", "001", "110"),
    "4": ("101", "101", "111", "001", "001"), "5": ("111", "100", "110", "001", "110"),
    "6": ("011", "100", "110", "101", "010"), "7": ("111", "001", "010", "010", "010"),
    "8": ("010", "101", "010", "101", "010"), "9": ("010", "101", "011", "001", "110"),
}


def draw_text(rows, x, y, text):
    for ch in text.upper():
        g = GLYPHS.get(ch)
        if g:
            for dy, line in enumerate(g):
                for dx, bit in enumerate(line):
                    if bit == "1" and 0 <= y + dy < len(rows) and 0 <= x + dx < len(rows[0]):
                        rows[y + dy][x + dx] = LABEL
        x += 4


# ---------------------------------------------------------------------------
# Slice
# ---------------------------------------------------------------------------
BOOTSTRAP = """\
; =============================================================================
; {name} -- written by themeslice.py because this file was absent.
;
; It is yours now: re-slicing the master NEVER overwrites an existing ini, so
; edit freely.
; =============================================================================


; --- SLICES ------------------------------------------------------------------
; size  corner size in GRID CELLS, whole, 1-12. The number IS the margin.
;
;       HOW BIG IS A CELL, IN REAL PIXELS. One grid cell is 0.55% of the screen
;       WIDTH, and a corner is square on screen, so `size` x that is what your
;       64-pixel corner art is resampled to:
;
;                     1920x1080    2560x1440    3840x2160
;           size = 1     11 px        14 px        21 px
;           size = 2     21 px        28 px        42 px
;           size = 3     32 px        42 px        63 px
;
;       So the art is MINIFIED everywhere -- 3x at size 2 on the commonest
;       display, 6x at size 1 -- and never magnified, which is the safe
;       direction: it cannot go blocky, it can only lose detail. Design for the
;       SMALLEST number in the row you pick: at size 1, three texels of your 64
;       are half a pixel at 1080p and simply will not be there. A 2-pixel
;       bevels survive because they are ~6 texels wide, measured.
;
;       If a motif needs more presence, raise `size` before re-cutting the art:
;       it is the only knob that buys the same picture more pixels.
; tint  0 = art carries its own colours; do not tint. THE DEFAULT, because art
;           painted in a master almost always does, and getting this wrong is
;           silent: baked colour times a near-black background is near-black,
;           which reads as "the theme did not load" rather than as a setting.
;       1 = art is white + alpha, so the HUD's background colour recolours it.
;           Switch to this only if you painted in white and shaped with alpha.

[frame]
size = 3
tint = 0

; One switch per panel family: hud-* tables and graphs, widget-* gauges,
; settings-* the menu.
[card]
size                = 2
hud-title-band      = 1
hud-content         = 1
widget-title-band   = 1
widget-content      = 1
settings-title-band = 1
settings-content    = 1

[button]
size = 1


; --- ICONS (optional) ---------------------------------------------------------
; A theme may restyle the icon set by dropping .tga files in an `icons` subfolder
; beside this file:
;
;     <theme>/icons/hud-map.tga
;
; RESTYLE, NEVER EXTEND. A file only takes effect if it matches an icon the base
; set already has; anything else is ignored with a warning in the log. The reason
; is that a rider's marker is saved by NAME, so a theme that could add or remove
; names would orphan saved choices the moment it was switched off. New glyphs
; belong in your own icons folder under Documents, which extends the base set for
; every theme at once.
;
; Fallback is per FILE, so overriding three icons inherits the other hundred-odd.
;
; WHICH ONES MATTER MOST: the thirty `hud-*` icons are the interface -- HUD titles
; and the settings tab list -- so restyling those changes the whole UI. Everything
; else is the marker vocabulary a user picks from for tracked riders on the map,
; radar and standings.
;
; Two roles, two looks, and the base set follows them strictly (see
; assets/icons/README.md): `hud-*` are FLAT with no outline, because the title copy
; gets an in-game drop shadow and the settings-tab copy is drawn plain -- an outline
; baked into one of those reads as a heavier icon in the tab list. Marker icons carry
; a 2px outline for contrast over the track.


; --- COLOURS -----------------------------------------------------------------
; Ten semantic slots as #rrggbb. Absent = follow the built-in default.
; Precedence: built-in default -> this file -> whatever you pin in Appearance.

;[colors]
;primary    = #ffffff
;background = #000000


; --- FONTS -------------------------------------------------------------------
; The .fnt filename without its extension. Absent = follow the built-in default.

;[fonts]
;title  = EnterSansman-Italic
;normal = RobotoMono-Regular
"""


def crop(rows, x, y, w, h):
    return [row[x:x + w] for row in rows[y:y + h]]


def mean(pixels):
    n = len(pixels)
    return tuple(sum(p[i] for p in pixels) / n for i in range(4))


def seam_warnings(cells, sets=SETS):
    """The one art rule nothing else can check.

    nine_slice.h: "the edge sprite's inner value must equal the center sprite's
    value, or the independently-stretched slices show a hard seam inset from the
    panel edge." Only something holding all nine slices at once can see it, which
    is this and nothing else in the toolchain.

    Compares the edge's INNER line -- slices are drawn as authored, so v=0 is the
    outer side for top/left and the far side for bottom/right -- against the
    centre border it abuts.
    """
    out = []
    for s in sets:
        c = cells[(s, "center")]
        checks = (
            ("edge_top", [r[:] for r in cells[(s, "edge_top")]][-1], c[0]),
            ("edge_bottom", cells[(s, "edge_bottom")][0], c[-1]),
            ("edge_left", [r[-1] for r in cells[(s, "edge_left")]], [r[0] for r in c]),
            ("edge_right", [r[0] for r in cells[(s, "edge_right")]], [r[-1] for r in c]),
        )
        for part, edge_line, centre_line in checks:
            if not edge_line or not centre_line:
                continue
            a, b = mean(edge_line), mean(centre_line)
            d = max(abs(a[i] - b[i]) for i in range(4))
            if d > 8:
                out.append(f"{s}_{part}: inner edge differs from centre by {d:.0f}/255 "
                           f"-- a visible seam will show inset from the panel edge")
    return out


def button_warnings(cells):
    """Button art must be WHITE + alpha, or its state colour is multiplied away.

    Unlike frame and card slices, button sprites always take the CALLER's colour --
    that colour is what encodes disabled / idle / hovered, and (once the plugin started
    drawing buttons opaque) green / red as well. The sprite modulates it, so art painted
    at 60,60,60 renders every one of those states at a quarter strength and they stop
    being distinguishable. `tint` does not govern this; buttons ignore it.

    Measured across the shipped themes when this was added: debug 255 (right), a bevelled theme 178
    (a 30% loss, tolerable), mxbikes 60 (a 76% loss, which is what prompted it).
    """
    out = []
    # THE CENTRE ONLY. It is the button's face -- the area a state colour has to read
    # across -- while the edges are a border and are legitimately darker for a bevel or
    # an outline. Checking them too flagged `debug`, whose slices are deliberately
    # garish so slice placement is visible, which is the kind of false alarm that gets a
    # warning ignored.
    cell = cells.get(("button", "center"))
    if not cell:
        return out
    px = [p for row in cell for p in row if p[3] > 8]         # ignore fully clear pixels
    if not px:
        return out
    lo = min(min(p[:3]) for p in px)
    if lo < 224:
        out.append(f"button_center: art is {lo}/255 at its darkest, not white -- a "
                   f"button's colour carries its STATE (disabled / idle / hovered, and "
                   f"any green/red you give it) and the sprite MULTIPLIES it, so every "
                   f"state renders at {lo * 100 // 255}% strength and they stop being "
                   f"distinguishable. Paint the button face white and shape it with alpha.")
    return out


# ---------------------------------------------------------------------------
# The .cells file
# ---------------------------------------------------------------------------
def cells_path(master):
    return os.path.splitext(master)[0] + ".cells"


def write_cells(master, cells, out=None):
    """The sheet's .cells beside it; `out` (a folder) is written relative to it."""
    with open(cells_path(master), "w", encoding="utf-8") as f:
        f.write(f"; Cells of {os.path.basename(master)} (tools/themeslice): one picture per\n"
                f"; line, <name> x y w h. Cutting writes each to <out>/<name>.tga.\n")
        if out:
            rel = os.path.relpath(out, os.path.dirname(os.path.abspath(master)))
            f.write(f"out = {rel.replace(os.sep, '/')}\n")
        for c in cells:
            f.write("%s %d %d %d %d\n" % c)


def read_cells(master):
    """(out folder or None, [(name, x, y, w, h)]) from the master's .cells file."""
    path = cells_path(master)
    if not os.path.exists(path):
        raise SystemExit(f"{master}: no {os.path.basename(path)} beside it -- a master "
                         f"is cut by the cells listed there (--template and --from-theme "
                         f"write both)")
    out, cells = None, []
    for n, line in enumerate(open(path, encoding="utf-8"), 1):
        line = line.split(";", 1)[0].strip()
        if not line:
            continue
        key, eq, value = line.partition("=")
        if eq and key.strip() == "out":
            out = os.path.normpath(os.path.join(os.path.dirname(path), value.strip()))
            continue
        parts = line.split()
        if len(parts) != 5 or not all(p.isdigit() for p in parts[1:]):
            raise SystemExit(f"{path}:{n}: expected `<name> x y w h`, got `{line}`")
        cells.append((parts[0], *map(int, parts[1:])))
    return out, cells


def set_cells(pictures):
    """{(set, part): pixels} for the nine-slice pictures among {name: pixels}."""
    return {(s, part): pictures[f"{s}_{part}"] for s in SETS_WITH_TITLE
            for row in PARTS for part in row if f"{s}_{part}" in pictures}


# ---------------------------------------------------------------------------
# Template and cut
# ---------------------------------------------------------------------------
def make_template(path, corner=CORNER, sets=SETS, ends=False):
    pictures = [(f"frame_{p}", *end_size("frame", corner))
                for p in ("slant_left", "slant_right")] if ends else []
    w, h, cells, captions = plan(corner, sets=sets, pictures=pictures)
    rows = [[GUIDE for _ in range(w)] for _ in range(h)]
    # Cells start fully transparent: the artist paints, and anything unpainted
    # renders as nothing rather than as a colour they did not choose.
    for _, x, y, cw, ch in cells:
        for yy in range(y, y + ch):
            rows[yy][x:x + cw] = [CLEAR] * cw
    for x, y, text in captions:
        draw_text(rows, x, y, text)
    write_png(path, rows)
    write_cells(path, cells)
    return w, h


def slice_master(master, outdir=None, force_ini=False):
    """Cut a master by its .cells; (folder, names written, art warnings)."""
    out, cells = read_cells(master)
    outdir = outdir or out
    if not outdir:
        raise SystemExit(f"{cells_path(master)}: no `out = <folder>` line, and no --out")
    w, h, rows = read_image(master)
    os.makedirs(outdir, exist_ok=True)
    pictures, written = {}, []
    for name, x, y, cw, ch in cells:
        if x + cw > w or y + ch > h:
            raise SystemExit(f"{master}: cell {name} runs past the {w}x{h} sheet")
        pictures[name] = crop(rows, x, y, cw, ch)
        write_tga(os.path.join(outdir, name + ".tga"), pictures[name])
        written.append(name + ".tga")

    # A sheet with a frame is a theme, and a theme needs its ini
    if "frame_corner_tl" in pictures:
        ini = os.path.join(outdir, "theme.ini")
        if force_ini or not os.path.exists(ini):
            name = os.path.basename(os.path.normpath(outdir))
            open(ini, "w").write(BOOTSTRAP.format(name=name))
            written.append("theme.ini  (bootstrap)")
        else:
            written.append("theme.ini  (kept -- already exists)")

    sc = set_cells(pictures)
    sets = tuple(s for s in SETS_WITH_TITLE if all((s, p) in sc for row in PARTS for p in row))
    return outdir, written, seam_warnings(sc, sets) + button_warnings(sc)


# ---------------------------------------------------------------------------
# The inverse: assemble an existing theme folder back into a master, so a theme
# that was authored slice-by-slice can join the master workflow.
#
# LOSSLESS OR REFUSED -- never resampled. A slice whose size does not match its
# cell is accepted only when it is UNIFORM along the axis that differs, which is
# the normal case: the stretched axes carry no detail by definition, so the
# shipped themes store a centre as one flat colour at whatever size the old
# generator happened to emit. Anything else would need interpolation, and quietly
# resampling somebody's art is how a conversion tool loses their work.
# ---------------------------------------------------------------------------
def can_fit(src, cw, ch):
    """Whether src can BECOME cw x ch without inventing or discarding detail.

    Same rule in both directions -- a 16px flat edge widening into a 64px square
    cell is the assemble path, a 64px one narrowing is the reverse -- because the
    differing axis being uniform is what makes either lossless.
    """
    sh, sw = len(src), len(src[0])
    if (sw, sh) == (cw, ch):
        return True
    if sw != cw and len({tuple(r[x] for r in src) for x in range(sw)}) != 1:
        return False
    if sh != ch and len({tuple(r) for r in src}) != 1:
        return False
    return True


def fit_cell(src, cw, ch, label):
    sh, sw = len(src), len(src[0])
    if (sw, sh) == (cw, ch):
        return [r[:] for r in src]
    if not can_fit(src, cw, ch):
        axis = "columns" if sw != cw else "rows"
        raise SystemExit(f"{label}: is {sw}x{sh}, needs {cw}x{ch}, and its {axis} "
                         f"differ -- resizing would resample. Re-cut from a master.")
    return [[src[y if sh == ch else 0][x if sw == cw else 0] for x in range(cw)]
            for y in range(ch)]



def assemble(theme_dir, out_png):
    """A master and its .cells from a theme folder's top-level .tga.

    The sets go in as 3x3 blocks (widened to square cells where can_fit allows);
    every other picture -- slanted ends, deck pictures -- goes in whole, as it is.
    """
    tgas = sorted(f[:-4] for f in os.listdir(theme_dir) if f.lower().endswith(".tga"))
    have = set(tgas)
    # A set rides along when its centre is there (title_center enables the title
    # set, as in the plugin); then all nine of its files are required.
    sets = tuple(s for s in SETS_WITH_TITLE if f"{s}_center" in have)
    if "frame" not in sets:
        raise SystemExit(f"{theme_dir}: no frame_center.tga -- not a theme folder")
    missing = [f"{s}_{p}" for s in sets for row in PARTS for p in row
               if f"{s}_{p}" not in have]
    if missing:
        raise SystemExit(f"{theme_dir}: missing {len(missing)} slice(s), first "
                         f"{missing[0]}.tga -- assemble needs every slice of each set")

    pics = {n: read_tga(os.path.join(theme_dir, n + ".tga")) for n in tgas}
    corner, ch, _ = pics["frame_corner_tl"]
    if corner != ch:
        raise SystemExit(f"{theme_dir}: frame_corner_tl is {corner}x{ch}, expected square")
    in_sets = {f"{s}_{p}" for s in sets for row in PARTS for p in row}
    others = [(n, pics[n][0], pics[n][1]) for n in tgas if n not in in_sets]

    # SQUARE CELLS when the art allows it, which for a theme whose stretched axes
    # are flat -- every theme authored before square cells existed -- it does: the
    # differing axis is uniform, so widening it is replication, not resampling
    # (see can_fit). A theme that DOES carry detail along a stretched axis keeps
    # its own size instead of being refused; the sheet is then not square, and
    # that is the honest answer rather than a lossy one.
    def all_fit(stretch):
        cells = plan(corner, stretch, sets, others)[2]
        return all(can_fit(pics[n][2], cw, ch_) for n, _, _, cw, ch_ in cells if n in in_sets)

    stretch = corner if all_fit(corner) else pics["frame_edge_top"][0]
    w, h, cells, captions = plan(corner, stretch, sets, others)
    # Clear, not GUIDE: this master is real art, and a gutter painted some opaque
    # grey would read as part of it the moment anyone opened the file.
    rows = [[CLEAR for _ in range(w)] for _ in range(h)]
    for x, y, text in captions:
        draw_text(rows, x, y, text)
    for n, x, y, cw, ch_ in cells:
        cell = fit_cell(pics[n][2], cw, ch_, n)
        for dy in range(ch_):
            rows[y + dy][x:x + cw] = cell[dy]
    write_png(out_png, rows)
    write_cells(out_png, cells, theme_dir)
    return w, h, len(cells)


def check_shipped():
    """Every shipped master (assets/themes/*.png with an `out`) cuts into exactly
    its theme's top-level pictures, byte for byte: none edited without its master,
    none missing from it, none left over."""
    import tempfile
    root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
    masters = os.path.join(root, "assets", "themes")
    checked = 0
    for f in sorted(os.listdir(masters)):
        if not f.endswith(".png"):
            continue
        master = os.path.join(masters, f)
        out, cells = read_cells(master)
        if not out:
            continue                      # debug: a measuring sheet, never shipped
        shipped = sorted(x[:-4] for x in os.listdir(out) if x.lower().endswith(".tga"))
        assert shipped == sorted(c[0] for c in cells), \
            f"{f}: {out} holds {shipped}, the master cuts {sorted(c[0] for c in cells)}"
        with tempfile.TemporaryDirectory() as td:
            slice_master(master, td)
            for name, *_ in cells:
                a = open(os.path.join(td, name + ".tga"), "rb").read()
                b = open(os.path.join(out, name + ".tga"), "rb").read()
                assert a == b, f"{f}: {name}.tga differs from its cell -- edit the master and re-cut"
                checked += 1
    return checked


# ---------------------------------------------------------------------------
# Self-test: the round trip. Cutting is only trustworthy if what comes out
# reassembles into what went in, so that is what is asserted -- on ASYMMETRIC
# art, since a symmetric picture cannot detect a transposed cell.
# ---------------------------------------------------------------------------
def selftest():
    import tempfile

    # ALPHA VARIES. A rounded or glass theme is mostly alpha, so a round trip that
    # only ever carries 255 proves nothing about the case that matters -- and both
    # formats here hand-roll their pixel loops, where dropping a channel is exactly
    # the kind of slip that survives an opaque test.
    def px(x, y):
        return ((x * 7 + 11) % 256, (y * 13 + 29) % 256, (x * y) % 256, (x * 5 + y * 3) % 256)

    def pic(w, h, seed):
        return [[px(x + seed, y + 3 * seed) for x in range(w)] for y in range(h)]

    def same(a, b):
        return open(a, "rb").read() == open(b, "rb").read()

    with tempfile.TemporaryDirectory() as td:
        rows = pic(37, 23, 1)
        src = os.path.join(td, "m.png")
        write_png(src, rows)
        assert read_png(src)[2] == rows, "PNG round-trip lost data"

    # A WHOLE THEME FOLDER -- four sets, slanted ends, deck pictures of odd sizes
    # -- assembles into a master and cuts back byte for byte. Captions are drawn
    # with the art, so one landing inside a cell would show up here as a change.
    with tempfile.TemporaryDirectory() as td:
        theme = os.path.join(td, "theme")
        os.makedirs(theme)
        names = [f"{s}_{p}" for s in SETS_WITH_TITLE for row in PARTS for p in row]
        sizes = {n: (64, 64) for n in names}
        sizes.update({"frame_slant_left": end_size("frame", 64),
                      "frame_slant_right": end_size("frame", 64),
                      "deck_tab": (11, 5), "deck_well_7x19": (7, 19), "deck_wide": (300, 12)})
        for i, (n, (w, h)) in enumerate(sizes.items()):
            write_tga(os.path.join(theme, n + ".tga"), pic(w, h, i))
        m = os.path.join(td, "art", "theme.png")
        os.makedirs(os.path.dirname(m))
        assemble(theme, m)
        out, cells = read_cells(m)
        assert out == os.path.normpath(theme), "the cells' out does not name the folder"
        assert sorted(c[0] for c in cells) == sorted(sizes), "a picture is missing from the cells"
        for i, a in enumerate(cells):            # no two cells share a pixel
            for b in cells[i + 1:]:
                assert (a[1] + a[3] <= b[1] or b[1] + b[3] <= a[1] or
                        a[2] + a[4] <= b[2] or b[2] + b[4] <= a[2]), f"{a[0]} overlaps {b[0]}"
        cut = os.path.join(td, "cut")
        _, written, _ = slice_master(m, cut)
        assert len([x for x in written if x.endswith(".tga")]) == len(sizes), written
        for n in sizes:
            assert same(os.path.join(cut, n + ".tga"), os.path.join(theme, n + ".tga")), \
                f"{n} changed through the round trip"
        assert slice_master(m)[0] == os.path.normpath(theme), "a cut without --out ignores `out`"

        # A theme.ini is bootstrapped, and an existing one is never clobbered.
        ini = os.path.join(cut, "theme.ini")
        assert os.path.exists(ini), "no theme.ini bootstrapped"
        open(ini, "w").write("; mine\n")
        slice_master(m, cut)
        assert open(ini).read() == "; mine\n", "bootstrap overwrote a real ini"

        # The seam check fires: a centre that disagrees with its edges.
        bad = os.path.join(td, "bad")
        os.makedirs(bad)
        for f in os.listdir(theme):
            if f.endswith(".tga"):
                open(os.path.join(bad, f), "wb").write(open(os.path.join(theme, f), "rb").read())
        flat = [[(255, 0, 255, 255)] * 64 for _ in range(64)]
        write_tga(os.path.join(bad, "frame_center.tga"), flat)
        assemble(bad, os.path.join(td, "bad.png"))
        warns = slice_master(os.path.join(td, "bad.png"), os.path.join(td, "badcut"))[2]
        assert any("frame_edge" in x for x in warns), "seam check stayed quiet on a bad centre"

    # A pre-square theme (flat 8 px edges and centre) assembles into SQUARE
    # cells, and cutting that reproduces the corners byte-for-byte.
    with tempfile.TemporaryDirectory() as td:
        old = os.path.join(td, "old")
        os.makedirs(old)
        oc, os_ = 32, 8
        for s in SETS:
            for row in PARTS:
                for part in row:
                    cw = oc if "corner" in part or part in ("edge_left", "edge_right") else os_
                    ch = oc if "corner" in part or part in ("edge_top", "edge_bottom") else os_
                    cell = [[px(x, y) if "corner" in part else (9, 9, 9, 255)
                             for x in range(cw)] for y in range(ch)]
                    write_tga(os.path.join(old, f"{s}_{part}.tga"), cell)
        m = os.path.join(td, "assembled.png")
        assemble(old, m)
        assert all(c[3] == oc and c[4] == oc for c in read_cells(m)[1]), "cells are not square"
        new = os.path.join(td, "recut")
        slice_master(m, new)
        for s in SETS:
            assert same(os.path.join(new, f"{s}_corner_tl.tga"),
                        os.path.join(old, f"{s}_corner_tl.tga")), \
                f"{s}_corner_tl changed through the square conversion"

    # TEMPLATES: every cell cuts CLEAR at every corner (no caption or guide pixel
    # in the art), a 2x corner is exactly 2x the sheet, and --title / --ends add
    # their cells.
    with tempfile.TemporaryDirectory() as td:
        for c in (16, 32, 64, 128):
            t = os.path.join(td, f"t{c}.png")
            make_template(t, c, SETS_WITH_TITLE, ends=True)
            _, written, _ = slice_master(t, os.path.join(td, f"c{c}"))
            assert len([x for x in written if x.endswith(".tga")]) == 38, written
            for x in written:
                if x.endswith(".tga"):
                    _, _, cell = read_tga(os.path.join(td, f"c{c}", x))
                    assert all(p == CLEAR for r in cell for p in r), f"--corner {c}: {x} is not clear"
        for mult in (2, 4):
            assert plan(64 * mult)[:2] == tuple(v * mult for v in plan(64)[:2]), \
                f"{mult}x sheet is not scaled exactly"
        assert len(plan(64)[2]) == 27, "a plain template is not 27 cells"

    shipped = check_shipped()
    print(f"selftest: a theme folder (4 sets, slanted ends, odd-sized pictures, alpha "
          f"included) round-trips through its master and .cells byte for byte; ini "
          f"bootstrapped and preserved; seam check fires; a pre-square theme assembles "
          f"square losslessly; templates cut clear at every corner and scale exactly; "
          f"the shipped masters cut into their {shipped} pictures byte for byte")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("master", nargs="?", help="master image (.png or .tga), cut by its .cells")
    ap.add_argument("--out", help="folder to cut into (default: the .cells' `out`); "
                                  "with --from-theme, the master .png to write")
    ap.add_argument("--template", metavar="PNG", help="write a blank master + .cells and exit")
    ap.add_argument("--from-theme", metavar="DIR",
                    help="assemble a theme folder's .tga into a master + .cells and exit "
                         "(writes DIR/_master.png unless --out names a .png)")
    ap.add_argument("--force-ini", action="store_true",
                    help="overwrite an existing theme.ini (off by default -- yours is kept)")
    ap.add_argument("--corner", type=int, default=CORNER,
                    help=f"--template: corner size in px, also each edge's thickness "
                         f"(default {CORNER})")
    ap.add_argument("--title", action="store_true",
                    help="--template: add the optional title set (title_*.tga, the "
                         "title band's own art) below the other three")
    ap.add_argument("--ends", action="store_true",
                    help="--template: add the frame's slanted ends below the sets")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()

    if a.selftest:
        selftest()
        return 0
    if a.from_theme:
        out = (a.out if a.out and a.out.lower().endswith(".png")
               else os.path.join(a.from_theme, "_master.png"))
        w, h, n = assemble(a.from_theme, out)
        print(f"wrote {out} ({w}x{h}, {n} pictures) and {os.path.basename(cells_path(out))}")
        return 0
    if a.template:
        w, h = make_template(a.template, a.corner,
                             SETS_WITH_TITLE if a.title else SETS, a.ends)
        print(f"wrote {a.template} ({w}x{h}) and {os.path.basename(cells_path(a.template))} "
              f"-- paint inside the cells, the gutter is ignored")
        return 0
    if not a.master:
        ap.error("need a master (or --template / --from-theme / --selftest)")

    outdir, written, warns = slice_master(a.master, a.out, a.force_ini)
    print(f"{a.master} -> {outdir}")
    for f in written:
        print(f"  {f}")
    for w in warns:
        print(f"  WARNING  {w}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
