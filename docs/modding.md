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

A pack's payload is its own `.tga` art, or `.wav` audio for a voice, alongside its `<type>.ini`. Every type's ini opens with the same `[pack]` section, and it takes an **optional `name`** giving the pack a human title in the picker. Leave it out and the folder name is used. It is a label only: a pack is stored and selected by its **folder** name, so retitling one never reassigns anybody's choice. User files override bundled assets of the same name.

**What needs a restart and what does not.** ADDING or REMOVING a `.tga` needs one: sprites are handed to the game once at startup and everything holds them by number afterwards. Everything else the **Reload Config** hotkey picks up - a changed `.ini` (theme, gamepad, pit board, gauges), and a voice pack's `.wav` outright, since audio is opened by path as it plays. Redrawn `.tga` art is the case in between: the companion window re-reads it on the hotkey, while the game keeps the old art until you relaunch.

## Panel themes

A theme draws a frame, a header band and a body card around every HUD and the settings menu. Pick one with **Panel Theme** (Settings > Appearance), or run with none.

A light theme is worth a note: its text is near-black, so a HUD you run with its background switched off will draw dark text straight onto the track. Turn those backgrounds on, or keep a dark theme for on-track HUDs.

Writing your own: a theme is a folder of 27 `.tga` slices plus a `theme.ini` of its colors, fonts and box terms. `tools/themeslice` cuts the slices out of one master image (it never draws - the art is yours), and `assets/themes/` in the repo holds the masters for the shipped themes plus a **debug** master whose every slice is a different flat color, which is the fastest way to see where each of the 27 pieces actually lands.

**Recoloring one is a single file.** `base = <theme>` layers your ini over a theme that already exists, so a folder containing nothing but a `theme.ini` is a complete theme:

```ini
[pack]
name = Carbon Teal
base = carbon-dark

[colors]
primary = #00e0c0
accent  = #ff8800
```

Every slice, font and box term comes from the base; only what you state wins. Bring a `.tga` too and it replaces that one file, leaving the other twenty-six alone - so you can redraw a corner without touching the rest. A base must itself be a theme with no `base` of its own.

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

- **A reskin** - same controller, new look - is two files. Copy any shipped skin folder (say `xbox-crimson`), rename the folder, replace `background.tga`. The ini is called `gamepad.ini` in every pack, so there is nothing else to rename. Its `base = xbox` line is what makes that enough: whatever your folder and ini state wins, and everything they leave out - the sixteen button sprites, the geometry - is answered from the base pack, the same layering rule spotter voice packs use. Add one of the other `.tga` (names in the base's folder) only if you redraw it, and a geometry key only if your art moves things. A base must be a pack without a `base` of its own.
- **A new controller** needs the full set: copy the `xbox` folder instead, replace all 17 `.tga` and adjust the `[size]` / `[offset]` / `[spacing]` values until the buttons line up - the shipped `xbox\gamepad.ini` documents every key.

Either way the folder goes under `gamepads\` in your Documents plugin folder, and your pack appears in the Texture column in Settings > Widgets. The **Reload Config** hotkey re-reads the `.ini` without restarting, which makes nudging offsets quick. Source design files (PSD) are in [`assets/`](../assets/).

**Pitboard** - The pit board is a **pack** too, for a sharper reason than the gamepad: the board picture was always replaceable, but the offsets positioning each row on it lived in *your own settings file*, so a board you drew could not be given to anyone else. A pack keeps the two together:

```
pitboards\
  classic\   pitboard.ini  background.tga
```

Nine skins of it ship - the same board with a recolored frame, in the same nine brand colors as the gamepad skins - and any of them is the two-file template to copy for your own board: rename the folder, drop in your artwork, done - its `base = classic` line answers every row offset from the classic board, so you only add `[offset]` keys for rows your art puts somewhere else (`classic\pitboard.ini` names them all). They keep classic's white writing surface, so they need no `[text] color` - row text defaults to the marker black that surface is drawn for. A board with a DARK surface does need it; `classic\pitboard.ini` documents the key. Your board's **aspect ratio comes from its own art**, so it is no longer forced to the shipped 16:9. Pick it in Settings > Pitboard; the **Reload Config** hotkey re-reads the `.ini` without a restart.

**Gauges** - The tacho and speedo are one **pack**, for the sharpest reason of the three: the ticks and figures are painted into the dial art while the needle used to be placed from numbers compiled into the plugin, so a face drawn to any other ceiling read wrong at every point but zero - and nothing anywhere said so. A pack keeps the picture and its scale together:

```
gauges\
  classic\   gauge.ini  tacho.tga  speedo.tga
```

Both faces live in one pack because they are drawn as a set. To mix, you do not need two packs: each gauge stores its own choice, so you can run your tacho with the shipped speedo by picking them separately - and a pack with `base = classic` that contains only `tacho.tga` is a two-file set that does the same thing.

The faces are square and drawn as a circle, so unlike a pit board there is no aspect to state - only what your art READS. `[tacho] max` and `[speedo] max` are the numbers worth checking first, along with `min-angle` / `max-angle` for how far your dial sweeps (0 is straight up; the shipped faces run -158 to 142). `speedo.max` is in km/h whatever unit your face is printed in, so a 0-140 mph dial writes `max-mph = 140` and the plugin converts. `needle-color`, `needle-length` and `needle-width` belong to the pack too, because a needle has to suit the face it sits on - your own `[TachoWidget] needleColor` in the settings file still wins if you set one. `classic\gauge.ini` documents every key. Pick a set in the Texture column in Settings > Widgets; the **Reload Config** hotkey re-reads the `.ini` without a restart, which is what makes lining a needle up bearable.

If you had drawn your own `tacho_widget_1.tga` before this, the plugin copies it into `gauges\legacy\` for you the first time it runs and selects it, so nothing is lost. That only works for art in your Documents plugin folder - a file dropped straight into the game's own `plugins\` folder cannot be told apart from the one older versions shipped there, so that one is left alone and the log says what to do with it.

**Helmet** - The helmet overlay uses two textures: `helmet_upper_1.tga` (visor rim/top) and `helmet_lower_1.tga` (chin bar). Author at screen resolution with transparent visor openings and ~10% bleed on all sides (extra opaque border beyond the visible area) so tilt and vibration don't expose hard edges.

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
