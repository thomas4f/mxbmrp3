# Icon sources (SVG)

Editable **source** SVGs for the in-game icon set. These are built into the
shipped `mxbmrp3_data/icons/*.tga` files by [`tools/icon_gen.py`](../../tools/icon_gen.py).

Sources only - the installer ships `mxbmrp3_data\icons\*.tga`, never these SVGs.
Only the SVG's alpha **shape** is used; the generator repaints it a flat tint
(white by default, so the in-game ColorConfig tint shows through), so the fill
colour inside the SVG doesn't matter.

## Two icon roles (the rule is the filename)

| Role | Names | Look | Build flags |
|------|-------|------|-------------|
| **Identity** | `hud-*` (e.g. `hud-standings`, `hud-timing`, `hud-widgets`, `hud-updates`) | Flat, no outline - they sit beside HUD titles / settings tabs and get an in-game **drop shadow** | *(none - defaults)* |
| **Status / marker** | everything else (`flag-checkered`, `clock`, `circle`, `crown`, `ban`, `gamepad`, …) | **Outlined** (2px) for contrast over the track; no shadow. Also selectable as tracked-rider markers | `--outline 2` |

Both roles use the same **smooth anti-aliased edges** (`--crisp 0`, the default).
The *only* difference is the outline: `hud-*` get none, everything else gets a
2px outline. The split is exactly the filename prefix, with no exceptions. The
namespacing exists because a few glyphs
(`flag-checkered`, `clock`) are used in *both* roles; the identity copy is `hud-*`
so it never collides with the outlined status set.

> ℹ️ **`--crisp 0` (smooth AA) is the default and what the whole set uses.** A
> higher `--crisp` adds an unsharp pass + a hard alpha threshold (binary edges);
> the icons were historically built that way for the outlined set but are now
> uniformly smooth so identity and status icons match.

## Taking an icon from Font Awesome

This set is uniformly on FA7's **640 grid** - 512 units of artwork inside a
64-unit margin. The FA GitHub repo (`FortAwesome/Font-Awesome`) serves the
**512 grid** on every branch and tag, artwork bleeding to the edges; the 640
version comes from the fontawesome.com icon page's *Download SVG*.

Drop in a 512-grid file unchanged and it renders 1.25x larger than every icon
beside it, which looks like a mistake and is easy to miss on a single icon. Take
the 640 one from the website; it also arrives with the attribution comment the
rest of the set carries, so the file lands here unedited.

If the website is genuinely out of reach, re-frame rather than rescale - set the
viewBox to `-64 -64 640 640` and leave the path alone. That is a stand-in, not
an equal: it was measured once against the real 640 file for `hud-confirm` and
came out within 2/255 of alpha on 231 of 4096 pixels (anti-aliasing only), but
that is one icon's worth of evidence, and FA is free to actually redraw a glyph
on the larger grid rather than scale it.

## Theme overrides

A panel theme may restyle any of these icons by shipping its own `.tga` under
`mxbmrp3_data/themes/<theme>/icons/`, matched **by filename**:

```
themes/carbon-dark/icons/hud-map.tga  overrides hud-map while Carbon Dark is selected
```

Three rules, and the first is the one everything else follows from:

1. **Restyle, never extend.** A file that matches no icon in this set is ignored
   (with a warning in the log). A rider's marker persists by *name* - in the
   settings INI and in `mxbmrp3_tracked_riders.json` - so a theme that could add or
   remove names would orphan saved choices the moment it was switched off. New
   glyphs belong in the user icons folder under Documents, which extends the base
   set for every theme at once.
2. **Fallback is per file.** A theme overriding three icons inherits the rest, so a
   partial set is the normal case rather than a broken one.
3. **Same two roles, same flags.** Build a theme's overrides exactly as the base
   set is built: `hud-*` flat (no `--outline`), everything else `--outline 2`. The
   identity copy is drawn in two places - beside a HUD title, where it gets an
   in-game drop shadow, and in the settings tab list, where it does not - so an
   outline baked into one reads as a single heavy icon among twenty flat ones.

The resolution itself is `core/icon_resolve.h` (pinned by
`tests/unit/test_icon_resolve.cpp`); that a switch of theme switches the icons with
it is pinned by `tests/integration/tests/theme_icons_test.cpp`.

## Renderer: cairosvg, pinned

`icon_gen.py` rasterises with **cairosvg only**, pinned to the exact version in
[`tools/requirements.txt`](../../tools/requirements.txt) (`cairosvg==2.9.0`). The
output TGA carries no timestamp or metadata (fixed 18-byte header + raw BGRA
pixels), so its content hash is fully deterministic - but **only for a fixed
renderer + version** (edge anti-aliasing differs between releases). cairosvg is a
pip package, so it pins cleanly and reproduces byte-for-byte on any machine.

`rsvg-convert`/system librsvg is **deliberately not used**: its output can't be
pinned via pip and drifts between OS releases - that is exactly why the original
icons (built with a now-unknown librsvg) couldn't be reproduced and the whole set
was re-rendered under cairosvg. `icon_gen.py` does not call it, so having it
installed has no effect.

## Regenerate

```bash
python3 -m pip install -r tools/requirements.txt   # Pillow + cairosvg==2.9.0

# Flat identity icons (hud-*) -> shipped folder
python3 tools/icon_gen.py 'assets/icons/hud-*.svg' -o mxbmrp3_data/icons

# Outlined status/marker icons (everything else)
python3 tools/icon_gen.py assets/icons/<name>.svg --outline 2 -o mxbmrp3_data/icons
```

Output is byte-compatible with the existing icons (uncompressed 32-bit BGRA,
bottom-left origin, 64×64 = 16402 bytes). See `tools/icon_gen.py --help` for
`--size`, `--supersample`, `--crisp`, and colour options. Every SVG here has a
shipped `.tga` of the same name under `mxbmrp3_data/icons/` - the sets are 1:1,
so a source added here without being generated is an oversight, not a spare.

## Verify a regeneration

The output is deterministic, so a regenerated icon must hash-match the shipped
one. To confirm your environment reproduces the set:

```bash
# Regenerate a known icon and diff against the shipped TGA (exit 0 = identical)
python3 tools/icon_gen.py 'assets/icons/hud-standings.svg' -o /tmp/iconcheck
cmp /tmp/iconcheck/hud-standings.tga mxbmrp3_data/icons/hud-standings.tga && echo "byte-identical"
```

If an icon does **not** reproduce byte-for-byte, your `cairosvg` is not `==2.9.0`
(`pip install -r tools/requirements.txt`).

## README table icons

The HUD table in the repo `README.md` embeds these **SVG sources directly**
(`<img src="assets/icons/hud-<name>.svg" width="20" height="20">`), so editing a
`hud-*` glyph's SVG is reflected in the README automatically - there is no raster
step to run and no second directory of raster copies to keep in step.
