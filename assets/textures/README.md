# Texture sources (SVG)

Editable **source** SVGs for shaded textures in `mxbmrp3_data/textures/`, built by
[`tools/texture_gen.py`](../../tools/texture_gen.py). Unlike the icons, these keep
their own colour and alpha (gradients, sheens, bevels) - so they are drawn in
**greyscale**, and the plugin multiplies the tint in when it draws them: white is
the tint itself, grey a darker shade of it.

**Brand logos are the exception**: they are full colour and drawn untinted,
because Twitch and YouTube both forbid recolouring their marks - which is also
why they are textures and not icons (every icon is a tintable glyph, and any
icon can be picked as a rider marker). Their geometry and colours are copied
from the official files (brand.twitch.com; YouTube's brand resources), only
re-framed on a square canvas; don't redraw them from another icon set.

| Source | Output | Used by |
|---|---|---|
| `badge_1.svg` .. `badge_4.svg` | `badge_1.tga` .. `badge_4.tga` | Settings > Achievements: the tile behind a row's marker, one per tier (Bronze to Platinum), each a step fancier - outline and sheen, then a shine, a bevel, a sparkle. A locked row takes the first. |
| `twitch_1.svg`, `youtube_1.svg` | `twitch_1.tga`, `youtube_1.tga` (64 px) | Stream Chat: the platform mark on each line. Official logos, full colour. |

Regenerate:

```bash
python3 tools/texture_gen.py assets/textures/badge_*.svg -o mxbmrp3_data/textures
python3 tools/texture_gen.py assets/textures/twitch_1.svg assets/textures/youtube_1.svg --size 64 -o mxbmrp3_data/textures
```

The other files in `mxbmrp3_data/textures/` (helmet halves, the radar disc, the
gear circle, the pointer) are hand-painted; they have no SVG source here.
