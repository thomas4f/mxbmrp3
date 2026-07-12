# mxbmrp3_fontgen — a portable PiBoSo `.fnt` bitmap-font generator

A cross-platform, drop-in replacement for PiBoSo's Windows-only `fontgen.exe`.
It rasterizes a TrueType/OpenType font and writes the exact `.fnt` binary the
PiBoSo games (MX Bikes / GP Bikes / KRP / WRS) load — so the output works both
in-game and in this plugin's companion-window software renderer.

Build it in Visual Studio (the `mxbmrp3_fontgen` project in `mxbmrp3.sln`, x64) or
cross-platform with `build.sh`:

```bash
tools/mxbmrp3_fontgen/build.sh                      # builds tools/mxbmrp3_fontgen/mxbmrp3_fontgen

# Auto-normalise: drop a .ttf/.otf in and get a normalised, drop-in .fnt.
# Sizes the cell to 135px, width-normalises digits to the plugin's column, and
# centres the cap/digit band — the whole "make it match the others" pipeline.
tools/mxbmrp3_fontgen/mxbmrp3_fontgen MyFont.ttf             # -> MyFont.fnt
tools/mxbmrp3_fontgen/mxbmrp3_fontgen MyFont.ttf out.fnt

# Config path (full control / manual tweaks):
tools/mxbmrp3_fontgen/mxbmrp3_fontgen myfont.cfg out.fnt
```

## Why not just use `fontgen.exe`?

| | `fontgen.exe` | `mxbmrp3_fontgen` |
|---|---|---|
| Platform | Windows 32-bit only (needs Wine + `wine32:i386` on Linux) | Linux / macOS / Windows, native |
| Cell size | opaque auto-fit; `scale` is an inverse *area* knob (2.0 → **smaller** cell) | you set `cell_height` in **pixels**, or omit it for auto-fit |
| Vertical placement | baked from the font, no control | `center` / `voffset` to fix off-centre fonts |
| High-range glyphs | CP1252 | correct CP1252 (bytes `0x80–0x9F` mapped to the right Unicode glyphs) |

It reproduces `fontgen`'s packing algorithm faithfully — at a given `cell_height`
the glyph widths, atlas packing, and advance follow the same rules (`test.sh`
asserts a regenerated `RobotoMono-Regular.fnt` is structurally identical to the
shipped font).

## The vertical-centering fix

`fontgen` sizes the glyph cell to the **actual ink bounding box** of the whole
character range (topmost ascender to bottommost descender). For a font with deep
descenders relative to its caps — e.g. **Tiny5** — the cell reserves descender
room that the digits/caps don't use, so on a number plate the digits float
off-centre (~7% of the cell). `fontgen` has no way to correct this.

`mxbmrp3_fontgen` adds these knobs:

- `center = 1` — shift the baseline so the **cap/digit band** is centred in the
  cell. Fixes the plate case automatically (Tiny5 goes from ~7% off to centred).
- `voffset = <px>` — manual nudge, `+` = down. Use when you want a specific shift.
- `mono_advance = <ratio>` — **width-normalise**: size the font so the average
  digit advance / cell height equals `<ratio>`, at a fixed cell. Different fonts
  otherwise render numbers at very different widths (measured digit-advance/cell
  ranged 0.43–0.69 across the shipped fonts), so a 3-digit race number overflows
  the standings number plate in some (EnterSansman) and floats tiny in others
  (FuzzyBubbles). Set `mono_advance = 0.489` (RobotoMono's value, the plugin's
  monospace column assumption) and every font's digits land the same width and
  overall size. Ink taller than the cell (some letters' ascenders/descenders) is
  cropped to the cell — digits, which have neither, are unaffected. The atlas
  auto-grows (up to 2048²) if the larger scale needs it.

## Config format

A superset of `fontgen.cfg` — existing configs work unchanged. Keys:

```ini
[config]                                 # section header optional
name        = Roboto Mono Regular        # font name stored in the .fnt
filename    = RobotoMono-Regular.ttf     # source .ttf/.otf (path relative to cwd)
code_page   = 1252                        # 1252 (CP1252) or anything else = Latin-1
char_start  = 32                          # first byte (0..255)
char_end    = 255                         # last byte
spacing     = 4                           # px gap between glyphs in the atlas
bitmap_x    = 512                         # atlas width
bitmap_y    = 512                         # atlas height
cell_height = 135                         # NEW: cell height in px (omit = auto-fit)
center      = 0                           # NEW: 1 = centre the cap/digit band
voffset     = 0                           # NEW: manual vertical nudge in px (+ = down)
mono_advance = 0                          # NEW: >0 = width-normalise digit advance to this ratio
normalize   = 0                           # NEW: 1 = apply the standard defaults below
```

## Normalisation (automatic)

`normalize = 1` (and the bare-`.ttf` invocation) applies the standard defaults to
any field you didn't set — so different fonts render at a **consistent size,
width, and vertical position** in-game:

| field | normalised default | why |
|---|---|---|
| `cell_height` | 135 | high-resolution cell — same on-screen size across fonts, but crisp when a HUD scales text up (high-DPI, large widgets); atlas auto-grows to 2048² |
| `mono_advance` | 0.489 | RobotoMono's digit-advance/cell = the plugin's monospace column, so numbers fit the plate the same in every font |
| `center` | on | centre the cap/digit band (fixes off-centre bakes like Tiny5) |

Any field you set explicitly wins, so `normalize = 1` with `center = 0` (or a
custom `voffset` / `cell_height`) keeps your manual tweak. The shipped fonts
(except the reference RobotoMono-Regular) are generated this way.

`scale` from the old format is accepted but ignored — use `cell_height` instead
(it is predictable; `fontgen`'s `scale` is not).

## `.fnt` binary format

Documented at the top of `mxbmrp3_fontgen.cpp` and in `tools/mxbmrp3_hud_window/README.md`. In
brief: `FNT\0` magic, font name to offset 264, `int32` cell height at 264, then
256 40-byte glyph records at 268 (indexed by codepoint), then at 10508 a small
bitmap header and a raw-DEFLATE 8-bit grayscale atlas.

## Test

`test.sh` builds the tool, regenerates `RobotoMono-Regular.fnt`, and asserts the
result is structurally identical to the shipped font (cell height, glyph widths,
advance, atlas dimensions) and that the atlas round-trips through inflate.
