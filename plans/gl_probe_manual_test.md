# Phase 0 in-game checklist: the GL feasibility probe

> **This run is done. Phase 0 PASSED on 2026-08-30** - see the *Phase 0 result*
> section at the end of `plans/gl_in_context_renderer.md`. The checklist is kept
> as-is because the same steps re-run the probe on other hardware, other drivers
> and other window geometries, which is exactly what a one-machine result still
> needs.

Companion to `plans/gl_in_context_renderer.md`. That file is the agreed scope;
this one is the run sheet for the part no machine here can do.

Phase 0 asks one question: **is a GL context current on the thread that runs the
Draw callback, and is drawing there visually correct?** The probe is built,
cross-build green and headlessly tested. What follows is yours.

## Before you start

The probe is `[Advanced] glProbe` in `mxbmrp3_settings.ini`, off by default:

| value | what it does |
|---|---|
| `0` | off. Not one GL call, not even a module lookup. |
| `1` | **read-only**. `glGetString`/`glGetIntegerv` only: no draw, no state change, no error-queue drain. It cannot perturb the game. |
| `2` | report, plus one magenta bar drawn in the game's GL context with full state save/restore, a readback confirming it landed, and a before/after state diff confirming nothing leaked. Also adds one cyan bar drawn by the engine, as the reference below. |

Run `1` first. It is the half that answers the kill question, and it carries no
risk at all.

## Step 1 - is there a context? (`glProbe=1`)

Set the key, launch, and go **on track** - the plugin gets no callbacks in
menus, so the probe cannot speak there. Then:

```
findstr GlProbe "%USERPROFILE%\Documents\PiBoSo\MX Bikes\mxbmrp3_log.txt"
```

Send the whole block back. What decides the spike:

| The log says | Verdict |
|---|---|
| `NO GL CONTEXT is current on the Draw callback thread` | **Dead.** The plan's Phase 0 kill criterion. Nothing further is worth building at this hook point. |
| `opengl32.dll is NOT loaded in this process` | **Dead in this form.** The game is not an OpenGL app and the spike's premise is wrong. |
| `vendor=... version=... profile=compatibility` | **Alive.** Go to step 2. |

The verdict line is only printed after 120 consecutive contextless Draw calls,
so a game that issues one Draw before its context is current does not produce a
false kill. If you see it, it is real.

Two lines worth reading even on a pass:

- `draw framebuffer=0` is what we want - the default framebuffer, the one that
  gets swapped. Non-zero means the engine has an FBO bound and our draw lands
  somewhere whose fate we do not control.
- `viewport` against `game client`. A viewport that is not the client size says
  `Draw` sits inside some other pass.

`profile=core` is **not** a kill. It means a GL backend would need a shader
pipeline instead of fixed function - a bigger Phase 2, not a different answer.
The probe declines to draw in that case and says so.

## Step 2 - does it land, and in the right place? (`glProbe=2`)

Set `glProbe=2`, restart or hit the RELOAD_CONFIG hotkey, go on track.

**Run this at a NON-16:9 resolution** - ultrawide, or a 4:3 window. At 16:9 the
two candidate coordinate mappings are identical and the step-2 position check
cannot tell them apart.

You are looking at the **top-left corner** for **two stacked bars**:

- **magenta** (upper) - drawn by us, in the game's GL context.
- **cyan** (lower) - drawn by the engine from an ordinary plugin quad.

They are specified to be the same width and flush against each other. So:

| What you see | What it means |
|---|---|
| Two bars, same width, flush, no offset | **Phase 0 passes.** The context is there, our draw lands, and our coordinate mapping agrees with the engine's. |
| Two bars, but offset horizontally or different widths | Context and drawing are fine; the engine maps normalized coordinates differently than `UiViewport` does. Very much worth knowing - screenshot it, the offset is the measurement. |
| Cyan only | Our draw is not surviving to the present. Check the log's readback line - it distinguishes "never drew" from "drew and got buried" (below). |
| Neither | The engine handoff is broken, which is a different bug. Send the log. |

The log's `bars in viewport pixels` line gives both rectangles in pixels, so you
can check the screenshot against the numbers rather than by eye.

### If the magenta bar is missing

The readback line separates the two very different reasons:

- `readback ... probe color NOT found` - the draw did not reach the
  framebuffer at all.
- `readback ... PROBE QUAD IS IN THE FRAMEBUFFER` but nothing visible - **we
  drew, and the engine painted over us afterwards.**

