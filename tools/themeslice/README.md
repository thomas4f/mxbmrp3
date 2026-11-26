# mxbmrp3_themeslice

Cut a theme's **master image** into its `.tga` files, by the `.cells` file
beside it, plus a bootstrap `theme.ini`.

```
themeslice.py --template master.png             # start a new theme (+ master.cells)
themeslice.py master.png                        # cut it into its `out` folder
themeslice.py master.png --out <theme folder>   # ... or into this one
themeslice.py --from-theme <theme folder>       # the inverse: a folder -> master
themeslice.py --selftest                        # round-trip check (CI gate)
```

Stdlib python only - nothing to install.

## Why this exists

The theme system deliberately has no art tooling: every `.tga` is drawn as
authored and edited directly, and a generator that *produced* art was removed
because it left committed art with an invisible source.

This is not that. It never draws anything - it **cuts** the image you gave it,
losslessly, at positions its `.cells` file states. Every pixel out is a pixel in.

What it buys you is consistency. A theme is dozens of files that have to look
like each other - a card is a quieter frame, a button a smaller card, a deck
picture the same family again - and that many separate files is the worst
possible shape for keeping them that way, whether the hand is yours or an
agent's. Drawn together in one image they stay together by construction, and one
PNG is something you can hand to somebody (or something) for a pass and get
coherent art back.

## One master per theme

A master holds **every** top-level `.tga` of its theme folder. The nine-slice
sets come first, each a 3×3 arrangement in the order the slices appear on screen:

```
corner_tl   edge_top      corner_tr
edge_left   center        edge_right
corner_bl   edge_bottom   corner_br
```

so you are drawing a panel, not filling in a sprite sheet. `frame`, `card` and
`button` stack down the image, then the optional `title` set (`--template
--title`): the title band's own art, for a theme whose bar and section cards are
different pictures. Without it the band draws the `card` set.

Below the sets, the theme's other pictures stack one under another, each whole:
the **slanted ends** (`frame_slant_left/right.tga`, the frame's two ends leaning,
for slanted decks; `--template --ends` adds blank cells for them, sized from the
corner by `END_ROWS` and `END_SCALE` in the script) and the **deck pictures** (`deck_*.tga`, each
stretched over one box).

Every set and picture is **captioned** in the gutter above it. **Gutters are not
art**: no picture ever reads them, so captions and guides never reach a `.tga`.
Paint inside the cells; whatever is left in the gutter is ignored.

## The .cells file

One line per picture, `<name> x y w h`, plus `out = <folder>` (relative to the
`.cells` file) naming where the pictures go:

```
out = ../../mxbmrp3_data/themes/retro-dark
frame_corner_tl 8 8 64 64
...
deck_stamp 8 903 90 28
```

`--template` and `--from-theme` write it with the sheet; cutting reads it back,
so the cuts cannot drift from the sheet. Keep the two files together.

- **To change a picture**, edit it in the master and re-cut.
- **To add one**, grow the sheet, paint it in a free spot, add its line.

Cutting used to *infer* the layout from the master's size. That only worked
while a master held nothing but the sets; deck pictures come in any size, so the
file now says where everything is.

## Geometry of a new sheet

`--corner` (default 64) is the corner art's size and also each edge's
*thickness*. The edges' length and the centre are the **same square**.

That is an authoring choice, not a rendering one: those axes are stretched to
the panel at draw time, so their resolution costs nothing on screen. It was 16px
once, on the reasoning that free-but-small is tidy and detail belongs in the
corners. Unequal cells cost more than the pixels they saved - a 3×3 of mismatched
rectangles stops reading as a panel, a stroke drawn across a cell boundary lands
at two different scales, a corner motif cannot be carried a little way along its
edges, and every art tool's grid and symmetry assume equal cells.

**Scaling.** The gutter is `corner/8` rather than a fixed 8, so `--corner 128`
lands on exactly twice the pixels. Below a 48 px corner the gutter cannot hold a
5 px caption, so the sheet goes uncaptioned rather than captioning inside a cell.

**Transparency** survives everywhere: PNG in, 32-bit TGA out, both channels
hand-rolled and both covered by the round-trip test on varying alpha. A rounded
or glass theme is mostly alpha, so a test that only carried 255 would prove
nothing about the case that matters.

A `--template` gutter is *semi*-transparent so the cells are visible to work in
without anything reading as art; an assembled master's gutter is fully clear.

## What cutting writes

Every picture the `.cells` lists, and - for a sheet with a frame - a bootstrap
`theme.ini`.

- **The `.tga` are overwritten** every run. They are derived; that is the point.
- **An existing `theme.ini` is never touched.** The bootstrap is written only
  when the file is absent, so a hand-tuned ini survives every re-cut.
  `--force-ini` overrides.

The ini's `tint` is a **guess** (`0`, art carries its own colours). The slicer
cannot tell baked art from white+alpha by looking; if you painted in white and
shaped with alpha, set it to `1`.

## The seam check

`nine_slice.h` documents one art rule that nothing else in the toolchain can
check:

> the edge sprite's inner value must equal the center sprite's value, or the
> independently-stretched slices show a hard seam inset from the panel edge

Only something holding all nine slices at once can see that, which is this. It
warns rather than fails - a deliberate hard edge is legitimate. A second warning
catches button art that is not white, which would multiply its state colours
away.

## Assembling an existing theme

`--from-theme` reads a theme folder's `.tga` and writes the master and `.cells`
they would have been cut from, so a theme authored file-by-file can join the
master workflow.

It is **lossless or refused**, never resampled. A slice whose size does not match
its cell is accepted only when it is uniform along the axis that differs, which
is the normal case: the stretched axes carry no detail by definition. Anything
else errors, because quietly resampling somebody's art is how a conversion tool
loses their work. Pictures outside the sets go in exactly as they are.

**It also squares up a theme it converts.** A pre-square theme's stretched axes
are flat, so widening them into square cells is replication rather than
resampling - the same uniformity rule, in the other direction. A theme that does
carry detail along a stretched axis keeps its own size instead of being refused,
so the master is not square and that is the honest answer.

## Where the shipped masters live

`assets/themes/<name>.png` + `.cells`, next to the `.psd`/`.pdn` sources for the
gamepad and helmet textures - that folder is this repo's home for editable source
art and is not shipped by the installer. The self-test cuts each one and fails on
any shipped picture that differs from its cell, is missing, or has no cell.

`debug` is the exception: a master whose every slice is a different flat colour,
for seeing where each piece lands. Its `.cells` has no `out`, so it is never
built; cut it with `--out`.

## Workflow

```
edit master.png  →  themeslice.py master.png  →  Reload Config  →  look
```

Point `out` (or `--out`) at `Documents\PiBoSo\<game>\mxbmrp3\themes\<name>\`
and the plugin picks it up from the same folder it already syncs from.
