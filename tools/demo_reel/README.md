# Demo reel

Shoots the README demo (the Toggles strip's previews, a race, the Dash's Fuel
page, Settings changes and a Gamepad dragged into place) from a Recorder tape,
with only the mouse scripted. The plugin does everything else, as it would in
the game.

Why bespoke: a screen recorder (ffmpeg `x11grab` over Xvfb, the first version)
caught Wine's window frame and dropped frames whenever the software renderer
fell behind, and nothing off the shelf drives the plugin's proprietary callbacks
from a tape. So the reel replays the tape itself and pulls each frame from the
companion window's renderer at a fixed 30 fps.

```
tools/demo_reel/make_demo.sh demo.mp4            # the made-up race
TAPE=my_race.tape tools/demo_reel/make_demo.sh demo.mp4   # a real tape
```

The script writes `demo.mp4` and `demo.gif` on a `#3d5a80` backdrop
(`BACKDROP=` changes the colour). Pass `--no-build` to reuse the test DLL.

| File | What it is |
|---|---|
| `tapegen.cpp` | Writes the made-up race as a tape: 8 riders, passes, a lapped backmarker, a crash |
| `hover_reel.cpp` | Plays a tape into the plugin and moves the mouse; the script is in `main()` |
| `make_demo.sh` | Builds, stages the assets, plays, composites |

**A real tape** comes from `[Recorder] enabled=1` in the plugin's INI (MX Bikes). For
every preview to show something, record 5 or more laps on a busy server, with
riders near you at times. The last 116 seconds of the tape play live under the
mouse script.

**Demo hooks.** The DLL needs `MXBMRP3_Test_CompanionCaptureRgba` (the companion
frame as RGBA), `MXBMRP3_Test_FriendsInject` (Steam friends, which a tape cannot
carry) and `MXBMRP3_Test_FakeGamepadInput` (a connected pad, fed from the rider's
inputs on the tape), all in `core/test_hooks_toggle_strip.cpp`. Test builds
only, so the shipped DLL never has them.