The second is the interesting one, and per the plan it is *not* automatically a
kill. The question is what buries us. If the engine draws its own **UI** after
the callback, a HUD sitting under the game's menus may well be correct
behaviour, and the approach survives. If the **3D scene** is drawn after the
callback, the hook point is wrong, and the only remaining route is detouring
`wglSwapBuffers` - a substantially more invasive project that deserves a
deliberate decision rather than being drifted into. Tell me which it looks like
and I will write it up in the plan either way.

## Step 3 - did anything leak?

The line to want is:

```
GlProbe: state restored clean (N values compared, 0 differ, 0 GL errors)
```

If instead you get `state NOT clean`, send the indented lines under it verbatim.
Each names one piece of GL state that would corrupt the game's next draw, and
each is directly fixable. It should not happen - the same machinery runs clean
against a real driver under Wine in `gl_probe_test.cpp` - but that is one
driver, and yours is the one that counts.

Then **ride for a few laps** with `glProbe=2` on and watch the game itself:
colours, geometry, flicker, anything odd on track load. The plan is explicit
that a leaked state bit is found by riding, not by screenshot.

## When you are done

Set `glProbe=0`. It defaults to off and nothing ships enabled, but there is no
reason to leave two coloured bars on screen.

## What I need back

1. The full `GlProbe:` log block from step 1 and step 2.
2. A screenshot of the top-left corner at a non-16:9 resolution.
3. Whether the game itself looked wrong at any point.

That is the whole gate. Everything past it in `gl_in_context_renderer.md` waits
on those three.

---

# Round 2: z-order and the Phase 1 numbers, in one launch

Phase 0 passed. Two things remain, and they are deliberately batched into a
single session because your in-game time is the bottleneck in this loop.

**Order matters: do part A first.** It is a kill criterion. If in-context GL
renders *under* the game's own UI, part B would be measuring the performance of
something we could not ship.

## Part A - does the game's own UI bury us?

The risk in one sentence: our GL draw happens *during* the Draw callback, but
plugin primitives are drawn by the engine *after* it returns. If the game draws
its own UI in between, going in-context moves the HUD from above game UI to
below it.

1. **Turn ON MX Bikes' own native HUD** in the game's options. You almost
   certainly have it off, using mxbmrp3 instead - but with it off the game draws
   no on-track UI, and there is nothing for this test to stack against.
2. `glProbe=2`, and park the bars over one of those native elements:
   ```
   glProbeX=0.40      ; normalized HUD coords, 0..1 across the 16:9 UI rect
   glProbeY=0.80
   ```
   Adjust until the magenta/cyan pair overlaps a native element. RELOAD_CONFIG
   applies without a restart.
3. Screenshot the overlap. Three outcomes:

| What you see | Meaning |
|---|---|
| Both bars over the game element | Game UI draws before the callback. **No regression** - in-context GL is as high as today's HUD. |
| Cyan over it, **magenta under it** | The dangerous case: the game draws its UI between our GL draw and the engine's plugin primitives. In-context HUD would sit lower than today. Not necessarily fatal, but Phase 2 has to design around it. |
| Both under it | The game draws its UI last. Same relative position as today, since our HUD would be under it either way. |

## Part B - the numbers (now one button, not nine INI edits)

**This is the automated sweep, not a hand-run comparison.** The first draft of
this checklist asked you to edit the INI nine times and read frame times off the
HUD. That is exactly the process `core/render_probe_sweep.cpp`'s header records
failing here before: five reports came back internally perfect and all measuring
the same thing, because the probe had never engaged. Nothing errors when an
experiment does not happen. So the sweep now drives both sides itself.

1. Set `glProbe=2` (the GL rows draw nothing without it - see below).
2. Go on track, park somewhere with a stable view, and leave the bike still.
3. Settings -> **Performance** tab -> **Run sweep**.
4. Wait. It is about 90 seconds now and the frame rate drops while it runs.
   It writes `probe_sweep_<timestamp>.txt` next to the log.

Send me that file. It already contains the answer, differenced:

```
In-context GL vs engine (per untextured tiny quad):
  engine:              0.00xxx us/quad
  in-context, batched: 0.00xxx us/quad
  in-context, immediate (a FLOOR, not a backend): 0.00xxx us/quad
  batched GL recovers xx.x% of the engine's per-quad cost
  at 6000 quads: engine x.xxx ms, in-context x.xxx ms (budget 2.08 ms)
  => PASS by the plan's bar / => FAILS the plan's bar
```

