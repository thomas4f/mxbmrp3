#!/usr/bin/env python3
"""Cut a MASTER IMAGE into a theme's 27 slice files, plus a bootstrap ini.

WHY THIS EXISTS, given the theme system deliberately has no art tooling. A theme
is 27 files that must look like each other -- a card is a quieter frame, a button
a smaller card -- and 27 separate files is the worst possible shape for keeping
them consistent, whether the hand is yours or an agent's. Drawn together in one
image they stay together by construction.

WHAT IT DOES NOT DO, which is the line the old generator crossed: it never draws
art. It cuts the image you gave it, losslessly, at positions the template it also
emits already fixed. Every pixel out is a pixel in.

THE MASTER LOOKS LIKE THE PANEL. Each set is a 3x3 arrangement in the order the
slices actually appear on screen:

    corner_tl   edge_top      corner_tr
    edge_left   center        edge_right
    corner_bl   edge_bottom   corner_br

so you are drawing a panel, not filling in a sprite sheet. The three sets --
frame, card, button -- stack down the image, in the ini's section order.

EVERY CELL IS THE SAME SQUARE, so the master really is a picture of a panel: a
stroke drawn across a cell boundary lands on both sides at the same scale, and a
corner motif can be carried a little way along its edges. The stretched axes
(edge lengths, the centre) are scaled to the panel at draw time, so their
resolution costs nothing on screen -- it was 16px once, and unequal cells turned
out to cost more in authoring than the pixels they saved. See STRETCH.

GUTTERS ARE NOT ART. Cells are separated by a gutter that no slice ever reads, so
the template can label and outline each cell without any of that reaching a .tga.
Paint inside the cells; whatever is left in the gutter is ignored.

GEOMETRY IS SHARED between --template and the slicer (see layout()), so the cuts
cannot drift from the guides the template drew -- and slicing infers it from the
master's dimensions (see infer_geometry), so there is nothing to pass and nothing
to get wrong.

    themeslice.py --template master.png          # start a new theme
    themeslice.py master.png --out <theme dir>   # cut it
    themeslice.py --selftest                     # round-trip check (the CI gate)

Stdlib only -- zlib for PNG, everything else by hand -- so it runs anywhere the
repo's other python gates do, with nothing to install.
"""
import argparse
import os
import struct
import sys
import zlib

# ---------------------------------------------------------------------------
# Geometry. ONE definition, read by both the template writer and the slicer.
# ---------------------------------------------------------------------------
# Defaults for --template. A slice does not need them: infer_geometry() recovers
# both from the master itself, so pointing the tool at an image is enough.
CORNER = 64      # corner art is CORNER x CORNER; also each edge's THICKNESS
STRETCH = CORNER # the stretched axis: edge length and the centre.
#
# EVERY CELL IS THE SAME SQUARE, which is an AUTHORING decision and not a
# rendering one -- the renderer stretches these axes to the panel either way, so
# their resolution is free. It used to be 16px on exactly that reasoning
# ("detail belongs in the corners"), and free-but-small turned out to cost more
# than the pixels it saved: a 3x3 of unequal cells does not read as a panel, so
# the master stopped looking like the thing it becomes. You cannot draw a stroke
# ACROSS a boundary and have it land, because the two sides are at different
# scales; a corner motif cannot be carried a little way along its edges; and
# every art tool's grid, guides and symmetry are set up for equal cells.
#
# Square costs ~1.8x the .tga bytes for a flat theme (uncompressed TGA, and
# these compress to nearly nothing in the installer) and nothing at all on
# screen. --stretch still overrides, and slicing INFERS the shape, so both the
# old 16px masters and square ones cut correctly with no flags.


