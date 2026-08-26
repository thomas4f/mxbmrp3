# mxbmrp3_themeslice

Cut a **master image** into a theme's 27 slice files plus a bootstrap ini.

```
themeslice.py --template master.png                   # start a new theme
themeslice.py master.png --out <theme folder>         # cut it
themeslice.py --from-theme <theme folder>             # the inverse: slices -> master
themeslice.py --selftest                              # round-trip check (CI gate)
```

Stdlib python only - nothing to install.

## Why this exists

The theme system deliberately has no art tooling: every `.tga` is drawn as
authored and edited directly, and a generator that *produced* art was removed
because it left committed art with an invisible source.

This is not that. It never draws anything - it **cuts** the image you gave it,
losslessly, at positions the template it also emits already fixed. Every pixel
out is a pixel in.

What it buys you is consistency. A theme is 27 files that have to look like each
other - a card is a quieter frame, a button a smaller card - and 27 separate
files is the worst possible shape for keeping them that way, whether the hand is
yours or an agent's. Drawn together in one image they stay together by
construction, and one PNG is something you can hand to somebody (or something)
for a pass and get coherent art back.

## The master looks like the panel

Each set is a 3×3 arrangement, in the order the slices appear on screen:

```
corner_tl   edge_top      corner_tr
edge_left   center        edge_right
corner_bl   edge_bottom   corner_br
```

so you are drawing a panel, not filling in a sprite sheet. The three sets -
`frame`, `card`, `button` - stack down the image in the ini's section order, each
captioned in the gutter above it.

**Gutters are not art.** Cells are separated by a gutter that no slice ever
reads, so the template can outline and label without any of that reaching a
`.tga`. Paint inside the cells; whatever is left in the gutter is ignored.

## Geometry

`--corner` (default 64) is the corner art's size and also each edge's
*thickness*. `--stretch` is the axis that gets scaled to the panel - edge lengths
and the centre - and **defaults to `--corner`, so every cell is the same
square**.

That is an authoring choice, not a rendering one: the stretched axes are scaled
to the panel at draw time, so their resolution costs nothing on screen. It was
16px once, on the reasoning that free-but-small is tidy and detail belongs in the
corners. Unequal cells cost more than the pixels they saved - a 3×3 of mismatched
rectangles stops reading as a panel, a stroke drawn across a cell boundary lands
at two different scales, a corner motif cannot be carried a little way along its
edges, and every art tool's grid and symmetry assume equal cells.

On disk a flat theme grows ~1.8× (uncompressed TGA); in the installer it is
nothing, because replicated flat fills compress away - measured at +288 bytes
across all three shipped themes.

**You do not have to pass either when slicing.** Both are recovered from the
master's own dimensions: with the gutter derived from the corner the layout is
two equations in two unknowns (`W = 2.5c + s`, `H = 7.25c + 3s`), so
`c = 4(3W - H)` and `s = W - 2.5c`. The result is verified by rebuilding the
layout, so a size that merely satisfies the algebra is still rejected.

They were required once, and getting them wrong was this tool's most likely
error - a master assembled from an existing theme has a different stretch from
one started from the default template, and nothing on the image says so. That was
only true while the gutter was a fixed 8px, which left one equation for two
unknowns; making it derived (for scaling, above) removed the need for the flags
as a side effect. They remain as overrides for a master built with some other
gutter.

**Scaling.** The gutter defaults to `corner/8` rather than a fixed 8, which is
what lets a master scale uniformly: pass `--corner 128` and the layout lands on
exactly twice the pixels, cells still square. With a fixed
gutter it does not - a 200% master is 352 wide while the tool would compute 320 -
so anyone making a hi-dpi version of their art would hit a size mismatch
immediately. `--gutter` overrides for a master that was not built this way.

**Transparency** survives everywhere: PNG in, 32-bit TGA out, both channels
hand-rolled and both covered by the round-trip test on varying alpha. A rounded
or glass theme is mostly alpha, so a test that only carried 255 would prove
nothing about the case that matters.

A `--template` gutter is *semi*-transparent so the cells are visible to work in
without anything reading as art; an assembled master's gutter is fully clear.

## What it writes

27 `.tga`, a bootstrap ini, and a copy of the master as `_master.png`.

- **The `.tga` are overwritten** every run. They are derived; that is the point.
- **An existing ini is never touched.** The bootstrap is written only when the
  file is absent, so a hand-tuned ini survives every re-slice. `--force-ini`
  overrides.
- **`_master.png` is the source**, kept beside what it cut into. Without it the
  slices are art whose origin nobody can see, which is exactly the problem that
  got the old generator deleted. (The repo's own masters live in `assets/themes/`
  instead - see below.)

The ini's `tint` is a **guess** (`1`). The slicer cannot tell baked art from
white+alpha by looking; if your art carries its own colours, set it to `0` or the
HUD background colour multiplies them away.

## The seam check

`nine_slice.h` documents one art rule that nothing else in the toolchain can
check:

> the edge sprite's inner value must equal the center sprite's value, or the
> independently-stretched slices show a hard seam inset from the panel edge

Only something holding all nine slices at once can see that, which is this. It
warns rather than fails - a deliberate hard edge is legitimate.

## Assembling an existing theme

`--from-theme` reads a theme's 27 slices and writes the master they would have
been cut from, so a theme authored slice-by-slice can join the master workflow.
Geometry is read from the art itself - a corner file states the corner size, an
edge states the stretch - and printed, so you can re-cut with the same values.

It is **lossless or refused**, never resampled. A slice whose size does not match
its cell is accepted only when it is uniform along the axis that differs, which
is the normal case: the stretched axes carry no detail by definition. Anything
else errors, because quietly resampling somebody's art is how a conversion tool
loses their work.

**It also squares up a theme it converts.** A pre-square theme's stretched axes
are flat, so widening them into square cells is replication rather than
resampling - the same uniformity rule, in the other direction. A theme that does
carry detail along a stretched axis keeps its own size instead of being refused,
so the master is not square and that is the honest answer.

All three shipped themes were converted this way. Every corner came back
byte-for-byte (that is where the detail lives); the edges and centres are the
same colours at the new size, which renders identically because those axes are
stretched to the panel anyway.

## Where the shipped masters live

`assets/themes/<name>.png`, next to the `.psd`/`.pdn` sources for the gamepad and
helmet textures - that folder is this repo's home for editable source art and is
not shipped by the installer.

A **user's** theme is different: there is no `assets/` in an install, so slicing
copies the master in beside the slices as `_master.png`, where it cannot get
separated from what it produced.

## Workflow

```
edit master.png  →  themeslice.py  →  Reload Config  →  look
```

Point `--out` at `Documents\PiBoSo\<game>\mxbmrp3\themes\<name>\` and the plugin
picks it up from the same folder it already syncs from.