**The KILL for this part**, straight from the plan: the report saying
`=> FAILS the plan's bar` - either GL recovers less than the majority of the
engine's per-quad cost, or its own cost pushes past 2.08 ms at realistic HUD
load. A marginal win does not justify a second renderer's permanent
maintenance. If it fails, we stop and write it up; that is a real outcome, not
a setback.

**And the anti-kill**, which matters just as much: if the report says

```
  *** GL ROWS MEASURED NOTHING.
```

then nothing was measured and the numbers are the cost of drawing zero quads -
which would otherwise read as "GL is free" and sail through the bar. Check
`glProbe=2` and re-run. Please do not send a report with that line as a result.

Ignore the `immediate` row except as a floor: `glBegin/glEnd` per quad is the
slowest thing GL can do, and no real backend would submit that way. The
`batched` row is the one the verdict uses.

Worth running the sweep **twice** - the bar says reproducibly, and two runs
minutes apart in the same scene is the cheapest way to see run-to-run noise.

## Part C - free, while you are in there

The probe now logs a **Draw heartbeat**: every time Draw resumes after a gap of
250 ms or more, with the gap length, plus every draw-state change (on track /
spectate / replay). This turns "the plugin gets no callbacks in menus" from an
inherited assumption into evidence on your build.

So just play normally for a few minutes with `glProbe=1` or higher, and visit:
ESC menu, pause, chat open, spectate, replay. Then send the `GlProbe: Draw
RESUMED` and `GlProbe: Draw state ->` lines. A visit that produces a gap means
no callbacks there; a visit that produces none means Draw kept firing.

## What to send back

1. Screenshot of the bars overlapping a native game UI element (part A).
2. The frame-time or fps numbers per cell (part B).
3. The `Draw RESUMED` / `Draw state ->` lines (part C).
4. `glProbe=0` when you are done.

---

# Round 3 addendum: two minutes on the D3D windows

`buildBatch` has been extracted out of the D3D11 backend into a shared
`render_batch` that the GL backend will consume, so the two cannot drift. The
move was mechanical - only the signature, the handle types and the cache lookups
changed, no arithmetic or ordering - and the extracted code now has 14 unit
tests it never had before.

But **the D3D backends have no headless coverage at all** (no device, no display
in CI), so nothing automated can confirm the companion window and the overlay
still render correctly. That needs eyes, and it is a two-minute look, not a
session. Please fold it into whatever you are next in-game for:

1. Open the **companion window** (Display target -> Companion or Both). The HUD
   should look exactly as before: panels, icons, text, the map ribbon.
2. Confirm `hwAccel=1` in `[Advanced]` (it is the default), then turn the
   **Overlay Renderer** on for a moment and check the in-game HUD still draws
   correctly.

What would count as a REGRESSION FROM THIS CHANGE specifically: text in the
wrong place or wrong size, icons untinted or wrongly tinted, sprites showing the
wrong art, elements vertically mirrored, or panels drawing in the wrong order
(labels behind their own backgrounds). Any of those, tell me and I will revert
the extraction rather than debug it in place.

If both windows look normal, nothing more is needed - say so and I will note it
in the plan.

---

# Round 4: the real thing. Turn the in-context GL renderer on.

Phase 2 is built. `core/hud_gl_renderer` draws the whole HUD inside the game's
own GL context and hands the engine nothing - no second window, so none of the
compositor, z-order, taskbar or focus behaviour the overlay renderer fought.

## Setup

Same as before (Auto-Save off, edit the INI with the game closed), and set:

```
glInGame=1
overlayInGame=0     ; leave the window overlay OFF - these are alternatives
glProbe=0           ; MUST be 0 - see below, this one is not just tidiness
```

**`glProbe=0` is required, not merely tidy.** The probe works by drawing TWO
bars - one by the engine, one by us into the GL context - stacked flush, so any
disagreement between the two coordinate mappings is visible at a glance. But
`glInGame=1` suppresses the engine's whole frame, and the engine's reference bar
is *in* that frame, so it would be drawn through the GL backend as well. The
comparison becomes GL against GL: it still looks like agreement and proves
nothing. The plugin now refuses the combination (`glProbe=2` wins, `glInGame`
stays off and logs why once), so setting both just means the feature you came to
test never runs.

