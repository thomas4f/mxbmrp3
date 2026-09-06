# Texture sources (SVG)

Editable **source** SVGs for shaded textures in `mxbmrp3_data/textures/`, built by
[`tools/texture_gen.py`](../../tools/texture_gen.py). Unlike the icons, these keep
their own colour and alpha (gradients, sheens, bevels) - so they are drawn in
**greyscale**, and the plugin multiplies the tint in when it draws them: white is
the tint itself, grey a darker shade of it.

| Source | Output | Used by |
|---|---|---|
| `badge_1.svg` .. `badge_4.svg` | `badge_1.tga` .. `badge_4.tga` | Settings > Achievements: the tile behind a row's marker, one per tier (Bronze to Platinum), each a step fancier - outline and sheen, then a shine, a bevel, a sparkle. A locked row takes the first. |

Regenerate all: `python3 tools/texture_gen.py assets/textures/badge_*.svg -o mxbmrp3_data/textures`

The other files in `mxbmrp3_data/textures/` (helmet halves, the radar disc, the
gear circle, the pointer) are hand-painted; they have no SVG source here.
