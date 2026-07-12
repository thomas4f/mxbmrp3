# HUD window — in-process companion window

The plugin can render its HUD in a **standalone OS window** outside the game, so
you can drag the HUD to a second monitor. It is not a network mirror: the window
lives inside the plugin and reads its live render primitives directly from memory,
drawing them with a software rasterizer (no GPU, no OpenGL).

The feature is in the plugin:

- `mxbmrp3/core/hud_sw_renderer.{h,cpp}` — a pure-C++ software rasterizer for the
  plugin's `SPluginQuad_t`/`SPluginString_t` (scanline quad fills, TGA sprites/icons,
  and text drawn from the game's own pre-rasterized bitmap fonts `fonts/*.fnt` —
  the exact glyph atlas the game samples, so it's pixel-faithful and allocation-free
  per frame; a stb_truetype `.ttf` path is kept as a fallback). No windowing dependency.
- `mxbmrp3/core/companion_window.{h,cpp}` — a Win32 window on its own thread. The
  game thread publishes each frame's primitives via `submit()` under a mutex; the
  window thread renders the latest snapshot on its own cadence (live even in menus)
  and presents via `StretchDIBits`. Works natively on Windows and under Proton/Wine.

Control it at runtime from the **Appearance** settings tab (Display section): the
**HUD Display** cycler switches between **In-game**, **Companion** (window only — the
in-game HUD is hidden), and **Both**. It's persisted as a `[Display]` setting:

```ini
[Display]
displayTarget=BOTH   ; IN_GAME | COMPANION | BOTH
companionWindowX=200 ; window rect (full window, screen coords); restored on open.
companionWindowY=120 ; W<=0 => open at the default 980x560, system-placed. A saved
companionWindowW=980 ; rect that no longer intersects any monitor (e.g. a screen was
companionWindowH=560 ; unplugged) is ignored and the window falls back to default.
```

The window's size/position are remembered as you move and resize it (normal state
only — minimized/maximized aren't persisted) and restored next time it opens.

## Demo / test

`companion_demo.{cpp,sh}` open the real window off the game and screenshot it —
they load the cross-compiled test DLL, drive a scenario (a Testing session with the
settings menu open) through the actual callbacks, enable the window
(`MXBMRP3_Test_CompanionWindow`), then hold it up for capture.

```bash
tools/mxbmrp3_hud_window/companion_demo.sh out.png       # runs under Wine + Xvfb
```

Requires the headless toolchain (mingw-w64 posix + wine64) plus Xvfb + ImageMagick
(`import`) for the capture. See `.claude/hooks/session-start.sh` / DEVELOPMENT.md.

## Notes

- Assets are read from `plugins/mxbmrp3_data` relative to the game (bitmap fonts
  from `fonts/*.fnt`, the `.ttf` fallback from `web/fonts/*.ttf`, sprites from `textures/`
  and `icons/`) — the same files the game ships. `companion_demo.sh` stages them next
  to the DLL for the headless run.
- Text is drawn from the game's `.fnt` bitmap fonts. The PiBoSo `.fnt` layout: 4-byte
  `FNT\0` magic, a font name to offset 264, `int32` cell height at 264, then 256
  40-byte glyph records at 268 indexed by codepoint (`valid, xoffset, width,
  rightBearing, atlasX0/X1/Y0/Y1`; advance = xoffset+width+rightBearing), then at
  10508 the bitmap header (`w`, `h`, payload size, compression=2) and a raw-DEFLATE
  8-bit grayscale atlas. Decompressed once with miniz (`tinfl`) and cached. Because
  the atlas cell height maps 1:1 to a string's normalized size, the on-screen metrics
  are exact — no calibration constant needed (the `.ttf` fallback still uses `FONT_PX`).
- The vertical position of a glyph inside its cell is baked into the atlas by PiBoSo's
  `fontgen` from the source `.ttf`'s vertical metrics — there is **no** per-glyph
  y-offset field in `.fnt` and **no** offset/baseline key in `fontgen.cfg`. Most fonts
  bake near-centered (±4% of cell height); Tiny5 is an outlier (~+10%, ink low in the
  cell), which is why it can look vertically off on the number plate.

## Open points / known limitations

Tracked so they aren't forgotten:

- **Input targets the game window, not this one.** The plugin polls the OS cursor
  (`GetCursorPos`) and maps it into the *game* window's client rect (`input_manager.cpp`).
  So the settings UI in the companion window responds to the cursor, but when the game
  is also open the mapping is the game's, not this window's. Refinement: retarget the
  cursor mapping to the focused/companion window's rect so clicks are unambiguous with
  both open. (Fine today when the companion window is the only surface — e.g. the
  replay tool with no game running.)
- **Companion mode hides the in-game HUD — including the settings button.** In
  `HUD Display = Companion` the in-game HUD is suppressed except while the settings
  menu is open, so to switch back you reopen settings with the settings hotkey (the
  gear button is hidden). Could keep the settings button visible in that mode.
- **Asset root is cwd-relative.** `plugins/mxbmrp3_data` resolves relative to the
  host's working directory — parity with the plugin's own asset loading, so it works
  wherever the in-game HUD does. A host launched from a different cwd needs the
  game-root `cd` (`mxbmrp3_replay --window` does this). Could harden by anchoring the root
  to the DLL's own directory via `GetModuleFileName`.
- **No CI coverage.** The harness helpers the demo uses (`trackCenterline`/
  `TrackSegmentRow`, `showSettings`, `companionWindow`) are exercised only by
  `companion_demo.sh`, which CI doesn't run. Worth a headless lifecycle test
  (enable → assert the thread opens/closes cleanly) to keep that path green.
- **Headless capture of the replay-tool path.** Under Wine + Xvfb the window opens
  (`CompanionWindow: opened` in the log) but doesn't map to the X root for the
  `mxbmrp3_replay` path specifically — a Wine window-mapping quirk; `companion_demo`
  (identical code) maps fine. So that path can't be headless-screenshotted here; it
  maps normally on real Windows/Proton.
- **Closing the window falls back to In-game.** The window's X button switches
  `HUD Display` to **In-game** (persisted, via `consumeUserClosed()` reconciled on the
  game thread) so the HUD reappears in the game rather than vanishing — Companion mode
  otherwise suppresses the in-game HUD. Re-open it from the Appearance tab's HUD Display
  cycler.
