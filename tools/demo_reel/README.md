# Demo reel

Shoots the README demo (the Toggles strip's previews, a race, the Dash's Fuel
page, Settings changes picked from their dropdowns: a panel theme, a language,
fonts; a Standings column and opacity, a Gamepad switched on, the pad dragged
into place, then the Dash calling a lapper, a hazard and a PB) from a
Recorder tape, with only the mouse scripted. The plugin does everything else, as
it would in the game. Each beat is captioned with what it demonstrates.

Why bespoke: a screen recorder (ffmpeg `x11grab` over Xvfb, the first version)
caught Wine's window frame and dropped frames whenever the software renderer
fell behind, and nothing off the shelf drives the plugin's proprietary callbacks
from a tape. So the reel replays the tape itself and pulls each frame from the
companion window's renderer at a fixed 30 fps.

```
tools/demo_reel/make_demo.sh demo.mp4            # the made-up race
TAPE=my_race.tape tools/demo_reel/make_demo.sh demo.mp4   # a real tape
```

The script writes `demo.mp4`, `demo.gif` and `demo.srt` on a `#3d5a80` backdrop
(`BACKDROP=` changes the colour). Pass `--no-build` to reuse the test DLL.
`make_demo.sh`'s header lists the other switches.

**Layouts.** `LAYOUT=toc` (the default) renders the game at 1600x900 right of a
contents column: every beat listed, done ones ticked, the current one on a card
with its detail line (the theme or language picked) and a bar filling as it runs.
Nothing of the plugin can reach the column. `LAYOUT=caption` keeps the game full
size with one caption in a box the reel checks stays empty on every frame (the
capture is RGBA, so anything drawn under it, cursor included, fails the run).

| File | What it is |
|---|---|
| `tapegen.cpp` | Writes the made-up race as a tape: 8 riders, passes, a lapped backmarker, a crash |
| `hover_reel.cpp` | Plays a tape into the plugin and moves the mouse; the script is in `main()` |
| `captions_ass.py` | Turns the reel's `captions.tsv` into the burned-in captions (ASS, via ffmpeg's libass) and an SRT |
| `make_demo.sh` | Builds, stages the assets (language packs included), plays, composites |

**A real tape** comes from `[Recorder] enabled=1` in the plugin's INI (MX Bikes). For
every preview to show something, record 5 or more laps on a busy server, with
riders near you at times. The last 165 seconds of the tape play live under the
mouse script (130 with `REEL_STRIP=main`, the main previews only).

**Demo hooks.** The DLL needs `MXBMRP3_Test_CompanionCaptureRgba` (the companion
frame as RGBA), `MXBMRP3_Test_FriendsInject` (Steam friends, which a tape cannot
carry) and `MXBMRP3_Test_FakeGamepadInput` (a connected pad, fed from the rider's
inputs on the tape), all in `core/test_hooks_toggle_strip.cpp`. Test builds
only, so the shipped DLL never has them.