**Expect a one-time hitch the first frame after enabling.** The GL backend loads
and decodes every font and sprite on first use, on the game thread. That is a
known cost, written down, and not what Round 4 is testing - please don't report
it as a defect. A *repeating* stutter, or one that happens on a later lap, is
worth reporting.

Then launch and go on track.

## What to look for, in this order

1. **Is the HUD there at all, and does it look right?** Every panel, icon,
   number and label, in the right place and the right colours. Compare against
   a screenshot with `glInGame=0` if anything looks off by a little.
2. **Ride for a few laps and watch the GAME, not the HUD.** This backend draws
   inside the game's own context, so a state leak corrupts the game's next
   draw, not ours. Anything odd - shifted colours, missing geometry, flicker,
   a wrong-looking sky or track - matters more than any HUD detail.
3. **Frame rate**, with a heavy HUD if you have one (standings + map + themes).
   That is where the measured saving lives; a light HUD will barely move.

## What would count as a KILL

- The HUD is **absent** while `glInGame=1` and the log says the backend was
  running. Suppression is supposed to follow what actually drew, so this would
  mean that contract is broken - which is the one failure mode this design must
  never have.
- The **game itself** renders wrong. Screenshot it and set `glInGame=0`.

## What would count as "not a kill, but tell me"

- The HUD looks subtly wrong - text misplaced, icons the wrong colour, sprites
  showing the wrong art, anything mirrored. That is a batching or texture bug,
  fixable, and the screenshot is what makes it fixable.
- The log carries `hudgl: in-context renderer unavailable (...)` or
  `hudgl: render failed (...)`. Both are handled - the engine keeps drawing -
  but the message names what went wrong and I want it.
- The log carries either `hudgl: the game had shader program N bound at Draw
  time` or the `glUseProgram could not be resolved` variant. Neither is a
  problem - the first says we handled it, the second says we stood down
  safely - but both answer a question no test on my side can: whether MX Bikes
  actually leaves a shader bound when it calls us. Each is logged once per
  session, so grep the whole log, not just the tail.

## The combination worth measuring: pluginThread + glInGame

`[Advanced] pluginThread=1` moves HUD BUILDING to a worker thread; `glInGame`
removes the ENGINE'S draw cost. They attack frame time from different
directions, so they ought to compose - but "ought to" is not a measurement, and
this is the only configuration in which the plugin has two threads that both
touch the GL backend's state.

Same track, same HUD, ~30 s each, back to back:

1. `pluginThread=1 glInGame=1` -> export report
2. `pluginThread=1 glInGame=0` -> export report

The `BENCH` line records `threaded=` next to `gl_ingame=`/`gl_drew=`, so all
four combinations are self-identifying and cannot be mixed up after the fact.

**Watch for the HUD going blank or flickering** under either. The GL render must
stay on the game thread (a context is per-thread and the worker has none), so if
the worker and the game thread ever disagree about which frame is current, that
is where it shows. Covered headlessly by `"gl in-game: works on the pluginThread
path too"`, but the harness is one machine and one driver.

## Also, while you are there (30 seconds)

The `buildBatch` extraction changed shipped D3D code, and CI cannot see the D3D
path. Open the **companion window** and confirm the HUD looks normal, then flip
the **Overlay Renderer** on briefly and confirm the same. Details and the
specific regression signatures are in the Round 3 addendum above.

## One number that would retire a guess: your real quad count

The plan's load table currently has an INFERRED figure in it - "your practice
profile, ~260 quads" - derived from your screenshots' 497 fps against the
sweep's 1.77 ms floor, not read from a counter. It is the one estimated number
in the whole Phase 1 write-up and it decides how the headline reads for a normal
user.

You can measure it in ten seconds. Turn on the **Benchmark** widget (Widgets
tab); its footer reads `Quads: N now, M peak`. Note N at your usual HUD, and
again with a heavy one (standings + map + themes) if you want the top of the
range.

Verified while writing this, so it is worth knowing: **that count stays
truthful with `glInGame=1`**. The benchmark captures it inside the draw handler
BEFORE the GL path zeroes the engine handoff, so it keeps reporting the HUD's
real size rather than the zero the engine ends up receiving. (In `pluginThread`
mode it is not updated at all, which is pre-existing and unrelated.)

## Send back

The log, a screenshot of the HUD with `glInGame=1`, the frame rate with and
without it at whatever HUD load you normally run, and the Benchmark widget's
quad count.
