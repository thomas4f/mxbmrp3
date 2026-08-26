# Replay tool - play a callback tape into a live plugin (real time)

Loads the real plugin `.dlo` and replays a recorded `.tape` tape into it at a
chosen speed. Distinct from the headless asserted replay in the test harness
(`PluginHost::replayTape()`): this one runs the **shipping** plugin so you can
watch its output - most usefully, **preview the web overlay against real data**
in a browser or OBS without launching the game.

Build the `mxbmrp3_replay` target (defined in
[`tools/CMakeLists.txt`](../CMakeLists.txt), x64) - from the generated
`build/msvc/mxbmrp3.sln`, or `cmake --build build/msvc --target mxbmrp3_replay`.
It's Windows-only (it `LoadLibrary`s a `.dlo`) and not part of the Linux
cross-build.

## Usage

```
mxbmrp3_replay.exe <plugin.dlo> <recording.tape> [options]

  --speed <N>   replay speed: 0 = max (no waiting), 1 = real-time, 10 = 10x
  --quiet       suppress the plugin's debug logs
  --web         serve the web overlay (see below)
  --window      render the HUD in the companion window (see below)
  --savepath <dir>  one folder for ALL plugin state: config + log + stats + settings
```

`<recording.tape>` tapes are produced by the in-plugin recorder (set
`[Recorder] enabled=1` in the plugin INI - MX Bikes only) under
`Documents\PiBoSo\<game>\mxbmrp3\tapes\`. (The `.tape` extension is by
convention; the tool validates the `MXBHREC` file magic, not the extension.)

## Interactive controls (any `--speed` > 0)

While a tape plays, the console takes single-key transport controls - built
for iterating on time-sensitive behavior (spotter audio, notices, director
cuts) without re-running whole tapes:

| Key | Action |
|---|---|
| `Space` | pause / resume (with `--window` the HUD stays live while paused) |
| `+` / `-` | double / halve speed, 0.125x..32x |
| `Right` or `.` | skip ahead 10s - skipped events still feed, just unpaced, so plugin state stays consistent |
| `Left` or `,` | skip back 10s |
| `R` | restart from the beginning |
| `Q` | quit with a clean shutdown |

Seeking **backwards** cannot rewind callback state, so it reloads the plugin
from scratch and re-feeds the tape up to the target at burst speed with output
suppressed - sub-second for typical tapes. Side effects of that reload: with
`--web` the HTTP server restarts (overlay pages reconnect on their own), and
the plugin's log/stats are re-opened. The replay clock starts at the tape's
first event rather than waiting out any pre-session dead air the recorder
captured. `--speed 0` (burst) stays non-interactive for batch/profiling use.

## Previewing the web overlay (`--web`)

The plugin serves the overlay from `plugins\mxbmrp3_data\web` and reads its
settings from `<savePath>\mxbmrp3\`, **both relative to the working directory** -
in-game that's the game root, but a tool launched from elsewhere (e.g. your build
folder) wouldn't find them. `--web` fixes that automatically:

- it derives the **game root** from the plugin's own path (a plugin lives in
  `<game>\plugins\`) and `cd`s there, so the *installed* overlay files resolve;
- it flips `webServer=1` on in `<game>\mxbmrp3\mxbmrp3_settings.ini` (idempotent -
  it preserves your other keys), so the plugin starts the server on `Startup`.

So, pointing at your installed plugin, from anywhere:

```
mxbmrp3_replay.exe "D:\...\MX Bikes\plugins\mxbmrp3.dlo" ^
  "C:\Users\You\Documents\PiBoSo\MX Bikes\mxbmrp3\tapes\session_XXXX.tape" ^
  --speed 5 --web