# The gutter is DERIVED from the corner, not fixed, so a master scales UNIFORMLY:
# --corner 128 lands on exactly 2x the pixels of the default. With a fixed gutter
# it does not -- a 200% master is 352 wide while the tool computes 320 -- and
# someone making a hi-dpi version of their art hits that immediately.
# --gutter overrides for a master that was not built this way.
def gutter_for(corner):
    return max(2, corner // 8)


GUTTER = gutter_for(64)   # 8, the shipped default

SETS = ("frame", "card", "button")

# Row-major, matching the 3x3 picture above.
PARTS = (
    ("corner_tl", "edge_top", "corner_tr"),
    ("edge_left", "center", "edge_right"),
    ("corner_bl", "edge_bottom", "corner_br"),
)

GUIDE = (150, 160, 190, 48)    # --template gutter: SEMI-transparent, so cells
                               # are visible to work in but nothing reads as art.
                               # An assembled master gets a fully clear gutter.
CLEAR = (0, 0, 0, 0)
LABEL = (120, 130, 150, 255)   # label glyphs, drawn in the gutter only


def infer_geometry(w, h):
    """(corner, stretch) for a master of this size, or None if it is not one.

    Once the gutter derives from the corner the layout is two equations in two
    unknowns -- W = 2.5c + s and H = 7.25c + 3s -- so eliminating s gives
    c = 4*(3W - H) and s = W - 2.5c. It was NOT solvable while the gutter was a
    fixed 8: that left one equation for two unknowns, which is why this used to
    demand --corner/--stretch and why getting them wrong was the tool's most
    likely error. Making the gutter derived to fix uniform scaling removed the
    need for the flags as a side effect, which went unnoticed for a while.

    Solved, then VERIFIED by rebuilding the layout: the algebra assumes the exact
    gutter rule, and a size that merely satisfies the equations is not a master.
    """
    n = 4 * (3 * w - h)
    if n <= 0 or n % 8:
        return None
    corner = n
    if corner % 2:                 # stretch = w - 5*corner/2 must be a whole pixel
        return None
    stretch = w - (5 * corner) // 2
    if stretch <= 0 or corner <= 0:
        return None
    if layout(corner, stretch)[:2] != (w, h):
        return None
    return corner, stretch


def layout(corner=CORNER, stretch=None, gutter=None):
    """(master_w, master_h, {(set, part): rect}, {set: block_top_y}) -- the contract.

    `stretch` defaults to the CORNER, i.e. square cells -- so a caller that scales
    the corner gets a master that stays square instead of one whose middle band
    silently keeps the old size.
    """
    stretch = corner if stretch is None else stretch
    g = gutter_for(corner) if gutter is None else gutter
    col_w = (corner, stretch, corner)
    row_h = (corner, stretch, corner)
    col_x, x = [], g
    for w in col_w:
        col_x.append(x)
        x += w + g
    master_w = x
    block_h = sum(row_h) + 2 * g

    rects, tops, y0 = {}, {}, g
    for s in SETS:
        tops[s] = y0
        row_y, y = [], y0
        for h in row_h:
            row_y.append(y)
            y += h + g
        for r, row in enumerate(PARTS):
            for c, part in enumerate(row):
                rects[(s, part)] = (col_x[c], row_y[r], col_w[c], row_h[r])
        y0 += block_h + g
    return master_w, y0, rects, tops


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
# Template
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


def make_template(path, corner=CORNER, stretch=None, gutter=None):
    w, h, rects, tops = layout(corner, stretch, gutter)
    rows = [[GUIDE for _ in range(w)] for _ in range(h)]
    # Cells start fully transparent: the artist paints, and anything unpainted
    # renders as nothing rather than as a colour they did not choose.
    for (x, y, cw, ch) in rects.values():
        for yy in range(y, y + ch):
            for xx in range(x, x + cw):
                rows[yy][xx] = (0, 0, 0, 0)
    # ONE caption per block, in the gutter above it. Position already says which
    # slice a cell is -- that is the whole point of laying the master out as the
    # panel -- so only the three SETS need naming. (Captioning every cell was the
    # first attempt: at the old 16px stretch a caption ran straight across its
    # neighbour, and now that cells are square it would just be noise inside art
    # you are trying to draw across.)
    # The caption sits in the ACTUAL gutter, which is gutter_for(corner) and not the
    # module default. Using the default put a 5px-tall caption above a gutter smaller
    # than 6 -- i.e. inside the previous block's bottom row of CELLS -- so a --corner 32
    # template silently baked label pixels into corner_bl. The whole promise of the
    # gutter is that nothing in it reaches a .tga, so this is that promise held: the
    # caption is skipped outright when the gutter cannot hold a glyph.
    g = gutter_for(corner) if gutter is None else gutter
    GLYPH_H = 5
    for s, y in tops.items():
        if y - GLYPH_H - 1 < 0 or g < GLYPH_H + 1:
            continue                      # no room in the gutter; a label is not worth art
        draw_text(rows, g, y - GLYPH_H - 1, s)
    write_png(path, rows)
    return w, h


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


def seam_warnings(cells):
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
    for s in SETS:
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


def slice_master(master, outdir, name, force_ini=False, corner=None, stretch=None,
                 gutter=None):
    w, h, rows = read_image(master)
    if corner is None or stretch is None:
        guess = infer_geometry(w, h)
        if not guess:
            raise SystemExit(
                f"{master}: {w}x{h} is not a master layout. A master is 2.5*corner + "
                f"stretch wide and 7.25*corner + 3*stretch tall -- 176x512 at the "
                f"defaults, or any uniform scale of one. Start from --template, or "
                f"pass --corner/--stretch if the master uses a custom gutter.")
        corner, stretch = guess
    mw, mh, rects = layout(corner, stretch, gutter)[:3]
    if (w, h) != (mw, mh):
        raise SystemExit(f"{master}: is {w}x{h}, but --corner {corner} --stretch "
                         f"{stretch} describes a {mw}x{mh} master.")

    os.makedirs(outdir, exist_ok=True)
    cells = {k: crop(rows, *r) for k, r in rects.items()}

    written = []
    for (s, part), cell in sorted(cells.items()):
        p = os.path.join(outdir, f"{s}_{part}.tga")
        write_tga(p, cell)
        written.append(os.path.basename(p))

    ini = os.path.join(outdir, f"{name}.ini")
    if force_ini or not os.path.exists(ini):
        open(ini, "w").write(BOOTSTRAP.format(name=name))
        written.append(os.path.basename(ini) + "  (bootstrap)")
    else:
        written.append(os.path.basename(ini) + "  (kept -- already exists)")

    # The master IS the source now, so it belongs beside what it cut into.
    # Without this the .tga are art with an invisible source, which is exactly
    # the problem the deleted theme generator had.
    keep = os.path.join(outdir, "_master" + os.path.splitext(master)[1].lower())
    if os.path.abspath(keep) != os.path.abspath(master):
        with open(master, "rb") as src, open(keep, "wb") as dst:
            dst.write(src.read())
        written.append(os.path.basename(keep) + "  (source copy)")

    return written, seam_warnings(cells) + button_warnings(cells)


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
    def slice_path(s, part):
        return os.path.join(theme_dir, f"{s}_{part}.tga")

    missing = [f"{s}_{p}" for s in SETS for row in PARTS for p in row
               if not os.path.exists(slice_path(s, p))]
    if missing:
        raise SystemExit(f"{theme_dir}: missing {len(missing)} slice(s), first "
                         f"{missing[0]}.tga -- assemble needs all 27")

    # Geometry comes from the art rather than the flags: a corner file states the
    # corner size, and an edge's stretched axis states the rest.
    cw, ch, _ = read_tga(slice_path("frame", "corner_tl"))
    if cw != ch:
        raise SystemExit(f"{theme_dir}: frame_corner_tl is {cw}x{ch}, expected square")

    cells = {(s, part): read_tga(slice_path(s, part))[2]
             for s in SETS for row in PARTS for part in row}

    # SQUARE CELLS when the art allows it, which for a theme whose stretched axes
    # are flat -- every theme authored before square cells existed -- it does: the
    # differing axis is uniform, so widening it is replication, not resampling
    # (see can_fit). A theme that DOES carry detail along a stretched axis keeps
    # its own size instead of being refused; the master is then not square, and
    # that is the honest answer rather than a lossy one.
    def all_fit(stretch):
        rects = layout(cw, stretch)[2]
        return all(can_fit(cell, rects[k][2], rects[k][3]) for k, cell in cells.items())

    art_stretch = read_tga(slice_path("frame", "edge_top"))[0]
    stretch = cw if all_fit(cw) else art_stretch

    w, h, rects = layout(cw, stretch)[:3]
    # Clear, not GUIDE: this master is real art, and a gutter painted some opaque
    # grey would read as part of it the moment anyone opened the file.
    rows = [[CLEAR for _ in range(w)] for _ in range(h)]
    for (s, part), (x, y, rw, rh) in rects.items():
        cell = fit_cell(cells[(s, part)], rw, rh, f"{s}_{part}")
        for dy in range(rh):
            rows[y + dy][x:x + rw] = cell[dy]
    write_png(out_png, rows)
    return w, h, cw, stretch


# ---------------------------------------------------------------------------
# Self-test: the round trip. Slicing is only trustworthy if what comes out
# reassembles into what went in, so that is what is asserted -- on an
# ASYMMETRIC master, since a symmetric one cannot detect a transposed cell.
# ---------------------------------------------------------------------------
def selftest():
    import tempfile
    w, h, rects = layout()[:3]

    # ALPHA VARIES. A rounded or glass theme is mostly alpha, so a round trip that
    # only ever carries 255 proves nothing about the case that matters -- and both
    # formats here hand-roll their pixel loops, where dropping a channel is exactly
    # the kind of slip that survives an opaque test.
    def px(x, y):
        return ((x * 7 + 11) % 256, (y * 13 + 29) % 256, (x * y) % 256, (x * 5 + y * 3) % 256)

    rows = [[px(x, y) for x in range(w)] for y in range(h)]
    with tempfile.TemporaryDirectory() as td:
        src = os.path.join(td, "m.png")
        write_png(src, rows)
        assert read_png(src)[2] == rows, "PNG round-trip lost data"

        out = os.path.join(td, "t")
        written, _ = slice_master(src, out, "t")
        assert len([x for x in written if x.endswith(".tga")]) == 27, written

        for (s, part), (x, y, cw, ch) in rects.items():
            got = read_tga(os.path.join(out, f"{s}_{part}.tga"))[2]
            want = [row[x:x + cw] for row in rows[y:y + ch]]
            assert got == want, f"{s}_{part} does not match its region of the master"

        # An existing ini is never clobbered.
        ini = os.path.join(out, "t.ini")
        open(ini, "w").write("; mine\n")
        slice_master(src, out, "t")
        assert open(ini).read() == "; mine\n", "bootstrap overwrote a real ini"

        # And the seam check actually fires: a centre that disagrees with its edges.
        bad = [row[:] for row in rows]
        cx, cy, cw, ch = rects[("frame", "center")]
        for yy in range(cy, cy + ch):
            for xx in range(cx, cx + cw):
                bad[yy][xx] = (255, 0, 255, 255)
        src2 = os.path.join(td, "bad.png")
        write_png(src2, bad)
        _, warns = slice_master(src2, os.path.join(td, "t2"), "t2")
        assert any("frame_edge" in x for x in warns), "seam check stayed quiet on a bad centre"

    # Geometry is recovered from the image, so nobody has to remember what a master
    # was built at. The 168x488 case is the one that bit in practice: a master
    # assembled from an existing theme has stretch=8, not the template's 16, and
    # nothing on the image says which you are holding.
    # 224x656 is the SQUARE default; the rest are pre-square masters, which still
    # have to cut correctly -- a user's theme folder does not get re-authored
    # because the tool's default changed.
    for (mw, mh), want in {(224, 656): (64, 64), (112, 328): (32, 32),
                           (176, 512): (64, 16), (168, 488): (64, 8),
                           (352, 1024): (128, 32), (88, 256): (32, 8)}.items():
        assert infer_geometry(mw, mh) == want, f"{mw}x{mh} inferred wrong"
    for bogus in ((500, 500), (177, 512), (176, 511), (1, 1)):
        assert infer_geometry(*bogus) is None, f"{bogus} should not read as a master"

    # Uniform scaling: 2x every dimension and the layout has to land on exactly
    # twice the pixels, or somebody making a hi-dpi master gets a size mismatch.
    for mult in (2, 4):
        big = layout(64 * mult)[:2]
        assert big == (w * mult, h * mult), f"{mult}x master is {big}, expected scaled"

    # SQUARE IS THE DEFAULT, and scaling the corner alone keeps it square -- the
    # trap a `stretch=STRETCH` default would reintroduce, where --corner 128 gives
    # a master with 128px corners and a 64px middle band.
    for c in (32, 64, 128):
        rects = layout(c)[2]
        assert all(r[2] == c and r[3] == c for r in rects.values()), \
            f"--corner {c} did not give square cells"

    # A pre-square theme assembles into a SQUARE master (its stretched axes are
    # flat, so widening them is replication), and cutting that master reproduces
    # the corners byte-for-byte -- the property that makes the conversion safe to
    # run over the shipped themes.
    with tempfile.TemporaryDirectory() as td:
        old = os.path.join(td, "old")
        os.makedirs(old)
        oc, os_ = 32, 8
        for s in SETS:
            for row in PARTS:
                for part in row:
                    cwid = oc if "corner" in part or part == "edge_left" or part == "edge_right" else os_
                    chgt = oc if "corner" in part or part in ("edge_top", "edge_bottom") else os_
                    if part == "center":
                        cwid = chgt = os_
                    cell = [[px(x, y) if "corner" in part else (9, 9, 9, 255)
                             for x in range(cwid)] for y in range(chgt)]
                    write_tga(os.path.join(old, f"{s}_{part}.tga"), cell)
        m = os.path.join(td, "assembled.png")
        aw, ah, ac, ast = assemble(old, m)
        assert (ac, ast) == (oc, oc), f"assemble gave {ac}x{ast}, expected square {oc}"
        assert (aw, ah) == layout(oc)[:2], "assembled master is not the square layout"
        new = os.path.join(td, "recut")
        slice_master(m, new, "recut")
        for s in SETS:
            got = read_tga(os.path.join(new, f"{s}_corner_tl.tga"))[2]
            want = read_tga(os.path.join(old, f"{s}_corner_tl.tga"))[2]
            assert got == want, f"{s}_corner_tl changed through the square conversion"

    print(f"selftest: {w}x{h} square-cell master round-trips through 27 slices (alpha "
          "included); ini preserved; seam check fires; 2x/4x scale exactly; cells stay "
          "square at any corner; pre-square masters still infer; a pre-square theme "
          "assembles square losslessly")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("master", nargs="?", help="master image (.png or .tga)")
    ap.add_argument("--out", help="theme folder to write into")
    ap.add_argument("--name", help="theme name; defaults to the folder name")
    ap.add_argument("--template", metavar="PNG", help="write a blank master and exit")
    ap.add_argument("--from-theme", metavar="DIR",
                    help="assemble an existing theme's slices into a master and exit "
                         "(writes DIR/_master.png unless --out names a .png)")
    ap.add_argument("--force-ini", action="store_true",
                    help="overwrite an existing ini (off by default -- yours is kept)")
    ap.add_argument("--corner", type=int, default=None,
                    help=f"corner size in px, also each edge's thickness. Inferred from "
                         f"the master when slicing; --template defaults to {CORNER}")
    ap.add_argument("--stretch", type=int, default=None,
                    help="the stretched axis in px. Inferred when slicing; defaults "
                         "to --corner, i.e. square cells")
    ap.add_argument("--gutter", type=int, default=None,
                    help="gutter in px (default corner/8, which is what makes a "
                         "uniformly scaled master land on exact pixels)")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()

    if a.selftest:
        selftest()
        return 0
    if a.from_theme:
        out = (a.out if a.out and a.out.lower().endswith(".png")
               else os.path.join(a.from_theme, "_master.png"))
        w, h, c, st = assemble(a.from_theme, out)
        print(f"wrote {out} ({w}x{h}) -- re-cut with --corner {c} --stretch {st}")
        return 0
    if a.template:
        w, h = make_template(a.template, a.corner or CORNER, a.stretch, a.gutter)
        print(f"wrote {a.template} ({w}x{h}) -- paint inside the cells, "
              f"the gutter is ignored")
        return 0
    if not a.master or not a.out:
        ap.error("need a master and --out (or --template / --selftest)")

    name = a.name or os.path.basename(os.path.normpath(a.out))
    written, warns = slice_master(a.master, a.out, name, a.force_ini, a.corner,
                                  a.stretch, a.gutter)
    print(f"{a.master} -> {a.out}")
    for f in written:
        print(f"  {f}")
    for w in warns:
        print(f"  WARNING  {w}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
