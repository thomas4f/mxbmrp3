# Icon sources (SVG)

Editable **source** SVGs for the in-game icon set. These are built into the
shipped `mxbmrp3_data/icons/*.tga` files by [`tools/icon_gen.py`](../../tools/icon_gen.py).

Sources only — the installer ships `mxbmrp3_data\icons\*.tga`, never these SVGs.
Only the SVG's alpha **shape** is used; the generator repaints it a flat tint
(white by default, so the in-game ColorConfig tint shows through), so the fill
colour inside the SVG doesn't matter.

## Two icon roles (the rule is the filename)

| Role | Names | Look | Build flags |
|------|-------|------|-------------|
| **Identity** | `hud-*` (e.g. `hud-standings`, `hud-timing`, `hud-widgets`, `hud-updates`) | Flat, no outline — they sit beside HUD titles / settings tabs and get an in-game **drop shadow** | *(none — defaults)* |
| **Status / marker** | everything else (`flag-checkered`, `clock`, `circle`, `crown`, `ban`, `gamepad`, …) | **Outlined** (2px) for contrast over the track; no shadow. Also selectable as tracked-rider markers | `--outline 2` |

Both roles use the same **smooth anti-aliased edges** (`--crisp 0`, the default).
The *only* difference is the outline: `hud-*` get none, everything else gets a
2px outline. The split is exactly the filename prefix (verified: 29 identity, 82
status, zero exceptions). The namespacing exists because a few glyphs
(`flag-checkered`, `clock`) are used in *both* roles; the identity copy is `hud-*`
so it never collides with the outlined status set.

> ℹ️ **`--crisp 0` (smooth AA) is the default and what the whole set uses.** A
> higher `--crisp` adds an unsharp pass + a hard alpha threshold (binary edges);
> the icons were historically built that way for the outlined set but are now
> uniformly smooth so identity and status icons match.

## Renderer: cairosvg, pinned

`icon_gen.py` rasterises with **cairosvg only**, pinned to the exact version in
[`tools/requirements.txt`](../../tools/requirements.txt) (`cairosvg==2.9.0`). The
output TGA carries no timestamp or metadata (fixed 18-byte header + raw BGRA
pixels), so its content hash is fully deterministic — but **only for a fixed
renderer + version** (edge anti-aliasing differs between releases). cairosvg is a
pip package, so it pins cleanly and reproduces byte-for-byte on any machine.

`rsvg-convert`/system librsvg is **deliberately not used**: its output can't be
pinned via pip and drifts between OS releases — that is exactly why the original
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
`--size`, `--supersample`, `--crisp`, and colour options. (Note: a few SVG
sources here have no shipped `.tga` and are intentionally not generated — only
regenerate names that already exist under `mxbmrp3_data/icons/`.)

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
`hud-*` glyph's SVG is reflected in the README automatically — there is no raster
step to run and no `assets/readme-icons/` directory.