```

Then open **http://localhost:8080** in a browser or as an OBS Browser Source. The
overlay updates live over SSE as the tape replays.

- Port / network bind live under `[Advanced]` in that same
  `<game>\mxbmrp3\mxbmrp3_settings.ini` (`webServerPort`, `webServerBindAddress` -
  set `0.0.0.0` to reach it from another machine).
- The settings file `--web` touches is under the **game root**, separate from your
  real in-game settings in `Documents\PiBoSo\...`, so it won't disturb your setup.
- If the plugin isn't inside a `<game>\plugins\` folder, `--web` can't find the
  overlay files and says so (the replay still runs, just without the overlay).

## Watching in the companion window (`--window`)

The plugin's in-process **companion window** (an off-game HUD window you can drag to
a second screen - see `mxbmrp3/core/companion_window`) works during a tape replay:
`mxbmrp3_replay` drives the plugin's `Draw` for every recorded frame, so the window
renders the replay live. `--window` sets it up the same way `--web` does:

- derives the **game root** from the plugin path and `cd`s there, so the plugin's
  fonts/sprites (`plugins\mxbmrp3_data`) resolve;
- flips `enabled=1` under `[CompanionWindow]` in `<game>\mxbmrp3\mxbmrp3_settings.ini`
  (idempotent), so `Startup` opens the window.

```
mxbmrp3_replay.exe "D:\...\MX Bikes\plugins\mxbmrp3.dlo" session_XXXX.tape --speed 1 --window
```

The window opens and renders the HUD as the tape plays. As with `--web`, the plugin
must be inside a `<game>\plugins\` folder; otherwise it says so and the replay runs
without the window. `--web` and `--window` can be combined.

## The save directory (`--savepath`)

Everything the plugin reads and writes lives under **one** folder's `mxbmrp3\`
subdirectory - the config it loads at `Startup`, and the log, stats and settings it
writes. `--savepath <dir>` points that folder wherever you want; the tool prints the
resolved path (and the log location) on startup so you never have to hunt for it.

The tool is forgiving about what you pass: your PiBoSo **save folder**
(`...\MX Bikes`), its `mxbmrp3\` subfolder, or the `mxbmrp3_settings.ini` file all
resolve to the same save folder.

**Replay with your real config** - point it at your PiBoSo save folder, and the
plugin loads your exact look (colors, fonts, HUD layout, Charts settings):

```
mxbmrp3_replay.exe "D:\...\MX Bikes\plugins\mxbmrp3.dlo" ^
  "C:\Users\You\Documents\PiBoSo\MX Bikes\mxbmrp3\tapes\session_XXXX.tape" ^
  --window --savepath "C:\Users\You\Documents\PiBoSo\MX Bikes"
```

⚠️ Because it's one folder for reads *and* writes, pointing `--savepath` at your
real save folder means the replay also **writes** there - it updates your real
odometer/stats and settings as it plays. To replay with your look but leave your
real data untouched, copy your `mxbmrp3_settings.ini` into a scratch folder's
`mxbmrp3\` and point `--savepath` at the scratch folder instead.

Without `--savepath`, the save folder defaults to the **game root** (the working
directory after the asset `cd`), i.e. a scratch config showing defaults - the same
behavior as before this flag existed. Note the game root is separate: the plugin's
fonts/overlay files always load relative to the game install, exactly like in-game
(cwd = install, save path = your folder).

## Interacting and exiting

With `--window` (or `--web`), the tool keeps the plugin ticking after the tape
finishes: it pumps `Draw` at ~60 Hz so the companion window stays **interactive** -
move the mouse to get the cursor, open the settings menu, drag/configure HUDs - and
the web overlay stays served. Press **Ctrl+C** to shut down cleanly (this also
cancels a replay in progress, even mid-wait at real-time speed). A plain replay
(no `--window`/`--web`) still exits on its own after printing the timing stats.

## Notes

- `--speed` needs a value after it (`--speed 5`); a bare `--speed` is ignored and
  runs at 1x.
- This runs the shipping `.dlo`, so its analytics/Discord/etc. behave as in a real
  session (minus the game). Use `--quiet` to hide the plugin's own logs.

## What a tape must contain for spotter cues

A replay can only speak what the tape carries. Event cues (penalties, fastest
laps, pits) replay from any tape. The **proximity and hazard cues** ("rider
behind", "clear", blue flag, rider down) additionally need:

- **`RaceTrackPosition` events** - the detectors' input. The committed test
  fixtures (`race_farm14_24riders`, `race2_mxbclub_1lap`) are slimmed to the
  standings-only profile and have **none**, so they will never produce these
  cues. Record your own master in-game (`[Recorder] enabled=1` in the INI
  records everything) and replay that - or use the committed
  **`spotter_demo_weekend`** fixture, a ~3 minute synthetic race weekend
  (short qualifying, then a timed race) whose events derive from simulated
  motion and fire every MX Bikes-triggerable cue: session calls, proximity
  left/right/behind/clear, fastest laps, penalties with seconds, hazards,
  wrong-way, blue flag, backmarker traffic, leader changes, milestones,
  position reports, overtime with laps, the flags and the finishes; see
  `tests/integration/make_spotter_demo_tape.cpp` for the full timeline.
- **A player in the recording** (ride, don't spectate, while recording). The
  tape format has no spectate-selection event, so a while-spectating capture
  replays with no focused rider and the proximity/hazard detectors stand
  down. `tests/integration/tests/spotter_test.cpp` ("replaying a recorded
  tape") pins the working path.
