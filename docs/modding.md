# Modding

Textures, packs, fonts, icons and the web overlay files. Everything here goes in your Documents plugin folder, which the [README](../README.md#modding) explains. Spotter voices are a pack type too, and have their own guide: [Spotter voice packs](spotter.md).

## Where custom assets go

Add custom fonts, textures, and icons by placing them in the appropriate subfolder:

```
mxbmrp3/
├── fonts/       ← Custom .fnt files
├── textures/    ← Custom .tga textures
├── icons/       ← Custom .tga icons
├── themes/      ← Panel themes, one folder per theme (<name>/ + theme.ini)
├── gamepads/    ← Gamepad packs, one folder per pad (<name>/ + gamepad.ini)
├── pitboards/   ← Pit board packs, one folder per board (<name>/ + pitboard.ini)
├── spotters/    ← Spotter voice packs, one folder per voice (<name>/ + spotter.ini)
├── gauges/      ← Gauges packs, one folder per dial set (<name>/ + gauge.ini)
└── web/         ← Overlay files (index.html, style.css, custom.css)
    ├── js/      ← Overlay scripts (overlay-config.js and the rest)
    ├── fonts/   ← Overlay fonts
    ├── icons/   ← Overlay icons
    └── logos/   ← Sponsor/logo PNGs for the web overlay slideshow
```

A pack's payload is its own `.tga` art, or `.wav` audio for a voice, alongside its `<type>.ini`. The `[pack]` section takes an **optional `name`** for a human title in the picker; leave it out and the folder name is used, title-cased. The shipped packs leave it out wherever that works, and state one only where it does not (`ds4` title-cases to "Ds4", not "DualShock 4"). Section headers are matched case-insensitively. User files override bundled assets of the same name.

**`base = <pack>` is one rule for every pack type.** Your ini layers over the named pack: whatever your folder and ini state wins, and everything they leave out - art, geometry, colors, offsets - is answered from the base. So a reskin is a folder holding a `<type>.ini` with a `base` line and the one or two files you actually changed. A base must itself be a pack with no `base` of its own.

**What needs a restart and what does not.** ADDING or REMOVING a `.tga` needs one: sprites are handed to the game once at startup and everything holds them by number afterwards. Everything else the **Reload Config** hotkey picks up - a changed `.ini` (theme, gamepad, pit board, gauges), and a voice pack's `.wav` outright, since audio is opened by path as it plays. Redrawn `.tga` art is the case in between: the companion window re-reads it on the hotkey, while the game keeps the old art until you relaunch.

## Panel themes

A theme draws a frame, a header band and a body card around every HUD and the settings menu. Pick one with **Panel Theme** (Settings > Appearance), or run with none.

A light theme is worth a note: its text is near-black, so a HUD you run with its background switched off will draw dark text straight onto the track. Turn those backgrounds on, or keep a dark theme for on-track HUDs.

Writing your own: a theme is a folder of 27 `.tga` slices plus a `theme.ini` of its colors, fonts and box terms. `tools/themeslice` cuts the slices out of one master image (it never draws - the art is yours), and `assets/themes/` in the repo holds the masters for the shipped themes plus a **debug** master whose every slice is a different flat color, which is the fastest way to see where each of the 27 pieces actually lands.

**Recoloring one is a single file.** With a `base` line, a folder containing nothing but a `theme.ini` is a complete theme:

```ini
[pack]
name = Carbon Teal
base = carbon-dark

[colors]
primary = #00e0c0
accent  = #ff8800
```

Bring a `.tga` too and it replaces that one slice, leaving the other twenty-six alone - so you can redraw a corner without touching the rest.

## Custom Textures

Textures use the naming convention `{element_name}_{number}.tga` (e.g., `radar_hud_2.tga`). Drop them into the `textures\` subfolder and they are auto-discovered at startup.

**The Texture control appears where there is art to pick, not on every HUD.** That row is a Texture cycle when the folder holds files for that element's name, and the per-HUD **Theme** override when it does not - so out of the box only Pointer and Radar show it, plus the three packs below. This is the useful half of the rule for a modder: add `standings_hud_1.tga` and Standings grows a Texture control it never had. (The helmet overlay is its own case - two variant controls on its own tab rather than the shared row.)

The element name is the HUD's own, spelled as in the shipped `textures\` folder (`radar_hud`, `pointer_widget`, ...). The plugin's log names every texture base it discovered, which is how to confirm a file was picked up rather than misnamed.

Three elements are **packs** instead, because their picture travels with the numbers that describe it - the gamepad, the pit board and the gauges, below.

**Gamepad** - The Gamepad widget uses **packs** rather than loose textures, because a pad is artwork *plus* the geometry that places buttons on it. A pack is a folder under `gamepads\` holding the pad's 17 `.tga` and a `gamepad.ini` of its measurements:

```
gamepads\
  xbox\   gamepad.ini  background.tga  stick.tga  dpad_button.tga  face_button_1.tga  ...
  ds4\    gamepad.ini  ...
```

Xbox and DualShock 4 ship built in, each with nine **brand-color skins** (Orange, Crimson, Navy, Royal, Lime, Cyan, Yellow, Graphite, Silver) taken from the plugin's own BrandColors table, so a pad and a pit board of the same name match. To add your own, start from what you are actually changing:

- **A reskin** - same controller, new look - is two files. Copy any shipped skin folder (say `xbox-crimson`), rename the folder, replace `background.tga`. Its `base = xbox` line answers the sixteen button sprites and the geometry. Add one of the other `.tga` (names in the base's folder) only if you redraw it, and a geometry key only if your art moves things.
- **A new controller** needs the full set: copy the `xbox` folder instead, replace all 17 `.tga` and adjust the `[size]` / `[offset]` / `[spacing]` values until the buttons line up - the shipped `xbox\gamepad.ini` documents every key.

Either way the folder goes under `gamepads\` in your Documents plugin folder, and your pack appears in the Texture column in Settings > Widgets. Source design files (PSD) are in [`assets/`](../assets/).

**Pitboard** - A pit board pack is the board picture plus the offsets that place each row on it, so a board you drew can be given to anyone:

```
pitboards\
  classic\   pitboard.ini  background.tga
```

Nine skins of it ship - the same board with a recolored frame, in the same nine brand colors as the gamepad skins - and any of them is the two-file template for your own board: rename the folder, drop in your artwork, and its `base = classic` line answers every row offset, so you add `[offset]` keys only for rows your art puts somewhere else (`classic\pitboard.ini` names them all). Row text defaults to marker black for a white writing surface; a board with a DARK surface needs a `[text] color`, which the same file documents. The board's **aspect ratio comes from its own art**. Pick it in Settings > Pitboard.

**Gauges** - The tacho and speedo are one **pack**: the ticks and figures are painted into the dial art, so the numbers that place the needle travel with it:

```
gauges\
  classic\   gauge.ini  tacho.tga  speedo.tga
```

Both faces live in one pack because they are drawn as a set. To mix, you do not need two packs: each gauge stores its own choice, so you can run your tacho with the shipped speedo by picking them separately - and a pack with `base = classic` that contains only `tacho.tga` is a two-file set that does the same thing.

The faces are square and drawn as a circle, so there is no aspect to state - only what your art READS. `[tacho] max` and `[speedo] max` are the numbers to check first, with `min-angle` / `max-angle` for how far the dial sweeps (0 is straight up; the shipped faces run -158 to 142). `speedo.max` is in km/h whatever unit the face is printed in, so a 0-140 mph dial writes `max-mph = 140` and the plugin converts. `needle-color`, `needle-length` and `needle-width` belong to the pack too, since a needle has to suit its face; your own `[TachoWidget] needleColor` in the settings file still wins if you set one. `classic\gauge.ini` documents every key. Pick a set in the Texture column in Settings > Widgets.

If you had drawn your own `tacho_widget_1.tga` before this, the plugin copies it into `gauges\legacy\` for you the first time it runs and selects it, so nothing is lost. That only works for art in your Documents plugin folder - a file dropped straight into the game's own `plugins\` folder cannot be told apart from the one older versions shipped there, so that one is left alone and the log says what to do with it.

**Helmet** - The helmet overlay uses two textures: `helmet_upper_1.tga` (visor rim/top) and `helmet_lower_1.tga` (chin bar). Author at screen resolution with transparent visor openings and ~10% bleed on all sides (extra opaque border beyond the visible area) so tilt and vibration don't expose hard edges.

**Twitch chat badges** - The stream chat shows an icon for each chatter's role. For Twitch lines, to use Twitch's own badge images instead, add them yourself - the plugin can't ship Twitch's badge art - as `.tga` files in `textures\`, named `twitch_` plus Twitch's badge name and `_1`: `twitch_broadcaster_1.tga`, `twitch_lead_moderator_1.tga`, `twitch_moderator_1.tga`, `twitch_vip_1.tga`, `twitch_staff_1.tga`, `twitch_partner_1.tga`, `twitch_artist_1.tga`, `twitch_premium_1.tga` (Prime), `twitch_turbo_1.tga`, `twitch_founder_1.tga`, `twitch_subscriber_1.tga`. Each file replaces that role's icon and is drawn as-is, not tinted; roles without a file keep their icon. Square images work best - Twitch serves its badges at 72x72.

## Custom Fonts

Fonts (`.fnt` files) are auto-discovered and assignable to categories (Title, Normal, Strong, Digits, Marker, Small) in Settings > Appearance.

To make one, either use PiBoSo's own `fontgen` (Windows, prebuilt - see [this forum post](https://forum.piboso.com/index.php?topic=1458.msg20183#msg20183)), or build ours from [`tools/fontgen`](../tools/fontgen): it is cross-platform, reads the same config keys, and ships as source rather than a binary, so `./build.sh` first. Its README has the keys and two worked examples.

## Custom Icons

Icons (`.tga` files) placed in the `icons\` subfolder are discovered alphabetically and available for tracked rider customization in Settings > Riders.

They are also how the plugin draws its own glyphs - panel title icons, the standings status marks, the map and radar markers - each looked up by NAME. So a file that replaces a shipped one (`flag.tga`, `circle-exclamation.tga`, ...) reskins it everywhere it is used, with no setting to change.

## Web Overlay Files

The overlay files are plain HTML, CSS, and JS. To customize them, place modified files in `Documents\PiBoSo\[Game]\mxbmrp3\web\`, at the SAME relative path the bundled file has - the scripts sit in a `js\` subfolder, so an override of one goes in `web\js\`.

- `style.css` - The `:root` block holds the theme tokens: colors, fonts, sizes, spacing, and animation timings. Colors and fonts sync from the game (to override those in `custom.css`, add `!important`); sizes, spacing, and animations can be set directly.
- `custom.css` - Optional file you create yourself for style overrides. Copy the bundled `custom-sample.css` to `custom.css` to start - it's a commented reference with ready-made recipes (light theme, compact, no-motion, fonts). Loaded after `style.css`, so its rules take precedence. Use it for small theme tweaks instead of forking the full stylesheet. Tip: append `?demo` to the overlay URL to preview your theme against a synthetic race without launching the game.
- `index.html` - Overlay structure
- `overlay-config.js` (in the `js\` subfolder) - The `CONFIG` block at the top defines defaults for all settings. These are overridden by the settings panel (stored in localStorage).
